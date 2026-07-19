# Patch Notes

All notable changes to the mod, per release. See `re_notes/known_issues.md` for the
full, actively-tracked issue list and `re_notes/iw5sp.md` for the underlying
reverse-engineering trail behind each entry.

---

## Unreleased

Nothing yet — see `re_notes/known_issues.md` for what's actively being worked on.

---

## v0.2.0 — Alpha (2026-07-19)

**This is the project's first release tagged as a real milestone rather than
incremental groundwork**, and its first non-pre-release tag on GitHub. Headline
changes: **Sprint migrated onto its real native kbutton**, which turned out to
also make its entire custom stamina/cooldown timer layer redundant (removed
entirely) and resolved the previously-open Extreme Conditioning perk override
for free — the engine's own native systems now handle both automatically. **3 of
Survival's 4 real killstreaks are now confirmed working** (Predator Missile
launch, Precision Airstrike, AI squadmate call-in), found via a
from-bytecode-to-native-delivery reverse-engineering pass through
`notifyonplayercommand`'s real GSC command-queue chain. Real D-pad/A menu
navigation now covers the main menu and title screen, not just in-game menus.
Also included: a first vibration/rumble implementation that turned out to crash
the game at startup (root-caused and disabled, not shipped broken), and a fully
proven, implementation-ready button-glyph asset/build pipeline (not yet wired
into rendering). See **Status at a glance** in `README.md` for the full,
explicit fully-working/partial/not-implemented breakdown this release
introduces. Full detail on every change below.

### Added
- **Predator Missile guidance-phase input: real per-frame reader chain
  found, diagnostic deployed (2026-07-19, task #30 follow-up).** User
  asked to re-investigate via the killstreak's own GSC rather than
  further native-side flag-guessing. Full re-read of `1555.gsc`'s
  guidance-phase loop confirms there is NO per-frame input read at the
  GSC level at all — it's a plain abort-condition poll; steering is 100%
  native. A whole-binary scan for the literal scalar `0x80000` (the bit
  `controlslinkto`'s native implementation sets on `clientStruct+0xc`)
  found the real per-frame dispatcher, `FUN_004554d0` — confirmed via its
  own caller (`FUN_00644ed0`, the Pmove-tick function) — which, when
  linked, tail-jumps into `FUN_006423d0`, reading 3 floats from
  `pml+0xc`/`+0x10`/`+0x14` (Pmove-locals, NOT the real `usercmd_t` this
  mod's look hook writes to) and angle-wrapping them into
  `clientStruct+0x10c`/`+0x110`/`+0x114`. **This REFUTES the earlier
  `cmd+0x3e`/`0x3f` theory (issue #30) as the mechanism for this specific
  bug** — that theory's `+0x1094` bit is a different address from the
  `clientStruct+0xc` bit `controlslinkto` actually sets. Whether
  `pml+0xc/+0x10/+0x14` already receives this mod's look input via some
  earlier copy step, or needs a direct write, wasn't resolved statically
  in the time available — a new log-and-forward diagnostic
  (`Hook_MissileGuidanceDispatch`, gated on the link bit so it's silent
  during normal play) logs both sides side by side, so the next real
  missile flight will show which hypothesis is correct. Builds clean (0
  warnings/0 errors, full rebuild). Not yet live-tested. See
  `re_notes/known_issues.md` issue #30's 2026-07-19 correction for the
  full trail.
- **Controller vibration/rumble (2026-07-18, task #17).** No native
  rumble infrastructure exists in this build at all — entirely this
  mod's own `XInputSetState` output, driven off two real, disassembly-
  confirmed native notify choke points: a short pulse on every real
  shot fired, and a damage-scaled pulse when the local player takes
  damage (filtered via the same "has a client struct" entity field the
  real `notifyonplayercommand` registration already gates on). New
  `[Vibration]` config section (`Enabled`, fire/damage intensity and
  duration). Known limitation: the local-player filter doesn't yet
  exclude a co-op partner's entity in 2-player Survival. Explosions, melee
  hits, killstreak activation, and low-ammo rumble are real leads (a
  ~600-entry GSC notify-event table already found includes
  `"explode"`/`"grenade_fire"`/`"missile_fire"`) but not yet wired up.
  **Correction, same day: live-tested and found to crash the game at
  startup.** Both native hook targets (`FUN_004895b0`/`FUN_0044cdb0`)
  turned out to be generic dispatchers with genuinely variable real
  argument counts across their call sites (confirmed via a disassembly-
  based push-count survey of every real caller) — the fixed-signature
  detour corrupted stack reads for unrelated boot-time events on most of
  them, crashing before any gameplay frame. **Disabled**: the
  `Rumble_Install()` call site is commented out (code kept, not deleted).
  A safer reimplementation is planned against a single-call-site-safe
  target (`FUN_0045e320` for fire) and health-polling (rather than a hook)
  for damage. See `re_notes/known_issues.md` issue #24. **Doc-audit
  finding, 2026-07-19 (flagged, not fixed — no code touched this pass):**
  `[Vibration] Enabled` still defaults to `true`, the exact same bug class
  already found and fixed once for `[AimAssist] Enabled` (v0.1.2).
  Currently harmless since `Rumble_Install()` is disabled with nothing to
  gate, but it's a landmine for the eventual reimplementation — flip this
  default to `false` in the same pass that re-enables real rumble hooks,
  not as an afterthought.
- **`[Experimental]` config section (2026-07-18)** — a new pattern for
  individually-toggleable, not-yet-fully-proven behaviors, so a live
  hypothesis under test can be flipped off via the INI without a
  recompile if it turns out to be wrong. First entry:
  `FireNotifyQueueKick` (see the Fire/killstreak entry below).
- **`[Experimental] SprintStaminaBypassForTesting` (2026-07-19, task #9) —
  ADDED THEN REMOVED THE SAME DAY.** Added specifically to isolate Sprint's
  real-`+sprint`-kbutton migration (see Changed below) for live testing by
  skipping this mod's own stamina/cooldown timer entirely. Live-testing
  confirmed the kbutton migration works AND that the underlying custom
  timer this toggle bypassed is now permanently redundant (see the Changed
  entry below) — with that timer gone entirely, there's nothing left for
  this toggle to bypass, so it was removed the same session rather than
  left around as dead config surface.

### Fixed
- **Sprint (L3) no longer force-stands the player while ADS'd (2026-07-18,
  task #24).** `InjectControllerSprint`'s auto-stand-from-crouch/prone call
  was firing unconditionally on any Sprint rising edge, including while
  aiming down sights with a sniper — breaking the player's crouch/prone
  cover the instant they tried to use Hold Breath (which shares the same
  physical bind as Sprint on console, `+breath_sprint`). Now gated on
  `!g_adsHeld`. Builds clean, not yet live-tested. **Known simplification**:
  gates on "ADS'd with any weapon," not specifically "ADS'd with a
  sniper-class weapon" (no clean native weapon-class query was available),
  so Sprint's rising edge is now a stance no-op for every ADS'd weapon, not
  just snipers — real console ADS+Sprint interaction on non-sniper weapons
  wasn't independently verified and should be checked during live testing.
  Hold Breath's actual sway-reduction feature remains unimplemented (see
  Investigated section below).

### Changed
- **Fire (RT) rewired off the raw usercmd bit onto the real `+attack`
  kbutton (2026-07-18, task #7).** Killstreak work started with Predator
  Missile: the GSC trace done earlier this session found its launch is
  gated behind `notifyonplayercommand("launch_remote_missile", "+attack")`,
  which fires on real bind/command dispatch, not on a raw usercmd bit being
  forced — the standing hypothesis for why the missile's camera/view
  worked but launch didn't reliably. `+attack`'s real kbutton_t address
  (`0x00A98C00`) was already sitting in an existing bit-correlation table
  from 2026-07-14, so this reused the same `CallKbuttonDown`/
  `CallKbuttonUp` mechanism already proven live for ADS/Reload — a full
  replace, not additive, same precedent as the crouch/prone migration off
  raw bit-forcing. Builds clean (0 warnings/0 errors). **Live-tested same
  day: half confirmed, half refuted.** Regular gunfire — CONFIRMED no
  regression, shooting still works normally. Predator Missile launch —
  CONFIRMED still broken, unchanged. The kbutton-level fix stays (it's
  real and correct, gunfire depends on it) but the hypothesis that a
  kbutton_t `KeyDown` call alone would reach `notifyonplayercommand`'s
  native trigger is disproven — that trigger point is still unfound. See
  `known_issues.md` issue #29.
- **Sprint (L3) migrated off raw `pm_flags` bit-forcing onto the real
  `+sprint` kbutton (2026-07-19, task #9).** Three prior live-memdiff
  searches for Sprint's real kbutton (twice via whole-heap correlation,
  once via live write-testing, once via a targeted static-range scan) had
  all come back negative and this was believed a genuine dead end (see
  `re_notes/iw5sp.md`, "Sprint's real kbutton — PARKED"). Found instead via
  a completely different, purely static technique needing no live game
  process: reconstructed `FUN_00438710`'s real 77-entry jump table by raw
  dword walk (the decompiler's own switch recovery only partially resolved
  it) and cross-referenced it against the real static 81-entry canonical
  bind-name table `FUN_005330a0` scans — confirmed the table's index IS
  `FUN_00438710`'s case number, four independent ways (`+attack`=1,
  `weapnext`=66, `togglecrouch`=72, ADS's `+toggleads_throw`=59-60,
  matching its already-confirmed `0xA98CB8` kbutton exactly). Case 61-62 =
  `"+sprint"`/`"-sprint"`, driving a dedicated kbutton at `0xA98CCC` —
  independently cross-confirmed because the real default SHIFT bind
  (`"+breath_sprint"`, case 9-10) disassembles to two kbutton calls, one on
  a newly-found `0xA98C04` (very likely Hold Breath's own kbutton, a live
  lead for task #24) and a second on this exact same `0xA98CCC`. Sprint now
  drives this kbutton via `CallKbuttonDown`/`CallKbuttonUp` (same mechanism
  as ADS/Reload/Fire), gated on `IsSprintActive()`. The old pm_flags-forcing
  mechanism (`InjectControllerSprintPmFlags`/`ReassertSprintPmFlags`, hooks
  on `FUN_00644ed0`/`FUN_00643ce0`) was removed entirely, not just disabled —
  full replace, same precedent as Fire's migration above. Builds clean (0
  warnings/0 errors, full rebuild). **LIVE-CONFIRMED WORKING, same day
  (2026-07-19).** User report: "this fixes multiple issues, having native
  sprint means no workaround needed for stamina and regen as its embedded
  naturally by the engine[,] same for extreme conditioning[,] fixed by this
  100%." Driving the real kbutton means the engine's own native sprint
  duration/recovery timer now applies automatically, INCLUDING Extreme
  Conditioning's real duration override — with zero separate detection code
  needed. **As a direct consequence, this mod's entire custom stamina/
  cooldown timer layer (maintained since 2026-07-15 specifically to work
  around the previous pm_flags-forcing approach bypassing the real timer)
  is now dead weight and has been removed in the same pass**: `g_sprintStamina`/
  `g_sprintWinded`/`g_sprintCooldownRemaining`/`g_sprintLastTickMs`, the
  `player_sprintUnlimited`-dvar bypass (redundant — the real kbutton already
  respects that dvar natively, same as real keyboard sprint does), the
  `[Sprint]` config section (`MaxStaminaSeconds`/`RegenSeconds`), the
  just-added `[Experimental] SprintStaminaBypassForTesting` toggle (see
  Added above), and the `GetRealSprintValue`/`LogSprintDiag` diagnostic code
  that had been investigating whether a real native timer existed at all
  (see `known_issues.md` issue #6, 2026-07-16) — all gone. Also resolves
  task #9/#24's previously-open "Extreme Conditioning perk override" item:
  no override code was ever needed, since the real kbutton makes it a
  native, automatic consequence rather than something this mod has to
  detect and apply itself. See `known_issues.md` issue #6's 2026-07-19
  update for the full disassembly trail and this removal.

### Investigated, not resolved
- **Predator Missile post-fire missile-guidance sequence: movement breaks
  on controller (2026-07-18, live-reported).** During the phase where the
  player controls the flying missile in flight (shares the real
  UAV-control system), controller movement input breaks. Not yet fixed.
  See the major research finding immediately below — this turned out to
  have a much more concrete, unifying explanation than the original
  "scripted-freeze" framing.
- **Major research finding: a third, previously-unknown analog-input
  channel (`cmd+0x3e`/`0x3f`) likely explains FOUR separately-tracked bugs
  at once (2026-07-18, task #25 deep dive, `known_issues.md` issue #30).**
  Decompiling the engine's real per-frame orchestrator revealed it has (at
  least) 3 control-mode branches — menu-active, a mounted/aim-only mode
  that routes real mouse-delta into a THIRD analog byte pair
  (`cmd+0x3e`/`0x3f`, distinct from normal movement and normal look), and
  vehicle steering — none of which this mod's controller hooks are aware
  of, since they only ever write the normal movement/look fields. The
  mounted/aim-only branch is a strong, evidence-backed unifying candidate
  for DPV aiming not working, the mounted-turret feeling too hard, AND
  today's Predator Missile guidance bug above — potentially one fix
  instead of three separate investigations. Not yet implemented; a new
  task (#30 in the live tracker) captures the concrete implementation
  plan. Mortar fire appears to be a genuinely separate mechanism (see
  below), not covered by this finding.
- **Turret damage/difficulty in "Back on the Grid" *(mission attribution
  corrected 2026-07-19 — this is actually Goalpost, see the Docs entry
  above)*: the health-regen hypothesis is REFUTED (2026-07-18, task #27, now
  closed).** Dumped the
  real mission zone and confirmed the mission DOES use a real
  faster-regen buff mechanic in two other scripted set-pieces — just never
  on the turret sequence. No turret-specific damage/regen logic exists in
  the mission's own scripts at all. The likely real explanation is the
  same missing-aim-channel issue described above (no aim assist +
  imprecise mounted aim), not a missing survivability mechanic.
- **Mortar fire ("Back on the Grid" *(mission attribution corrected
  2026-07-19 — this is actually Goalpost, see the Docs entry above)*) will
  very likely still be broken
  after the Fire rewrite above (2026-07-18, task #26) — do not assume it
  was fixed for free.** Confirmed the mortar (`bog_mortar`) is deliberately
  excluded from the engine's generic vehicle-fire pipeline, and — more
  importantly — the turret in the same mission already fired correctly
  under the OLD raw-usercmd-bit Fire, which is real evidence mortar and
  turret don't share a fire mechanism (otherwise both would have failed
  identically before today's change). The mortar's own fire-control
  script wasn't located this pass (hash-named, no distinguishing string).
- **Killstreak catalog correction (2026-07-18): the previously-assumed
  6-item killstreak list was wrong.** Re-extracting the real buy-station
  economy CSV directly shows Survival only ever sells 4 real killstreaks
  (`remote_missile`, `precision_airstrike`, `friendly_support_delta`,
  `friendly_support_riotshield`) — `stealth_airstrike`/`carepackage_c4`/
  `carepackage_ammo` don't exist as purchasable items at all (dead/
  vestigial precache-only content). Also resolved: `precision_airstrike`
  turns out to use a genuinely different, THIRD input mechanism (a native
  UI-style placement-marker API, not gated by `notifyonplayercommand` at
  all — may already work via this mod's existing D-pad+A menu navigation,
  worth a live test); and the standing hypothesis that AI squadmate
  call-ins (`friendly_support_delta`/`riotshield`) have a per-type code
  divergence bug is REFUTED — both run byte-for-byte identical spawn
  logic, differing only in a cosmetic HUD icon. See
  `re_notes/killstreak_reference.md`'s corrected roster table.
- **`notifyonplayercommand`'s native trigger point: reframed, not found
  (2026-07-18).** A full decompile of the entire input-dispatch chain
  found it is purely numeric with zero bind-name-string logic anywhere,
  and a raw byte-level scan confirmed the literal strings
  `"notifyonplayercommand"`/`"playercommand"` don't exist anywhere in the
  binary's static data. Conclusion: there is almost certainly no native
  "keypress pushes a notify" trigger to find — it's very likely a
  GSC-VM-internal builtin (bytecode polls bind state itself), the same
  architecture already confirmed for `hasperk` elsewhere in this project.
  **Polling-frequency ruled out same day**: user confirmed from prior
  play that holding Fire for a long duration still never launches the
  missile, closing off the "our held press doesn't last long enough for a
  slow poll" theory. The real kbutton_t this mod writes to is either never
  read by whatever GSC-VM intrinsic backs `notifyonplayercommand`, or some
  other precondition is unmet — next step is GSC bytecode/opcode-level
  analysis of `1555.gsc`'s compiled `.gscbin`, not further native
  dispatch-chain RE.
- **Second research wave, same day — five more forks, real progress on
  several fronts (`known_issues.md` issues #29/#30/#31):**
  - **Bytecode-level breakthrough on `notifyonplayercommand`.** Using
    `gsc-tool`'s own open-source engine tables, confirmed
    `notifyonplayercommand` compiles to a real, findable opcode (`0x8D`,
    `OP_CallBuiltinMethod2`) + method ID (`0x82A5`) — found 7 times in
    `1555.gsc`'s actual compiled bytecode, matching every known call site
    exactly. Also confirmed `notifyoncommand` (the bare/global variant
    `friendly_support_called` uses) is a SEPARATE builtin entirely
    (function ID `0x00D`), not the same mechanism with an optional
    receiver — a real architectural distinction missed until now. Native
    dispatch table (how the ID resolves to an actual function) not yet
    found — needs the GSC interpreter's opcode-dispatch loop located in
    Ghidra, a well-defined next step rather than an open question.
  - **Turret's "success" reframed — it was never evidence about notify
    gates.** Decompiled `FUN_0057a930` (previously unresolved) and found
    it's just a weapon-select fallback, not killstreak-specific. All
    three weapon-type killstreaks (sentry, `remote_missile`,
    `precision_airstrike`) ride a real native `weapon_change` event fired
    by an ordinary weapon switch — completely bypassing any notify-gate
    mechanism. Squadmate call-ins aren't registered in that same
    dispatcher at all, so they never get this free ride — explaining the
    working/broken split without it being evidence either way about
    synthetic input reaching notify gates.
  - **Squadmate call-in failure: a second, concretely-evidenced
    explanation found.** A full grep sweep of all 240 decompiled scripts'
    notify-call sites traced `friendly_support_called`'s real spawn logic
    to an explicit defensive early-return when a Survival map lacks
    `drop_path_start` structs — a genuine, silent, map-dependent no-op
    completely independent of input device. Stacks with (doesn't replace)
    the notify-reachability theory above as a candidate cause.
  - **`precision_airstrike` may already partially work today.** Its
    artillery-marker cursor movement was traced to `FUN_0057df60` — the
    same function a parallel pass had flagged as "vehicle steering," now
    understood to be a shared mode dispatcher (mode 1 = artillery cursor,
    mode 2 = actual vehicle driving). Mode 1's cursor math reuses the
    exact same raw mouse-delta source normal look already feeds — user
    confirmed the real in-game mechanic (aim + confirm, like a smoke
    marker throw) is consistent with this being a real-time cursor rather
    than a menu interaction, reinforcing that controller aiming may
    already work with zero new code. Only the confirm/Fire-detection step
    is still unlocated. Worth a live test before writing anything.
  - **Predator Missile's Campaign appearance confirmed and corrected.**
    "Down the Rabbit Hole" (`rescue_2.ff`) runs the LITERAL SAME compiled
    `1554.gscbin`/`1555.gscbin` scripts as Survival's version — a
    Survival-side fix for Fire's `notifyonplayercommand` reachability
    fixes both simultaneously, not two separate problems. Also corrected
    a stale, unverified claim that "Black Tuesday" also uses this
    killstreak — checked the two best zone candidates, found no supporting
    evidence, removed from `killstreak_reference.md`.

### Docs
- **Full documentation consistency pass across README.md/PATCHNOTES.md
  (2026-07-19), ahead of tagging this build toward v0.2.0.** Reconciled every
  doc against this session's actual findings rather than carrying forward
  stale claims:
  - **Mission mis-attribution corrected everywhere**: the mortar-fire and
    mounted-turret-difficulty bugs were filed under "Back on the Grid"/
    `dubai.ff` across multiple prior sessions and multiple docs
    (`README.md`'s compatibility table and killstreak table, `known_issues.md`
    issues #26/#27, live-tracker tasks #26/#27). A dedicated zone-
    identification pass found the real mission/zone is **Goalpost**/
    `hamburg.ff` (matching the mortar impact-FX table and the player-operable
    M1A1 turret actually present there) — "Back on the Grid" is untested and
    was wrongly given credit for both a pass ("fully compatible") and a fail
    (these two bugs) in different places. All four surfaces corrected, with
    an explicit "(corrected 2026-07-19)" note left in place rather than
    silently rewriting history.
  - **Killstreak status updated for real, live-confirmed progress**: README's
    Survival killstreak table and Campaign killstreak-type-weapon-system
    table both updated — Predator Missile launch is fixed (see the `"n 1"`
    fix below), Precision Airstrike is confirmed fully working (a
    smoke-grenade-throw mechanic, not a HUD/cursor system), and AI squadmate
    call-in stays confirmed. Predator Missile's post-fire guidance aim is
    now called out as a separate, still-open bug rather than folded into a
    generic "partial."
  - **Vibration/rumble's own "Added" entry (above) corrected in place**,
    same day it was written, once live testing showed it crashes the game at
    startup — see that entry for the root cause and the disable.
  - **Scorecard recomputed**: feature-completeness matrix moved 37/50→42/50
    given killstreaks (1→4 of 4), button-glyph prompts (2→3 of 4, full build
    pipeline now proven), and the options-menu implementation plan (1→2 of
    4); raw-functionality methodology's killstreak dataset and mission-
    compatibility dataset both recomputed against the corrected numbers.
  - **Second, deeper audit pass (2026-07-19): every commit since the last
    pre-session push (`418333f`) re-verified diff-by-diff against the
    docs, not just re-reading the prior summary.** Found and fixed several
    real gaps:
    - README's Configuration table listed every `[Look]`/`[Stance]`/
      `[Interact]`/`[Survival]`/`[Sprint]`/`[Bindings]`/`[AimAssist]` key
      but was missing the `[Vibration]` and `[Experimental]` sections
      entirely, even though both were added to the actual config this
      session — added all 7 missing rows.
    - `[Vibration] Enabled` still defaults to `true` in `mod_config.h`/the
      generated INI — the same bug CLASS already found and fixed once for
      `[AimAssist] Enabled` (v0.1.2). Currently harmless (nothing can gate
      on it while `Rumble_Install()` stays disabled) but flagged as a
      landmine for the eventual reimplementation. See the Vibration/rumble
      entry above and `known_issues.md` issue #24.
    - **`re_notes/killstreak_reference.md` was never updated with the
      `"n 1"` Predator Missile launch fix** — its Campaign and Survival
      tables both still read "currently broken"/"hypothesis REFUTED, real
      fix still not found," directly contradicting README/PATCHNOTES'
      "launch confirmed working live" status. Also still said "Back on the
      Grid" for the mortar/turret rows. Both corrected, with the squadmate
      call-in rows also upgraded from unlabeled to explicit ✅ CONFIRMED
      WORKING to match README.
    - `known_issues.md` issue #29's own HEADING still read "Predator
      Missile hypothesis REFUTED" while its own body, several hundred
      lines later, documents the fix and a live "CONFIRMED" launch —
      self-contradicting within the same file. Heading corrected to
      reflect the current, superseding status.
    - **README's Campaign compatibility summary row was left un-recomputed
      after the Goalpost correction**: table said "8 tested / 4 full / 4
      partial / 9 untested," but Goalpost moving from ✅ to ⚠️ (and "Back on
      the Grid" already having reset to ❓ untested in an earlier session)
      means the real count is **7 tested / 3 full / 4 partial / 10
      untested** — fixed. This also changed the Raw Functionality
      scorecard's own methodology (Campaign-compatibility dataset
      recomputed 75%→71%, Campaign killstreak-weapon-system dataset
      recomputed against README's own table 83%→69%, overall Raw
      Functionality score 83→**77/100**) — Feature Completeness (~84/100)
      is a separate axis and unaffected.
    - Everything else audited (all 4 commits' code diffs against their own
      commit messages, `re_notes/ui_assets.md`, `survival_mode_overview.md`,
      `survival_wave_scaling.md`, `iw5sp.md` spot checks) matched what was
      actually implemented — no further gaps found.
- **Added a scorecard to README.md**: raw functionality (~80/100, from the
  control map, `compatibility_matrix.md`, and `killstreak_reference.md`)
  and a feature-completeness matrix (~74/100, SP/Survival scope).
  Multiplayer is explicitly excluded from both scores rather than blended
  in at a misleadingly low weight, since it's a separate phase that hasn't
  started at all. **Iterated same day, twice**: (1) feature completeness
  was initially computed from the live task-tracking list (29 tasks,
  ~57/100) — flagged as not viable, since that list is an ever-expanding
  scratchpad where every newly-found bug adds another entry, making its
  completed/total ratio get worse the more thoroughly this project tests
  itself; recomputed from a curated named-system list instead (climbed to
  ~75-85/100 across a couple of revisions as genuinely-missing foundational
  items, like the injection/hooking layer itself and raw controller
  detection, were found by scanning the full commit history rather than
  just this file's own Feature List). (2) That flat list was then itself
  replaced with the current, more granular matrix — a single "aim assist:
  partial" row and a single "stick layout presets: done" row don't carry
  equal real-world weight, so large remaining systems (killstreaks, aim
  assist, the real options menu, vibration, button glyphs) are now broken
  into their own atomic done/not-done sub-items instead of one lightly-
  weighted line each, landing at 37/50 ≈ 74/100.

### Fixed
- **Full documentation pass across README.md/CONTRIBUTING.md/iw5sp.md
  (2026-07-18)** to close out the remaining Back/slider/exception-count
  drift the earlier targeted fixes didn't fully catch — found by
  systematically checking every claim rather than just the ones already
  flagged. Fixed: the top-of-file status summary (still said Back
  unassigned and sliders unadjustable), the D-pad and Survival ready-up
  feature descriptions ("one/second of two exceptions" → "of three"), the
  "Why native, not an emulator" section (only described ONE exception and
  claimed it was "the only place in the whole mod that does this" — wrong
  even before Back was added, since it never mentioned D-pad Left's
  exception either; rewritten to list all three), the squadmate call-in
  killstreak-table entry (still shown as an open "known bug" despite issue
  #14 documenting it fixed), the architecture diagram ("two exceptions" →
  three, added Back/TAB), the keyboard/mouse fallback list (still named
  sliders and Back as requiring keyboard), `CONTRIBUTING.md`'s own
  "two existing exceptions" rule text, and a stale Plutonium `iw5sp.exe`
  "~175KB smaller" figure in README's own client-compatibility table (the
  correct 2,320-byte figure had already been fixed in `known_issues.md`
  but never copied over here). Also added a superseding pointer to the old
  dead-end Back attempt recorded in `iw5sp.md`.
- **Corrected a stale "Campaign mostly untested" claim in README's
  killstreak sections (2026-07-18)**, in two places — the control-map
  table's Killstreaks row and the dedicated killstreak table's own intro
  line. Both were written before this session's Campaign playtest (8/17
  missions tested, see `re_notes/compatibility_matrix.md`) and were never
  updated. Also clarified the dedicated killstreak table is specifically
  Survival's buy-station roster, distinct from the newer Campaign-mission
  killstreak-type weapon systems table added earlier this session.
- **Documentation-drift correction pass, three items (2026-07-18).**
  Verified against the actual source (not assumed from prior docs) that
  Back's real `+scores` implementation (a third key-synthesis exception,
  synthetic TAB keypress, same technique as Survival ready-up and D-pad
  Left's squadmate call-in) was already fully written and wired up as of
  2026-07-17 — confirmed by rebuilding the project clean (0 warnings, 0
  errors) — but was never reflected anywhere: `known_issues.md`'s own
  summary still said "first of two" exceptions (now "first of three," new
  issue #28 documents the implementation), `README.md`'s control map still
  showed Back as "unassigned, not yet implemented," and task #5 stayed
  "pending" the whole time. Also corrected two related stale claims
  surfaced along the way, both confirmed live by the user: buy-station/
  armory D-pad navigation is 100% confirmed working (was marked "believed,
  not verified"), and slider-type settings VALUE adjustment via Left/Right
  is confirmed working (was marked "unsolved" — the original claim was
  based on one native function found via decompile without checking
  whether the `.menu` files' own script-level key handlers, already proven
  for options-pane drilling, also covered sliders directly). Back itself
  remains implemented-but-not-yet-separately-live-confirmed, not
  overclaimed as done.
- **Corrected a stale, self-contradicting README row (2026-07-18).** The
  "Current control map" table's "Menu/UI navigation" row said "Not yet
  implemented — mouse/keyboard still required," directly contradicting
  the same table's own pause-menu row two lines above (✅ Confirmed) and
  task #22's full write-up further down the same file — never updated
  when that work actually landed and was confirmed live. Split into four
  accurate rows: D-pad+A menu navigation (✅ confirmed), buy-station/armory
  navigation (🟡 believed working, not separately live-verified), slider
  value adjustment (⬜ still unsolved), and button-glyph prompts (⬜ not
  started) — matching what `known_issues.md` issue #22 actually says.

### Docs
- **`TacticalLefty` button layout preset CONFIRMED CORRECT against real
  hardware (2026-07-19).** This was the one open accuracy question left in
  the button-layout-presets system (task #15) — all four presets
  (`Default`/`Tactical`/`Lefty`/`TacticalLefty`) were reconstructed from the
  known-unchanged CoD4→MW2→MW3 console control scheme, but `TacticalLefty`
  specifically (Lefty with Tactical's face-button swap layered on top) had
  never been independently verified. User confirmation closes this out —
  updated the caveat text in `mod_config.h`/`mod_config.cpp` (the generated
  INI's own comments) and `README.md` accordingly, no functional code
  changed.
- **Added `re_notes/killstreak_reference.md` and a "Killstreak support"
  section to `README.md` (2026-07-18).** Two clearly-separated parts: a
  real, first-party controller-support status table for the killstreak-
  type weapon systems actually encountered and tested during this
  session's Campaign playtest (boat/UGV/door-gun/SMAW dumb-fire working;
  DPV aim, mortar fire, turret difficulty, SMAW lock-on, and Predator
  Missile's fire input each tracked with their specific known-issue/task
  number) — plus a separate, clearly-labeled MP killstreak reference list
  (3 strike packages, ~20+ rewards) sourced from public research, kept
  strictly as forward-planning material since MP work hasn't started.
- **Added a condensed compatibility summary table to `README.md`**, and
  wired `re_notes/compatibility_matrix.md` into the project's documented
  ownership model: `CODE_STANDARDS.md`'s "Documentation Standards" section
  now names it as the file that owns per-mission/per-mode live playtest
  status (alongside `iw5sp.md` for RE trail, `known_issues.md` for the
  issue list, `PATCHNOTES.md` for the changelog), and `CONTRIBUTING.md`'s
  "Reporting bugs" section now points contributors there before filing a
  mission-specific report, and welcomes PRs that add/correct compatibility
  entries. Keeps the new matrix file from being an orphaned addition —
  every doc that should reference it now does.
- **Simplified Survival tracking in `compatibility_matrix.md` to a single
  overall entry instead of 16 per-map rows (2026-07-18, user direction)**:
  unlike Campaign/Special Ops, Survival's controller support is
  map-independent (same input/engine hooks apply regardless of map), and
  live testing across maps confirms this — works well overall, with one
  known issue (Predator missile killstreak, cross-referenced to the
  existing task #7/issue #10).
- **Possible 8th playtest bug, NOT YET CONFIRMED (2026-07-18)**: SMAW may
  have failed to lock onto an aircraft target in "Goalpost" — but per the
  user's own follow-up, the target may be a non-targetable scripted
  entity, which would make this a non-issue rather than a real bug. Needs
  a same-target keyboard comparison before status changes either way
  (task #29). Logged as `known_issues.md` issue #27 bug #8, explicitly
  flagged as unconfirmed rather than treated as a defect.
- **First live Campaign playtest session (2026-07-17/18) — 7 bugs found
  across Act 1 through the start of Act 2 (missions 2-9), against
  everything else in that range confirmed fully working.** Logged in
  full as `known_issues.md` issue #27: DPV aim broken in Hunter Killer
  (bug #1); crouch intermittently fails to fire, ~2%, recovers after
  pause/unpause (bug #2); Hold Breath never implemented + L3 wrongly
  force-stands the player while ADS+crouched with a sniper (bug #3, task
  #24); movement hook bypasses the scripted player-freeze during
  Turbulence's plane-breakup sequence, potentially systemic (bug #4, task
  #25); mortar aim works but fire doesn't in Back on the Grid (bug #5,
  task #26); mounted-turret sequence feels harder on controller in the
  same mission, cause unconfirmed and flagged for dedicated deep
  investigation (bug #6, task #27); Interact didn't fire for a
  `+usereload`-gated tank-exit prompt in Mind the Gap (bug #7, task #28,
  confirmed root cause). **User confirmed (2026-07-18) every other
  mission in the missions-2-through-9 range was fully compatible** —
  Persona Non Grata's UGV, Davis Family Vacation, Goalpost, and Return to
  Sender's door gun all playable with zero fallback, alongside the
  partially-working missions' unaffected sequences (Hunter Killer's boat,
  Mind the Gap's opening aerial sequence). Mission 1 (Black Tuesday) and
  mission 10 onward (Bag and Drag, where the session paused) remain
  untested.
- **Added `re_notes/compatibility_matrix.md`**: a new, living per-
  mission/per-map controller-compatibility tracker (Campaign by mission,
  Special Ops and Survival each as individual entries), separate from
  `known_issues.md`'s technical RE trail — this file answers "what's
  actually been tested and how did it go," `known_issues.md` stays the
  place for the underlying bug/fix detail. Seeded with this session's
  playtest results; Special Ops and Survival rows scaffolded from this
  install's own real zone files but not yet tested.
- **Full-breadth engine research pass** (killstreaks, weapons, perks, HUD/UI, AI/vehicles,
  physics/health, and a research-only pass on `iw5mp.exe`) — no code changes, pure
  groundwork. Headline findings: a real, actionable hypothesis for Predator missile's
  partial-working state (task #7); confirmation that turret and AI-squadmate call-ins
  are genuinely separate script systems, not two branches of one (correcting task
  #13's own framing) with the squadmate bug's divergence point narrowed to a single
  unresolved function; Extreme Conditioning's native detection (task #9) confirmed
  genuinely parked, not just unstarted; a real, unambiguous god-mode bit found for
  task #20; and confirmation that MP (`iw5mp.exe`) shares the same core architecture
  as SP (same `usercmd_t` layout, same class of registration function) without yet
  resolving CLAUDE.md's open anti-cheat question, which any future MP work still
  needs first. Full detail in `re_notes/iw5sp.md` and `re_notes/known_issues.md`
  issue #26.
- Renamed the mod's project folder from `MW3 Survival and Campaign Controller
  Support/` to `MW32011NCP/` (same repo/git history) to match the project's actual
  GitHub name. A new sibling project, `MW32011NSP` (netcode/security modernization),
  now also lives at the game install root — `re_notes/` cross-referencing between the
  two is now a standing policy, see `CLAUDE.md`.
- **Corrected two factual errors in issue #25's Plutonium client-compatibility
  survey (2026-07-17, later session).** The "`iw5sp.exe` ~175KB smaller" figure
  was wrong — direct re-measurement (prompted by the sibling `MW32011NSP`
  project's own netcode research) found the actual size delta is 2,320 bytes;
  the ~175KB figure was very likely a byte-difference COUNT (175,411
  individual differing positions) mistaken for an overall file-size
  difference. Also added a cross-reference: `MW32011NSP` found that since
  Plutonium's `iw5mp.exe` is byte-identical to retail (already recorded here),
  a client-side netcode vulnerability they confirmed in retail `iw5mp.exe` is
  present on Plutonium MP installs too — Plutonium's routing-through-their-
  own-servers mitigation only covers server-side code. Full detail in
  `re_notes/known_issues.md` issue #25.

---

## v0.1.3 (2026-07-17)

The biggest research release so far, alongside one real shipped feature. Real,
native D-pad/A menu navigation is now confirmed working live across the main
menu, pause menu, and options screens. Everything else this release is deep
groundwork — a real controller-options-menu injection mechanism (blocked on a
genuine architectural limit, with a promising fix already found), a likely
static-analysis solution to aim assist's classification problem (not yet
live-verified, still disabled), real vibration trigger points, a complete
keycode reference, and an MW3-client-compatibility survey that surfaced a
concrete anti-cheat risk worth knowing about before ever pairing this mod with
Plutonium multiplayer. The zone/menu-injection debug trigger built during this
research is disabled for this build (real, working test code, just not a
finished player-facing feature yet) — see `re_notes/known_issues.md` issue #23.

### Added
- **Real native controller menu navigation (task #22): D-pad + A, confirmed working
  live across the main menu, pause menu, and options screens.** Extracted the game's
  own plain-text `.menu` UI definitions from `zone/english/ui.ff`/`ui_mp.ff` via
  OpenAssetTools, and decompiled the real key-event handler chain
  (`FUN_00541020` → `FUN_004d9850`/`ForwardKeyToMenu` → `FUN_004dfd30`) to find the
  actual keycodes real keyboard input uses for menu interaction — the same generic
  `ForwardKeyToMenu` call B's ESC-back already used turned out to forward ANY
  keycode, not just ESC. D-pad Up/Down send the real "previous/next item" alt-
  keycodes (`0x9a`/`0x9b`); D-pad Left/Right send the real, *separate* keycodes
  (`0x9c`/`0x9d`) that options-style two-pane screens (category list + that
  category's settings) specifically recognize for drilling in/out between panes —
  confirmed by finding the actual `execKeyInt 156`/`157` handlers in
  `ui/pc_options_video.menu` and matching them, rather than assuming Left/Right
  should behave the same as Up/Down. A sends real Enter (`0xd`) for select/activate.
  D-pad's normal gameplay actionslot dispatch and A's normal Jump are both
  suppressed while a menu is open so they can't mean two things at once. An initial
  guess using the standard idTech `K_UPARROW`/`K_DOWNARROW` constants (128/129) was
  live-tested and found completely wrong — the real values were read directly out of
  the decompiled dispatcher instead. See `re_notes/known_issues.md` issue #22 and
  `re_notes/iw5sp.md` for the full trail.

### Docs
- **Surveyed the real hint-text content behind pickup/reload/interact-style
  prompts** ahead of wiring the controller-glyph resolver hook (task #6's other
  half). Found three genuinely different substitution mechanisms in the real
  localized strings, not one: `&&1`-token strings (the entire weapon-pickup/perk/
  stance-hint family), a separate `[{+command}]` syntax embedded directly in some
  strings (confirmed NOT handled by the same `&&N` engine), and literal hardcoded
  PC-only text (`[Right Mouse]`/`[Left Mouse]`) that no resolver hook can fix at
  all. Also confirmed Reload has no hint text/bind token whatsoever — it's a
  plain HUD element, nothing to glyph-swap. **Follow-up (2026-07-17): traced the
  `[{+command}]` mechanism fully — good news, it routes through the exact same
  `FUN_0061f6f0` bind-resolver the `&&N` path already uses, so one hook covers
  both, not two.** See `re_notes/ui_assets.md`.
- **Complete real keycode reference recovered (95 entries)** — traced the real
  resolution chain from the bind-resolver down to the raw `{name, keynum}` table
  the game itself uses, ending years of finding individual keycodes ad hoc one at
  a time. Notably includes `AUX1`-`AUX16`, the idTech/Quake3-lineage joystick-
  button placeholder range — structurally present and bindable, unused since this
  build has no XInput import. New reusable script,
  `re_notes/ghidra_scripts/DumpKeynamesTable.java`. See `re_notes/iw5sp.md`.
- **GSC mission-scripting architecture survey.** Cataloged the ~140 real `.ff`
  zones (10 Campaign missions, 15 Spec Ops missions, 18 Spec Ops Survival maps,
  shared/common code) and found the Survival/Spec Ops buy-station economy is
  **data-driven, not GSC-driven** — a single CSV (`sp/survival_armories.csv`)
  defines every weapon/attachment/perk/killstreak with price and wave-gate, no
  scattered purchase logic to reverse-engineer. Surfaced the full real perk/
  killstreak roster and the `maps\<levelname>::main()` mission-entry convention.
  See `re_notes/iw5sp.md`.
- Committed `re_notes/ghidra_scripts/FindStrideArrayBase.java` (used during the aim-
  assist entity-classification investigation, task #16) — a general-purpose static-
  analysis tool independent of that investigation's outcome, so kept regardless.
- **Re-extracted `assets/button_glyphs/` from a cleaner, user-trimmed source sheet**
  (still pure source-art groundwork, not yet wired into any rendering code — see
  task #6's other half). Replaces the original 106-icon set (all platforms/
  generations, with one unresolved text-bleed issue on `ps4_circle`) with 47 icons
  across three slimmed style groups (Xbox 360/Classic, Xbox Modern, PlayStation)
  plus universal D-pad/stick-direction indicators. Re-extracted with a proper
  connected-component labeler instead of row/column-band heuristics, so the old
  clipping/text-bleed issues can't recur — every icon's bounding box now comes
  directly from its own alpha-channel content. D-pad only ships one real icon
  (`dpad_up`); the other three directions are the same asset rotated
  90°/180°/270° programmatically, per the user's explicit design intent (D-pad
  glyphs are visually identical across brands, no need for four separate source
  crops). See `re_notes/ui_assets.md` for the full naming scheme and method.

- **First implementation of aim assist (rotational friction + magnetism, task #16)
  — EXPERIMENTAL, NOT FUNCTIONAL, DISABLED BY DEFAULT. Must stay disabled for any
  public/release build.** The native aim-assist system turned out to be shared math
  bots use to aim at the player, not a player-facing feature (MW3 PC genuinely has no
  mouse aim-assist) — so this is built entirely from scratch instead: real entity
  position data plus our own targeting and curve math (the curve shape recovered
  from this game's own `aim_assist/view_input_0.graph` asset), applied directly on
  top of the same look-angle globals controller look already writes to. New
  `[AimAssist]` config section (`Enabled`, `Range`, `ConeDegrees`,
  `FrictionStrength`, `MagnetismDegreesPerSecond`). Live-tested across several
  tuning passes: the core math (angle error, friction curve, magnetism) is confirmed
  correct via diagnostic logging, but the target-validity filter (currently
  movement-based — a real prop never moves, a living AI's position does) is
  genuinely broken in practice — it oscillates between multiple
  simultaneously-moving things (a real enemy, a settling ragdoll, a thrown grenade),
  not just imprecise. **Intended** to ship with `Enabled=0` — see the Fixed entry
  below, this wasn't actually true until a bug found right before release was
  corrected. Do not flip this on outside active development until a real
  type/health-based classification replaces the movement heuristic — see
  `re_notes/known_issues.md` issue #15.

### Fixed
- **Aim assist's config default was `true`, not `false` — every brand-new install
  would have shipped with the confirmed-broken aimbot silently turned on.**
  `mod_config.h`'s `aimAssistEnabled` struct default was `true` since the feature
  was first added; every doc (README, this changelog, `known_issues.md`) said the
  opposite the whole time (`Enabled=0`, "disabled by default"). Went undetected
  because the config file only gets freshly generated when none exists yet — this
  development machine already had a hand-corrected `Enabled=0` on disk from
  earlier testing, which masked the bug locally the entire time. Caught during a
  pre-release check (explicitly asked for, right before packaging v0.1.3) and
  confirmed via full trace: the struct default is the only place this value is
  ever set for a fresh config, `WriteDefaultConfig` writes it verbatim, and an
  existing file's saved value is otherwise correctly respected on later launches.
  Fixed to `false`, rebuilt, verified via full code trace (not just "should be
  fixed"). **A real lesson for this project going forward**: an already-populated
  local dev config can mask exactly this class of default-value bug — worth a
  fresh-install check (rename/delete the local config, confirm what gets
  regenerated) before any future release, not just before this one.
- **Start's pause/unpause could desync from the real game state if the player also
  used keyboard ESC.** `InjectControllerPauseMenu` tracked its own `g_paused` bool,
  updated only on a controller Start press — the same class of "manually-tracked
  copy can drift from the engine's own real state" bug the crouch/prone rewrite
  (see below) was built to eliminate, just not caught in this specific function at
  the time. Keyboard ESC also natively opens/closes the pause menu (keyboard/mouse
  stays fully supported alongside controller), so a player switching between the
  two could leave `g_paused` believing the wrong thing, making the next controller
  Start press act on stale state — potentially eating that press with no visible
  effect. Found during a pre-release code review, ahead of v0.1.3. Fixed the same
  way the crouch/prone rewrite fixed its own version of this bug: reads the real
  `cl_paused` dvar directly via the existing `GetDvarInt` helper instead of
  trusting a local copy, eliminating the desync class entirely rather than
  patching around it.
- **Sprint's stamina timer could compute a huge, bogus time delta after a
  controller disconnect/reconnect.** The tick-baseline timestamp only got
  refreshed on ticks where a controller was actually present — a disconnect while
  sprint was held, followed by a reconnect, computed `dt` across the entire
  disconnected duration on the next tick. Self-correcting in practice (clamps
  stamina to 0, marks winded with the normal cooldown — not a hang or crash), but
  an avoidable inconsistency with the exact pattern this function had already
  established for a different case (the `player_sprintUnlimited` bypass path
  right below it). Found during the same pre-release review; fixed to refresh the
  baseline on the no-controller path too.
- **Two `[Sprint]` config values had no lower-bound guard, unlike every other
  tunable float in the same file.** Hand-editing `RegenSeconds=0` alone produces a
  divide-by-zero (harmless — clamped away the same tick); `MaxStaminaSeconds=0`
  together with it makes it `0/0`, permanently setting the stamina value to `NaN`
  (the existing `>= 0` clamp is always false for `NaN`, so it never
  self-corrects). Only reachable via manual config editing, not normal play — same
  found-during-pre-release-review batch. Fixed with the same clamp-on-read pattern
  already used for every other config value in this function.
- **B didn't exit the pause menu.** The ESC-forward logic (`InjectControllerMenuBack`)
  was only ever wired into the per-frame gameplay tick, which stops running entirely
  while genuinely paused — so B's menu-back action never fired in the one state it
  exists to handle. Now also driven from the same always-running WndProc/timer tick
  that already handles Start's open/close.
- **Crouch fired unexpectedly when exiting pause with B.** A side effect of the fix
  above: B is also the crouch/prone button, and its tap/hold tracking went stale while
  paused, so the same press that closed the menu looked like a fresh crouch tap the
  instant gameplay resumed. Fixed by tracking, per B press, whether it ever touched an
  open menu, and suppressing crouch/prone for that press if so — scoped to the actual
  current press rather than any menu open/close in general, so it can't suppress a
  genuine crouch/prone elsewhere. See `re_notes/known_issues.md` issue #13.
- **Pausing while a buy-station menu was open left it stacked underneath the pause
  menu**, and unpausing would have dropped the player back inside it instead of into
  plain gameplay. Start now auto-closes any other open menu (the same real ESC-forward
  mechanism B itself uses) before opening the pause menu, so pause always opens cleanly
  on top of gameplay and unpausing always returns straight to it.
- **D-pad Left (Survival AI-squadmate call-in) failed 100% of the time**, while turret
  call-ins on the same slot worked fine. Confirmed unique to Survival, not a general
  regression — real keyboard `'4'` (the same bind) worked correctly the whole time.
  Fixed by synthesizing a real key press for `'4'` instead of calling the native
  weapon-switch function directly, for D-pad Left only (the other three D-pad
  directions are unchanged). Same category of workaround as Survival ready-up's F5
  synthesis, not a general policy change — see `re_notes/known_issues.md` issue #14.
- **Turret couldn't be un-toggled once deployed via D-pad Left.** Turns out to be a
  genuine bug in the old direct-call implementation, not a native limitation — the real
  `+actionslot4` behavior is a plain press-to-toggle, but the old call pair only ever
  drove the "deploy" side. Fixed for free by the same key-synthesis change above, since
  it now goes through the real dispatcher's own toggle logic.

### Investigated, not resolved
- **`dllmain.cpp`'s generic export-forwarding stubs have no null-pointer guard,
  accepted as a known, low-risk limitation, not fixed.** Found during the same
  pre-release review as the fixes above: `FORWARD_STUB`'s naked tail-jump forwards
  ~15 obscure real `d3d9.dll` exports (`D3DPERF_*`, `PSGPError`, etc.) without
  checking the resolved pointer is non-null first — if the real system `d3d9.dll`
  were ever missing one AND the game somehow called it, this jumps through a null
  pointer and crashes. Deliberately not fixed: these are intentionally
  unknown-arity stdcall/cdecl exports (the whole point of the tail-jump approach
  is not needing to know each one's real signature), so a "graceful" fallback
  can't safely `ret` without knowing how many bytes of the caller's stack to
  clean up — a naive guard would either need the real signature anyway (defeating
  the point) or risk corrupting the caller's stack on return, worse than the
  crash it would guard against. Real-world risk is low: these are standard
  exports present on essentially any genuine Windows `d3d9.dll`.
  `Direct3DCreate9` — the one export MW3 unconditionally needs — is correctly
  guarded elsewhere (`ResolveRealExports` returns `false` and `DllMain` aborts
  entirely if that specific one is missing).
- **Real controller options menu (task #23): native zone/menu injection pipeline
  built and confirmed working for bare content, blocked on a real architectural
  limit for real content — but a structurally-sound fix was found the same day.**
  Built and live-confirmed an entirely in-memory mechanism to inject a
  custom-compiled `.menu` asset into the running game via its own real
  zone-loading system — a bare custom menuDef genuinely rendered in the real
  pause menu's own slot, real `ui.ff` never touched on disk. Real menu content
  (anything with a background material, virtually all of it) turned out to be
  fundamentally unsafe via LIVE injection: loading a material triggers a genuine
  D3D9 GPU-resource-creation cascade unsafe outside the engine's own controlled
  loading context, with no workaround available from a live hook. **Follow-up
  research the same day found a real fix, not a workaround**: `LoadZones` has
  exactly 4 real callers in the whole binary; two of them (a level-load call site
  and a boot-time/main-menu call site) have enough stack-array headroom to safely
  append our own zone entry via a single hook, distinguishing real callers by
  return address. This loads our content through the engine's own genuinely safe
  context instead of a live hook — recommended next implementation step, before
  the `ui.ff`-on-disk-replacement fallback (whose backup/hash-verify safety net,
  `tools/ff_installer/backup_and_verify.ps1`, is already built and live-tested).
  Full trail in `re_notes/known_issues.md` issue #23 and `re_notes/iw5sp.md`.
- **Aim assist target classification — likely solved statically, not yet
  live-verified.** Following a lead from the cragson/mw3-surviv0r reference repo's
  own aimbot source, found strong static evidence of a real, second entity array
  in our own binary (base `0x01197AD8`, stride `0x270`) carrying type/health
  fields — but the assumed link from our existing entity array to it (a
  hypothesized clientnum field) produced garbage values in a live test and was
  disproven. **Follow-up research found the array doesn't need that link at
  all**: a real checkpoint/save-deserialization function proves it's fixed-
  capacity (2048 slots), independently walkable via its own parallel validity-
  flag array, with zero dependency on `centity`. The same function independently
  confirms type `13` is the real AI-actor type (from our own vanilla binary, not
  just the reference repo). `type==13 && health>0` is genuine native
  classification with no movement heuristic needed — very likely the actual fix
  for the oscillation bug that's kept this disabled all along. Still needs a live
  diagnostic pass before shipping. See `re_notes/known_issues.md` issue #15.
- **Vibration/rumble (task #17) — real trigger points found, not yet
  implemented.** No native vibration infrastructure exists (confirmed empty
  search), so output has to be entirely our own `XInputSetState` calls — research
  found confirmed, hookable native events for weapon fire (a single clean choke
  point, fires per-shot for both semi/full-auto) and player/entity damage
  (carries the literal damage amount, usable for intensity scaling; needs a
  local-player filter, not yet resolved). Explosions, melee, and killstreak
  activation not yet traced. See `re_notes/known_issues.md` issue #24.
- **MW3 client compatibility survey (Plutonium/AlterWare/DeckOps) — research
  only.** Long-term goal is supporting other MW3 clients, not just retail Steam.
  Plutonium (installed locally, directly compared): `iw5mp.exe` is byte-identical
  to retail, but **its anti-cheat is confirmed to ban DLL injection/memory
  access** — do not use this mod with Plutonium MP. `iw5sp.exe` differs
  significantly from retail. AlterWare IW5-Mod (SP+Spec Ops specific, its own
  separate binary, not yet acquired) looks like the most promising third-party
  target given this project's SP-first scope and no known anti-cheat concern.
  DeckOps isn't a separate client — it wraps Plutonium for Steam Deck, inheriting
  its anti-cheat risk. See the new **Client compatibility** section in README.md
  and `re_notes/known_issues.md` issue #25.

### Added
- **`AdsSlowdownBaseline`** — a new `[Look]` config value multiplied on top of the
  existing zoom-proportional ADS slowdown curve. Live feedback: the pure
  `ratio^strength` curve gave almost no slowdown at all on low-zoom optics (iron
  sights/red dots), since the zoom ratio itself stays too close to `1.0` for any
  power of it to produce a noticeable effect, regardless of strength. This baseline
  applies real slowdown even at minimal zoom while preserving the proportional shape
  on top for higher-magnification optics. Default `0.65` (started at `0.85`, further
  lowered after live testing showed more slowdown felt better even at minimal zoom).
  Same safety guarantee as strength — guarded `>= 0.0`, so the combined scale factor
  can never go negative/invert at any value.

### Changed
- **Default `AdsSlowdownStrength` raised from `1.0` to `1.75`** (via `1.5` first, then
  further refined live). Confirmed live to feel closer to real console controller CoD
  than exactly proportional (`1.0`).
- **Default `[Interact] HoldThresholdMs` lowered from `740` to `300`.** Confirmed live
  to feel better than the original 740ms default. Comment wording also simplified to
  describe the observed net effect (a quick tap reloads, same as console) rather than
  the underlying two-mechanism split (Interact's hold-gated usercmd bit vs. Reload's
  own always-instant kbutton, which fires on every press regardless of hold duration
  and is what actually produces the "quick tap reloads" behavior).

Both defaults only affect freshly-generated `mw3ncp_config.ini` files — existing
configs keep whatever value is already in them; edit by hand to pick up new defaults.

---

## v0.1.2 (2026-07-16)

Mostly a documentation-accuracy release: several features already present since
v0.1.1 (or earlier) had never been written up in the README or a changelog at all,
and a proofread pass against the actual source found real inaccuracies in the ones
that were. Small functional fix included (INI comment text only — no behavior
change).

### Newly documented (already shipped, not previously covered anywhere)
- **`mw3ncp_config.ini`** — self-generating configuration file, written next to the
  DLL the first time the mod runs, with every option pre-filled at its default value
  and a comment explaining it. Covers `[Look]` (sensitivity, ADS slowdown strength,
  invert look), `[Stance]` (B hold-vs-tap threshold), `[Interact]` (hold threshold),
  `[Survival]` (ready-up hold threshold), `[Sprint]` (stamina/regen seconds), and
  `[Bindings]` (button layout, stick layout, trigger flip). No live-reload yet —
  changes take effect on next launch. See README's **Configuration & customization**
  section for the full key reference.
- **Button layout presets** — `Default` / `Tactical` / `Lefty` / `TacticalLefty`,
  reconstructed from the unchanged CoD4→MW2→MW3 console control scheme (not
  independently verified against real hardware yet — `TacticalLefty` in particular
  may need a correction pass; see README for the full per-preset table).
- **Stick layout presets** — `Default` / `Southpaw` / `Legacy` / `LegacySouthpaw`.
  `Legacy` swaps only the horizontal axes between sticks (left stick keeps
  forward/back but turns instead of strafing; right stick keeps look up/down but
  strafes instead of turning) — the historical CoD4-era scheme, not a full stick
  swap.
- **`FlipTriggers`** — an independent toggle that swaps RT↔RB and LT↔LB, layered on
  top of whichever button layout is active.
- **Invert Look** — the OG console option, flips vertical look.
- **Interact (X) requires a hold, not an instant tap** (task #11) — a press released
  before the threshold (740ms default, configurable) simply does nothing; Reload (a
  separate real kbutton on the same physical button) is completely unaffected and
  still fires instantly. This was already implemented but wasn't reflected in the
  task list or any doc until now.
- **ADS look-slowdown fix and the Crouch/Prone real-toggle rewrite** (see v0.1.1's
  own entry below for what these actually fixed) are now covered by full mechanic
  tables in README (the stance ladder's tap/hold-per-state table, and Sprint's
  ready/sprinting/winded/regenerating state machine) — neither had a full
  state-transition writeup anywhere before this pass.

### Fixed
- **Three inaccurate setting descriptions**, found during the proofread pass above,
  corrected in both `mw3ncp_config.ini`'s self-generated comments
  (`mod_config.cpp`'s `WriteDefaultConfig`) and the matching README prose:
  - `[Look] Sensitivity` was described as "right-stick" unconditionally — it's
    actually whichever stick `StickLayout` currently routes to look, not always the
    right stick.
  - `[Stance] ProneHoldThresholdMs` described "hold" as simply "go prone," which is
    wrong for the Prone→hold transition specifically (that one stands you back up,
    the reverse). Corrected to describe the full 3-state ladder.
  - `[Interact] HoldThresholdMs` incorrectly claimed a quick tap "switches weapons
    instead" — that's Y/ready-up's behavior, not Interact's. A quick Interact tap
    does nothing; Reload (separate, same physical button) is unaffected either way.
  - Rebuilt `d3d9.dll` so the corrected INI comments actually ship (no other
    behavior change in this build vs. v0.1.1).

---

## v0.1.1 (2026-07-16)

### Fixed
- **Keyboard/mouse sprint regression.** The controller Sprint hooks
  (`InjectControllerSprintPmFlags`/`ReassertSprintPmFlags`) are wired directly into a
  real per-tick engine entry point and ran unconditionally — with no controller
  engaging sprint, they unconditionally cleared the real `pm_flags` sprint bit every
  tick, silently breaking vanilla keyboard Shift-to-sprint regardless of whether a
  controller was plugged in at all (a fully-unplugged controller, or one connected
  but idle, both triggered it). Fixed with bit-ownership tracking: the hooks now only
  ever clear a bit they set themselves, leaving real keyboard/native input completely
  untouched otherwise.
- **ADS look-slowdown could invert look direction on deep zooms.** The slowdown
  formula was a linear blend (`1 - strength*(1-ratio)`) that went negative — inverting
  look — for any configured strength above 1.0 once the zoom ratio dropped low enough
  (a real ACOG-level zoom, not an edge case). Not a native engine bug, not FPU
  corruption (both theories investigated and ruled out via diagnostic logging) — just
  the formula's own shape. Fixed by switching to a power curve (`ratio^strength`),
  which can never go negative for any non-negative strength while still allowing a
  stronger-than-proportional slowdown at high strength values.
- **Crouch/Prone rewired to the real native togglecrouch/toggleprone toggle**,
  replacing the mod's own tracked stance state and per-frame bit-forcing. Fixes a real
  stuck-prone bug (a Campaign session neither B nor Sprint could recover from stance
  lock, but real keyboard Ctrl could) and, as a side effect, a separate game-breaking
  bug where using the Predator missile killstreak while prone left the player
  permanently stuck prone. The stance ladder's user-facing behavior (tap/hold →
  crouch/prone, see README) is unchanged — only the underlying implementation, which
  no longer has a separate copy of stance state that can desync from the engine's own.

### Added
- **B backs out of menus like ESC.** B now forwards a real ESC keypress
  (`FUN_004d9850`) to whatever menu is currently active — the same real mechanism the
  engine's own key handler uses for ESC generically, not something pause-specific —
  so B backs out one level in the main menu, closes the pause menu (same as Start),
  or exits any other open menu. Hardcoded to physical B regardless of button-layout
  preset.
- **106 controller button-glyph icons extracted** (`assets/button_glyphs/`) covering
  Xbox 360, Xbox One, Xbox Series X|S, PS3, PS4, PS5, D-pad/stick-direction
  indicators, and shared/extra buttons — source art groundwork for native
  controller-glyph button prompts. **Not yet wired into any rendering code** — this
  is asset preparation only; see `re_notes/ui_assets.md` for the two remaining
  pieces of implementation work (a bind-text-resolver hook, and getting the art into
  a font the game will actually render) and one known outstanding polish issue (a
  faint stray text fragment in a couple of PS4 icons).
- **`tools/memdiff` gained a `poke` mode** (write-test a candidate memory address's
  behavioral effect live, with a configurable lead-in countdown) and a `rangewatch`
  mode (live-correlate a real key/button against one fixed, already-known-real
  address range instead of the whole process heap) — dev-only diagnostic tooling,
  not shipped as part of the mod itself. Rebuilt as x64 (was x86, which started
  hitting its own ~2GB address-space ceiling once heap-scan caps were widened).

### Changed
- **Keyboard/mouse deprioritized as a primary input path, not removed.** A direct
  consequence of the regression above: keyboard/mouse remains functionally required
  for menu navigation, Back, and most killstreak call-ins (none of which are
  controller-native yet), but is no longer verified to the same live-reproduction
  bar controller features get going forward. Controller is the primary,
  actively-verified input method with this mod installed. See
  `re_notes/known_issues.md` issues #10-#11.

### Investigated, not resolved
- **Sprint's real `+breath_sprint` kbutton — parked.** Three independent techniques
  (whole-process heap correlation, live write-testing the strongest candidates, and
  a targeted scan restricted to the confirmed-real kbutton neighborhood used by
  ADS/Reload) all came back negative. Controller Sprint keeps its existing
  `pm_flags`-forcing implementation. Full trail in `re_notes/iw5sp.md`.
- **Real native sprint duration/cooldown timer — found, but unobservable while our
  own hook drives sprint.** Traced the real sprint-meter HUD render path to
  `FUN_004b9350`, a genuine current/max stamina-ratio function — but it early-exits
  to a flat baseline whenever `pm_flags` bit `0x4000` is already set, which this
  mod's own Sprint hook forces unconditionally every tick. So the real timer can't
  be observed (or benefited from) as long as sprint is driven by forcing that bit
  directly rather than through the real native trigger path. Switching to whatever
  real `kbutton_t`/command actually engages sprint (once found — see the parked
  `+breath_sprint` search above) would make the mod's own sprint naturally subject
  to the real timer, perk multipliers, and Extreme Conditioning, without needing to
  replicate any of it by hand — a real architecture change, not attempted yet, and
  current sprint behavior is already confirmed working well so this needs a
  deliberate decision before touching it. Full trail in `re_notes/iw5sp.md`.

---

## v0.1.0-prealpha (2026-07-15)

Initial pre-alpha release. Analog movement, look, and most buttons confirmed working
live against `iw5sp.exe` (Campaign/Survival):

- Analog movement (left stick) and look (right stick), both driven through real
  engine calls, not mouse/keyboard emulation.
- Fire (RT), ADS (LT, true hold via real `kbutton_t` calls), Melee (R3),
  Tactical/Lethal (LB/RB), Jump (A).
- Crouch/Prone stance ladder (B) — tap toggles crouch, hold goes prone.
- Interact + Reload (X) — real, context-sensitive `kbutton_t`.
- Sprint (L3) — real `pm_flags` bit, auto-stands from crouch/prone first, with a
  real stamina/cooldown model (4s sprint, 2s cooldown), correctly bypassed during
  missions that live-set `player_sprintUnlimited`.
- Weapon switch (Y) — real `weapnext` dispatch.
- Start — opens and closes the pause menu via real engine calls, working even while
  the game's own gameplay-simulation tick halts during pause.
- D-pad (all 4 directions) — real `+actionslot 1-4` dispatch, data-driven by
  loadout.
- Survival ready-up (hold Y ~740ms between waves) — the one deliberate exception to
  this mod's native-only approach (synthesizes a real F5 keypress; the real native
  trigger was never found despite an extensive search).
- Buy-station + pause interaction fix (a real native engine bug, not ours, that
  could permanently break all input until level reload).

Known limitations at this release: Back unassigned, killstreaks need per-killstreak
work, full menu/UI navigation not implemented, Multiplayer not started.
