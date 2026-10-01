// TextureCacheBuilder -- standalone, offline, out-of-process texture-upscale
// cache generator (2026-10-01). Direct request, following a real root-cause
// finding: this session traced a live "Exceeded limit of 4448 'image' assets"
// failure (and a cascade of related crashes) to this project's own
// proactive, in-game, FindOrLoadAsset-based texture-fetch pump -- it forces
// real, PERMANENT native image-pool registrations for every name in the
// bundled basemap regardless of what the player is actually near, with no
// safe way to individually release them (the native engine only supports
// bulk, per-zone release, not per-asset). Direct conclusion: "im thinking a
// prerequisite etxernal app for esgran caching" -- do the heavy lifting
// (Real-ESRGAN upscaling, DXT re-encoding, compression, cache-file writing)
// in a completely separate, standalone process that never touches
// iw5sp.exe's own memory, threads, or native asset pool at all, run ONCE
// before playing rather than live during a session.
//
// Architecture: reuses the exact same proven pipeline modules the live mod
// already ships (texture_upscale_ncnn.cpp, texture_upscale_dxt_codec.cpp,
// texture_upscale_iwi_writer.cpp, texture_upscale_compress.cpp,
// texture_upscale_cache.cpp), compiled as-is with zero source
// modifications -- their only external dependency across all five files is
// a single `LogFromController` function, implemented fresh here. Writes
// into the SAME <gameDir>\texture_upscale_cache\ directory the live mod
// already reads from, so the in-game substitution hooks need zero changes
// at all -- only the GENERATION side moves out-of-process.
//
// Deployment convention: this exe is meant to be dropped into the game's
// own install directory (same place d3d9.dll and the bundled Unlinker.exe
// already live), matching the existing in-game bulk-precache orchestrator's
// own real convention (TexturePrecacheOrchestrator -- GetModuleFileNameA-
// relative paths for everything: zone\english\, zone\dlc\,
// precache_tools\Unlinker.exe, texture_upscale_cache\, realesrgan_models\).
// Run once, as a real prerequisite step, before launching the game -- not
// injected into it, not triggered by it.

#include <windows.h>
#include <commctrl.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>

#include "../../proxy_d3d9/src/texture_upscale_ncnn.h"
#include "../../proxy_d3d9/src/texture_upscale_dxt_codec.h"
#include "../../proxy_d3d9/src/texture_upscale_iwi_writer.h"
#include "../../proxy_d3d9/src/texture_upscale_cache.h"

#pragma comment(lib, "comctl32.lib")

// ---------------------------------------------------------------------------
// LogFromController -- the one real external dependency shared by the five
// reused pipeline modules. Writes to a real log file AND posts to the GUI's
// own log box (ownership of the heap copy transfers to the window procedure,
// which frees it after appending).
// ---------------------------------------------------------------------------
namespace
{
    HWND g_mainWnd = nullptr;
    CRITICAL_SECTION g_logLock;
    FILE* g_logFile = nullptr;
    constexpr UINT WM_APP_LOG = WM_APP + 1;
    constexpr UINT WM_APP_PROGRESS = WM_APP + 2;
    constexpr UINT WM_APP_DONE = WM_APP + 3;
}

void LogFromController(const char* msg)
{
    EnterCriticalSection(&g_logLock);
    if (!g_logFile) fopen_s(&g_logFile, "texture_cache_builder.log", "a");
    if (g_logFile) {
        fprintf(g_logFile, "%s\n", msg);
        fflush(g_logFile);
    }
    LeaveCriticalSection(&g_logLock);

    if (g_mainWnd) {
        char* copy = _strdup(msg);
        if (copy) PostMessageA(g_mainWnd, WM_APP_LOG, 0, reinterpret_cast<LPARAM>(copy));
    }
}

namespace
{
    // ---- Format helpers (ported from texture_upscale_worker.cpp, same real
    // logic, zero behavioral change -- see that file's own header comments
    // for the full rationale on each). ----
    bool IwiFormatToBlockFormat(int iwiFormat, TextureUpscaleDxt::BlockFormat* out)
    {
        switch (static_cast<TextureUpscaleIwi::Format>(iwiFormat)) {
            case TextureUpscaleIwi::Format::DXT1: *out = TextureUpscaleDxt::BlockFormat::BC1; return true;
            case TextureUpscaleIwi::Format::DXT3: *out = TextureUpscaleDxt::BlockFormat::BC2; return true;
            case TextureUpscaleIwi::Format::DXT5: *out = TextureUpscaleDxt::BlockFormat::BC3; return true;
            default: return false;
        }
    }

    bool RawBitmapBytesPerPixel(int iwiFormat, int* outBpp)
    {
        switch (static_cast<TextureUpscaleIwi::Format>(iwiFormat)) {
            case TextureUpscaleIwi::Format::BitmapRGBA: *outBpp = 4; return true;
            case TextureUpscaleIwi::Format::BitmapRGB: *outBpp = 3; return true;
            default: return false;
        }
    }

    volatile LONG g_cancelRequested = 0;
    volatile LONG g_totalQueued = 0;
    volatile LONG g_totalProcessed = 0;
    volatile LONG g_totalCached = 0;
    volatile LONG g_totalSkippedExisting = 0;
    volatile LONG g_totalFailed = 0;

    void PostProgress()
    {
        if (g_mainWnd) PostMessageA(g_mainWnd, WM_APP_PROGRESS, 0, 0);
    }

    // Real IWI-v8 header parse + real source mip-count detection, same exact
    // algorithm as TextureUpscaleWorker::QueueUpscaleJobFromIwiFile (ported,
    // not reinvented -- see that function's own header comment in
    // texture_upscale_worker.cpp for the full "why match the source's real
    // count" rationale: shipping more or fewer mip levels than the source
    // texture actually has causes real native engine crashes/corruption,
    // live-confirmed this same session).
    bool ParseIwiHeader(const uint8_t* data, uint32_t size, int* outFormat,
                         uint16_t* outWidth, uint16_t* outHeight, int* outSourceMipCount,
                         const uint8_t** outBaseMipData, uint32_t* outBaseMipSize)
    {
        constexpr uint32_t kIwiHeaderSize = 0x20;
        if (!data || size <= kIwiHeaderSize) return false;
        if (data[0] != 'I' || data[1] != 'W' || data[2] != 'i' || data[3] != 8) return false;

        int rawFormat = static_cast<int8_t>(data[0x08]);
        uint16_t width = *reinterpret_cast<const uint16_t*>(data + 0x0A);
        uint16_t height = *reinterpret_cast<const uint16_t*>(data + 0x0C);
        if (width == 0 || height == 0) return false;

        TextureUpscaleDxt::BlockFormat blockFmt;
        int rawBpp = 0;
        uint32_t baseMipSize = 0;
        if (IwiFormatToBlockFormat(rawFormat, &blockFmt)) {
            baseMipSize = TextureUpscaleDxt::CompressedSize(width, height, blockFmt);
        } else if (RawBitmapBytesPerPixel(rawFormat, &rawBpp)) {
            baseMipSize = static_cast<uint32_t>(width) * height * static_cast<uint32_t>(rawBpp);
        } else {
            return false; // genuinely unidentified format, out of scope
        }
        if (size < kIwiHeaderSize + baseMipSize) return false;

        int sourceMipCount = 0;
        {
            const uint32_t realPayloadSize = size - kIwiHeaderSize;
            uint32_t running = 0;
            uint32_t lw = width, lh = height;
            for (int i = 0; i < 64; ++i) {
                uint32_t levelSize;
                if (IwiFormatToBlockFormat(rawFormat, &blockFmt)) {
                    levelSize = TextureUpscaleDxt::CompressedSize(lw, lh, blockFmt);
                } else {
                    levelSize = lw * lh * static_cast<uint32_t>(rawBpp);
                }
                if (running + levelSize > realPayloadSize) break;
                running += levelSize;
                ++sourceMipCount;
                if (running == realPayloadSize) break;
                if (lw <= 1 && lh <= 1) break;
                lw = lw > 1 ? lw / 2 : 1;
                lh = lh > 1 ? lh / 2 : 1;
            }
        }

        *outFormat = rawFormat;
        *outWidth = width;
        *outHeight = height;
        *outSourceMipCount = sourceMipCount;
        *outBaseMipData = data + (size - baseMipSize);
        *outBaseMipSize = baseMipSize;
        return true;
    }

    // Processes one real texture end to end: decode -> Real-ESRGAN upscale ->
    // box-filter to the target scale -> generate a real mip chain matching
    // the SOURCE's own real mip count -> DXT5 re-encode each level ->
    // EncodeIwi8 -> compress -> store. Same real pipeline as
    // TextureUpscaleWorker::ProcessJob (ported, not reinvented), minus the
    // live-session-specific concerns (no memory-pressure gate needed -- this
    // process owns the whole machine while it runs; no queue/thread-pool
    // plumbing -- the caller already parallelizes across files).
    bool ProcessOneTexture(const char* assetName, int rawFormat, uint16_t width, uint16_t height,
                            int sourceMipCount, const uint8_t* baseMipData, uint32_t baseMipSize,
                            int scaleMultiplier)
    {
        TextureUpscaleDxt::BlockFormat decodeFmt;
        int rawBpp = 0;
        uint8_t* rgba = nullptr;
        if (IwiFormatToBlockFormat(rawFormat, &decodeFmt)) {
            rgba = TextureUpscaleDxt::DecodeToRGBA(baseMipData, width, height, decodeFmt);
        } else if (RawBitmapBytesPerPixel(rawFormat, &rawBpp)) {
            uint64_t pixelCount = static_cast<uint64_t>(width) * height;
            rgba = static_cast<uint8_t*>(malloc(pixelCount * 4));
            if (rgba) {
                if (rawBpp == 4) {
                    for (uint64_t i = 0; i < pixelCount; ++i) {
                        rgba[i * 4 + 0] = baseMipData[i * 4 + 2]; // BGRA -> RGBA
                        rgba[i * 4 + 1] = baseMipData[i * 4 + 1];
                        rgba[i * 4 + 2] = baseMipData[i * 4 + 0];
                        rgba[i * 4 + 3] = baseMipData[i * 4 + 3];
                    }
                } else { // rawBpp == 3
                    for (uint64_t i = 0; i < pixelCount; ++i) {
                        rgba[i * 4 + 0] = baseMipData[i * 3 + 2];
                        rgba[i * 4 + 1] = baseMipData[i * 3 + 1];
                        rgba[i * 4 + 2] = baseMipData[i * 3 + 0];
                        rgba[i * 4 + 3] = 255;
                    }
                }
            }
        }
        if (!rgba) {
            char buf[300];
            sprintf_s(buf, "[builder] '%.200s': decode failed", assetName);
            LogFromController(buf);
            return false;
        }

        uint32_t netOutW = 0, netOutH = 0;
        uint8_t* netOut = TextureUpscaleNcnn::UpscaleRGBA4x(rgba, width, height, &netOutW, &netOutH);
        free(rgba);
        if (!netOut) {
            char buf[300];
            sprintf_s(buf, "[builder] '%.200s': UpscaleRGBA4x failed", assetName);
            LogFromController(buf);
            return false;
        }

        uint32_t finalW = static_cast<uint32_t>(width) * static_cast<uint32_t>(scaleMultiplier);
        uint32_t finalH = static_cast<uint32_t>(height) * static_cast<uint32_t>(scaleMultiplier);
        uint8_t* finalRgba = netOut;
        bool freeFinalRgba = false;
        if (scaleMultiplier != 4) {
            uint32_t boxX = netOutW / finalW, boxY = netOutH / finalH;
            uint8_t* boxed = static_cast<uint8_t*>(malloc(static_cast<size_t>(finalW) * finalH * 4));
            if (boxed) {
                for (uint32_t y = 0; y < finalH; ++y) {
                    for (uint32_t x = 0; x < finalW; ++x) {
                        uint32_t sums[4] = {0, 0, 0, 0};
                        uint32_t sampleCount = boxX * boxY;
                        for (uint32_t by = 0; by < boxY; ++by) {
                            for (uint32_t bx = 0; bx < boxX; ++bx) {
                                uint32_t srcX = x * boxX + bx, srcY = y * boxY + by;
                                const uint8_t* p = netOut + (static_cast<size_t>(srcY) * netOutW + srcX) * 4;
                                sums[0] += p[0]; sums[1] += p[1]; sums[2] += p[2]; sums[3] += p[3];
                            }
                        }
                        uint8_t* dst = boxed + (static_cast<size_t>(y) * finalW + x) * 4;
                        dst[0] = static_cast<uint8_t>(sums[0] / sampleCount);
                        dst[1] = static_cast<uint8_t>(sums[1] / sampleCount);
                        dst[2] = static_cast<uint8_t>(sums[2] / sampleCount);
                        dst[3] = static_cast<uint8_t>(sums[3] / sampleCount);
                    }
                }
                finalRgba = boxed;
                freeFinalRgba = true;
            }
        }
        const uint32_t actualW = (freeFinalRgba || scaleMultiplier == 4) ? finalW : netOutW;
        const uint32_t actualH = (freeFinalRgba || scaleMultiplier == 4) ? finalH : netOutH;

        // Real mip chain matching the SOURCE's own real mip count -- see
        // ParseIwiHeader's own header comment for why. kMaxMipLevels is a
        // generous array-sizing bound (not a format limit).
        constexpr int kMaxMipLevels = 16;
        TextureUpscaleIwi::MipLevel mips[kMaxMipLevels];
        uint8_t* mipsOwned[kMaxMipLevels] = {};
        int mipCount = 0;
        uint32_t curW = actualW, curH = actualH;
        const uint8_t* curRgba = finalRgba;
        uint8_t* curOwnedRgba = nullptr;

        for (; mipCount < kMaxMipLevels; ++mipCount) {
            uint32_t compSize = 0;
            uint8_t* comp = TextureUpscaleDxt::EncodeFromRGBA(curRgba, curW, curH,
                TextureUpscaleDxt::BlockFormat::BC3, &compSize);
            if (!comp) break;
            mips[mipCount].data = comp;
            mips[mipCount].size = compSize;
            mipsOwned[mipCount] = comp;

            bool reachedKnownSourceCount = sourceMipCount > 0 && mipCount + 1 >= sourceMipCount;
            bool reachedFullPyramid = curW <= 1 && curH <= 1;
            if (mipCount + 1 >= kMaxMipLevels || reachedKnownSourceCount || reachedFullPyramid) {
                ++mipCount;
                break;
            }

            const uint32_t nextW = curW > 1 ? curW / 2 : 1;
            const uint32_t nextH = curH > 1 ? curH / 2 : 1;
            uint8_t* half = static_cast<uint8_t*>(malloc(static_cast<size_t>(nextW) * nextH * 4));
            if (!half) { ++mipCount; break; }
            for (uint32_t y = 0; y < nextH; ++y) {
                for (uint32_t x = 0; x < nextW; ++x) {
                    uint32_t sx0 = x * 2, sx1 = (sx0 + 1 < curW) ? sx0 + 1 : sx0;
                    uint32_t sy0 = y * 2, sy1 = (sy0 + 1 < curH) ? sy0 + 1 : sy0;
                    const uint8_t* p0 = curRgba + (static_cast<size_t>(sy0) * curW + sx0) * 4;
                    const uint8_t* p1 = curRgba + (static_cast<size_t>(sy0) * curW + sx1) * 4;
                    const uint8_t* p2 = curRgba + (static_cast<size_t>(sy1) * curW + sx0) * 4;
                    const uint8_t* p3 = curRgba + (static_cast<size_t>(sy1) * curW + sx1) * 4;
                    uint8_t* d = half + (static_cast<size_t>(y) * nextW + x) * 4;
                    d[0] = static_cast<uint8_t>((p0[0] + p1[0] + p2[0] + p3[0]) / 4);
                    d[1] = static_cast<uint8_t>((p0[1] + p1[1] + p2[1] + p3[1]) / 4);
                    d[2] = static_cast<uint8_t>((p0[2] + p1[2] + p2[2] + p3[2]) / 4);
                    d[3] = static_cast<uint8_t>((p0[3] + p1[3] + p2[3] + p3[3]) / 4);
                }
            }
            if (curOwnedRgba) free(curOwnedRgba);
            curRgba = half;
            curOwnedRgba = half;
            curW = nextW; curH = nextH;
        }
        if (curOwnedRgba) free(curOwnedRgba);
        free(netOut);
        if (freeFinalRgba) free(finalRgba);

        if (mipCount == 0) {
            char buf[300];
            sprintf_s(buf, "[builder] '%.200s': EncodeFromRGBA failed", assetName);
            LogFromController(buf);
            return false;
        }

        uint32_t iwiSize = 0;
        uint8_t* iwi = TextureUpscaleIwi::EncodeIwi8(static_cast<uint16_t>(actualW), static_cast<uint16_t>(actualH), 1,
            TextureUpscaleIwi::Format::DXT5, mips, mipCount, &iwiSize);
        for (int i = 0; i < mipCount; ++i) free(mipsOwned[i]);
        if (!iwi) {
            char buf[300];
            sprintf_s(buf, "[builder] '%.200s': EncodeIwi8 failed", assetName);
            LogFromController(buf);
            return false;
        }

        bool stored = TextureUpscaleCache::StoreUpscaledIwi(assetName, scaleMultiplier, iwi, iwiSize);
        free(iwi);

        char buf[300];
        if (stored) {
            sprintf_s(buf, "[builder] '%.200s': %ux%u -> %ux%u cached (%u bytes, %d mip levels)",
                assetName, width, height, actualW, actualH, iwiSize, mipCount);
            LogFromController(buf);
            InterlockedIncrement(&g_totalCached);
        } else {
            sprintf_s(buf, "[builder] '%.200s': StoreUpscaledIwi failed", assetName);
            LogFromController(buf);
        }
        return stored;
    }

    void GetExeDir(char* out, size_t outSize)
    {
        GetModuleFileNameA(nullptr, out, static_cast<DWORD>(outSize));
        char* lastSlash = strrchr(out, '\\');
        if (lastSlash) *(lastSlash + 1) = '\0';
    }

    // Recursively walks a directory for real .iwi files extracted by
    // Unlinker, processing each one through the full pipeline above, then
    // deletes it (bounds the scratch dump's own real disk footprint to
    // roughly one zone at a time). Same real approach as
    // TexturePrecacheOrchestrator::WalkAndQueueIwiFiles, adapted to call the
    // pipeline directly instead of queueing to a live-session worker pool.
    int WalkAndProcessIwiFiles(const char* dirPath, int scaleMultiplier)
    {
        int processedCount = 0;
        char searchPattern[MAX_PATH];
        sprintf_s(searchPattern, "%s\\*", dirPath);
        WIN32_FIND_DATAA findData;
        HANDLE h = FindFirstFileA(searchPattern, &findData);
        if (h == INVALID_HANDLE_VALUE) return 0;
        do {
            if (g_cancelRequested) break;
            if (strcmp(findData.cFileName, ".") == 0 || strcmp(findData.cFileName, "..") == 0) continue;
            char fullPath[MAX_PATH];
            sprintf_s(fullPath, "%s\\%s", dirPath, findData.cFileName);
            if (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                processedCount += WalkAndProcessIwiFiles(fullPath, scaleMultiplier);
                RemoveDirectoryA(fullPath);
                continue;
            }
            const char* ext = strrchr(findData.cFileName, '.');
            if (!ext || _stricmp(ext, ".iwi") != 0) continue;

            char assetName[256];
            strncpy_s(assetName, findData.cFileName, _TRUNCATE);
            char* dot = strrchr(assetName, '.');
            if (dot) *dot = '\0';

            if (TextureUpscaleCache::CacheEntryExists(assetName, scaleMultiplier)) {
                InterlockedIncrement(&g_totalSkippedExisting);
                DeleteFileA(fullPath);
                InterlockedIncrement(&g_totalProcessed);
                PostProgress();
                continue;
            }

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
                            int format = 0, sourceMipCount = 0;
                            uint16_t width = 0, height = 0;
                            const uint8_t* baseMipData = nullptr;
                            uint32_t baseMipSize = 0;
                            if (ParseIwiHeader(buf, static_cast<uint32_t>(size), &format, &width, &height,
                                                &sourceMipCount, &baseMipData, &baseMipSize)) {
                                bool ok = ProcessOneTexture(assetName, format, width, height, sourceMipCount,
                                                             baseMipData, baseMipSize, scaleMultiplier);
                                if (ok) ++processedCount; else InterlockedIncrement(&g_totalFailed);
                            }
                        }
                        free(buf);
                    }
                }
                fclose(f);
            }
            DeleteFileA(fullPath);
            InterlockedIncrement(&g_totalProcessed);
            PostProgress();
        } while (FindNextFileA(h, &findData));
        FindClose(h);
        return processedCount;
    }

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
        if (h == INVALID_HANDLE_VALUE) return;
        do {
            if (g_zoneCount >= kMaxZones) break;
            if (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
            sprintf_s(g_zonePaths[g_zoneCount], "%s\\%s", dirPath, findData.cFileName);
            strncpy_s(g_zoneNames[g_zoneCount], findData.cFileName, _TRUNCATE);
            char* dot = strrchr(g_zoneNames[g_zoneCount], '.');
            if (dot) *dot = '\0';
            ++g_zoneCount;
        } while (FindNextFileA(h, &findData));
        FindClose(h);
    }

    bool RunUnlinkerForZone(const char* unlinkerPath, const char* zonePath, const char* outputFolderPattern)
    {
        char cmdLine[2048];
        sprintf_s(cmdLine, "\"%s\" --include-assets image --image-format IWI --output-folder \"%s\" \"%s\"",
            unlinkerPath, outputFolderPattern, zonePath);

        STARTUPINFOA si{};
        si.cb = sizeof(si);
        si.dwFlags = STARTF_USESHOWWINDOW;
        si.wShowWindow = SW_HIDE;
        PROCESS_INFORMATION pi{};
        if (!CreateProcessA(nullptr, cmdLine, nullptr, nullptr, FALSE,
                             CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
            return false;
        }
        constexpr DWORD kZoneTimeoutMs = 5 * 60 * 1000;
        DWORD waitResult = WaitForSingleObject(pi.hProcess, kZoneTimeoutMs);
        bool completed = (waitResult == WAIT_OBJECT_0);
        if (!completed) TerminateProcess(pi.hProcess, 1);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        return completed;
    }

    int g_scaleMultiplier = 4;

    DWORD WINAPI BuildThreadProc(LPVOID)
    {
        LogFromController("[builder] TextureCacheBuilder starting.");

        if (!TextureUpscaleCache::EnsureCacheDir()) {
            LogFromController("[builder] FATAL: could not create/access texture_upscale_cache\\ -- "
                "is this exe running from inside the real game install directory?");
            PostMessageA(g_mainWnd, WM_APP_DONE, 0, 0);
            return 0;
        }

        LogFromController("[builder] loading Real-ESRGAN model...");
        if (!TextureUpscaleNcnn::EnsureModelLoaded()) {
            LogFromController("[builder] FATAL: Real-ESRGAN model failed to load -- is realesrgan_models\\ "
                "present next to this exe?");
            PostMessageA(g_mainWnd, WM_APP_DONE, 0, 0);
            return 0;
        }
        LogFromController("[builder] model loaded.");

        char exeDir[MAX_PATH];
        GetExeDir(exeDir, sizeof(exeDir));

        char unlinkerPath[MAX_PATH];
        sprintf_s(unlinkerPath, "%sprecache_tools\\Unlinker.exe", exeDir);
        if (GetFileAttributesA(unlinkerPath) == INVALID_FILE_ATTRIBUTES) {
            LogFromController("[builder] FATAL: precache_tools\\Unlinker.exe not found next to this exe.");
            PostMessageA(g_mainWnd, WM_APP_DONE, 0, 0);
            return 0;
        }

        char englishZoneDir[MAX_PATH], dlcZoneDir[MAX_PATH];
        sprintf_s(englishZoneDir, "%szone\\english", exeDir);
        sprintf_s(dlcZoneDir, "%szone\\dlc", exeDir);
        AddZonesFromDir(englishZoneDir);
        AddZonesFromDir(dlcZoneDir);

        if (g_zoneCount == 0) {
            LogFromController("[builder] FATAL: no zone files found under zone\\english\\ or zone\\dlc\\ -- "
                "is this exe running from inside the real game install directory?");
            PostMessageA(g_mainWnd, WM_APP_DONE, 0, 0);
            return 0;
        }

        char buf[256];
        sprintf_s(buf, "[builder] %d zone file(s) found, scale=%dx. Starting.", g_zoneCount, g_scaleMultiplier);
        LogFromController(buf);
        g_totalQueued = g_zoneCount;
        PostProgress();

        char scratchRoot[MAX_PATH];
        sprintf_s(scratchRoot, "%sbuilder_scratch", exeDir);
        CreateDirectoryA(scratchRoot, nullptr);

        for (int i = 0; i < g_zoneCount && !g_cancelRequested; ++i) {
            char zoneScratchDir[MAX_PATH];
            sprintf_s(zoneScratchDir, "%s\\%s", scratchRoot, g_zoneNames[i]);
            char outputPattern[MAX_PATH];
            sprintf_s(outputPattern, "%s\\?game?\\?zone?", zoneScratchDir);

            char zbuf[300];
            sprintf_s(zbuf, "[builder] zone %d/%d: %s", i + 1, g_zoneCount, g_zoneNames[i]);
            LogFromController(zbuf);

            bool ranOk = RunUnlinkerForZone(unlinkerPath, g_zonePaths[i], outputPattern);
            if (!ranOk) {
                sprintf_s(zbuf, "[builder] zone %d/%d: Unlinker timed out/failed to run", i + 1, g_zoneCount);
                LogFromController(zbuf);
            }
            int processedThisZone = WalkAndProcessIwiFiles(zoneScratchDir, g_scaleMultiplier);
            RemoveDirectoryA(zoneScratchDir);

            sprintf_s(zbuf, "[builder] zone %d/%d complete: %d texture(s) newly cached this zone "
                "(%ld total cached, %ld already up to date, %ld failed so far)",
                i + 1, g_zoneCount, processedThisZone, g_totalCached, g_totalSkippedExisting, g_totalFailed);
            LogFromController(zbuf);
        }
        RemoveDirectoryA(scratchRoot);

        sprintf_s(buf, "[builder] DONE. %ld newly cached, %ld already up to date, %ld failed.%s",
            g_totalCached, g_totalSkippedExisting, g_totalFailed,
            g_cancelRequested ? " (cancelled)" : "");
        LogFromController(buf);

        PostMessageA(g_mainWnd, WM_APP_DONE, 0, 0);
        return 0;
    }

    // ---- GUI ----
    HWND g_logEdit, g_progressBar, g_statusLabel, g_startButton, g_scaleCombo;

    void AppendLog(const char* msg)
    {
        int len = GetWindowTextLengthA(g_logEdit);
        SendMessageA(g_logEdit, EM_SETSEL, len, len);
        SendMessageA(g_logEdit, EM_REPLACESEL, FALSE, reinterpret_cast<LPARAM>(msg));
        SendMessageA(g_logEdit, EM_REPLACESEL, FALSE, reinterpret_cast<LPARAM>("\r\n"));
        SendMessageA(g_logEdit, WM_VSCROLL, SB_BOTTOM, 0);
    }

    void UpdateProgressUi()
    {
        LONG queued = g_totalQueued, processed = g_totalProcessed;
        if (queued > 0) {
            SendMessageA(g_progressBar, PBM_SETRANGE32, 0, queued);
            SendMessageA(g_progressBar, PBM_SETPOS, processed, 0);
        }
        char buf[300];
        sprintf_s(buf, "Zones processed: %ld / %ld  |  Cached: %ld  |  Already up to date: %ld  |  Failed: %ld",
            processed, queued, g_totalCached, g_totalSkippedExisting, g_totalFailed);
        SetWindowTextA(g_statusLabel, buf);
    }

    LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        switch (msg) {
        case WM_CREATE: {
            CreateWindowExA(0, "STATIC", "Scale:", WS_CHILD | WS_VISIBLE,
                10, 12, 50, 20, hwnd, nullptr, nullptr, nullptr);
            g_scaleCombo = CreateWindowExA(0, "COMBOBOX", "", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST,
                65, 10, 80, 100, hwnd, reinterpret_cast<HMENU>(100), nullptr, nullptr);
            SendMessageA(g_scaleCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>("2x"));
            SendMessageA(g_scaleCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>("3x"));
            SendMessageA(g_scaleCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>("4x"));
            SendMessageA(g_scaleCombo, CB_SETCURSEL, 2, 0); // default 4x

            g_startButton = CreateWindowExA(0, "BUTTON", "Start",
                WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 160, 8, 100, 26,
                hwnd, reinterpret_cast<HMENU>(101), nullptr, nullptr);

            g_statusLabel = CreateWindowExA(0, "STATIC", "Ready.", WS_CHILD | WS_VISIBLE,
                10, 44, 760, 20, hwnd, nullptr, nullptr, nullptr);

            g_progressBar = CreateWindowExA(0, PROGRESS_CLASSA, "", WS_CHILD | WS_VISIBLE,
                10, 68, 760, 20, hwnd, nullptr, nullptr, nullptr);

            g_logEdit = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", "",
                WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL,
                10, 96, 760, 400, hwnd, nullptr, nullptr, nullptr);
            HFONT font = CreateFontA(14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                DEFAULT_QUALITY, FIXED_PITCH, "Consolas");
            SendMessageA(g_logEdit, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
            return 0;
        }
        case WM_COMMAND:
            if (LOWORD(wParam) == 101) { // Start/Cancel button, toggled by its own text
                char btnText[32];
                GetWindowTextA(g_startButton, btnText, sizeof(btnText));
                if (strcmp(btnText, "Start") == 0) {
                    int sel = static_cast<int>(SendMessageA(g_scaleCombo, CB_GETCURSEL, 0, 0));
                    g_scaleMultiplier = (sel == 0) ? 2 : (sel == 1) ? 3 : 4;
                    g_cancelRequested = 0;
                    g_totalQueued = g_totalProcessed = g_totalCached = g_totalSkippedExisting = g_totalFailed = 0;
                    SetWindowTextA(g_startButton, "Cancel");
                    EnableWindow(g_scaleCombo, FALSE);
                    CreateThread(nullptr, 0, BuildThreadProc, nullptr, 0, nullptr);
                } else {
                    g_cancelRequested = 1;
                    AppendLog("[builder] cancel requested -- finishing current zone...");
                }
            }
            return 0;
        case WM_APP_LOG: {
            char* text = reinterpret_cast<char*>(lParam);
            AppendLog(text);
            free(text);
            return 0;
        }
        case WM_APP_PROGRESS:
            UpdateProgressUi();
            return 0;
        case WM_APP_DONE:
            SetWindowTextA(g_startButton, "Start");
            EnableWindow(g_scaleCombo, TRUE);
            UpdateProgressUi();
            return 0;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        }
        return DefWindowProcA(hwnd, msg, wParam, lParam);
    }
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int nCmdShow)
{
    InitializeCriticalSection(&g_logLock);

    INITCOMMONCONTROLSEX icc{ sizeof(icc), ICC_PROGRESS_CLASS };
    InitCommonControlsEx(&icc);

    WNDCLASSA wc{};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = "TextureCacheBuilderWnd";
    wc.hCursor = LoadCursorA(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
    RegisterClassA(&wc);

    g_mainWnd = CreateWindowExA(0, "TextureCacheBuilderWnd",
        "MW32011NCP Texture Cache Builder (offline, out-of-process ESRGAN cache generator)",
        WS_OVERLAPPEDWINDOW & ~WS_MAXIMIZEBOX & ~WS_THICKFRAME,
        CW_USEDEFAULT, CW_USEDEFAULT, 800, 540,
        nullptr, nullptr, hInstance, nullptr);

    AppendLog("Run this from inside the real game install directory (same folder as d3d9.dll). "
        "It writes into texture_upscale_cache\\, the same folder the live mod reads from.");
    AppendLog("Requires precache_tools\\Unlinker.exe and realesrgan_models\\ next to this exe.");

    ShowWindow(g_mainWnd, nCmdShow);
    UpdateWindow(g_mainWnd);

    MSG msg;
    while (GetMessageA(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }
    return 0;
}
