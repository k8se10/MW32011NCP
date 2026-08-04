// options_render_suppress.h -- full suppression of the real Options menu's own
// rendering, for the custom Options replacement screen (re_notes/known_issues.md
// issue #66, task #11/#21). Full research trail (why this needs 2 specific hooks,
// not a menu-level visibility flag -- none was found -- and why hooking is safe here
// unlike the disabled 2026-08-01 registry-search hook) is in
// re_notes/options_menu_full_map.md sec 13.
//
// Design note: while the replacement screen is open, the real underlying Options
// menu is NEVER driven to switch tabs (our own screen reads/writes settings
// directly via real_settings.h/vanilla_settings_sync.h regardless of which real tab
// happens to be open underneath) -- so suppressing exactly the ONE real menuDef
// pointer that was open at the moment our screen took over is sufficient. No other
// real menu (main menu, pause menu, popups, HUD) is ever affected.
#pragma once

// Marks `menuPtr` as the real menuDef whose rendering should be skipped from now on.
// Pass nullptr to stop suppressing (render everything normally again). Safe to call
// every frame with the same value; only takes effect for the two hooked render
// functions below.
void SetSuppressedMenuPointer(void* menuPtr);

// Installs the two real per-frame menu-paint entry-point hooks (0x0050b740 layer 0,
// 0x004a4150 layer 1 -- each confirmed via disassembly to have exactly one real
// caller, so gating on the menuDef pointer argument's identity is precise with no
// return-address gating needed, unlike the cursor-suppression hook precedent). Call
// once from InstallAnalogInputHooks(), after MH_Initialize(). Completely inert
// (never skips anything) until SetSuppressedMenuPointer is called with a non-null
// pointer.
void InstallOptionsRenderSuppressionHooks();
