#pragma once

#include <cstdint>

// Runtime texture-upscale-cache background worker (2026-09-28) --
// re_notes/x64_migration/texture_upscale_cache_research.md. Closes this
// feature's own last real wiring gap: Hook_ImageFileLoadX64
// (analog_input_hooks_x64.cpp) only ever CHECKED for an existing cache/
// custom_assets override -- nothing populated the cache on a real miss. This
// module owns the actual population: a dedicated background thread (matching
// this project's own established "one thread per job, event-driven"
// convention from the 2026-08-25 stutter-fix architecture, and
// asset_capture.cpp's own precedent for a fixed-capacity queue + dedicated
// writer thread) that decodes a real captured DXT source texture, runs it
// through TextureUpscaleNcnn::UpscaleRGBA4x (tiled internally for large
// images), re-encodes, and writes the result via
// TextureUpscaleCache::StoreUpscaledIwi -- entirely off the game's own
// loading/render thread.
//
// A queued job never blocks or alters the CURRENT texture load that
// triggered it -- the real original texture is what the game displays this
// time; the upscaled result only becomes available on a LATER load of the
// same image name (a future level visit, or the next session), matching
// this whole feature's own "population is best-effort background work, never
// a stall" design.
namespace TextureUpscaleWorker
{
    // Queues a real captured source texture for background upscaling, if
    // `name` isn't already queued or mid-processing this session (a simple
    // in-flight dedup set -- separate from the on-disk cache check in
    // Hook_ImageFileLoadX64, which only ever catches a PRIOR session's
    // completed work, not a job already in flight THIS session). The queue
    // itself is unbounded (2026-09-29 -- an earlier fixed-capacity ring
    // buffer silently dropped jobs with no log line once full, a real,
    // plausible source of "why didn't this texture ever get queued"
    // mysteries; see texture_upscale_worker.cpp's own comment) -- the only
    // realistic failure mode left is genuine OOM. Takes ownership of
    // `compressedData` (malloc'd) on success -- the worker thread frees it
    // once processed. On failure (already in flight, or real OOM), the
    // caller must free it instead.
    //
    // `format` is the real TextureUpscaleIwi::Format the source data is
    // encoded in (DXT1/DXT3/DXT5 only -- other formats are the caller's
    // responsibility to filter out before calling this). `scaleMultiplier`
    // is the real g_modConfig.textureRenderRes value at capture time (2/3/4).
    // `source` (2026-09-29) -- a short, fixed literal identifying which of
    // this feature's real capture mechanisms called in (e.g. "loadtime",
    // "precache", "viewport") -- included in the success log line so a live
    // session can be checked to see which mechanism(s) are actually
    // contributing real captures, not just whether the pipeline as a whole
    // is working. Diagnostic only, never affects behavior.
    bool QueueUpscaleJob(const char* name, int format, uint32_t width, uint32_t height,
                          const uint8_t* compressedData, uint32_t compressedSize, int scaleMultiplier,
                          const char* source = "unknown");

    // Starts the background worker thread if not already running. Safe to
    // call repeatedly (idempotent) -- QueueUpscaleJob calls this internally,
    // so callers don't need to call it directly in the ordinary case.
    void EnsureWorkerStarted();

    // Real IWI-v8-header-parse + base-mip-extraction + QueueUpscaleJob, all
    // in one call -- shared logic between the live capture path
    // (Hook_ImageFileLoadX64's own parse-on-miss step) and the bulk
    // pre-cache orchestrator (texture_precache_orchestrator.cpp), which both
    // need to turn "a complete, real, on-disk-or-captured IWI file's raw
    // bytes" into a queued job (2026-09-28, factored out to avoid the same
    // header-parse logic drifting between two copies). `iwiFileBytes` is the
    // COMPLETE file (header + every mip, smallest-first) -- this function
    // reads the header, validates the format is DXT1/3/5, and extracts the
    // base (largest) mip using the same "always the trailing
    // CompressedSize(w,h,format) bytes" trick Hook_ImageFileLoadX64's own
    // capture path already relies on. Returns false (no-op, no log) on any
    // real, expected miss (bad magic, unsupported format, truncated file) --
    // a bulk pass walking real dumped files will hit plenty of these
    // (raw-bitmap icons, etc.), not worth logging each one individually.
    bool QueueUpscaleJobFromIwiFile(const char* name, const uint8_t* iwiFileBytes, uint32_t iwiFileSize, int scaleMultiplier,
                                     const char* source = "unknown");

    // Real, exported check for whether `name` is already queued or
    // mid-processing this session (2026-09-28) -- lets a high-volume caller
    // (the bulk pre-cache orchestrator) skip a texture outright instead of
    // retrying a doomed QueueUpscaleJob call against a name that's already
    // in flight for an unrelated, unretryable reason (as opposed to a
    // "queue temporarily full" failure, which IS worth a bounded retry).
    bool IsNameInFlight(const char* name);

    // Real, live session-wide progress counters (2026-09-29) -- backs the
    // status-bar overlay (overlay_hud.cpp's own DrawTextureCacheStatusBar).
    // `queued` = every real job ever accepted by QueueUpscaleJob this
    // session; `processed` = every job the worker has finished (success OR
    // failure) and popped off the queue; `failed` is a subset of
    // `processed`, purely informational. All three are monotonically
    // increasing for the life of the process -- reading them is a plain,
    // lock-free InterlockedExchangeAdd(..., 0)-style snapshot, safe to call
    // every frame from the render thread.
    void GetProgressSnapshot(uint32_t* queued, uint32_t* processed, uint32_t* failed);
}
