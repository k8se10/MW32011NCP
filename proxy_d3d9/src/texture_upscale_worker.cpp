#include "texture_upscale_worker.h"

#include <windows.h>
#include <cstdio>
#include <cstring>
#include <cstdlib>

#include "texture_upscale_dxt_codec.h"
#include "texture_upscale_iwi_writer.h"
#include "texture_upscale_ncnn.h"
#include "texture_upscale_cache.h"
#include "mod_config.h" // g_modConfig.textureUpscaleWorkerThreads -- see
    // EnsureWorkerStarted's own comment on the real thread-count read.
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
    // Made customizable (2026-09-29, direct instruction: "add customisable
    // worker threads(hardware makes this a great option) - in my config i
    // want 12(2080 ti hardware so) but for default should be 3") -- real
    // per-texture latency is dominated by tiled GPU inference cost (large
    // world textures split into many 256x256 tiles, each a real GPU round
    // trip -- see texture_upscale_ncnn.cpp's kTileMaxDim), so the right
    // thread count genuinely depends on the player's own GPU headroom, not
    // a single fixed constant this project can pick for everyone. The real
    // count is `[Experimental] TextureUpscaleWorkerThreads` in
    // mw3ncp_config.ini (mod_config.h/.cpp, default 3, clamped 1..16 --
    // ClampIntSetting there is the real source of truth for the bounds).
    // kMaxWorkerThreadCount here is just the array's fixed storage size,
    // matching that same clamp ceiling -- the array is always allocated at
    // its max size; only the first N (the real configured count) ever get a
    // real thread.
    constexpr int kMaxWorkerThreadCount = 16;
    HANDLE g_workerThreadHandles[kMaxWorkerThreadCount] = {};

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
    // atomically together. Declared here (moved up from further down the
    // file) so the session-continuity baseline code below -- which needs to
    // pre-seed these at startup -- can reference them without a forward-
    // declaration issue.
    volatile LONG g_totalQueued = 0;
    volatile LONG g_totalProcessed = 0;
    volatile LONG g_totalFailed = 0;

    // Real "is a job actively being worked on right now" signal (2026-09-29,
    // for CheckAutoExitOnCacheCompleteIfDue below) -- distinct from the
    // queued/processed counters above, which only capture completed events,
    // not in-progress ones. Incremented right before ProcessJob runs,
    // decremented right after -- a single large/slow job (real GPU inference
    // can genuinely take a while) must count as "active," not "idle," for
    // the whole time it's running, not just at its start/end instants.
    volatile LONG g_activeWorkerCount = 0;

    // ---- Cross-session continuity (2026-09-29) -----------------------------
    // Direct request: "we should have session continuity so cacheing knows
    // what to cache from last runs." The on-disk upscale cache itself already
    // persists forever once a texture IS cached -- this closes the other real
    // gap: the QUEUE (what's still pending) is pure in-memory, so a crash or
    // early exit loses every not-yet-processed job, and the next session only
    // re-discovers that work if the player happens to revisit the same
    // content again. This tracks just NAMES (never the actual captured
    // texture bytes -- those are cheap to re-capture the next time the game
    // itself loads that name, and persisting them would make this a much
    // larger, riskier on-disk structure for no real benefit), written at a
    // deliberately LOW frequency (not per-event -- after this same session's
    // own file-handle-table crash, adding another high-frequency file I/O
    // path is exactly the kind of risk worth avoiding).
    struct CarriedOverNode
    {
        char name[256];
        CarriedOverNode* next;
    };
    CarriedOverNode* g_carriedOverHead = nullptr;
    CRITICAL_SECTION g_carriedOverLock;
    bool g_carriedOverLockInit = false;

    bool IsCarriedOver(const char* name)
    {
        EnterCriticalSection(&g_carriedOverLock);
        bool found = false;
        for (CarriedOverNode* n = g_carriedOverHead; n; n = n->next) {
            if (_stricmp(n->name, name) == 0) { found = true; break; }
        }
        LeaveCriticalSection(&g_carriedOverLock);
        return found;
    }

    // Called once a carried-over name is successfully cached this session --
    // stops it being artificially boosted to the front of the queue forever
    // (it would otherwise never get removed, since nothing else drops entries
    // from this set).
    void RemoveCarriedOver(const char* name)
    {
        EnterCriticalSection(&g_carriedOverLock);
        CarriedOverNode* prev = nullptr;
        for (CarriedOverNode* n = g_carriedOverHead; n; prev = n, n = n->next) {
            if (_stricmp(n->name, name) == 0) {
                if (prev) prev->next = n->next; else g_carriedOverHead = n->next;
                free(n);
                break;
            }
        }
        LeaveCriticalSection(&g_carriedOverLock);
    }

    bool BuildManifestPath(char* outPath, size_t outPathSize, const char* fileName)
    {
        char path[MAX_PATH];
        GetModuleFileNameA(nullptr, path, MAX_PATH); // same convention as
            // TextureUpscaleCache::EnsureCacheDir -- full path of the .exe
            // that loaded us.
        char* lastSlash = strrchr(path, '\\');
        if (lastSlash) *(lastSlash + 1) = '\0';
        strcat_s(path, "texture_upscale_cache\\");
        strcat_s(path, fileName);
        if (strlen(path) + 1 > outPathSize) return false;
        strcpy_s(outPath, outPathSize, path);
        return true;
    }

    // Loads a plain, one-name-per-line text file into g_carriedOverHead --
    // shared by both LoadPendingManifest (the live, per-session file) and the
    // bundled basemap below. Returns the number of names actually loaded (0
    // if the file doesn't exist, which is a real, expected case for both
    // callers, not an error).
    int LoadNamesFromFile(const char* path)
    {
        FILE* f = nullptr;
        if (fopen_s(&f, path, "r") != 0 || !f) return 0;
        char line[256];
        int loadedCount = 0;
        while (fgets(line, sizeof(line), f)) {
            size_t len = strlen(line);
            while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) line[--len] = '\0';
            if (len == 0) continue;
            CarriedOverNode* node = static_cast<CarriedOverNode*>(malloc(sizeof(CarriedOverNode)));
            if (!node) break; // real OOM -- stop loading, keep whatever's already loaded
            strncpy_s(node->name, line, _TRUNCATE);
            node->next = g_carriedOverHead;
            g_carriedOverHead = node;
            ++loadedCount;
        }
        fclose(f);
        return loadedCount;
    }

    // Called exactly once, from InitWorkerOnceCallback below, before any real
    // queuing can happen -- loads TWO sources into g_carriedOverHead, both
    // best-effort priority hints, never critical state (a partially-written
    // or slightly stale file degrades gracefully: worst case, a name that's
    // actually already cached gets a harmless priority boost once, then
    // RemoveCarriedOver clears it the moment it's confirmed cached again).
    //
    // 1. texture_names_basemap.txt -- a real, bundled, shipped-with-the-mod
    //    list (2026-09-29, direct request: "ive now documented every single
    //    texture in that text thats used in campaign and survival... we
    //    should install this as the basemap before it scans for active use
    //    ones"). 17,004 real names -- the deduped union of every already-
    //    cached texture (real .iwi files in texture_upscale_cache\, scale
    //    suffix stripped) and everything still genuinely pending, both
    //    accumulated across extensive real Campaign+Survival play, confirmed
    //    by the user as the real total. Committed to the repo
    //    (proxy_d3d9/bundled_data/) and deployed by proxy_d3d9.vcxproj's own
    //    DeployTextureUpscaleAssets target -- so a FRESH install starts with
    //    this coverage already known, instead of needing the same extensive
    //    playtime to rediscover it session by session. Loaded FIRST, per the
    //    direct request's own "before it scans" ordering (though since both
    //    sources merge into the same set, the practical effect is the same
    //    either way -- kept in this order for clarity, matching the request
    //    literally).
    // 2. pending_manifest.txt -- the live, per-session file
    //    PersistPendingManifestIfDue writes; carries forward whatever a
    //    PRIOR session left genuinely still-pending (a real, growing
    //    superset of the static basemap for names the basemap didn't cover,
    //    or that were added to the game/discovered since the basemap was
    //    last refreshed).
    void LoadPendingManifest()
    {
        char basemapPath[MAX_PATH];
        int basemapCount = 0;
        if (BuildManifestPath(basemapPath, sizeof(basemapPath), "texture_names_basemap.txt")) {
            basemapCount = LoadNamesFromFile(basemapPath);
        }

        char pendingPath[MAX_PATH];
        int pendingCount = 0;
        if (BuildManifestPath(pendingPath, sizeof(pendingPath), "pending_manifest.txt")) {
            pendingCount = LoadNamesFromFile(pendingPath);
        }

        if (basemapCount > 0 || pendingCount > 0) {
            char buf[220];
            sprintf_s(buf, "[texture-upscale-continuity] loaded %d name(s) from the bundled basemap and "
                "%d carried over from a prior session's own pending queue.",
                basemapCount, pendingCount);
            LogFromController(buf);
        }

        // Real-progress baseline (2026-09-29, live-reported gap: "the progress
        // indicator doesn't show the loaded progress and it looks like it
        // starts fresh"). Without this, DrawTextureCacheStatusBar's own
        // queued/processed counters only ever reflected THIS session's own
        // increments, starting at 0/0 regardless of how much of the known
        // game was already cached from prior sessions -- a real, misleading
        // gap now that a comprehensive bundled basemap exists. Pre-seeds
        // g_totalQueued/g_totalProcessed with the REAL known total and how
        // many of those already have a real cache file (a cheap
        // GetFileAttributesA check per name, not a real read -- see
        // TextureUpscaleCache::CacheEntryExists), so the status bar shows
        // genuine overall completion (e.g. "9802/17008 (58%)") from the very
        // first frame instead of appearing to start from scratch.
        // QueueUpscaleJob's own `wasKnown` check (see that function's own
        // comment) prevents double-counting any of these names again once
        // they're actually queued this session.
        if (g_carriedOverHead) {
            int totalKnown = 0, alreadyCached = 0;
            int scale = g_modConfig.textureRenderRes;
            for (CarriedOverNode* n = g_carriedOverHead; n; n = n->next) {
                ++totalKnown;
                if (TextureUpscaleCache::CacheEntryExists(n->name, scale)) ++alreadyCached;
            }
            InterlockedExchange(&g_totalQueued, totalKnown);
            InterlockedExchange(&g_totalProcessed, alreadyCached);
            char buf[220];
            sprintf_s(buf, "[texture-upscale-continuity] real coverage baseline: %d/%d known textures "
                "already cached at %dx (%.1f%%).",
                alreadyCached, totalKnown, scale,
                totalKnown > 0 ? (100.0 * alreadyCached / totalKnown) : 0.0);
            LogFromController(buf);
        }
    }

    // Called periodically (from WorkerThreadProc's own loop, never per-event)
    // to snapshot the CURRENT pending queue's own real names to disk, so a
    // crash or early exit doesn't lose the whole backlog's worth of "what
    // still needs caching" -- only whatever was queued since the last periodic
    // write. Full rewrite each time (not incremental) -- simplest correct
    // design, and the file itself is small (plain names, one per line) even
    // for a real multi-thousand-entry backlog.
    void PersistPendingManifestIfDue()
    {
        constexpr DWORD kIntervalMs = 30000; // deliberately low frequency --
            // see this section's own header comment for why.
        static DWORD s_lastWriteMs = 0;
        DWORD now = GetTickCount();
        if (s_lastWriteMs != 0 && (now - s_lastWriteMs) < kIntervalMs) return;
        s_lastWriteMs = now;

        char path[MAX_PATH];
        if (!BuildManifestPath(path, sizeof(path), "pending_manifest.txt")) return;
        FILE* f = nullptr;
        if (fopen_s(&f, path, "w") != 0 || !f) return; // real failure -- skip this
            // period's write silently; the next period tries again, and losing
            // one snapshot is a real but low-cost degradation, not worth a log
            // line every 30s if the directory is ever briefly unwritable.

        EnterCriticalSection(&g_pendingLock);
        for (PendingJob* n = g_pendingHead; n; n = n->next) {
            fprintf(f, "%s\n", n->name);
        }
        LeaveCriticalSection(&g_pendingLock);

        fclose(f);
    }

    // Real dev/QoL feature (2026-09-29, direct request): "end process on
    // texture cache completion (no cache activity for 2+mins (this means no
    // active i.o not just a task taking a while)." Default OFF --
    // [Experimental] AutoExitOnCacheComplete.
    //
    // "Idle" is deliberately defined as NO job currently in flight
    // (g_activeWorkerCount == 0) AND the queue empty (nothing left
    // unprocessed, via the same queued/processed counters the status bar
    // uses) -- checked EVERY call (cheap, four atomic reads), not just
    // periodically, so a single large/slow job (real GPU inference can
    // genuinely take a while at bigger tile sizes) never gets mistaken for
    // "done" just because no job has FINISHED recently. The moment either
    // condition goes false, the idle timer resets to zero -- only a truly
    // continuous 2 real minutes of nothing happening triggers the exit.
    void CheckAutoExitOnCacheCompleteIfDue()
    {
        if (!g_modConfig.autoExitOnCacheCompleteEnabled) return;

        LONG queued = InterlockedCompareExchange(&g_totalQueued, 0, 0);
        LONG processed = InterlockedCompareExchange(&g_totalProcessed, 0, 0);
        LONG activeNow = InterlockedCompareExchange(&g_activeWorkerCount, 0, 0);
        bool idleNow = (activeNow == 0) && (processed >= queued);

        static DWORD s_idleSinceMs = 0;
        DWORD now = GetTickCount();
        if (!idleNow) {
            s_idleSinceMs = 0;
            return;
        }
        if (s_idleSinceMs == 0) {
            s_idleSinceMs = now; // just became idle -- start the real timer
            return;
        }

        constexpr DWORD kIdleExitDelayMs = 2 * 60 * 1000; // 2 real minutes
        if ((now - s_idleSinceMs) < kIdleExitDelayMs) return;

        char buf[220];
        sprintf_s(buf, "[texture-upscale-continuity] AutoExitOnCacheComplete: no cache activity for 2+ "
            "minutes (%d/%d processed, 0 active) -- closing the process now.",
            processed, queued);
        LogFromController(buf);
        ExitProcess(0); // real, deliberate self-termination -- this feature's
            // entire purpose is to end the process once there's genuinely
            // nothing left to do.
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

    // Real raw-bitmap (uncompressed) format support (2026-09-29), direct
    // request: "i also think we should support all textures the game
    // serves." TextureUpscaleIwi::Format::BitmapRGBA/BitmapRGB were already
    // defined in the format enum but never actually decoded -- this
    // project's own capture-outcome diagnostic showed these are real,
    // sometimes large, visible assets (main-menu backgrounds, mission-select
    // thumbnails, loading screens -- e.g. `background_image_blur_less` at
    // 2.76MB, `mission_sel1/2/3`) rejected outright as "unsupported" the
    // whole time. No block compression involved -- `bytesPerPixel` straight
    // pixel data, row-major, no padding (matches how this project's own IWI
    // writer/reader already treats DXT mip data: no per-mip padding
    // either). Other observed-but-uncharacterized raw formats (rawFormat
    // 3/4/6/7/9 in live logs) are a real, separate, NOT-yet-identified gap
    // -- would need dedicated RE work (this project has no confirmed
    // mapping for what those actually are) to support; left open rather
    // than guessed at.
    bool RawBitmapBytesPerPixel(int iwiFormat, int* outBpp)
    {
        switch (static_cast<TextureUpscaleIwi::Format>(iwiFormat)) {
            case TextureUpscaleIwi::Format::BitmapRGBA: *outBpp = 4; return true;
            case TextureUpscaleIwi::Format::BitmapRGB: *outBpp = 3; return true;
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
        // Real, always-DXT5 output regardless of source format (2026-09-29) --
        // every source this feature can now decode (BC1/BC2/BC3 block-
        // compressed, or raw uncompressed RGBA/RGB) converges on the same
        // real RGBA buffer before inference, so there's no reason the OUTPUT
        // encoding needs to track the input's own format at all -- DXT5 is a
        // real, strictly reasonable, storage-efficient choice for any content
        // (already the existing precedent for BC2's own "re-encode as BC3"
        // upgrade below). Simplifies what would otherwise need a real
        // "encode raw bitmap back to raw bitmap" path for zero real benefit.
        TextureUpscaleDxt::BlockFormat encodeFmt = TextureUpscaleDxt::BlockFormat::BC3;
        TextureUpscaleIwi::Format iwiEncodeFmt = TextureUpscaleIwi::Format::DXT5;

        uint8_t* rgba = nullptr;
        TextureUpscaleDxt::BlockFormat blockFmt;
        int rawBpp = 0;
        if (IwiFormatToBlockFormat(job.format, &blockFmt)) {
            rgba = TextureUpscaleDxt::DecodeToRGBA(job.compressedData, job.width, job.height, blockFmt);
        } else if (RawBitmapBytesPerPixel(job.format, &rawBpp)) {
            // Real, direct request: "i also think we should support all
            // textures the game serves." No block decompression needed --
            // the captured bytes already ARE row-major pixel data. Expand to
            // RGBA (inserting full alpha=255) if the source was 3-byte RGB;
            // a 4-byte RGBA source is used as-is via a plain copy, keeping
            // ownership/lifetime handling identical to the decoded-DXT path
            // below (this function always owns and frees `rgba` itself).
            uint64_t pixelCount = static_cast<uint64_t>(job.width) * job.height;
            uint64_t expectedSize = pixelCount * static_cast<uint64_t>(rawBpp);
            if (static_cast<uint64_t>(job.compressedSize) < expectedSize) {
                char buf[300];
                sprintf_s(buf, "[texture-upscale-worker] '%.200s': raw bitmap source truncated "
                    "(have %u bytes, need %llu for %ux%u at %d bpp)",
                    job.name, job.compressedSize, static_cast<unsigned long long>(expectedSize),
                    job.width, job.height, rawBpp);
                LogFromController(buf);
                return;
            }
            rgba = static_cast<uint8_t*>(malloc(static_cast<size_t>(pixelCount) * 4));
            if (rgba) {
                if (rawBpp == 4) {
                    memcpy(rgba, job.compressedData, static_cast<size_t>(pixelCount) * 4);
                } else { // rawBpp == 3
                    for (uint64_t i = 0; i < pixelCount; ++i) {
                        rgba[i * 4 + 0] = job.compressedData[i * 3 + 0];
                        rgba[i * 4 + 1] = job.compressedData[i * 3 + 1];
                        rgba[i * 4 + 2] = job.compressedData[i * 3 + 2];
                        rgba[i * 4 + 3] = 255;
                    }
                }
            }
        } else {
            char buf[300];
            sprintf_s(buf, "[texture-upscale-worker] '%.200s': unsupported source format %d, skipping", job.name, job.format);
            LogFromController(buf);
            return;
        }

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
                InterlockedIncrement(&g_activeWorkerCount);
                ProcessJob(job);
                InterlockedDecrement(&g_activeWorkerCount);
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
                    RemoveCarriedOver(job.name); // 2026-09-29, session continuity --
                        // confirmed cached, stop artificially boosting this name.
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
            PersistPendingManifestIfDue(); // 2026-09-29, session continuity --
                // real no-op most iterations (its own internal 30s interval
                // check), called unconditionally (not just in the idle branch)
                // so a continuously busy backlog-draining session still gets
                // periodic snapshots, not just an idle one.
            CheckAutoExitOnCacheCompleteIfDue(); // 2026-09-29, dev/QoL --
                // real no-op unless enabled; cheap (four atomic reads) even
                // when enabled, safe to call every iteration.
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
        InitializeCriticalSection(&g_carriedOverLock);
        g_carriedOverLockInit = true;
        LoadPendingManifest(); // 2026-09-29, session continuity -- see that
            // function's own header comment. Must run before any real thread
            // starts queuing jobs, which InitOnceExecuteOnce already guarantees
            // (every other caller blocks until this whole callback returns).
        // Real, live config value, already clamped 1..16 by mod_config.cpp's
        // own ClampIntSetting at load time -- clamped again here purely as a
        // defensive array-bounds safety net, not because the config layer is
        // untrusted.
        int threadCount = g_modConfig.textureUpscaleWorkerThreads;
        if (threadCount < 1) threadCount = 1;
        if (threadCount > kMaxWorkerThreadCount) threadCount = kMaxWorkerThreadCount;
        for (int i = 0; i < threadCount; ++i) {
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

    // The base (largest, full-resolution) mip is always the LAST bytes in
    // the file -- mip data is smallest-first by construction, so the base
    // mip's own byte size is exactly and unambiguously the real per-format
    // size for (width,height), regardless of exact real mip-count/
    // fileSizeForPicmip-table semantics -- true for both DXT block
    // compression (no per-mip padding) and raw bitmap data (no row padding).
    uint32_t baseMipSize = 0;
    TextureUpscaleDxt::BlockFormat blockFmt;
    int rawBpp = 0;
    if (IwiFormatToBlockFormat(rawFormat, &blockFmt)) {
        baseMipSize = TextureUpscaleDxt::CompressedSize(iwiWidth, iwiHeight, blockFmt);
    } else if (RawBitmapBytesPerPixel(rawFormat, &rawBpp)) {
        baseMipSize = static_cast<uint32_t>(iwiWidth) * iwiHeight * static_cast<uint32_t>(rawBpp);
    } else {
        return false; // genuinely unidentified format (rawFormat 3/4/6/7/9 --
            // see this file's own RawBitmapBytesPerPixel header comment) --
            // out of scope, a routine/expected case, not an error.
    }
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

    // Real sanity cap (2026-09-29), live-reported: "if the requested
    // asset/texture isnt then called by the engine it will infinitely stall
    // on that item (i left overnight and it stopped scannin around 11800."
    // Leading theory: a proactively-triggered or otherwise-forced capture for
    // a name the real engine never genuinely finished loading this session
    // can hand back garbage/uninitialized width/height -- a corrupted huge
    // dimension pair (not literally infinite, but effectively so: tiled
    // inference cost scales with pixel count, so a garbage 60000x60000
    // "texture" could take unrealistically, practically-forever-feeling
    // hours on a single job) would silently occupy the one serialized GPU
    // slot (RunInferenceOnTileRgb's own header comment) for the rest of the
    // session, exactly matching "stopped scanning" -- nothing behind it
    // could ever run. No real texture this feature has ever captured live
    // (from either Campaign or Survival, across this project's own extensive
    // testing) has exceeded 2048 on either axis -- 4096 is a real, generous
    // ceiling with margin, not a tight guess. Rejected loudly (not silently)
    // so a genuinely-new, legitimately-larger real texture would be caught
    // and reported rather than quietly dropped forever.
    constexpr uint32_t kMaxSaneDim = 4096;
    if (width > kMaxSaneDim || height > kMaxSaneDim) {
        char buf[300];
        sprintf_s(buf, "[texture-upscale-worker] '%.200s': REJECTED, %ux%u exceeds the real sanity "
            "cap (%u) -- likely garbage/uninitialized dimensions from a texture the engine never "
            "genuinely finished loading this session, not a real texture size.",
            name, width, height, kMaxSaneDim);
        LogFromController(buf);
        return false;
    }

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

    // Inserted in ascending pixel-count order, not appended FIFO (2026-09-29,
    // direct observation: "maybe the issue is that were not sorting via
    // texture size then processing accordingly (it shouldnt be uniform)").
    // With real GPU submission now correctly serialized (RunInferenceOnTileRgb's
    // own header comment) only ONE job's inference can ever be in flight at a
    // time regardless of worker-thread count -- a large texture queued early
    // (FIFO order) could sit at the front and block every small, fast job
    // behind it for its own full processing time, making a real session look
    // completely stalled (queued climbing, nothing completing) even though
    // work genuinely is happening. Small/common textures (UI, icons, most
    // weapon textures) now jump ahead of large ones queued earlier, so
    // completions happen steadily and visibly throughout a session instead of
    // in one large batch at the end; large world textures still get processed
    // eventually, just after the cheap ones already queued. O(n) insert is
    // fine here -- this runs once per texture LOAD EVENT (not per frame), and
    // real sessions queue in the hundreds/low thousands, not a hot path.
    // Session-continuity priority boost (2026-09-29) -- a name a PRIOR
    // session left pending (never got cached before that session ended), OR
    // a name from the bundled basemap, jumps straight to the very front,
    // ahead of even same-size new work, so genuinely interrupted/known work
    // actually finishes instead of getting re-queued and re-interrupted at a
    // similar point every session. Captured ONCE, before the lock, and
    // reused below for the progress-counter decision too -- see that
    // comment for why.
    bool wasKnown = IsCarriedOver(name);
    EnterCriticalSection(&g_pendingLock);
    if (wasKnown) {
        node->next = g_pendingHead;
        g_pendingHead = node;
        if (!g_pendingTail) g_pendingTail = node;
    } else {
        uint64_t newJobPixels = static_cast<uint64_t>(width) * height;
        PendingJob* prev = nullptr;
        PendingJob* cur = g_pendingHead;
        while (cur && (static_cast<uint64_t>(cur->width) * cur->height) <= newJobPixels) {
            prev = cur;
            cur = cur->next;
        }
        node->next = cur;
        if (prev) prev->next = node; else g_pendingHead = node;
        if (!cur) g_pendingTail = node;
    }
    LeaveCriticalSection(&g_pendingLock);

    MarkInFlight(name);

    // Progress-counter accounting (2026-09-29): g_totalQueued/g_totalProcessed
    // are pre-seeded at startup (LoadPendingManifest) with the REAL total
    // known-basemap count and how many of those are already cached, so the
    // status bar reflects genuine overall progress against the whole known
    // game, not just this session's own increments (see that function's own
    // header comment). A `wasKnown` name (basemap or carried-over) is
    // already counted in that startup baseline -- incrementing g_totalQueued
    // again here would double-count it. Only a genuinely NEW name (never
    // part of the known baseline) grows the total.
    if (!wasKnown) InterlockedIncrement(&g_totalQueued);
    LONG currentQueuedCount = InterlockedCompareExchange(&g_totalQueued, 0, 0); // atomic read

    // Real one-time "expect hitching, restart to see results" notice
    // (2026-09-29, direct instruction) -- fires exactly once, the first time
    // the real session-wide queued count crosses 100. Checked against the
    // CURRENT total (which can already be well past 100 from the startup
    // baseline alone, not just this call's own increment) rather than only
    // InterlockedIncrement's own return value, since a wasKnown item never
    // increments at all.
    // InterlockedCompareExchange (not a plain bool check) so two worker...
    // no wait, this runs on the QUEUING side (the load-time/viewport capture
    // threads), which genuinely can call QueueUpscaleJob concurrently from
    // more than one thread -- the same race class InitOnceExecuteOnce above
    // already had to be introduced for, guarded the same way here rather
    // than risking two threads both crossing 100 at once and double-firing.
    constexpr LONG kCacheBuildNoticeThreshold = 100;
    static volatile LONG s_cacheBuildNoticeShown = 0;
    if (currentQueuedCount >= kCacheBuildNoticeThreshold &&
        InterlockedCompareExchange(&s_cacheBuildNoticeShown, 1, 0) == 0) {
        ShowOverlayMessageUntilDismissed(kCacheBuildNoticeText, OverlayAnimStyle::Plain);
    }

    char buf[300];
    sprintf_s(buf, "[texture-upscale-worker] queued '%.200s' (%ux%u, format=%d, target=%dx, source=%s)",
        name, width, height, format, scaleMultiplier, source ? source : "unknown");
    LogFromController(buf);
    return true;
}

void ForEachCarriedOverName(void (*callback)(const char* name, void* userData), void* userData)
{
    if (!callback) return;
    EnterCriticalSection(&g_carriedOverLock);
    for (CarriedOverNode* n = g_carriedOverHead; n; n = n->next) {
        callback(n->name, userData);
    }
    LeaveCriticalSection(&g_carriedOverLock);
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
