#include "frame_benchmark.h"

#include <windows.h>
#include <cstdio>

#include "mod_config.h"

extern void LogFromController(const char* msg); // defined in dllmain.cpp

namespace {

double g_rumbleMsThisFrame = 0.0;
double g_assetCaptureMsThisFrame = 0.0;
double g_realCreateTextureMsThisFrame = 0.0;
int g_createTextureCallsThisFrame = 0;

// Dedup set for the "this specific texture size/format took a while" log line --
// fixed-size, non-STL (this file ships in the real proxy_d3d9.dll), same "log
// once per distinct thing, not every occurrence" standard as issue #67's own
// lineage of fixes elsewhere in this project.
constexpr int kMaxLoggedSlowTextures = 64;
struct SlowTextureKey { UINT width, height; DWORD format; };
SlowTextureKey g_loggedSlowTextures[kMaxLoggedSlowTextures];
int g_loggedSlowTextureCount = 0;

FILE* g_csvFile = nullptr;
bool g_triedOpen = false;
LARGE_INTEGER g_qpcFrequency{};
bool g_qpcInit = false;
LARGE_INTEGER g_lastFrameTime{};
bool g_haveLastFrameTime = false;
unsigned long g_frameIndex = 0;

// Opened lazily (first LogFrame call with the flag on), not at DLL attach --
// matches this project's established "only touch disk when the feature is
// actually in use" standard. Truncates on open (mode "w", same fix/rationale as
// dllmain.cpp's own proxy_d3d9.log change earlier the same day) -- this is a
// fresh benchmark run every time the flag turns on, not a growing historical log.
void EnsureCsvOpen()
{
    if (g_triedOpen) return;
    g_triedOpen = true;
    char path[MAX_PATH];
    GetModuleFileNameA(nullptr, path, MAX_PATH);
    char* lastSlash = strrchr(path, '\\');
    if (lastSlash) *(lastSlash + 1) = '\0';
    strcat_s(path, "frametime_benchmark.csv");
    fopen_s(&g_csvFile, path, "w");
    if (g_csvFile) {
        fprintf(g_csvFile, "frameIndex,frameTimeMs,optionsMenuMs,overlayMessageMs,glyphIconMs,hintSlotsMs,rumbleMs,assetCaptureMs,realCreateTextureMs,createTextureCalls,ourOwnTotalMs\n");
        fflush(g_csvFile); // header line always visible even if the process is
                            // killed hard before the first periodic flush below.
        LogFromController("[frame-benchmark] frametime_benchmark.csv opened -- logging every frame until FrametimeBenchmarkLogging is turned back off");
    } else {
        LogFromController("[frame-benchmark] FAILED to open frametime_benchmark.csv -- benchmark logging disabled this session");
    }
}

} // namespace

void FrameBenchmark_AddRumbleMs(double ms)
{
    if (!g_modConfig.frametimeBenchmarkLogging) return;
    g_rumbleMsThisFrame += ms;
}

void FrameBenchmark_AddAssetCaptureMs(double ms)
{
    if (!g_modConfig.frametimeBenchmarkLogging) return;
    g_assetCaptureMsThisFrame += ms;
}

void FrameBenchmark_AddRealCreateTextureCall(double ms, UINT width, UINT height, DWORD format)
{
    if (!g_modConfig.frametimeBenchmarkLogging) return;
    g_realCreateTextureMsThisFrame += ms;
    ++g_createTextureCallsThisFrame;

    // Log the FIRST time a given (width,height,format) is seen taking a while, so
    // a real repeated-recreation culprit can be identified by its actual
    // dimensions instead of just "something is slow" -- 2ms is well above what a
    // cached/already-resident texture creation should cost. Deduped by
    // (width,height,format) so a genuinely repeated offender (the actual
    // regression pattern being chased -- "post-glyphs-update, alternates every
    // frame") logs once, not every single occurrence.
    if (ms <= 2.0) return;
    for (int i = 0; i < g_loggedSlowTextureCount; ++i) {
        if (g_loggedSlowTextures[i].width == width && g_loggedSlowTextures[i].height == height &&
            g_loggedSlowTextures[i].format == format) {
            return; // already logged this exact (width,height,format) once
        }
    }
    if (g_loggedSlowTextureCount < kMaxLoggedSlowTextures) {
        g_loggedSlowTextures[g_loggedSlowTextureCount++] = { width, height, format };
    }
    char buf[192];
    sprintf_s(buf, "[frame-benchmark] real CreateTexture(%ux%u, format=%lu) took %.3fms -- "
                    "check frametime_benchmark.csv's createTextureCalls/realCreateTextureMs columns",
        width, height, format, ms);
    LogFromController(buf);
}

void FrameBenchmark_LogFrame(double optionsMenuMs, double overlayMessageMs,
                              double glyphIconMs, double hintSlotsMs)
{
    if (!g_modConfig.frametimeBenchmarkLogging) {
        // Still reset the cross-file accumulators even when off, in case the
        // flag was just turned off mid-session -- keeps state clean if it's
        // turned back on later without a full relaunch (config hot-reloads).
        g_rumbleMsThisFrame = 0.0;
        g_assetCaptureMsThisFrame = 0.0;
        g_realCreateTextureMsThisFrame = 0.0;
        g_createTextureCallsThisFrame = 0;
        return;
    }

    EnsureCsvOpen();
    if (!g_csvFile) return;

    if (!g_qpcInit) {
        QueryPerformanceFrequency(&g_qpcFrequency);
        g_qpcInit = true;
    }
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);

    double frameTimeMs = 0.0;
    if (g_haveLastFrameTime) {
        frameTimeMs = (static_cast<double>(now.QuadPart - g_lastFrameTime.QuadPart) * 1000.0) /
                      static_cast<double>(g_qpcFrequency.QuadPart);
    }
    g_lastFrameTime = now;
    g_haveLastFrameTime = true;

    double rumbleMs = g_rumbleMsThisFrame;
    double assetCaptureMs = g_assetCaptureMsThisFrame;
    double realCreateTextureMs = g_realCreateTextureMsThisFrame;
    int createTextureCalls = g_createTextureCallsThisFrame;
    g_rumbleMsThisFrame = 0.0;
    g_assetCaptureMsThisFrame = 0.0;
    g_realCreateTextureMsThisFrame = 0.0;
    g_createTextureCallsThisFrame = 0;

    // realCreateTextureMs deliberately NOT included in ourOwnTotalMs -- it's real
    // engine/driver texture-creation cost, not overhead this project's own hook
    // code added (see this function's own header comment). Kept as its own
    // column specifically so it can be compared against frameTimeMs directly to
    // test whether repeated real texture creation (ours or the engine's) is what
    // a felt stutter actually lines up with.
    double ourOwnTotalMs = optionsMenuMs + overlayMessageMs + glyphIconMs + hintSlotsMs + rumbleMs + assetCaptureMs;

    ++g_frameIndex;
    // First row has no meaningful frameTimeMs (no previous frame to diff against)
    // -- still written (as 0) so frameIndex stays a simple running count, not
    // worth a special-case skip for one row out of a whole session's worth.
    fprintf(g_csvFile, "%lu,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%d,%.4f\n",
        g_frameIndex, frameTimeMs, optionsMenuMs, overlayMessageMs, glyphIconMs,
        hintSlotsMs, rumbleMs, assetCaptureMs, realCreateTextureMs, createTextureCalls, ourOwnTotalMs);

    // 2026-08-17, third pass -- live data from the first two passes ruled out this
    // project's own instrumented code (never correlated with the real spikes) and
    // real texture creation (also never correlated), but frametime_benchmark.csv
    // has no shared correlation point with proxy_d3d9.log -- there was no way to
    // tell what the ENGINE was actually doing during an unexplained slow frame.
    // This closes that gap: any frame slower than 20ms (well above the 16.7ms/60fps
    // line, chosen so this only fires on the genuinely rare frames -- ~0.06-0.28%
    // of a session per the first two runs, not a volume risk) gets a marker written
    // to proxy_d3d9.log too, at the real moment it happened, so the next capture
    // can be read side-by-side with whatever else was logging around it (bind-
    // resolver, menu-focus-track, level-load-zone-hook, etc.) instead of guessing
    // from the CSV numbers alone.
    if (frameTimeMs > 20.0) {
        char slowBuf[192];
        sprintf_s(slowBuf, "[frame-benchmark] SLOW FRAME #%lu: %.2fms (ourOwnTotalMs=%.3f realCreateTextureMs=%.3f calls=%d)",
            g_frameIndex, frameTimeMs, ourOwnTotalMs, realCreateTextureMs, createTextureCalls);
        LogFromController(slowBuf);
    }

    // Flushed every frame, deliberately, NOT throttled like proxy_d3d9.log's own
    // 1-second interval -- this file's whole purpose is being handed back for
    // analysis after a real stutter is felt, and an unflushed tail lost to a
    // hard process kill (e.g. force-closing the game right after feeling a
    // hitch) would be exactly the data most likely to matter. The per-call
    // fflush cost is accepted here on the same "diagnostic tool, not shipped
    // default behavior" basis as every other DEFAULT-OFF toggle in this
    // project's [Experimental] section -- never pays this cost unless the user
    // explicitly turned this on to chase this exact problem.
    fflush(g_csvFile);
}
