// dll_stubs.cpp -- satisfies the small set of extern symbols overlay_hud.cpp/
// mod_config.cpp reference that normally come from the real game hooks
// (d3d9_hook.cpp/analog_input_hooks.cpp/dllmain.cpp), none of which exist in this
// standalone harness. Lives in ui_hot.dll (not the host exe) since overlay_hud.cpp/
// mod_config.cpp are compiled into this DLL, not the host, under the hot-reload
// architecture (tools/ui_harness/README.md).
//
// Every stub here is either never actually called by the Options-menu code path
// this harness exercises (DrawCustomOptionsMenuIfOpen), or has an obviously-safe
// harness-local answer -- see each one's own comment.
#include <windows.h>
#include <cstdio>

namespace {
HWND g_harnessWindow = nullptr;
}

// Exported (exports.cpp) so the host can tell this DLL which real window it's
// drawing into, once per load -- window handle itself doesn't change across a
// hot-reload (the host keeps the same window; only this DLL gets swapped).
void SetHarnessWindow(HWND hwnd)
{
    g_harnessWindow = hwnd;
}

// Real definition (dllmain.cpp) logs to proxy_d3d9.log + OutputDebugString. Harness
// equivalent: stdout + the debugger's Output window.
void LogFromController(const char* msg)
{
    printf("[ui_hot] %s\n", msg);
    OutputDebugStringA(msg);
    OutputDebugStringA("\n");
}

// Real definition (d3d9_hook.cpp) returns the game's own hooked window -- only used
// by overlay_hud.cpp as a GetClientRect fallback when GetViewport fails, and by
// cursor/hint-slot drawing this harness never calls.
extern "C" HWND GetGameWindow()
{
    return g_harnessWindow;
}

// Real definition (d3d9_hook.cpp) captures WM_MOUSEMOVE specifically, because the
// real game's GetCursorPos-vs-window-coordinate-space can disagree under DPI
// virtualization (see that function's own comment). This harness owns its window
// outright with no such concern, so real-time GetCursorPos+ScreenToClient is exactly
// as accurate and needs no message-capture plumbing of its own -- this IS the real
// mouse position the Options screen's new click support (2026-08-04) hit-tests
// against, not a stand-in.
extern "C" bool GetLastMouseMoveClientPos(int& outX, int& outY)
{
    if (!g_harnessWindow) { outX = 0; outY = 0; return false; }
    POINT p;
    if (!GetCursorPos(&p)) { outX = 0; outY = 0; return false; }
    ScreenToClient(g_harnessWindow, &p);
    outX = p.x;
    outY = p.y;
    return true;
}

// Real definition (d3d9_hook.cpp) tracks real WM_LBUTTONDOWN/UP. Same reasoning as
// GetLastMouseMoveClientPos above -- this harness can just poll the real live key
// state directly, no message-capture plumbing needed for its own self-owned window.
extern "C" bool IsLeftMouseButtonHeld()
{
    return (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
}

// Real definition (analog_input_hooks.cpp) reads a real per-player menu-active gate
// bit -- not read anywhere in the Options-menu draw/tick path itself (that path
// tracks its OWN g_optMenuOpen state instead).
extern "C" bool IsMenuActive_Exported()
{
    return true;
}

// Real definitions (analog_input_hooks.cpp) are per-frame housekeeping for glyph-hint
// systems Hook_EndScene drives that this harness doesn't call at all (only
// DrawCustomOptionsMenuIfOpen, via RunCustomOptionsMenuHarnessFrame).
extern "C" void __cdecl InjectSyntheticBackHintIfNeeded() {}
extern "C" void __cdecl ResetMenuListItemOrdinalForFrame() {}

// Real definition (controller_input.cpp, which lives in the HOST exe, not this DLL,
// since input polling is "platform" code that never needs hot-reloading) -- only
// used by DrawCustomCursorIfNeeded, never called by DrawCustomOptionsMenuIfOpen.
bool IsControllerActiveInputMethod()
{
    return false;
}
