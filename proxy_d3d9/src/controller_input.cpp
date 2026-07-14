#include "controller_input.h"

#include <windows.h>
#include <xinput.h>   // struct definitions only -- resolved dynamically below, never linked
#include <cmath>

namespace {

typedef DWORD(WINAPI* XInputGetState_t)(DWORD, XINPUT_STATE*);

XInputGetState_t g_XInputGetState = nullptr;
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

bool Controller_GetLeftStick(float& x, float& y)
{
    x = 0.0f; y = 0.0f;
    EnsureLoaded();
    if (!g_XInputGetState) return false;

    XINPUT_STATE state{};
    if (g_XInputGetState(0, &state) != ERROR_SUCCESS) return false;

    ShapeStick(state.Gamepad.sThumbLX, state.Gamepad.sThumbLY, kLeftDeadzone, x, y);
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
    return true;
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
