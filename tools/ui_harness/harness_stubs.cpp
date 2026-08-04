// harness_stubs.cpp -- satisfies the small set of extern symbols overlay_hud.cpp/
// mod_config.cpp reference that normally come from the real game hooks
// (d3d9_hook.cpp/analog_input_hooks.cpp/dllmain.cpp), none of which exist in this
// standalone harness. Every one of these is either never actually called by the
// Options-menu code path this harness exercises (DrawCustomOptionsMenuIfOpen),
// or has an obviously-safe harness-local answer -- see each stub's own comment.
#include <windows.h>
#include <cstdio>

namespace {
HWND g_harnessWindow = nullptr;
}

void SetHarnessWindow(HWND hwnd)
{
    g_harnessWindow = hwnd;
}

// Real definition (dllmain.cpp) logs to proxy_d3d9.log + OutputDebugString. Harness
// equivalent: stdout + the debugger's Output window, so a run from a terminal or
// from the IDE both show it.
void LogFromController(const char* msg)
{
    printf("%s\n", msg);
    OutputDebugStringA(msg);
    OutputDebugStringA("\n");
}

// Real definition (d3d9_hook.cpp) returns the game's own hooked window -- only used
// by overlay_hud.cpp as a GetClientRect fallback when GetViewport fails, and by
// cursor/hint-slot drawing this harness never calls. Returning our own real window
// keeps that fallback path correct anyway, in case it's ever hit.
extern "C" HWND GetGameWindow()
{
    return g_harnessWindow;
}

// Real definition (d3d9_hook.cpp) tracks real mouse movement for the custom cursor
// overlay -- not exercised by the Options-menu draw path this harness calls.
extern "C" bool GetLastMouseMoveClientPos(int& outX, int& outY)
{
    outX = 0;
    outY = 0;
    return false;
}

// Real definition (analog_input_hooks.cpp) reads a real per-player menu-active gate
// bit -- not read anywhere in the Options-menu draw/tick path itself (that path
// tracks its OWN g_optMenuOpen state instead). Stubbed true since, conceptually,
// this harness only exists to look at menu UI.
extern "C" bool IsMenuActive_Exported()
{
    return true;
}

// Real definitions (analog_input_hooks.cpp) are per-frame housekeeping for glyph-hint
// systems Hook_EndScene drives that this harness doesn't call at all (only
// DrawCustomOptionsMenuIfOpen, via RunCustomOptionsMenuHarnessFrame).
extern "C" void __cdecl InjectSyntheticBackHintIfNeeded() {}
extern "C" void __cdecl ResetMenuListItemOrdinalForFrame() {}

// controller_input.cpp's own "which input method is active" tracking (real
// definitions split across analog_input_hooks.cpp/d3d9_hook.cpp) -- only feeds
// IsControllerActiveInputMethod(), which this harness's Options-menu drawing never
// calls. Stubbed as plain counters so the real logic still runs consistently if
// something ever does call it.
void MarkControllerActivity() {}
extern "C" DWORD GetLastControllerActivityTickMs() { return 0; }
extern "C" DWORD GetLastMouseMoveTickMs() { return 0; }
