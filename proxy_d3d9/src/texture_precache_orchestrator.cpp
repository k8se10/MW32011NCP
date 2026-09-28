#include "texture_precache_orchestrator.h"

#include <windows.h>
#include <cstdio>
#include <cstring>
#include <cstdlib>

#include "mod_config.h"
#include "texture_upscale_worker.h"
#include "overlay_hud.h"

extern void LogFromController(const char* msg); // defined in dllmain.cpp

namespace TexturePrecacheOrchestrator
{
namespace
{
    bool g_started = false;

    void GetExeDir(char* out, size_t outSize)
    {
        GetModuleFileNameA(nullptr, out, static_cast<DWORD>(outSize));
        char* lastSlash = strrchr(out, '\\');
        if (lastSlash) *(lastSlash + 1) = '\0';
    }

    // Marker file -- real completeness record, keyed on the mod version and
    // the configured scale, so a mod update or a TextureRenderRes change
    // both naturally invalidate it and trigger a fresh pass. Deliberately a
    // plain, human-readable text file (not a binary format) -- this is a
    // one-line, infrequently-touched record, no reason to make it opaque.
    void GetMarkerPath(char* out, size_t outSize)
    {
        char exeDir[MAX_PATH];
        GetExeDir(exeDir, sizeof(exeDir));
        sprintf_s(out, outSize, "%stexture_upscale_cache\\precache_marker.txt", exeDir);
    }

    bool MarkerIsCurrent()
    {
        char markerPath[MAX_PATH];
        GetMarkerPath(markerPath, sizeof(markerPath));
        FILE* f = nullptr;
        if (fopen_s(&f, markerPath, "r") != 0 || !f) return false; // no marker
            // yet -- real, expected first-run case, not an error.
        char line[256] = {};
        fgets(line, sizeof(line), f);
        fclose(f);

        char expected[256];
        sprintf_s(expected, "%s|%dx", kModVersionString, g_modConfig.textureRenderRes);
        // Trim trailing newline before comparing.
        size_t len = strlen(line);
        while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) line[--len] = '\0';
        return strcmp(line, expected) == 0;
    }

    void WriteMarker()
    {
        char exeDir[MAX_PATH];
        GetExeDir(exeDir, sizeof(exeDir));
        char cacheDir[MAX_PATH];
        sprintf_s(cacheDir, "%stexture_upscale_cache", exeDir);
        CreateDirectoryA(cacheDir, nullptr); // ignore failure -- ERROR_ALREADY_EXISTS
            // is the common/expected case, matching TextureUpscaleCache's own convention.

        char markerPath[MAX_PATH];
        GetMarkerPath(markerPath, sizeof(markerPath));
        FILE* f = nullptr;
        if (fopen_s(&f, markerPath, "w") == 0 && f) {
            fprintf(f, "%s|%dx\n", kModVersionString, g_modConfig.textureRenderRes);
            fclose(f);
        }
    }

    // Real fixed-capacity zone list -- 256 covers this game's own real zone
    // count with generous headroom (153 real files observed live in
    // zone\english\ alone, per texture_upscale_cache_research.md). Fixed-
    // size, non-STL, matching this project's own established shipped-code
    // convention -- this orchestrator's own background thread is the only
    // reader/writer, no locking needed.
    constexpr int kMaxZones = 256;
    char g_zonePaths[kMaxZones][MAX_PATH];
    char g_zoneNames[kMaxZones][128];
    int g_zoneCount = 0;

    void AddZonesFromDir(const char* dirPath)
    {
        char searchPattern[MAX_PATH];
        sprintf_s(searchPattern, "%s\\*.ff", dirPath);
        WIN32_FIND_DATAA findData;
        HANDLE h = FindFirstFileA(searchPattern, &findData);
        if (h == INVALID_HANDLE_VALUE) return; // dir doesn't exist or is
            // empty -- real, expected (e.g. no dlc\ content) not an error.
        do {
            if (g_zoneCount >= kMaxZones) break; // degrade gracefully -- see
                // this array's own header comment.
            if (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
            sprintf_s(g_zonePaths[g_zoneCount], "%s\\%s", dirPath, findData.cFileName);
            strncpy_s(g_zoneNames[g_zoneCount], findData.cFileName, _TRUNCATE);
            char* dot = strrchr(g_zoneNames[g_zoneCount], '.');
            if (dot) *dot = '\0'; // strip ".ff" for the real Unlinker
                // ?zone? output-folder placeholder and for logging.
            ++g_zoneCount;
        } while (FindNextFileA(h, &findData));
        FindClose(h);
    }

    // Recursively walks a directory for real .iwi files, queueing each one
    // via TextureUpscaleWorker::QueueUpscaleJobFromIwiFile, then deletes the
    // file once queued (or once determined unusable) to keep the scratch
    // dump's real disk footprint bounded to roughly one zone at a time
    // rather than accumulating a second full copy of every extracted
    // texture for the whole pass.
    int WalkAndQueueIwiFiles(const char* dirPath, int scaleMultiplier)
    {
        int queuedCount = 0;
        char searchPattern[MAX_PATH];
        sprintf_s(searchPattern, "%s\\*", dirPath);
        WIN32_FIND_DATAA findData;
        HANDLE h = FindFirstFileA(searchPattern, &findData);
        if (h == INVALID_HANDLE_VALUE) return 0;
        do {
            if (strcmp(findData.cFileName, ".") == 0 || strcmp(findData.cFileName, "..") == 0) continue;
            char fullPath[MAX_PATH];
            sprintf_s(fullPath, "%s\\%s", dirPath, findData.cFileName);
            if (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                queuedCount += WalkAndQueueIwiFiles(fullPath, scaleMultiplier);
                RemoveDirectoryA(fullPath); // best-effort cleanup, ignore failure
                continue;
            }
            const char* ext = strrchr(findData.cFileName, '.');
            if (!ext || _stricmp(ext, ".iwi") != 0) continue;

            FILE* f = nullptr;
            if (fopen_s(&f, fullPath, "rb") == 0 && f) {
                fseek(f, 0, SEEK_END);
                long size = ftell(f);
                fseek(f, 0, SEEK_SET);
                if (size > 0) {
                    uint8_t* buf = static_cast<uint8_t*>(malloc(static_cast<size_t>(size)));
                    if (buf) {
                        size_t readBytes = fread(buf, 1, static_cast<size_t>(size), f);
                        if (readBytes == static_cast<size_t>(size)) {
                            char assetName[256];
                            strncpy_s(assetName, findData.cFileName, _TRUNCATE);
                            char* dot = strrchr(assetName, '.');
                            if (dot) *dot = '\0'; // real engine asset name, no ".iwi"
                            // Bounded retry-with-backoff for a real "queue
                            // temporarily full" condition (2026-09-28) --
                            // this orchestrator is a much higher-volume
                            // producer than organic per-load capture, and
                            // can easily outpace TextureUpscaleWorker's own
                            // bounded queue. Skips the retry entirely when
                            // the name is already in flight (a real,
                            // unretryable, and genuinely common case -- the
                            // same shared texture appears across many real
                            // zones) rather than wasting real wall-clock
                            // time on a doomed retry loop.
                            bool queued = false;
                            if (!TextureUpscaleWorker::IsNameInFlight(assetName)) {
                                for (int attempt = 0; attempt < 20 && !queued; ++attempt) {
                                    if (attempt > 0) Sleep(250);
                                    queued = TextureUpscaleWorker::QueueUpscaleJobFromIwiFile(
                                        assetName, buf, static_cast<uint32_t>(size), scaleMultiplier);
                                }
                            }
                            if (queued) ++queuedCount;
                        }
                        free(buf);
                    }
                }
                fclose(f);
            }
            DeleteFileA(fullPath);
        } while (FindNextFileA(h, &findData));
        FindClose(h);
        return queuedCount;
    }

    // Runs Unlinker.exe as a real subprocess against one zone file,
    // image-only mode, waiting up to a real timeout -- a hung or crashed
    // Unlinker instance must never block the whole pass indefinitely.
    // Returns true if the process completed (regardless of its own exit
    // code -- a zone that fails to load still gets its own real error
    // logged by Unlinker itself; this just confirms the subprocess didn't
    // hang), false if it had to be force-killed on timeout.
    bool RunUnlinkerForZone(const char* unlinkerPath, const char* zonePath, const char* outputFolderPattern)
    {
        char cmdLine[2048];
        sprintf_s(cmdLine, "\"%s\" --include-assets image --image-format IWI --output-folder \"%s\" \"%s\"",
            unlinkerPath, outputFolderPattern, zonePath);

        STARTUPINFOA si{};
        si.cb = sizeof(si);
        // Real, deliberate: no window, no inherited stdio -- this runs
        // silently in the background, its own real per-zone success/failure
        // is confirmed by whether .iwi files land in its output folder, not
        // by parsing its console output.
        si.dwFlags = STARTF_USESHOWWINDOW;
        si.wShowWindow = SW_HIDE;
        PROCESS_INFORMATION pi{};

        if (!CreateProcessA(nullptr, cmdLine, nullptr, nullptr, FALSE,
                             CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
            return false;
        }

        constexpr DWORD kZoneTimeoutMs = 5 * 60 * 1000; // 5 minutes -- generous
            // real headroom over any single zone this project has observed
            // taking to load/dump, while still bounding a genuinely hung
            // instance (e.g. an infinite loop in a not-yet-fixed parser bug).
        DWORD waitResult = WaitForSingleObject(pi.hProcess, kZoneTimeoutMs);
        bool completed = (waitResult == WAIT_OBJECT_0);
        if (!completed) {
            TerminateProcess(pi.hProcess, 1); // real, necessary force-kill --
                // a hung subprocess must never block the whole precache pass.
        }
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        return completed;
    }

    DWORD WINAPI PrecacheThreadProc(LPVOID)
    {
        char exeDir[MAX_PATH];
        GetExeDir(exeDir, sizeof(exeDir));

        char unlinkerPath[MAX_PATH];
        sprintf_s(unlinkerPath, "%sprecache_tools\\Unlinker.exe", exeDir);
        if (GetFileAttributesA(unlinkerPath) == INVALID_FILE_ATTRIBUTES) {
            LogFromController("[texture-precache] Unlinker.exe not found -- bulk pre-cache unavailable this session "
                "(dev machine without tools/iw5oat built, or a release package missing precache_tools\\). "
                "The runtime capture path (organic per-load caching) still works normally.");
            return 0;
        }

        // Real, separate game install directory -- zones live at
        // <gameDir>\zone\english\ and \zone\dlc\ (confirmed live,
        // texture_upscale_cache_research.md). This mod's own deployed DLL
        // already lives in <gameDir>, so exeDir IS the game install root.
        char englishZoneDir[MAX_PATH], dlcZoneDir[MAX_PATH];
        sprintf_s(englishZoneDir, "%szone\\english", exeDir);
        sprintf_s(dlcZoneDir, "%szone\\dlc", exeDir);
        AddZonesFromDir(englishZoneDir);
        AddZonesFromDir(dlcZoneDir);

        if (g_zoneCount == 0) {
            LogFromController("[texture-precache] no zone files found -- bulk pre-cache skipped");
            return 0;
        }

        char scratchRoot[MAX_PATH];
        sprintf_s(scratchRoot, "%sprecache_scratch", exeDir);
        CreateDirectoryA(scratchRoot, nullptr);

        char startBuf[256];
        sprintf_s(startBuf, "[NCP] First-run texture pre-cache starting: %d zones to scan...", g_zoneCount);
        ShowOverlayMessage(startBuf, 5000);
        char logBuf[300];
        sprintf_s(logBuf, "[texture-precache] starting bulk pre-cache pass: %d real zone files found", g_zoneCount);
        LogFromController(logBuf);

        int zonesSucceeded = 0, zonesFailed = 0, totalQueued = 0;
        DWORD lastProgressMs = GetTickCount();

        for (int i = 0; i < g_zoneCount; ++i) {
            char zoneScratchDir[MAX_PATH];
            sprintf_s(zoneScratchDir, "%s\\%s", scratchRoot, g_zoneNames[i]);
            char outputPattern[MAX_PATH];
            sprintf_s(outputPattern, "%s\\?game?\\?zone?", zoneScratchDir);

            bool ranOk = RunUnlinkerForZone(unlinkerPath, g_zonePaths[i], outputPattern);
            if (ranOk) ++zonesSucceeded; else ++zonesFailed; // "ranOk" only
                // means the subprocess didn't hang -- a zone that fails to
                // LOAD (a real, currently-known limitation for many zones,
                // see this file's own header comment) still counts as
                // "ranOk" here; WalkAndQueueIwiFiles below simply finds zero
                // .iwi files for it, which is the real, honest outcome.

            int queuedThisZone = WalkAndQueueIwiFiles(zoneScratchDir, g_modConfig.textureRenderRes);
            totalQueued += queuedThisZone;
            RemoveDirectoryA(zoneScratchDir); // best-effort, ignore failure --
                // WalkAndQueueIwiFiles already removed every file/subdir it
                // found; this only cleans up the now-empty zone dir itself.

            DWORD nowMs = GetTickCount();
            if (nowMs - lastProgressMs > 8000 || i == g_zoneCount - 1) { // periodic,
                // not every zone -- a zone with zero images completes near-
                // instantly, and this project's own log-volume lesson
                // (issue #87) applies to overlay messages too.
                lastProgressMs = nowMs;
                char progBuf[300];
                sprintf_s(progBuf, "[NCP] Pre-caching: zone %d/%d, %d textures queued so far",
                    i + 1, g_zoneCount, totalQueued);
                ShowOverlayMessage(progBuf, 9000);
            }
        }

        RemoveDirectoryA(scratchRoot); // best-effort, only succeeds if genuinely empty

        char doneBuf[300];
        sprintf_s(doneBuf, "[texture-precache] pass complete: %d/%d zones loaded, %d real textures queued for background upscaling",
            zonesSucceeded, g_zoneCount, totalQueued);
        LogFromController(doneBuf);
        sprintf_s(doneBuf, "[NCP] Pre-cache scan complete: %d textures queued (upscaling continues in the background)", totalQueued);
        ShowOverlayMessage(doneBuf, 6000);

        WriteMarker();
        return 0;
    }
}

void EnsurePrecacheStarted()
{
    if (g_started) return;
    g_started = true;

    if (g_modConfig.textureRenderRes <= 1) return; // upscaling itself
        // disabled -- nothing to precache, matches every other gate in this
        // feature.
    if (!g_modConfig.bulkTexturePrecacheEnabled) {
        LogFromController("[texture-precache] BulkTexturePrecache disabled in config -- skipping bulk pass "
            "(the runtime capture path still caches textures organically as they're loaded)");
        return;
    }
    if (MarkerIsCurrent()) {
        LogFromController("[texture-precache] marker up to date for this mod version/scale -- bulk pass already complete");
        return;
    }

    CreateThread(nullptr, 0, PrecacheThreadProc, nullptr, 0, nullptr);
}

}
