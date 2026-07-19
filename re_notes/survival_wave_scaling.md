# Survival wave/difficulty scaling — real, CSV-verified (2026-07-18)

Full trace of `common_survival.ff` script `1571.gsc`, cross-referenced
against the real data table it consumes. Research only — not used by any
mod code yet, reference material for future work (debug menu wave-skip,
a possible difficulty-tuning feature, or just understanding the mode).

## Wave-loop mechanism

- Per-player wave counter: `level._id_17F6`, initialized to `1`
  (`1571.gsc` line 653), incremented via `level._id_17F6++` (line 803)
  inside the main wave loop.
- Real notify events bracket each wave: `level notify("wave_started",
  level._id_17F6)` / `level notify("wave_ended", level._id_17F6)`.
- HUD wave-number display: `maps\_specialops::_id_18A6("surHUD_wave",
  level._id_17F6)`.
- The ready-up mechanic (`known_issues.md` issue #5) only shortens the
  prep window between `wave_ended` and the next `wave_started` — confirmed
  separate from the counter itself, doesn't skip a wave.

## Scaling is DATA-TABLE-driven, not a formula

`level.loadout_table = "sp/survival_waves.csv"` (line 19), consumed via
`tablelookup(level.loadout_table, ...)` and further via
`_id_061C::_id_3E04`–`_id_3E09` wrapper functions keyed on
`level._id_17F6`. Real CSV at
`sp/survival_waves.csv`, two sub-tables:

### Per-wave rows (waves 1–21, 0-indexed 0–20)

Columns: wave index, constant `1`, display wave number, **difficulty
tier name** (`easy`/`regular`/`hardened`/`veteran`), base enemy count,
special-enemy-type list, per-type count list, juggernaut type(s),
chopper-wave marker, a flag column (`1` from wave 16 onward), unlock-
category hint (weapon/equipment/airsupport — only present on the first 3
rows).

### Enemy-type roster (ids 100–116)

Columns: name, display name, description, actor classname, weapon,
grenade type, **health**, **speed multiplier**, score value, an
armor/heavy flag, an accuracy/damage multiplier.

| Tier | Health | Speed mult. | Damage mult. |
|---|---|---|---|
| easy | 50 | 0.9 | 1.3 |
| regular | 86 | 1.0 | 1.1 |
| hardened | 158 | 1.1 | 1.2 |
| veteran | 194 | 1.2 | 1.5 |
| elite | 266 | 1.3 | 1.8 |
| juggernaut variants | 1000–2000 | — | — |

**`elite` is in the roster but never appears in any of the 21 authored
wave rows** — likely unused in the authored range, possibly a leftover/
future-use tier.

## Enemy composition — confirmed wave-gated, escalating

Special types layer onto the base infantry tier progressively:
- Wave 3: dogs introduced.
- Wave 6–7: martyrdom enemies.
- Wave 8–9: martyrdom + exploding-dogs combo.
- Wave 9: first juggernaut.
- Wave 12: riot-shield juggernaut pairs with a regular juggernaut.
- Wave 13: chemical/gas enemies.
- Wave 18–20: 3-type combos (chemical + martyrdom + dog_splode
  simultaneously).
- Wave 19: explosive-juggernaut pairs.

## Real "boss wave" system confirmed

`_id_3F90()`/`_id_3F8F()` (`1571.gsc` lines 1043–1068): a wave counts as
a boss wave if the CSV's juggernaut or chopper columns are defined for
it, firing `level notify("boss_spawning", level._id_17F6)`. Chopper waves
specifically escalate 1→2→3 aircraft at waves 6, 15, and 21 (0-indexed
5, 14, 20) — a real, deliberate escalation pattern, not random.

## Open questions, not yet resolved

- **What happens past wave 21** (the last authored row)? No modulo/clamp
  on `level._id_17F6` was found in `1571.gsc` itself for wave numbers
  beyond the 21 rows — unclear whether it loops, clamps to wave 21's
  values, or extrapolates. Would need tracing
  `_id_061C::_id_3E04`–`_id_3E09`'s own bodies (a different script, not
  yet located/read) to resolve.
- **Difficulty-preset interaction** — whether a separate Survival
  difficulty selector (distinct from Campaign's) scales this same table
  by a multiplier, or the table above already bakes in one specific
  difficulty, is not confirmed either way.
- **Unexplored sub-table**: a `1000–1014`-id block in the same CSV
  (`weapon_1-3`/`grenade_1-2`/`equipment_1-3`/`airsupport_1-3`/
  `perk_1-3`/`armor_1`) appears to be a separate default-loadout
  definition — not traced, possibly bot/ally loadouts or an unused
  legacy structure. Its consumer function was never identified.

## Cross-references

- `known_issues.md` issue #5 — Survival ready-up (shortens prep timer,
  doesn't skip a wave).
- `killstreak_reference.md` — the buy-station economy this wave-gate
  system runs alongside (`sp/survival_armories.csv`, a sibling data
  table, same `tablelookup` mechanism).
