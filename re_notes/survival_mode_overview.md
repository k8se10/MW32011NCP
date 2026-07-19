# Survival mode — full architecture overview (2026-07-18)

Broad survey of MW3 Survival's real structure, cross-referencing this
session's narrower research (buy-station economy, killstreaks, ready-up,
wave scaling — see `killstreak_reference.md`/`survival_wave_scaling.md`).
Research only — reference material, not yet used by any mod code.

## Overall game-mode structure

Core wave loop: `1571.gsc` function `_id_3D47()` (lines 705-806).
`level._id_17F6` is the real wave counter. Enemy composition per wave is
computed via `_id_061C::_id_3E04/_id_3E05/_id_3E07(level._id_17F6)` — a
separate script (owning the actual spawn-count/type logic) that was
referenced constantly this session but never located by filename, still
an open thread. A per-wave data structure (`level._id_3D45`, indexed by
wave number) drives this, backed by `sp/survival_waves.csv` (see
`survival_wave_scaling.md` for the full table). A real `"boss_spawning"`
notify and an `aggressive_mode` flag (set/cleared once per wave, gates
whether AI reinforcements spawn aggressively) confirm elevated-intensity
wave behavior, gated per-wave via `level._id_3D47[wave]._id_3D4F`.

Between-wave ready-up (`_id_3F51()`/`_id_3F83()`) is a real 30s-then-5s
countdown before the next wave starts (matches this project's
already-known ready-up trace, `known_issues.md` issue #5).

**Real game-over path**: `179.gsc`'s `_id_18D0()` — `missionfailed()`
(real builtin) → `_id_17F9(0)` sets `special_op_terminated` flag +
`ui_mission_success=0` + `ui_opensummary=1` (drives the end-of-round
summary screen, `popup_summary_so`/`victoryscreen` menus already
catalogued in `ui_assets.md`'s menu inventory). **`_id_18D0()`
early-returns entirely if `getdvarint("so_nofail")` is true** (line
587) — a real, dvar-gated "can't fail the mission" switch, a much
simpler god-mode candidate than the entity-flag bit found separately
for task #20 (see `known_issues.md`'s task #20 entry for the comparison).

## Scoring/currency — two separate systems

- **Buy-station currency**: `credits`, a distinct per-player field
  (`self._id_18D3["credits"]`, initialized to 0 per player at round
  start, `1571.gsc` line ~1148) — separate from combat score.
- **Combat score**: standard `registerscoreinfo()` calls in `97.gsc`
  (kill=100, headshot=100, assist=20, suicide=0, teamkill=0,
  completion_xp=5000 end-of-round bonus).
- **Wave-end bonus tuning** (`1571.gsc` `_id_3F93()`): a difficulty
  check (`maps\_utility::_id_12C1()`) selects between two bonus tables —
  wavebonus/headshot/kill = 50/50/50 on one branch, 25/20/10 on the
  other. NOT confirmed which branch maps to which named difficulty.
- Currency/score tracked **per-player** (`foreach (var_1 in
  level.players)`), not a shared team pool.

## Player state/persistence

Real Last-Stand-style downed/revive system confirmed: events
`player_downed`/`so_revive_success`, flags `laststand_downed`/
`laststand_lives_updated`, a purchasable `specialty_self_revive` perk.
Per-player `downed`/`revives` counters tracked — teammates can revive a
downed player, self-revive is purchasable. **NOT resolved**: whether
purchased loadout/killstreaks reset on death/being downed vs. persisting
for the run — open question, not found this pass.

## Environmental hazards

**Inconclusive.** A broad grep for barricade/board-window terms hit many
files, but spot-checking suggests most are generic AI pathing terms
(enemies vaulting through windows), not player-buildable Zombies-style
barricades. Not confirmed or ruled out either way — would need
dedicated file-by-file reading.

## AI/behavior

One confirmed Survival-specific mechanic: the `aggressive_mode` flag (set
during active combat within a wave, cleared once the wave's enemies are
cleared/boss defeated) — the one concrete AI-intensity toggle specific
to Survival's wave structure found this pass. No deeper AI-behavior
comparison against Campaign attempted.

## Co-op

Confirmed real **2-player** co-op is the built structure —
`ui_eog_player1_bestscore`/`ui_eog_player2_bestscore` (end-of-game UI
dvars), `surHUD_performance`/`surHUD_performance_p2` (HUD performance
fields). No evidence of >2-player support found anywhere.

## Open threads for a future pass

- `_id_061C`'s own defining script (owns the real per-wave enemy-count/
  type formulas) — referenced constantly, never located by filename.
- Loadout/killstreak persistence on death/downed state.
- Environmental hazards beyond a surface grep.
- Which wave-end bonus tier maps to which named difficulty.
