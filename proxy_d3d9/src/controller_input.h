#pragma once

// XInput polling + stick shaping (radial deadzone + response curve). The game itself
// links no controller API at all (confirmed via re_notes/iw5sp.md) -- we own this
// entirely, loading XInput dynamically so a missing DLL degrades to "no controller"
// instead of crashing the mod.

// Polls once per call; safe to call from multiple hook sites in the same frame (cheap).
// Returns false (and zeroes the outputs) if no controller is connected.
bool Controller_GetLeftStick(float& x, float& y);   // both in [-1, 1], deadzone+curve applied
bool Controller_GetRightStick(float& x, float& y);  // both in [-1, 1], deadzone+curve applied

// Seconds since the last call to this function (for this call site) -- used to convert
// a continuous stick deflection into a per-frame look delta, since (unlike a real mouse)
// the stick reports a position, not an already-frame-scaled delta.
float Controller_DeltaTimeSeconds();
