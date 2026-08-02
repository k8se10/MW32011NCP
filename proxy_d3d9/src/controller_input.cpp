#include "controller_input.h"

#include <windows.h>
#include <xinput.h>   // struct definitions only -- resolved dynamically below, never linked
#include <cmath>

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
    // xinput9_1_0.dll ships on every Windows Vista+ install (widest compatibility);
    // GetState's ABI is identical across all XInput DLL versions.
    HMODULE h = LoadLibraryA("xinput9_1_0.dll");
    if (!h) return;
    g_XInputGetState = reinterpret_cast<XInputGetState_t>(GetProcAddress(h, "XInputGetState"));
    g_XInputSetState = reinterpret_cast<XInputSetState_t>(GetProcAddress(h, "XInputSetState"));
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
    if (g_XInputGetState(0, &state) != ERROR_SUCCESS) return false;

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
    if (g_XInputGetState(0, &state) != ERROR_SUCCESS) return false;

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
    if (g_XInputGetState(0, &state) != ERROR_SUCCESS) return false;

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
    return g_XInputGetState(0, &state) == ERROR_SUCCESS;
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
    g_XInputSetState(0, &vib);
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
