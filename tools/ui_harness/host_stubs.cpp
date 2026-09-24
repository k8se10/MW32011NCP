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
#include "dualsense_input.h" // DualSenseRawState, DualSense_* signatures, Controller_DetectGlyphStyle
#include "frame_benchmark.h" // FrameBenchmark_AddRumbleMs/AddPollThreadMs
#include "mod_config.h"      // ModConfig, g_modConfig

// 2026-09-24: controller_input.cpp gained real calls to Controller_DetectGlyphStyle
// (glyph-style auto-detection), FrameBenchmark_AddRumbleMs/AddPollThreadMs (per-thread
// benchmark instrumentation, issue #87), and a direct g_modConfig read/write in its own
// XInputPollThreadProc -- none of which this stub file had been updated for, breaking
// this host exe's link (LNK2019 x4, found while investigating an unrelated set of
// real CodeQL alerts). Same "not called by anything we need, stub it out" rationale as
// every other symbol in this file -- Controller_DetectGlyphStyle returns its own
// fallback unchanged (this harness has no real DualSense hardware path either way, see
// the DualSense_* stubs below), the two FrameBenchmark_Add* calls are real no-ops here
// (this harness doesn't write frametime_benchmark.csv), and g_modConfig needs a real,
// single definition somewhere for the linker -- this host exe provides it, using the
// struct's own default member initializers (same defaults the real mod ships with).
ModConfig g_modConfig;

GlyphStyle Controller_DetectGlyphStyle(GlyphStyle fallback) { return fallback; }
void FrameBenchmark_AddRumbleMs(double /*ms*/) {}
void FrameBenchmark_AddPollThreadMs(double /*ms*/) {}

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
