#pragma once

// Runtime material/texture capture for tools/ui_harness's .menu renderer -- see
// mod_config.h's captureRuntimeMenuAssets field comment for the full rationale
// (2026-08-17: the static OpenAssetTools extraction under zone_dump\ui\materials\/
// images\ is real but incomplete -- 310/301 files -- and some real material names
// are almost certainly procedurally generated at runtime and can never be extracted
// statically no matter how thorough a re-extraction is). Entirely gated behind
// g_modConfig.captureRuntimeMenuAssets; every function here is a safe no-op when
// that's off, and NEVER alters the real CreateTexture call's own behavior/return
// value even when on -- this is a read-only observer, same standard as every other
// diagnostic hook in this project (e.g. analog_input_hooks.cpp's Hook_004dfd30).
//
// Mechanism: FUN_004ff000 (FindOrLoadAsset, already RE'd/documented in
// re_notes/iw5sp.md -- plain __cdecl(int assetType, const char* name, int flag),
// no implicit-register risk) is hooked in analog_input_hooks.cpp; while a
// assetType==5 (material) load is on the stack, this file's PushMaterialName/
// PopMaterialName track the real name being loaded. Separately, this file's own
// CreateTexture device-vtable hook (installed alongside overlay_hud.cpp's existing
// EndScene/Reset hooks, same "one real device for this game's lifetime" pattern)
// captures the resulting IDirect3DTexture9's pixel data to disk, name-keyed off
// whatever material name was on top of the stack at that moment -- a "correlate by
// call-order" approach, not a perfect single hook point (the real per-image loader
// several levels deeper turned out to route through implicit-register/hash-table
// code not safe to hook blind -- see re_notes/iw5sp.md's own account of that dead
// end). A WRONG name association would be worse than none, so this only captures
// while a material load is confirmed on the stack, never speculatively.

// Returns true if the name was actually pushed (false if capturing is off, or the
// fixed-depth stack is already full) -- the caller MUST only call
// AssetCapture_PopMaterialName() when this returned true, or a push silently
// dropped at the depth cap would let its matching pop decrement someone ELSE's
// still-legitimately-active stack entry, desyncing the whole stack for any deeper
// caller. (In practice the deepest real chain documented in re_notes/iw5sp.md is
// 2-3 levels, far under the cap -- this pairing discipline is defensive, not
// expected to matter, but costs nothing to get right.)
bool AssetCapture_PushMaterialName(const char* name);
void AssetCapture_PopMaterialName();

// Installs the CreateTexture hook on the real device. No-op (does nothing, logs
// nothing) if g_modConfig.captureRuntimeMenuAssets is false. Safe to call every
// time a device is (re)created -- internally a "one install for this device"
// guard, matching InstallEndSceneHook's own convention.
void AssetCapture_InstallHookIfEnabled(void* realDevice);
