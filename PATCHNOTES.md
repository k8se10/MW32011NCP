# Patch Notes

All notable changes to the mod, per release. See `re_notes/known_issues.md` for the
full, actively-tracked issue list and `re_notes/iw5sp.md` for the underlying
reverse-engineering trail behind each entry.

---

## Unreleased

### Docs
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
