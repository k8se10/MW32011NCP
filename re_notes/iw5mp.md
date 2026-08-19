# iw5mp.exe Reverse-Engineering Notes

**Binary:** `iw5mp.exe` (Call of Duty: Modern Warfare 3 Multiplayer, 32-bit x86)  
**Status (Investigating, updated 2026-08-19):** Exploratory research only, NOT an active
implementation effort — **zero MP-specific hook or signature-scan code exists anywhere
in `proxy_d3d9/src/`** as of this update. Explicitly re-authorized as a narrow,
**static/offline-analysis-only** exception (2026-08-19: "lets do as much static as
we can") to the locked "SP + Survival first, then MP" ordering — Ghidra/disassembly
work only, no live process, no debugger attach, no network connection, no
implementation. See Session 3 below for a major correction to Session 2's dispatch-
function hypothesis: **the prior candidate (`FUN_005a3960`) is now confirmed via full
decompile + caller trace to be UI/menu display code, not the gameplay dispatcher.**
**Session 4 (same day) found MP's real key-event handler (`FUN_0048d120`) and its full
ESCAPE/pause-menu call chain, structurally confirmed** — but the general per-button
*gameplay* dispatcher (SP's `FUN_00438710` analog, for ADS/Sprint/Reload/etc.) is still
genuinely unfound; evidence now points to it not existing in this key-event chain at all
(see Session 4's closing analysis). Consistent with this project's own locked scope
decision (SP + Survival first, then port to MP) — this is still a research-only detour,
not a violation of it, and MP implementation genuinely has not begun.  
**Scope:** Controller input architecture, signature-scanning targets, hook points

---

## Session 1 Summary (2026-07-19)

See `iw5sp.md` section "MP (`iw5mp.exe`) foundational RE -- STATIC RESEARCH ONLY" for foundational findings. Key reconfirmations:

- **Architecture matches SP:** Same engine, proxy-DLL-injectable, identical usercmd_t struct
- **Per-frame pipeline found:**
  - `FUN_00489c40` — writes forwardmove/rightmove at `+0x1c`/`+0x1d` (same offsets as SP)
  - `FUN_0048a5d0` — orchestrator, assembles kbutton flags, writes angles at `+0x26`-`+0x2a`
  - `FUN_004896c0` — angle accumulator update via arrow keys/gamepad
  - `FUN_00489ba0` — raw mouse delta source (sensitivity/accel scaling)
- **Real structural difference:** MP's `FUN_00489c40` branches on freelook mode (mouse Y as movement vs. look pitch)
- **Menu/zone-loading:** Not fully located; expected but not confirmed

---

## Session 2 Analysis (2026-07-20) — Bind Table & Dispatch Discovery

### Bind-Name Table Located

**Address Range:** `0x008aa3bc` – `0x008aa4e8` (~300 bytes)  
**Structure:** Same as SP (8-byte stride, string-table format)  
**Confirmed References:** All 5 core bind-name strings found via Ghidra string search

### Dispatch Function Identified

**Primary Candidate:** `FUN_005a3960`  
- **Evidence:** References `+breath_sprint` exactly 4 times (high density, matches handler pattern)
- **Structural role:** Acts as bind-name → action dispatcher (same concept as SP's `FUN_00438710`)
- **Status:** Decompile pending (last script truncated mid-output)

### Lookup/Resolver Function

**Function:** `FUN_0048c1c0`  
- **Evidence:** Walks bind-name table via string comparison (confirmed via script output)
- **Structural role:** Maps bind names to case numbers or action IDs
- **Cross-reference:** Decompile output confirmed matching table dump

### Critical Architectural Difference: Hold Breath

**Finding:** MP has separate `+holdbreath` / `-holdbreath` binds (distinct from Sprint)

**SP behavior (known):** Hold Breath folded into Sprint; Y hold either spawns weapon-next or triggers Survival ready-up  
**MP behavior (confirmed):** Separate, explicit hold-breath binds (likey L3 dedicated, same as console)

**Implication:** MP's button-mapping architecture may differ from SP's; Hold Breath is a first-class bind, not a hack.

---

## Immediate Work: Complete Decompile Analysis (Live Verification Required)

### Critical Next Step: Verify Dispatch Function Identity

**Current evidence for FUN_005a3960:**
- References `+breath_sprint` 4x (high density)
- Located at bind-table adjacent address range
- Candidate density matches handler patterns

**What we MUST verify via decompile:**
1. **Function signature** — parameters, calling convention
2. **Case structure** — is it a switch/jump table dispatcher?
3. **Parameter passing** — how does it receive the bind ID/case number?
4. **Real vs. false positive** — confirm it's not just a string matcher, but the actual dispatcher

**Live debugging verification needed:**
- Set breakpoint at 0x005a3960
- Trigger a button press in-game
- Confirm breakpoint fires and parameters match expected dispatcher layout

**Why this matters:** SP's dispatcher is a 77-entry switch; if MP's is fundamentally different (e.g., uses a lookup table instead of switch), our hook strategy changes entirely.

### 2. Identify Hook Targets

Based on SP's precedent, expect these functions to exist in MP:

| Function Type | SP Reference | MP Status | Hook Target? |
|---|---|---|---|
| Movement (forwardmove/rightmove) | `FUN_0057d430` @ `0x00:57d430` | `FUN_00489c40` @ `0x00:489c40` | ✅ Yes — post-hook to add stick input |
| Angle accumulator update | `FUN_0057d680` (raw mouse deltas) | `FUN_00489ba0` (confirmed) | ✅ Yes — inject right-stick look |
| Orchestrator/finalize | `FUN_0057de60` | `FUN_0048a5d0` (confirmed) | ✅ Yes — per-frame hook point |
| Dispatch handler | `FUN_00438710` | `FUN_005a3960` (candidate) | ❓ Conditional — depends on button architecture |
| Bind lookup | N/A documented | `FUN_0048c1c0` | ❓ Conditional — may not need hooking |

### 3. Extract Byte Patterns

Once decompiles are confirmed, generate signature patterns for:
- `FUN_005a3960` (dispatch)
- `FUN_0048c1c0` (lookup)
- Plus any additional helpers discovered in decompile

**Pattern Extraction Technique:**
1. Identify function prologue (unique instruction sequence)
2. Extract first 16-20 bytes with wildcards for variable immediates
3. Validate uniqueness across the binary
4. Document any register-usage differences vs. SP equivalents

### 4. Cross-Reference SP Findings

**Verify offset shifts are predictable:**
- SP movement: `FUN_0057d430` → MP: `FUN_00489c40` (shift: -0x00f43f0)
- SP mouse delta: `FUN_0057d680` → MP: `FUN_00489ba0` (shift: -0x00ee4e0)
- **Pattern:** If offsets shift by a consistent delta, reverse-engineering is structurally sound

### 5. Button Mapping Differences

**Key questions to answer from decompiles:**

1. **Hold Breath mechanism:**
   - Is `+holdbreath` implemented as a real kbutton (like ADS in SP)?
   - Or as a one-shot command (like Y's weapon-next in SP)?
   - Does it have native duration/recovery or is it state-based?

2. **Sprint architecture:**
   - Does MP implement Sprint's pm_flags bit the same way as SP?
   - Or does MP have a different sprint throttle/cooldown mechanism?
   - Is Extreme Conditioning perk handled the same way?

3. **Dispatch case mapping:**
   - Does `FUN_005a3960` use the same case-number scheme as SP's `FUN_00438710`?
   - Are bind indices and case numbers 1:1, or does MP use a different mapping?
   - This is critical for verifying the Back regression lesson applies equally to MP.

---

## Known Structural Similarities

### Confirmed (Cross-Session)

| Component | SP Finding | MP Finding | Status |
|---|---|---|---|
| Controller import path | None (hardcoded keyboard/mouse only) | None | ✅ Matches |
| `d3d9.dll` import | Yes (proxy surface exists) | Yes | ✅ Matches |
| `usercmd_t` layout | 0x40 bytes, forwardmove @ +0x1c, rightmove @ +0x1d | Same offsets observed | ✅ Matches |
| Angle accumulators | Pitch/yaw at +0x26-0x2a, fixed-point | Same offsets | ✅ Matches |
| Boot-time dvar registration | `FUN_00498d10` (SP) | `FUN_00492560` (MP) | ✅ Matches (different offset) |
| Bind-name table | `0x00929fa0`, 81 entries, 8-byte stride | `0x008aa3bc`, ~300 bytes, 8-byte stride | ✅ Matches (different offset) |
| Dispatch function | `FUN_00438710` (77-entry switch) | `FUN_005a3960` (candidate, density match) | ⏳ Pending decompile |

### Differences (To Document)

| Component | SP | MP | Impact |
|---|---|---|---|
| Freelook mode branching | Not explicitly documented | Present in `FUN_00489c40` | Minor — affects mouse-Y behavior, not stick input |
| Hold Breath bind | Folded into Sprint (Y) | Separate `+holdbreath` / `-holdbreath` | Medium — button mapping may differ |
| Menu/zone loading | `FUN_00428010` family (partially documented) | Not yet located | Medium — affects menu nav work, post-scope for input hooks |

---

## Outstanding Questions

1. **What is `FUN_005a3960` decompiled code?**
   - Confirm it's the dispatcher (not a false positive)
   - Identify parameter format and case ranges
   - Compare structure to SP's `FUN_00438710` for pattern analysis

2. **Are there other dispatcher variants for MP-specific commands?**
   - Lobbies, party systems, online matchmaking
   - Confirm these don't interfere with controller input dispatch

3. **Is the 300-byte bind table fully populated?**
   - SP's `0x00929fa0` covers 81 entries (8-byte each = 648 bytes theoretically, but sparsely populated in practice)
   - MP's `0x008aa3bc` covering ~300 bytes suggests ~37-40 entries
   - Is MP using fewer binds, or are they packed differently?

4. **Does MP have a separate real kbutton for Sprint?**
   - Or does it also use the pm_flags workaround (as SP discovered post-2026-07-15)?
   - Critical for live testing parity.

---

## Session 3 Analysis (2026-08-19) — Dispatch Candidate Refuted, Real Dispatcher Still Unfound

**Method:** Static-only, per the 2026-08-19 scope authorization. Reused the existing
dedicated MP Ghidra project (`D:\Tools\ghidra_projects_mp\iw5mp.gpr`, program `/iw5mp.exe`,
already imported from a prior pass) headless via `analyzeHeadless.bat ... -process
iw5mp.exe -readOnly -noanalysis`, running this repo's own `DecompileFuncs.java` and
`FindCallers.java`. No live game process, no debugger, no network connection at any point.

### `FUN_005a3960` is NOT the dispatcher — full decompile obtained, hypothesis refuted

Session 2 flagged `FUN_005a3960` as the dispatch-function candidate purely on a
reference-density heuristic ("references `+breath_sprint` exactly 4 times"), explicitly
un-confirmed pending decompile. **The full decompile (no truncation) refutes this.**

Real structure: `FUN_005a3960(param_1, param_2)` string-compares an incoming bind-name
(passed via `unaff_EDI`, consistent with this project's established "custom
register-passed args" pattern for this function family) against a small fixed set of
literal strings — `DAT_007ef050` (an unresolved global, likely `"+changezoom"`'s twin
or a related alias, not yet named), `"+sprint"`, `"+holdbreath"`, and falls through to a
`"+changezoom"` compare — and for whichever one matches, selects a short NULL-terminated
list of 2 *canonical* bind names to try in priority order:

- match on `DAT_007ef050` → try `{"+melee_breath", "+melee_zoom"}`
- match on `"+sprint"` → try `{"+breath_sprint", "+sprint_zoom"}`
- match on `"+holdbreath"` → try `{"+melee_breath", "+breath_sprint"}`
- match on `"+changezoom"` (else branch) → try `{"+melee_zoom", "+sprint_zoom"}`
- no match at all → return 0

For each candidate in the selected list it calls `thunk_FUN_0048c620(param_1, name,
param_2)`, returning the first nonzero result. This reads as a **context-sensitive
alias resolver for a genuinely overlapping bind cluster** (Hold Breath / Sprint /
Melee-Zoom sharing physical inputs on console, matching known real MW3 MP behavior),
not a general-purpose 77-entry switch like SP's `FUN_00438710`.

**Confirmed definitively via its only caller**, found with `FindCallers.java`:
`FUN_005a3ac0` — this function calls `thunk_FUN_0048c620(param_1)` first, falls back to
`FUN_005a3960(param_1, local_100)` only if that returns 0, then **builds a display
string** for the Controls options menu: `FUN_0058f760("KEY_UNBOUND", param_3)` if
nothing's bound, or — for a dual-bind case — formats `"%s %s %s"` with a
`FUN_0058f760("KEY_OR")` separator (i.e. renders text like `E OR Q`) via
`FUN_005c2cc0`. **This is UI code that renders "what key(s) are bound to this action"
in the options menu — not gameplay-time input dispatch.** `FUN_005a3960` has exactly
one caller, so this isn't ambiguous.

**Corrected conclusion**: Session 2's `FUN_005a3960` candidate is **discarded**. The
real MP bind-name→action dispatcher (the actual analog of SP's `FUN_00438710`) has
**not yet been located** — this is a genuine dead end, not a solved item, and should
be treated as unfound going into any future session, not silently assumed to be
`FUN_005a3960` by a reader skimming only the top of this file.

### Two real primitives identified along the way

- **`FUN_0048c620(param_1, param_2, char* param_3)`** — confirmed `Key_KeynumToBindString`-
  style helper: writes up to two formatted key-name strings into `param_3`/`param_3+0x80`
  (128-byte stride each) and returns `0` (unbound) / `1` (single key) / `2` (dual key).
  Calls an inner `FUN_0048c220()` (not yet decompiled) to get the raw keynum(s) first.
  Part of the same Controls-menu display pathway as `FUN_005a3ac0` above, not a
  gameplay-time primitive.
- **`FUN_005c2a80(param_1, param_2)`** — thin wrapper: `FUN_005c2970(param_1, param_2,
  0x7fffffff)`, i.e. an unbounded-length string-compare-style primitive. Used by
  `FUN_0048c1c0`'s lookup loop (below).

### Bind-table lookup (`FUN_0048c1c0`) — refines the table size, role still unconfirmed

Full decompile obtained: `FUN_0048c1c0(param_1)` linearly scans a pointer table at
`PTR_DAT_008aa3b8` — **4 bytes before** the previously-documented bind-name table start
(`0x008aa3bc`), so this is the same table (or its true base is `0x008aa3b8`, not `...3bc`
— worth re-checking the exact base in a future pass) — calling `FUN_005c2a80(param_1,
table[i])` for each entry, returning the matching index, up to a **hard bound of `0x5b`
(91 decimal) entries**. This refines Session 2's ~37-40-entry guess (which assumed an
8-byte stride over the ~300-byte range originally eyeballed) — 91 entries suggests a
**4-byte stride** (a flat pointer array, `PTR_DAT_...` naming is consistent with this),
not the 8-byte paired stride SP's table uses. **Not yet confirmed**: whether
`FUN_0048c1c0` itself is called from the real gameplay dispatcher, the same UI-display
pathway as `FUN_005a3960`, or both — its own callers were not traced this pass.

### What this means for the checklist below

Items 1-3 (decompile, dispatch-structure determination, byte-pattern extraction) are
**complete for `FUN_005a3960` specifically, with a negative result** — no signature
extraction was done for it, since it's confirmed UI code, not a hook target. Item 4
(SP→MP offset-shift check) doesn't apply — there's no SP analog on record for a
Controls-menu key-display helper to compare against. Item 5 (outstanding questions)
is unresolved — the real dispatcher search restarts from scratch. This is a real,
honest setback, not a wasted pass: it prevents any future session from building a
hook on top of what would have been the wrong function.

---

## Session 4 Analysis (2026-08-19, same day) — Real Key-Event Handler & Menu Chain Found; General Dispatcher Still Missing

**Method:** Static-only, same constraints as Session 3 — Ghidra headless
(`analyzeHeadless.bat ... -process iw5mp.exe -readOnly -noanalysis`) against the
existing `D:\Tools\ghidra_projects_mp\iw5mp.gpr` project, running `FindConstantRefs.java`,
`DecompileFuncs.java`, and `FindCallers.java`. No live game process, no debugger, no
network connection at any point.

### Technique note: the SP string-anchor method didn't transfer, byte-level constant scan did

Tried repeating SP's exact discovery method first — `FindExactStrings.java` for
`"CL_KeyEvent_Add"`/`"CL_KeyEvent_Sub"`/`"CL_KeyEvent_Mul"` (the cvar-name strings that
led to SP's `FUN_00541020`) — **zero matches in `iw5mp.exe`**. These appear to be
SP/Campaign-specific debug cvars not compiled into the retail MP build, not a naming
convention that transfers. Fell back to `FindConstantRefs.java` scanning every
instruction operand in the whole binary for the literal scalar `0x1b` (ESCAPE) — a
blunter but binary-agnostic technique. This produced 22 `CMP reg, 0x1b`-style hits;
narrowed by address-locality to the two closest to the already-confirmed movement/look
cluster (`0x00489xxx`-`0x0048cxxx`): `FUN_0048d070` and `FUN_0048d120`, both immediately
adjacent. This locality heuristic (same-source-file functions cluster in the compiled
binary) is the same one already validated for the confirmed movement/look/orchestrator
functions — worth keeping as a general MP-RE technique going forward, not just for this
one lookup.

### `FUN_0048d120` confirmed as MP's real key-event handler (SP's `FUN_00541020` analog)

**`FUN_0048d120(int param_1, int param_2, int param_3)`** — `param_1` = playerIndex,
`param_2` = raw keycode, `param_3` = isDown. Structural confirmation, not a heuristic
guess: ESCAPE (`0x1b`) is checked and special-cased at three separate points (lines
102-104, 148, 233-235 of the decompile) against a game-state value read from
`DAT_010625f4`, exactly mirroring SP's `FUN_00541020` pattern of hardcoding ESCAPE
against a state/gate check rather than routing it through any generic string-name
dispatcher. Also hardcodes the console/tilde key (`0x60`/`0x7e`) the same way SP does.
Non-special keys fall through to a generic path (line 194) that calls
`FUN_006ada70(param_1, param_2, param_3)` — see below, this is NOT the general gameplay
dispatcher despite being the natural next hop.

**Full ESCAPE/pause-menu call chain mapped, each function decompiled and structurally
confirmed (not guessed):**

| Role | SP equivalent | MP function | Confirmed behavior |
|---|---|---|---|
| Key-event handler (ESCAPE hardcoded here) | `FUN_00541020` | `FUN_0048d120` | 3-arg (playerIndex, keycode, isDown); ESCAPE + `~` special-cased against `DAT_010625f4` game-state |
| Open pause menu | `FUN_004d6620` | `FUN_0048f050` | Reads `DAT_010625f4`; if `==1` calls a cleanup (`FUN_004860e0`), else zeroes a per-player state row; dispatches into `FUN_0058ef50` with mode 0 or 1 |
| Menu-mode/state dispatcher | `FUN_004396d0` (mode 0=resume, mode 2=open) | `FUN_0058ef50(param_1, param_2, mode)` | Real `switch(mode)`, 11 real cases decompiled — see below |
| Forward key to active menu / unpause | `FUN_004d9850` | `FUN_00592820` | Checks gate bit `0x10` via `FUN_0048c700(...,0x10)`; if menu active, forwards through `FUN_005ac2c0`; else clears the gate bit, sets `cl_paused 0`, and calls `FUN_0048d5f0` (unpause path) |

**`FUN_0058ef50`'s mode switch, fully decompiled — the real MP menu-state map:**
mode 0 = resume/unpause (clears gate bit `0x10`, `cl_paused 0`), mode 1 = open main
pause menu (with `com_errorMessage`-driven error-popup branching), mode 2 = disconnect/
demo-playback handling, mode 3 = `pregame_loaderror_mp` popup, mode 6 = scoreboard, mode
7/8/9 = legacy Xbox Live lobby/private-lobby menu screens (`menu_xboxlive`,
`menu_xboxlive_lobby`, `menu_xboxlive_privatelobby` — real leftover console-menu string
names, still present and reachable in this PC build), mode 10 = `ingame_migration`
(host-migration UI), mode `0xb` = `popup_vault` (create-a-class/loadout screen). This is
a genuinely useful map for any future menu-navigation work (item 4 in this project's own
architecture doc), independent of the controller-input-dispatch question this session
was actually chasing.

### `FUN_006ada70` is NOT the general gameplay dispatcher — it's Killcam/Theater-mode only

The natural next hop from `FUN_0048d120`'s generic (non-ESCAPE) key path. Full decompile
shows a real `switch(param_2)` over specific keycodes (`0x20` space, `0x72` 'r', `0x9a`-
`0xab` zoom/playback-speed float adjustments, `200`/`0xc9` numeric-row keys) — **but the
entire switch is gated on `DAT_010625f4 == 0xb`**, the same state value `FUN_0058ef50`
uses for the Vault/create-a-class popup case, but used here as a *precondition* rather
than a *target* — meaning this dispatcher only does anything when the game is in a
specific non-gameplay state. The case semantics (play/pause, rewind/fast-forward zoom
level adjustment via float accumulation, `FUN_006aaaa0` state-machine calls) read as
**Killcam/Theater/replay-mode controls**, not live multiplayer gameplay input. Confirmed
functionally inert for regular gameplay: outside state `0xb` every case falls through
and the function returns without effect.

### Bind-name lookup pathway fully mapped — confirmed UI-only, and a second, separate table found

`FindCallers.java` on `FUN_0048c1c0` found exactly 3 callers, all decompiled:

- **`FUN_0048c220(int param_1)`** (the Session 3 checklist item) — `__fastcall`, calls
  `FUN_0048c1c0()` to resolve a bind-name to an internal ID, then linearly scans a
  **256-entry, 3-dword-stride** table at `&DAT_00b3c768 + param_1*0x34b` for up to 2
  matching raw keycodes. **This is a second, distinct table from the 91-entry
  `PTR_DAT_008aa3b8` name table** — it's the direct structural analog of SP's
  `DAT_00a98e4c` (256 entries, 3-dword stride, per-player row) used for
  `Key_KeynumToBindString`-style "what key is this bound to" queries. MP's per-player
  row stride (`0x34b`) is one dword off SP's (`0x34a`) — consistent with the two being
  independently-compiled analogs of the same underlying structure, not shared addresses.
- **`FUN_005a3e10`** — a bind-*rebinding* state machine (`DAT_05989878`/`DAT_05989880`
  toggle flags gate a "waiting for a key to assign" mode), calls `FUN_0048c1c0` to
  resolve the target bind's ID before writing a new key assignment via `FUN_0048c180`.
  Confirms this whole pathway is the Controls-menu's interactive rebind flow, not
  anything gameplay-time.
- **`FUN_005ac2c0`** — the real find here: this is called directly from `FUN_00592820`
  (the "forward key to active menu" function above) whenever a menu is open, and its
  full decompile is a genuine **menu-navigation key router** — real cases for Enter/
  select (`0xd`/`0xbf`/`0xca`), ESCAPE-within-menu, D-pad-like navigation-adjacent
  keycodes (`0x9a`/`0x9c`/`0xb7`/`0xce` and `0xcd`/`0xce` pairs), tab/switch (`200`/
  `0xc9`), and developer-only console/screenshot toggles (`0xb1`/`0xb2`, gated on the
  `"developer"` dvar). **This is the real menu/UI navigation dispatcher this project's
  own architecture doc (item 4, "still not implemented beyond Start's open/close") has
  been looking for** — a concrete, structurally-confirmed target for that future work,
  found as a byproduct of this session's actual goal rather than the goal itself.

### Where this leaves the actual gameplay-dispatcher hunt

**Genuinely still unfound, and the evidence now suggests a different conclusion than
"it exists somewhere in this chain and hasn't been found yet."** Every function reached
from `FUN_0048d120`'s key-event path is either ESCAPE/pause-menu-specific
(`FUN_0058ef50`, `FUN_0048f050`, `FUN_00592820`), menu-navigation-specific
(`FUN_005ac2c0`), Controls-menu-display/rebind-specific (`FUN_0048c1c0`/`FUN_0048c220`/
`FUN_0048c620`/`FUN_005a3ac0`/`FUN_005a3960`), or Killcam/Theater-specific
(`FUN_006ada70`). None of them fire during ordinary live gameplay with a menu closed.
**The likeliest explanation, consistent with how SP's own ADS/Sprint/Reload were found
to work** (real `kbutton_t` `KeyDown`/`KeyUp` calls triggered by bind *execution*
— i.e. the Cbuf/Cmd string-command pathway running `"+ads"`/`"-ads"` etc. — not by
this discrete per-keycode special-case chain at all): MP's regular gameplay buttons
almost certainly bypass this entire key-event chain the same way, going through
`kbutton_t` state directly via bind-command execution. **Next static step, not yet
attempted**: locate MP's own `kbutton_t` array and `KeyDown`/`KeyUp`-style helper
functions directly (by analogy to how SP's ADS/Reload kbuttons were originally found —
memdiff/live-write-testing isn't available under the static-only constraint, so this
needs a structural approach: cross-reference the confirmed bind-name table entries
against candidate `kbutton_t`-sized/shaped global structures, or search for functions
with the same two-call-site KeyDown-then-KeyUp shape SP's confirmed kbuttons have),
rather than continuing to chase the key-event special-case chain further — that chain
has now been explored close to exhaustively and doesn't lead there.

---

## Next Session Checklist (revised 2026-08-19 after Session 4)

- [x] Run Ghidra decompile script without truncation
- [x] Document `FUN_005a3960` and `FUN_0048c1c0` full decompiles
- [x] ~~Extract byte patterns for both functions~~ — N/A for `FUN_005a3960` (confirmed UI code, not a hook target); `FUN_0048c1c0` also confirmed UI/rebind-only, not a gameplay hook target
- [x] ~~Verify offset shift predictability vs. SP~~ — N/A, no SP analog for a Controls-menu display helper
- [x] Answer outstanding questions via decompile analysis — the dispatcher question specifically restarts from a different angle, see below
- [x] Update this file with findings
- [ ] Create pattern-scanning test candidates — still blocked on finding the real gameplay dispatcher/kbutton mechanism first
- [ ] Plan hook-point validation via live debugging — out of scope until this project's ordering/CVP gating is resolved (see CLAUDE.md's locked scope decisions)
- [x] Trace `FUN_0048c1c0`'s own callers — done, all 3 confirmed UI/menu-nav/rebind, not gameplay dispatch
- [x] Decompile `FUN_0048c220` — done, confirmed `Key_KeynumToBindString`-style helper over a second, 256-entry raw-keycode table
- [x] Find MP's real per-frame key-event handler (SP's analog: `FUN_00541020`) — **found: `FUN_0048d120`**, full ESCAPE/pause-menu chain mapped

**New checklist, static-only, Session 5:**
- [ ] **Highest priority**: locate MP's `kbutton_t` array and `KeyDown`/`KeyUp` helper functions directly — the likeliest real home of gameplay button dispatch (ADS/Sprint/Reload/Fire/etc.), structurally separate from the key-event chain this session fully explored
- [ ] Resolve the unnamed global `DAT_007ef050` string (one of the four alias-cluster trigger names in `FUN_005a3960`, still unresolved from Session 3)
- [ ] Re-derive the bind-name table's exact base address and stride now that TWO separate tables are confirmed to exist (91-entry name table at `PTR_DAT_008aa3b8`; 256-entry raw-keycode table at `&DAT_00b3c768+param*0x34b`, stride 3) — don't conflate them in future notes
- [ ] `FUN_005ac2c0` (the confirmed menu-navigation key router) is a strong, ready-to-use future target for this project's still-unimplemented full menu/UI navigation work (architecture doc item 4) — worth flagging there, not just here

---

## Cross-Project Reference

**Netcode Security (MW32011NSP):**
- MP's bind-dispatch architecture is distinct from netcode parsing, but command-execution paths (Cbuf_AddText, Cmd_ExecuteString) are shared
- Any networked command injection vulnerabilities in MP would flow through the same dispatch family as controller input
- See MW32011NSP/re_notes/vulnerability_research.md for netcode-specific findings

**Plutonium & Third-Party Compatibility:**
- MP's bind table and dispatch offsets differ from SP's (`0x008aa3bc` vs. SP's `0x00929fa0`)
- Plutonium MP's `iw5mp.exe` may have different offsets again (byte-identical retail MP is the current assumption; needs verification once pattern-scanning is implemented)
- See known_issues.md for compatibility matrix

---

## Signature-Scanning Readiness

**Status:** ~35% ready (revised 2026-08-19, Session 4 — up from Session 3's 25%: a real,
structurally-confirmed function, `FUN_0048d120`, is now a genuine hook-target candidate
for menu/pause-state key handling, and a full menu-navigation dispatcher was located as
a byproduct; still capped well below 70% because the actual *gameplay*-button dispatch
mechanism — the highest-value target for this project's controller work — remains
unfound, and no byte-pattern signatures have been extracted for anything yet)
- Bind-name table location confirmed ✅ (base address still needs re-verification per checklist)
- Movement/look/orchestrator functions matched to SP equivalents ✅ (Session 1, unaffected by later corrections)
- Key-event handler (`FUN_0048d120`) ✅ — structurally confirmed (ESCAPE hardcoded against game-state, matching SP's `FUN_00541020` pattern), a real hook-target candidate for future pause/menu-state input work
- Full ESCAPE/pause-menu call chain (`FUN_0048f050`, `FUN_0058ef50`, `FUN_00592820`) ✅ decompiled and mapped, 11 real menu-mode cases documented
- Menu-navigation key router (`FUN_005ac2c0`) ✅ found and decompiled — a strong future target for this project's still-unimplemented full menu/UI navigation work (architecture doc item 4), found as a byproduct of this session
- Killcam/Theater-mode dispatcher (`FUN_006ada70`) ✅ identified and ruled out as the gameplay dispatcher
- Bind-name-to-key lookup pathway (`FUN_0048c1c0`/`FUN_0048c220`/`FUN_0048c620`/`FUN_005a3e10`) ✅ fully traced — confirmed Controls-menu display/rebind UI only, not gameplay dispatch; a second, distinct 256-entry raw-keycode table found in the process (SP `DAT_00a98e4c` analog)
- **General gameplay-button dispatcher (ADS/Sprint/Reload/Fire/etc., SP's `FUN_00438710` analog): still unfound** — this session's evidence now points toward it not existing in the key-event chain at all (see Session 4's closing analysis); next step is a direct `kbutton_t`-structure search, not further key-event-chain tracing
- Byte patterns extracted: none — nothing in the key-event chain is itself a gameplay hook target, so signature extraction stays deferred until the real kbutton mechanism is found
- Live validation: not started, and out of scope until this project's ordering/CVP gating is resolved

**Ready to proceed with:** Session 5's checklist above — primarily, locating MP's `kbutton_t` array/KeyDown-KeyUp helpers directly, since the key-event chain this session explored has been ruled out as the gameplay-dispatch mechanism. All still static/offline work.  
**Blocked on:** Nothing tooling-wise — blocked only on the actual RE work of finding the real gameplay-input mechanism, which this session's findings suggest lives in a structurally different part of the binary than where Sessions 2-4 were looking.
