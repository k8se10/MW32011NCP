#include "controller_input.h"

#include <windows.h>
#include <xinput.h>   // struct definitions only -- resolved dynamically below, never linked
#include <cmath>
#include <cstdio>
#include <cstdlib> // std::abs(int) -- XInputStateHasActivity's stick-magnitude check
#include "overlay_hud.h" // ShowOverlayMessage -- connect/disconnect notifications, see
                          // NotifyControllerConnectionChange's own header comment below

extern void LogFromController(const char* msg); // defined in dllmain.cpp

namespace {

typedef DWORD(WINAPI* XInputGetState_t)(DWORD, XINPUT_STATE*);
typedef DWORD(WINAPI* XInputSetState_t)(DWORD, XINPUT_VIBRATION*);

XInputGetState_t g_XInputGetState = nullptr;
XInputSetState_t g_XInputSetState = nullptr;
bool g_triedLoad = false;

// XInput's own documented deadzone constants (thumbstick, not trigger).
constexpr float kLeftDeadzone = static_cast<float>(XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE) / 32767.0f;
constexpr float kRightDeadzone = static_cast<float>(XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE) / 32767.0f;

// Response curve exponent -- >1 gives finer control near center (console-shooter feel),
// 1.0 would be perfectly linear. Not user-tunable yet (task #6 options screen will
// expose this); a reasonable default for now.
constexpr float kCurveExponent = 1.6f;

void EnsureLoaded()
{
    if (g_triedLoad) return;
    g_triedLoad = true;
    // Issue #24 follow-up (2026-08-03): xinput9_1_0.dll -- the "legacy" DLL this
    // project originally loaded for its widest-compatibility guarantee (ships on
    // every Windows Vista+ install with zero extra dependencies) -- is a documented,
    // deliberately cut-down compatibility shim: on real Windows installs its own
    // XInputSetState either isn't exported at all or is a silent no-op, since it
    // predates/bypasses the "full" XInput redistributable vibration support
    // entirely. This is a well-known XInput gotcha, not specific to this project --
    // live-confirmed here as the actual root cause of "vibration hook fires clean,
    // zero crash, but no physical rumble ever happens" once the rumble feature
    // itself (issue #24) was finally reimplemented and needed a REAL SetState.
    // GetState's ABI/behavior is identical and fine across every XInput DLL version,
    // so this only matters for vibration -- fixed by trying the full-featured DLLs
    // FIRST (xinput1_4, Windows 8+; xinput1_3, the older DirectX-redist version)
    // and falling back to xinput9_1_0 last, so a system with either of the real
    // DLLs available gets working vibration, and a system with neither still gets
    // the exact same GetState-only behavior this project already had and relied on.
    const char* kCandidateDlls[] = { "xinput1_4.dll", "xinput1_3.dll", "xinput9_1_0.dll" };
    HMODULE h = nullptr;
    const char* loadedDllName = nullptr;
    for (const char* dllName : kCandidateDlls) {
        h = LoadLibraryA(dllName);
        if (h) { loadedDllName = dllName; break; }
    }
    if (!h) {
        LogFromController("[xinput] LoadLibrary FAILED for xinput1_4/xinput1_3/xinput9_1_0 -- no controller input or vibration this session");
        return;
    }
    g_XInputGetState = reinterpret_cast<XInputGetState_t>(GetProcAddress(h, "XInputGetState"));
    g_XInputSetState = reinterpret_cast<XInputSetState_t>(GetProcAddress(h, "XInputSetState"));
    char buf[160];
    sprintf_s(buf, "[xinput] loaded %s -- GetState=%s SetState=%s",
        loadedDllName, g_XInputGetState ? "OK" : "MISSING", g_XInputSetState ? "OK" : "MISSING");
    LogFromController(buf);
}

// Scaled radial deadzone: rescales the post-deadzone range back to [0,1] smoothly,
// instead of just clamping (which would leave a "dead click" feel right at the
// deadzone edge). Then applies the response curve, preserving sign per axis.
void ShapeStick(SHORT rawX, SHORT rawY, float deadzone, float& outX, float& outY)
{
    float x = rawX / 32768.0f;
    float y = rawY / 32768.0f;
    float mag = std::sqrt(x * x + y * y);

    if (mag < deadzone) {
        outX = 0.0f;
        outY = 0.0f;
        return;
    }

    float normalizedMag = (mag - deadzone) / (1.0f - deadzone);
    if (normalizedMag > 1.0f) normalizedMag = 1.0f;
    float curved = std::pow(normalizedMag, kCurveExponent);

    // Reapply the curved magnitude along the original direction.
    outX = (x / mag) * curved;
    outY = (y / mag) * curved;
}

LARGE_INTEGER g_qpcFrequency{};
bool g_qpcInit = false;

int g_activeXInputSlot = 0;

// User-requested (2026-08-08, alongside the multi-slot scan below): surface
// controller connect/disconnect through this project's existing on-screen toast
// (issue #47's `ShowOverlayMessage`, same mechanism as the startup/config-reload
// messages) instead of silently changing behavior with no visible feedback --
// unplugging/replugging a controller mid-session (or a low battery cutting a
// wireless pad) should be an obvious, safe, non-crashing event, not a silent
// "why did my glyphs disappear" mystery. `g_connectionStateKnown` gates the
// very first check specifically so a fresh launch with no controller connected
// yet doesn't show a confusing "Disconnected" toast before one was ever known
// to be connected -- only a genuine CHANGE fires a message, in either direction.
bool g_connectionStateKnown = false;
bool g_controllerCurrentlyConnected = false;

void NotifyControllerConnectionChange(bool nowConnected)
{
    if (g_connectionStateKnown && nowConnected == g_controllerCurrentlyConnected) return;
    bool wasKnown = g_connectionStateKnown;
    g_connectionStateKnown = true;
    g_controllerCurrentlyConnected = nowConnected;
    if (!wasKnown && !nowConnected) return; // first-ever check, nothing was ever connected -- no toast
    ShowOverlayMessage(nowConnected ? "Controller Connected" : "Controller Disconnected", 3000);
    LogFromController(nowConnected ? "[xinput] controller connected" : "[xinput] controller disconnected");
}

// A little above XInput's own documented thumbstick deadzone constants (already
// used for real input shaping above) -- deliberately coarser, since this is only
// asking "is a HUMAN actually touching this pad right now," not shaping a real
// movement value, so idle analog drift/noise shouldn't ever count as activity.
constexpr SHORT kSlotActivityStickThreshold = 6000;
constexpr BYTE kSlotActivityTriggerThreshold = 10;

bool XInputStateHasActivity(const XINPUT_STATE& state)
{
    const XINPUT_GAMEPAD& g = state.Gamepad;
    if (g.wButtons != 0) return true;
    if (g.bLeftTrigger > kSlotActivityTriggerThreshold || g.bRightTrigger > kSlotActivityTriggerThreshold) return true;
    if (std::abs(static_cast<int>(g.sThumbLX)) > kSlotActivityStickThreshold || std::abs(static_cast<int>(g.sThumbLY)) > kSlotActivityStickThreshold) return true;
    if (std::abs(static_cast<int>(g.sThumbRX)) > kSlotActivityStickThreshold || std::abs(static_cast<int>(g.sThumbRY)) > kSlotActivityStickThreshold) return true;
    return false;
}

void LogActiveSlotChange(int fromSlot, int toSlot)
{
    char buf[96];
    sprintf_s(buf, "[xinput] active controller slot changed %d -> %d", fromSlot, toSlot);
    LogFromController(buf);
}

// Scans all 4 real XInput user indices for a connected controller instead of
// assuming slot 0. Live-reported 2026-08-08 (Nexus, v0.3.1): several players see
// no controller-glyph icons at all -- even on English, with default settings --
// and it's NOT reproducible on the developer's own machine, pointing at a real
// per-environment cause rather than a universal regression. Every read in this
// file (movement, buttons, vibration, "is a controller even connected") was
// hardcoded to XInput user index 0 -- a controller that Windows/Steam assigns
// to a different slot (a second pad plugged in, a tool like x360ce occupying
// slot 0 with its own virtual device while the real physical pad lands
// elsewhere, Steam Input's own passthrough renumbering, etc.) would make every
// one of those calls report ERROR_DEVICE_NOT_CONNECTED forever, identical to
// "no controller at all," even with a real, working controller connected.
//
// User-requested follow-up: also handle MULTIPLE legitimate controllers
// connected at once correctly, not just "find any one pad" -- if the current
// slot is connected but sitting idle while a DIFFERENT connected slot is
// actively showing real button/stick/trigger input, that's a strong signal a
// human is holding THAT one, not the idle one, so this follows the activity
// rather than latching onto whichever slot merely happened to be found first.
// Only falls back to "just pick a connected slot" when nothing anywhere is
// currently showing activity (e.g. right at launch, before the player has
// touched anything yet). Sticks with the current slot when it's still both
// connected AND the only one showing activity (or nothing is), so this never
// flip-flops between two idle-but-connected pads on its own.
int ResolveActiveXInputSlot()
{
    if (!g_XInputGetState) return g_activeXInputSlot;

    XINPUT_STATE currentState{};
    bool currentConnected = g_XInputGetState(static_cast<DWORD>(g_activeXInputSlot), &currentState) == ERROR_SUCCESS;
    if (currentConnected && XInputStateHasActivity(currentState)) {
        NotifyControllerConnectionChange(true);
        return g_activeXInputSlot; // actively in use -- no reason to look anywhere else
    }

    int firstConnectedOther = -1;
    for (int slot = 0; slot < 4; ++slot) {
        if (slot == g_activeXInputSlot) continue;
        XINPUT_STATE state{};
        if (g_XInputGetState(static_cast<DWORD>(slot), &state) != ERROR_SUCCESS) continue;
        if (firstConnectedOther < 0) firstConnectedOther = slot;
        if (XInputStateHasActivity(state)) {
            // Someone's actively using THIS slot right now -- switch to it even
            // though the current slot might also still be genuinely connected
            // (the real "multiple legitimate controllers" case).
            LogActiveSlotChange(g_activeXInputSlot, slot);
            g_activeXInputSlot = slot;
            NotifyControllerConnectionChange(true);
            return slot;
        }
    }

    if (currentConnected) {
        // Still connected, just idle right now, and nothing else showed live
        // activity either -- keep it rather than flip-flopping onto some other
        // merely-connected pad.
        NotifyControllerConnectionChange(true);
        return g_activeXInputSlot;
    }
    if (firstConnectedOther >= 0) {
        // Current slot genuinely disconnected; nothing anywhere showed live
        // activity yet, but at least one other slot IS connected -- fall back to
        // it (matches this project's original "assume a controller exists"
        // behavior, just now actually finding whichever slot has one).
        LogActiveSlotChange(g_activeXInputSlot, firstConnectedOther);
        g_activeXInputSlot = firstConnectedOther;
        NotifyControllerConnectionChange(true);
        return g_activeXInputSlot;
    }

    // Nothing connected on any of the 4 slots -- a real disconnect (or nothing
    // was ever plugged in yet, filtered out inside NotifyControllerConnectionChange
    // itself).
    NotifyControllerConnectionChange(false);
    return g_activeXInputSlot;
}

} // namespace

// BUG-001 follow-up (2026-08-02): centralized here rather than at each of this
// project's ~17 call sites for these two getters. Live-reported regression from the
// first attempt: the cursor stayed visible even during controller-driven MENU
// navigation ("until gameplay") because MarkControllerActivity() had only been added
// to the gameplay-tick functions (InjectControllerMovement/Buttons), which halt
// during menus/pause -- menu-nav functions run via the always-on WndProc/timer tick
// and read these SAME getters, so marking activity HERE instead covers every caller,
// present and future, without relying on remembering to add the call at each one
// individually (the exact class of mistake CLAUDE.md's own Hold-Breath/Fire
// bind-index lesson warns about for "must be distinct" constants -- same principle
// applies to "every reader of real input must mark activity").
extern void MarkControllerActivity(); // defined in analog_input_hooks.cpp
extern "C" DWORD GetLastControllerActivityTickMs(); // defined in analog_input_hooks.cpp
extern "C" DWORD GetLastMouseMoveTickMs(); // defined in d3d9_hook.cpp

// See controller_input.h's own comment on IsControllerActiveInputMethod for the
// rationale (shared by the cursor overlay and the glyph-hint overlays). Same
// recency-window-then-comparison logic already live-proven for the cursor
// (overlay_hud.cpp's DrawCustomCursorIfNeeded, BUG-001/#55): controller counts as
// the active method outright if used within the last kRecentControllerActivityMs,
// otherwise whichever of controller/(deadzone-filtered) mouse movement is more
// recent wins.
bool IsControllerActiveInputMethod()
{
    constexpr DWORD kRecentControllerActivityMs = 300;
    DWORD lastController = GetLastControllerActivityTickMs();
    if (GetTickCount() - lastController < kRecentControllerActivityMs) return true;
    return lastController > GetLastMouseMoveTickMs();
}

bool Controller_GetLeftStick(float& x, float& y)
{
    x = 0.0f; y = 0.0f;
    EnsureLoaded();
    if (!g_XInputGetState) return false;

    XINPUT_STATE state{};
    if (g_XInputGetState(static_cast<DWORD>(ResolveActiveXInputSlot()), &state) != ERROR_SUCCESS) return false;

    ShapeStick(state.Gamepad.sThumbLX, state.Gamepad.sThumbLY, kLeftDeadzone, x, y);
    if (x != 0.0f || y != 0.0f) MarkControllerActivity();
    return true;
}

bool Controller_GetRightStick(float& x, float& y)
{
    x = 0.0f; y = 0.0f;
    EnsureLoaded();
    if (!g_XInputGetState) return false;

    XINPUT_STATE state{};
    if (g_XInputGetState(static_cast<DWORD>(ResolveActiveXInputSlot()), &state) != ERROR_SUCCESS) return false;

    ShapeStick(state.Gamepad.sThumbRX, state.Gamepad.sThumbRY, kRightDeadzone, x, y);
    if (x != 0.0f || y != 0.0f) MarkControllerActivity();
    return true;
}

bool Controller_GetRawButtonsAndTriggers(unsigned short& buttons, unsigned char& leftTrigger, unsigned char& rightTrigger)
{
    buttons = 0; leftTrigger = 0; rightTrigger = 0;
    EnsureLoaded();
    if (!g_XInputGetState) return false;

    XINPUT_STATE state{};
    if (g_XInputGetState(static_cast<DWORD>(ResolveActiveXInputSlot()), &state) != ERROR_SUCCESS) return false;

    buttons = state.Gamepad.wButtons;
    leftTrigger = state.Gamepad.bLeftTrigger;
    rightTrigger = state.Gamepad.bRightTrigger;
    if (buttons != 0 || leftTrigger != 0 || rightTrigger != 0) MarkControllerActivity();
    return true;
}

bool Controller_IsConnected()
{
    EnsureLoaded();
    if (!g_XInputGetState) return false;
    XINPUT_STATE state{};
    return g_XInputGetState(static_cast<DWORD>(ResolveActiveXInputSlot()), &state) == ERROR_SUCCESS;
}

void Controller_SetVibration(float leftMotor, float rightMotor)
{
    EnsureLoaded();
    if (!g_XInputSetState) return;

    if (leftMotor < 0.0f) leftMotor = 0.0f;
    if (leftMotor > 1.0f) leftMotor = 1.0f;
    if (rightMotor < 0.0f) rightMotor = 0.0f;
    if (rightMotor > 1.0f) rightMotor = 1.0f;

    XINPUT_VIBRATION vib{};
    vib.wLeftMotorSpeed = static_cast<WORD>(leftMotor * 65535.0f);
    vib.wRightMotorSpeed = static_cast<WORD>(rightMotor * 65535.0f);
    g_XInputSetState(static_cast<DWORD>(ResolveActiveXInputSlot()), &vib);
}

float Controller_DeltaTimeSeconds()
{
    if (!g_qpcInit) {
        QueryPerformanceFrequency(&g_qpcFrequency);
        g_qpcInit = true;
    }
    static LARGE_INTEGER lastTime{};
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    if (lastTime.QuadPart == 0) {
        lastTime = now;
        return 0.0f;
    }
    float dt = static_cast<float>(now.QuadPart - lastTime.QuadPart) / static_cast<float>(g_qpcFrequency.QuadPart);
    lastTime = now;
    // Guard against absurd values (e.g. first call after a long stall/breakpoint).
    if (dt < 0.0f || dt > 0.25f) dt = 0.0f;
    return dt;
}
