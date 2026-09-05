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

## Status: Movement/look pipeline now FOUND and confirmed (Session 2), on top of Session 1's fully-mapped key-event -> gameplay-bind dispatch chain. Both pillars of "on par with x86 MP" are now static-confirmed. ADS candidate (case 0x43/0x44) upgraded with real independent corroborating evidence; killstreak/loadout-slot candidate (cases 0xf-0x1a) reinforced by a second, independent function cluster. No case is asserted "confirmed by name" beyond the 4 already found via literal command string in Session 1.

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
| 0x43 (down) / 0x44 (up) | `0x140e1dcf4` (SAME kbutton as case 0xd!) | single, shared kbutton, down-edge also toggles `DAT_140e1df60` | **Upgraded this pass (section 4-1 below) with real, independent corroborating evidence from the confirmed movement pipeline, not just table position -- still not name-confirmed, but the strongest-supported non-command-string case in this whole table.** |
| 0x47/0x48 (down/up) | none (calls `FUN_140090c80`/`FUN_140090d20(player)` directly, no kbutton at all) | one-shot start/stop pair | A distinct case TYPE, not a raw kbutton -- worth remembering when a future pass extends this table, since not every case follows the kbutton pattern. |
| 0x4b (down only, no matching up) | `FUN_1400d71b0(player)` | one-shot | -- |
| 0x4c / 0x50 | `FUN_14007f5b0(player, 1)` / `FUN_14007f5b0(player, 0)` | boolean set/clear | -- |
| **0xf/0x10 .. 0x19/0x1a** (6 parameterized slots) | `FUN_14007c5b0(player, N)` / `FUN_14007c760(player, N)`, N=0..5 | **parameterized slot family, 6 slots** | **First real look taken this pass (Session 2, section 4-2 below) -- structural read REINFORCED, not refuted, by a second independent function cluster (HUD-prompt rendering).** Directly relevant to this project's own stated MP motivation. |
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

**Verdict**: the structural read from Session 1 (an item/equipment-slot
system, killstreak-shaped) is **reinforced, not refuted** -- two
independent function clusters (gameplay dispatch AND HUD-prompt rendering)
both consume the same three globals the same way, and the item-name-prefix
check confirms real game-item references are involved. **Still not
resolved to "killstreak specifically" vs. "general equipment/lethal-
tactical/killstreak slots collectively"** -- no `"killstreak"`-literal
string anchor was found pointing directly at this cluster this pass, and
the type-1/2/3 per-slot distinction (cycle-with-hold vs. single-delegate
vs. flag-set) hasn't been mapped to real category names. A reasonable
next-session target, not resolved further here per the task's own scope.

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

### Open items, movement/look

- The final compressed-angle write in `FUN_1400d0050` (`param_2+0x20`/
  `+0x22`) has not been reconciled against `FUN_1400d0350`'s own
  `+0x26`-`+0x2a` angle-byte writes -- both write SOME angle-related field of
  the same `usercmd_t`, but whether they're the same logical field (written
  from two different code paths depending on freelook state) or genuinely
  different fields (e.g. a compressed short-angle vs. the final per-axis
  bytes) is not resolved. Worth a focused pass before any hook is written on
  either.
- `DAT_140e1dbb0` (freelook-mode-branch byte in `FUN_1400d0050`) vs.
  `DAT_140e1dbc4` (angle-speed-scale byte in `FUN_1400d0be0`, and the
  per-player mirror compared against `DAT_140e1df60` in `FUN_1400cfce0`,
  see section 4 below) are 20 bytes apart in the same small per-player
  state cluster (`~0x140e1dbb0`-`0x140e1dbc4`) -- plausibly related fields
  of one small "input mode" struct, not investigated further this pass.
  Do not assume they're the same field.

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

**Status: ~60% ready** (updated Session 2 -- both the button/dispatch chain
AND the movement/look pipeline are now static-confirmed, matching or
exceeding x86 MP's own "~55%" figure reached after 6 sessions, in two
sessions here. The remaining gap is case-to-real-bind-NAME confirmation for
the movement-critical dispatch cases, and two open reconciliation items in
the movement pipeline itself -- not missing mechanism, missing final
identity/detail confirmation).

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

**Ready to proceed with**: reconciling the two open items in section 6
(the second angle-write site, and the `DAT_140e1dbb0`/`DAT_140e1dbc4`
field-identity question), taking a full pass at the remaining ~75
undissasembled dispatch cases the same way x86 MP's own Session 6 checklist
called for, and/or extending the killstreak-slot investigation (section 4-2)
toward a real category-name anchor -- all static-only next steps, no live
process needed for any of them. **Blocked on nothing** -- this remains a
continuation task, not a stalled one.
