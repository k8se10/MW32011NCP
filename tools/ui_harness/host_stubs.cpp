// host_stubs.cpp -- satisfies controller_input.cpp's own small set of extern
// symbols (real definitions split across analog_input_hooks.cpp/d3d9_hook.cpp/
// overlay_hud.cpp/dualsense_input.cpp in the real mod), which only feed
// IsControllerActiveInputMethod() and the background XInput/DualSense poll thread's
// own bookkeeping -- not called by anything main.cpp or the hot-swapped ui_hot.dll
// actually needs.
//
// 2026-08-17: controller_input.cpp's DualSense-preference branch (added 2026-08-16,
// issue #76 follow-up) pulled in GetLastKnownRenderDevice/ShowOverlayMessage
// (overlay_hud.h) and the whole DualSense_* family (dualsense_input.h) as NEW extern
// dependencies this file was never updated for -- broke this host exe's link
// entirely (LNK2019 x7, discovered while building tools/ui_harness for the .menu
// renderer, unrelated to that feature itself). This host has never needed real
// DualSense/render-device access (it's not the real game, there's no real D3D9
// device from the GAME's perspective, only this harness's own standalone one main.cpp
// creates directly) -- stubbed exactly like every other symbol in this file already
// is, same "not called by anything we need" rationale.
#include <windows.h>
#include <cstdio>
#include "overlay_hud.h"    // OverlayAnimStyle, GetLastKnownRenderDevice's own signature
#include "dualsense_input.h" // DualSenseRawState, DualSense_* signatures

void MarkControllerActivity() {}
extern "C" DWORD GetLastControllerActivityTickMs() { return 0; }
extern "C" DWORD GetLastMouseMoveTickMs() { return 0; }

void* GetLastKnownRenderDevice() { return nullptr; }
void ShowOverlayMessage(const char* /*text*/, unsigned long /*durationMs*/, OverlayAnimStyle /*style*/) {}

bool DualSense_EnsureOpen() { return false; }
bool DualSense_IsOpen() { return false; }
bool DualSense_Poll(DualSenseRawState& /*outState*/) { return false; }
unsigned short DualSense_ToXInputButtons(const DualSenseRawState& /*state*/) { return 0; }
bool DualSense_HasGyro() { return false; }
bool DualSense_SetVibration(uint8_t /*leftMotor*/, uint8_t /*rightMotor*/) { return false; }

// controller_input.cpp also logs XInput load failures via this -- host's own
// separate copy from ui_hot.dll's dll_stubs.cpp (the two never share a definition
// across the module boundary).
void LogFromController(const char* msg)
{
    printf("[ui_harness] %s\n", msg);
    OutputDebugStringA(msg);
    OutputDebugStringA("\n");
}
