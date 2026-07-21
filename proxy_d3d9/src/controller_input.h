#pragma once

// XInput polling + stick shaping (radial deadzone + response curve). The game itself
// links no controller API at all (confirmed via re_notes/iw5sp.md) -- we own this
// entirely, loading XInput dynamically so a missing DLL degrades to "no controller"
// instead of crashing the mod.

// Polls once per call; safe to call from multiple hook sites in the same frame (cheap).
// Returns false (and zeroes the outputs) if no controller is connected.
bool Controller_GetLeftStick(float& x, float& y);   // both in [-1, 1], deadzone+curve applied
bool Controller_GetRightStick(float& x, float& y);  // both in [-1, 1], deadzone+curve applied

// Seconds since the last call to this function -- used to convert a continuous stick
// deflection into a per-frame look delta, since (unlike a real mouse) the stick reports
// a position, not an already-frame-scaled delta. CAUTION: the implementation uses a
// single process-wide shared timer, NOT one per call site, despite what that might
// suggest -- calling this from more than one place in the same per-frame tick starves
// every caller but the first to a near-zero delta (whichever call happens first each
// frame resets the shared clock; the next call that frame reads almost no elapsed time).
// Currently only InjectControllerLookAngles() uses this; any other per-frame timing
// need (e.g. sprint stamina drain/regen) should keep its own independent GetTickCount()-
// based timer instead of calling this a second time.
float Controller_DeltaTimeSeconds();

// Raw XInput digital buttons (XINPUT_GAMEPAD_* bitmask) and analog trigger values
// (0-255), unshaped -- for button-mapping work (task #10), not stick movement/look.
// Returns false (zeroing outputs) if no controller is connected.
bool Controller_GetRawButtonsAndTriggers(unsigned short& buttons, unsigned char& leftTrigger, unsigned char& rightTrigger);

// True if a real XInput controller currently responds on slot 0 -- a thin wrapper
// around the same XInputGetState poll every function above already does, exposed
// standalone for call sites (e.g. the bind-resolver glyph-substitution work, task #6)
// that only need a yes/no gate and don't otherwise need stick/button data this frame.
bool Controller_IsConnected();

// Sets the controller's two rumble motors directly (task #17). leftMotor/rightMotor
// are normalized [0, 1] -- clamped internally, so an out-of-range caller can't send a
// bogus value to XInputSetState. Same controller slot (index 0) as every other XInput
// call in this codebase. Safe to call every frame; XInputSetState itself is cheap and
// idempotent for an unchanged value. No-ops (does not error) if no controller is
// connected or XInput failed to load.
void Controller_SetVibration(float leftMotor, float rightMotor);
