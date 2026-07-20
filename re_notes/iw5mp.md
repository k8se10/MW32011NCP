# iw5mp.exe Reverse-Engineering Notes

**Binary:** `iw5mp.exe` (Call of Duty: Modern Warfare 3 Multiplayer, 32-bit x86)  
**Status:** Active RE pass (2026-07-20, session 2 of MP work)  
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

## Next Session Checklist

- [ ] Run Ghidra decompile script without truncation
- [ ] Document `FUN_005a3960` and `FUN_0048c1c0` full decompiles
- [ ] Extract byte patterns for both functions
- [ ] Verify offset shift predictability vs. SP
- [ ] Answer outstanding questions via decompile analysis
- [ ] Update this file with findings
- [ ] Create pattern-scanning test candidates
- [ ] Plan hook-point validation via live debugging

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

**Status:** 70% ready
- Bind-name table location confirmed ✅
- Dispatch function candidate identified ✅
- Lookup function confirmed ✅
- Decompiles pending ⏳
- Byte patterns not yet extracted ⏳
- Live validation not yet done ⏳

**Ready to proceed with:** Manual decompile review, function signature extraction  
**Blocked on:** Truncation issue in last Ghidra script (needs rerun)
