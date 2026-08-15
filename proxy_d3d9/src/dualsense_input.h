#pragma once

// Raw-HID DualSense (PS5 controller) backend -- 2026-08-11, "better controller/device
// support" pass (re_notes/known_issues.md issue #76).
//
// Why this exists: this project's ENTIRE controller layer (controller_input.cpp) was,
// until now, XInput-only. A DualSense has no native XInput driver on Windows -- Sony
// never licensed XInput compatibility -- so the only way it was ever visible to this
// mod was via a third-party translator (Steam Input, DS4Windows, DSX) presenting a
// virtual XInput device. Real Nexus feedback (known_issues.md issue #74, Biactyk)
// confirmed Steam Input's translation is unreliable for this project specifically
// ("Controller Input is not passed through unless Steam Input is off"), and the user
// separately confirmed they want native gyro-aim support, which Steam Input's own
// gyro-to-mouse remapping would otherwise be the only way to get -- so the fix is to
// stop depending on Steam Input at all for this device family: open the DualSense's
// raw HID interface directly and parse its real input report ourselves, the same way
// this project already owns its own XInput polling instead of trusting anything else
// to hand it clean input.
//
// PREVIEW/WIP (per this project's own labeling convention, feedback_preview_wip_labeling):
// implemented against a real, working, MIT-licensed reference implementation
// (github.com/Ohjurot/DualSense-Windows, DS5_Input.cpp/IO.cpp) for the exact byte
// offsets and VID/PID, cross-checked against community RE docs -- NOT against this
// project's own hardware, since no DualSense is available to the developer. Gyro/accel
// are raw, uncalibrated sensor units (the reference implementation itself never solved
// real calibration either -- its own comment says exactly that), scaled by a plain
// tunable Sensitivity multiplier rather than a claimed deg/s conversion this project
// can't verify. Axis sign/orientation for pitch/yaw is a best-effort guess (config
// toggles exist to flip it without a rebuild) -- needs a live tester with real
// hardware before any of this is called done, per this project's own
// Production Readiness Criteria (CODE_STANDARDS.md).

#include <cstdint>

// One controller-frame's worth of parsed DualSense state. Buttons/dpad use the SAME
// bit values the reference implementation (DS5State.h) defines -- kept as this
// project's own constants below rather than pulling in that project's header, since
// this is a from-scratch implementation informed by (not copied from) that reference.
struct DualSenseRawState
{
    // Signed, already stick-centered per this project's own convention (DS5_Input.cpp's
    // own math: raw byte - 128, one direction inverted for Y) -- NOT yet deadzone/curve
    // shaped; ShapeStick() (controller_input.cpp) is reused for that, same as XInput.
    int16_t leftStickX = 0, leftStickY = 0;
    int16_t rightStickX = 0, rightStickY = 0;
    uint8_t leftTrigger = 0, rightTrigger = 0; // 0-255, matches XINPUT_GAMEPAD's own range

    // Raw report bytes 8/9/10 (USB offsets, see dualsense_input.cpp) -- translated to an
    // XINPUT_GAMEPAD_*-shaped bitmask by DualSense_ToXInputButtons() below, not exposed
    // as these raw DualSense-specific bit meanings, so every existing consumer in this
    // codebase (button mapping, glyph detection, etc.) never needs to know DualSense
    // exists at all.
    uint8_t buttonsAndDpad = 0;
    uint8_t buttonsA = 0;
    uint8_t buttonsB = 0;

    // Raw IMU samples -- signed 16-bit, uncalibrated (see this file's own header
    // comment). Axis identity (which physical rotation each of X/Y/Z corresponds to)
    // is per Sony's own DualSense orientation convention as documented by the
    // reference implementation, not independently verified against real hardware.
    int16_t gyroX = 0, gyroY = 0, gyroZ = 0;
    int16_t accelX = 0, accelY = 0, accelZ = 0;

    bool connected = false;
};

// Lazily enumerates + opens a DualSense over raw HID (VID 0x054C, PID 0x0CE6 -- Sony's
// real, confirmed DualSense USB identifiers) if one isn't already open. Safe to call
// every poll iteration; a real open only happens once, cached until the device is
// unplugged/fails to read. Returns false if no DualSense is present or the open failed
// -- never crashes/throws, matches this project's "missing hardware degrades to no
// controller" standard already established for XInput (controller_input.cpp's
// EnsureLoaded).
bool DualSense_EnsureOpen();

// Blocking read of exactly one input report from the currently-open device (matches
// this project's existing background-poll-thread design -- see
// controller_input.cpp's XInputPollThreadProc, which this backend is polled from
// alongside XInput, not on a separate thread). Returns false (and closes the device,
// so the next DualSense_EnsureOpen() retries a fresh open) if the read fails, e.g. the
// controller was unplugged.
bool DualSense_Poll(DualSenseRawState& outState);

// Translates DualSenseRawState's buttons/dpad into the exact same bit values
// XINPUT_GAMEPAD::wButtons uses (XINPUT_GAMEPAD_A, XINPUT_GAMEPAD_DPAD_UP, etc.) --
// letting every existing consumer of Controller_GetRawButtonsAndTriggers stay
// completely unaware DualSense exists. PS/mute/touchpad-click have no XInput
// equivalent (the real XINPUT_GAMEPAD_GUIDE bit isn't exposed through the public
// XInput API either) and are silently dropped, same as a real Xbox controller's own
// Guide button already is to this codebase.
unsigned short DualSense_ToXInputButtons(const DualSenseRawState& state);

// Returns whether the currently-open DualSense (if any) can supply gyro data this
// poll -- distinct from Controller_IsConnected() because an XInput-connected pad
// (including a Steam Input virtual one) has no gyro at all, and callers (the gyro-look
// injection) need to know specifically whether real gyro data is available, not just
// whether *a* controller is connected.
bool DualSense_HasGyro();
