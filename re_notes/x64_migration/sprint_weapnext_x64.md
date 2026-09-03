# Sprint and weapnext — x64 relocation (2026-09-03)

Continuation of the x64 migration RE work (issue #111, `re_notes/x64_migration/README.md`).
Same methodology as the rest of this pass: `RawStringScan.java` → `DecompileAt.java` →
`FindGlobalRefs.java`, run via `analyzeHeadless.bat -process iw5sp.exe -noanalysis`
against the existing `re_notes/ghidra_project_x64/iw5sp_x64_proj.gpr` — not re-imported,
not modified structurally.

**Standing reminder, per the locked 2026-09-03 signature-scanning policy (`CLAUDE.md`
§5/§10.3): every address below is a coordinate against THIS specific x64 binary build,
found for today's RE work — not a value to hardcode into shipped hook code.** When this
gets implemented, each of these needs a real byte-pattern signature resolved once at
startup and cached, built from the surrounding instruction shape, not the literal
address recorded here.

## Sprint — high-confidence static match, NOT live-verified

x86 precedent (`CLAUDE.md`'s "Sprint stamina/cooldown" section, `re_notes/iw5sp.md`):
a real `pm_flags` bit forced every tick via a Pmove-entry hook, with `player_sprintUnlimited`
(a dvar, confirmed live-set only by specific mission scripts) bypassing the project's own
timer entirely when set, and `FUN_00643870` as the real `player_sprintSpeedScale`
consumer with no timer logic of its own.

**Anchor**: `player_sprintUnlimited` (already confirmed present identically in both
x86/x64 binaries earlier this session) found once, as `DATA` inside the giant dvar-
registration function `FUN_14000dba0` (the SAME function already documented in
`README.md` section 1a as the x64 equivalent of the renderer/HUD dvar-registration
batch — this is a second, separate registration call inside that same function, not a
new function).

**Real x64 storage globals** (decompiled `FUN_14000dba0`):

| dvar | x86 global | x64 global |
|---|---|---|
| `player_sprintSpeedScale` | (not previously recorded) | `DAT_1404e7f90` |
| `player_sprintUnlimited` | (not previously recorded) | `DAT_1404e8208` |

**Cross-referencing these globals** (`FindGlobalRefs.java`) found 5 real consumer
functions beyond the registration site itself:

- **`FUN_140014a80`** — the real per-tick sprint state machine. Reads a flags byte at
  `param_1+0xc` (the same struct offset the sprint-active bit is checked at in every
  other consumer below — a real, cross-validated `pm_flags`-equivalent field, not a
  guess) and a real sprint-duration/depleted-flag block at `lVar3+0x1cc`/`+0x1d0`/
  `+0x1d4`/`+0x1d8`/`+0x1dc` (start time, duration, cooldown bookkeeping — matches the
  x86 project's own documented "4 seconds continuous sprint, 2-second cooldown" design
  shape closely, though exact ms values weren't independently re-derived this pass).
  Bit `0x4000` in the flags field gates "is currently sprinting" consistently across
  every function below — real, repeated confirmation, not a one-off read.
  **`player_sprintUnlimited`'s real bypass logic is intact and unchanged**:
  `*(char*)(DAT_1404e8208 + 0x10) == '\0'` (dvar false) is required, alongside a
  separate `DAT_14252b620`-gated check, before the duration/depletion math even runs —
  i.e. sprinting stays literally unlimited when the dvar is set, same behavior as x86.
- **`FUN_140012e20`** / **`FUN_140012eb0`** — both real sprint-speed-scale accessors,
  same `player_sprintUnlimited` bypass check repeated identically in both. Likely the
  x64 equivalent of `FUN_00643870` (or its near neighbors) — the "how fast is the
  player currently allowed to sprint" query, not itself a duration timer.
- **`FUN_140011d60`** / **`FUN_1400124c0`** — real per-frame movement/animation
  consumers of `player_sprintSpeedScale` specifically (view-model bob / animation
  blending shape, not the core duration/flag logic) — lower priority for a controller
  hook, noted for completeness.

**Not yet done**: the actual Pmove-entry HOOK POINT (the function this project's own
`InjectControllerSprintPmFlags`/`ReassertSprintPmFlags`-equivalent would need to hook to
force the sprint bit, per-tick, the way the x86 hook does) hasn't been separately
pinned down — `FUN_140014a80` is the strongest candidate given it's the one function
that actually READS and interprets the flags bit as a real state machine, but which
function actually WRITES `param_1+0xc`'s bit `0x4000` in the first place (the analog of
where a controller hook would need to intervene) is still unlocated. Real next step for
whoever picks this up: `FindDataWriters.java`-style search for writers to that flags
field, scoped to the `+0xc` offset pattern, not just readers.

## weapnext — anchor found, same table as buttons, NOT live-verified

x86 precedent: resolved via live-reading `FUN_00541020`'s real raw-keycode dispatch
table (`DAT_00a98e4c`) rather than string-to-function correlation, since `weapnext` (like
`toggleprone`/`togglemenu`) is a one-shot command with no `kbutton_t` of its own at all.

**Real x64 anchor**: `RawStringScan.java` on `"weapnext"` found exactly one reference —
`DATA @ 1404c1a80`, immediately after `"togglemenu"` (`1404c1a78`) at the tail end of
the same 32-entry bind-name table `README.md` section 1d already fully mapped (base
`1404c1870`, 8-byte stride). Computed table index: `(1404c1a80 - 1404c1870) / 8 = 66`.
A singleton entry (no paired `-weapnext`), consistent with `weapnext` never having a
release-edge the way held binds do — same shape as `togglemenu`'s own singleton entry
right before it.

**No direct code reference to the `weapnext` string was found beyond the table
itself** — exactly the same shape as the x86 precedent (no string-to-function
correlation exists; the real dispatch goes through a raw-keycode/index table instead).
This is consistent with, not contradictory to, the x86 finding — it confirms the same
"one-shot commands don't get a clean string xref, use the index/dispatch-table
technique instead" lesson applies unchanged on x64.

**Not yet done**: the actual x64 equivalent of `FUN_00541020`'s raw-keycode dispatch
table and its real consumer (landing on the x64 equivalent of `FUN_004a5f70`/
`FUN_0057a670`, the real weapon-cycling function) hasn't been traced this pass — index
66 in the bind-name table is a real, useful anchor for that future work, not a
complete answer on its own. Given this session's separate finding of `FUN_14007eaf0`
(the index-based `KeyDown`/`KeyUp` setter for HELD binds, `README.md` section 1e),
it's plausible but NOT confirmed that a parallel, similarly-simple index-based
dispatcher exists for one-shot commands too — worth checking directly before assuming
the x86 raw-keycode-table approach is still the only path in the x64 build.
