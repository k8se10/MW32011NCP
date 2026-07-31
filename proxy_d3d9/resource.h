#pragma once

// Resource IDs for the fonts embedded directly into this DLL (see proxy_d3d9.rc).
// Bundled so the on-screen overlay (overlay_hud.cpp) never depends on Barlow
// Condensed being installed system-wide -- previously it was requested from GDI by
// face name only, which silently substitutes a default font if the real one isn't
// present on the user's machine (see re_notes/known_issues.md issue #47's own
// follow-up note on this gap). Barlow Condensed is SIL Open Font License 1.1
// licensed (see assets/fonts/BarlowCondensed-OFL.txt); embedding it in a
// distributed application is explicitly permitted under that license. See
// README.md's Credits section for the required attribution.
#define IDR_FONT_BARLOWCONDENSED_SEMIBOLD        101
#define IDR_FONT_BARLOWCONDENSED_SEMIBOLD_ITALIC 102
