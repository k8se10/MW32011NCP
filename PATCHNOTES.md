# Patch Notes

All notable changes to the mod, per release. See `re_notes/known_issues.md` for the
full, actively-tracked issue list and `re_notes/iw5sp.md` for the underlying
reverse-engineering trail behind each entry.

---

## v0.1.1 (2026-07-16)

### Added — configuration & customization
- **`mw3ncp_config.ini`** — self-generating configuration file, written next to the
  DLL the first time the mod runs, with every option pre-filled at its default value
  and a comment explaining it (nothing to configure by hand to get started). Covers:
  - `[Look]` — look sensitivity (deg/sec), ADS zoom-aware slowdown strength, invert
    look.
  - `[Stance]` — the B stance-ladder hold-vs-tap threshold.
  - `[Interact]` — the new Interact hold threshold (see below).
  - `[Survival]` — the ready-up hold threshold.
  - `[Sprint]` — max stamina seconds / regen seconds.
  - `[Bindings]` — button layout, stick layout, trigger flip (all below).

  No live-reload yet — changes take effect on next launch. See README's
  **Configuration & customization** section for the full key reference.
- **Button layout presets** — `Default` / `Tactical` / `Lefty` / `TacticalLefty`,
  reconstructed from the unchanged CoD4→MW2→MW3 console control scheme. Remaps
  Fire/ADS/Lethal/Tactical/Crouch-Prone/Sprint/Melee as a set per preset (not
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
- **Interact (X) now requires a hold, not an instant tap** — matches feedback that
  tap-to-interact felt too eager. A press released before the threshold (740ms
  default, configurable) simply does nothing (no fallback action on a quick tap);
  Reload (a separate real kbutton on the same physical button) is completely
  unaffected and still fires instantly.

### Added — other
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

### Changed
- **Keyboard/mouse deprioritized as a primary input path, not removed.** A direct
  consequence of the sprint regression above: keyboard/mouse remains functionally
  required for menu navigation, Back, and most killstreak call-ins (none of which are
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

### Docs
- **Corrected inaccurate setting descriptions** in `mw3ncp_config.ini`'s
  self-generated comments (and the matching README/PATCHNOTES prose), found during
  a comprehensive proofread pass against the actual source behavior:
  - `[Look] Sensitivity` was described as "right-stick" unconditionally — it's
    actually whichever stick `StickLayout` currently routes to look, not always the
    right stick.
  - `[Stance] ProneHoldThresholdMs` described "hold" as simply "go prone," which is
    wrong for the Prone→hold transition specifically (that one stands you back up,
    the reverse). Corrected to describe the full 3-state ladder.
  - `[Interact] HoldThresholdMs` incorrectly claimed a quick tap "switches weapons
    instead" — that's Y/ready-up's behavior, not Interact's. A quick Interact tap
    does nothing; Reload (separate, same physical button) is unaffected either way.

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
