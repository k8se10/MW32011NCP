// exports.cpp -- the plain C ABI boundary ui_hot.dll exposes to the host exe
// (tools/ui_harness/main.cpp). extern "C" + __declspec(dllexport) disables C++ name
// mangling, so the host can GetProcAddress each of these by its exact plain name
// with no .def file and no mangled-name guessing -- a real .def file (see
// proxy_d3d9/def/proxy_d3d9.def) is only needed when the EXPORTED name must match
// something external (real d3d9.dll's own export table); here we control both
// sides, so this is simpler and just as robust.
//
// Deliberately thin: every one of these just forwards to the real, unmodified
// function overlay_hud.cpp/mod_config.cpp already define -- no logic of its own,
// so there's nothing here that could itself drift from what the real proxy DLL
// does.
#include <windows.h>
#include <cstdio>
#include "overlay_hud.h"
#include "mod_config.h"
#include "menu_render.h"

void SetHarnessWindow(HWND hwnd); // dll_stubs.cpp

extern "C" {

__declspec(dllexport) void Hot_SetWindow(void* hwnd)
{
    SetHarnessWindow(static_cast<HWND>(hwnd));
}

__declspec(dllexport) bool Hot_LoadOverlayFonts(void* selfModuleHandle)
{
    return LoadOverlayFonts(selfModuleHandle);
}

__declspec(dllexport) void Hot_UnloadOverlayFonts()
{
    UnloadOverlayFonts();
}

__declspec(dllexport) void Hot_LoadModConfig()
{
    LoadModConfig();
}

__declspec(dllexport) bool Hot_TickInput(bool openRequestedEdge,
    bool upEdge, bool downEdge, bool leftEdge, bool rightEdge,
    bool selectEdge, bool backEdge, bool tabPrevEdge, bool tabNextEdge)
{
    return CustomOptionsMenu_TickInput(openRequestedEdge, upEdge, downEdge, leftEdge, rightEdge,
                                         selectEdge, backEdge, tabPrevEdge, tabNextEdge);
}

__declspec(dllexport) bool Hot_IsOpen()
{
    return CustomOptionsMenu_IsOpen();
}

__declspec(dllexport) void Hot_ResetOnMenuClose()
{
    CustomOptionsMenu_ResetOnMenuClose();
}

__declspec(dllexport) void Hot_DrawFrame(void* device)
{
    RunCustomOptionsMenuHarnessFrame(device);
}

// Harness-only diagram anchor editor (2026-08-05) -- see overlay_hud.h's own
// comment on DiagramEditor_ToggleEditMode for why these exist only here, never
// called from the real game.
__declspec(dllexport) void Hot_ToggleDiagramEditMode()
{
    DiagramEditor_ToggleEditMode();
}

__declspec(dllexport) void Hot_ExportDiagramLayout()
{
    DiagramEditor_ExportCurrentLayout();
}

// ---- Real .menu renderer (2026-08-16/17, Phase 1) ---------------------------------
// See menu_parser.h/menu_render.h for the full design. Kept as separate exports
// (not folded into Hot_TickInput/Hot_DrawFrame above) so the host can freely load/
// switch .menu files and draw them independently of the unrelated custom Options
// screen those functions already drive -- both can coexist in the same harness
// window since MenuRender_DrawFrame is a no-op whenever nothing's loaded.
__declspec(dllexport) bool Hot_LoadMenuFile(const char* path)
{
    return MenuRender_LoadFile(path);
}

__declspec(dllexport) bool Hot_IsMenuLoaded()
{
    return MenuRender_IsLoaded();
}

__declspec(dllexport) void Hot_DrawMenuFrame(void* device)
{
    MenuRender_DrawFrame(device);
}

// ---- Phase 2 (2026-08-17) -- fake MenuGameState hotkeys ---------------------------
// See menu_expr.h's MenuGameState comment and main.cpp's key handling for which real
// keys drive these. Deliberately harness-only test-scenario toggles (same "never
// called from the real game" category as the diagram editor above), not general
// GameState API -- kept here rather than in menu_render.cpp so that file stays
// focused on rendering, matching this project's existing exports.cpp boundary role.
__declspec(dllexport) void Hot_MenuGameState_ToggleTeam()
{
    MenuGameState& s = MenuRender_GetGameState();
    s.teamName = (s.teamName == "TEAM_ALLIES") ? "TEAM_AXIS" : "TEAM_ALLIES";
    MenuRender_RefreshDebugReport();
}

__declspec(dllexport) void Hot_MenuGameState_ToggleMatchRules()
{
    MenuGameState& s = MenuRender_GetGameState();
    s.usingMatchRulesData = !s.usingMatchRulesData;
    MenuRender_RefreshDebugReport();
}

__declspec(dllexport) void Hot_MenuGameState_AdvanceClock()
{
    MenuGameState& s = MenuRender_GetGameState();
    s.fakeMillisBase += 1000.0;
    MenuRender_RefreshDebugReport();
}

// Toggles a broad "everything's unlocked/selected" test preset -- sets a wide range of
// getplayercardinfo(a,b,c)-shaped keys (group indices 0/8, item indices 0-19, sub-field
// 0-19 -- covers the real "0_0_7"/"8_0_7"-style keys survival_armory_weapon.menu's
// FUNC_23-28 pattern reads for weapon/attachment ownership+equipped checks) to a common
// nonzero test value. NOT a claim of matching the real engine's exact per-attachment ID
// scheme -- FUNC_8() (team-based equip-id selector) makes exact replication depend on
// values this harness has no source for -- this is a blunt "does toggling game state
// visibly change which items show as unlocked/equipped" smoke test, good enough to
// verify the exp/visible evaluation pipeline actually works end-to-end on a real file.
__declspec(dllexport) void Hot_MenuGameState_ToggleUnlockedPreset()
{
    static bool unlocked = false;
    unlocked = !unlocked;
    MenuGameState& s = MenuRender_GetGameState();
    double val = unlocked ? 1.0 : 0.0;
    for (int group = 0; group <= 8; group += 8) {
        for (int item = 0; item < 20; ++item) {
            for (int field = 0; field < 20; ++field) {
                char key[32];
                sprintf_s(key, "%d_%d_%d", group, item, field);
                s.playerCardInfo[key] = val;
            }
        }
    }
    // Phase 3: also drives the real Survival-armory level/ownership gates (see
    // MenuGameState's own comment) -- same toggle now covers both gating mechanisms
    // the corpus actually uses, not just the getplayercardinfo-shaped one Phase 2 wired.
    s.fakeExperience = unlocked ? 99000.0 : 0.0;
    s.fakeOwnsEverything = unlocked;
    MenuRender_RefreshDebugReport();
}

// Phase 3 (2026-08-17): survival_armory_weapon.menu's real weapon rows are NOT a
// feeder/listbox (confirmed by reading the actual file -- see re_notes/iw5sp.md's
// Phase 3 notes) -- they're hand-unrolled static itemDefs (WEAPON_POPUP_3..10), each
// one hardcoding a DIFFERENT literal CSV row index into its own tablelookup() calls,
// with `exp rect y` giving each one a distinct index-based vertical offset via pure
// arithmetic (no CSV needed for position). That means real per-row DATA content (which
// weapon, cost, locked state) for THOSE specific 8 rows is entirely driven by their own
// hardcoded row indices already, not by any MenuGameState toggle -- there is nothing to
// "select" for that specific file. `selected_item_index` (used by a differently-scoped
// icon/material lookup earlier in the same file, line ~464/467) is the one place this
// file DOES key off a variable index -- exposed here for completeness/future files that
// might use the same pattern more broadly, not because the 8 popup rows need it.
__declspec(dllexport) void Hot_MenuGameState_NextItemIndex()
{
    MenuGameState& s = MenuRender_GetGameState();
    s.localVarNum["selected_item_index"] += 1.0;
    MenuRender_RefreshDebugReport();
}

__declspec(dllexport) void Hot_MenuGameState_PrevItemIndex()
{
    MenuGameState& s = MenuRender_GetGameState();
    double& v = s.localVarNum["selected_item_index"];
    if (v > 0.0) v -= 1.0;
    MenuRender_RefreshDebugReport();
}

__declspec(dllexport) void Hot_MenuGameState_RefreshDebugReport()
{
    MenuRender_RefreshDebugReport();
}

} // extern "C"
