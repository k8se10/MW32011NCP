#pragma once

#include <cstdint>

// Real raw-capture staging for the standalone TextureCacheBuilder tool
// (2026-10-01). Direct conclusion from this session's own root-cause chain:
// the live DLL's organic capture path (Hook_ImageFileLoadX64) safely gets
// real, correct raw texture bytes from the game's own real asset loader --
// that part was never the problem. What WAS the problem (and what this
// closes) is handing those bytes off to the live ncnn-based upscale worker,
// which does real, heavy GPU/CPU work (and real native API churn) IN the
// game process. Direct instruction, following the external-tool pivot:
// "id rather us use the existing known asset list as base alone with
// caputring also unknown assets in gameplay" -- the known basemap drives
// PROGRESS TRACKING only (never forced loading, the confirmed root cause of
// the original pool-exhaustion crash), and organic, real-gameplay capture
// (known names AND genuinely new/unknown ones alike) is the only sanctioned
// way real texture bytes ever leave the game process. This module is the
// other half of that: instead of queueing a real, heavy upscale job
// in-process, it just writes the already-captured raw bytes to a plain
// staging folder -- a small, synchronous, already-in-memory-to-disk copy,
// not meaningfully heavier than this project's own existing synchronous
// cache-read path (TextureUpscaleCache::TryLoadCachedUpscaledIwi). The
// standalone TextureCacheBuilder exe (tools/texture_cache_builder/) is the
// only thing that ever processes these staged files -- fully out-of-process,
// on the player's own schedule, never live during a session.
namespace TextureCaptureStaging
{
    // Writes a real, complete, raw (NOT upscaled) IWI file -- exactly what
    // Hook_ImageFileLoadX64 already captured from the game's own real asset
    // loader -- to <gameDir>\texture_capture_staging\<sanitized name>.iwi.
    // A no-op (returns true) if that name is already staged this session --
    // the same real texture can be captured many times across one play
    // session (shared materials, revisited areas), and there's no reason to
    // re-write the same bytes to disk repeatedly. Returns false only on a
    // real I/O failure (logged internally); never fatal to the caller, same
    // "a failed cache-population step degrades gracefully" standard this
    // project's own upscale pipeline already follows.
    bool WriteRawCapture(const char* name, const uint8_t* data, uint32_t size);
}
