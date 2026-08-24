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
// Only Isotherm Sans is bundled -- mod_config.h's overlayFontFamily lets a player
// point this project's text draws at any OTHER system-installed font by name
// instead (see that field's own comment), so a second bundled alternative isn't
// needed the way Barlow briefly was.
#define IDR_FONT_ISOTHERMSANS        101
#define IDR_FONT_ISOTHERMSANS_ITALIC 102
