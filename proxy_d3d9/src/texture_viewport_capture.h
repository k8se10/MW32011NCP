#pragma once

#include <windows.h>
#include <cstdint>

// Viewport-triggered texture capture (2026-09-29) -- the third real capture
// path for the texture-upscale-cache feature, added alongside runtime
// capture-on-load (analog_input_hooks_x64.cpp) and bulk file-based pre-cache
// (texture_precache_orchestrator.cpp). Direct instruction: "i do think we
// should chase the viewport side to potentially surface these textures at
// runtime too so that way we can do both precache and live cache and read."
//
// Real gap this closes: the existing load-time capture correlates a real
// asset NAME to raw file bytes at the moment Hook_ImageFileLoadX64 sees a
// load -- but it only fires for whatever routes through that one specific
// loader function. This module instead triggers off the real
// IDirect3DDevice9::SetTexture call -- i.e. a texture actually being BOUND
// and DRAWN this frame -- which is a genuinely independent signal from "was
// it loaded via the one hooked loader function." Two real pieces:
//
//   1. A name<->D3D-texture-pointer correlation table, populated from
//      asset_capture.cpp's own existing Hook_CreateTexture (already
//      intercepts EVERY real CreateTexture call) whenever a "currently
//      loading" name is active (set by Hook_ImageFileLoadX64 for the
//      duration of its own real call-through).
//   2. A new Hook_SetTexture: on the first real bind of a correlated,
//      not-yet-handled texture, captures its REAL, CURRENT GPU-resident
//      block data via LockRect (same technique asset_capture.cpp's own
//      DDS-writing code already uses) and queues it directly to
//      TextureUpscaleWorker -- deferred to first actual USE rather than
//      firing unconditionally at creation, so textures that get created but
//      never rendered (culled LOD variants, etc.) don't cost real upscale
//      work for nothing.
namespace TextureViewportCapture
{
    // Called by Hook_ImageFileLoadX64 (analog_input_hooks_x64.cpp) right
    // before its own real call-through, and cleared right after. `wasHit`
    // is true when this load was served from the cache/custom_assets
    // substitution path -- the created texture will already be the
    // upscaled/overridden result, so OnCreateTexture below records it as
    // already-handled rather than a real capture candidate.
    void SetCurrentlyLoadingName(const char* name, bool wasHit);
    void ClearCurrentlyLoadingName();

    // Called unconditionally from asset_capture.cpp's own Hook_CreateTexture,
    // for every real CreateTexture call on the device -- a pure, cheap,
    // in-memory correlation-table insert; does no real capture work itself.
    void OnCreateTexture(void* texturePtr, UINT width, UINT height, DWORD format);

    // Installs the real IDirect3DDevice9::SetTexture hook. SP-only (same
    // per-exe signature-verification policy as the rest of this feature --
    // this hooks a stable, standard COM vtable slot, not a signature-scanned
    // game function, so it's actually exe-agnostic in practice, but kept
    // behind the same SP-only call site as the rest of this feature for
    // consistency). Call once, after the real device is confirmed (same
    // call site AssetCapture_InstallHookIfEnabled already uses).
    void InstallSetTextureHook(void* realDevice);
}
