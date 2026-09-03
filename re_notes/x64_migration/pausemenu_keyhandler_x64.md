# Pause menu / key-event-handler cluster — x64 RE pass (2026-09-03)

**Status: Partial, honest results.** Found the real x64 `cl_paused` storage
global and its registration site, plus several real functions that touch it —
but did NOT conclusively pin down x64 equivalents of the specific small,
dedicated `FUN_004d6620` (`OpenPauseMenu`)/`FUN_004396d0` (`SetMenuState`)
functions from the x86 project, nor `FUN_00541020` (the raw key-event
handler) or `FUN_004d9850`/`FUN_00547980` (`ForwardKeyToMenu`/
`GetTopmostActiveMenu`). This is real, useful groundwork, not the finished
chain — say so plainly rather than overclaiming a match.

**Signature-scan reminder** (per CLAUDE.md §5/§10.3, locked 2026-09-03):
every address below is a coordinate against THIS specific x64 binary for
today's RE work, not a value to hardcode into shipped code. A real
byte-pattern signature, resolved once at startup and cached, still needs to
be built from each one's surrounding instruction shape before any of this is
usable in hook code.

## Confirmed: real x64 `cl_paused` storage global and registration

`RawStringScan.java` on `"cl_paused"` found the string at `1403ee240`
(`.rdata`) with 16 code references across 11 functions (this cross-references
cleanly with the movement-pipeline work's own `input_refs_x64.txt`/
`global_refs_x64.txt`/`kbutton_table_x64.txt`, already on disk before this
pass started).

Decompiling the registration function found the real storage global:

```c
// FUN_14023d890 -- a large dvar-registration batch, also registers
// com_maxfps/timescale/fixedtime/com_maxFrameTime/sv_running nearby
// (interesting incidental cross-reference to the already-CLOSED x86 issue
// #90 60fps-tick investigation -- not otherwise explored this pass)
DAT_141efb7a8 = FUN_1402c46d0("cl_paused", 0, 0, 2, uVar3, "Pause the client");
```

So **`DAT_141efb7a8`** is the real x64 `cl_paused` cvar storage global
(distinct from `1403ee240`, which is just the string literal's own address).

## Real functions that touch `cl_paused` (from `FindCallers.java` on the
`SetDvarBool`-equivalent `FUN_1402c5b30`, 10 real callers)

- **`FUN_14029dfd0`** — a genuine UI menu-action-string dispatcher: string-
  compares a menu action name against `"clearError"`, `"LoadSaveGames"`,
  `"Loadgame"`, `"Savegame"`, `"forcesave"`, `"DelSavegame"`,
  `"SavegameSort"`, `"playerstart"`, `"Controls"`, `"Leave"`,
  `"closeingame"`, `"update"`. Two real, direct `cl_paused` writes inside:
  `"Controls"` action sets it to a computed value (likely 1, not fully
  traced), `"closeingame"` action clears it to 0. **This is the closest
  candidate found to "the code that resumes the game via a named UI action,"
  but it's structured as a broad script-action dispatcher, not a small
  focused `SetMenuState(mode)`-style function** — may be this engine's real
  x64 equivalent of a `UI_RunMenuScript`-class function rather than a direct
  match for `FUN_004396d0`. Not confirmed either way.
- **`FUN_14023c970`** — a level-load/reinit function (calls
  `__dyn_tls_on_demand_init`, walks several other subsystem-init calls,
  checks a `DAT_141efbe88` state variable for a value of 1 or 3). Clears
  `cl_paused` as one step of level (re)initialization, not a dedicated
  pause-toggle function.
- **`FUN_1400804b0`** — end-game/disconnect flow (`"COOP_CLIENT_END_GAME"`/
  `"COOP_HOST_END_GAME"`/`"EXE_DISCONNECTED"` strings), touches
  `cl_paused` indirectly through a nested call, not directly in its own body.
- **`FUN_140264160`** — large (>150 local variables/1KB+ stack buffers),
  not fully characterized this pass — time-boxed out. Real candidate worth
  a dedicated follow-up pass, not ruled out.
- **`FUN_1402a0090`** — UI language-change notifier, unrelated (touches
  `cl_paused`'s writer function only incidentally via a shared low-level
  dvar-set call, not a real pause-flow function).
- The remaining callers (`FUN_14029baa0`, `FUN_14029cc50`, `FUN_14029ccb0`,
  `FUN_14029fa20`, `FUN_14029f3f0`) were already characterized in this
  session's earlier pass (`re_notes/x64_migration/decomp_candidates_x64.txt`)
  as HUD/UI hint-drawing functions gated on whether the game is currently
  paused — readers, not the real pause-state-change logic.

## Not found this pass, real open work

- `FUN_00541020`'s x64 equivalent (raw key-event handler, ESC/backtick
  special-casing, D-pad actionslot's raw-keycode dispatch table). No
  reliable string anchor found — ESC's `0x1b` and the actionslot dispatch
  table are pure numeric/data constructs, not directly string-searchable the
  way the movement-pipeline cvars were. Real next step: trace from
  `FUN_00410ad0`/`FUN_0044ec40`'s (D-pad actionslot) x64 equivalents
  backward, since those at least have a stronger anchor via the
  `+actionslot 1`-`4` bind-name-table entries already confirmed in
  `kbutton_table_x64.txt`.
- `FUN_004d9850` (`ForwardKeyToMenu`)/`FUN_00547980`
  (`GetTopmostActiveMenu`)/the `kMenuStackCtx`/`kMenuStackDepthOffset`
  per-player context struct — not attempted this pass, no string anchor
  identified. These are pure-data/context-pointer mechanisms in the x86
  version too (fixed context address `0x01c00458`+offset `0xA7C`), so
  finding the x64 equivalent will likely need either (a) tracing from
  something already found (e.g. `FUN_14007e1e0`'s per-frame orchestration,
  already confirmed this session, may share a call path) or (b) live
  debugging once an injectable x64 build exists, matching how much of the
  ORIGINAL x86 discovery of this exact cluster also leaned on live
  correlation rather than pure statics.
- `FUN_004d6620`/`FUN_004396d0` — see above, best current candidate is
  `FUN_14029dfd0`'s `"closeingame"`/`"Controls"` branches, not confirmed.

## Real process note for future parallel Ghidra work

Hit a transient Ghidra headless project-lock contention once mid-pass
(`FindCallers.java` failed outright with a project-open exception) — almost
certainly from another concurrent fork/session running scripts against the
same `iw5sp_x64_proj` project at the same moment. Retried after an 8-second
wait and it succeeded immediately. **Real lesson for any future parallel RE
work against a shared Ghidra project: build in a short retry/backoff loop
around `analyzeHeadless.bat` invocations, don't assume a single failed
attempt means the target itself is broken.**
