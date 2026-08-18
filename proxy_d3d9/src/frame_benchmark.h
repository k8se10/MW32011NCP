#pragma once

#include <windows.h> // UINT/DWORD, used by FrameBenchmark_AddCreateTextureCall's signature

// Opt-in per-frame benchmark CSV logger -- see mod_config.h's
// frametimeBenchmarkLogging field comment for the full rationale: live-reported
// "still jittery, 239fps on the counter but feels like 40" after two real,
// evidence-backed fixes (proxy_d3d9.log truncate-on-launch, asset-capture async
// disk write) didn't resolve it, and MSI Afterburner/RTSS's own frametime graph
// shows nothing abnormal -- which argues against a generic render-thread stall a
// tool like that would normally catch. This answers it with real per-frame data
// from INSIDE this project's own hooks instead of another guess: writes
// frametime_benchmark.csv (next to the DLL), one row per frame, with the real
// frame-to-frame time (QueryPerformanceCounter, measured at the same per-frame
// cadence -- Hook_EndScene -- RTSS itself uses) plus a breakdown of how much of
// that frame went into each of this project's own suspect sections.
//
// Every function here is a safe, cheap no-op when
// g_modConfig.frametimeBenchmarkLogging is off -- call sites don't need their own
// guard, matching this project's established "toggleable diagnostic" convention
// (e.g. HudGlyphPositionLogging, ArmorFieldScanLogging).

// Called from controller_input.cpp's Controller_SetVibration and
// asset_capture.cpp's hooks -- adds this call's own measured duration to the
// CURRENT frame's running total for that bucket. Both call sites run on the
// main/render thread only (confirmed: Controller_SetVibration is documented as
// "deliberately still called directly on the CALLING thread," and asset_capture's
// hooks fire from wherever the real game calls CreateTexture/FindOrLoadAsset,
// which for menu/material loading is the main thread) -- no locking, would need
// adding if that ever changes.
void FrameBenchmark_AddRumbleMs(double ms);
void FrameBenchmark_AddAssetCaptureMs(double ms);

// 2026-08-17, second pass -- live data from the first pass showed a real, tightly
// regular alternating stall pattern near end-of-session that doesn't match the
// mid-session level-load burst's irregular magnitudes, and this project's own
// asset-capture CreateTexture hook intercepts EVERY texture creation on the real
// device -- including this mod's OWN glyph-icon/hint-text textures (overlay_hud.cpp
// creates those via the SAME real device vtable slot this hook sits on), not just
// the real engine's material loads. The REAL CreateTexture call's own duration was
// deliberately excluded from AddAssetCaptureMs above (treated as "the engine's
// cost, not ours") -- but if THIS mod's own glyph/hint code is what's actually
// triggering repeated real texture creation (a cache-invalidation bug would fit
// "became noticeable post-glyphs-update, post-0.2.5" exactly), that real cost was
// invisible until now. Called from asset_capture.cpp's Hook_CreateTexture for
// EVERY real CreateTexture call, regardless of capture-depth/tracking state.
void FrameBenchmark_AddRealCreateTextureCall(double ms, UINT width, UINT height, DWORD format);

// Called once per frame from Hook_EndScene, after all of this frame's drawing is
// done, with however much time each already-measured section took. Folds in
// whatever rumble/asset-capture time was accumulated via the Add* functions above
// since the last call (then resets those accumulators to 0 for the next frame),
// computes this frame's real frame-to-frame time, and writes one CSV row.
void FrameBenchmark_LogFrame(double optionsMenuMs, double overlayMessageMs,
                              double glyphIconMs, double hintSlotsMs);
