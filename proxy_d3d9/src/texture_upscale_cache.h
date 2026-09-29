#pragma once

#include <cstdint>

// Runtime AI texture-upscale-cache feature -- on-disk cache lookup/storage.
// See re_notes/x64_migration/texture_upscale_cache_research.md for the full
// design record. This module owns: where cache files live, how a real image
// name maps to a cache filename, and reading/writing those files. It does
// NOT own the substitution hooks (Hook_ImageFileLoadX64 et al.) or the
// upscale pipeline itself (ncnn inference, DXT decode/encode) -- those are
// separate, not-yet-built pieces that call into this module.
//
// No-STL, malloc-based (matching asset_capture.cpp's own established
// shipped-code convention).
namespace TextureUpscaleCache
{
    // Call once before any Lookup/Store call. Idempotent, safe to call
    // repeatedly (matches asset_capture.cpp's own EnsureOutputDir pattern) --
    // creates <gameDir>\texture_upscale_cache\ if it doesn't already exist.
    bool EnsureCacheDir();

    // A cache entry is keyed by the real image name (as read from
    // param_1+0x20 at the FUN_1401bae80 hook) plus the scale multiplier
    // currently configured (TextureRenderRes) -- changing the scale factor
    // naturally invalidates old entries by simply not matching any existing
    // filename, no explicit invalidation pass needed.
    //
    // scaleMultiplier is the real integer multiplier (e.g. 4 for "4x", parsed
    // from mod_config.h's own TextureRenderRes string elsewhere) -- kept as a
    // plain int here, not a string, so this module doesn't need to know
    // mod_config's own string-parsing details.

    // Synchronous read -- deliberately not backgrounded. A cache HIT here
    // replaces a real synchronous disk/archive read the game's own
    // FUN_1401bae80 would otherwise do at this exact call site anyway (the
    // real param_2 read-callback chain, already traced in this project's own
    // research, is itself fully synchronous) -- so this read carries no new
    // stall risk beyond what native code already does at this point. Only
    // the WRITE side (StoreUpscaled, below) involves genuinely slow work
    // (ncnn inference) and must be backgrounded.
    //
    // On a hit, returns a malloc'd buffer (caller must free()) containing the
    // real, complete, ready-to-substitute .iwi file bytes (as produced by
    // TextureUpscaleIwi::EncodeIwi8 when this entry was written). On a miss
    // or any read failure, returns nullptr and leaves *outSize at 0 --
    // callers must treat this identically (fall through to the real,
    // original image load) in both cases, never distinguish "miss" from
    // "corrupt cache file" behaviorally beyond logging.
    uint8_t* TryLoadCachedUpscaledIwi(const char* imageName, int scaleMultiplier, uint32_t* outSize);

    // Lightweight existence check (2026-09-29) -- a plain GetFileAttributesA
    // call, never a real file open/read. For callers that only need to know
    // WHETHER a cache entry exists, not its contents (e.g. the session-
    // continuity basemap's own startup coverage count, which checks up to
    // ~17,000 names and can't afford a real read per name).
    bool CacheEntryExists(const char* imageName, int scaleMultiplier);

    // Writes a complete, already-encoded .iwi buffer (as produced by
    // TextureUpscaleIwi::EncodeIwi8) to the cache under the same
    // name+scaleMultiplier key TryLoadCachedUpscaledIwi will later look up.
    // Deliberately NOT implemented as fire-and-forget disk I/O on the calling
    // thread -- the real upscale pipeline that calls this (not yet built)
    // must run on its own background thread already (ncnn inference is far
    // too slow for any hook's own call path), so this function itself stays
    // a plain synchronous write FROM that already-background thread, mirroring
    // asset_capture.cpp's own background-write-worker precedent rather than
    // adding a second, redundant queue on top of one that will already exist.
    // Returns false on any failure (logged internally); the caller should
    // treat a failed store as non-fatal -- worst case, the same image gets
    // re-upscaled next time it's requested.
    bool StoreUpscaledIwi(const char* imageName, int scaleMultiplier, const uint8_t* iwiData, uint32_t iwiSize);

    // ---- Custom bundled asset overrides (2026-09-28) -------------------------
    // A genuinely separate concept from the upscale cache above, sharing only
    // the same real substitution mechanism (Hook_ImageFileLoadX64/
    // Hook_ReadBytesSubstitutionX64) -- these are real, fixed, BUNDLED assets
    // shipped with the mod itself (e.g. the custom main menu background),
    // never generated at runtime, never scale-keyed, never written by
    // StoreUpscaledIwi. Kept as a distinct directory/function pair rather
    // than overloading the upscale cache's own (name, scale) key space with
    // a sentinel scale value, for real code clarity -- these are different
    // things and should read as different things.

    // Synchronous read, same "no new stall risk" reasoning as
    // TryLoadCachedUpscaledIwi above -- a hit here replaces the exact same
    // real synchronous read the game would otherwise do. Looks under
    // <gameDir>\custom_assets\<sanitized name>.iwi. Returns a malloc'd
    // buffer (caller must free()) on a hit, nullptr on a miss/failure.
    uint8_t* TryLoadCustomAsset(const char* imageName, uint32_t* outSize);
}
