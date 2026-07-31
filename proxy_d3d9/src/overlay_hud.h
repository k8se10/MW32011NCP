#pragma once

// overlay_hud — small top-right on-screen text notifications (2026-07-31, user-
// requested QoL: a "MW32011NCP Started" message on launch, and a matching one for
// config hot-reload). Drawn via raw GDI directly onto the D3D9 backbuffer inside an
// EndScene hook -- this project had no confirmed-alive per-frame render hook before
// this (re_notes/known_issues.md issue #37 flagged this as an open blocker: Present
// is confirmed dead, EndScene was untried). No D3DX/font-atlas dependency -- GDI
// TextOut on the backbuffer's own DC is the standard lightweight technique for this,
// avoiding the whole real-glyph-rendering rabbit hole issues #23/#34/#35 are still
// stuck on for in-engine text.

// Called once from Hook_CreateDevice (d3d9_hook.cpp) with the real IDirect3DDevice9*,
// right after the real HAL device is confirmed -- same call site InstallWndProcHook
// already uses. Hooks EndScene via MinHook.
void InstallEndSceneHook(void* realDevice);

// Loads Barlow Condensed SemiBold (regular + italic) as a PRIVATE, in-process-only
// font via AddFontMemResourceEx, from the .ttf data embedded directly in this DLL
// (proxy_d3d9.rc/resource.h) -- so overlay text no longer depends on the real font
// being installed system-wide (previously requested from GDI by face name only,
// which silently substitutes a default font if missing). Call once from DllMain's
// DLL_PROCESS_ATTACH, before any overlay text is ever drawn. Returns false (and logs
// why) if either resource/AddFontMemResourceEx call fails -- CreateFontA's own
// system-font fallback still applies in that case, same graceful-degradation
// behavior as before this change, just no longer the expected path.
bool LoadOverlayFonts(void* selfModuleHandle);

// Call once from DllMain's DLL_PROCESS_DETACH, before closing the log file. Releases
// the private font resource(s) loaded by LoadOverlayFonts (RemoveFontMemResourceEx)
// so GDI never holds a reference into this DLL's own mapped memory after it unloads.
// Safe to call even if LoadOverlayFonts was never called or failed.
void UnloadOverlayFonts();

// Visual flourishes for ShowOverlayMessage, added 2026-07-31 as a "vibes" homage to
// WaW's real documented hidden dev clan tags (re_notes/known_issues.md issue #37 --
// GOLD: solid gold tag; RAIN: animated rainbow scrolling through the tag; CYLN: a red
// "laser"/highlight sweep through the tag, letter by letter). Cheap to add now that
// overlay_hud draws a real textured quad: color variants are just the quad's diffuse
// color (modulated with the white-alpha text texture), and Sweep is one extra
// additive-blended pass of the same texture with a narrow scrolling UV window --
// no new texture or font work needed for any of them.
enum class OverlayAnimStyle { Plain, Gold, Rainbow, Sweep };

// Queues a message to draw top-right for durationMs milliseconds, starting now.
// Replaces any message currently showing (only one at a time -- these are meant to
// be brief, infrequent notifications, not a log feed).
void ShowOverlayMessage(const char* text, unsigned long durationMs, OverlayAnimStyle style = OverlayAnimStyle::Plain);

// STRICTLY A TESTING AID (2026-07-31, [Overlay] TestCycleAllVariants in
// mw3ncp_config.ini, default off). Call every tick (analog_input_hooks.cpp's
// InjectMenuInputTick already does) -- a no-op unless that config toggle is on, in
// which case it continuously cycles through every known message/style variant every
// few seconds, each held for its full interval with no early timeout, so every
// variant can actually be inspected on demand instead of waiting on the real 1-in-20
// startup RNG roll. Never enable the underlying config toggle for normal play.
void TickOverlayTestCycle();
