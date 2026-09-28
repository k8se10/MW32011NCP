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
    // Queues a real captured source texture for background upscaling, if:
    //  - `name` isn't already queued or mid-processing this session (a
    //    simple in-flight dedup set -- separate from the on-disk cache check
    //    in Hook_ImageFileLoadX64, which only ever catches a PRIOR session's
    //    completed work, not a job already in flight THIS session), and
    //  - the queue has room (a fixed-capacity ring buffer, matching
    //    asset_capture.cpp's own kMaxPendingWrites precedent -- degrades
    //    gracefully by dropping the request, not blocking the caller).
    // Takes ownership of `compressedData` (malloc'd) on success -- the
    // worker thread frees it once processed. On failure (already queued, or
    // queue full), the caller must free it instead.
    //
    // `format` is the real TextureUpscaleIwi::Format the source data is
    // encoded in (DXT1/DXT3/DXT5 only -- other formats are the caller's
    // responsibility to filter out before calling this). `scaleMultiplier`
    // is the real g_modConfig.textureRenderRes value at capture time (2/3/4).
    bool QueueUpscaleJob(const char* name, int format, uint32_t width, uint32_t height,
                          const uint8_t* compressedData, uint32_t compressedSize, int scaleMultiplier);

    // Starts the background worker thread if not already running. Safe to
    // call repeatedly (idempotent) -- QueueUpscaleJob calls this internally,
    // so callers don't need to call it directly in the ordinary case.
    void EnsureWorkerStarted();
}
