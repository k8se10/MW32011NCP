#pragma once

// Resource IDs for the fonts embedded directly into this DLL (see proxy_d3d9.rc).
// Bundled so the on-screen overlay (overlay_hud.cpp) never depends on the default
// font being installed system-wide -- requested from GDI by face name only, which
// silently substitutes a default font if the real one isn't present on the user's
// machine (see re_notes/known_issues.md issue #47's own follow-up note on this gap,
// originally written about Barlow Condensed, the font this project bundled before
// the 2026-08-24 switch to Isotherm Sans). Isotherm Sans is SIL Open Font License
// 1.1 licensed (see assets/fonts/IsothermSans-OFL.txt); embedding it in a
// distributed application is explicitly permitted under that license. See
// README.md's Credits section for the required attribution (also covering Manrope,
// the upstream font Isotherm Sans is derived from).
//
// TWO Isotherm Sans variants are bundled, 2026-08-24 -- live-reported the initial
// single-variant swap didn't fit every context ("condensed only works for the
// throwback prompt and turrets etc, the normal ui text looks better for stuff like
// buy stations, pickups and interacts"). "Isotherm Sans UI" is the general default
// (mod_config.h's overlayFontFamily); "Isotherm Sans" (Condensed) is explicitly
// requested only by the gameplay hint call sites that want it (throwback, sentry
// gun placement -- see analog_input_hooks.cpp). Both font FILES' own internal
// family name was originally "Isotherm Sans" (the UI style is NOT a separate GDI
// family by default, just a different .ttf under the same name, so loading both
// as-is would collide -- GDI can't tell them apart via CreateFontA's family-name
// lookup with two Regular-style faces registered under one name). Fixed by
// renaming the UI variant's name-table entries (nameID 1/4/6/16, via fonttools) to
// "Isotherm Sans UI" before embedding -- confirmed live (a standalone GDI harness,
// both loaded simultaneously) that CreateFontA then resolves each family to the
// correct, distinct face. IsothermSans-UI.ttf/-UI-Italic.ttf in assets/fonts/ are
// this project's own renamed copies, not the upstream repo's originals -- the
// glyph outlines/hinting are untouched, only the name table changed.
//
// mod_config.h's overlayFontFamily still lets a player point this project's text
// draws at any OTHER system-installed font by name too (see that field's own
// comment) -- these two bundled variants are just the compiled-in defaults.
#define IDR_FONT_ISOTHERMSANS           101
#define IDR_FONT_ISOTHERMSANS_ITALIC    102
#define IDR_FONT_ISOTHERMSANS_UI        103
#define IDR_FONT_ISOTHERMSANS_UI_ITALIC 104
