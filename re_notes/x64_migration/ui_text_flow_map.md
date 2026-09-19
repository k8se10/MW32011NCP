# x64 UI / text flow map — where every on-screen string comes from (skeleton, 2026-09-19)

**Why this exists.** One line of text (Survival's "Press F5 to ready up: NN") cost
over a dozen investigation rounds because the x64 engine draws text through
several independent mechanisms, and this project had only ever mapped one
(the `FUN_14029a2b0` funnel). x86 had a single monolithic draw function
(`FUN_00690c80`); x64 does not. This document is organised by **where text
originates**, not by function address, so any future glyph / prompt / rebuild
feature is a lookup, not a new investigation. It supersedes nothing —
`ui_draw_pipeline_map.md` (draw-side numbered-HUD-element table) and
`drawtext_hook_x64.md` (the hook trail) remain the detailed references for
mechanisms 1-2.

**Confidence key**: **CONFIRMED** = read off a real decompile or live log.
**INFERRED** = reasoned from shape, not proven. **UNKNOWN** = not yet found.
All addresses are Ghidra-space for the current retail x64 build — RE reference
only; shipped code resolves via signature scan / RIP-relative from a known
anchor, never hardcoded.

## Rules for working in this area (learned the hard way, 2026-09-19)

1. **No probes on hot draw paths.** Log-and-call-through hooks and per-call
   stack sampling on text/quad draw functions caused visible UI flicker
   (user-confirmed). Use **read-only scans triggered by rare events** (the
   `VM_Notify` Survival events) instead — they cost nothing per frame.
2. **Absence from the text hook is data.** An element that never flickers under
   our draw-path probes does not pass through any path we hook.
3. **Follow the state, not the function.** Several mechanisms are
   server-side state -> snapshot -> client draw; scanning code for offsets never
   finds the draw side. Read the state live and work forward.
4. Prefer live reads via our own proven tools (section 7) over more blind
   decompile chasing.

## 1. The mechanisms

| # | Mechanism | Origin of the text | Transport | Draw entry | Covered by our text hook? | Status |
|---|---|---|---|---|---|---|
| M1 | **Menu itemDef text** (menu screens, most of Survival's HUD: Wave / Kill Streak / `$` values / stats screen / "Weapon Armory Enabled!") | menu files (`.menu` itemDefs), often dvar/expression driven | itemDef fields; dvar reads | `FUN_14029d170` (UI root) -> `FUN_1402abb70` -> `FUN_1402a7660` (itemDef list painter, INFERRED) -> `FUN_1402a9950` -> `FUN_1402b1090` -> `FUN_14029a2b0`; window/border paint `FUN_1402b0a70` | **Yes** (live: stats screen, "Weapon Armory Enabled!", "Purchase and upgrade weapons.", "Reload" all seen) | CONFIRMED |
| M2 | **Numbered HUD-element dispatcher** (ammo, compass, Mantle/Pickup/Hold-Breath/Reload hints, health, sprint meter, objectives, ...) | native C++ per-element handlers; localized `PLATFORM_*` / `WEAPON_*` templates | per-element gate globals (named-element registry, `ui_draw_pipeline_map.md` §3.5) | `FUN_140052220` case table -> ... -> `FUN_14029a2b0` | **Yes** | CONFIRMED (full case table in `ui_draw_pipeline_map.md`) |
| M3 | **Use-hint** ("Hold F to use Weapon Armory", other interact prompts) | GSC `sethintstring` (id `0x80C7`) | writes `client+0x14f8`; resolver `FUN_140176080` -> use-target state `+0x1b4`/`0x1b8`/`0x1bc` (type / string index / target entity, `0x7ff`=none) -> snapshot | **UNKNOWN** (client side) | **No** (never seen by text hook) | server half CONFIRMED, draw half UNKNOWN |
| M4 | **Script hudelems** (`newHudElem` + `settext` / `setvalue` / `setclock` / label) — **Survival ready-up prompt lives here** | GSC | server hudelem array `DAT_140f4d080` (stride `0xAC`) -> snapshot copy -> client | **UNKNOWN** (client side) | **No** | server half CONFIRMED, draw half UNKNOWN |
| M5 | **Subtitles / name tags / killfeed rows** | `video/subtitles.csv`, live entity names | `FUN_1402a9520` (label builder), `FUN_1402a9dd0` (subtitle renderer) | `FUN_14029a2b0` or `FUN_14029a610` (icon variant) | Partly (via `14029a2b0`); `14029a610` never fired for gameplay | CONFIRMED (see `ui_draw_pipeline_map.md` §1) |
| M6 | **"Game message" / death-quote captions** | GSC / native | wrappers `FUN_1402afa60` / `FUN_1402afb10` / `FUN_140299b40` (custom register convention) | reaches `14029a2b0` per earlier trace | Yes (INFERRED) | partly mapped |

## 2. Script hudelem structure (M4) — CONFIRMED from `settext`/`setvalue` decompiles + live raw dumps

Server array base `DAT_140f4d080` (`0x140f4d080`), **stride `0x2b` dwords
(`0xAC` bytes)**, ~1024 slots. Located at runtime from `settext`'s own
`LEA` (+0x24) — no hardcoded address. Field map (dword index; INFERRED unless noted):

| dword | Meaning | Evidence |
|---|---|---|
| `[0]` | **type**: 1 = text, 2 = value | CONFIRMED (`settext` sets 1, `setvalue` sets 2) |
| `[1]`, `[2]` | x, y offset (floats) | idx6 = -72.0/-55.0; countdown = -202.0/-100.0 |
| `[4]` | entity / owner (`0x7ff` = none) | matches engine's "no entity" sentinel |
| `[5]` | scale (float) | 1.0 label, 0.75 countdown |
| `[9]`, `[10]`, `[11]` | alignment / font / misc (`7`, `9` vs `1`, `0x32`) | unresolved |
| `[12]` | colour RGB | `0xFFFFFF` |
| `[16]` | **label slot** (string reference; configstring index = slot + `0xb2`) | **CONFIRMED live**: idx6 slot `0x49` -> `SPECIAL_OPS_TIME` (the "Time:" element); prompt value element slot `0x36` -> **`SO_SURVIVAL_READY_UP`** |
| `[0x20]` | **value** (float) for type 2 | countdown 30.0 -> 24.0 -> 17.0 across scans |
| `[0x21]` | text slot for type 1 (`settext` string) | 0 for every element seen |
| `[0x22]`, `[0x23]` | `2.0f`, `0x999999` | shared by both prompt elements |
| `[0x29]` | flags (`0x7` on both prompt elements) | distinct signature vs. other elements |

**Ready-up prompt = ONE value hudelem** (CONFIRMED live, 2026-09-19): the type-2
element that appears ~5 s after `wave_ended`, label `SO_SURVIVAL_READY_UP`
(the localized "Press F5 to ready up: &&1"), value = the countdown seconds.
(The persistent idx-6 element earlier assumed to be its text label is a
different hudelem, label `SPECIAL_OPS_TIME` — the timer readout.) `survival_player_ready` fires *after* the player readies (user
confirmed) — it is an end-of-prompt/control signal, not a "prompt visible"
signal.

## 3. String plumbing (CONFIRMED unless marked)

- **Interned string pool** (the GSC string table): entry = `tableBase + (id << 4)`,
  refcount at +0, text at +4; `tableBase` is behind pointer variable
  `DAT_14201ff08`. Read via `TryResolveGscInternedString`. Live-confirmed.
- **Configstring table**: `word DAT_1425353aa[index]` (`FUN_14026ad10`) — each
  entry is an interned-string ID.
- **Hudelem strings**: registered by `FUN_14016b5d0` at configstring index
  `slot + 0xb2` (2000 slots, refcount array `DAT_1411499e0`); the hudelem field
  stores the slot. The `+0xb2` mapping also applies to the *label* field
  `[16]` — CONFIRMED live (raw-slot lookup resolves nothing).
- `FUN_140021130` / `FUN_140021140` are the **weapon-name** getter (weapon
  index -> name with attachment suffixes), *not* a general string getter.

## 4. GSC builtin method table (the bridge from script to native)

Dispatch site inside `VM_Execute` (`FUN_14025e950`, opcodes `0x8b`-`0x91`):
`CALL [base + (methodId-0x8000)*8 + 0x201e670]`; table is zeroed at init
(`FUN_1402524d0`) and filled at runtime. Resolved live (Ghidra addresses):

| Method | id | Native impl |
|---|---|---|
| `sethintstring` | `0x80C7` | `0x14014DB50` |
| `setcursorhint` | `0x80C6` | `0x14014DA50` |
| `forceusehinton` / `off` | `0x80C8` / `0x80C9` | `0x14014DC30` / `0x14014DCA0` |
| `settext` | `0x80B6` | `0x14012F5F0` |
| `setshader` | `0x80B8` | `0x14012F6B0` |
| `settenthstimer` | `0x80BE` | `0x14012FAC0` |
| `setclock` | `0x80C1` | `0x14012FD10` |
| `setvalue` | `0x80C3` | `0x14012FD50` |

The full method-ID list is gsc-tool's `iw5_pc_meth.cpp` (`0x8000`-`0x830C`).
Any GSC-driven UI behaviour can be traced by resolving its builtin here.

## 5. Negative results (don't repeat)

- Sibling text-draw functions `FUN_14029a610`, `FUN_14029a4d0`, `FUN_1402b1090`:
  probed live in real Survival waves; none carry the ready-up / buy-station
  prompts (and hooking them flickers the UI).
- Menu itemDef paint `FUN_1402a9950`: pure layout, already funnels into the
  hooked primitive — not where the prompts moved.
- Cases `0x53`/`0x54` of `FUN_140052220`: player-name composer, not hints.
- Constant-offset scans for `+0x1b4`/`0x1b8`/`0x1bc` and `+0x14f8`: too noisy;
  the real consumers found are server-side use-target logic.
- `VM_Notify` caller address: generic interpreter bookkeeping.
- No `cos`/`sin` imports; x86's animated-icon rotation entry has no x64 twin.

## 6. Open questions (ordered)

1. ~~Resolve the prompt's label text~~ **DONE 2026-09-19**: `SO_SURVIVAL_READY_UP`.
   New capability: a cheap, read-only "ready-up prompt is showing" signal +
   live countdown value straight from the server hudelem array (no draw hook).
2. **Find the client-side draw of script hudelems.** Server array is read-only
   scannable; the client copy's base/layout is unknown. Candidate approach:
   read our own known hudelem (`countdown`) values live and search client-side
   memory for the same float pattern once per intermission (event-triggered,
   read-only).
3. **Find the client-side draw of the use-hint (M3).** Same technique: the
   resolved hint string index is known server-side; locate its client copy.
4. Reclassify `FUN_1402a7660` in `ui_draw_pipeline_map.md` (itemDef list
   painter, not "entity/name-tag compositor").
5. Decide the product answer once M3/M4 draw paths are known: suppress-and-
   replace (needs the draw hook point) vs. a rebuilt overlay drawn while the
   native one is hidden.
6. Map the M6 wrappers (`FUN_1402afa60`/`afb10`/`140299b40`).

## 7. Tool inventory

In `proxy_d3d9/src/analog_input_hooks_x64.cpp`:
- `Hook_VmNotify` — resolves every GSC notify to text; state accessors
  `IsSurvivalPlayerReadyConfirmedX64` / `IsSurvivalAllReadyConfirmedX64` /
  `IsArmoryMenuOpenX64`.
- `TryResolveGscInternedString` — string-pool reader.
- `TryResolveAndLogGscBuiltinMethod` — live GSC builtin -> native address.
- `ScanHudElems` — read-only live hudelem scan (event-triggered, no per-frame
  cost), incl. raw dump and label resolution.
- (TEMP, disabled) draw-sibling probes, quad-draw stack sampler.

Ghidra scripts (`re_notes/ghidra_scripts/`): `DecompileAt`, `DumpDisasm`,
`FindCallers`, `FindConstantRefs` (bare hex, no `0x`), `FindLeaRefsToAddr`,
`CreateFuncAndDumpSig`, `DumpRawQwords` (start/end, not count).

## 8. Cross-references

`ui_draw_pipeline_map.md`, `drawtext_hook_x64.md`,
`gsc_vm_native_functions_x64.md`, `../known_issues_x64.md` issue #1 (rounds
2026-09-16 .. 2026-09-19), `../x64_feature_parity_audit.md`.
