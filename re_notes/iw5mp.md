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
decompile + caller trace to be UI/menu display code, not the gameplay dispatcher** —
the real MP input dispatcher remains genuinely unfound. Consistent with this project's
own locked scope decision (SP + Survival first, then port to MP) — this is still a
research-only detour, not a violation of it, and MP implementation genuinely has not begun.  
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

## Next Session Checklist (revised 2026-08-19 after Session 3's correction)

- [x] Run Ghidra decompile script without truncation
- [x] Document `FUN_005a3960` and `FUN_0048c1c0` full decompiles
- [x] ~~Extract byte patterns for both functions~~ — N/A for `FUN_005a3960` (confirmed UI code, not a hook target); `FUN_0048c1c0`'s role still unconfirmed, revisit once its own callers are traced
- [x] ~~Verify offset shift predictability vs. SP~~ — N/A, no SP analog for a Controls-menu display helper
- [ ] Answer outstanding questions via decompile analysis — **restarted**, see below
- [x] Update this file with findings
- [ ] Create pattern-scanning test candidates — blocked on finding the real dispatcher first
- [ ] Plan hook-point validation via live debugging — out of scope until this project's ordering/CVP gating is resolved (see CLAUDE.md's locked scope decisions)

**New checklist, static-only:**
- [ ] Trace `FUN_0048c1c0`'s own callers (`FindCallers.java` on `0048c1c0`) — is it part of the real dispatcher, the same UI pathway as `FUN_005a3960`, or both?
- [ ] Decompile `FUN_0048c220` (the inner primitive `FUN_0048c620` calls) — likely the real raw-keynum-for-bind-name resolver, may lead toward the actual dispatcher
- [ ] Find MP's real per-frame key-event handler (SP's analog: `FUN_00541020`) by searching for where ESCAPE (`0x1b`) is hardcoded, same technique that found SP's handler — this is the most promising path to the real dispatcher, since SP's dispatcher was found by following that handler's call chain, not by reference-density guessing
- [ ] Resolve the unnamed global `DAT_007ef050` string (one of the four alias-cluster trigger names in `FUN_005a3960`)
- [ ] Re-derive the bind-name table's exact base address and stride (4-byte vs 8-byte) now that the entry-count bound (91) is known but doesn't cleanly fit the previously-eyeballed 300-byte range

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

**Status:** ~25% ready (revised down 2026-08-19 — Session 2's "70%" counted an
unconfirmed dispatch candidate as found; it wasn't)
- Bind-name table location confirmed ✅ (base address needs re-verification, see checklist — off by at least 4 bytes from what was recorded)
- Movement/look/orchestrator functions matched to SP equivalents ✅ (Session 1, unaffected by this correction)
- Dispatch function candidate identified ❌ — **refuted this session; real dispatcher unfound**
- Lookup function (`FUN_0048c1c0`) — decompiled, but its role in real input handling still unconfirmed
- Decompiles for `FUN_005a3960`/`FUN_0048c1c0`/`FUN_0048c620`/`FUN_005c2a80` ✅ complete, no truncation
- Byte patterns extracted: none — nothing confirmed as a real hook target yet
- Live validation: not started, and out of scope until static analysis actually locates the dispatcher

**Ready to proceed with:** Tracing `FUN_0048c1c0`'s callers, decompiling `FUN_0048c220`, and searching for MP's real per-frame key-event handler (see "New checklist, static-only" above) — all still static/offline work.  
**Blocked on:** Nothing tooling-wise (the truncation issue from Session 2 is resolved — writing decompiles directly to a file via `DecompileFuncs.java` never truncates); blocked only on the actual RE work of finding the real dispatcher.
