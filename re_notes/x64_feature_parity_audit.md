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

**The 34/21/6 split above is this audit's original snapshot, deliberately
not renumbered as fixes land** (same convention as `PATCHNOTES.md` — a
snapshot describes a point in time; individual row updates below carry the
current truth). As a running tally, not a replacement for reading the
actual table: menu-focus/itemDef tracking, vibration/rumble, the visual-
enhancement suite, native menu navigation, Sprint's kbutton migration,
Survival ready-up, Hold Breath, and DualSense gyro-aim have all moved from
ABSENT/PARTIAL to FIXED since this audit was taken, all same-day
(2026-09-12) — roughly 8 of the original 21+6 non-present items. Back's
`+scores` scoreboard key-synthesis (row 30) was ported the next day
(2026-09-13), a small, cheap, well-understood wiring fix (x86's own function
already had no arch guard) — bringing the running tally to 9.
Auto-Mantle was separately investigated (not just left unattempted) and
found genuinely blocked on a missing native text-draw hook
(`Hook_DrawGlyphText`'s x64 equivalent) — see its own row below and
`known_issues_x64.md` issue #1. **That missing hook shipped 2026-09-13**
(`FUN_14029a2b0`, found via the same anchor-string RE technique x86's own
discovery used, confirmed via 22 real callers — full trail:
`re_notes/x64_migration/drawtext_hook_x64.md`), with Mantle-hint structural-
match detection wired on top — **this resolves Auto-Mantle's own detection
DEPENDENCY specifically** and **not gameplay glyph-icon drawing
generally** (the hook currently only observes text, it never substitutes an
icon for any case, Mantle included) — the honest current count is row by
row, not this summary. **Auto-Mantle's actual `+gostand`-forcing feature
itself then shipped later the same day (2026-09-13)**: `IsSprintActiveX64()`
was composed from three already-existing x64 tracking vars (exact parity
port of x86's own `IsSprintActive()`, no new RE needed) and wired together
with the detection above into the same Jump raw-usercmd-bit block — bringing
the running tally to 10. See row #24 below and `known_issues_x64.md` issue #1's
own "UPDATE 2026-09-13 (later same day)" round for the full trace.
**A separate, concurrent 2026-09-13 session then extended row #34's own
"observes text only" caveat into real substitution for three hint families**
(Mantle, Pickup/Swap/PickupHealth, Throwback grenade) — `RequestCustomHintOverlay`
now actually draws this project's own icon+text for those, suppressing the
native draw, all via a purely structural template match (no font-name
filtering needed). Row #34 is updated to reflect this partial (not full)
coverage — buy-station, Survival ready-up, Sentry-Place, and Reload remain
genuinely absent (see that row's own updated detail and
`re_notes/x64_migration/drawtext_hook_x64.md`'s "Stage (c)" for exactly why
each one is blocked).

**Correction, 2026-09-13 — five rows found silently stale, fixed same day
they were caught.** Rows #20 (vibration/rumble) and #43/#44/#45 (the visual-
enhancement suite: render scale, FSR, motion blur) were all genuinely fixed
in the 2026-09-12 `942936c` commit, but that commit's own docs pass never
came back to annotate these specific rows with a "FIXED" note the way
rows #22/#29/#33 correctly were — anyone reading this table would have
wrongly concluded all four were still blocked. Row #41 (the Custom Options
screen's real open trigger) had the identical problem: fixed the same day
menu-focus tracking landed, never annotated here. All five corrected in
place, same "leave the original finding legible, only the annotation
records the fix" convention as every other correction in this file — this
brings the running tally to 15 (10 from the paragraph above, plus these 5).
Lesson for future sessions: after a multi-file fix commit, re-check this
specific document's own rows for the feature just fixed, don't assume the
commit's other doc updates (README/PATCHNOTES/known_issues_x64.md) covered
it here too — they're separate files with separate update passes.

**Rows #35/#36 (highlighted-item A-glyph, F2/F3 glyph-position editor) FIXED
2026-09-13** — both depended entirely on `TryGetRealFocusedGroupAndIndex`/
`GetMenuStackDepth`'s x64 stubs (row #35's original finding), but neither
feature ever called those raw functions directly — both go through
`TryGetStableFocusedGroupAndIndex()`'s single debounced wrapper, so wiring
that one function's x64 branch to the already-working x64 menu-focus
functions (built 2026-09-12 for the Custom Options screen's own trigger)
closed both rows in one small commit, no new RE required. Brings the
running tally to 17 (15 from the paragraph above, plus these 2). See the
rows themselves for the full detail.

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
   **FIXED same day (2026-09-12), a separate fix pass, per this doc's own note
   above that findings here don't fix anything by themselves**: real x64
   equivalents implemented as `InjectControllerMenuNavX64()`/
   `InjectControllerMenuBackX64()` (`analog_input_hooks_x64.cpp`), driven by
   `ForwardKeyToMenuX64()` — a real x64 signature for `FUN_1402aac50`, confirmed
   as the combined equivalent of x86's `ForwardKeyToMenu` (`0x004d9850`) +
   `FUN_004dfd30` (same keycode switch, case-for-case). Wired into
   `InjectMenuInputTick`'s always-on x64 tick, after `PollCustomOptionsMenuX64()`.
   Two real conflicts found and fixed alongside the port (see row #29/#33 below
   for detail): D-pad actionslot dispatch and CrouchProne's stance dispatch both
   lacked a menu-active gate on x64, which would have double-fired against the
   new native menu nav. Build-verified both platforms; **not yet live-tested**.
   See `re_notes/known_issues_x64.md` issue #1 for the full trail. This
   paragraph is intentionally left in place, not deleted, so the audit's own
   original finding stays legible — only this annotation records the fix.
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
   **FIXED same day (2026-09-12), a separate fix pass, per this doc's own note
   above that findings here don't fix anything by themselves**: `Hook_SprintTick`
   now drives the real kbutton (`FUN_14007e460`/`FUN_14007e490` on
   `DAT_1406448f4`, case `0x3d`/`0x3e` of `FUN_14007c3a0` — the same dispatcher
   already resolved for Fire/ADS/Reload/CrouchProne), matching x86's design
   exactly, plus the rising-edge stand-from-crouch/prone behavior. Build-verified
   both platforms; not yet live-tested. See row #22 below and
   `re_notes/known_issues_x64.md` issue #1 for the full trail. This paragraph is
   intentionally left in place, not deleted, so the audit's own original finding
   stays legible — only this annotation records the fix.

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
| 3 | ADS zoom-aware look-slowdown (`AdsSlowdownStrength`/`Baseline`/`AdsCloseRangeSlowdownStrength`) | Confirmed working | **FIXED (2026-09-13)** — build-verified, not yet live-tested | `Dvar_FindVar`/`GetEffectiveFov`'s x64 equivalents resolved (`FUN_1402c3890`/`FUN_140069e60`, `re_notes/x64_migration/getEffectiveFov_dvarFindVar_x64.md`) and `GetAdsLookRateScaleX64()` (byte-for-byte port of x86's formula) wired into `Hook_MovementTick`'s Look pre-hook (`GetAdsLookRateScaleX64() * GetLookAccelerationScaleX64()`), matching x86's own `InjectControllerLookAngles` sharedScale exactly |
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
| 20 | Vibration/rumble (fire + damage) | Confirmed working, 1.5/2 completeness | **FIXED (2026-09-12)** | Was 100% unported (`Rumble_Install()` only ever called from x86-only-guarded code). Ported same day: fire rumble via `FUN_14016bf50` (x64 equivalent of x86's `FUN_0045e320`, confirmed 3 independent ways), damage rumble via the real x64 entity array (`DAT_140f57cf0`, stride `0x2a0`, confirmed via 3 independent consumers). `Rumble_Install()` now called from `InstallAnalogInputHooksX64()`, `Rumble_Tick()`/`Rumble_TickExpiryWatchdog()` now have real events to consume. Build-verified; **not yet live-tested**. See `re_notes/known_issues_x64.md` issue #1 and `re_notes/x64_migration/rumble_scratch/` for the full RE trail. This paragraph is intentionally left in place per this doc's own convention — only this annotation records the fix |

### Stance & Sprint

| # | Feature | x86 status | x64 status | Evidence |
|---|---|---|---|---|
| 21 | Crouch/Prone 3-state stance ladder (B) | Confirmed working, real native toggle, tap-vs-hold ladder | **PRESENT**, different mechanism | x64 forwards raw press/release edges directly to `FUN_14007c3a0`'s case 0x17/0x18 (`+stance`/`-stance`), trusting native logic to handle tap/hold internally, rather than replicating x86's own explicit tap/hold state machine — a deliberate, reasoned design choice (see the in-file comment on why replicating the ambiguous "restore previous posture" semantics was judged riskier than trusting native dispatch), but the exact resulting behavior (does it match x86's documented tap→crouch/hold→prone table exactly?) is **not independently live-confirmed** |
| 22 | Sprint (L3) | Confirmed working, **real `+sprint` kbutton**, native duration/recovery timer + Extreme Conditioning apply automatically | **FIXED (2026-09-12, same day, separate fix pass)** | `Hook_SprintTick` now calls the real kbutton activate/deactivate handlers (`FUN_14007e460`/`e490`) on `DAT_1406448f4` (case `0x3d`/`0x3e`), matching x86's final design exactly, plus the rising-edge stand-from-crouch/prone behavior (reuses `ForceStandingViaRealToggleX64()`). Build-verified both platforms; **not yet live-tested**. Originally: `Hook_SprintTick` forced the `pm_flags`-equivalent bit directly (`FUN_140014a80`'s own field) — x86's ORIGINAL, deprecated pre-kbutton design. See Summary finding #2 above for the original finding and the fix's full trail |
| 23 | Hold Breath (L3 while ADS'd, sniper) | Confirmed working, real kbutton | **FIXED (2026-09-12, same day, separate fix pass)** | `Hook_SprintTick` now also computes `holdBreathActive = sprintHeld && adsHeldNow` and edge-triggers the real kbutton activate/deactivate handlers on `g_holdBreathStruct` (`DAT_14064482c`, resolved via case 9's own dual-call disassembly + independently re-dumped raw LEA bytes), matching x86's exact gating (no explicit sniper-class check in either platform's own code — the native kbutton itself limits the effect to sniper weapons). A real pre-existing bug in `Hook_SprintTick` (an early `return` that would have starved Hold Breath's own edge check on steady ticks) was found and fixed in the same pass. Build-verified both platforms; **not yet live-tested**. Full trail in `known_issues_x64.md` issue #1, "Hold Breath (L3 while ADS'd) ported to x64" |
| 24 | Auto-Mantle while sprinting | Confirmed working (v0.3.4), ships off by default | **FIXED (2026-09-13, later same day)** — build-verified, not yet live-tested | Was: zero references to `AutoMantle`/`auto.?mantle` anywhere in the x64 file, blocked on the missing native text-draw hook (`known_issues_x64.md` issue #1, 2026-09-12 investigation). That hook shipped (`FUN_14029a2b0`, `re_notes/x64_migration/drawtext_hook_x64.md`) with real Mantle-hint structural-match detection wired on top (`IsMantleHintCurrentlyShowingX64()`), resolving the ledge-availability SIGNAL dependency. The remaining gap ("x64 has no `IsSprintActive()`-equivalent read") turned out smaller than it looked: x86's own `IsSprintActive()` is a plain logical check (`sprintHeld && stance==0 && !adsHeld`), and x64 already had all three pieces for unrelated reasons (`g_sprintKbuttonActiveX64`, `GetRealStanceX64()`, `g_adsHeldX64`) — composed into `IsSprintActiveX64()`, no new RE needed. Wired together with the detection above into `Hook_MovementTick`'s Jump raw-usercmd-bit block (same `kJumpUsercmdBit`/0x400), same 750ms cooldown and stick-forward-cone check as x86. Ships off by default, matching x86's exact default. See `known_issues_x64.md` issue #1's "UPDATE 2026-09-13 (later same day)" round for the full trail |
| 25 | Extreme Conditioning perk override | Resolved "for free" via Sprint's real kbutton | **Now applies "for free," same as x86** (fixed alongside #22 — the real kbutton is what makes this automatic, no separate code needed) | Depends entirely on #22's kbutton design, which x64 now uses |
| 26 | Jump (A) | Confirmed working | **PRESENT** | Raw bit `kJumpUsercmdBit=0x400`, suppressed while a menu is active via `g_menuActiveGateFlag` |
| 27 | Jump auto-stand from crouch/prone | Implemented (`ForceStandingViaRealToggle`) | **PRESENT**, w/ caveat | `ForceStandingViaRealToggleX64()`, ported 2026-09-05 — listed in `known_issues_x64.md` as "still awaiting live confirmation" as of that entry |

### Menu & pause

| # | Feature | x86 status | x64 status | Evidence |
|---|---|---|---|---|
| 28 | Start — pause menu open AND close | Confirmed working, real engine calls | **PRESENT** | `g_pauseToggle` (`FUN_1400823b0`) called from both the gameplay tick and `PollPauseToggleX64()` (always-on tick) — confirmed working live per `known_issues_x64.md` |
| 29 | B — back out of menus (real ESC-forward) | Confirmed working | **FIXED (2026-09-12)** | `InjectControllerMenuBackX64()` (`analog_input_hooks_x64.cpp`) ports x86's `InjectControllerMenuBack()` directly, calling `ForwardKeyToMenuX64(0x1b, ...)` on B's edge changes while a menu is active and the custom Options overlay isn't open. Also fixes a real conflict found during the port: CrouchProne's stance dispatch (same physical B button) had no menu-active gate on x64 at all -- fixed via a new shared `g_currentBPressTouchedMenuX64` flag, x64's own equivalent of x86's `g_currentBPressTouchedMenu`, avoiding a real stuck-crouch/prone regression risk. Build-verified; **not yet live-tested** |
| 30 | Back button (`+scores` scoreboard/objectives) | ✅ on x86 — implemented, confirmed via direct Xbox 360 console testimony to be a genuine no-op in Campaign/Survival on every platform (no scoreboard UI exists in SP at all, `known_issues.md` issue #28), not an undiagnosed bug | **PORTED (2026-09-13)** | `SendSyntheticScoreboardKeyX64()` (`analog_input_hooks_x64.cpp`) wired into `Hook_MovementTick`'s existing edge-tracking block, gated on `g_buttonMap.scoreboard`, same hold-through-passthrough `PostMessageA(VK_TAB)` mechanism as x86's `InjectControllerScoreboard()` (a pure wiring port, no new RE — x86's own function already had no arch guard, per the row's prior finding). Correctly expected to remain a visible no-op in SP (see the x86 column) — real value arrives once Multiplayer (which has a genuine scoreboard) ships. Build-verified both platforms; not yet live-tested (expected outcome recorded in `re_notes/x64_live_testing_checklist.md`: confirm no visible change in SP, not "confirm it works") |
| 31 | Survival ready-up (hold Y ~740ms) | Confirmed working, synthetic-key exception #1 of 3 | **FIXED (2026-09-12; IsInSurvivalMode() gate closed 2026-09-13)** | `SendSyntheticF5X64()` (`analog_input_hooks_x64.cpp`) ports x86's `SendSyntheticF5`/`InjectControllerWeaponNext` hold-vs-tap split directly onto the existing `g_weaponNext` dispatch, using `GetGameWindow()`/`PostMessageA` (same mechanism as `SendSyntheticActionSlot4KeyX64`). x86's extra `IsInSurvivalMode()` gate — originally omitted pending x64's `Dvar_FindVar` equivalent — is now wired at the same call site (`IsInSurvivalModeX64()`, reading the real `mapname` dvar via the now-resolved `FUN_1402c3890`/`FUN_1402c3b50`), matching x86's own call-site gate structure exactly. Build-verified; **not yet live-tested** |
| 32 | Buy-station + pause interaction fix | Confirmed working (fixes a real native bug) | **UNKNOWN / not reachable** | This fixed a real native engine bug, not this mod's own feature — but since controller-driven buy-station navigation itself doesn't work on x64 at all (see #34), this is currently moot for controller players; whether the underlying native bug still exists on x64 (relevant to mouse/keyboard buy-station+pause use) was not independently re-checked this pass |
| 33 | Native D-pad+A menu/UI navigation (main menu, pause menu, options two-pane drill, buy-station/armory lists, slider VALUE adjustment) | Confirmed working, full coverage (task #22) | **FIXED (2026-09-12) — D-pad/A/Y/X/Back-button, build-verified, not yet live-tested; slider VALUE adjustment not independently confirmed** | `InjectControllerMenuNavX64()` (`analog_input_hooks_x64.cpp`) ports x86's `InjectControllerMenuNav()` -- Up/Down/Left/Right forward the same real alt-keycodes (`0x9a`/`0x9b`/`0x9c`/`0x9d`) to `ForwardKeyToMenuX64()`, A forwards Enter (`0xd`), Y/X/Back-button send the same synthetic Friends/Game-Summary/Leaderboards keys x86 does. LB/RB tab-prev/next needed no new code -- `PollCustomOptionsMenuX64` already owns them. `ForwardKeyToMenuX64()` itself resolves `FUN_1402aac50`, the real x64 combined equivalent of x86's `ForwardKeyToMenu` (`0x004d9850`) + `FUN_004dfd30`, confirmed via an internal switch(keyCode) that matches x86's case-for-case. A real conflict found during the port: x64's D-pad actionslot dispatch (`Hook_MovementTick`) had no menu-active gate at all (x86's `InjectControllerDpad` does) -- fixed, matching x86's symmetric press/release gate exactly, so native D-pad menu-nav and the raw actionslot dispatch can't double-fire while a non-pause menu is open during live gameplay (e.g. a Survival buy station). Custom Options screen's own D-pad/A ownership while open is preserved via a `CustomOptionsMenu_IsOpen()` read (`PollCustomOptionsMenuX64` now required to run first in the same tick, wired that way in `InjectMenuInputTick`). Whether slider VALUE adjustment specifically works end-to-end (it rides the same Left/Right forward as options drill-in/out on x86, per that engine behavior) was not independently re-verified this pass. See `re_notes/known_issues_x64.md` issue #1 for the full trail |

### Button-glyph UI / F2-F3 editor / custom cursor

| # | Feature | x86 status | x64 status | Evidence |
|---|---|---|---|---|
| 34 | Button-glyph UI prompts (in-game interact hints, menu corner hints) | Confirmed working | **PARTIAL (2026-09-13) — Mantle/Pickup-Swap-PickupHealth/Throwback visually working; buy-station/ready-up/Reload/Sentry-Place/menu hints still ABSENT** | Was: `known_issues_x64.md`'s own follow-up audit: "`analog_input_hooks_x64.cpp` makes zero calls to any glyph/hint-request function... `DrawGlyphIconIfRequested`'s own gate... simply never gets set to true on x64." The native text-draw hook (x86's `Hook_DrawGlyphText` target, `FUN_14029a2b0`) shipped 2026-09-13 with Mantle-hint DETECTION only, then a same-day follow-up pass extended it to real visual SUBSTITUTION (`RequestCustomHintOverlay` now actually called, native draw suppressed) for **Mantle, Pickup/Swap/PickupHealth, and Throwback grenade** — all three detected via an exact structural template match against the real, live-resolved reference-key text (`PLATFORM_MANTLE`/`PLATFORM_PICKUPNEWWEAPON`/`PLATFORM_SWAPWEAPONS`/`PLATFORM_PICKUPHEALTH`/`PLATFORM_THROWBACKGRENADE`, all confirmed via fresh decompile to flow through this exact draw call), so font-name filtering was not needed for these three. **Buy-station and Survival ready-up remain absent** — neither has a known reference-key template even on x86 (x86 gates them via `IsGameplayHintFont` instead), and x64's `Font_s` `fontName` offset is still unconfirmed (only `pixelHeight`@+0x08/`glyphCount`@+0x0C/`DiagGlyph*`@+0x20 were recovered this pass, `fontName`@+0x00 is an alignment inference only). **Reload remains absent for a structural reason**: confirmed via decompile that it flows through a completely different native draw function (`FUN_1402afa60`), which this hook can never observe. **Sentry-Place remains absent**: its own reference string was searched for across the whole x64 binary and found zero times. **Menu corner hints** (Back/Friends) were not attempted this pass. No position/scale nudge tuning was ported for the three working cases either — on-screen alignment is unverified pending live test. Full trail: `re_notes/x64_migration/drawtext_hook_x64.md` |
| 35 | Highlighted-item A-glyph (menu list navigation) | 🟡 on x86 too (draws where manually calibrated) | **FIXED (2026-09-13), not yet live-tested** | Was: depends on `TryGetRealFocusedGroupAndIndex`/`GetMenuStackDepth` (`analog_input_hooks.cpp`), both explicit x64 stubs returning `false`/`-1` — hardcode a 4-byte pointer stride (`arr + i*4`) that's meaningless on x64's 8-byte pointers even if un-stubbed. Fix: neither consumer ever called the raw functions directly — both go through `TryGetStableFocusedGroupAndIndex()`'s debounced wrapper, whose x64 `#else` branch was the actual dead stub. Gave that ONE wrapper a real x64 branch (structurally identical debounce logic) calling the already-working, already-verified `TryGetRealFocusedGroupAndIndexX64`/`GetMenuStackDepthX64` (`analog_input_hooks_x64.cpp`, built 2026-09-12 for the Custom Options screen's own open-trigger) via two new thin `extern "C"` wrappers, same anonymous-namespace-internal-linkage fix already applied elsewhere in this file. No new RE. Build-verified both platforms; not yet confirmed live. See `known_issues_x64.md` issue #1's own "menu-focus" entry and `CLAUDE.md`'s Version Timeline for the full trail. |
| 36 | F2/F3 in-game glyph-position editor | Confirmed working (menu items + gameplay hints) | **FIXED (2026-09-13), not yet live-tested** | Same root dependency and same fix as #35 — `EditGlyphPositionsForFrame`'s caller (`ResetMenuListItemOrdinalForFrame`) also calls only `TryGetStableFocusedGroupAndIndex()`, never the raw function, so wiring that one wrapper closes both rows at once. F3 export itself (`ExportGlyphEditPositions`) was already arch-independent and untouched. |
| 37 | Custom mouse cursor overlay | Confirmed working | **FIXED (2026-09-13), not yet live-tested** | Was blocked on the 2026-09-04/05 `#if defined(_M_X64)` early-return stub (`DrawCustomCursorIfNeeded`, `overlay_hud.cpp`), added after the raw x86-only `kCursorVisibleFlagAddr`/`kCursorUiStateAddr` reads were found to silently SEH-swallow an access violation on x64. Resolved same day via `analog_input_hooks_x64.cpp`'s new `kCursorGateSignature`: `DAT_142615b20` (the x64 UI-state global, independently already confirmed elsewhere in this file as `DAT_01c0ad14`'s real equivalent via `SetMenuState`'s own literal mode writes) led to `FUN_14029d170`, the x64 native cursor-draw dispatcher — structurally identical to x86's `FUN_00478540` (same visFlag/uiState gate shape, same `"sp_acceptinvite_warning[_nosave]"` string check, same `"ui_cursor"`-asset draw call) and confirmed via IDENTICAL struct-offset arithmetic (`DAT_14260506c`/position pair sit at the same +0x1c/+0x10 offsets from their UI-context base that x86's own `DAT_01c00474`/`DAT_01c00468`/`046c` do). Exposed via `TryGetCursorGateX64()`, fails closed. A second, previously-invisible bug found while wiring this: the function's own `IsMenuActive_Exported()` gate call is x86-only real (its x64 stub always returns `false`), which would have made the WHOLE function a permanent no-op on x64 outside the glyph-position editor — fixed by branching to the real, already-resolved `IsMenuActiveX64_Exported()` on that platform. Build-verified both platforms (x64 `/t:Rebuild` 0 errors, `dumpbin`-confirmed `8664 machine (x64)` fresh timestamp, Win32 regression rebuild 0 errors, x64 redeployed last); **not yet live-tested**. Full trail: `re_notes/known_issues_x64.md` issue #1's own "Custom mouse cursor overlay" round |
| 38 | Non-English language glyph-position fix | Confirmed working (v0.3.1) | **N/A** | Moot — the whole glyph system is absent on x64, so there's nothing to be correctly or incorrectly positioned |
| 39 | On-screen notifications (startup "MW32011NCP Started" / hot-reload toast) | Confirmed working | **PRESENT** | `DrawOverlayMessage`/`ShowStartupMessage` (`overlay_hud.cpp`) carry no arch guard and confirmed firing successfully on x64 (`proxy_d3d9.log` showed `DrawPrimitiveUP hr=0x00000000` at least once per `known_issues_x64.md`'s own investigation) |
| 40 | Custom Options screen's own draw/navigate-once-open logic | Confirmed present (preview/WIP on x86) | **PRESENT** | `DrawCustomOptionsMenuIfOpen`/`CustomOptionsMenu_TickInput` (`overlay_hud.cpp`) confirmed genuinely cross-platform by inspection — zero hardcoded addresses in either function |
| 41 | Custom Options screen's real open TRIGGER (native menu-focus detection) | Confirmed present (via `InjectControllerMenuNav`'s focus-read) | **FIXED (2026-09-12)** | Was blocked on the same #35/#36 x86-only focus-reading infra, substituted with a temporary LB+RB chord. Resolved same day: `TryGetRealFocusedGroupAndIndexX64`/`GetTopmostActiveMenuX64` (real x64 menu-focus/itemDef-array tracking, independently re-derived offsets for x64's different 64-bit-aligned struct layout) wired as the real trigger, OR'd with the existing LB+RB chord into one `openRequestedEdge` — the chord is kept as a fallback, not replaced outright, until the real trigger is live-confirmed. A `[x64-optmenu-realtrigger]` log line fires on real activation. Note: this does NOT retroactively fix #35/#36 (the A-glyph/F2-F3-editor) — those consume a *different* pair of functions (the old x86-only stubs in `analog_input_hooks.cpp`), never re-pointed at this new x64 implementation. Build-verified; **not yet live-tested**. This paragraph is intentionally left in place — only this annotation records the fix |
| 42 | Custom Options screen chord-based open/navigate/close flow | New to x64 (no x86 equivalent needed) | **PRESENT**, unverified live | Build-verified only per `known_issues_x64.md`: "NOT YET LIVE-TESTED" |

### Visual-enhancement suite (v0.3.5)

| # | Feature | x86 status | x64 status | Evidence |
|---|---|---|---|---|
| 43 | `InternalRenderScalePercent` | Confirmed working live (real GPU cost scaling) | **FIXED (2026-09-12)** | Was blocked across two dedicated attempts (indirect/function-pointer call, resisted static xref tooling). Resolved same day: `FUN_1401bd1d0` confirmed as the real x64 equivalent of x86's `FUN_00679010`, hooked via MinHook, same override-before-native-call design as x86. Build-verified; **not yet live-tested**. This paragraph is intentionally left in place per this doc's own convention — only this annotation records the fix |
| 44 | `FsrSharpenEnabled`/`FsrSharpenStrength` (FSR 1.0 RCAS) | Confirmed working live | **FIXED (2026-09-12)** | Was blocked (same early-return as #43; `clcState`/in-level-flag genuinely not found across three independent static techniques). Resolved same day: `clcState` resolved cleverly as a fixed `+8` byte offset from the already-resolved `g_menuActiveGateFlag` pointer (no separate signature scan needed); the in-level flag resolved via its own dedicated signature. All three of x86's proven-necessary safety gates (menu-active, clcState, in-level) now wired, fail-closed, matching x86's gating exactly — deliberately not shipped on a weaker gate than x86's own issue #103/#104 crash history proved necessary. Build-verified; **not yet live-tested**. This paragraph is intentionally left in place — only this annotation records the fix |
| 45 | `MotionBlurEnabled`/`Strength`/`CenterFalloff` | Confirmed working live | **FIXED (2026-09-12)** | Same root cause and same-day fix as #44 (shares all three safety gates). Per-frame yaw/pitch deltas sourced from `Hook_MovementTick`'s own already-computed look-injection data, not a new RE target. Build-verified; **not yet live-tested**. This paragraph is intentionally left in place — only this annotation records the fix |
| 46 | `ForceAnisotropicFiltering` | Confirmed working live | **PRESENT** | `overlay_hud.cpp`, no arch guard, real `SetDvarBool` call — confirmed firing on x64 via `[aniso-force]` log evidence |
| 47 | `ForceHighQualityShadows` | Confirmed working live | **PRESENT** | Same file, no guard, `[shadow-quality-force]` log evidence |
| 48 | `ForceHighQualityLighting` | Confirmed working live | **PRESENT** | Same file, no guard, `[lighting-quality-force]` log evidence |

### DualSense / gyro

| # | Feature | x86 status | x64 status | Evidence |
|---|---|---|---|---|
| 49 | DualSense stick input (movement/look, USB + Bluetooth) | Confirmed working (Bluetooth fix live-confirmed, USB not independently re-confirmed by a 2nd tester) | **PRESENT** | `Controller_GetLeftStick`/`GetRightStick` (`controller_input.cpp`) carry no arch guards anywhere in that file and are called directly by `Hook_MovementTick` — transparent abstraction over XInput/DualSense, already in active x64 use |
| 50 | DualSense gyro-aim | Preview/WIP even on x86 (Enabled, Sensitivity, InvertPitch/Yaw, OnlyWhileAds) | **FIXED (2026-09-12)** | Same "mechanism exists, never wired into the x64 tick" shape as the vibration gap — `Controller_GetGyroRate` (`controller_input.cpp`) has no arch guard at all. Wired into `Hook_MovementTick`'s LOOK PRE-hook block (`analog_input_hooks_x64.cpp`), applied additively onto the yaw/pitch accumulators and motion-blur delta feed after the stick-look branch (confirmed matching x86's own `=` then `+=` pattern in `InjectControllerLookAngles` before writing this). Stays PREVIEW/WIP, same as x86 — issues #76/#77 carry over unchanged. Build-verified; **not yet live-tested** (needs real DualSense hardware) |

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
