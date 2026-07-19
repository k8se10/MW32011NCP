#pragma once

// rumble.h -- controller vibration output (task #17). Kept as its own module, separate
// from controller_input.cpp's XInput polling and analog_input_hooks.cpp's per-frame
// gameplay-input translation, per CLAUDE.md's "keep hook plumbing and gameplay logic
// separate" rule.
//
// No native vibration/rumble infrastructure exists in this build at all (confirmed via
// a clean zero-hit string search for "rumble"/"vibrat"/"forcefeedback" -- see
// re_notes/known_issues.md issue #24 / re_notes/iw5sp.md's "Vibration/rumble trigger
// points" section). Output is entirely our own XInputSetState calls; this module hooks
// two real, disassembly-confirmed native choke points to know WHEN to trigger them:
//   - FUN_004895b0(entity, eventHandle, paramCount) -- the general native notify
//     dispatcher. FUN_0045e320 (the per-shot fire-effects handler) calls this with the
//     real "weapon_fired" event handle once per real shot, semi-auto and full-auto
//     alike -- confirmed plain __cdecl via raw disassembly (flat stack args, bare RET).
//   - FUN_0044cdb0(eventHandle, entity, ..., damageAmount, ...) -- the richer native
//     notify dispatcher, called with the real "damage" event handle whenever any
//     damageable entity takes damage; the damage amount is the function's own 6th
//     parameter, directly usable for intensity scaling. Also confirmed plain __cdecl
//     via raw disassembly.
// Both hooks filter to the LOCAL player only via the same per-entity "has a client
// struct" field (+0x10c, non-null) FUN_005BC9A0's notifyonplayercommand registration
// already gates on (see known_issues.md issue #29) -- in solo SP/Survival this is
// equivalent to "is this entity the local player," since only real client entities
// (not AI) have a non-null client-struct pointer there. NOT scoped to specifically
// exclude a co-op partner in 2-player Survival -- see the .cpp for the honest caveat.

// Installs the two native hooks. Call once, after MinHook itself is initialized (same
// point in the startup sequence as every other hook in this project).
void Rumble_Install();

// Call once per real gameplay frame (from the same tick InjectAllControllerInput
// already runs on) to decay any active rumble toward zero and push the current motor
// state to the controller. Menu-tick-only periods (paused, in a menu) don't need this --
// rumble is a gameplay-feedback feature, not a UI one.
void Rumble_Tick();
