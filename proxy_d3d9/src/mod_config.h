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
// console control scheme (user-supplied, ~90-95% confidence, NOT independently
// verified against a real Xbox 360/PS3 unit or console-build memory) rather than
// RE'd from this binary -- expect the Tactical Lefty combination in particular to
// need a correction pass once it can be checked against real hardware.
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

    // [Stance]
    unsigned long proneHoldThresholdMs = 400; // B: hold vs. tap threshold

    // [Interact]
    unsigned long interactHoldThresholdMs = 300; // X: hold-to-interact threshold;
                                                  // 300 confirmed live to feel better
                                                  // than the original 740 default

    // [Survival]
    unsigned long readyUpHoldThresholdMs = 740; // Y: hold-to-ready-up threshold (Survival only)

    // [Sprint]
    float sprintMaxStaminaSeconds = 4.0f; // continuous sprint time before depleting
    float sprintRegenSeconds = 2.0f;      // time NOT sprinting to fully recover from empty

    // [Bindings] -- OG console layout presets, see the enum comments above.
    ButtonLayout buttonLayout = ButtonLayout::Default;
    StickLayout stickLayout = StickLayout::Default;
    bool flipTriggers = false; // independent toggle: swaps RT<->RB and LT<->LB

    // [AimAssist] (task #16) -- our own implementation, not the native chain (that
    // turned out to be shared bot-aiming math, not a player-facing feature -- see
    // re_notes/iw5sp.md). Built from real entity data (position, a live-traced
    // type/state byte) plus our own targeting/curve math, applied directly onto the
    // same kPitchAccum/kYawAccum globals our own look injection already writes.
    bool aimAssistEnabled = false;
    float aimAssistRange = 1200.0f;         // max world-unit distance to consider a target
    float aimAssistConeDegrees = 6.0f;      // half-angle of the "near crosshair" cone
    float aimAssistFrictionStrength = 0.6f; // 0 = no slowdown near a target, 1 = strongest
    float aimAssistMagnetismDegreesPerSecond = 40.0f; // max pull-toward-target rate

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
    bool sprintStaminaBypassForTesting = false; // task #9, 2026-07-19: Sprint just
        // migrated off raw pm_flags-forcing onto the real +sprint kbutton
        // (0xA98CCC) -- this isolates that change for live testing by skipping
        // this mod's OWN stamina/cooldown timer entirely (same code path as the
        // existing player_sprintUnlimited dvar bypass), so Sprint runs purely off
        // the new kbutton mechanism with no 4s/2s timeout fighting for attention
        // while confirming the kbutton itself actually works. Default false --
        // flip to true in mw3ncp_config.ini for this one test pass, then back to
        // false once confirmed (the stamina/cooldown system itself is unaffected
        // by the kbutton migration and isn't what's being tested here).
};

// The loaded config, populated once by LoadModConfig(). Read-only after startup --
// nothing in this mod hot-reloads the INI mid-session.
extern ModConfig g_modConfig;

// Reads mw3ncp_config.ini from the same directory as this DLL, filling in any
// missing/malformed key with its default. Writes a fresh, fully-commented default file
// if none exists yet, so the file is discoverable and self-documenting on first run.
// Call once, early in DllMain (before any hook that reads g_modConfig runs).
void LoadModConfig();
