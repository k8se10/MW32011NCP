#pragma once

// menu_render -- Phase 1 (2026-08-16) static renderer for parsed real .menu content
// (menu_parser.h). Lives under tools/ui_harness/ui_hot/, never shipped in the real
// proxy_d3d9.dll -- see menu_parser.h's own header comment for why STL is fine here.
//
// Coordinate model: mirrors the REAL engine exactly (RE'd + live-capture-verified
// this session, see re_notes/iw5sp.md's "## .menu itemDef/menuDef rect-to-screen
// transform" and "### Live capture closes the last open question" sections) --
// every real .menu rect is defined in a 640x480 virtual space, transformed into a
// FIXED 1920x1080 logical canvas via `screen = virtual * 2.25 + margin[mode]`
// (confirmed live for horz/vertMode 1/2/3; modes 0/7/8/9/10 are best-effort
// placeholders, modes 4/6 stretch-fill are structurally understood but unverified --
// see MenuRender_TransformRect's own comment). That fixed 1920x1080 logical image is
// then uniformly letterbox-scaled to whatever real window size the harness is
// actually drawing into, exactly like the real engine's own separate later upscale
// step -- so a position/size calibrated here transfers directly to real gameplay
// regardless of the player's actual display resolution.

#include "menu_parser.h"

// Loads and parses `path` (a real .menu file, e.g. an absolute path into
// D:\Tools\OpenAssetTools\zone_dump\ui\...) as the currently active menu to render.
// Returns false (and clears the active menu) on parse failure. Safe to call again to
// switch files.
bool MenuRender_LoadFile(const char* path);

// True if a menu file is currently loaded (regardless of whether it parsed any
// menuDef blocks -- an empty-but-successfully-parsed file still counts as "loaded").
bool MenuRender_IsLoaded();

// Draws every menuDef/itemDef in the currently loaded file this frame, letterboxed
// into the real device's current backbuffer/window size. No-op if nothing is loaded.
// Call once per frame from the harness's own draw entry point (Hot_DrawFrame or a
// dedicated new export -- see ui_hot/exports.cpp).
void MenuRender_DrawFrame(void* device);

// Applies the confirmed real-engine rect transform (virtual 640x480 rect ->
// 1920x1080 logical-canvas screen rect) for one rect + horz/vertMode pair. Exposed
// separately from MenuRender_DrawFrame so it can be sanity-checked/logged
// independently (e.g. against the live-captured example values in re_notes/iw5sp.md)
// without needing a real device/window.
void MenuRender_TransformRect(float virtualX, float virtualY, float virtualW, float virtualH,
                                int horzMode, int vertMode,
                                float& outScreenX, float& outScreenY, float& outScreenW, float& outScreenH);

// Phase 2 (2026-08-17): the fake game-state `exp`/`visible` expressions evaluate
// against. Plain mutable struct (menu_expr.h) -- the harness (main.cpp) edits its
// fields directly via a handful of hotkeys to exercise real conditional .menu logic
// (team-locked items, already-unlocked attachments, etc.) without a real match
// running. Persists across file switches/hot-reloads (owned by ui_hot.dll's own
// globals, same lifetime as the rest of menu_render's state).
MenuGameState& MenuRender_GetGameState();

// Re-writes menu_parse_debug.txt for the currently loaded file against the CURRENT
// MenuGameState -- call after mutating MenuRender_GetGameState()'s fields (e.g. from
// a harness hotkey) to check an evaluated visible/exp result from text output without
// a screenshot. No-op if nothing is loaded.
void MenuRender_RefreshDebugReport();
