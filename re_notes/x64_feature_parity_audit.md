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

**Row #16 (Predator Missile launch) strengthened 2026-09-13 — not a code fix,
a confirmation-strength upgrade, so the running tally above is unaffected.**
A dedicated static-RE task (fresh Ghidra decompiles of `FUN_14007c3a0`,
`FUN_14007fc00`, and a direct memory dump of the x64 bind-name table)
replaced the prior "plausible shared mechanism, never connected to Predator
Missile specifically" framing with a structural proof that controller
Fire-down sends the byte-identical `"n 1"` string a real x64 keyboard
`+attack` press already sends natively — see row #16's own updated evidence
column and `known_issues_x64.md`'s matching dated round for the full trail.
Still not counted as a tally-moving fix because the code was already there
and unchanged; what changed is how confident this document is in it, and it
remains explicitly NOT live-tested.

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

**Audit-completeness sweep, 2026-09-13 — 7 new rows added (#62-68), separate
from this document's original 61-row snapshot.** A full six-way parallel sweep
of the ENTIRE x86-era git history (539 commits, project start through the
discontinued `v0.3.5-x86` release) found 7 real, shipped x86 features/fixes
with zero mention anywhere in this table, `README.md`, or
`known_issues_x64.md` — this document's original pass (2026-09-12) was
sourced from `legacy-x86-docs/README.md` and `CLAUDE.md`'s own timeline
entries, not a raw commit-by-commit history read, so a real gap in that
sourcing method was always possible; this sweep is the first pass to check
against git history directly rather than existing summaries. Each of the 7
was independently re-verified against its real x86 origin commit(s) via
`git show`, then traced through the current x64 source to a real, precise
verdict (not "no arch guard visible" alone — the actual call chain from a
confirmed-reachable x64 entry point was traced in every case). Six confirmed
**PRESENT** (rows #62, #63, #65, #66, #67 were already fully working on x64,
inherited "for free" from shared/arch-neutral code paths already reachable
from the confirmed x64 pipeline; row #68 was genuinely absent and was
fixed in this same pass — a small, well-understood, low-risk port, build-
verified both platforms). **One confirmed PARTIAL and left as a new,
precisely-documented open item, not fixed this pass** (row #64, the Custom
Options screen's real depth) — this is a materially larger and more
consequential finding than any of the other six: the screen's entire UI
shell (navigation, tabs, mouse clicks, controller-photo diagrams) is real
and reachable on x64, but every one of the 7 real vanilla-game-setting tabs
(Look/Video/Audio/Voice/AdvancedVideo/Movement/Actions — the actual
substance of x86's Aug 6 "every option from native and our mod" expansion)
silently reads and writes nothing at all on x64, because `real_settings.cpp`'s
dvar/keybind read+write functions are either x86-only `__asm` (reads) or
explicit x64 no-op stubs (every write). A controller/mouse/keyboard user can
open the screen, navigate every tab, and interact with every row — and
nothing they do to a real vanilla setting has any effect, while the screen
gives no visual indication of this. Only the Controller tab and the new
Custom Binds tab (both backed by this mod's own `g_modConfig`, not real
dvars) are actually functional. See row #64's own Evidence column for the
full trace; left as an open item per the task's own instruction, not
attempted as a fix in this pass (closing it needs real x64 RE work: the
x86 `FindDvar`/`SetDvarBool`/`SetDvarFloat`/`SetDvarString`/`GetKeybind`/
`SetKeybind`/`KeyNameToKeynum`/`KeynumToDisplayName` custom-register-
convention internals all need real x64 equivalents found and resolved,
mirroring the same signature-scanning work already done for
`Dvar_FindVar`/`GetEffectiveFov` in row #3).

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
| 6 | Fire (RT) | Confirmed working | **FIXED (2026-09-13), build-verified, NOT YET LIVE-TESTED** — root cause found and corrected, distinct from the earlier sniper/notify investigation | `g_kbuttonActivate`/`Deactivate` on `g_fireStruct` is (and always was) structurally correct. The real cause of the "intermittent, on and off" symptom reported live across all weapons (pistol included): `Hook_MovementTick` (the function these calls live in) had a stray `if (moveX == 0.0f && moveY == 0.0f) return;` that early-returned the ENTIRE function — not just the movement-byte write two lines below it — whenever the left stick was centered. Every downstream control (Fire, ADS, Reload, Weapnext, Melee, Lethal, Tactical, Jump, Interact, D-pad, CrouchProne, Scoreboard, the gameplay-tick Pause-open poll, `Rumble_Tick`) lived below that line and was silently skipped on every tick the player wasn't also pushing the movement stick — i.e. exactly the moments a player stands still to aim/fire carefully. x86's own `InjectAllControllerInput` (`analog_input_hooks.cpp`) calls `InjectControllerMovement`/`Ads`/`Fire`/`Reload`/etc. as fully independent functions with no such cross-dependency, confirming this was an x64-introduced regression from fusing everything into one tick function, not an engine behavior change. Fixed by scoping the early-out to just the `cmd[0x1c]`/`[0x1d]` movement write. The earlier `g_notifyBindDispatch`/"n 1" reliable-command-notify fix (sniper-only hypothesis, then broadened, never confirmed) is left in place — additive/inert, not implicated in this root cause, not removed. See `known_issues_x64.md`'s 2026-09-13 "root cause found" round for the full trace |
| 7 | ADS (LT) hold-to-aim | Confirmed working, real kbutton | **PRESENT** | kbutton call + explicit force of `g_adsToggleFlag` (the real "is aiming" flag) — went through 3 live-test-driven correction rounds documented in-file, currently believed correct |
| 8 | Melee (R3) | Confirmed working | **PRESENT** | Raw usercmd bit `kMeleeUsercmdBit=0x4`, mirrors x86 |
| 9 | Reload (X) | Confirmed working | **PRESENT** | kbutton call on `g_reloadStruct` |
| 10 | Interact hold-vs-tap (X) | Confirmed working, 300ms hold | **PRESENT** | `g_interactPressStartMsX64` + `g_modConfig.interactHoldThresholdMs`, mirrors x86 exactly |
| 11 | Weapon switch (Y) / `weapnext` | Confirmed working | **PRESENT** | `g_weaponNext(0,1)` direct call on press edge |
| 12 | D-pad Up/Right/Down (`+actionslot 1-3`) | Confirmed working | **PRESENT** | `g_actionSlot` direct call |
| 13 | D-pad Left (AI squadmate call-in, synthetic-key exception) | Confirmed working (x86's 2nd of 3 exceptions) | **PRESENT**, w/ caveat | `SendSyntheticActionSlot4KeyX64` ported 2026-09-05; in-file comment: "expected to exist here too, unconfirmed until live-tested" |
| 14 | Lethal (RB) | Confirmed working | **PRESENT** | Raw bit `kLethalUsercmdBit=0x4000` |
| 15 | Tactical (LB) | Confirmed working | **PRESENT** | Raw bit `kTacticalUsercmdBit=0x8000` |
| 16 | Killstreak: Predator Missile — launch | Fixed via `FireNotifyQueueKick` (pushes literal `"n 1"` client command) | **STRUCTURALLY CONFIRMED (2026-09-13), still NOT live-tested** | A dedicated static-RE pass (fresh Ghidra decompiles, not just re-reading existing comments) closed the gap between "plausible shared mechanism" and "provably the correct argument," via two independent lines of evidence that both land on the same value: (1) `FUN_14007c3a0`'s own decompile shows it calls `FUN_14007fc00(param_1,param_2)` **unconditionally, as the literal first thing it does for every non-zero case, before the switch** — i.e. this is what happens for a REAL keyboard `+attack` press natively, not something the mod introduces; case 1 of that same switch is independently confirmed = "+attack" (calls the kbutton pair on `&DAT_140644818`, the exact address `kFireStructInsnOffset`/`g_fireStruct` already resolves). (2) A fresh decompile of `FUN_14007fc00` itself confirms it does nothing but gate (`FUN_14026afa0`: connection state == 2; `FUN_140265a20`: demo/override check) and then `sprintf`-format `param_2` directly (no separate table lookup) into `DAT_1403f5fd4`, read raw and confirmed to literally be `"n %i"`. Since `g_notifyBindDispatch = FUN_14007fc00` and `kFireBindCaseDown = 1`, controller Fire-down therefore sends the byte-identical `"n 1"` a real x64 keyboard `+attack` press already sends via this same native call chain — not merely "the same command name," the same argument, by construction. **Independently cross-checked from the receiving side too**: `FUN_14007eff0` (the x64 bind-name→index resolver, an 81-entry/`0x51` table at `PTR_DAT_1404c1870` — same size as x86's own confirmed 81-entry table) was dumped directly; index 1 in that table is literally the string `"+attack"`, matching x86's own independently-confirmed index-1-="+attack" finding exactly. Two unrelated methods (dispatch-case address matching, and direct bind-table memory dump) converge on the same number for the same reason x86's fix works. Residual, honestly-unclosed gaps: (a) this is a static-RE argument, not a live repro — no live playtest has confirmed the missile actually launches on x64; (b) the outer gate `FUN_14007c3a0` checks before calling `FUN_14007fc00` (`FUN_140078f00(param_1)!=0 && DAT_1405145a8!=0`, described in-file as a client-ready check) is bypassed by the mod's direct call — believed inert during ordinary gameplay (matches the existing in-file reasoning for the sniper fix), not independently re-verified; (c) x64 has no config-toggle equivalent to x86's `[Experimental] FireNotifyQueueKick` — the notify fires unconditionally whenever `g_notifyBindDispatch` resolves, which mirrors x86's own default-on behavior and is not itself a bug, just a missing parity toggle, left as-is since it isn't broken. See `known_issues_x64.md`'s Predator Missile round (2026-09-13) for the full RE trail and file/address citations |
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
| 33 | Native D-pad+A menu/UI navigation (main menu, pause menu, options two-pane drill, buy-station/armory lists, slider VALUE adjustment) | Confirmed working, full coverage (task #22) | **Main menu LIVE-CONFIRMED 2026-09-13** ("menus are on par from what i saw main menu wise") — D-pad/A navigation and selection works as expected. Pause menu/options drill-down/buy-station/armory/slider adjustment remain build-verified only, not yet independently exercised | `InjectControllerMenuNavX64()` (`analog_input_hooks_x64.cpp`) ports x86's `InjectControllerMenuNav()` -- Up/Down/Left/Right forward the same real alt-keycodes (`0x9a`/`0x9b`/`0x9c`/`0x9d`) to `ForwardKeyToMenuX64()`, A forwards Enter (`0xd`), Y/X/Back-button send the same synthetic Friends/Game-Summary/Leaderboards keys x86 does. LB/RB tab-prev/next needed no new code -- `PollCustomOptionsMenuX64` already owns them. `ForwardKeyToMenuX64()` itself resolves `FUN_1402aac50`, the real x64 combined equivalent of x86's `ForwardKeyToMenu` (`0x004d9850`) + `FUN_004dfd30`, confirmed via an internal switch(keyCode) that matches x86's case-for-case. A real conflict found during the port: x64's D-pad actionslot dispatch (`Hook_MovementTick`) had no menu-active gate at all (x86's `InjectControllerDpad` does) -- fixed, matching x86's symmetric press/release gate exactly, so native D-pad menu-nav and the raw actionslot dispatch can't double-fire while a non-pause menu is open during live gameplay (e.g. a Survival buy station). Custom Options screen's own D-pad/A ownership while open is preserved via a `CustomOptionsMenu_IsOpen()` read (`PollCustomOptionsMenuX64` now required to run first in the same tick, wired that way in `InjectMenuInputTick`). Whether slider VALUE adjustment specifically works end-to-end (it rides the same Left/Right forward as options drill-in/out on x86, per that engine behavior) was not independently re-verified this pass. See `re_notes/known_issues_x64.md` issue #1 for the full trail |

### Button-glyph UI / F2-F3 editor / custom cursor

| # | Feature | x86 status | x64 status | Evidence |
|---|---|---|---|---|
| 34 | Button-glyph UI prompts (in-game interact hints, menu corner hints) | Confirmed working | **PARTIAL (2026-09-13, re-investigated TWICE same day) — Mantle/Pickup-Swap-PickupHealth/Throwback/Reload/Back/Friends/Quit/Leaderboards/GameSummary visually working (nine cases) + Special-Ops-modal/Friends-list-open suppression logic also wired; buy-station/ready-up/Sentry-Place still ABSENT, genuinely BLOCKED (Font_s.fontName unconfirmed on x64), not just unattempted** | Was: `known_issues_x64.md`'s own follow-up audit: "`analog_input_hooks_x64.cpp` makes zero calls to any glyph/hint-request function... `DrawGlyphIconIfRequested`'s own gate... simply never gets set to true on x64." The native text-draw hook (x86's `Hook_DrawGlyphText` target, `FUN_14029a2b0`) shipped 2026-09-13 with Mantle-hint DETECTION only, then a same-day follow-up pass extended it to real visual SUBSTITUTION (`RequestCustomHintOverlay` now actually called, native draw suppressed) for **Mantle, Pickup/Swap/PickupHealth, and Throwback grenade** — all three detected via an exact structural template match against the real, live-resolved reference-key text (`PLATFORM_MANTLE`/`PLATFORM_PICKUPNEWWEAPON`/`PLATFORM_SWAPWEAPONS`/`PLATFORM_PICKUPHEALTH`/`PLATFORM_THROWBACKGRENADE`, all confirmed via fresh decompile to flow through this exact draw call), so font-name filtering was not needed for these three. **Reload and menu corner hints (Back/Friends) were then ALSO confirmed working the same day** — an earlier finding claimed Reload flowed through a completely different, unobservable native draw function (`FUN_1402afa60`); that turned out to be a decompiler artifact (`-noanalysis` misreads it as a bare one-line tail-forward) — one more hop of RE (`FUN_1402afa60` → `FUN_1402b1090` → the already-hooked `FUN_14029a2b0`) confirmed it's fully reachable, and the same investigation found menu corner hints reachable through the identical path, previously believed simply not attempted. **A THIRD same-day follow-up (menu-hint parity check) then closed the remaining menu-hint gap almost entirely.** The prior state of this row (and this hook's own in-code header comment) claimed Quit/Leaderboards/Game-Summary's literal-text special cases, the corner-hint-row positional-tolerance check, and the Special-Ops-nested-modal/Friends-list-open suppression logic all depended on "x86-only menu-focus/itemDef-position infrastructure not yet ported to x64" — RE-CHECKED against x86's own originals in full (per this project's own compare-to-x86-original rule) and found **stale, not actually true**: `looksLikeCornerHintRow` reads only the draw call's own raw `y` parameter (no itemDef dependency at all, ported directly as `looksLikeCornerHintRowX64`); Quit/Leaderboards/Game-Summary are plain resolved-template string compares (same class as the already-working Back/Friends); and the Special-Ops/Friends-list suppression needs the FOCUSED ITEM'S RAW NAME specifically, not the `(group,index,siblingCount,depth)` tuple `TryGetRealFocusedGroupAndIndexX64` exposes (that function deliberately returns false for non-"`<group>_<index>`"-shaped names like "Chaos"/"friendList"/"none" — exactly the names this logic needs) — closed via `TryGetRealFocusedItemNameX64`, a small, confident extension reusing the SAME already-live-confirmed itemDef-array walk/offsets the row #35/#36 fix validated (2026-09-12), no new RE. All three (Quit, Leaderboards, Game-Summary) plus the suppression logic are now wired in `Hook_DrawTextX64` (`analog_input_hooks_x64.cpp`); build-verified both platforms (x64 `/t:Rebuild` 0 errors, `dumpbin`-confirmed `8664 machine (x64)` fresh timestamp, Win32 regression rebuild 0 errors, x64 redeployed last), **not yet live-tested**. **Buy-station and Survival ready-up remain absent** — neither has a known reference-key template even on x86 (x86 gates them via `IsGameplayHintFont` instead, a genuinely different, font-family-based discriminator that Quit/Leaderboards do NOT rely on — those use position, not font). A dedicated same-day follow-up session spent real, multi-angle effort trying to independently confirm x64's `Font_s.fontName` offset via decompile specifically to unlock these two (string scan for the real lowercase font-name literals, a full trace of the font load/asset-cache chain — which turned out to be a generic, type-agnostic system where the cache entry, not the payload, tracks names for lookup purposes — and an audit of every confirmed consumer of the actual `Font_s*` payload, none of which dereference offset +0x00) and **could not confirm it** — a genuine negative result, not a skipped step. Per this project's own "no unconfirmed-offset OOB read" standard, no fontName-gated substitution was wired; only `pixelHeight`@+0x08/`glyphCount`@+0x0C/`DiagGlyph*`@+0x20 are decompile-confirmed, `fontName`@+0x00 remains unconfirmed. **Sentry-Place remains absent**: its own reference string was searched for across the whole x64 binary and found zero times. No position/scale nudge tuning was ported for any of the nine working cases — on-screen alignment is unverified pending live test. **Live-tested 2026-09-13: this predicted risk is now a confirmed real bug** — Mantle's icon substitution suppresses the native text but draws no visible icon; Interact/Reload's substituted text renders at the very top of the screen instead of near the real prompt location. Both trace through the same `ConvertRealScreenPosToDesignSpaceX64` position pipeline, differing only in `centerOnScreen` — likely the same underlying wrong coordinate, not two separate bugs. **ROOT-CAUSED same day, fix shipped as a HYPOTHESIS (not yet live-tested)**: a fresh decompile+disassembly of `FUN_14029a2b0` (this hook's own target) found the prior header comment's "x/y already = final screen-pixel position" claim was WRONG — the REAL position isn't computed until `FUN_14008d020` runs (a per-draw-context scale-multiply + alignment-mode anchor-offset add, selected by `color1`/`color2`, which turned out to be an 11-way alignment enum, not RGBA colors), which happens INSIDE `FUN_14029a2b0` AFTER this hook's own interception point — so the x/y this hook captured were always pre-transform, local-unit coordinates, not real screen pixels. Fixed via `ComputeRealDrawPositionX64` (`analog_input_hooks_x64.cpp`), which calls the REAL native transform directly (two new direct-call signature resolves, `FUN_1401b7c90`/`FUN_14008d020`, both independently verified via `PatternScan.java` to resolve to exactly one match each) instead of guessing at `dcHandle`'s field layout, falling back to the old (known-wrong) raw x/y if either signature fails to resolve. Build-verified (x64 0 errors via a scratch `OutDir`, `dumpbin`-confirmed `8664 machine (x64)` fresh timestamp; Win32 regression 0 errors — this file is fully `ExcludedFromBuild` on Win32) but **could not be deployed/live-tested this session** (the real game directory's `d3d9.dll` was locked by a concurrent live-test process at the time). A one-shot `[x64-drawtext-pos]` diagnostic log line was added to confirm on the next live test whether the real transform is actually running and what position it produces. **UPDATE 2026-09-13 (second pass, same day) — the `ComputeRealDrawPositionX64` fix above was live-tested and confirmed correct for gameplay hints, but a live report then found the SAME pre-transform-x/y bug still live in the separate menu corner-hint block (Back/Friends/Quit/Leaderboards/Game-Summary) — that block's four `ConvertRealScreenPosToDesignSpaceX64` call sites (including the `looksLikeCornerHintRowX64` gating check, which needs a REAL y to be comparable against `kStandardCornerHintYX64`(995)) were never routed through `ComputeRealDrawPositionX64` when the gameplay-hint call sites were fixed.** Fixed by applying the identical pattern to all four sites. Build-verified (x64 `/t:Rebuild` 0 errors, `dumpbin`-confirmed `8664 machine (x64)` fresh timestamp, Win32 regression rebuild 0 errors, x64 redeployed last); **not yet live-tested**. **2026-09-14 live playtest**: the nine substituted categories confirmed working; separately, Survival ready-up's own absence (buy-station/ready-up's `Font_s.fontName` gap) is now confirmed VISIBLE in real play — native prompt text shows unmodified where a controller-glyph icon should replace it, matching this row's own documented, already-known-blocked reason exactly (not a new/different symptom). Full trail: `re_notes/x64_migration/drawtext_hook_x64.md` (see its "Stage (c)"/"Stage (d)" sections) and `known_issues_x64.md`'s matching 2026-09-13 updates |
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
| 45 | `MotionBlurEnabled`/`Strength`/`CenterFalloff` | Confirmed working live | **LIVE-CONFIRMED 2026-09-14** ("it works now") — real x64 TRIGGER found and wired 2026-09-13, closing the gap the correction below identified | The 2026-09-12 fix (below, kept for record) genuinely wired the three safety gates and the yaw/pitch delta feed — real work, not fabricated — but `RunPreOverlayMotionBlurPassIfEnabled`'s only real-world caller, `TriggerMotionBlurFromEngineHook()`, is invoked exclusively from `Hook_693ff0` (`analog_input_hooks.cpp`) — a raw `__declspec(naked)`/inline-`__asm` hook on `FUN_00693ff0`, wrapped in `#if !defined(_M_X64) && !defined(_WIN64)` and NEVER compiled for x64 at all (x64 doesn't support this register-convention trick the same way MASM-free inline asm does on x86). Confirmed via direct grep: zero callers of `TriggerMotionBlurFromEngineHook`/`RunPreOverlayMotionBlurPassIfEnabled` exist anywhere in `analog_input_hooks_x64.cpp`. The gate is armed but nothing ever pulls the trigger — same "mechanism exists, never wired into the tick" bug class as vibration/gyro-aim, here caused by non-portable inline asm rather than a missed call site. Contrast: FSR (`RunFullScreenPostProcessIfEnabled`, row #44) DOES fire on x64 because it hooks a different, already-ported point (`Hook_EndScene`, confirmed live via `[overlay-hud] EndScene hook fired`) — motion blur deliberately uses an earlier hook point instead, for real reasons documented in its own x86 crash history (issue #96/#97: avoiding a partially-composited backbuffer, avoiding blurring the native HUD) that haven't been re-verified as still applicable to x64's engine internals. Real next step: either find a C-callable x64 equivalent of `FUN_00693ff0`'s hook point, or determine whether `Hook_EndScene` is actually safe for this specific effect on x64 despite x86's own reasons for avoiding it there. Not yet fixed. Original 2026-09-12 finding, left in place per this file's own convention: "Same root cause and same-day fix as #44 (shares all three safety gates). Per-frame yaw/pitch deltas sourced from `Hook_MovementTick`'s own already-computed look-injection data, not a new RE target." **UPDATE 2026-09-13 (dedicated follow-up task) — real x64 hook point found via fresh Ghidra RE, option 1 of the two above, NOT option 2 (EndScene was never used).** A new whole-binary self-recursion scan (`FindSelfRecursiveFuncs.java`, 13295 functions scanned) located `FUN_140194130`, a byte-for-byte structural match for x86's `FUN_00508970` (same base case, same four-way rect-split recursion, same NUMERIC `+0x160`/`+0x164`/`+0x168`/`+0x16c` viewport-rect field offsets, unshifted despite the x86→x64 pointer-width growth around them). Its only external caller, `FUN_14018e720`, is the x64 equivalent of `FUN_00694650` — same triple-loop shape, and in all three call sites (mirroring x86's own three `FUN_00693ff0` call sites exactly) calls **`FUN_14018def0(ctx)`** immediately after each `FUN_1401939f0`(the x64 `FUN_00497210` equivalent)/`FUN_140194130` call returns. `FUN_14018def0` itself decompiles to the same gate-then-dispatch shape as x86's `FUN_00693ff0` (`if (*(ctx+0x330) != 0) { ...; FUN_140189940(...); }`, and `FUN_140189940` is confirmed to be the exact opcode-stream dispatch-loop shape x86's `FUN_004ee300` has). Unlike x86, this is a plain, C-callable function — raw disassembly confirms standard MS x64 fastcall (`RCX` = the viewport context, no register-implicit tricks) — so a normal (non-naked) MinHook C++ detour was used instead of x86's naked-asm template, confirming this project's own repeated finding that x64 functions use standard calling conventions even where x86 needed raw `__asm`. Hooked via `Hook_MotionBlurTrigger` (`analog_input_hooks_x64.cpp`), resolved via a 17-byte literal signature (no wildcarding needed; `PatternScan.java`-confirmed exactly 1 match in the whole binary) per the locked signature-scanning policy. Build-verified: x64 `/t:Rebuild` 0 errors, `dumpbin`-confirmed `8664 machine (x64)` fresh timestamp (`6AA71937`, Sun Sep 13 22:44:23 2026), Win32 regression rebuild 0 errors (file fully excluded from that platform), x64 rebuilt and redeployed last. **Not yet live-tested** — build/decompile-verified only. Full trail: `re_notes/known_issues_x64.md` issue #1's "where is motion blur?" round, `re_notes/ghidra_scripts/{selfrecursive_x64.txt,decomp_selfrec_candidates_x64.txt,callers_140194130_x64.txt,decomp_14018def0_x64.txt,decomp_140189940_1401939f0_x64.txt,sigbytes_14018def0_x64.txt,patternscan_14018def0_x64.txt}`. |
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

### Additional findings — git-history audit-completeness sweep (2026-09-13)

Not part of the original 2026-09-12 snapshot (see this document's own note
above the Full table header) — found via a direct read of x86-era git
history rather than existing documentation, then independently re-verified
against current x64 source. Same Legend as above.

| # | Feature | x86 status (v0.3.5) | x64 status | Evidence |
|---|---|---|---|---|
| 62 | Display-mode-change device recreation / WndProc re-subclassing (x86 commits `b13d897103`, `28cef08ada`, 2026-08-01) | Confirmed fixed — a display-mode change recreates the whole D3D9 device AND window (new HWND), which broke cached-texture cleanup and silently killed WM_MOUSEMOVE-based cursor tracking on the new window | **PRESENT** | `InstallWndProcHook` (`d3d9_hook.cpp:627-652`) re-subclasses whenever `hwnd != g_gameHwnd`, restoring the old window's original WndProc first — carries zero `_M_IX86`/`_M_X64`/`_WIN64` guards anywhere in its body. `OnDeviceRecreated` (`overlay_hud.cpp:6943-6946`, calls `ReleaseAllCachedTextures()`) is equally unguarded. Both are called unconditionally from `Hook_CreateDevice` (`d3d9_hook.cpp` ~line 754-770: `InstallWndProcHook(hFocusWindow)` every call, `OnDeviceRecreated()` on every call after the first) — the single, arch-neutral `CreateDevice` hook already confirmed shared by both platforms (row #57, "the one standing exception, resolving live from the actual COM vtable"). Full chain traced end to end with no guard found at any link; inherited "for free," not independently ported |
| 63 | Non-16:9/aspect-ratio-safe overlay rendering — size AND position (x86 commits `0de7a5ce`, `112c06652`, `db30d7d6`, `dbcc1cc9`, `a2713481`, `bfa22eeb`, `0c03f8f`, 7 rounds, 2026-08-08) | Confirmed fixed after 7 live-tested rounds: non-uniform `scaleX`/`scaleY` stretched glyph icon SIZE (fixed via `min(scaleX,scaleY)`), and separately broke corner-hint-row detection (`looksLikeCornerHintRow`, a raw-Y positional check against a fixed design-space constant) at non-16:9 resolutions | **PRESENT** (both pieces) | **Size**: `GetUniformSizeScale` (`overlay_hud.cpp:6936-6941`, `scaleX < scaleY ? scaleX : scaleY`) and the identical inline expression in `DrawOneGameplayHintSlot` (`overlay_hud.cpp:1893`, `float uniformScale = scaleX < scaleY ? scaleX : scaleY;`) — no arch guard, this is the single shared consumer every x64 glyph-icon draw (`RequestCustomHintOverlay` → `GameplayHintSlot` → `DrawOneGameplayHintSlot`) already routes through. **Position / corner-hint-row**: `ConvertRealScreenPosToDesignSpaceX64` (`analog_input_hooks_x64.cpp:3406-3411`, x64-local reimplementation of x86's `ConvertRealScreenPosToDesignSpaceX64` — internal-linkage, ported separately per this project's own anonymous-namespace lesson) and `looksLikeCornerHintRowX64` (`analog_input_hooks_x64.cpp:3737-3740`, using the identical `kStandardCornerHintYX64=995.0f`/`kCornerHintRowTolerancePxX64=40.0f` constants x86 uses) are both confirmed live in `Hook_DrawTextX64`, already covered in row #34's own detailed text but not previously broken out as its own line item. **Caveat inherited from row #34, not new**: the empirical PIXEL NUDGE constants (`kHintVerticalNudge`, `kMantleHintXNudge`/`YNudge` from the later 2026-08-08 rounds) were explicitly NOT ported — only the uniform-scale/position-conversion math itself, per `Hook_DrawTextX64`'s own in-code "HONEST CAVEAT" comment |
| 64 | Custom Options screen's real depth — all 7 vanilla tabs, real keybind rebind capture, Custom Binds tab, Apply Settings popup, mouse clicks, multiple entry points, controller-photo diagrams (x86 commits `6c6bed632`, `acade9f97`, `d24577de3`, `c15703a12`, `924b9ee33`, `df32021a6`, `fb184acc3`, 2026-08-04 through 2026-08-06) | **CORRECTION (2026-09-14, direct user statement): NEVER actually finished/matured on x86 either — "custom options screen was NEVER finished even in 0.3.5 it basically was unchanged from 0.3.0."** Verified against `legacy-x86-docs/PATCHNOTES.md`: shipped v0.3.1 (2026-08-06) explicitly as "PREVIEW/WIP... off by default... hasn't been played yet" (`[Options] UseCustomOptionsScreen` default OFF), and stayed that way — zero mention anywhere in PATCHNOTES from v0.3.2 through the final v0.3.5 of it graduating past preview status, being turned on by default, or being confirmed fully live-tested. The x86 DATA LAYER itself was real working code (unlike x64's deliberately-stubbed-dead one), but the FEATURE as a whole never matured into a finished, validated, on-by-default piece of x86's own final shipped state — meaning x64's current gap here is not "behind a mature x86 baseline," it's "neither platform ever finished this," a materially lower-priority parity item than the table's original framing implied. **DEFERRED (2026-09-14, direct decision): explicitly NOT a priority for this release** — `mw3ncp_config.ini` is a reliable, already-working settings path for everything this mod itself controls; the Options screen's remaining value is only for real vanilla game settings, genuinely lower-value. Not planned again until a later pass, well past the current parity push. Original framing kept below for the record. | Confirmed working — direct instruction "EVERY SINGLE OPTION FROM NATIVE AND OUR MOD," expanded from 3 tabs (Controller/Look/Voice) to all 9 (Controller + Look/Video/Audio/Voice/AdvancedVideo/Movement/Actions + Custom Binds), real rebind capture, real Apply-Settings-popup restart flow (read straight from `zone_dump/ui/all_restart_popmenu.menu`), mouse click hit-testing, 3 real entry points (pause + 2 main-menu), real controller-photo Stick/Button Layout diagrams | **PARTIAL — UI shell PRESENT and reachable; the actual DATA LAYER for all 7 vanilla tabs is ABSENT (silent no-op reads+writes)** | `DrawCustomOptionsMenuIfOpen` (`overlay_hud.cpp:5356-5778`) and `CustomOptionsMenu_TickInput` (`overlay_hud.cpp:5895-6086`) — the full 9-tab `UnifiedTab` enum (`overlay_hud.cpp:4153`, `Controller, Look, Video, Audio, Voice, AdvancedVideo, Movement, Actions, Binds`), tab-bar drawing, drill-down diagrams, mouse hit-testing, and the Apply Settings popup all confirmed present in this SAME shared function with zero `_M_IX86`/`_M_X64`/`_WIN64` guards anywhere in either function's body — this is genuinely the identical compiled code on both platforms, matching row #40's own "zero hardcoded addresses" finding, and the real open trigger is already wired (row #41, FIXED). **However**, every one of the 7 vanilla tabs' rows is a `VanillaSettingKind::{DvarBool,DvarFloat,DvarString,Keybind}` (`vanilla_settings_table.h:112+`) read/written exclusively through `GetVanillaSettingValueString`/`SetVanillaSettingFromString` (`vanilla_settings_sync.cpp:9-67`, itself carrying zero arch guards — it just calls straight through to `real_settings.h`'s functions). `real_settings.cpp` is the actual data layer, and on x64 it is provably dead for every one of these: `FindDvar()` (`real_settings.cpp:69-84`) is `#ifdef _M_IX86`-only `__asm` — on x64 it always returns `nullptr`, so `GetDvarBool`/`GetDvarFloat`/`GetDvarString` (lines 87-106, no arch guard of their own, they just get a null pointer) always return `0`/`0.0f`/`nullptr`. Every setter — `SetDvarBool`/`SetDvarFloat`/`SetDvarString` (lines 117-134) and `SetKeybind`/`UnbindKeynum`/`KeyNameToKeynum`/`KeynumToDisplayName`/`QueueConsoleCommand` (lines 167-222) — has an EXPLICIT `#if defined(_M_X64) \|\| defined(_WIN64)` early-return no-op, added 2026-09-04 specifically to prevent a crash from the raw x86-only function-pointer calls (`SetDvarBoolRaw`/etc., hardcoded x86 addresses like `0x0044d700`) — confirmed deliberate, not an oversight, per that block's own in-code comment. `GetKeybind` (lines 144-165) is the same `#ifdef _M_IX86`-only-`__asm` shape as `FindDvar` — `count` stays 0 and the caller's local `{-1,-1}` init is never overwritten, so every real keybind displays as unbound regardless of its true value. Net effect: a controller/mouse/keyboard user can open the screen, see and navigate all 9 tabs, and "select"/"adjust" any row — but every Look/Video/Audio/Voice/AdvancedVideo/Movement/Actions setting always displays a stub value and every edit is silently discarded; nothing about the UI itself indicates this. The two tabs that DO work are both backed by this mod's OWN config, not real dvars: Controller (reads/writes `g_modConfig` fields directly) and the new Custom Binds tab (writes `g_modConfig.customButtonMap`, `overlay_hud.cpp:4304-4305`, confirmed no dependency on `real_settings.h` at all). **Not fixed this pass** — closing this needs real x64 RE work (x64 equivalents of the custom-register-convention `FindDvar`/`SetDvarBool`/`SetDvarFloat`/`SetDvarString`/`GetKeybind`/`SetKeybind`/`KeyNameToKeynum`/`KeynumToDisplayName` internals), left as a precisely-documented open item per this sweep's own task instructions rather than attempted here. `GetDvarFloatX64`/`GetDvarStringX64` (`analog_input_hooks_x64.cpp:2233`/`2250`, resolved 2026-09-13 for row #3's ADS-slowdown fix) are real x64 reads that exist elsewhere in the codebase but are NOT wired into `real_settings.cpp`/`vanilla_settings_sync.cpp` at all — a genuinely separate call path the Options screen's data layer never reaches |
| 65 | Custom/system UI font support — `FontFamily`/`FontFamilyCondensed`/`FontRole` (x86 commits `4f347106d`, `d7c9afff4`, `cf4d3edd3`, `95e63a731`, 2026-08-24) | Confirmed working — Isotherm Sans (UI + Condensed variants) bundled and embedded via `AddFontMemResourceEx`, `[Overlay] FontFamily`/`FontFamilyCondensed` INI keys allow any system-installed font by name, `FontRole` threaded through every text-measure/render call site | **PRESENT** | `LoadOverlayFonts(hModule)` (`dllmain.cpp:472`) is called unconditionally from `DllMain`'s `DLL_PROCESS_ATTACH`, BEFORE the `#if defined(_M_X64)` branch that installs the platform-specific hooks — not itself inside any arch guard. `ResolveFontFamily` (`overlay_hud.cpp:634-640`, reads `g_modConfig.overlayFontFamily`/`overlayFontFamilyCondensed`), the two `CreateFontA` call sites that use it (`overlay_hud.cpp:703`, `777`), and `LoadOverlayFonts` itself (`overlay_hud.cpp:7061+`, `AddFontMemResourceEx` at line 7044) carry zero `_M_IX86`/`_M_X64`/`_WIN64` guards — pure Win32 GDI calls, inherently arch-neutral, feeding the already-confirmed-firing x64 text-draw pipeline (`Hook_DrawTextX64`, row #34) |
| 66 | Glyph icon mip-chain/LINEAR-filtering fix + texture prewarm-at-launch (x86 commits `a7dd1995`, `babfe4696`, `a17684a40`, 2026-08-24) | Confirmed fixed — icons created with `Levels=1`/no mip chain aliased when scaled down; fixed via `Levels=0`+`D3DUSAGE_AUTOGENMIPMAP` + LINEAR sampler filtering around the draw call, plus prewarming every icon/blur-shader/white/debug texture at device-creation time instead of lazily mid-gameplay (stutter fix) | **PRESENT** | `LoadGlyphIconTexture`'s `CreateTexture` call (`overlay_hud.cpp:1400`, `kD3DUSAGE_AUTOGENMIPMAP`) and `DrawGenericTexturedQuad`'s LINEAR-filter save/set/restore around its draw (`overlay_hud.cpp:1580-1635`, `kD3DSAMP_MIPFILTER`) carry zero arch guards. `PrewarmGlyphIconTextures` (`overlay_hud.cpp:1503`) plus `EnsureWhiteTexture`/`EnsureBlurTexture`/`EnsureBlurShader`/`EnsureDebugMarkerTexture` are all called from `InstallEndSceneHook` (`overlay_hud.cpp:6949-6969`) — itself called unconditionally from `Hook_CreateDevice`'s shared device-creation path (same call site row #62 above traces) — with no arch guard anywhere in that call chain. `GetOrLoadGlyphIconTexture` (the consumer of all of this) is already the confirmed live texture path for every one of row #34's nine working x64 hint cases |
| 67 | `GlyphStyleAuto` — VID/PID-based controller-type auto-detection (Xbox360/XboxModern/DualSense) (x86 commit `3396ec22b1`, 2026-08-25) | Confirmed shipped (not independently live-confirmed even on x86 — flagged as such in `known_issues.md` #81/#82/#83 at the time) — detects DualSense via native HID or a Microsoft-vendored pad via a best-effort VID/PID table, once per session at the point the input source locks, sets `GlyphStyle` accordingly | **PRESENT** | `TryDetectXboxGlyphStyle`/`kXboxPidTable` (`dualsense_input.cpp`, real `SetupDiGetClassDevs`/`HidD_GetAttributes` HID enumeration against Microsoft's real VID `0x045E` and a 13-entry real Xbox 360/One/Series PID table) and `Controller_DetectGlyphStyle` (checks `DualSense_IsOpen()` first, falls back to the Xbox scan, falls back to the player's own config) carry zero arch guards — consistent with row #49's existing finding that this whole file has none. Wired into `XInputPollThreadProc`'s session-lock point (`controller_input.cpp`, both the XInput-lock and DualSense-lock branches call `Controller_DetectGlyphStyle` when `g_modConfig.glyphStyleAuto` is set) — the same poll thread row #58 already confirms starts and runs correctly on x64 |
| 68 | High-render-scale safety warning — 4GB address-space warning at high `InternalRenderScalePercent`/`CustomResolutionWidth`/`Height` (x86 commit `1e107cf62b`, 2026-08-29) | Confirmed shipped — one-time `ShowOverlayMessageUntilDismissed` on-screen warning + `[video-scale][WARNING]` log line, firing the first time a session's target crosses ~150% linear (~2.25x native pixel area), citing this process's real 4GB 32-bit address-space ceiling and real crashes/freezes reproduced at 250-300% (issue #105) — does not block, just warns | **WAS ABSENT — FIXED in this pass (2026-09-13)** | x86's warning lived entirely inside `Hook_FUN_00679010` (`analog_input_hooks.cpp`), a function that whole file is guarded out of the x64 build — so it was never carried over when `InternalRenderScalePercent`'s own BASE mechanism was separately ported to x64 as `Hook_RenderResCompute` (`analog_input_hooks_x64.cpp`, row #43, FIXED 2026-09-12). Confirmed genuinely absent by direct inspection of `Hook_RenderResCompute` before this fix (zero references to `video-scale][WARNING`, `ShowOverlayMessageUntilDismissed`, or any area/threshold check anywhere in `analog_input_hooks_x64.cpp`). **Ported this pass**: same `>2.25x`-area (`targetArea*4 > nativeArea*9`, i.e. `>150%` linear) threshold, same one-time-per-session gate (`static bool`), same `ShowOverlayMessageUntilDismissed` mechanism, added directly into `Hook_RenderResCompute` (`analog_input_hooks_x64.cpp`, right after its existing `[x64-video-scale]` log line) — using `nativeW`/`nativeH`/`targetW`/`targetH` the function already computes, no new RE needed. Wording deliberately NOT copied verbatim: x86's warning text cites a hard 4GB ceiling specific to a 32-bit process, which does not apply as-is to `iw5sp_x64.exe`/`iw5mp_x64.exe` (genuine 64-bit processes) — the ported warning says so explicitly and frames itself as a precaution (the underlying high-scale crash/freeze risk was never independently re-tested on x64), not a claim the identical failure mode reproduces here. Build-verified both platforms this pass: x64 `/t:Rebuild` 0 errors (10 pre-existing, unrelated `C4312` warnings only), `dumpbin /headers`-confirmed `8664 machine (x64)` with a fresh timestamp, Win32 regression rebuild 0 errors/0 warnings, x64 rebuilt and redeployed last. **Not yet live-tested** |

### Config-key consumer audit (2026-09-14)

Not part of either prior pass (2026-09-12 original snapshot or the
2026-09-13 git-history sweep) — a dedicated, systematic pass over every one
of `mw3ncp_config.ini`'s 120 keys across all 15 sections, checking not just
that each key parses (confirmed 100% arch-neutral: `mod_config.cpp`/`.h`
carry zero `_M_IX86`/`_M_X64`/`_WIN64` guards anywhere) but that the code
which actually READS `g_modConfig.<field>` is reachable on x64, tracing one
level up to the enclosing hook/install function where needed. Full trail:
`known_issues_x64.md`'s 2026-09-14 round. Of the ~35 keys not already
covered by an existing row/prior-confirmed list, 34 came back clean
(arch-neutral or explicitly x64-branched); one genuine new gap found.

| # | Feature | x86 status | x64 status | Evidence |
|---|---|---|---|---|
| 69 | `[Experimental] BindResolverGlyphSubstitution` (in-font glyph-codepoint substitution inside resolved hint text) | Already self-documented as SUPERSEDED on x86 itself as of 2026-07-31 (project pivoted to the overlay-quad approach, row #34) — key ships off by default, not part of x86's own live feature set either | **ABSENT** | Sole consumer, `BindResolverLogAfterCall()` (`analog_input_hooks.cpp`), is only called from `Hook_0061f6f0`'s naked-asm trampoline, installed via a hardcoded x86 address (`MH_CreateHook(0x0061f6f0, ...)`) inside `InstallAnalogInputHooks()` — the SAME function wrapped in `#if !defined(_M_X64) && !defined(_WIN64)` that also gated the vibration/motion-blur-trigger gaps (rows #20/#45) before those were fixed. A dedicated x64-only stub of the same function name exists later in the file purely to log that the real x86 body "is not called on this build" — confirmed deliberate architectural split, this one key just never got individually re-ported. Zero references to `bindResolver`/`0061f6f0` anywhere in `analog_input_hooks_x64.cpp` (direct grep). **Practical severity LOW**: the mechanism this key controls was already dead weight on x86 itself (superseded by row #34's overlay-quad approach before the x64 recompile even happened), ships off by default, and isn't referenced in the live `mw3ncp_config.ini` — a real "key parses, zero x64 consumer" finding by the letter of the audit, not a regression any player has experienced on either platform. Not fixed this pass (audit-only, no source changes) |

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
  - ~~Whether the sniper Fire/ADS notify fix (#6) also incidentally fixes
    Predator Missile launch (#16) — plausible from the shared mechanism, not
    tested against that specific killstreak.~~ **Superseded 2026-09-13** — a
    dedicated static-RE pass closed this from "plausible" to "structurally
    confirmed the same string/argument a real keyboard press sends," via two
    independent, converging methods (dispatch-case address matching + a
    direct bind-name-table memory dump). See row #16's own updated evidence
    column. What remains unclosed either way is the same as everywhere else
    in this document: an actual live playtest.
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
