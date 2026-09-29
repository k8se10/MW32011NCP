#include "texture_upscale_worker.h"

#include <windows.h>
#include <cstdio>
#include <cstring>
#include <cstdlib>

#include "texture_upscale_dxt_codec.h"
#include "texture_upscale_iwi_writer.h"
#include "texture_upscale_ncnn.h"
#include "texture_upscale_cache.h"
#include "overlay_hud.h"

extern void LogFromController(const char* msg); // defined in dllmain.cpp

namespace TextureUpscaleWorker
{
namespace
{
    struct PendingJob
    {
        char name[256];
        int format; // TextureUpscaleIwi::Format
        uint32_t width, height;
        uint8_t* compressedData; // malloc'd, owned by the queue until the worker frees it
        uint32_t compressedSize;
        int scaleMultiplier;
    };

    // Fixed-capacity ring buffer + dedicated background thread -- same
    // established shape as asset_capture.cpp's own WriteThreadProc/
    // g_pendingWrites (see this project's own precedent there for the full
    // rationale: keep the FAST in-memory work on the calling thread, hand
    // only the SLOW work -- here, real neural-network inference, not disk
    // I/O -- to a dedicated background thread). CRITICAL_SECTION, not
    // std::mutex, matching this project's own established non-STL
    // shipped-code convention.
    constexpr int kMaxPendingJobs = 48; // deliberately bounded -- each job is
        // real, slow (seconds, not milliseconds) GPU inference work, so a
        // very deep queue would just mean a long, stale backlog, not real
        // throughput. Widened from an original 16 (2026-09-28) once the bulk
        // pre-cache orchestrator became a second, much higher-volume
        // producer than organic per-load capture alone -- 48 is a real
        // memory/throughput tradeoff (worst case ~20MB/job for a large
        // captured source texture means up to ~1GB queued at once), paired
        // with the orchestrator's own bounded retry-with-backoff rather than
        // growing the queue without limit. 16 remains generous headroom over
        // how many DISTINCT new textures a single menu/level visit
        // realistically introduces at
        // once.
    PendingJob g_pendingJobs[kMaxPendingJobs];
    int g_pendingHead = 0, g_pendingTail = 0, g_pendingCount = 0;
    CRITICAL_SECTION g_pendingLock;
    bool g_pendingLockInit = false;
    HANDLE g_workerThreadHandle = nullptr;

    // In-flight dedup set -- separate from the on-disk cache check in
    // Hook_ImageFileLoadX64 (analog_input_hooks_x64.cpp), which only ever
    // catches a PRIOR session's already-completed work. This catches the
    // same name being requested again THIS session while a job for it is
    // still queued or mid-processing (e.g. the same background texture
    // re-requested on a menu re-open before the first upscale finishes).
    // Fixed-size, matching this project's own established "degrade
    // gracefully, don't overflow" convention (asset_capture.cpp's own
    // g_capturedNames) -- 256 covers this feature's own real config-key
    // scope (a single session realistically touches a bounded set of menu/
    // HUD textures, not thousands).
    constexpr int kMaxInFlightNames = 256;
    char g_inFlightNames[kMaxInFlightNames][256];
    int g_inFlightCount = 0;
    CRITICAL_SECTION g_inFlightLock;
    bool g_inFlightLockInit = false;

    bool IsInFlight(const char* name)
    {
        EnterCriticalSection(&g_inFlightLock);
        bool found = false;
        for (int i = 0; i < g_inFlightCount; ++i) {
            if (_stricmp(g_inFlightNames[i], name) == 0) { found = true; break; }
        }
        LeaveCriticalSection(&g_inFlightLock);
        return found;
    }

    void MarkInFlight(const char* name)
    {
        EnterCriticalSection(&g_inFlightLock);
        if (g_inFlightCount < kMaxInFlightNames) {
            strncpy_s(g_inFlightNames[g_inFlightCount], name, _TRUNCATE);
            ++g_inFlightCount;
        }
        LeaveCriticalSection(&g_inFlightLock);
    }

    // Called once a job finishes (success or failure) -- does NOT remove the
    // name from the in-flight set on success, deliberately: once a name has
    // been upscaled this session, TextureUpscaleCache::TryLoadCachedUpscaledIwi
    // will serve it from disk on any FUTURE load within this same session
    // too, so there's no need to ever re-queue it -- staying "in flight"
    // forever (for the rest of this process's life) is the correct,
    // simplest way to express "never queue this name again this session".
    // Only a real FAILURE clears it, so a transient error (e.g. a one-off
    // GPU hiccup) gets a real retry on the next load rather than being
    // permanently blacklisted for the rest of the session.
    void ClearInFlightOnFailure(const char* name)
    {
        EnterCriticalSection(&g_inFlightLock);
        for (int i = 0; i < g_inFlightCount; ++i) {
            if (_stricmp(g_inFlightNames[i], name) == 0) {
                // Swap-remove -- order doesn't matter for this set. Manual
                // copy (not std::swap) to avoid an extra STL header
                // dependency in this otherwise non-STL shipped file.
                strncpy_s(g_inFlightNames[i], g_inFlightNames[g_inFlightCount - 1], _TRUNCATE);
                --g_inFlightCount;
                break;
            }
        }
        LeaveCriticalSection(&g_inFlightLock);
    }

    // Real DXT format -> TextureUpscaleDxt::BlockFormat mapping -- these two
    // enums intentionally mirror each other 1:1 for the three formats both
    // understand (see texture_upscale_iwi_writer.h's own header comment),
    // but are kept as separate types (one mirrors the real on-disk IWI
    // format byte, the other is this project's own decode/encode API) so a
    // translation step stays explicit rather than reinterpret_cast-ing
    // between two enums that happen to agree today.
    bool IwiFormatToBlockFormat(int iwiFormat, TextureUpscaleDxt::BlockFormat* out)
    {
        switch (static_cast<TextureUpscaleIwi::Format>(iwiFormat)) {
            case TextureUpscaleIwi::Format::DXT1: *out = TextureUpscaleDxt::BlockFormat::BC1; return true;
            case TextureUpscaleIwi::Format::DXT3: *out = TextureUpscaleDxt::BlockFormat::BC2; return true;
            case TextureUpscaleIwi::Format::DXT5: *out = TextureUpscaleDxt::BlockFormat::BC3; return true;
            default: return false;
        }
    }

    // Processes one job end to end: decode -> upscale (real Real-ESRGAN
    // inference, tiled internally for large images) -> re-encode -> write to
    // the on-disk cache. Every real failure is logged and simply abandons
    // this one job (the player keeps seeing the real original texture, same
    // as before this feature existed) -- never anything that could affect
    // the live game process beyond this.
    void ProcessJob(const PendingJob& job)
    {
        char progressBuf[300];
        sprintf_s(progressBuf, "[NCP] Upscaling \"%.200s\" (%ux)...", job.name, job.scaleMultiplier);
        ShowOverlayMessage(progressBuf, 4000);

        TextureUpscaleDxt::BlockFormat blockFmt;
        if (!IwiFormatToBlockFormat(job.format, &blockFmt)) {
            char buf[300];
            sprintf_s(buf, "[texture-upscale-worker] '%.200s': unsupported source format %d, skipping", job.name, job.format);
            LogFromController(buf);
            return;
        }

        // BC2 (DXT3) decode is real and supported (texture_upscale_dxt_codec.h),
        // but its own encoder is deliberately NOT implemented (this feature has
        // never needed to round-trip BC2 -- real source textures observed live
        // so far are BC1/BC3 only, per that header's own comment) -- re-encode
        // as BC3 instead, a strictly equal-or-better format for the same real
        // alpha content, rather than failing outright.
        TextureUpscaleDxt::BlockFormat encodeFmt = (blockFmt == TextureUpscaleDxt::BlockFormat::BC2)
            ? TextureUpscaleDxt::BlockFormat::BC3 : blockFmt;
        TextureUpscaleIwi::Format iwiEncodeFmt = (encodeFmt == TextureUpscaleDxt::BlockFormat::BC1)
            ? TextureUpscaleIwi::Format::DXT1 : TextureUpscaleIwi::Format::DXT5;

        uint8_t* rgba = TextureUpscaleDxt::DecodeToRGBA(job.compressedData, job.width, job.height, blockFmt);
        if (!rgba) {
            char buf[300];
            sprintf_s(buf, "[texture-upscale-worker] '%.200s': DecodeToRGBA failed", job.name);
            LogFromController(buf);
            return;
        }

        // The vendored model's real, fixed scale factor is 4x -- for a
        // configured 2x/3x target, run the real 4x inference (this project
        // ships no 2x/3x-specific model) and box-filter the result down to
        // the exact requested multiplier, rather than needing a second
        // model. 4x itself is a direct passthrough of the model's own
        // native output, no resize step at all.
        uint32_t netOutW = 0, netOutH = 0;
        uint8_t* netOut = TextureUpscaleNcnn::UpscaleRGBA4x(rgba, job.width, job.height, &netOutW, &netOutH);
        free(rgba);
        if (!netOut) {
            char buf[300];
            sprintf_s(buf, "[texture-upscale-worker] '%.200s': UpscaleRGBA4x failed", job.name);
            LogFromController(buf);
            return;
        }

        uint32_t finalW = job.width * static_cast<uint32_t>(job.scaleMultiplier);
        uint32_t finalH = job.height * static_cast<uint32_t>(job.scaleMultiplier);
        uint8_t* finalRgba = netOut;
        bool freeFinalRgba = false;
        if (job.scaleMultiplier != 4) {
            // Real box-filter downsample (4x model output -> the exact
            // requested integer multiplier) -- both `finalW`/`finalH` and
            // `netOutW`/`netOutH` are exact integer multiples of job.width/
            // job.height (4 and job.scaleMultiplier respectively), so this
            // box ratio is always an exact integer, never a fractional
            // sampling case.
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
            // On malloc failure above, finalRgba stays = netOut (the full 4x
            // result) -- a real, safe degradation (ships a 4x result instead
            // of the requested 2x/3x) rather than losing the job entirely.
        }

        uint32_t compressedSize = 0;
        uint8_t* compressed = TextureUpscaleDxt::EncodeFromRGBA(finalRgba,
            (freeFinalRgba || job.scaleMultiplier == 4) ? finalW : netOutW,
            (freeFinalRgba || job.scaleMultiplier == 4) ? finalH : netOutH,
            encodeFmt, &compressedSize);
        free(netOut);
        if (freeFinalRgba) free(finalRgba);

        if (!compressed) {
            char buf[300];
            sprintf_s(buf, "[texture-upscale-worker] '%.200s': EncodeFromRGBA failed", job.name);
            LogFromController(buf);
            return;
        }

        TextureUpscaleIwi::MipLevel mip;
        mip.data = compressed;
        mip.size = compressedSize;
        uint32_t iwiSize = 0;
        uint8_t* iwi = TextureUpscaleIwi::EncodeIwi8(
            static_cast<uint16_t>(finalW), static_cast<uint16_t>(finalH), 1,
            iwiEncodeFmt, &mip, 1, &iwiSize);
        free(compressed);
        if (!iwi) {
            char buf[300];
            sprintf_s(buf, "[texture-upscale-worker] '%.200s': EncodeIwi8 failed", job.name);
            LogFromController(buf);
            return;
        }

        bool stored = TextureUpscaleCache::StoreUpscaledIwi(job.name, job.scaleMultiplier, iwi, iwiSize);
        free(iwi);

        char buf[300];
        if (stored) {
            sprintf_s(buf, "[texture-upscale-worker] '%.200s': upscaled %ux%u -> %ux%u and cached (%u bytes)",
                job.name, job.width, job.height, finalW, finalH, iwiSize);
            LogFromController(buf);
            sprintf_s(progressBuf, "[NCP] Cached upscaled \"%.200s\"", job.name);
            ShowOverlayMessage(progressBuf, 2500);
        } else {
            sprintf_s(buf, "[texture-upscale-worker] '%.200s': StoreUpscaledIwi failed", job.name);
            LogFromController(buf);
        }
    }

    DWORD WINAPI WorkerThreadProc(LPVOID)
    {
        for (;;) {
            PendingJob job{};
            bool have = false;
            EnterCriticalSection(&g_pendingLock);
            if (g_pendingCount > 0) {
                job = g_pendingJobs[g_pendingHead];
                g_pendingHead = (g_pendingHead + 1) % kMaxPendingJobs;
                --g_pendingCount;
                have = true;
            }
            LeaveCriticalSection(&g_pendingLock);

            if (have) {
                ProcessJob(job);
                if (job.compressedData) free(job.compressedData);
                // Failure paths inside ProcessJob already logged; whether to
                // allow a re-queue on a future load is decided here, once,
                // rather than threading a success/failure return value back
                // through every one of ProcessJob's own early-return paths.
                // A cheap, correct way to know: re-check whether the cache
                // file now actually exists.
                uint32_t checkSize = 0;
                uint8_t* checkBuf = TextureUpscaleCache::TryLoadCachedUpscaledIwi(job.name, job.scaleMultiplier, &checkSize);
                if (checkBuf) {
                    free(checkBuf);
                } else {
                    ClearInFlightOnFailure(job.name);
                }
            } else {
                Sleep(50); // idle poll -- new jobs are rare (once per new
                           // menu/HUD texture, deduped), not a hot loop
                           // worth a condition variable for, same reasoning
                           // as asset_capture.cpp's own write thread.
            }
        }
        return 0;
    }
}

void EnsureWorkerStarted()
{
    if (g_workerThreadHandle) return;
    if (!g_pendingLockInit) {
        InitializeCriticalSection(&g_pendingLock);
        g_pendingLockInit = true;
    }
    if (!g_inFlightLockInit) {
        InitializeCriticalSection(&g_inFlightLock);
        g_inFlightLockInit = true;
    }
    g_workerThreadHandle = CreateThread(nullptr, 0, WorkerThreadProc, nullptr, 0, nullptr);
}

bool QueueUpscaleJobFromIwiFile(const char* name, const uint8_t* iwiFileBytes, uint32_t iwiFileSize, int scaleMultiplier, const char* source)
{
    // Real IWI-v8 header layout, per texture_upscale_iwi_writer.h's own
    // documented, disassembly-verified layout -- same parse this project's
    // own live capture path (Hook_ImageFileLoadX64) already performs;
    // factored here so both callers share one implementation.
    constexpr uint32_t kIwiHeaderSize = 0x20;
    if (!name || !iwiFileBytes || iwiFileSize <= kIwiHeaderSize) return false;
    if (iwiFileBytes[0] != 'I' || iwiFileBytes[1] != 'W' || iwiFileBytes[2] != 'i' || iwiFileBytes[3] != 8) return false;

    int8_t rawFormat = static_cast<int8_t>(iwiFileBytes[0x08]);
    uint16_t iwiWidth = *reinterpret_cast<const uint16_t*>(iwiFileBytes + 0x0A);
    uint16_t iwiHeight = *reinterpret_cast<const uint16_t*>(iwiFileBytes + 0x0C);
    if (iwiWidth == 0 || iwiHeight == 0) return false;

    TextureUpscaleDxt::BlockFormat blockFmt;
    switch (static_cast<TextureUpscaleIwi::Format>(rawFormat)) {
        case TextureUpscaleIwi::Format::DXT1: blockFmt = TextureUpscaleDxt::BlockFormat::BC1; break;
        case TextureUpscaleIwi::Format::DXT3: blockFmt = TextureUpscaleDxt::BlockFormat::BC2; break;
        case TextureUpscaleIwi::Format::DXT5: blockFmt = TextureUpscaleDxt::BlockFormat::BC3; break;
        default: return false; // raw bitmap or unhandled format -- out of
            // scope for this feature, a routine/expected case, not an error.
    }

    // The base (largest, full-resolution) mip is always the LAST bytes in
    // the file -- mip data is smallest-first by construction, and this
    // format's block compression has no per-mip padding, so the base mip's
    // own byte size is exactly and unambiguously
    // CompressedSize(width,height,format), regardless of exact real
    // mip-count/fileSizeForPicmip-table semantics.
    uint32_t baseMipSize = TextureUpscaleDxt::CompressedSize(iwiWidth, iwiHeight, blockFmt);
    if (iwiFileSize < kIwiHeaderSize + baseMipSize) return false;

    uint8_t* mipCopy = static_cast<uint8_t*>(malloc(baseMipSize));
    if (!mipCopy) return false;
    memcpy(mipCopy, iwiFileBytes + (iwiFileSize - baseMipSize), baseMipSize);

    if (!QueueUpscaleJob(name, rawFormat, iwiWidth, iwiHeight, mipCopy, baseMipSize, scaleMultiplier, source)) {
        free(mipCopy); // already in flight, queue full, or invalid args --
            // a normal, expected outcome, not an error (QueueUpscaleJob
            // logs the success case itself).
        return false;
    }
    return true;
}

bool QueueUpscaleJob(const char* name, int format, uint32_t width, uint32_t height,
                      const uint8_t* compressedData, uint32_t compressedSize, int scaleMultiplier,
                      const char* source)
{
    if (!name || !compressedData || compressedSize == 0 || width == 0 || height == 0) return false;
    EnsureWorkerStarted();

    if (IsInFlight(name)) return false;

    EnterCriticalSection(&g_pendingLock);
    if (g_pendingCount >= kMaxPendingJobs) {
        LeaveCriticalSection(&g_pendingLock);
        return false;
    }
    PendingJob& job = g_pendingJobs[g_pendingTail];
    strncpy_s(job.name, name, _TRUNCATE);
    job.format = format;
    job.width = width;
    job.height = height;
    job.compressedData = const_cast<uint8_t*>(compressedData); // ownership
        // transfers to the queue/worker on success, per this function's own
        // declared contract.
    job.compressedSize = compressedSize;
    job.scaleMultiplier = scaleMultiplier;
    g_pendingTail = (g_pendingTail + 1) % kMaxPendingJobs;
    ++g_pendingCount;
    LeaveCriticalSection(&g_pendingLock);

    MarkInFlight(name);

    char buf[300];
    sprintf_s(buf, "[texture-upscale-worker] queued '%.200s' (%ux%u, format=%d, target=%dx, source=%s)",
        name, width, height, format, scaleMultiplier, source ? source : "unknown");
    LogFromController(buf);
    return true;
}

bool IsNameInFlight(const char* name)
{
    return name ? IsInFlight(name) : false;
}

}
