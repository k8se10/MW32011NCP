# iw5mp.exe x64 Reverse-Engineering Notes

**Binary:** `iw5mp.exe` (Call of Duty: Modern Warfare 3, Multiplayer), x64 build,
recompiled 2026-09-03 (see `re_notes/known_issues_x64.md` issue #1 and
`re_notes/x64_migration/README.md` for the full recompile event record).
Working copy analyzed: `re_notes/x64_migration/binaries/iw5mp.exe`. Ghidra
project: `re_notes/ghidra_project_x64/iw5mp_x64_proj` (imported + fully
auto-analyzed 2026-09-05, program `/iw5mp.exe`).

**This is a NEW, dedicated tracker for MP's x64 line**, mirroring
`known_issues_x64.md`'s own relationship to the pre-x64 tracker (per
CLAUDE.md's 2026-09-03 "known issues should probably restart fresh" entry).
Numbering/section structure restarts fresh here; it does **not** replace or
renumber `re_notes/iw5mp.md` (the x86 MP tracker, ~55% signature-scanning-ready
as of its own Session 6), which stays the reference map for x86 function
roles and structure — every finding below is cross-checked against it, but
**no x86 MP address carries over**, per the standing project policy (the
recompile invalidated every x86 address project-wide, confirmed independently
for `iw5sp.exe` already).

**Method: static-only, per this project's locked "static RE first, live/
injection work later" MP ordering.** Everything below came from Ghidra
headless (`analyzeHeadless.bat ... -process iw5mp.exe -readOnly -noanalysis`)
using this repo's own existing `re_notes/ghidra_scripts/` toolkit
(`FindExactStrings.java`, `DecompileFuncs.java`, `FindCallers.java`,
`FindGlobalRefs.java`). No live process attach, no debugger, no hook/injection
code written or built. This session's own raw script output is saved under
`re_notes/x64_migration/mp_*.txt` for anyone who wants to re-derive a claim
below without re-running Ghidra.

---

## Status: Session 6 complete. Both of section 4-6's own untried next steps attempted this round. Type-1 killstreak-slot: a real, cleanly-bounded, exactly-6-entry lethal-equipment content table found (frag/semtex/knife/betty/claymore/C4) sitting in the same real Create-a-Class content-name region as the already-confirmed killstreak tables, thematically and numerically consistent with the equipment-cycling hypothesis -- upgraded from "reasonable hypothesis" to "reasonable hypothesis with a real, specific table candidate," explicitly NOT the same evidentiary strength as type-2's own runtime-array-bound match (no pointer-chase from type-1's actual runtime array to this table was found). `DAT_140e1dbc4` ambiguity: `FUN_1400cfef0`'s full decompile didn't directly touch it (a correction to Session 5's own framing), but its call sites let the byte be precisely located at offset 0xC4 within the confirmed per-player kbutton-region struct, immediately after the digital-hold-duration field cluster -- real support for the "digital-look-active" reading, but the ADS-flag comparison itself remains unresolved; no further concrete untried next step is currently recorded for this specific thread. Sessions 1-5's own findings all stand unchanged.

This session started from string anchors (bind-name literals: `+attack`,
`+sprint`, `+holdbreath`, `+frag`, `+gostand`, etc.) rather than trying to
reuse any x86 MP address, and worked outward via caller-tracing
(`FindCallers.java`) until it reached the real per-frame input-event queue at
the top and the real per-player `kbutton_t` array at the bottom. Every
function below was independently found and full-decompiled this session; none
were assumed from x86 MP's notes. Confidence levels are marked per finding —
several are "confirmed" via direct structural/behavioral evidence (not
heuristic address-locality guesses), matching this project's own "never trust
a bind-index as a case number without independent confirmation" standard
(the issue #3 lesson, carried over explicitly from the x86-era `CLAUDE.md`).

**Bottom line for hook-target readiness**: the mechanism (a real per-player,
per-raw-keycode resolved-case-ID table feeding a 89-case switch that calls a
real, x64-native `kbutton_t` KeyDown/KeyUp pair with clean explicit pointer
args) is now understood and mapped as thoroughly as x86 MP's own Session 5/6
ever got it -- arguably more so, since the x64 decompiler resolved real
`DAT_` global addresses for nearly every case directly, where x86 needed raw
disassembly to manually recover only 5 of 89 case targets. **What's still
missing, exactly like x86 MP's own final open item, is tying specific case
numbers to specific real bind NAMES** (which case is Fire, which is ADS,
which is Sprint) -- several strong candidates are identified below by
structural pattern-matching against x86 MP's own confirmed bit/case
identities, but none should be treated as hook-ready without either a
genuine string/name anchor or live verification, per this project's own
locked issue #3 policy.

---

## 1. Bind-name table and lookup/resolver functions -- CONFIRMED

### Bind-name table

Real bind-name strings (`+attack`, `+frag`, `+holdbreath`, `+gostand`,
`+smoke`, `+breath_sprint`, `+usereload`, `+actionslot 1/2/3`, `+reload`,
`+sprint`, `+scores`) all resolve to one contiguous string-pointer table
starting around **`0x14053abe0`** (each entry referenced once from `DATA`,
consistent with a flat `char*[]` registration list -- same shape as x86 MP's
own `0x008aa3bc` table). Raw dump: `re_notes/x64_migration/mp_bindstrings_x64.txt`.

### `FUN_1400b5cf0(char* bindName)` -- bind-name -> index resolver

```c
uint FUN_1400b5cf0(undefined8 param_1) {
  ppuVar3 = &PTR_DAT_14053abe0;
  uVar2 = 0;
  do {
    iVar1 = FUN_14032bb70(param_1, *ppuVar3);   // strcmp-equivalent
    if (iVar1 == 0) return uVar2;
    uVar2 = uVar2 + 1;
    ppuVar3 = ppuVar3 + 1;
  } while (uVar2 < 0x5b);   // 91 decimal
  return 0;
}
```

**Confirmed** (direct decompile, not inferred): linear scan, bound `0x5b`
(91 decimal) entries, table base `&PTR_DAT_14053abe0` -- **exact match to
x86 MP's `FUN_0048c1c0`** (same 91-entry bound, same table-adjacent-to-string-
data shape). `FUN_14032bb70(a, b)` is the underlying string-compare primitive
used project-wide (also seen gating dev-command checks and the giant menu
command-string dispatcher in section 4).

### `FUN_1400b4800(playerIdx, char* bindName, char* outBuf)` -- Key_KeynumToBindString equivalent

Scans `&DAT_1407f9320 + player*0x34b` (256-entry, 3-dword-stride,
per-player table) for up to 2 keycodes matching a resolved bind index, then
formats the key name(s) into `outBuf`/`outBuf+0x80` via `FUN_1400dd4c0`, or
writes "KEY_UNBOUND" if none found. **Confirmed** exact structural and
**exact stride** match to x86 MP's `FUN_0048c220` (`&DAT_00b3c768 +
param*0x34b`) -- the `0x34b`-dword per-player stride survived the recompile
byte-for-byte even though the base address changed completely. `FUN_1400b5d60`
is a near-duplicate that returns raw indices instead of formatted strings.

### `FUN_1402f13d0` -- Controls-menu key-REBINDING state machine

Gated on toggle flag `DAT_146a69f70` ("waiting for a key to assign" mode,
exact same shape as x86 MP's `FUN_005a3e10`), calls `FUN_1400b5cf0` to
resolve the target bind, then `FUN_1400b5fb0` to write the new key
assignment. **Confirmed UI/rebind-flow only, not gameplay-time.**

### `FUN_1402f0f80` -- alias-cluster resolver + dual-bind "KEY_OR" display (fused x86 `FUN_005a3960`+`FUN_005a3ac0`)

String-compares an incoming bind name against `"+sprint"`/`"+holdbreath"`/
`"+changezoom"`, then tries a short alias list in priority order:
- `"+holdbreath"` (implicit, first branch) -> `{"+melee_breath","+melee_zoom"}`
- `"+sprint"` -> `{"+breath_sprint","+sprint_zoom"}`
- `"+holdbreath"` (explicit strcmp branch) -> `{"+melee_breath","+breath_sprint"}`
- `"+changezoom"` -> `{"+melee_zoom","+sprint_zoom"}`

then formats `"%s %s %s"` with a `"KEY_OR"` separator for a dual-bind display.
**Exact match to x86 MP's Session 3 finding for `FUN_005a3960`+`FUN_005a3ac0`
combined into one function** -- same alias clusters, word for word. Confirmed
**UI Controls-menu display code, NOT a gameplay dispatcher** -- x64 reproduces
x86's own dead-end here, so no further time was spent on it this session
(matches x86 Session 3's already-closed conclusion).

---

## 2. HUD button-prompt / on-screen-hint rendering cluster -- identified, NOT gameplay input

`FUN_1400a0cf0` (a large `switch(param_11)` dispatcher, cases keyed by an
internal prompt-type ID, not a raw keycode) and its sub-cases
`FUN_14009c830`/`FUN_1400a0350` build and draw on-screen button-prompt icons
+ text (`"PLATFORM_MANTLE"`, `"PLATFORM_HOLD_BREATH"`, `"PLATFORM_CHANGE_ZOOM"`,
`"PLATFORM_FOLLOWNEXTPLAYER"`, `"PLATFORM_PICKUPHEALTH"`, etc.), resolving the
bound key via `FUN_1402fdcb0`/`FUN_14030eca0`/`FUN_14030cb80` (the same
Key_KeynumToBindString-adjacent family as section 1) and drawing via
`FUN_1402d81f0`/`FUN_140303bc0` with real screen-space float math. This is
the x64 MP analog of the HUD "&&N"-token prompt system CLAUDE.md documents
for SP (`FUN_00433a10`/`FUN_0061f6f0`). **Not gameplay input** -- flagged
here because it's directly relevant to this project's future controller-glyph
coverage work on MP (currently zero glyph work exists for MP), not because
it's a hook target now. `FUN_1400ca510`/`FUN_1400ca580` (small "screenshot"
dev-command guard checks) trace up into a single ~6800-line HUD text-render
function (`FUN_1402dc130`) -- confirmed to be the broader HUD/UI text
rendering engine, not investigated further this pass (too large, not gameplay
input).

---

## 3. Menu-navigation and command-string dispatch cluster -- CONFIRMED

### `FUN_1402f84a0(ctx, menuCtx, keycode, isDown)` -- real menu-navigation key router

Full decompile shows a genuine `switch(keycode)` over Enter/select
(`0xd`/`0xbf`/`0xca`), ESC (`0x1b`), D-pad/nav-adjacent keys
(`0x9a`/`0x9c`/`0xb7`/`0xce`), tab/switch (`200`/`0xc9`), and developer-only
console/screenshot toggles gated on the `"developer"` dvar (`0xb1` toggles a
dev flag, `0xb2` executes `"screenshot\n"` via `FUN_140262850`). **Confirmed
exact structural and functional match to x86 MP's `FUN_005ac2c0`** -- same
case shape, same developer-only screenshot escape hatch. This is the
still-most-directly-reusable target for this project's own unimplemented
full menu/UI controller-navigation work (per the architecture doc's item 4),
now with a real x64 address.

### `FUN_140262850(playerIdx, char* cmdString)` -- confirmed `Cbuf_AddText`-equivalent

Called everywhere a literal console command string needs to run
(`"screenshot\n"`, `"disconnect\n"`, `"vid_restart\n"`, `"startSingleplayer"`,
vote/chat commands -- see section 4). Real, confirmed x64 MP command-string
executor.

### `FUN_140309270(ctx, keycode, isDown)` -- confirmed `ForwardKeyToMenu`/unpause (x86 `FUN_00592820` analog)

```c
void FUN_140309270(undefined4 param_1, int param_2, int param_3) {
  ...
  if (menu-stack checks pass) {
    if (param_2==0x1b && param_3!=0 && no-popup-open) FUN_1402fa100(...);  // pop/close a menu
    if (FUN_1400b5e10(DAT_146b2df00, 0x10) != 0)         // <-- gate bit 0x10, exact x86 shape
      FUN_1402f84a0(&DAT_146b2df00, plVar3, param_2, param_3);   // forward key to menu
    ...
  }
  if (FUN_1400b5e10(DAT_146b2df00, 0x10) != 0) {
    FUN_1400b5f80(param_1, 0xffffffef);
    ...
    FUN_140326ac0("cl_paused", 0);    // <-- confirmed Cvar_Set("cl_paused", 0)
  }
}
```

`FUN_1400b5e10(ctx, 0x10)` is the exact x64 analog of x86 MP's
`FUN_0048c700(param_1, 0x10)` gate-bit check ("is a menu active"), and
`FUN_140326ac0` is the confirmed x64 MP `Cvar_Set` equivalent. **Confirmed**
via this exact structural match, not a guess.

### `FUN_14030cd40(ctx, cmdString)` -- giant UI command-string dispatcher

A ~1400-line function that string-compares an incoming UI command
(`"StartServer"`, `"Controls"`, `"closeingame"`, `"JoinServer"`,
`"simulateKeyPress"`, dozens more) and executes the matching menu action.
Confirmed as the general `.menu`-file button-action dispatcher (every
`onButton { exec ... }` style menu callback in this game's UI system funnels
through here). Notable finds inside it:
- `"Controls"` case: `Cvar_Set("cl_paused",1)`, opens `"setup_menu2"` --
  confirms the real Controls-menu open path.
- `"closeingame"` case: mirrors `FUN_140309270`'s own unpause sequence
  (`Cvar_Set("cl_paused",0)` + the same gate-bit clear) almost verbatim.
- `"simulateKeyPress"` case: reads a keycode argument and calls
  `FUN_140309270(param_1, keycode, 1)` then `FUN_140309270(param_1, keycode, 0)`
  -- i.e. the game's OWN menu system already has a "synthesize a keypress"
  primitive for scripted button macros. Not gameplay-relevant on its own, but
  worth knowing this pattern exists natively if a future menu-nav
  implementation ever needs to fake a key press through the menu layer
  specifically (distinct from this project's existing `PostMessageA`-based
  `SendSyntheticActivationClick`/Survival-ready-up techniques, which operate
  at the OS message level instead).
- Not gameplay input. Not investigated further beyond confirming its role.

---

## 4. THE key-event handler and gameplay-bind dispatch chain -- CONFIRMED END TO END

This is the session's main result. Full chain, every link independently
confirmed via decompile (not assumed):

```
FUN_140270030                              per-frame input-event QUEUE DRAIN
  (calls FUN_14034cf10 to pull queued       (Com_EventLoop-equivalent; confirmed:
   sysEvent-style entries, dispatches        event type 1 -> key, type 2 -> char,
   by event type)                            type 3 -> console-text/mouse-console)
    |
    v  event type 1 (raw key), calls FUN_1400b4960(0, keycode, ?, isDown)
FUN_1400b4960(playerIdx, keycode, isDown)   THE key-event handler (x86 FUN_0048d120 analog)
    |-- hardcodes tilde (0x60/0x7e) console toggle
    |-- writes isDown/refcount/case-ID into the per-player per-keycode table
    |     (&DAT_1407f9314 count / &DAT_1407f9318 + keycode*0xc + player*0xd2c
    |      table, 256 entries, 3-dword stride -- EXACT stride match to x86 MP's
    |      own confirmed `&DAT_00b3c760 + keycode*0xc + player*0xd2c` table)
    |-- hardcodes ESCAPE (0x1b) against connect-state DAT_140e1dddc + gate flags
    |-- calls FUN_1400c3290 (Killcam/Theater dispatcher -- see below), inert
    |     outside connect-state 0xb, exact x86 FUN_006ada70 analog
    |-- KEY UP:  reads table[+8] (resolved case ID); if odd, calls
    |            FUN_1400ce950(player, caseID+1, keycode)   <-- release dispatch
    |-- KEY DOWN, menu active (gate bit 0x10):  FUN_140309270 -> FUN_1402f84a0
    |-- KEY DOWN, console open (bit 0x20):      FUN_1400b67e0 (console char insert)
    |-- KEY DOWN, connect-state != 0 (i.e. actually connected to a match):
    |            reads table[+8] (resolved case ID), calls
    |            FUN_1400ce950(player, caseID, keycode)     <-- LIVE GAMEPLAY DISPATCH
    |-- KEY DOWN, connect-state == 0 (main menu, not connected) or chat-mode:
    |            falls through to FUN_1400b4dd0 (confirmed console/chat-box
    |            line editor -- Enter/Tab/history nav/backspace -- NOT gameplay)
    v
FUN_1400ce950(playerIdx, caseID, keycode)   THE real gameplay-bind dispatcher
                                              (x86 FUN_0048af00 analog -- CONFIRMED,
                                               not a heuristic guess: reached via
                                               direct, traced call sites on both the
                                               key-down and key-up edges, exactly
                                               the same evidence standard x86 MP's
                                               own Session 5 used)
```

### `FUN_1400ce950` -- the real dispatcher, full case table extracted this pass

Real `switch(param_2)` (param_2 = case/bind ID, cases observed 1 through at
least `0x59` = 89 decimal, matching x86 MP's own 89-case bound almost
exactly). Unlike x86 MP (which needed raw disassembly to hand-recover only 5
of 89 case targets across two sessions), **the x64 decompiler resolved real
named `DAT_` global addresses for nearly every case directly** -- a huge
practical win. Full text: `re_notes/x64_migration/mp_decomp_1400b5820_ce950.txt`.

**Confirmed structural pattern**: most cases come in odd(down)/even(up) pairs
calling `FUN_1400d0ea0(kbuttonPtr, keyId, downtime)` /
`FUN_1400d0ed0(kbuttonPtr, keyId, time)` against a per-player kbutton region
based around `0x140e1db00`-`0x140e1df70`, **confirmed per-player stride 600
decimal (0x258)** -- exact match to x86 MP's own confirmed `IMUL ESI,ESI,0x258`
stride. Case 9/10 drives **two kbuttons simultaneously**
(`&DAT_140e1dc2c` AND `&DAT_140e1dd08`), an exact structural match to x86 MP's
own confirmed dual-kbutton shape for `+toggleads_throw`.

### `FUN_1400d0ea0`/`FUN_1400d0ed0` -- CONFIRMED real x64 `kbutton_t` KeyDown/KeyUp

```c
// KeyDown
void FUN_1400d0ea0(int *kb, int keyId, int downtime) {
  if (keyId != kb[0] && keyId != kb[1]) {
    if (kb[0]==0) kb[0]=keyId; else { if (kb[1]!=0) return; kb[1]=keyId; }
    if ((char)kb[4] == '\0') { kb[2]=downtime; *(short*)(kb+4) = 0x101; }  // active=1, wasPressed=1
  }
}
// KeyUp
void FUN_1400d0ed0(int *kb, int keyId, int time) {
  if (kb[0]==keyId) { kb[0]=0; iVar1=kb[1]; }
  else { if (kb[1]!=keyId) return; iVar1=0; kb[1]=0; if (kb[0]!=0) return; }
  if (iVar1==0) {
    *(char*)(kb+4)=0;
    kb[3] += (time!=0) ? (time-kb[2]) : (DAT_1415e5920>>1);   // msec accumulation
  }
}
```

**This is the real, confirmed `kbutton_t` struct**: `{down[2], downtime, msec,
active:byte, wasPressed:byte}` -- byte-for-byte the same fields x86 (both SP
and MP) confirmed, with one genuine x64 simplification worth noting for future
hook-writing: **the pointer is now a clean, explicit first parameter**
(`int *kb`), not an implicit register-passed pointer the way x86's
`FUN_00489550`/`FUN_00489590` needed `in_EAX`. This matches the same "no
custom calling convention needed" simplification already documented for the
x64 SP dvar API and D-pad/kbutton-table cluster in `known_issues_x64.md`.

### Case -> kbutton-address table extracted this pass (from the decompile directly, not raw disasm)

All addresses are per-player base; actual runtime address is
`base + playerIndex*0x258`.

| case (down/up) | kbutton base(s) | shape | x86 MP cross-reference / confidence |
|---|---|---|---|
| 1/2 | `0x140e1dc18` | single | -- |
| 3/4 | `0x140e1dc7c` | single | -- |
| 5/6 | `0x140e1dc54` | single | -- |
| 7/8 | `0x140e1dc68` | single | -- |
| 9/10 | `0x140e1dc2c` **and** `0x140e1dd08` (driven together) | **dual kbutton** | Exact structural match to x86 MP's confirmed dual-kbutton case (9/10 in x86 too) -- x86 flagged this as a strong `+toggleads_throw`/ADS-grenade-context candidate by pattern alone, not confirmed by name. Same caveat applies here: **candidate, not confirmed.** |
| 0xb/0xc | `0x140e1dcb8` | single | -- |
| 0xd/0xe | `0x140e1dcf4` (always) + `0x140e1dbb4` (conditional, connect-state>5) | dual, conditional | On down: also zeroes `DAT_140e1df60` (a toggle flag, see 0x43/0x44 below). Medium-confidence candidate for **Sprint** (`+speed`) -- forcing an ADS-toggle flag off on sprint-start matches known "can't ADS while sprinting" behavior, and `"+speed"` is a real bind-name literal seen in section 2's HUD-prompt cluster. **Not independently re-checked this pass beyond the section-4-1 cross-check below** -- still candidate, not confirmed. |
| 0x1f/0x20 | `0x140e1dbf0` + `0x140e1dbc8` | dual, gated on connect-state>5 | -- |
| 0x21/0x22 | `0x140e1dc7c` (shared with case 3/4!) + `0x140e1dc40` | dual, one kbutton shared with another case | Real multi-source kbutton -- same "one kbutton, two independent bind commands" mechanism `FUN_1400d0ea0`'s two-source (`kb[0]`/`kb[1]`) design supports. |
| 0x23/0x24 | `0x140e1dc40` | single | -- |
| 0x25/0x26 .. 0x3f/0x40 | `0x140e1db00`..`0x140e1dccc` (14 more single-kbutton pairs, contiguous region) | single, uniform | Not individually investigated -- straightforward mechanical continuation of the same pattern, matching x86 MP's own "remaining cases are mechanical, not a new unknown" framing for its own leftover cases. |
| 0x41 (down only) | `0x140e1dc14` (direct byte flag, NOT a kbutton_t) | raw boolean set=1 | -- |
| 0x42 (up) | same byte, conditional clear + `LAB_1400cf55d`: `DAT_140e21454 = DAT_140e1dfc8 ^ DAT_1404e87e0` | raw boolean + XOR-toggle side effect | **Candidate for `+scores`** (Back/scoreboard-toggle bind) -- a raw hold-flag rather than a kbutton_t, matching the conceptual shape of a "hold to show scoreboard" bind rather than a continuous-movement bind. Case 0x51 (no matching even case) reaches the SAME `LAB_1400cf55d` XOR side effect directly, suggesting 0x51 is a second, one-shot-tap bind for the same underlying action (e.g. a keyboard Tab-equivalent alongside a controller hold). **Not confirmed by name.** |
| 0x43 (down) / 0x44 (up) | `0x140e1dcf4` (SAME kbutton as case 0xd!) | single, shared kbutton, down-edge also toggles `DAT_140e1df60` | **The strongest-supported non-command-string case in this whole table.** Four independent dispatch cases (`0xd`, `0x43`, and section 4-4's `0x56`/`0x57`) all touch `DAT_140e1df60` consistent with a real ADS toggle/cancel state, plus the section 4-1 movement-pipeline cross-check. Still not name-confirmed per issue #3, but the evidentiary base is now real and multi-sourced, not a single table-position guess. |
| 0x47/0x48 (down/up) | none (calls `FUN_140090c80`/`FUN_140090d20(player)` directly, no kbutton at all) | one-shot start/stop pair | **Decompiled Session 4** -- see section 4-5. Real state-machine shape (2-second cooldown, per-player activity-state getter/setter pair), no name anchor found. |
| 0x4b (down only, no matching up) | `FUN_1400d71b0(player)` | one-shot | **Decompiled Session 4** -- see section 4-5. Melee candidacy COMPLICATED, not strengthened: reveals dual Killcam-mode/live-gameplay behavior real melee wouldn't typically need. |
| 0x4c / 0x50 | `FUN_14007f5b0(player, 1)` / `FUN_14007f5b0(player, 0)` | boolean set/clear | **Decompiled Session 4 -- Hold Breath candidacy significantly upgraded.** See section 4-5: real ADS-gate + sniper-weapon-class-range check + per-weapon capability-table lookup, matching MW3's actual Hold Breath behavior precisely. |
| **0xf/0x10 .. 0x19/0x1a** (6 parameterized slots) | `FUN_14007c5b0(player, N)` / `FUN_14007c760(player, N)`, N=0..5 | **parameterized slot family, 6 slots** | **Name-anchored (Session 3, section 4-3): "type 2" slot handling resolves with high confidence to MW3's real `assaultStreaks` killstreak category** (exact 15-entry count match to a table a real loadout-validator function names explicitly), not just a structural shape. "Type 1"/"type 3" still unresolved to a category name. Directly relevant to this project's own stated MP motivation. |
| `"+chatmodepublic"`-adjacent: 0x4e | `FUN_140262850(player, "chatmodepublic\n")` | one-shot command string | **Confirmed by literal command string** -- real Cbuf_AddText call, not a kbutton. |
| 0x4f | `FUN_140262850(player, "chatmodeteam\n")` | one-shot command string | **Confirmed by literal command string.** |
| 0x58 | `FUN_140262850(player, "vote yes\n")` (gated on an active-vote check) | one-shot command string | **Confirmed by literal command string.** |
| 0x59 | `FUN_140262850(player, "vote no\n")` | one-shot command string | **Confirmed by literal command string.** |

**These four command-string cases (0x4e/0x4f/0x58/0x59) are the first
genuine, non-heuristic case-to-real-action confirmations found for x64 MP** --
real literal strings executed via a confirmed Cbuf_AddText call, not a
bind-table-index guess. They're chat/vote actions, not movement-critical, but
they prove this specific dispatcher CAN be tied to real names directly from
static analysis alone in at least some cases, which is a more optimistic
signal than x86 MP Session 5/6 ever had (x86's own notes flagged case-to-name
mapping as possibly gated on live verification with no static path at all --
this session shows that's not universally true for x64, even if most cases
still need it).

### 4-1. Case 0x43/0x44 (ADS candidate) -- independently cross-checked against the confirmed movement pipeline (Session 2)

Per this project's own issue #3 policy ("never trust a case number without
independent confirmation"), this is a genuine cross-check against a
SEPARATE piece of evidence (the movement/look pipeline found in section 6),
not just the case's position in the dispatch table. Full raw output:
`re_notes/x64_migration/mp_globalrefs_140e1df60.txt`,
`mp_decomp_ads_consumers.txt`.

**Method**: `FindGlobalRefs.java` against `DAT_140e1df60` alone (not the
kbutton addresses, which Session 1 already showed dead-end at the writer)
found **28 references across 15 functions** -- a genuinely high reference
count for a single byte flag, consistent with a widely-consulted piece of
gameplay state (not a rarely-touched field). Two of those 15 functions are
directly relevant:

- **`FUN_1400cfb60`** (the confirmed raw mouse-delta reader from section 6)
  is called as `FUN_1400cfb60(&DAT_140e1df60, ...)` -- **checked directly,
  not assumed**: its own body only ever reads/writes offsets `+0x3420`
  through `+0x3450` relative to that pointer, never byte 0 itself. **This
  does NOT corroborate the ADS hypothesis** -- `&DAT_140e1df60` is being
  used here purely as the base address of an unrelated mouse-delta-buffer
  struct that happens to start at the same address, not as a read of the
  flag's own value. Recorded here explicitly as a checked-and-ruled-out
  angle, per this project's own "checking is cheaper than digging" standard
  -- an earlier pass through this same call site briefly mis-read it as
  corroborating evidence before the struct-offset check caught it.
- **`FUN_1400cfce0`**, called directly inside the confirmed live-gameplay
  movement/button pipeline (`FUN_1400d0be0` calls it immediately before
  `FUN_1400d0050`, the movement writer) DOES read `DAT_140e1df60` directly
  (not as a struct base):
  ```c
  if ((&DAT_140e1dbc4)[lVar5] != DAT_140e1df60) {
      uVar3 = uVar2 | 0x800;              // set usercmd.buttons bit 0x800
  }
  ...
  if ((DAT_140e1df60 == '\0') || ((&DAT_140e1dbc4)[lVar5] != '\0')) {
      uVar3 = uVar3 & 0xefffffff;
  } else {
      uVar3 = uVar3 | 0x10000000;         // set usercmd.buttons bit 0x10000000
  }
  ```
  This compares the GLOBAL `DAT_140e1df60` against a PER-PLAYER mirror byte
  (`DAT_140e1dbc4[player]`) and sets two distinct `usercmd_t.buttons` bits
  depending on whether they match/mismatch -- **an edge-detection shape**
  (comparing a "requested" state against a "currently applied" per-player
  state and flagging the transition) that is structurally consistent with
  "ADS state just changed this frame," a real and expected thing for a
  network-synced multiplayer usercmd to need to signal explicitly (unlike
  SP, MP's usercmd has to communicate discrete state transitions to the
  server, not just continuous button-held state).

**Net effect on confidence**: this is real, new, independent evidence --
not a second look at the same dispatch-table fact -- and it's consistent
with the ADS hypothesis without being a decisive name-level confirmation.
`DAT_140e1dbc4[player]`'s own exact meaning is not fully pinned down either
(see section 6's open items -- it's also read by the angle-speed-scaling
code, which complicates a clean "current ADS state" reading of it). Honest
verdict: **upgraded from "medium-high, table-position-only" to "medium-high,
with real corroborating structural evidence from an independent function" --
still short of this project's own bar for a hook-ready confirmation**,
which per issue #3 requires either a genuine name anchor or live testing.

### 4-2. Cases 0xf/0x10 .. 0x19/0x1a (6-slot family) -- first real look, structural read reinforced (Session 2)

Per the task's own framing ("decompile it enough to say whether the
structural read is right or wrong, don't need to fully resolve it"), this
is a first pass, not a closing one. Full raw output:
`re_notes/x64_migration/mp_decomp_14007c5b0_c760.txt`,
`mp_globalrefs_killstreak_slots.txt`, `mp_callers_140059710.txt`.

**`FUN_14007c5b0(playerIdx, slotIdx)`** (the "down" half) reads a per-slot
**TYPE byte** from `&DAT_1405a65e8[slotIdx]` (values observed: 1, 2, 3) and
branches on it:
- type 1: a real "cycle to next available item in this slot" flow, gated by
  a **~100ms press-and-hold debounce** (`DAT_1406112d0 - _DAT_1406be80c >
  99`) against a "currently equipped index" global (`DAT_1405a6440`,
  37 references project-wide -- a heavily-used real piece of state) and a
  per-slot index array (`&DAT_1405a6604[slotIdx]`).
- type 2: delegates to a separate single function, `FUN_140087540`.
- type 3: sets a standalone flag bit (`DAT_1406be770 |= 0x40000`).

**`FUN_14007c760(playerIdx, slotIdx)`** (the "up" half) is much simpler --
just resolves the slot via `FUN_14007c4f0(&DAT_1405a60d0, slotIdx)`, real
"release does less work than press" asymmetry.

**Independent reinforcement found via `FindGlobalRefs.java` on the same
three globals** (`DAT_1405a65e8` slot-type array, `DAT_1405a6604` slot-index
array, `DAT_1405a6440` equipped-index): the SAME three globals are also
touched by **`FUN_140059710`**, a completely separate function reached from
section 2's confirmed HUD button-prompt cluster (`FUN_1400a0cf0`, cases
`0xab`-`0xae`, called with `param_11 - 0xab` as a 0-3 slot index -- a
smaller 4-slot subset of the same system, plausibly the subset visible
during a specific game mode/context). Decompiling `FUN_140059710` shows it:
- Also branches on the SAME `(&DAT_1405a65e8)[slot]==1` / `==2` type check.
- For type 2, walks a **15-entry array** (`&DAT_1405a6344`, bound `0xf`)
  looking for a matching item ID, then reads an internal item-name string
  and explicitly checks for an **`"iw5_"` prefix** (`pcVar2[0]=='i' &&
  [1]=='w' && [2]=='5' && [3]=='_'`) -- a real, recognizable CoD internal-
  asset-naming convention (weapon/equipment reference names in this engine
  commonly carry an `iw5_`-style prefix), strong evidence this system
  really does hold references to real game items/equipment, not arbitrary
  UI state.
- Ends by calling the same HUD-prompt draw primitive (`FUN_1402fd6f0`)
  section 2 already confirmed for on-screen button-prompt icons.

**Verdict from Session 2**: the structural read from Session 1 (an item/
equipment-slot system, killstreak-shaped) is **reinforced, not refuted** --
two independent function clusters (gameplay dispatch AND HUD-prompt
rendering) both consume the same three globals the same way, and the
item-name-prefix check confirms real game-item references are involved.

### 4-3. Cases 0xf-0x1a: real category name found (Session 3) -- "type 2" slots resolved to MW3's Assault Streak system, with high confidence

A genuine `"killstreak"`-literal string search (`FindExactStrings.java` for
`killstreak`, `airstrike`, `uav`, `sentry`, `predator_missile`, `ac130`,
and a dozen more real MW3 killstreak internal names) found real hits, and
following the data table two of them (`predator_missile`, `ac130`) sit in
led to a genuine, unambiguous confirmation. Full raw output:
`re_notes/x64_migration/mp_killstreak_strings.txt`,
`mp_killstreak_table_qwords.txt`, `mp_decomp_140276380.txt`.

`DumpRawQwords.java` over the table those two strings' pointers sit in
(`0x14054a600`-`0x14054a900`) revealed a large, contiguous, real MW3
content-name table: perks (`specialty_hardline`, `specialty_stalker`,
`specialty_scrambler`, ...), killstreaks (`predator_missile`, `ac130`,
`uav_support`, `counter_uav`, `precision_airstrike`, `airdrop_juggernaut`,
`remote_mg_turret`, `sam_turret`, `escort_airdrop`, `osprey_gunner`,
`littlebird_flock`, `remote_mortar`, ...), killstreak-unlock perks
(`specialty_longersprint_ks`, `specialty_fastreload_ks`, ...), and
deathstreaks (`specialty_juiced`, `specialty_revenge`, `specialty_finalstand`,
`specialty_c4death`, `specialty_grenadepulldeath`) -- genuine retail MW3
content data, not a coincidental string cluster.

**The decisive find**: this table has exactly one direct code reference,
`FUN_140276380` -- a real challenge/achievement-style **loadout validator**
that checks a player's saved custom class against specific criteria. Its own
literal string labels name the table's sub-ranges explicitly:

```c
FUN_14032be10(local_88,"assaultStreaks",0x20);
cVar2 = FUN_140278e50(param_1,param_2,local_c8,&local_148,&PTR_DAT_14054a690,
                      0xf,param_3,0,local_120);          // 15 entries
...
FUN_14032be10(local_88,"defenseStreaks",0x20);
cVar2 = FUN_140278e50(...,&PTR_s_uav_support_14054a710,0xc,...);   // 12 entries
...
FUN_14032be10(local_88,"specialistStreaks",0x20);
cVar2 = FUN_140278e50(...,&PTR_s_specialty_longersprint_ks_14054a770,0xe,...); // 14 entries
...
FUN_14032be10(local_88,"deathstreak",0x20);
```

**`&PTR_DAT_14054a690`, the real "assaultStreaks" table, has exactly `0xf`
(15) entries** -- an EXACT count match to `FUN_140059710`'s own confirmed
"type 2" slot-lookup array (`&DAT_1405a6344`, bound `0xf`, from section 4-2
above), which walks the same structure shape (linear scan for a matching
item ID) for the same reason (resolving which killstreak the player has
equipped in a given slot). **This is a real, named category confirmation**,
not a table-position guess: MW3's own three killstreak play-style trees are
literally named `assaultStreaks` (traditional streak-count killstreaks --
UAV, Predator Missile, AC-130, etc.), `defenseStreaks` (Support-style,
count resets on death, e.g. `uav_support`, `sam_turret`, `remote_mg_turret`),
and `specialistStreaks` (Specialist-style, perk-based rather than
kill-count-based, e.g. `specialty_longersprint_ks`) -- console MW3's real,
documented Create-a-Class killstreak-tree choice.

**Honest caveat on the remaining gap**: `DAT_1405a6344` (the runtime array
`FUN_140059710` actually walks) and `&PTR_DAT_14054a690` (the static
content-name table `FUN_140276380` names) are NOT the same memory address --
the static table is compile-time asset-name data (`0x14054axxx` region), the
runtime array is very likely a per-loadout CACHE built from it when a match/
class loads (a live-populated array of resolved killstreak entries at a
completely different, dynamically-allocated-looking address,
`0x1405a6xxx`). **The exact runtime code that populates `DAT_1405a6344` from
this static table was not traced this pass** -- the identity claim rests on
the count match (15, an unusual and specific number unlikely to coincide by
chance) plus both consuming the same "linear-scan for a matching item ID,
resolve a display name" shape, not a direct pointer-chase proof. Treat as
**high confidence, not a certainty** -- a reasonable, well-evidenced
stopping point for this pass, with the exact runtime-population code as the
natural next step if full certainty is ever needed.

**Net effect on the original task-3 question**: the 6-slot family (cases
0xf-0x1a) is now name-anchored, not just structurally reinforced --
"type 2" slots resolve to MW3's real Assault Streak system with high
confidence. "Type 1" and "type 3" slot semantics (the other two branches in
`FUN_14007c5b0`) remain unresolved to a specific category by name -- a
reasonable follow-up, not attempted further this pass.

### 4-4. Remaining dispatch cases -- full transcription completed, several new finds (own-judgment pass, Session 3)

Session 1's full `FUN_1400ce950` decompile (89 cases, 1 through `0x59`)
was already captured in `mp_decomp_1400b5820_ce950.txt` but not fully
transcribed into this file's own case table -- pure documentation
completeness work, no new Ghidra queries needed for most of it. Doing
that pass surfaced several real, coherent finds worth recording on their
own, not just filler:

**A full voice/text-chat-mode cluster, previously only partly
documented.** Cases `0x1d`/`0x1e` are a real **hold-vs-tap** pair
(matching the exact same shape as SP's own confirmed Y-hold weapon-next/
ready-up mechanic): on down, if not already connected+chatting, arms a
pending-state timer (`DAT_140e21394`/`DAT_140e2139c`/`DAT_140e213a0`) and
switches `DAT_140e21398` (the confirmed chat-mode state this project
already found consumed by `FUN_1400cfce0`'s usercmd bits 0x100/0x200,
section 6) to `1` (team chat); on up, reverts it if the hold was genuine.
Cases `0x52`-`0x55` are four more chat-mode setters (toggle / cycle /
force-public / force-team), all gated on the same "not already chatting"
per-player byte pair (`DAT_140e1dcdc`/`DAT_140e1dbec`). Together with the
already-confirmed one-shot command-string cases `0x4e`/`0x4f`
(`"chatmodepublic\n"`/`"chatmodeteam\n"`), this is a complete, internally
consistent communication-mode subsystem spanning 8 of the 89 cases --
useful future-work context (voice/text chat binds), not itself
gameplay-movement-critical.

**Two more independent touches to the ADS candidate (`DAT_140e1df60`),
strengthening section 4-1's already-upgraded confidence further.** Case
`0x56` toggles it exactly like case `0x43` does (`DAT_140e1df60 =
DAT_140e1df60 == '\0'`) but WITHOUT the paired kbutton call `0x43` also
makes; case `0x57` force-clears it exactly like case `0xd`'s down-edge
does, also without a kbutton call. **This is now FOUR independent dispatch
cases** (`0xd`, `0x43`, `0x56`, `0x57`) all touching the same flag in ways
consistent with a real ADS toggle/cancel state -- a genuinely strong
static signal (two cases pair a kbutton call with the flag change, two
don't, plausibly a real "hold to ADS" vs. "toggle ADS" pair of physical
binds sharing the underlying state, matching this game's real "hold vs.
toggle ADS" options-menu setting). Confidence stays formally
"medium-high, static-only" per this project's own issue #3 policy, but
the evidentiary base behind that confidence is now meaningfully larger
than it was after Session 2.

**Case `0x51` independently reaches the same scoreboard-toggle effect as
case `0x42`'s up-edge** (`LAB_1400cf55d`, `DAT_140e21454 = DAT_140e1dfc8 ^
DAT_1404e87e0`) -- a third case pointing at the section-4 `+scores`
candidate, reinforcing rather than changing that entry's existing
"candidate, not confirmed" status.

**A new, reasonably well-supported Hold Breath candidate.** Case `0x4c`/
`0x50` is a plain boolean set/clear pair (`FUN_14007f5b0(player, 1)` /
`FUN_14007f5b0(player, 0)`) -- notably NOT a `kbutton_t` KeyDown/KeyUp call
and NOT the generic held-bind array, a third, distinct case shape this
table hadn't seen before this pass. This matches x86 MP's own Session 2
open question about Hold Breath almost exactly ("Is `+holdbreath`
implemented as a real kbutton... or as a one-shot command... does it have
native duration/recovery or is it state-based?") -- x64 static evidence
here answers "state-based, a direct boolean, not a kbutton_t and not a
one-shot command." Medium confidence, shape-based only, no name string
anchor found for this specific case.

Full case table addition (cases not already in the table above):

| case (down/up) | target | shape | note |
|---|---|---|---|
| `0x1d`/`0x1e` | chat-mode state machine (`DAT_140e21394`/`98`/`9c`/`a0`) | hold-vs-tap | Team-chat hold candidate, see above |
| `0x25`-`0x40` (14 more pairs) | `0x140e1db00`-`0x140e1dccc` region | single kbutton, uniform | Confirmed mechanical continuation of the mapped pattern -- addresses now fully in the raw decompile output, not individually named here |
| `0x45`/`0x46` | `0x140e1dd08` (SAME as case 9/10's second kbutton) | single, shared with a dual-kbutton case | -- |
| `0x47`/`0x48` | `FUN_140090c80`/`FUN_140090d20(player)` | one-shot pair, no kbutton | Not decompiled further this pass |
| `0x49`/`0x4a` | `0x140e1dd44` | single kbutton | -- |
| `0x4b` (down only, no up case) | `FUN_1400d71b0(player)` | pure one-shot | Candidate shape for a single-press action (e.g. Melee) -- not name-confirmed |
| `0x4c`/`0x50` | `FUN_14007f5b0(player, 1/0)` | boolean set/clear, distinct third shape | Candidate for **Hold Breath**, see above |
| `0x51` | reaches `LAB_1400cf55d` (scoreboard XOR) when connected | one-shot, no kbutton | Third case pointing at the `+scores` candidate |
| `0x52`-`0x55` | chat-mode setters (`DAT_140e21398`) | one-shot, gated | Part of the chat cluster above |
| `0x56` | toggles `DAT_140e1df60` (no kbutton) | one-shot toggle | 3rd independent ADS-candidate touch |
| `0x57` | force-clears `DAT_140e1df60` (no kbutton) | one-shot | 4th independent ADS-candidate touch |

### 4-5. Cases 0x47/0x48, 0x4b, 0x4c/0x50 decompiled (Session 4, 2026-09-05) -- Hold Breath significantly upgraded, Melee candidacy complicated, one dead end honestly closed

Per Session 3's own recorded next step ("`FUN_1400d71b0`/`FUN_140090c80`/
`FUN_140090d20`/`FUN_14007f5b0`... were identified by shape but not
decompiled -- doing so could turn the Hold-Breath and one-shot-melee
candidates from shape-based into better-supported"). Full raw output:
`re_notes/x64_migration/mp_decomp_case47_48_4b_4c_50.txt`,
`mp_decomp_1400cd110.txt`, `mp_globalrefs_weaponclass_631c.txt`,
`mp_globalrefs_be770.txt`, `mp_decomp_140091780.txt`.

**Cases 0x4c/0x50 (`FUN_14007f5b0`) -- Hold Breath, upgraded from "medium,
shape-based only" to high confidence, multi-signal corroborated.** The
decompile shows real, specific gating that matches MW3's actual Hold Breath
mechanic precisely, not a generic "any special ability" shape:
- Gated on bit 0xc of a per-player state field (`(DAT_1405a60e0 >> 0xc & 1)
  != 0`) -- consistent with an "is aiming down sights" check (Hold Breath is
  ADS-only in real MW3).
- Gated on `DAT_1405a631c - 0x10U < 3` -- a **3-value range check** against
  a per-player field, consistent with a weapon-CLASS enum restricted to a
  small contiguous range (sniper-rifle-class weapons specifically, matching
  Hold Breath's real weapon-class restriction). **Independently checked, not
  assumed**: `FindGlobalRefs.java` against `DAT_1405a631c` alone found it
  referenced by **10 distinct functions** (including `FUN_140087540`, the
  same function section 4-2's killstreak-slot "type 2" branch delegates to)
  -- a genuinely widely-consulted field, consistent with a general "current
  weapon class ID" enum rather than a coincidental one-off variable.
- If the weapon-class check passes, looks up a **per-weapon-class
  capability byte** at a fixed offset (`+0x9ae`) in a per-weapon data table
  (`&DAT_1405711a0`, indexed by `DAT_1405a6434`) -- exactly the shape of a
  "does this specific weapon support Hold Breath" flag, not a hardcoded
  weapon-class check alone.
- Two distinct outcomes: if the capability check + an "already active" flag
  region are set, it just flips a bit in a shared per-frame event-flags
  register (`DAT_1406be770 |= 0x200000`) and returns; otherwise it calls
  `FUN_1400abe80(player, 1)` + `FUN_140085c20(player, 0, 0, 0)`, the real
  activation pair.

**Still not name-confirmed** per this project's own issue #3 policy (no
literal string/bind-name anchor found), but this is now the strongest
static evidence this project has for any non-command-string MP dispatch
case -- three independent, mechanically-specific gates (ADS state, a narrow
weapon-class range, a per-weapon capability table) all pointing the same
direction, not a single structural coincidence.

**Case 0x4b (`FUN_1400d71b0`) -- Melee candidacy COMPLICATED, not
strengthened.** The decompile reveals genuinely dual-context behavior: a
distinct branch when `DAT_140e1dddc == 0xb` (the SAME Killcam/Theater
connect-state section 4's `FUN_1400c3290` gates on) that does
Killcam-specific state transitions (state 2/0 via the `FUN_1403077c0`/
`FUN_14030eed0` getter/setter pair, see below), versus a separate branch
for live gameplay that checks a per-player byte (`&DAT_140e1dd75)[player]`)
before setting state 1. A genuine single-press Melee bind would not
typically need Killcam-mode-specific logic at all -- this doesn't rule out
Melee, but it's a real complication worth recording honestly rather than
treating the earlier "candidate shape" note as reinforced by this pass. No
replacement candidate identified.

**Cases 0x47/0x48 (`FUN_140090c80`/`FUN_140090d20`) -- real state-machine
shape found, no name anchor.** Both gate on `DAT_1405a9448` (a mode-active
flag also referenced by the Hold Breath function above) and a shared getter,
`FUN_1400cd110()` (see the dead-end note below). The down case (0x47) is
further gated by a real **2000ms (2-second) cooldown**
(`DAT_14061e7dc + 2000 < DAT_1406112d0`), then transitions a per-player
"activity/animation state" field to value `6` via `FUN_14030eed0(player,
6)` -- but only if currently NOT already in state 6 (read via
`FUN_1403077c0(player)`). The up case (0x48) reverses this back toward
state 0. **New reusable finding**: `FUN_1403077c0`/`FUN_14030eed0` is a
real, generic per-player "activity state" get/set accessor pair -- multiple
different case handlers in this section use it with different numeric
states (0, 1, 2, 6), suggesting a small enum of mutually-exclusive
special-action states (e.g. reviving/being-revived, a downed/last-stand
state, Killcam playback state) that different binds transition into and out
of. Worth reaching for by name if a future pass investigates any of the
remaining un-decompiled one-shot cases. No specific name confirmed for
state value 6 itself this pass.

**`FUN_1400cd110` -- checked and honestly ruled out as Item B's writer
(Session 3's own "if one can be found" follow-up).** Decompiles to a
trivial one-line getter: `return DAT_140e1dd82;` -- a completely different
global from `DAT_140e1dbc4`, not its writer. This specific avenue is
closed; **section 6's `DAT_140e1dbc4` cross-context ambiguity (the "why is
ADS state compared against digital-look-active specifically" open question)
remains genuinely unresolved.** Recorded per this project's own "document
dead ends" standard rather than silently dropped.

**`DAT_1406be770` (the shared event-flags register both Hold Breath and
killstreak-slot type 3 write into) -- structural role confirmed.**
Decompiled its one meaningful consumer, `FUN_140091780`: reads the entire
flags register, passes it to `FUN_1400cdbe0(player, flags)`, then
**immediately zeroes it** (`DAT_1406be770 = 0`) every call, alongside
per-frame float/position math and calls that look like a client-side
feedback/effects packaging step (`FUN_1400cdc00`/`FUN_1400cddf0`/
`FUN_1400cdbe0`). **This is a per-frame, self-clearing "gameplay event
occurred this frame" flag accumulator, not persistent per-player state** --
meaning bits like Hold Breath's `0x200000` and the killstreak-slot type-3
bit (`0x40000`, section 4-2) are one-shot per-frame triggers (plausibly for
client-side sound/HUD-flash feedback), not "currently active" flags. Real
architectural context for any future work touching this system; does not
by itself name either specific bit.

### 4-6. Follow-up on Session 4's own recorded open items (Session 5, 2026-09-05): type-1 killstreak-slot gets a real (if not name-confirmed) structural lead; the `DAT_140e1dbc4` ambiguity confirmed genuine, not an artifact

Both items Session 4 flagged as open. Full raw output:
`re_notes/x64_migration/mp_decomp_14007c4f0.txt`,
`mp_decomp_ads_consumers.txt` (already saved Session 3, re-examined in full
this pass).

**Type-1 killstreak-slot: a real cross-link found, structural lead only,
not a name.** `FUN_14007c5b0`'s type-1 branch (section 4-2) indexes the
SAME per-weapon capability table (`&DAT_1405711a0`) Hold Breath's function
indexes (section 4-5) -- but at a DIFFERENT field offset (`+0x60` here vs.
`+0x9ae` for Hold Breath), keyed by `DAT_1405a6440` ("currently equipped
index," masked to a byte) rather than a weapon-class-range value, and
compared against the constant `4`. This is genuine, new evidence that
`&DAT_1405711a0` is a real, general per-weapon (or per-weapon-class)
capability/definition table serving multiple, otherwise-unrelated systems
(type-1 slot cycling AND Hold Breath) -- reinforces both findings'
underlying premise (a real weapon-database table exists and is what each
system is actually querying) without pinning down type-1's specific
category name. Given CoD's own real engine convention of storing
lethal/tactical equipment through the same "weapon" definition system as
guns, and given "type 2" is confirmed killstreaks (section 4-3), a
plausible (NOT confirmed) reading is that "type 1" represents
lethal/tactical equipment cycling rather than a second killstreak
category -- flagged as a reasonable hypothesis worth checking against a
real equipment-name string search next session, not asserted as fact.

**Separately checked and ruled out as a naming path**: decompiled
`FUN_14007c4f0`, the small helper both `FUN_14007c5b0` and `FUN_14007c760`
call through before their own type-specific logic. It reads/writes a large
per-player context struct (offsets up to `+0x3378`+, consistent with the
same class of big per-player struct section 6 already found offsets into)
and a genuine 6-entry per-slot state array at `+0x518` (`+ slotIdx*4`) --
but every check inside is slot-INDEX-specific eligibility gating (is this
slot currently selectable this frame), not slot-TYPE-specific content
logic. One concrete, real, non-naming finding: **slot index 3 specifically
gets an extra exclusion condition no other slot has** (`param_2 != 3 ||
(structByte_0xc & 4) == 0`), a genuine structural oddity -- plausibly a
mode-gated slot (e.g. Support-streak-only, or unavailable in certain game
modes), not investigated further this pass. This function is a dead end
for the type-1/type-3 NAMING question specifically -- recorded so a future
pass doesn't re-walk it expecting a different answer.

**`DAT_140e1dbc4` ambiguity (section 6, Item B): confirmed to be a real,
structural double-duty byte, not a stride-mismatch artifact.** Re-read
`FUN_1400cfce0`'s FULL decompile (not just the excerpt previously quoted)
specifically to check one thing: does `lVar5` in the ADS-comparison
function actually use the same per-player stride as `FUN_1400d0be0`'s
confirmed digital-angle-speed-key usage of `DAT_140e1dbc4`, or could this
be two different loop variables coincidentally overlapping at the same
byte address? **Directly confirmed, not assumed**: line 22 of
`FUN_1400cfce0` is `lVar5 = (longlong)param_1 * 600` -- the exact same
per-player-index * 600 stride confirmed everywhere else in this file. This
rules out the "coincidental address overlap" explanation the ambiguity
could otherwise have had -- both functions genuinely read the identical
per-player field, so the double-duty (or single, more-general-than-assumed)
meaning is real, not an artifact of two different things sharing a name by
chance.

Re-reading the full function also surfaced a real, previously-unrecorded
structural detail worth keeping for a future attempt at this: the
comparison this project has been calling "ADS state" (`DAT_140e1df60`, a
GLOBAL, not per-player) against `DAT_140e1dbc4[player]` (per-player) is an
inherently ASYMMETRIC pairing -- one shared global compared against one
per-player mirror -- which doesn't cleanly fit either of section 6's two
proposed readings for `dbc4` alone (a simple "digital-look-active" flag,
confirmed real in `FUN_1400d0be0`, has no obvious reason to gate an ADS
network-state signal; equally, nothing here suggests `dbc4` secretly means
something ADS-specific instead). **Honest verdict, not forced**: this
pass CONFIRMS the ambiguity is real and worth resolving, and RULES OUT one
possible innocent explanation (stride mismatch) for it, but does not
itself resolve it. The most promising untried next step, not attempted
this pass: decompile `FUN_1400cfef0` fully (currently only understood by
its return-value SHAPE, "a normalized 0.0-1.0 hold fraction," per section
6) since it's the one function called with `DAT_140e1dbc4`-ADJACENT
addresses (`puVar6 + 0x78`, etc.) in `FUN_1400cfce0` itself -- its own
full body might reveal what per-player struct region `dbc4` actually
belongs to, rather than treating it as an isolated single byte.

### 4-7. Session 6 (2026-09-05): both of section 4-6's own untried next steps attempted -- a real, well-bounded content-table candidate found for type-1; `DAT_140e1dbc4` precisely located (not resolved)

Per the coordinator's own explicit framing this round ("keep pushing on
those same two specific threads... or pick fresher ground -- your call"):
both of section 4-6's own recorded next steps were tractable, so both were
attempted rather than switching targets. Full raw output:
`re_notes/x64_migration/mp_decomp_1400cfef0.txt`,
`mp_equipment_strings.txt`, `mp_equipment_table_qwords.txt`.

**Type-1 killstreak-slot: a real, exactly-bounded content-table candidate
found.** A direct string search for real MW3 lethal/tactical equipment
names (`frag_grenade_mp`, `semtex_mp`, `throwingknife_mp`,
`bouncingbetty_mp`, `claymore_mp`, `c4_mp`, `flash_grenade_mp`,
`smoke_grenade_mp`) found real hits, and dumping the raw table region
around them (`DumpRawQwords.java`, `0x14054a5a0`-`0x14054a700`) revealed
the SAME content-name table region section 4-3 already found for
killstreaks, extended further back: a **cleanly-bounded, exactly 6-entry
LETHAL equipment table** at `0x14054a5a0`-`0x14054a5c8` (frag, semtex,
throwing knife, bouncing betty, claymore, C4 -- the complete, real MW3
lethal-grenade roster), immediately followed by a large perks table
(`specialty_paint`/`specialty_fastreload`/`specialty_hardline`/etc.,
`0x14054a5d0`-`0x14054a648`), then a tactical-equipment cluster
(`flash_grenade_mp`/`concussion_grenade_mp`/`emp_grenade_mp`/
`smoke_grenade_mp`/`trophy_mp`, `0x14054a650`-`0x14054a678`), then the
already-confirmed killstreak trees starting at `0x14054a690`. **This is a
real, coherent, ordered Create-a-Class content taxonomy** (lethal → perks
→ tactical → killstreak trees), not a coincidental string cluster.

**Honest strength assessment, matching Session 3's own standard for this
exact class of evidence**: the lethal table's entry count (6) is an EXACT
match to the killstreak-slot family's own total slot count (6, cases
`0xf`-`0x1a`) -- real, notable, and thematically coherent with section
4-6's equipment-cycling hypothesis for "type 1." But per this project's
own issue #3 discipline, **this is NOT the same strength of evidence as
type-2's own confirmation** (which matched a runtime-populated array's OWN
bound, not just the outer slot family's total count) -- no pointer-chase
from type-1's actual runtime lookup array (`DAT_1405a6604`) to THIS
specific static table was attempted or found this pass. Upgraded from "a
reasonable hypothesis" to "a reasonable hypothesis with a real, specific,
well-bounded, thematically-coherent content-table candidate identified" --
still short of a name confirmation, honestly labeled as such.

**`DAT_140e1dbc4` ambiguity: precisely located within its own struct,
still not resolved.** Decompiled `FUN_1400cfef0` in full as section 4-6
recommended -- it turned out NOT to take `&DAT_140e1dbc4` as an argument
anywhere (a slight correction to section 4-6's own framing of why this
function was promising); it confirms the digital-hold-duration struct's
exact field layout (`{heldMs:+0xc, lastTimestamp:+8, isHeld:+0x10}`) but
doesn't itself touch `dbc4`. **A genuinely new, useful fact found instead
by simple arithmetic**: `DAT_140e1dbc4` is exactly **offset `0xC4` from
the confirmed per-player kbutton-region base** (`&DAT_140e1db00 +
player*600`) -- i.e. it's `puVar6 + 0xC4` in this section's own established
notation, sitting immediately AFTER the cluster of digital movement/look
hold-duration sub-fields `FUN_1400cfef0` reads from (which occupy roughly
the region's first ~0x90 bytes: `+0x00`, `+0x14`, `+0x28`, `+0x3c`, `+0x78`
are all confirmed call sites). This places `dbc4` structurally WITHIN the
same "digital key state" cluster, not in an unrelated part of the region
-- real support for the "digital-look-active" reading being genuinely
correct, which makes `FUN_1400cfce0`'s ADS-flag comparison against it MORE
puzzling on its face, not less. **Honest verdict**: precisely located, not
resolved -- the double-duty (or genuinely dual-purpose) meaning remains
open. No further untried concrete next step is currently recorded for this
specific thread; a future pass would likely need to decompile a wider
survey of every other consumer of the surrounding `0x00`-`0xC4` region to
build a fuller struct map, a larger undertaking than this pass's scope.

### `FUN_1400c3290` -- confirmed Killcam/Theater-mode dispatcher (x86 `FUN_006ada70` analog)

Entire body gated on `DAT_140e1dddc == 0xb` (inert everywhere else, return-only
outside that state). Real cases match x86's own description almost exactly:
space (0x20) = play/pause toggle, 0x9a/0x9b = zoom float-accumulator
in/out, 0x9c/0x9d = playback-speed adjust, 0x72 ('r') = record,
200/0xc9 = numeric-row seek. **Confirmed dead/inert during ordinary live
gameplay**, same conclusion x86 MP Session 4 reached for its own analog.

---

## 5. Per-player kbutton region -- static analysis wall (same as x86 MP)

Tried `FindGlobalRefs.java` against three of the kbutton addresses from the
case table above (`0x140e1dcf4`, `0x140e1dbb4`, `0x140e1dc2c`). **Zero
additional consumer functions found beyond `FUN_1400ce950` itself** (the
writer) -- no Pmove/movement-tick function shows up as a typed reference to
any of these globals. This is the exact same static-analysis wall x86 MP's
own Session 5 hit for its generic-array kbutton class ("the writer for this
array... is still not locatable statically... a real, precedented limitation
of this specific bind class without a live-memory-diff anchor"), now
reproduced independently for x64 MP's individually-placed `kbutton_t` class
too. Not a gap in this session's effort -- a genuine limitation of
address-typed-reference analysis when a consumer reads through computed
pointer arithmetic Ghidra can't resolve to a fixed global. **Movement/Pmove-
tick functions were NOT found via this angle and remain unlocated.**

---

## 6. Movement/look pipeline -- CONFIRMED (Session 2, 2026-09-05)

**Found via the exact technique Session 1 recommended**: not the section-5
kbutton-global-reference dead end, and not a locality/constant-scan sweep --
instead, the dvar-registration function itself. `FindExactStrings.java` for
the classic id-Tech movement/look cvar names (`cl_yawspeed`,
`cl_pitchspeed`, `cl_anglespeedkey`, `m_pitch`, `m_yaw`, `m_forward`,
`m_side`, `sensitivity`, `cl_freelook`, `cl_bypassMouseInput`,
`player_sprintUnlimited`) found every one of them registered in a single
function, `FUN_1400d4370` -- the real x64 MP boot-time dvar-registration
function (x86 MP's `FUN_00492560` analog). Decompiling it gave the real
storage-handle globals for each dvar; `FindGlobalRefs.java` against those
handles (the exact "dvar-value-discovery chain" technique this project's own
CLAUDE.md documents, and the literal example the `FindGlobalRefs.java`
header comment gives -- "a function touching cl_yawspeed + cl_pitchspeed +
m_pitch + m_yaw + cl_anglespeedkey together is a strong CL_AdjustAngles/
usercmd-angle-update candidate") led straight to the real pipeline in one
pass. Full raw output: `re_notes/x64_migration/mp_movement_strings.txt`,
`mp_decomp_1400d4370_cvarreg.txt`, `mp_globalrefs_movement_dvars.txt`,
`mp_decomp_movement_candidates.txt`.

### `FUN_1400d0be0(usercmdOut, playerIdx)` -- the real top-level per-frame orchestrator (x86 MP `FUN_0048a7b0` analog)

Zeros a `usercmd_t`-shaped output struct, then:
- Applies `cl_yawspeed`/`cl_pitchspeed`/`cl_anglespeedkey`-scaled deltas
  (via a `FUN_1400cfef0` per-key-held-duration reader, see below) into
  **`DAT_140e21454`/`DAT_140e21458`** -- confirmed (cross-validated across
  three separate functions in this section) as the real per-frame **yaw/
  pitch view-angle accumulators**, direct x64 analog of x86's fixed-point
  angle accumulators. This whole block is gated `(_DAT_140e1df74 & 0x800)
  == 0` -- the exact x86 MP "freelook mode" bit this project's own Session 1
  notes already flagged (`iw5mp.md`: "MP's FUN_00489c40 branches on freelook
  mode").
- Branches on `_DAT_140e1df74 & 0x80000` -- **the exact same bit value**
  x86 MP's own Session 5 confirmed as the "reduced-summer gate"
  (`DAT_010627a4 & 0x80000`, spectator/killcam-restricted state, per
  `iw5mp.md`). When clear (normal live gameplay): calls
  `FUN_1400ce5c0` -> `FUN_1400cfce0` -> `FUN_1400d0050` (movement, see
  below). When set (restricted state): calls `FUN_1400d0350` instead.
  **This is the exact same two-path branch structure x86 MP's own
  `FUN_0048a7b0` was confirmed to have** (full-scope `FUN_00489f40` path vs.
  reduced `FUN_0048a5d0` path) -- reproduced independently in x64 without
  any x86 address being assumed.

### `FUN_1400d0050(playerIdx, usercmdPtr, ...)` -- CONFIRMED real movement writer (x86 MP `FUN_00489c40` analog)

```c
// (abridged)
FUN_1400cfb60(&DAT_140e1df60, local_88, local_res20);   // raw mouse-delta read (see below)
...
*(char*)(param_2 + 0x1d) = FUN_1403170f0(...);   // usercmd_t.rightmove  <-- EXACT x86 offset
...
*(char*)(param_2 + 0x1c) = FUN_1403170f0(...);   // usercmd_t.forwardmove <-- EXACT x86 offset
```

**Writes `usercmd_t.forwardmove`/`.rightmove` at the exact same byte offsets
(+0x1c/+0x1d) x86 confirmed for BOTH `iw5sp.exe` and `iw5mp.exe`** -- this is
the single strongest confirmation in this section, a byte-offset match, not
a structural-shape inference. Branches on a per-player byte
(`&DAT_140e1dbb0)[player]`) to decide whether the scaled Y-axis delta feeds
strafe (`m_side`-scaled) or look-pitch (`m_pitch`-scaled) -- **exact
reproduction of x86 MP's own documented "real structural difference: MP's
FUN_00489c40 branches on freelook mode (mouse Y as movement vs. look
pitch)"**. Also calls `FUN_14001e2c0` to fold the yaw/pitch accumulators
(`DAT_140e21454`/`DAT_140e21458`) into a final compressed angle write at
`param_2+0x20`/`+0x22` (not yet reconciled against x86's documented
+0x26-+0x2a angle-byte range -- see open item below).

### `FUN_1400d0350(playerIdx, usercmdPtr)` -- CONFIRMED reduced-scope button-summer + angle finalize (x86 MP `FUN_0048a5d0` analog)

Writes `usercmd_t.buttons` (at `+4`) bits `0x1`/`0x8`/`0x20`/`0x4000`/`0x8000`
from the SAME generic-array kbutton region as `FUN_1400ce5c0` (below) --
**5 checks only**, exactly matching x86 MP's own documented "`FUN_0048a5d0`
(5 bind checks only)" reduced variant. Separately writes final `usercmd_t`
angle BYTES at `param_2+0x26`, `+0x27`, `+0x28`, `+0x29`, `+0x2a` --
**exact byte-for-byte match to x86's documented usercmd_t angle range
(+0x26-+0x2a)**, using `m_yaw`/`m_pitch`-scaled accumulator deltas. This is
the clearest single confirmation that x64's `usercmd_t` layout is
byte-identical to x86's for the angle fields, independently reproducing
x64 SP's own already-documented finding that this struct survived the
recompile unchanged.

### `FUN_1400ce5c0(playerIdx, usercmdPtr)` -- CONFIRMED full generic "simple held bind" array summer (x86 MP `FUN_00489f40` analog)

**14 bit checks, all 14 bit VALUES an exact match to x86 MP's own
Session 5 confirmed table** (`iw5mp.md`), reproduced independently here with
different (x64) `DAT_` addresses:

| bit | x64 kbutton address (per-player, `+player*600`) | x86 MP bit identity (from `iw5mp.md`) | match |
|---|---|---|---|
| `0x1` | `0x140e1dc28`/`29` | `+attack` (Fire), High confidence | exact |
| `0x2000` | `0x140e1dc3c`/`3d` | `+melee`, High confidence (bit match) | exact |
| `0x4000000` | `0x140e1dc50`/`51` | New, no SP analog | exact |
| `0x4000` | `0x140e1dc64`/`65` | `+frag`, High confidence | exact (also independently seen in `FUN_1400d0350` above) |
| `0x8000` | `0x140e1dc78`/`79` | `+smoke`, High confidence | exact (also seen in `FUN_1400d0350`) |
| `0x4` | `0x140e1dc8c`/`8d` | Retracted/unreliable in x86, Low confidence | exact position |
| `0x8` | `0x140e1dca0`/`a1` | `+usereload` (Use), High confidence | exact (also seen in `FUN_1400d0350`) |
| `0x10` | `0x140e1dcb4`/`b5` | "Reload" empirically, bind-name mismatch flagged, Low confidence | exact position |
| `0x20` | `0x140e1dcc8`/`c9` | `+actionslot 1`, High confidence | exact (also seen in `FUN_1400d0350`) |
| `0x100` | `0x140e1dcdc`/`dd` | `+actionslot 2`, High confidence | exact |
| `0x200` | `0x140e1dcf0`/`f1` | `+actionslot 3`, High confidence | exact |
| `0x400` | `0x140e1dbd8`/`d9` | `+gostand` (jump), High confidence | exact |
| `0x80000` | `0x140e1dd04`/`05` | New, no SP analog | exact |
| `0x400` (2nd, conditional on `DAT_140e1dddc>6` and a killcam/follow-mode-shaped guard) | `0x140e1dc00`/`01` | New, "likely a killcam/spectator-follow-specific duplicate of the jump bit, not independently investigated" | exact, including the conditional shape |

**This is a genuinely strong cross-architecture validation** -- not just bit
values matching, but the exact SET of 14 bits, their relative ordering, and
even the one conditional/gated duplicate bit all carrying over unchanged
from x86 MP's own independently-derived table. Reinforces confidence in
BOTH tables simultaneously (any given bit identity is now supported by two
independent binaries' worth of static evidence, not one).

### `FUN_1400cfb60`/`FUN_1400cfef0` -- raw mouse-delta and per-key-hold-duration readers (x86 MP `FUN_00489ba0` analog, split into two helpers)

`FUN_1400cfb60(structBase, outX, outY)` reads a double-buffered raw
mouse-delta accumulator (`structBase+0x3420`/`+0x3424` and `+0x3428`/
`+0x342c`, parity-toggled via `structBase+0x3430`), scales by
`cl_mouseAccel`/`sensitivity`, and returns `outX`/`outY`. Called as
`FUN_1400cfb60(&DAT_140e1df60, ...)` -- **note this passes `&DAT_140e1df60`
purely as a struct BASE POINTER for its own +0x3420-range fields, not as a
meaningful read of byte 0 itself** -- see the explicit correction in
section 4's ADS discussion below, this was checked directly (not assumed)
after an initial mis-read. `FUN_1400cfef0(fieldPtr)` reads a single
`{lastFrameHoldMs, lastTimestamp, isHeld}`-shaped field (matches the classic
id-Tech "digital direction key" read pattern) and returns a normalized
0.0-1.0 hold fraction -- used both for arrow-key angle accumulation in
`FUN_1400d0be0` and for analog-adjacent reads in `FUN_1400ce5c0`/
`FUN_1400d0be0`'s freelook branch.

### Open items -- RESOLVED (Session 3, 2026-09-05)

Both items flagged at the end of Session 2 were followed to a real
conclusion this session using the same static toolkit (no live process).
Full raw output: `re_notes/x64_migration/mp_decomp_14001e2c0.txt`,
`mp_decomp_14001d3e0.txt`, `mp_globalrefs_dbb0_dbc4.txt`.

**Item A -- `usercmd_t+0x20`/`+0x22` are NOT angle fields at all.**
Decompiling `FUN_1400d0050`'s own `FUN_14001e2c0` call (the function that
writes those two offsets) resolved this decisively, and turned up a
significant, unrelated discovery along the way -- flagged carefully below.

`FUN_14001e2c0` copies the current yaw/pitch accumulators
(`DAT_140e21454`/`DAT_140e21458`) into a small output struct, then --
**only if a per-weapon "zoom active" config flag is set** -- calls
`FUN_14001d3e0`, which turned out to be **the game's own native aim-assist
targeting function**, confirmed beyond doubt by a real embedded debug string
still present in the retail binary: `"AimAssist_GetTagPos: Cannot find tag
[%s] on entity %i.\n"`. It scans nearby entities within a screen-space
bounding cone, computes a yaw/pitch "pull" toward a locked target's tag
position, and folds that into the yaw/pitch accumulators it was handed.
Two further fields it writes turned out to be exactly the two offsets from
open item A: a 2-byte **locked-target handle** (`0x7ff` = a real sentinel
for "no target locked") written to `usercmd_t+0x20`, and a 1-byte
**assist-strength/distance value** (clamped 0-255) written to
`usercmd_t+0x22`. **Neither offset is an angle field** -- they're native
aim-assist target-tracking state that rides along in the per-frame usercmd,
entirely separate from the real per-axis angle bytes at `+0x26`-`+0x2a`
(which `FUN_1400d0350` writes independently, on a code path this
aim-assist function is never even called from). This fully explains why the
two "angle-adjacent" writes never lined up -- they were never the same kind
of field to begin with.

**Standing note, read before touching this**: this is the GAME'S OWN native
engine system (`iw5mp.exe`'s own compiled code), found here purely by
following a data-flow trail from the already-confirmed movement pipeline --
not a rediscovery of, a step toward, or any kind of reopening of this
project's own **permanently removed** aim-assist feature (see the main
`CLAUDE.md`'s "Aim-assist target" entry, 2026-07-20: reversed and
permanently removed, explicitly "not a 'not yet done' item, it's a closed,
deliberately-abandoned one," and not to be resurrected without a fresh,
explicit risk discussion). Documenting that the native engine has its own
aim-assist implementation is ordinary static-RE bookkeeping, exactly the
same class of finding as this project's own already-documented
`missileHellfireUpAccel`/Predator Missile constant or `player_sprintUnlimited`
dvar discoveries -- describing what the retail binary does, not proposing
this project read live entity/aim-target memory itself. No further detail
beyond what's needed to close the two open items was extracted, and none of
it is being carried into any hook-target recommendation.

**Item B -- `DAT_140e1dbb0` and `DAT_140e1dbc4` are two distinct fields, with
different (though not perfectly disambiguated) roles.** `FindGlobalRefs.java`
against both addresses individually found both are read-only (no writer
resolvable via typed references, the same static wall other per-player mode
bytes in this binary have hit) by the same two functions
(`FUN_1400cfce0`, `FUN_1400d0be0`), plus `FUN_1400d0050` reads `dbb0` alone
-- consistent with both sitting in one small per-player "input mode" state
cluster, but re-reading each one's OWN specific usage separately (not just
noting proximity) gives each a distinct, reasonably-supported role:

- **`DAT_140e1dbb0[player]`**: in `FUN_1400d0050`, this is the actual
  decision bit for whether raw mouse-Y drives look-pitch (0, normal
  mouselook) or `usercmd_t.rightmove`/strafe (nonzero) -- the real,
  runtime per-player instance of x86 MP's own documented "mouse Y as
  movement vs. look pitch" freelook branching, not just the static
  `cl_freelook` dvar value (which is checked separately, elsewhere).
- **`DAT_140e1dbc4[player]`**: in `FUN_1400d0be0`, gates whether
  `cl_anglespeedkey`'s multiplier ("Multiplier for max angle speed for game
  pad and keyboard," per its own registration string) applies to the
  digital-look angle-speed calculation -- consistent with a "digital/
  keyboard-arrow-key look active this frame" flag, since that dvar's own
  description only makes sense applied to discrete key input, not
  continuous mouse/analog-stick deltas.

**Honest residual ambiguity, not papered over**: section 4-1's ADS
discussion also found `DAT_140e1dbc4[player]` compared directly against the
ADS candidate `DAT_140e1df60` inside `FUN_1400cfce0`. Given the
"digital-look-active" reading above, that specific comparison's exact
intent (why would ADS state be checked against digital-look-active
specifically?) is not fully explained by either role alone -- either the
byte genuinely serves double duty in a way this pass didn't fully trace, or
one of the two readings is incomplete. Recorded honestly as still open,
rather than forcing a clean answer neither piece of evidence fully
supports. This does not roll back section 4-1's own conclusion (the
edge-detection SHAPE of that comparison is real regardless of exactly what
`dbc4` represents) -- it's a refinement flag on the supporting detail, not
a retraction.

---

## Cross-reference

- **x86 MP tracker**: `re_notes/iw5mp.md` -- every finding above was
  cross-checked against it; see inline notes for exact structural matches
  (bind-table bound, per-player kbutton stride, kbutton_t struct layout,
  dual-kbutton case shape) and the one confirmed dead-end shared between
  both architectures (the alias-cluster/KEY_OR display function).
- **x64 SP tracker**: `re_notes/known_issues_x64.md` issue #1, and
  `re_notes/x64_migration/README.md` + its own sub-cluster files -- the
  x64-native kbutton_t struct (explicit pointer arg, no custom calling
  convention) and the ADS-toggle-flag pattern (`DAT_1406e26e0` in SP) both
  directly informed section 4's case 0x43/0x44 ADS hypothesis above.
- **Ghidra project**: `re_notes/ghidra_project_x64/iw5mp_x64_proj` (fully
  auto-analyzed, program `/iw5mp.exe`).
- **Raw script output this session**: `re_notes/x64_migration/mp_*.txt`
  (bind-string search, all decompiles, all caller-traces, the global-ref
  dead-end check) -- kept so any claim above can be re-verified without
  re-running Ghidra.
- **Killstreak-control motivation** (CLAUDE.md's locked scope decision,
  2026-08-21): section 4's 6-slot parameterized case family
  (`FUN_14007c5b0`/`FUN_14007c760`) is the first concrete lead toward that
  goal found in either architecture's MP research to date -- flagged for
  early priority next session specifically because of that standing project
  motivation, not just because it's structurally interesting.

---

## Signature-Scanning Readiness

**Status: ~69% ready** (Session 6 found a genuinely new, real, well-bounded
content-table candidate for type-1 -- not just reinforcing an existing
candidate the way Session 5 did, so a small increment over Session 5's
flat ~68% is warranted; still explicitly short of type-2's own stronger
runtime-array-bound-match evidentiary bar, per the honesty caveat recorded
in section 4-7. The `DAT_140e1dbc4` thread was precisely located but not
resolved -- no percentage weight given to location-without-resolution.
The remaining gap is unchanged in kind -- case-to-real-bind-NAME
confirmation for the movement-CRITICAL dispatch cases
(Fire/ADS/Sprint/Reload/Hold-Breath), not missing mechanism, which per
this project's own issue #3 policy may be gated on live verification for
the cases static evidence alone can't fully close).

- Bind-name table + lookup/resolver (`FUN_1400b5cf0`, `FUN_1400b4800`) [OK] confirmed
- Controls-menu rebind flow, alias-cluster/KEY_OR display [OK] confirmed UI-only, correctly not pursued as hook targets (mirrors x86 MP's own already-closed dead end)
- Menu-navigation key router (`FUN_1402f84a0`), `ForwardKeyToMenu`/unpause (`FUN_140309270`), `Cvar_Set` (`FUN_140326ac0`), `Cbuf_AddText` (`FUN_140262850`) [OK] confirmed -- all real, reusable future menu-nav targets
- Top-level input-event queue drain (`FUN_140270030`) [OK] confirmed -- the real per-frame entry point for all raw input
- Key-event handler (`FUN_1400b4960`) [OK] confirmed, all branches traced (tilde/console, ESC/pause, Killcam-gate, menu-forward, console-edit, live-gameplay dispatch, main-menu chat/console-line fallback)
- Killcam/Theater dispatcher (`FUN_1400c3290`) [OK] confirmed inert outside connect-state 0xb, correctly ruled out as the general dispatcher
- **Real gameplay-bind dispatcher (`FUN_1400ce950`)** [OK] confirmed via direct traced call sites on both key-down and key-up edges (not a heuristic) -- full case table (1 through 0x59) extracted directly from the decompiler output, a first for either architecture's MP work
- **Real `kbutton_t` KeyDown/KeyUp (`FUN_1400d0ea0`/`FUN_1400d0ed0`)** [OK] confirmed, exact struct match to both x86 binaries, real x64 simplification noted (explicit pointer arg)
- Case-to-real-bind-NAME mapping: 4 of ~89 cases confirmed by literal command string (chat/vote actions); the ADS candidate (0x43/0x44) now has real independent corroborating evidence (section 4-1) beyond table position; the rest are structural candidates only -- **none of the movement-critical candidates (Fire/ADS/Sprint/Reload) are name-confirmed**, same open item x86 MP's own Session 6 closed with
- **Movement/look/orchestrator pipeline: CONFIRMED this session** (section 6) -- top-level orchestrator, movement writer (exact `usercmd_t` byte-offset match to x86), reduced-scope button-summer/angle-finalize (exact byte-offset match), and the full 14-bit generic-array summer (exact bit-value match to x86 MP's own independently-derived table) are all found and cross-validated
- 6-slot parameterized case family (0xf-0x1a): first real look taken (section 4-2) -- structural "item/equipment-slot system" read reinforced by a second, independent function cluster (HUD-prompt rendering), not yet resolved to a specific real category name
- Byte-pattern signatures: still none extracted -- reasonable to start once the two movement-pipeline open items (section 6) and the case-name-confirmation gap are resolved or a live-verification phase is authorized
- Live validation: not started, and out of scope for this project's own locked static-first MP ordering until explicitly authorized for a live/injection phase

**Ready to proceed with** (updated Session 6): the type-1 killstreak-slot
thread now has a real, specific, well-bounded content-table candidate
(section 4-7); the `DAT_140e1dbc4` thread is precisely located but has no
further concrete untried next step recorded -- likely near diminishing
returns for now without a larger struct-mapping undertaking. **Natural
next steps for a future session**: (1) attempt an actual pointer-chase
from type-1's real runtime array (`DAT_1405a6604`) to the static
lethal-equipment table (`0x14054a5a0`) to bring type-1's confirmation up
to type-2's own evidentiary bar -- the concrete gap section 4-7 itself
flags; (2) the slot-3-specific exclusion condition found in
`FUN_14007c4f0` (section 4-6) remains a real, unexplained structural
oddity; (3) the real per-player "activity state" enum found Session 4
(`FUN_1403077c0`/`FUN_14030eed0`, values 0/1/2/6 observed) remains a
reusable lead for a future pass wanting to pin down state 6 (case
0x47/0x48) or state 2 (case 0x4b's Killcam branch); (4) fresher ground
elsewhere in the 89-case table may now offer better returns than further
narrow pushes on the `DAT_140e1dbc4` thread specifically -- a genuine
judgment call for whoever picks this up next. All static-only. **Blocked
on nothing.**
