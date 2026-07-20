#pragma once

// mod_config — user-facing tuning values, loaded from an INI file next to the DLL
// (task #14). Ships with sane defaults matching what was previously hardcoded
// throughout analog_input_hooks.cpp; this file replaces those constants with runtime
// values so a player can retune without recompiling. A real in-game options screen
// (sliders, live preview) is future work once controller menu/UI navigation exists --
// this INI is the interim, and the file this mod ships with today.

// ---- Button/stick layout presets (task #15, 2026-07-16) ---------------------------
//
// The original Xbox 360/PS3 console builds are confirmed to have no controller
// layout data present in this PC binary at all (PC never shipped controller support
// in the first place -- consistent with CLAUDE.md's original findings: no native
// gamepad-nav cvars, no controller-menu asset strings found in any .iwd). These
// presets are therefore reconstructed from the known-unchanged CoD4->MW2->MW3
// console control scheme (user-supplied) rather than RE'd from this binary.
// CONFIRMED CORRECT against real hardware, 2026-07-19 -- including TacticalLefty,
// previously the one open accuracy question in this table.
enum class ButtonLayout { Default, Tactical, Lefty, TacticalLefty };
enum class StickLayout { Default, Southpaw, Legacy, LegacySouthpaw };

// One entry per logical action; resolves to whichever physical XInput button/trigger
// the active ButtonLayout (+ FlipTriggers) currently assigns it to. Scoreboard (Back)
// is included for completeness even though nothing is wired to it yet (task #5).
enum class PhysicalInput { RT, LT, RB, LB, X, Y, A, B, LS, RS, Start, Back };

struct ButtonMap
{
    PhysicalInput fire = PhysicalInput::RT;
    PhysicalInput ads = PhysicalInput::LT;
    PhysicalInput lethal = PhysicalInput::RB;
    PhysicalInput tactical = PhysicalInput::LB;
    PhysicalInput reloadUse = PhysicalInput::X;
    PhysicalInput weaponSwitch = PhysicalInput::Y;
    PhysicalInput jump = PhysicalInput::A;
    PhysicalInput crouchProne = PhysicalInput::B;
    PhysicalInput sprint = PhysicalInput::LS;
    PhysicalInput melee = PhysicalInput::RS;
    PhysicalInput pause = PhysicalInput::Start;
    PhysicalInput scoreboard = PhysicalInput::Back;
};

// Resolved once by LoadModConfig() (via ResolveButtonMap()) from
// g_modConfig.buttonLayout + g_modConfig.flipTriggers. Read-only after startup, same
// as g_modConfig itself.
extern ButtonMap g_buttonMap;

// Computes a ButtonMap for the given layout + flip-triggers setting. Exposed (not
// just called internally from LoadModConfig) so it's independently testable/callable.
ButtonMap ResolveButtonMap(ButtonLayout layout, bool flipTriggers);

struct ModConfig
{
    // [Look]
    float lookDegreesPerSecond = 250.0f;   // right-stick turn rate at full deflection
    float adsSlowdownStrength = 1.75f;     // 0 = off, 1 = fully proportional to live zoom;
                                            // 1.75 confirmed live to feel closer to real
                                            // console controller CoD than exactly 1.0
                                            // (1.5 was tried first and improved on, too)
    float adsSlowdownBaseline = 0.65f;     // multiplied on top of the zoom-proportional
                                            // curve above -- without this, low-zoom optics
                                            // (ratio close to 1.0) got almost no slowdown
                                            // at all regardless of strength, since
                                            // ratio^strength stays near 1 when ratio does.
                                            // 0.65 confirmed live better than an initial
                                            // 0.85 -- more slowdown even on minimal zoom.
                                            // 1.0 = no baseline effect (old behavior);
                                            // lower = more slowdown even with minimal zoom.
    bool invertLook = false;               // OG console "Invert Look" -- flips vertical look
    unsigned long lookAccelerationRampMs = 33; // ms for look turn-rate to ramp from 0 to full
                                                  // speed after the stick leaves neutral, matching
                                                  // real console CoD (MW2/Black Ops, same IW-engine
                                                  // era as MW3) confirmed via external research to
                                                  // apply a linear turn-speed ramp rather than
                                                  // instant full-rate response -- this project's own
                                                  // look had none at all until 2026-07-19. 200ms was
                                                  // tried first and confirmed WRONG live (2026-07-20)
                                                  // -- user live-tested many values and concluded the
                                                  // real ramp is tied to this old engine's locked
                                                  // 30fps tick (33.33ms/frame), not an arbitrary
                                                  // wall-clock duration: one engine tick, not ~0.2s.
                                                  // 33 = one 30fps frame, confirmed live as the right
                                                  // feel. 0 = off (instant-response behavior).

    // [Stance]
    unsigned long proneHoldThresholdMs = 400; // B: hold vs. tap threshold

    // [Interact]
    unsigned long interactHoldThresholdMs = 300; // X: hold-to-interact threshold;
                                                  // 300 confirmed live to feel better
                                                  // than the original 740 default

    // [Survival]
    unsigned long readyUpHoldThresholdMs = 740; // Y: hold-to-ready-up threshold (Survival only)

    // [Sprint] section removed 2026-07-19 (task #9): Sprint now drives the real
    // +sprint kbutton_t directly (CallKbuttonDown/CallKbuttonUp), so the engine's own
    // native sprint duration/recovery timer applies automatically -- LIVE-CONFIRMED
    // this also correctly picks up Extreme Conditioning's real duration override for
    // free, with no separate detection code needed. The custom stamina/cooldown
    // timer layer this project maintained since 2026-07-15 (to work around the
    // earlier pm_flags-forcing approach bypassing the real timer entirely) is gone;
    // see re_notes/known_issues.md issue #6 and PATCHNOTES.md for the full history.

    // [Bindings] -- OG console layout presets, see the enum comments above.
    ButtonLayout buttonLayout = ButtonLayout::Default;
    StickLayout stickLayout = StickLayout::Default;
    bool flipTriggers = false; // independent toggle: swaps RT<->RB and LT<->LB

    // Aim assist (task #16) permanently removed 2026-07-20 -- see
    // re_notes/known_issues.md issue #15/#16 for why: reading live entity/target data
    // out of process memory to adjust aim is mechanically identical to a soft-aimbot
    // regardless of intent, and this project's own VAC research found the closest real
    // precedent for a proxy-DLL that manipulates gameplay state beyond pure input
    // remapping (ENB, vs. ReShade's visual-only clean record) has actual documented
    // ban history. Cut entirely rather than left disabled-by-default, since the goal
    // is removing the risk surface, not just defaulting it off.

    // [Vibration] (task #17, 2026-07-18) -- no native rumble infrastructure exists in
    // this build at all (confirmed zero-hit string search for "rumble"/"vibrat"/
    // "forcefeedback" -- see re_notes/known_issues.md issue #24), so this is entirely
    // our own XInputSetState output, driven off two real, disassembly-confirmed native
    // notify choke points (FUN_004895b0 for weapon fire, FUN_0044cdb0 for damage taken
    // by the local player -- see rumble.h/.cpp). This one Enabled toggle IS this
    // feature's kill-switch (no separate [Experimental] entry needed on top of it).
    bool vibrationEnabled = true;
    float vibrationFireIntensity = 0.25f;    // motor strength [0,1] on each real shot
    unsigned long vibrationFireDurationMs = 60; // how long a fire pulse takes to decay
    float vibrationDamagePerPoint = 0.03f;   // motor strength added per point of real
                                              // damage taken (local player only)
    float vibrationDamageMaxIntensity = 1.0f;   // hard cap regardless of damage amount
    unsigned long vibrationDamageDurationMs = 200; // how long a damage pulse takes to decay

    // [Experimental] (2026-07-18) -- individually toggleable, not-yet-fully-proven
    // behaviors, so a hypothesis under live test can be flipped off without a
    // recompile if it turns out to be wrong or to cause a regression, instead of
    // reverting/re-editing source. Once a toggle here is confirmed correct and
    // stable, it graduates to being unconditional (the toggle is removed, not left
    // around indefinitely) -- this section is for active experimentation, not a
    // permanent settings surface.
    bool fireNotifyQueueKick = true; // task #7/#29: also pushes the literal command
        // "n" onto the local player's real command queue (via FUN_00428a70) on
        // Fire's down-edge, alongside the existing real +attack kbutton call --
        // an attempt to reach notifyonplayercommand's real delivery mechanism for
        // killstreaks like Predator Missile. NOT YET LIVE-CONFIRMED to help or be
        // harmless; toggle off here if it's ever suspected of causing a gameplay
        // regression, without needing to touch analog_input_hooks.cpp.
    // sprintStaminaBypassForTesting (task #9) REMOVED 2026-07-19: graduated to
    // unconditional the same day it was added -- Sprint's real +sprint kbutton
    // migration was LIVE-CONFIRMED working, and with it confirmed that the real
    // engine's own native stamina/duration timer (and Extreme Conditioning's real
    // override) now applies automatically. There's no longer a custom timer left
    // to bypass, so the toggle itself is dead weight, not just proven-safe.
};

// The loaded config, populated once by LoadModConfig(). Read-only after startup --
// nothing in this mod hot-reloads the INI mid-session.
extern ModConfig g_modConfig;

// Reads mw3ncp_config.ini from the same directory as this DLL, filling in any
// missing/malformed key with its default. Writes a fresh, fully-commented default file
// if none exists yet, so the file is discoverable and self-documenting on first run.
// Call once, early in DllMain (before any hook that reads g_modConfig runs).
void LoadModConfig();
