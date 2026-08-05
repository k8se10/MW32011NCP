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
#include "overlay_hud.h"
#include "mod_config.h"

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

} // extern "C"
