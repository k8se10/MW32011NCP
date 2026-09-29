// vram_diag.h -- real, driver-authoritative GPU VRAM diagnostic via DXGI's
// IDXGIAdapter3::QueryVideoMemoryInfo (Windows 10+), 2026-09-23.
//
// Replaces reliance on IDirect3DDevice9::GetAvailableTextureMemory (a legacy,
// widely-known-unreliable D3D9 API most drivers report conservatively through --
// see overlay_hud.cpp's own [vram-diag] comment) with the same real per-process
// VRAM budget/usage accounting Windows' own Task Manager GPU memory tab and
// tools like GPU-Z use. Real motivation: direct user report that observed VRAM
// usage never exceeds ~3.8GB even at InternalRenderScalePercent=300 (true 8K
// internal rendering), which should need far more -- worth checking against an
// authoritative number rather than continuing to reason from the unreliable
// legacy API or from this project's own CPU-side resource-diag (process commit,
// not GPU memory at all). See known_issues_x64.md issue #4 for the full trail.
#pragma once

// Call from a low-frequency point (this project's convention: once per real
// frame from Hook_EndScene, matching the existing CreateTexture-storm and
// legacy vram-diag call sites). Internally rate-limited to roughly once per
// second; a safe, fast no-op otherwise. Lazily creates and caches a DXGI
// factory/adapter on first real call -- never torn down, matching this
// project's "install once, never uninstall" background-resource convention.
void LogRealVramDiagIfDue();

// Real, on-demand VRAM query (2026-09-29) -- same cached DXGI adapter/
// QueryVideoMemoryInfo call LogRealVramDiagIfDue already uses, exposed as a
// direct accessor for callers that need a real, current budget/usage reading
// right now (e.g. a memory-safety gate deciding whether it's safe to keep
// creating more GPU resources), not just a periodic log line. Returns false
// (outputs left untouched) if the real DXGI query isn't available/fails --
// callers should treat that as "unknown," not "safe" or "unsafe."
bool TryGetRealVramInfo(double* outBudgetMB, double* outUsageMB);

// Same real, on-demand query, but for the NON_LOCAL segment group -- the
// real, distinct segment a discrete-GPU system's HOST-VISIBLE (shared
// system-memory) heap draws its budget from. See this function's own
// implementation comment (vram_diag.cpp) for the real, twice-repeated
// incident (DXVK's own "Heap 1" hitting 99% full while the LOCAL segment
// sat at 57%) that made this a real, necessary second check, not just
// belt-and-suspenders. Returns false (outputs untouched) if unavailable
// (e.g. a unified-memory system with no real NON_LOCAL segment) --
// callers should treat that as "unknown," not "safe" or "unsafe," same as
// TryGetRealVramInfo's own contract.
bool TryGetRealNonLocalVramInfo(double* outBudgetMB, double* outUsageMB);
