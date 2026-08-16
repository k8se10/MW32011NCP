#pragma once

// rumble.h -- controller vibration output (task #17, reimplemented 2026-08-03 --
// see re_notes/known_issues.md issue #24 for the full history). Kept as its own
// module, separate from controller_input.cpp's XInput polling and
// analog_input_hooks.cpp's per-frame gameplay-input translation, per CLAUDE.md's
// "keep hook plumbing and gameplay logic separate" rule.
//
// No native vibration/rumble infrastructure exists in this build at all (confirmed
// via a clean zero-hit string search for "rumble"/"vibrat"/"forcefeedback" -- see
// re_notes/iw5sp.md's "Vibration/rumble trigger points" section). Output is entirely
// our own XInputSetState calls.
//
// FIRE: a real hook, found via a runtime byte-pattern scan (never a hardcoded
// address, per CLAUDE.md), on FUN_0045e320 (the per-shot fire-effects handler) --
// its own decompiled 2-parameter signature was cross-checked against the raw
// disassembly of its ONE real call site (exactly 2 real PUSH instructions
// immediately precede the CALL, matching exactly) before being trusted.
//
// DAMAGE: deliberately NOT a hook. The original implementation hooked a generic
// multi-purpose native notify dispatcher directly and crashed the game at startup
// (some other real caller almost certainly passed a different real argument shape
// than the fixed hook signature assumed). The documented "safer" candidate
// (FUN_0045f770, a single-purpose damage-application function) was re-verified this
// session via the same raw-disassembly rigor -- and confirmed to have the EXACT
// SAME problem one layer deeper: its 14 real call sites push between 6 and 11
// arguments, not a consistent count. It is NOT hooked. Instead, damage is detected
// via a per-frame poll of the local player's own real health field, comparing
// against the previous frame's value -- a real decrease is damage; increases
// (regen/perks/pickups) and implausibly large single-frame drops (checkpoint/
// respawn resets, not real hits) are explicitly filtered out. See rumble.cpp's own
// PollDamageRumble() for the full detail.
//
// Both fire and damage filter to the LOCAL player only via the same per-entity "has
// a client struct" field (+0x10c, non-null) FUN_005BC9A0's notifyonplayercommand
// registration already gates on (see known_issues.md issue #29) -- in solo
// SP/Survival this is equivalent to "is this entity the local player," since only
// real client entities (not AI) have a non-null client-struct pointer there. NOT
// scoped to specifically exclude a co-op partner in 2-player Survival -- see the
// .cpp for the honest caveat.

// Installs the fire-rumble hook (signature-scanned, MinHook) and logs the outcome.
// Call once, after MinHook itself is initialized (same point in the startup
// sequence as every other hook in this project). Damage rumble needs no
// installation step -- it's driven entirely by Rumble_Tick()'s own per-frame poll.
void Rumble_Install();

// Call once per real gameplay frame (from the same tick InjectAllControllerInput
// already runs on) to: poll for real damage taken this frame, decay any active
// rumble toward zero, and push the current motor state to the controller.
void Rumble_Tick();

// Live report (2026-08-16): "vibration can tend to get stuck on". Root cause:
// InjectAllControllerInput (and therefore Rumble_Tick above) lives on the
// gameplay-simulation tick, which a genuine pause halts entirely (the SAME
// documented dead-tick problem InjectMenuInputTick's own big comment in
// analog_input_hooks.cpp already solved for pause-menu input). A rumble event
// triggered right before a pause -- or a hit taken the instant before a death/
// loading transition freezes simulation -- never gets its own already-scheduled
// per-event expiry (g_rumbleDecayStartMs + g_rumbleDecayDurationMs, set fresh by
// TriggerRumble on EVERY event) checked again until gameplay resumes, so the
// physical motor keeps buzzing for the entire paused/loading duration instead of
// cutting off on that event's own real timeout.
//
// Call this from InjectMenuInputTick's always-running WndProc+SetTimer tick (the
// one tick confirmed to keep firing during a real pause) as a watchdog: it only
// enforces whichever event-specific deadline TriggerRumble already scheduled --
// it does NOT poll for new damage/fire events (that stays gameplay-tick-only,
// same as before) and is a no-op whenever no rumble is currently active.
void Rumble_TickExpiryWatchdog();
