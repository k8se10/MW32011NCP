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
// Custom (2026-08-06, issue #66 full-scope expansion, explicit direction: "add a
// controller bindings section for people who wish to use custom controller/stick
// layouts we dont yet offer") -- set automatically the moment a player edits any row
// on the new Binds tab; g_buttonMap then comes from g_modConfig.customButtonMap
// directly instead of ResolveButtonMap's preset switch. Picking one of the 4 real
// presets from the Controller tab's own Button Layout drilldown abandons Custom back
// to that preset (the customButtonMap itself is preserved on disk either way, so
// switching back to Custom later restores it).
enum class ButtonLayout { Default, Tactical, Lefty, TacticalLefty, Custom };
enum class StickLayout { Default, Southpaw, Legacy, LegacySouthpaw };

// Controller-glyph icon style (task #6, 2026-07-21) -- independent of ButtonLayout:
// XInput doesn't distinguish an Xbox-branded pad from a PlayStation-branded one on
// Windows, so which button-prompt ART a player wants can't be auto-detected from the
// input API alone, same reasoning already documented in re_notes/ui_assets.md's "Open
// questions" section. Names match assets/button_glyphs/'s own real file-prefix
// convention exactly (Xbox360 -> xbox360_*, XboxModern -> xboxmodern_*, PlayStation ->
// ps_*) -- don't invent new naming here. This selects ICON STYLE ONLY; it has no
// effect yet -- the actual glyph rendering it would drive is still being built
// (pivoted 2026-07-31 from in-font substitution to independent overlay quads
// drawn over the real button-prompt character; see re_notes/known_issues.md
// issue #48 for the current approach and status).
enum class GlyphStyle { Xbox360, XboxModern, PlayStation };

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
    float lookDegreesPerSecondHorizontal = 250.0f; // right-stick turn rate at full
                                                    // deflection, yaw (left/right) axis
    float lookDegreesPerSecondVertical = 145.0f;   // right-stick turn rate at full
                                                    // deflection, pitch (up/down) axis --
                                                    // split from horizontal 2026-07-31 per
                                                    // user request, as a deliberate PC-side
                                                    // enhancement (real MW3 console has only
                                                    // ONE Sensitivity slider driving both
                                                    // axes through a fixed internal ratio,
                                                    // not independently tunable -- confirmed
                                                    // both by the real console Options menu
                                                    // itself and by the user's own firsthand
                                                    // console play experience, 2026-08-03).
                                                    // Default corrected TWICE the same day:
                                                    // 250 (an ~80% ratio, wrongly assumed to
                                                    // match console) -> 75 (an initial ~30%
                                                    // estimate from feel alone, live-tested
                                                    // and found way too slow) -> 145 (~58%,
                                                    // the corrected live-tested estimate,
                                                    // "closer to about 55-60%"). See
                                                    // re_notes/known_issues.md issue #65.
    float adsSlowdownStrength = 1.75f;     // 0 = off, 1 = fully proportional to live zoom;
                                            // 1.75 confirmed live to feel closer to real
                                            // console controller CoD than exactly 1.0
                                            // (1.5 was tried first and improved on, too)
    float adsSlowdownBaseline = 0.65f;     // multiplied on top of the zoom-proportional
                                            // curve above -- without this, low-zoom optics
                                            // (ratio close to 1.0) got almost no slowdown
                                            // at all regardless of strength, since
                                            // ratio^strength stays near 1 when ratio does.
                                            // 0.65 confirmed live better than an initial 0.85.
                                            // REVERTED BACK to 0.65 (2026-07-31) after a
                                            // same-day round-trip to 0.45 and back: lowering
                                            // this SHARED constant to fix pistols/iron sights
                                            // (issue #44) also multiplies down the already-
                                            // small high-zoom ratio^strength term by the same
                                            // proportion, and live testing confirmed that WAS
                                            // perceptible ("too harsh on higher zoom") even
                                            // though the absolute numbers looked tiny -- a
                                            // single multiplicative constant structurally
                                            // cannot tune the ratio~1 and ratio~0 ends
                                            // independently, since it scales every ratio value
                                            // by the same relative percentage. The real fix is
                                            // adsCloseRangeSlowdownStrength below, a genuinely
                                            // separate, decoupled knob -- this constant is back
                                            // to being ONLY "how strongly zoom itself slows you
                                            // down," its original, proven-good role.
                                            // 1.0 = no baseline effect (old behavior);
                                            // lower = more slowdown even with minimal zoom.
    float adsCloseRangeSlowdownStrength = 0.35f; // Issue #44's real fix (2026-07-31):
                                            // an EXTRA slowdown multiplier that only matters
                                            // for low-zoom weapons (ratio close to 1.0, e.g.
                                            // pistols/iron sights) and decays away rapidly as
                                            // ratio drops, so it does NOT compound with
                                            // adsSlowdownBaseline/Strength's existing,
                                            // already-tuned high-zoom feel the way lowering
                                            // adsSlowdownBaseline itself did. Computed as
                                            // (1 - this * ratio^kCloseRangeFocusPower) in
                                            // GetAdsLookRateScale() -- at ratio=1 (pistol) this
                                            // multiplies scale by (1-0.35)=0.65 on top of
                                            // baseline, giving ~0.65*0.65=0.42 total (a real,
                                            // meaningful slowdown); by ~0.4 zoom ratio (a 3x+
                                            // scope) ratio^kCloseRangeFocusPower is already
                                            // negligible, so this contributes essentially
                                            // nothing there -- restoring high-zoom feel to
                                            // exactly what it was before issue #44 touched
                                            // anything. 0 = off (no extra low-zoom slowdown,
                                            // pre-issue-44 behavior). Must stay in [0, 1] --
                                            // clamped on load (1.0 would fully zero out
                                            // look at ratio=1, almost certainly too extreme).
                                            // NOT YET LIVE-CONFIRMED at this exact value.
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

    // [Gyro] (2026-08-11, issue #76) -- PREVIEW/WIP, per this project's own
    // shipped-but-unplayed labeling convention: real native gyro-aim, sourced
    // directly from a raw-HID DualSense (dualsense_input.h), deliberately NOT
    // routed through Steam Input's own gyro-to-mouse remapping -- the explicit
    // reason this exists at all is that Steam Input's device passthrough was found
    // unreliable for this project (issue #74), and a player who wants gyro
    // shouldn't be forced to choose between that and this mod ever seeing their
    // controller. DEFAULT OFF: unlike this project's other tunables (look
    // sensitivity, ADS slowdown, etc.), this hasn't been live-tested even once --
    // no DualSense is available to the developer. Additive on top of the existing
    // right-stick look delta, not a replacement for it (matches how gyro-as-
    // fine-aim commonly works elsewhere, e.g. Splatoon/Steam Input's own default).
    bool gyroEnabled = false;
    float gyroSensitivity = 1.0f; // multiplies the raw, uncalibrated gyro units
                                    // (see dualsense_input.h) before adding to look
                                    // delta -- NOT a claimed degrees/second value,
                                    // purely an empirical starting point to be
                                    // live-tuned, same convention this project
                                    // already used for kCurveExponent/vibration
                                    // intensity before those were dialed in via
                                    // real playtesting.
    bool gyroInvertPitch = false; // axis-sign guess, unverified -- see dualsense_input.h
    bool gyroInvertYaw = false;   // axis-sign guess, unverified -- see dualsense_input.h

    // [Stance]
    unsigned long proneHoldThresholdMs = 400; // B: hold vs. tap threshold

    // [Interact]
    unsigned long interactHoldThresholdMs = 300; // X: hold-to-interact threshold;
                                                  // 300 confirmed live to feel better
                                                  // than the original 740 default

    // [Survival]
    unsigned long readyUpHoldThresholdMs = 740; // Y: hold-to-ready-up threshold (Survival only)

    // [Movement] Auto-mantle (2026-08-03) -- STRICTLY opt-in, OFF by default per
    // explicit user direction. The real native mantle trigger is the same +gostand
    // command Jump already drives (ForceStandingViaRealToggle) -- confirmed via
    // FindStringRefs on the PLATFORM_MANTLE hint string, resolving to a single real
    // call site (FUN_00568da0 -> FUN_004fafd0(param_1, "+gostand", ...)); the engine
    // itself contextually reinterprets +gostand as mantle-over-a-ledge vs. stand-up
    // based on its own real condition flags (see re_notes/iw5sp.md's "Mantle --
    // found, concretely" section) -- this project never needs to detect the ledge
    // itself, only decide WHEN to drive the same real command automatically instead
    // of waiting for an explicit Jump press. When enabled, driving +gostand every
    // frame the player is real-sprinting AND pushing the left stick within
    // AutoMantleForwardConeDegrees of straight-forward at at least
    // AutoMantleMinStickMagnitude deflection -- a no-op on flat ground (same
    // command Jump already spams harmlessly while held), only doing anything when
    // the engine's own mantle condition is separately true.
    bool autoMantleEnabled = false;
    float autoMantleForwardConeDegrees = 45.0f; // total cone width (not half-angle),
                                                 // centered on straight-forward --
                                                 // user-specified as "top 45 degree cone"
    float autoMantleMinStickMagnitude = 0.9f;   // stick deflection (0..1) must be at
                                                 // least this close to full to count as
                                                 // "full analog stick forward"

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
    // [CustomBinds] -- per-action override for ButtonLayout::Custom (2026-08-06, issue
    // #66's Binds tab). Struct defaults match ButtonLayout::Default's own resolved
    // map, same as ResolveButtonMap's own "defaults already correct" convention, so a
    // player who's never touched the Binds tab has a sane starting point the moment
    // they first change one row instead of an unrelated/stale layout.
    ButtonMap customButtonMap;
    StickLayout stickLayout = StickLayout::Default;
    bool flipTriggers = false; // independent toggle: swaps RT<->RB and LT<->LB
    GlyphStyle glyphStyle = GlyphStyle::Xbox360; // task #6 -- see enum comment above;
                                                   // purely cosmetic until the
                                                   // substitution feature below is
                                                   // both implemented AND enabled

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
    // Strength/duration bumped three times now (2026-08-03): 0.25->0.55->0.85 /
    // 60->90->150ms across rounds 1-2 (see rumble.cpp's kRumbleSustainFraction for
    // round 2's sustain/decay-envelope fix, addressing real ERM motors' ~50-100ms
    // physical spin-up lag) -- still user-reported "better now but kinda weak" after
    // round 2. Round 3: FireIntensity raised to its own ceiling (1.0 -- there's no
    // headroom left above this; XInputSetState's wLeftMotorSpeed/wRightMotorSpeed are
    // already commanded at their real maximum, 65535, at this value), duration
    // extended further, and the sustain fraction itself raised alongside it
    // (kRumbleSustainFraction 0.6->0.7) so more of an already-longer pulse sits at
    // that ceiling before decaying. If this is STILL reported weak on the same
    // physical controller, the remaining variable is very likely the controller's own
    // hardware (third-party/Bluetooth pads commonly have materially weaker real motor
    // response than a genuine Xbox controller) rather than anything left to tune in
    // software -- there is no higher intensity value to send. Still fully
    // user-tunable; not yet live-retested.
    float vibrationFireIntensity = 1.0f;    // motor strength [0,1] on each real shot -- already at the ceiling
    unsigned long vibrationFireDurationMs = 180; // how long a fire pulse takes to decay
    float vibrationDamagePerPoint = 0.12f;   // motor strength added per point of real
                                              // damage taken (local player only)
    float vibrationDamageMaxIntensity = 1.0f;   // hard cap regardless of damage amount
    unsigned long vibrationDamageDurationMs = 280; // how long a damage pulse takes to decay

    // [Overlay] (task #47/#48, 2026-07-31) -- the top-right on-screen notification
    // text (overlay_hud.cpp). Uses Barlow Condensed SemiBold, bundled directly in
    // this DLL as a private, in-process-only font (proxy_d3d9.rc/resource.h,
    // AddFontMemResourceEx in overlay_hud.cpp's LoadOverlayFonts) since the
    // 2026-07-31 follow-up -- no longer depends on the font being installed
    // system-wide (previously requested by face name only, silently falling back
    // to a default font if missing; see re_notes/known_issues.md issue #47).
    bool overlayFontItalic = true; // selects the bundled font's real Italic style
                                     // (both Regular and Italic .ttf weights are
                                     // embedded, not a GDI-faked oblique slant).
    bool overlayTestCycleAllVariants = false; // STRICTLY A TESTING TOGGLE, default off.
                                     // When on, continuously cycles through every
                                     // known message/animation-style variant (Plain,
                                     // the rare "Thanks" text, Gold, Rainbow, Sweep)
                                     // every few seconds for as long as this stays
                                     // enabled, instead of the normal one-shot
                                     // startup roll -- lets every variant actually be
                                     // seen on demand rather than waiting on a 1-in-20
                                     // RNG roll. Never enable this for normal play.

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
    bool bindResolverHookLogging = true; // task #6/#35 (2026-07-21): the bind-resolver
        // text hook (FUN_0061f6f0) itself is always installed and always forwards to
        // the real trampoline completely unmodified (log-only first pass, no glyph
        // substitution yet -- see re_notes/known_issues.md issue #35) -- this toggle
        // only controls whether it LOGS what it observes. Default on for the first
        // live test; turn off if the hint-resolution log line ever gets too noisy
        // during normal play (it's already deduped to log only on text changes, not
        // every frame a hint is on screen, but a busy interact-hint-heavy session
        // could still be chatty).
    bool bindResolverGlyphSubstitution = false; // task #6/#35 (2026-07-21): when the
        // bind-resolver hook (FUN_0061f6f0) resolves real hint text for one of the 3
        // real hint-resolution callers (the key-rebind-capture caller is always
        // skipped regardless of this flag -- see known_issues.md issue #35) to a
        // single, unmodified key name this project has a glyph mapping for (see
        // analog_input_hooks.cpp's glyph-substitution table), overwrite the real
        // output buffer with a substitution codepoint sequence instead of leaving the
        // plain key-name text in place. DEFAULT OFF, DELIBERATELY: this is pure
        // preparatory groundwork -- no font asset currently loaded in the running
        // game can render the substitution codepoints yet (see known_issues.md issue
        // #23's still-open safe-loading problem), so flipping this on today would
        // just replace readable key-name text with tofu/missing-glyph boxes, a
        // regression, not an improvement. SUPERSEDED, NOT JUST BLOCKED, as of
        // 2026-07-31: the project pivoted away from in-font glyph substitution
        // entirely, toward independent overlay quads drawn over the real button-
        // prompt character instead (see known_issues.md issue #48) -- this toggle
        // stays here only in case that pivot doesn't pan out; don't assume it's on
        // the critical path for glyph work going forward.
    bool hudFontIdLogging = true; // task #6/#34 (2026-07-21): read-only diagnostic hook
        // on FUN_00690c80 (the real glyph-draw call every on-screen HUD/menu text goes
        // through) that logs the real Font_s.fontName whenever it CHANGES -- built to
        // empirically identify which real font renders interact-hint text, after
        // static tracing (FUN_00568110 -> ... -> FUN_00690c80) confirmed the font is
        // threaded as a parameter from a data-driven HUD-element source rather than
        // selected via the generic textfont enum (see known_issues.md issue #34).
        // Always forwards to the real trampoline completely unmodified regardless of
        // this toggle; only controls whether it logs. Default on for the first live
        // test.
    bool hudGlyphPositionLogging = false; // issue #48 (2026-07-31): read-only diagnostic,
        // same hook site as hudFontIdLogging above (FUN_00690c80/Hook_DrawGlyphText) but
        // dedup'd by DRAWN TEXT changing rather than by font changing, and logs the full
        // raw parameter set (param_2/param_3/param_5..param_9/param_14 -- x/y/scale/color
        // are suspected but NOT yet confirmed among these) instead of just the font name.
        // Purpose: empirically determine the real screen-space position/size convention
        // interact-hint text draws at, so a future overlay-quad glyph icon (issue #48's
        // proposed pivot away from in-font glyph injection) can be positioned against it
        // without guessing. DEFAULT OFF -- this is a one-off investigation toggle, not
        // meant to stay on during normal play; turn on, reproduce a real interact-hint
        // prompt (e.g. stand near a weapon/ammo pickup), then check proxy_d3d9.log for
        // "[hud-glyph-pos]" lines and turn back off. Always forwards to the real
        // trampoline completely unmodified regardless of this toggle; only controls
        // whether it logs.
    bool listItemPositionLogging = false; // issue #67 log-slimming pass (2026-08-08):
        // `[list-item-diag]` used to fire unconditionally on EVERY menu text-draw call
        // (i.e. once per visible list item, on ANY active menu screen) with no gating
        // and no dedup at all -- a real, confirmed contributor to proxy_d3d9.log
        // growing to ~22GB in one session (see re_notes/known_issues.md issue #67).
        // The specific hypothesis it was originally added to test (ordinal-vs-
        // selIndex matching, issue #51) was CLOSED 2026-08-02 via a different fix
        // (kManualGlyphPositions, a per-group calibrated table) -- but the ordinal-
        // based fallback this log sits in is still live for any menu group WITHOUT a
        // manual table entry yet, so the data is still occasionally useful for
        // calibrating a NEW screen, just not worth paying unconditionally on every
        // frame of every session. DEFAULT OFF -- turn on, reproduce the specific menu
        // screen needing calibration, check proxy_d3d9.log for "[list-item-diag]"
        // lines, then turn back off. Always forwards to the real trampoline
        // completely unmodified regardless of this toggle; only controls whether it
        // logs.
    // glyphIconOverlayEnabled REMOVED 2026-08-16 (issue #74 root-cause fix follow-up).
    // This was the master on/off switch for the whole controller-glyph overlay --
    // introduced issue #48 (2026-07-31) defaulted OFF as an internal "first live-test
    // round" flag, never flipped on for release, and directly caused every "no glyphs"
    // community report across v0.3.0/v0.3.1/v0.3.1.h1 (see known_issues.md issue #74).
    // Flipping the in-code default to `true` (2026-08-15) is NOT enough on its own:
    // ReadBool() below would still load `GlyphIconOverlay=0` from any EXISTING user's
    // already-written mw3ncp_config.ini (this key gets written out by SaveModConfig on
    // every run, so every past install already has an explicit `false` on disk) and
    // silently override the new default right back to broken -- upgraders would stay
    // stuck with no glyphs forever with no obvious reason why. Removed the flag and its
    // ini key entirely rather than just changing the default, so a stale on-disk `false`
    // from before this fix physically cannot block the overlay anymore. The overlay is
    // now gated only by ShouldDrawGlyphOverlay()'s remaining real condition
    // (forceGlyphOverlay || IsControllerActiveInputMethod()).
    bool armorFieldScanLogging = false; // issue #63 follow-up (2026-08-03): user-reported
        // the damage-rumble health poll (rumble.cpp's PollDamageRumble) never fires while
        // Survival's purchasable Body Armor is absorbing hits, since armor is a separate
        // value from real health and doesn't touch the +0x150 health field this project
        // already polls -- no prior research in this codebase has located that separate
        // armor field. DEFAULT OFF -- a one-off investigation toggle, not meant to stay on
        // during normal play. When enabled, scans a window of the local player's entity
        // struct every tick for any value that was STABLE for at least 2 consecutive
        // frames and then dropped by a plausible single-hit amount (filters out
        // continuously-ticking countdown timers, which are never stable beforehand) --
        // candidates are logged tagged "[armor-scan-diag]". Turn on, play a Survival wave
        // with Body Armor purchased and equipped, take a few hits while armored (confirm
        // on the real HUD that the armor number is what's dropping, not health), then send
        // back proxy_d3d9.log so the real offset can be picked out of the candidates.
        // Capped at 100 total log lines per session so a noisy candidate can't flood the
        // log across a long play session.
    bool forceGlyphOverlay = false; // 2026-08-08: community-reported (Nexus, v0.3.1) --
        // several players see NO controller-glyph icons at all, even on English with
        // default settings, and it's NOT reproducible on the developer's own machine --
        // meaning this is a per-environment issue (a real candidate: Controller_GetLeftStick/
        // etc. hardcode XInput user index 0, never scanning other slots, so a controller
        // sitting in slot 1+ -- e.g. Steam Input passthrough, multiple pads -- would never
        // register as "active," and ShouldDrawGlyphOverlay's IsControllerActiveInputMethod()
        // gate would then always be false), not a universal regression. DEFAULT OFF -- a
        // one-off diagnostic toggle, not meant to stay on during normal play. When enabled,
        // ShouldDrawGlyphOverlay (analog_input_hooks.cpp) draws the glyph/hint overlay
        // regardless of whether a controller is currently detected as the active input
        // method. If icons then show correctly, that confirms the "controller never
        // detected as active" theory (points at the XInput-slot issue above); if they still
        // don't show even forced on, the real cause is elsewhere (glyph-detection/resolver
        // logic itself, not the active-input gate) and this flag has done its job narrowing
        // that down. Does not affect glyph SELECTION (GlyphStyle) or the cursor overlay's
        // own separate visibility gate -- only this one gate.
    bool glyphPositionEditMode = false; // 2026-08-16, issue #51 follow-up ("finish our menu
        // glyphs properly ... use that click and drag thing we did to make it accurate per
        // screen"). kManualGlyphPositions was originally calibrated via expensive
        // MiniDumpWriteDump snapshots + raw memory scans -- slow enough that several real
        // screens were left uncovered (see that table's own "Deliberately NOT covered this
        // pass" list: LEVELS_BUTTON_LIST depth=4, OPTIONS_LIST indices 3+, keybind screens,
        // PAUSE_LIST). This reuses the same click-and-drag UX already proven live for the
        // harness-only controller-diagram editor (DiagramEditor_ToggleEditMode, issue #66),
        // but wired into the REAL game instead of the disconnected harness, gated behind this
        // flag for the same reason that one was harness-only: an accidental drag mid-game
        // could otherwise silently corrupt a real, already-correct calibrated position.
        // DEFAULT OFF, DELIBERATELY -- only ever turn on for an active calibration session.
        // When on, this is the MASTER gate only -- F2 (same key/two-step convention as the
        // harness diagram editor) is a second, live, in-session toggle on top of it: while
        // active, the currently-focused real menu-list item's glyph position becomes
        // draggable with the mouse (per (group, depth, index), seeded from the existing
        // manual table entry if one exists); F3 exports every touched group this session to
        // exported_glyph_positions.txt, next to the DLL, as ready-to-paste
        // kManualGlyphPositions entries. See analog_input_hooks.cpp's EditGlyphPositionsForFrame.

    bool captureRuntimeMenuAssets = false; // 2026-08-17, tools/ui_harness .menu-renderer
        // project follow-up: the static OpenAssetTools extraction under
        // D:\Tools\OpenAssetTools\zone_dump\ui\materials\/images\ only has 310/301 files --
        // real, but incomplete, and some real material names (e.g. "white") are almost
        // certainly procedurally generated at runtime and will never exist as an
        // extractable file no matter how thorough a re-extraction is. This dumps every
        // material texture the REAL game actually creates while playing to
        // <gameDir>\runtime_asset_capture\materials\<materialName>.dds, name-keyed off
        // the real material name (read from FindOrLoadAsset's own `name` argument at
        // FUN_004ff000, assetType==5==material -- an already-confirmed, plain __cdecl,
        // disassembly-verified signature from an earlier RE pass, see re_notes/iw5sp.md's
        // "FindOrLoadAsset" entry -- no new naked-hook/implicit-register risk introduced),
        // correlated forward to whichever CreateTexture call happens while that material's
        // load is still on the stack. DEFAULT OFF: this is a dev-only diagnostic capture
        // mode, not something that should run for normal play -- unbounded per-texture disk
        // writes every session is exactly the class of mistake issue #67's 22GB log file
        // already taught this project to avoid by default. Turn on, play a session touching
        // the menus/screens you want captured (main menu at minimum exercises some real
        // materials), turn back off -- captured files persist across sessions (never
        // auto-cleared) so the ui_harness's own .menu renderer can pick them up next time
        // it runs. See dllmain.cpp's InstallAssetCaptureHooks / analog_input_hooks.cpp's
        // FindOrLoadAsset hook for the implementation.

    bool frametimeBenchmarkLogging = false; // 2026-08-17: live-reported "still jittery,
        // 239 fps on the counter but feels like 40" AFTER the log-truncate fix and the
        // asset-capture async-write fix -- both real, evidence-backed fixes that didn't
        // fully resolve it, and MSI Afterburner/RTSS's own frametime graph shows nothing
        // abnormal, which argues against a generic render-thread stall RTSS would
        // normally catch. Built to answer this with real per-frame data instead of
        // another guess: writes frametime_benchmark.csv (next to the DLL) with one row
        // per frame -- frame index, real frame-to-frame time in ms (QueryPerformanceCounter,
        // measured in Hook_EndScene, same per-frame cadence RTSS itself uses), plus how
        // much of that frame's time (if any) went into this project's own two current
        // suspects: Controller_SetVibration's synchronous XInputSetState/DualSense HID
        // write, and the asset-capture CreateTexture/FindOrLoadAsset hooks. DEFAULT OFF --
        // a per-frame CSV write has its own real disk-I/O cost, exactly the class of
        // mistake this project already learned to gate behind an explicit toggle (issue
        // #67's lineage) rather than ever leave on by default. Turn on, play until the
        // stutter is felt, turn back off, share frametime_benchmark.csv. See
        // frame_benchmark.h/.cpp for the implementation.

    // [Options] (issue #66, 2026-08-04 full-scope pivot) -- STRICTLY OPT-IN, OFF by
    // default per this project's own established pattern for structurally significant,
    // not-yet-verified behavior changes (autoMantleEnabled, glyphIconOverlayEnabled,
    // etc. all default off the same way). When enabled, replaces the ENTIRE real
    // Options screen with a fully custom-drawn one covering every real vanilla
    // setting (not just this mod's own), not just the small extra-row extension
    // rounds 1-4 built. See re_notes/known_issues.md issue #66 and
    // re_notes/options_menu_full_map.md for the full design/research trail.
    bool useCustomOptionsScreen = false;

    // sprintStaminaBypassForTesting (task #9) REMOVED 2026-07-19: graduated to
    // unconditional the same day it was added -- Sprint's real +sprint kbutton
    // migration was LIVE-CONFIRMED working, and with it confirmed that the real
    // engine's own native stamina/duration timer (and Extreme Conditioning's real
    // override) now applies automatically. There's no longer a custom timer left
    // to bypass, so the toggle itself is dead weight, not just proven-safe.
};

// The loaded config, populated by LoadModConfig() at startup and re-populated live by
// CheckConfigHotReload() (added v0.2.5) whenever mw3ncp_config.ini's on-disk write time
// changes -- no longer strictly read-only after startup.
extern ModConfig g_modConfig;

// Reads mw3ncp_config.ini from the same directory as this DLL, filling in any
// missing/malformed key with its default. Writes a fresh, fully-commented default file
// if none exists yet, so the file is discoverable and self-documenting on first run.
// Call once, early in DllMain (before any hook that reads g_modConfig runs).
void LoadModConfig();

// Persists the CURRENT in-memory g_modConfig back to mw3ncp_config.ini, in the
// current schema -- for the in-game custom options overlay (2026-08-04) to call
// after the player adjusts a value live. Safe to call from any thread the game's
// own hooks already run on (same file I/O this project's existing config code
// already does synchronously).
extern "C" void SaveModConfig();

// ---- Vanilla settings mirror (issue #66, 2026-08-04 full-scope pivot) -------------
//
// Unlike g_modConfig above (where the ini IS the source of truth, loaded into memory
// and driving mod behavior), the real vanilla dvars/keybinds remain the source of
// truth for actual gameplay -- these two functions mirror between the real engine
// and mw3ncp_config.ini as a genuine local settings BACKUP, independent of Steam
// Cloud sync issues (explicit user request: "it allows much better settings backups
// so if people have issue with steam saving options we actually help with that
// too"). NOT called from LoadModConfig()/DllMain() -- the real dvar/keybind systems
// are not guaranteed initialized that early; callers must only invoke these once the
// game is confirmed running (same timing class as this project's other per-frame
// hooks).

// Reads every real vanilla setting's CURRENT value (live from the real dvar/keybind
// table, via vanilla_settings_sync.h) and writes it into mw3ncp_config.ini, one
// section per Options tab. Read-only against the engine -- never changes a real
// setting. Safe to call periodically to keep the backup fresh.
void SyncVanillaSettingsToIni();

// The reverse direction: reads every real vanilla setting's LAST-SYNCED value back
// out of mw3ncp_config.ini and writes it to the real engine. An explicit, user-
// triggered action (e.g. a "Restore from Backup" UI control) -- never called
// automatically, since silently overwriting live game settings from a possibly-
// stale ini on every launch would be surprising, not helpful.
void RestoreVanillaSettingsFromIni();
