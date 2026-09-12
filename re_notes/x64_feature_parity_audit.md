# x64 Feature Parity Audit (2026-09-12)

**Status: COMPLETE (static/code-level pass).** Direct instruction that triggered
this: "ALL 0.3.5 stuff needs to be present at the same level or better," followed
by "what about the entire enhancement suite and stuff" / "what about all base
input, menu glyphs everything." This is a systematic, source-code-verified sweep
of every feature `legacy-x86-docs/README.md` (the authoritative v0.3.5-x86 record)
and `CLAUDE.md`'s 2026-08-25 through 2026-08-29 timeline entries document, checked
directly against the current x64 source — not against any existing summary
(including this project's own `known_issues_x64.md` and `README.md`, both of
which were already shown incomplete before this pass started). This document is
the deliverable; it does not fix anything. Do not edit `analog_input_hooks_x64.cpp`,
`overlay_hud.cpp`, `rumble.cpp`, or any other source file based on this document
without a separate, explicit fix pass — three concurrent agents are already
implementing fixes for a subset of what's found here (menu-focus/glyph tracking,
visual-enhancement-suite porting, vibration).

## Summary

Of **61 distinct features/mechanisms** tracked below:

- **34 confirmed present** on x64 (real, wired code found — a meaningful number
  still carry their own "not yet live-tested" caveat, noted per-row; a working
  build is not the same as a confirmed-live one)
- **21 confirmed absent / not ported at all** (verified by direct trace: either
  the implementing function is wrapped in `#if !defined(_M_X64) && !defined(_WIN64)`
  and never compiled for x64, or it compiles but nothing in the x64 input
  pipeline ever calls it, or it's an explicit early-return stub)
- **6 partial / regressed / structurally different** (present in some form, but
  either using a materially different — and in one case, worse — mechanism than
  x86's own final shipped design, or genuinely ambiguous/unconfirmed pending a
  live test)

**Two findings below are new — not in `known_issues_x64.md`'s existing
"Corrected gap list, 2026-09-12" entry or anywhere else in this project's
documentation before this pass:**

1. **Native D-pad+A generic menu/UI navigation is 100% absent on x64** — not just
   the custom Options screen's own open-trigger (already documented), but the
   entire underlying mechanism: main menu, pause menu, options two-pane drill,
   buy-station/armory item lists, and slider VALUE adjustment. `InjectControllerMenuNav()`
   (`analog_input_hooks.cpp`) — the single function responsible for all of it — is
   wrapped in `#if !defined(_M_X64) && !defined(_WIN64)` in its entirety and never
   compiles for x64, let alone runs. A controller player on x64 today cannot
   navigate any native menu at all; every menu interaction requires keyboard/mouse.
   `InjectControllerMenuBack()` (B's ESC-forward, the equally load-bearing partner
   function) is wrapped the identical way and is equally absent.
2. **Sprint (L3) on x64 uses a materially different, and likely worse, mechanism
   than x86's final shipped design.** x86's Sprint went through two designs
   historically: an original raw `pm_flags`-forcing approach (gave infinite
   sprint, no native duration/recovery timer, no Extreme Conditioning override)
   that was deliberately superseded by driving the real `+sprint` kbutton once
   found (engages the native timer and the Extreme Conditioning perk override
   automatically, confirmed live 2026-07-19 — see `CLAUDE.md`'s "Sprint's real
   kbutton" section). x64's `Hook_SprintTick` (`analog_input_hooks_x64.cpp`)
   forces the `pm_flags`-equivalent bit directly — the ORIGINAL, deprecated x86
   design, not the final one. A real x64 `+sprint`-style kbutton was never
   searched for (the RE effort found and hooked the `pm_flags` writer itself,
   `FUN_140014a80`, and stopped there — a reasonable, working first cut, but not
   yet the same design x86 shipped). Practical consequence, not yet live-confirmed
   either way: x64 Sprint likely has no duration limit/cooldown and the Extreme
   Conditioning perk's duration override likely does not apply automatically,
   both real regressions from `v0.3.5-x86`'s confirmed-working behavior if true.

See the **Methodology & caveats** section at the end for what this pass could
and couldn't verify statically.

## Full table

Legend: **PRESENT** = real, wired code found, reachable from the live x64 input
pipeline. **ABSENT** = verified not reachable on x64 (guarded out at compile
time, or compiles but nothing calls it, or an explicit early-return stub).
**PARTIAL** = present but materially different from x86's final design, or
real-but-unconfirmed. Every PRESENT row still needs the normal "verify live"
bar (CLAUDE.md SS7/SS8) applied separately — many are build-verified only.

### Movement & look

| # | Feature | x86 status (v0.3.5) | x64 status | Evidence |
|---|---|---|---|---|
| 1 | Analog movement (left stick) | Confirmed working | **PRESENT** | `analog_input_hooks_x64.cpp` `Hook_MovementTick` — writes `cmd[0x1c]`/`[0x1d]` additively, confirmed live per `known_issues_x64.md` issue #1 |
| 2 | Analog look (right stick) + acceleration ramp | Confirmed working | **PRESENT** | Same function, `g_pitchAccum`/`g_yawAccum` pre-hook write + `GetLookAccelerationScaleX64()`; confirmed live |
| 3 | ADS zoom-aware look-slowdown (`AdsSlowdownStrength`/`Baseline`/`AdsCloseRangeSlowdownStrength`) | Confirmed working | **ABSENT** | `analog_input_hooks_x64.cpp` lines ~384-391: explicit comment, "Deliberately scoped OUT of this first x64 pass... genuinely unresolved RE targets, not yet found." Config keys still exist and are written to a fresh `.ini` (shared `mod_config.cpp`) but have no consumer on x64 |
| 4 | Invert Look | Confirmed working | **PRESENT** | `Hook_MovementTick`: `g_modConfig.invertLook ? -lookY : lookY` |
| 5 | Aim assist | Permanently removed (x86 policy) | **N/A** | Not applicable to either platform — policy decision, not a port gap |

### Combat & interaction

| # | Feature | x86 status | x64 status | Evidence |
|---|---|---|---|---|
| 6 | Fire (RT) | Confirmed working | **PRESENT**, w/ caveat | `g_kbuttonActivate`/`Deactivate` on `g_fireStruct`; the additional "sniper Fire/ADS fix" (`g_notifyBindDispatch`) is explicitly flagged **NOT YET LIVE-TESTED** in-file |
| 7 | ADS (LT) hold-to-aim | Confirmed working, real kbutton | **PRESENT** | kbutton call + explicit force of `g_adsToggleFlag` (the real "is aiming" flag) — went through 3 live-test-driven correction rounds documented in-file, currently believed correct |
| 8 | Melee (R3) | Confirmed working | **PRESENT** | Raw usercmd bit `kMeleeUsercmdBit=0x4`, mirrors x86 |
| 9 | Reload (X) | Confirmed working | **PRESENT** | kbutton call on `g_reloadStruct` |
| 10 | Interact hold-vs-tap (X) | Confirmed working, 300ms hold | **PRESENT** | `g_interactPressStartMsX64` + `g_modConfig.interactHoldThresholdMs`, mirrors x86 exactly |
| 11 | Weapon switch (Y) / `weapnext` | Confirmed working | **PRESENT** | `g_weaponNext(0,1)` direct call on press edge |
| 12 | D-pad Up/Right/Down (`+actionslot 1-3`) | Confirmed working | **PRESENT** | `g_actionSlot` direct call |
| 13 | D-pad Left (AI squadmate call-in, synthetic-key exception) | Confirmed working (x86's 2nd of 3 exceptions) | **PRESENT**, w/ caveat | `SendSyntheticActionSlot4KeyX64` ported 2026-09-05; in-file comment: "expected to exist here too, unconfirmed until live-tested" |
| 14 | Lethal (RB) | Confirmed working | **PRESENT** | Raw bit `kLethalUsercmdBit=0x4000` |
| 15 | Tactical (LB) | Confirmed working | **PRESENT** | Raw bit `kTacticalUsercmdBit=0x8000` |
| 16 | Killstreak: Predator Missile — launch | Fixed via `FireNotifyQueueKick` (pushes literal `"n 1"` client command) | **PARTIAL / unconfirmed** | x64's Fire-down handler calls `g_notifyBindDispatch(0, kFireBindCaseDown)` → `FUN_14007fc00` formats `"n %i"` into the same reliable-command channel — functionally the same mechanism as x86's fix, built for a different bug (sniper Fire/ADS) and never connected to or tested against Predator Missile specifically. Plausible free fix, not verified |
| 17 | Killstreak: Predator Missile — post-fire guidance aim | Already broken/partial on x86 itself (known lead, not fixed) | **ABSENT**, not attempted | No x64 work on this; inherits x86's own open status, not a new regression |
| 18 | Killstreak: Precision Airstrike | Confirmed working (rides on Fire) | **PRESENT**, likely, unverified | Depends only on Fire (#6, present) — no x64-specific mechanism needed per x86's own design, but never independently live-tested on x64 |
| 19 | Killstreak: AI squadmate call-in | Confirmed working | Same as #13 | Duplicate of D-pad Left row above |
| 20 | Vibration/rumble (fire + damage) | Confirmed working, 1.5/2 completeness | **ABSENT — 100% unported** | `Rumble_Install()` (`rumble.cpp`) is only ever called from `InstallAnalogInputHooks()` (`analog_input_hooks.cpp`), entirely wrapped in `#if !defined(_M_X64) && !defined(_WIN64)` — never executes on x64. `Rumble_Tick()`/`Rumble_TickExpiryWatchdog()` are called but are harmless no-ops with nothing ever triggering an event. Already documented in `known_issues_x64.md`'s "Corrected gap list, 2026-09-12," confirmed again independently this pass |

### Stance & Sprint

| # | Feature | x86 status | x64 status | Evidence |
|---|---|---|---|---|
| 21 | Crouch/Prone 3-state stance ladder (B) | Confirmed working, real native toggle, tap-vs-hold ladder | **PRESENT**, different mechanism | x64 forwards raw press/release edges directly to `FUN_14007c3a0`'s case 0x17/0x18 (`+stance`/`-stance`), trusting native logic to handle tap/hold internally, rather than replicating x86's own explicit tap/hold state machine — a deliberate, reasoned design choice (see the in-file comment on why replicating the ambiguous "restore previous posture" semantics was judged riskier than trusting native dispatch), but the exact resulting behavior (does it match x86's documented tap→crouch/hold→prone table exactly?) is **not independently live-confirmed** |
| 22 | Sprint (L3) | Confirmed working, **real `+sprint` kbutton**, native duration/recovery timer + Extreme Conditioning apply automatically | **PARTIAL / regressed** | `Hook_SprintTick` forces the `pm_flags`-equivalent bit directly (`FUN_140014a80`'s own field) — this is x86's ORIGINAL, deprecated pre-kbutton design, not the final one. See Summary finding #2 above for full detail. Bit-ownership tracking (never clear a bit we didn't set) IS correctly ported, so keyboard sprint should stay unaffected |
| 23 | Hold Breath (L3 while ADS'd, sniper) | Confirmed working, real kbutton | **ABSENT** | Zero references anywhere in `analog_input_hooks_x64.cpp` (grepped: `Hold Breath`, `HoldBreath`, `breath_sprint` — all 0 hits). L3 on x64 only ever drives raw Sprint, with no ADS-aware branch to a separate Hold Breath kbutton |
| 24 | Auto-Mantle while sprinting | Confirmed working (v0.3.4), ships off by default | **ABSENT** | Zero references to `AutoMantle`/`auto.?mantle` anywhere in the x64 file. Not gated off — simply never implemented for x64 |
| 25 | Extreme Conditioning perk override | Resolved "for free" via Sprint's real kbutton | **ABSENT / not applicable** | Depends entirely on #22's kbutton design, which x64 doesn't use — no override mechanism exists or was needed to exist under x86's own original design either (it also lacked this until the kbutton was found) |
| 26 | Jump (A) | Confirmed working | **PRESENT** | Raw bit `kJumpUsercmdBit=0x400`, suppressed while a menu is active via `g_menuActiveGateFlag` |
| 27 | Jump auto-stand from crouch/prone | Implemented (`ForceStandingViaRealToggle`) | **PRESENT**, w/ caveat | `ForceStandingViaRealToggleX64()`, ported 2026-09-05 — listed in `known_issues_x64.md` as "still awaiting live confirmation" as of that entry |

### Menu & pause

| # | Feature | x86 status | x64 status | Evidence |
|---|---|---|---|---|
| 28 | Start — pause menu open AND close | Confirmed working, real engine calls | **PRESENT** | `g_pauseToggle` (`FUN_1400823b0`) called from both the gameplay tick and `PollPauseToggleX64()` (always-on tick) — confirmed working live per `known_issues_x64.md` |
| 29 | B — back out of menus (real ESC-forward) | Confirmed working | **ABSENT** | `InjectControllerMenuBack()` (`analog_input_hooks.cpp`) is entirely wrapped in `#if !defined(_M_X64) && !defined(_WIN64)` — never compiled, never called from x64's `InjectMenuInputTick` (confirmed by direct inspection of that function's x64 vs. x86 sections). **New finding, not previously documented** |
| 30 | Back button (`+scores` scoreboard/objectives) | 🟡 on x86 too (implemented, zero visible effect, parked) | **ABSENT** | `InjectControllerScoreboard()` itself has NO arch guard and would compile fine on x64 (pure synthetic-key logic, no hardcoded addresses) — but it is never called from anywhere in the x64 input pipeline (`analog_input_hooks_x64.cpp` has zero references to `scores`/`scoreboard`). A cheap gap to close relative to most others in this list, since the function already exists and is arch-clean |
| 31 | Survival ready-up (hold Y ~740ms) | Confirmed working, synthetic-key exception #1 of 3 | **ABSENT** | Zero references to `ready.?up`/`ReadyUp` in the x64 file. Y on x64 only ever fires `weapnext` on press, with no hold-duration branch at all |
| 32 | Buy-station + pause interaction fix | Confirmed working (fixes a real native bug) | **UNKNOWN / not reachable** | This fixed a real native engine bug, not this mod's own feature — but since controller-driven buy-station navigation itself doesn't work on x64 at all (see #34), this is currently moot for controller players; whether the underlying native bug still exists on x64 (relevant to mouse/keyboard buy-station+pause use) was not independently re-checked this pass |
| 33 | Native D-pad+A menu/UI navigation (main menu, pause menu, options two-pane drill, buy-station/armory lists, slider VALUE adjustment) | Confirmed working, full coverage (task #22) | **ABSENT — 100%, entire system** | `InjectControllerMenuNav()` (`analog_input_hooks.cpp`) — the single function implementing ALL of this — is wrapped in `#if !defined(_M_X64) && !defined(_WIN64)` in its entirety (lines 2902-onward) and is never called from x64's `InjectMenuInputTick` (confirmed: the x64 `#if` block in that function calls only `PollPauseToggleX64`/`AutoUnstickPauseCycleX64`/`PollCustomOptionsMenuX64`; the x86-only block below it — containing `InjectControllerMenuNav()`/`InjectControllerMenuBack()`/`InjectControllerPauseMenu()` — is excluded via the `#if !defined(_M_X64)` guard immediately preceding it). **New finding, not previously documented anywhere in this project.** This is distinct from, and larger than, the already-documented "custom Options screen's own open-trigger is unported" gap (#37 below) — this is the underlying native-menu navigation mechanism the custom screen's trigger was ALSO trying to reuse, and its absence means a controller player cannot navigate ANY native menu (main menu, pause menu, buy stations, sliders) on x64 today — keyboard/mouse is required for all menu interaction |

### Button-glyph UI / F2-F3 editor / custom cursor

| # | Feature | x86 status | x64 status | Evidence |
|---|---|---|---|---|
| 34 | Button-glyph UI prompts (in-game interact hints, menu corner hints) | Confirmed working | **ABSENT** | `known_issues_x64.md`'s own follow-up audit (same investigation, pre-dates the 2026-09-12 gap-list entry): "`analog_input_hooks_x64.cpp` makes zero calls to any glyph/hint-request function... `DrawGlyphIconIfRequested`'s own gate... simply never gets set to true on x64." Confirmed independently this pass via the same grep |
| 35 | Highlighted-item A-glyph (menu list navigation) | 🟡 on x86 too (draws where manually calibrated) | **ABSENT** | Depends on `TryGetRealFocusedGroupAndIndex`/`GetMenuStackDepth` (`analog_input_hooks.cpp`), both explicit x64 stubs returning `false`/`-1` — hardcode a 4-byte pointer stride (`arr + i*4`) that's meaningless on x64's 8-byte pointers even if un-stubbed |
| 36 | F2/F3 in-game glyph-position editor | Confirmed working (menu items + gameplay hints) | **ABSENT** | Same root dependency as #35 — the editor needs real focused-item position data that doesn't resolve on x64 |
| 37 | Custom mouse cursor overlay | Confirmed working | **ABSENT — explicit early-return** | `DrawCustomCursorIfNeeded` (`overlay_hud.cpp`) reads raw x86-only addresses (`kCursorVisibleFlagAddr`/`kCursorUiStateAddr`); found as a real, previously-invisible bug (SEH silently swallowed the resulting access violation) and fixed 2026-09-04/05 by adding an honest `#if defined(_M_X64)` early-return stub, same pattern as the other deferred functions |
| 38 | Non-English language glyph-position fix | Confirmed working (v0.3.1) | **N/A** | Moot — the whole glyph system is absent on x64, so there's nothing to be correctly or incorrectly positioned |
| 39 | On-screen notifications (startup "MW32011NCP Started" / hot-reload toast) | Confirmed working | **PRESENT** | `DrawOverlayMessage`/`ShowStartupMessage` (`overlay_hud.cpp`) carry no arch guard and confirmed firing successfully on x64 (`proxy_d3d9.log` showed `DrawPrimitiveUP hr=0x00000000` at least once per `known_issues_x64.md`'s own investigation) |
| 40 | Custom Options screen's own draw/navigate-once-open logic | Confirmed present (preview/WIP on x86) | **PRESENT** | `DrawCustomOptionsMenuIfOpen`/`CustomOptionsMenu_TickInput` (`overlay_hud.cpp`) confirmed genuinely cross-platform by inspection — zero hardcoded addresses in either function |
| 41 | Custom Options screen's real open TRIGGER (native menu-focus detection) | Confirmed present (via `InjectControllerMenuNav`'s focus-read) | **ABSENT — temporary substitute shipped instead** | Real trigger depends on the same #35/#36 x86-only focus-reading infra. Replaced with a documented-as-temporary LB+RB chord (`PollCustomOptionsMenuX64`), gated on `g_menuActiveGateFlag` + `[Options] UseCustomOptionsScreen` (still opt-in, matching x86's default-off convention) |
| 42 | Custom Options screen chord-based open/navigate/close flow | New to x64 (no x86 equivalent needed) | **PRESENT**, unverified live | Build-verified only per `known_issues_x64.md`: "NOT YET LIVE-TESTED" |

### Visual-enhancement suite (v0.3.5)

| # | Feature | x86 status | x64 status | Evidence |
|---|---|---|---|---|
| 43 | `InternalRenderScalePercent` | Confirmed working live (real GPU cost scaling) | **ABSENT — blocked, not a simple port** | `RunFullScreenPostProcessIfEnabled` early-returns unconditionally on x64. Root hook target (`FUN_00679010`'s x64 equivalent) not located across two dedicated attempts — real caller is reached via what both attempts independently conclude is an indirect/function-pointer call, which static xref tooling structurally cannot find. Needs live tracing, not more static analysis, per `known_issues_x64.md`'s own conclusion |
| 44 | `FsrSharpenEnabled`/`FsrSharpenStrength` (FSR 1.0 RCAS) | Confirmed working live | **ABSENT — blocked** | Same early-return function as #43; `clcState`/in-level-flag x64 equivalents genuinely not found despite three independent static techniques |
| 45 | `MotionBlurEnabled`/`Strength`/`CenterFalloff` | Confirmed working live | **ABSENT — blocked** | `RunPreOverlayMotionBlurPassIfEnabled` early-returns unconditionally on x64, same root cause as #43/#44 |
| 46 | `ForceAnisotropicFiltering` | Confirmed working live | **PRESENT** | `overlay_hud.cpp`, no arch guard, real `SetDvarBool` call — confirmed firing on x64 via `[aniso-force]` log evidence |
| 47 | `ForceHighQualityShadows` | Confirmed working live | **PRESENT** | Same file, no guard, `[shadow-quality-force]` log evidence |
| 48 | `ForceHighQualityLighting` | Confirmed working live | **PRESENT** | Same file, no guard, `[lighting-quality-force]` log evidence |

### DualSense / gyro

| # | Feature | x86 status | x64 status | Evidence |
|---|---|---|---|---|
| 49 | DualSense stick input (movement/look, USB + Bluetooth) | Confirmed working (Bluetooth fix live-confirmed, USB not independently re-confirmed by a 2nd tester) | **PRESENT** | `Controller_GetLeftStick`/`GetRightStick` (`controller_input.cpp`) carry no arch guards anywhere in that file and are called directly by `Hook_MovementTick` — transparent abstraction over XInput/DualSense, already in active x64 use |
| 50 | DualSense gyro-aim | Preview/WIP even on x86 (Enabled, Sensitivity, InvertPitch/Yaw, OnlyWhileAds) | **ABSENT** | Zero gyro/DualSense-specific references anywhere in `analog_input_hooks_x64.cpp`. Config keys still generate in a fresh `.ini` (shared `mod_config.cpp`) but have no consumer on x64. Already documented in `known_issues_x64.md`'s "Corrected gap list," confirmed again this pass |

### Configuration & customization

| # | Feature | x86 status | x64 status | Evidence |
|---|---|---|---|---|
| 51 | Config file system (`.ini` generation, self-documenting defaults) | Confirmed working | **PRESENT** | `mod_config.cpp`/`.h` carry zero arch guards anywhere; `LoadModConfig()` called unconditionally from `DllMain` regardless of arch |
| 52 | Config hot-reload (~1s poll, on-screen confirmation) | Confirmed working | **PRESENT** | `CheckConfigHotReload()` — explicitly named in `InjectMenuInputTick`'s own comment as one of "the real, confirmed x64-safe calls" that "stay unconditional" outside the x86-only-guarded block |
| 53 | Config auto-migration (`ConfigVersion` marker) | Confirmed working (v0.2.5+) | **PRESENT** | Same file, same no-guard status; migration logic (`configVersion < kCurrentConfigVersion`) is pure `.ini`-parsing/rewriting logic with no platform dependency |
| 54 | Button layout presets (Default/Tactical/Lefty/**TacticalLefty**) | Confirmed correct against real hardware (2026-07-19) | **PRESENT** | `ResolveButtonMap()` (`mod_config.cpp`, no arch guard) populates the shared `g_buttonMap` global once at config-load time; `analog_input_hooks_x64.cpp` reads `g_buttonMap.<action>` directly throughout — the preset computation itself needed zero x64-specific porting |
| 55 | Stick layout presets (Default/Southpaw/Legacy/LegacySouthpaw) | Confirmed working | **PRESENT** | `RouteStickAxes_Exported()` called directly from `Hook_MovementTick` with `g_modConfig.stickLayout` |
| 56 | `FlipTriggers` | Confirmed working | **PRESENT** | Same `ResolveButtonMap()` call as #54, arch-independent |

### Foundation / infrastructure

| # | Feature | x86 status | x64 status | Evidence |
|---|---|---|---|---|
| 57 | Signature-scanning + D3D9 hook installation (`EndScene`/`Reset`/`CreateDevice`) | N/A (x86 used hardcoded addresses, not signature scanning, as its own historical policy) | **PRESENT, confirmed live** | The foundational, first-confirmed-working piece of the entire x64 port (`known_issues_x64.md` issue #1, "confirmed working, end to end, live," 2026-09-04) |
| 58 | Background 4-thread architecture (poll / vibration-output / config-hot-reload / log-flush) | Confirmed working (issue #87 overhaul) | **PRESENT** (thread infra only) | All 4 `CreateThread` call sites (`controller_input.cpp`, `mod_config.cpp`) carry no arch guards — threads spin up correctly on x64. **Caveat**: the vibration-output thread has nothing feeding it real trigger events, since #20 (vibration) is entirely unported — the thread exists and is harmless, not functionally exercised |
| 59 | Plugin API (`InstallHook`/`RemoveHook`/`ReadMemory`/`WriteMemory`/`Log`/`GetGameWindow`/`GetGameModuleBase`/`SetTextGlyphColorOverride`) | Confirmed working live | **PRESENT, build-verified** | `plugin_loader.cpp` carries no arch guards; `LoadPlugins()` called unconditionally from `DllMain` regardless of arch. Build-verified on x64 per `known_issues_x64.md`, not yet independently exercised end-to-end on x64 with a real third-party plugin |
| 60 | Greenlit trusted-plugin allowlist (auto-loads `mw32011nsp_security.dll` without the opt-in gate) | New, x64-era feature (no x86 equivalent — added after the x86 line was archived) | **PRESENT, build-verified** | `kTrustedPluginFilenames` in `plugin_loader.cpp`. Per `known_issues_x64.md`'s last note on this: could not be end-to-end tested at the time because the NSP DLL didn't exist yet — check current NSP repo state before assuming this is still untestable |
| 61 | Keyboard/mouse non-interference | Confirmed working (a real regression was found and fixed once, issue #10) | **PRESENT, structurally** | x64's Fire/ADS/Reload calls use a synthetic source-id design and Sprint uses bit-ownership tracking, both explicitly modeled on x86's own established non-interference patterns — but no x64-specific keyboard-regression test has been independently run/documented |

### Campaign killstreak-type weapon systems (Boat, UGV, Helicopter door gun, DPV, Mortar, M2 turret, SMAW, AC-130)

Not itemized as separate rows — every one of these rides entirely on core
Fire/ADS/Movement/Look (#1, #2, #6, #7 above, all PRESENT on x64) rather than
having their own dedicated mechanism, matching x86's own design. x86 itself
already scores several of these as 🟡 partial (DPV aim broken, Mortar fire not
wired, M2 turret feels too hard, SMAW lock-on unconfirmed) or ❓ untested
(AC-130) — x64 inherits that same open status by construction, with zero
independent x64-specific testing done. Not counted separately in the Summary
numbers above since they're not distinct code paths.

## Methodology & caveats

- **This was a static, source-code-level audit** — every PRESENT/ABSENT verdict
  above is backed by a direct code trace (a function body, a call site, an
  `#if` guard, or a grep confirming zero references), not an inference from
  existing documentation. Where this pass relied on a finding already recorded
  in `known_issues_x64.md` (e.g. the visual-enhancement-suite blockers, the
  vibration/gyro gaps), that finding was independently re-confirmed by this
  pass via its own direct code read, not merely cited.
- **PRESENT does not mean live-confirmed.** Several rows marked PRESENT are
  explicitly flagged in-file as "NOT YET LIVE-TESTED" (the sniper Fire/ADS
  notify fix, D-pad Left's synthetic-key exception, Jump auto-stand, the
  Custom Options screen's chord-based flow). Treat PRESENT as "real code exists
  and is reachable," not as "confirmed working in actual play" — the latter
  needs the normal manual-playtest bar this project's own `CODE_STANDARDS.md`/
  §8 already requires.
- **What this pass could NOT verify statically, and would need a live test or
  a deeper RE pass to settle:**
  - Whether Sprint's `pm_flags`-forcing on x64 (#22) genuinely produces
    infinite sprint the way x86's original design did, or whether some other
    native mechanism (not yet identified) still applies a duration limit —
    this is a reasoned prediction from x86's own documented history, not a
    directly observed x64 symptom.
  - Whether CrouchProne's (#21) native-dispatch-forwarding design produces
    tap-vs-hold behavior that actually matches x86's documented ladder table
    exactly, or some subtly different result.
  - Whether the sniper Fire/ADS notify fix (#6) also incidentally fixes
    Predator Missile launch (#16) — plausible from the shared mechanism, not
    tested against that specific killstreak.
  - The Precision Airstrike (#18) and Boat/UGV/door-gun/DPV/Mortar/turret/SMAW/
    AC-130 killstreak-type systems' actual x64 behavior — these were never
    independently exercised on x64 at all this session; their status is
    inherited from x86's own (already partial in several cases) documented
    state, not re-verified.
- **What was deliberately excluded from this pass, per the task's hard
  constraints**: no source file was modified. Where a gap this document
  identifies overlaps with work already in progress by a concurrent agent
  (menu-focus/glyph tracking — #33/#34/#35/#36/#41; visual-enhancement-suite
  porting — #43/#44/#45; vibration — #20), this document records the gap for
  planning purposes only and defers the actual fix to that work.
- **Two items flagged as genuinely new findings (Summary, above) were not
  previously written down anywhere in this project** — `known_issues_x64.md`'s
  own "Corrected gap list, 2026-09-12" entry (written before this deeper pass)
  covered vibration and gyro but did not mention menu navigation or Sprint's
  mechanism difference at all. Both are now the most significant open gaps
  found by this audit, ahead of vibration/gyro in practical player impact
  (menu navigation blocks basic controller-only play entirely; Sprint's
  mechanism affects every single sprint action, not an edge case).
