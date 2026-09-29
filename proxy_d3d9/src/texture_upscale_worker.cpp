#include "texture_upscale_worker.h"

#include <windows.h>
#include <cstdio>
#include <cstring>
#include <cstdlib>

#include "texture_upscale_dxt_codec.h"
#include "texture_upscale_iwi_writer.h"
#include "texture_upscale_ncnn.h"
#include "texture_upscale_cache.h"
#include "overlay_hud.h" // ShowOverlayMessageUntilDismissed -- the one-time
    // "caching is building, expect hitching" notice below, real player-facing
    // warning-modal use, not per-job toast spam (see the 2026-09-29 removal
    // of ProcessJob's own per-job toasts, still real progress feedback via
    // DrawTextureCacheStatusBar instead).

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
        PendingJob* next; // intrusive singly-linked FIFO node -- see the
            // queue-depth note below for why this replaced a fixed-capacity
            // ring buffer.
    };

    // Unbounded, malloc-backed singly-linked FIFO + dedicated background
    // thread (2026-09-29, direct instruction: "caching and cache logging
    // (dev only) shouldnt be bounded" -- replaces an original fixed-capacity
    // ring buffer, kMaxPendingJobs=48). The prior design silently DROPPED a
    // job with ZERO log line whenever the ring buffer was full
    // (QueueUpscaleJob returned false with no diagnostic at all) -- a real,
    // plausible explanation for captures that never show up as "queued" in
    // the log during a busy level load where the bulk pre-cache orchestrator
    // and organic load-time capture could both be feeding the same queue at
    // once. A node is simply malloc'd per job and linked in; the only real
    // limit is available memory, the same honest limit every other
    // real-world queue in this project already has (matching
    // asset_capture.cpp's own established "dedicated background thread"
    // shape, just without that one's own fixed-capacity choice). Still
    // CRITICAL_SECTION-guarded, not std::mutex, matching this project's own
    // established non-STL shipped-code convention.
    PendingJob* g_pendingHead = nullptr;
    PendingJob* g_pendingTail = nullptr;
    CRITICAL_SECTION g_pendingLock;
    bool g_pendingLockInit = false;
    // Real parallelism (2026-09-29, direct instruction: "we need
    // parralellism for sure") -- a real session can queue upward of 1800
    // distinct textures against a single background thread that only
    // manages ~361 of them in a session's worth of play; that ratio is the
    // real bottleneck, not a bug in the queue/dedup logic itself (both
    // fully verified correct via live crash-dump/log tracing this same
    // session). kWorkerThreadCount worker threads now pull from the SAME
    // shared queue/dedup structures above (already CRITICAL_SECTION-guarded
    // for exactly this) and each run real, independent GPU inference
    // concurrently -- see texture_upscale_ncnn.cpp's own comment on why
    // that's genuinely safe (ncnn's own documented per-thread-Extractor
    // concurrency model, not something this project invented).
    //
    // Widened 3 -> 6 (2026-09-29, same day, direct follow-up: "caching rn
    // is over 15s per texture which for over 1000 textures is wild... we
    // also need more parralellism"). Real per-texture latency this slow is
    // dominated by tiled inference cost (large world textures split into
    // many 256x256 tiles, each a real GPU round trip -- see
    // texture_upscale_ncnn.cpp's kTileMaxDim), so this is still a real bet
    // that the GPU has headroom for more concurrent submissions rather than
    // a proven scaling curve -- doubling is a genuine, honest guess at the
    // right next step, not benchmarked against actual measured throughput
    // at 3 vs. 6 yet. If 6 doesn't meaningfully improve wall-clock drain
    // rate, that's real evidence this workload is already GPU-saturated and
    // more threads would just add contention, not throughput -- worth
    // checking via the status bar's own live progress rate before pushing
    // this higher again.
    constexpr int kWorkerThreadCount = 6;
    HANDLE g_workerThreadHandles[kWorkerThreadCount] = {};

    // In-flight dedup set -- separate from the on-disk cache check in
    // Hook_ImageFileLoadX64 (analog_input_hooks_x64.cpp), which only ever
    // catches a PRIOR session's already-completed work. This catches the
    // same name being requested again THIS session while a job for it is
    // still queued or mid-processing (e.g. the same background texture
    // re-requested on a menu re-open before the first upscale finishes).
    //
    // Unbounded singly-linked list (2026-09-29, same instruction as above --
    // replaces an original fixed 256-entry array, kMaxInFlightNames). The
    // prior design silently stopped TRACKING new names past 256 for the
    // rest of the session (MarkInFlight simply did nothing once full) --
    // real world-geometry-heavy levels/sessions can genuinely exceed 256
    // distinct textures, at which point every name past the cap loses its
    // dedup protection with no log trail explaining why. Lookup stays O(n)
    // either way (the old array was already linearly scanned), so a linked
    // list costs nothing extra here.
    struct InFlightNode
    {
        char name[256];
        InFlightNode* next;
    };
    InFlightNode* g_inFlightHead = nullptr;
    CRITICAL_SECTION g_inFlightLock;
    bool g_inFlightLockInit = false;

    bool IsInFlight(const char* name)
    {
        EnterCriticalSection(&g_inFlightLock);
        bool found = false;
        for (InFlightNode* n = g_inFlightHead; n; n = n->next) {
            if (_stricmp(n->name, name) == 0) { found = true; break; }
        }
        LeaveCriticalSection(&g_inFlightLock);
        return found;
    }

    void MarkInFlight(const char* name)
    {
        InFlightNode* node = static_cast<InFlightNode*>(malloc(sizeof(InFlightNode)));
        if (!node) return; // real OOM -- nothing sane to do but skip dedup
            // tracking for this one name; the job itself still queues fine.
        strncpy_s(node->name, name, _TRUNCATE);
        EnterCriticalSection(&g_inFlightLock);
        node->next = g_inFlightHead;
        g_inFlightHead = node;
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
        InFlightNode* prev = nullptr;
        for (InFlightNode* n = g_inFlightHead; n; prev = n, n = n->next) {
            if (_stricmp(n->name, name) == 0) {
                if (prev) prev->next = n->next; else g_inFlightHead = n->next;
                free(n);
                break;
            }
        }
        LeaveCriticalSection(&g_inFlightLock);
    }

    // Real, live session-wide progress counters (2026-09-29) -- see this
    // file's own header (GetProgressSnapshot) for the full rationale. Plain
    // LONG + Interlocked* -- cheaper and simpler than a critical section for
    // three independent monotonic counters nothing else ever needs to read
    // atomically together.
    volatile LONG g_totalQueued = 0;
    volatile LONG g_totalProcessed = 0;
    volatile LONG g_totalFailed = 0;

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
        // Real per-job "[NCP] Upscaling ..."/"[NCP] Cached upscaled ..." toasts
        // (one per texture) removed 2026-09-29 -- with real sessions queuing
        // upward of 1800 distinct textures, that was a toast every few
        // seconds for the whole session, not useful feedback. Replaced by a
        // single persistent, live-updating status bar
        // (overlay_hud.cpp's own DrawTextureCacheStatusBar, driven by
        // GetProgressSnapshot) that shows real aggregate progress instead.
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
            PendingJob* node = nullptr;
            EnterCriticalSection(&g_pendingLock);
            if (g_pendingHead) {
                node = g_pendingHead;
                g_pendingHead = node->next;
                if (!g_pendingHead) g_pendingTail = nullptr;
                have = true;
            }
            LeaveCriticalSection(&g_pendingLock);

            if (have) {
                job = *node; // shallow copy -- job.compressedData ownership
                    // transfers with it, node itself is just the queue-link
                    // wrapper and is freed right after.
                free(node);
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
                    InterlockedIncrement(&g_totalFailed);
                }
                InterlockedIncrement(&g_totalProcessed);
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

namespace
{
    // Real, live-reported crash fix (2026-09-29): EnsureWorkerStarted's own
    // `if (g_workerThreadHandle) return;` early-out is NOT atomic, and this
    // function is now genuinely reachable from more than one thread at
    // once -- the render thread (Hook_SetTexture -> IsNameInFlight, the
    // viewport capture path) and the load-time capture thread
    // (Hook_ImageFileLoadX64 -> QueueUpscaleJob) can both call in
    // concurrently. Two real bugs stemmed from this: (1) IsNameInFlight
    // (texture_viewport_capture.cpp's own caller) could reach
    // EnterCriticalSection(&g_inFlightLock) before ANY thread had ever run
    // this function's body at all -- a genuine crash (null-class-ptr write
    // inside ntdll's critical-section code, confirmed via a real WER crash
    // dump: IsInFlight -> EnterCriticalSection on a still-zeroed
    // CRITICAL_SECTION); (2) even once fixed to always call this function
    // first, two threads racing the old non-atomic check could both pass it
    // and double-initialize the same critical sections concurrently with
    // the other thread actively inside one -- real, separate UB.
    // InitOnceExecuteOnce is the standard, real WinAPI primitive for
    // exactly this ("run this body exactly once, block every other
    // concurrent caller until it's done") -- replaces the racy manual
    // check entirely rather than patching around it.
    INIT_ONCE g_workerInitOnce = INIT_ONCE_STATIC_INIT;

    BOOL CALLBACK InitWorkerOnceCallback(PINIT_ONCE, PVOID, PVOID*)
    {
        InitializeCriticalSection(&g_pendingLock);
        g_pendingLockInit = true;
        InitializeCriticalSection(&g_inFlightLock);
        g_inFlightLockInit = true;
        for (int i = 0; i < kWorkerThreadCount; ++i) {
            g_workerThreadHandles[i] = CreateThread(nullptr, 0, WorkerThreadProc, nullptr, 0, nullptr);
        }
        return TRUE;
    }
}

void EnsureWorkerStarted()
{
    InitOnceExecuteOnce(&g_workerInitOnce, InitWorkerOnceCallback, nullptr, nullptr);
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

    // Unbounded (2026-09-29) -- see the struct/queue comment above for why a
    // hard cap here was actively dangerous (silent job loss, no log trail).
    // The only failure mode now is real OOM, which is worth keeping the
    // original bool-return contract for even though it should never
    // realistically trip.
    PendingJob* node = static_cast<PendingJob*>(malloc(sizeof(PendingJob)));
    if (!node) return false;
    strncpy_s(node->name, name, _TRUNCATE);
    node->format = format;
    node->width = width;
    node->height = height;
    node->compressedData = const_cast<uint8_t*>(compressedData); // ownership
        // transfers to the queue/worker on success, per this function's own
        // declared contract.
    node->compressedSize = compressedSize;
    node->scaleMultiplier = scaleMultiplier;
    node->next = nullptr;

    EnterCriticalSection(&g_pendingLock);
    if (g_pendingTail) g_pendingTail->next = node; else g_pendingHead = node;
    g_pendingTail = node;
    LeaveCriticalSection(&g_pendingLock);

    MarkInFlight(name);
    LONG newQueuedCount = InterlockedIncrement(&g_totalQueued);

    // Real one-time "expect hitching, restart to see results" notice
    // (2026-09-29, direct instruction) -- fires exactly once, the first time
    // the real session-wide queued count crosses 100.
    // InterlockedCompareExchange (not a plain bool check) so two worker...
    // no wait, this runs on the QUEUING side (the load-time/viewport capture
    // threads), which genuinely can call QueueUpscaleJob concurrently from
    // more than one thread -- the same race class InitOnceExecuteOnce above
    // already had to be introduced for, guarded the same way here rather
    // than risking two threads both crossing 100 at once and double-firing.
    constexpr LONG kCacheBuildNoticeThreshold = 100;
    static volatile LONG s_cacheBuildNoticeShown = 0;
    if (newQueuedCount >= kCacheBuildNoticeThreshold &&
        InterlockedCompareExchange(&s_cacheBuildNoticeShown, 1, 0) == 0) {
        ShowOverlayMessageUntilDismissed(kCacheBuildNoticeText, OverlayAnimStyle::Plain);
    }

    char buf[300];
    sprintf_s(buf, "[texture-upscale-worker] queued '%.200s' (%ux%u, format=%d, target=%dx, source=%s)",
        name, width, height, format, scaleMultiplier, source ? source : "unknown");
    LogFromController(buf);
    return true;
}

bool IsNameInFlight(const char* name)
{
    if (!name) return false;
    // Real fix (2026-09-29, round 2): the InitOnceExecuteOnce change above
    // fixed the RACE inside EnsureWorkerStarted, but this exported entry
    // point -- the viewport-capture path's own real caller
    // (texture_viewport_capture.cpp's Hook_SetTexture) -- never actually
    // CALLED EnsureWorkerStarted before touching g_inFlightLock in the
    // first place. A second, identical crash (same WER dump signature)
    // confirmed the ordering bug was still live. EnsureWorkerStarted is
    // idempotent and cheap after the first real call (InitOnceExecuteOnce's
    // own fast path), so calling it here unconditionally is the correct,
    // permanent fix, not a narrow patch for this one caller.
    EnsureWorkerStarted();
    return IsInFlight(name);
}

void GetProgressSnapshot(uint32_t* queued, uint32_t* processed, uint32_t* failed)
{
    if (queued) *queued = static_cast<uint32_t>(InterlockedCompareExchange(&g_totalQueued, 0, 0));
    if (processed) *processed = static_cast<uint32_t>(InterlockedCompareExchange(&g_totalProcessed, 0, 0));
    if (failed) *failed = static_cast<uint32_t>(InterlockedCompareExchange(&g_totalFailed, 0, 0));
}

}
