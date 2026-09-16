# GSC-VM native function mapping — x64 (2026-09-16)

New investigation thread, following the 2026-09-16 policy reversal
(`CLAUDE.md`/`AGENTS.md`'s Plugin API section, "REVERSED, 2026-09-16") that
unblocked the main mod reading live GSC-VM/gameplay-entity state and
calling existing shipped GSC script functions through the game's own real
native APIs. This doc tracks the real, ongoing effort to find x64 SP
equivalents of the core GSC-VM primitives, using real signatures from a
public reference project as a starting point (addresses never transfer —
different binary, different architecture — only signatures/behavior do).

## Reference signatures (external, not this binary)

From `alicealys/iw5-gsc-utils` (a Plutonium MW3-*multiplayer* native
plugin, `src/game/symbols.hpp`) — real, published, but for a **different
binary** (Plutonium's own MP client, x86) than this project's target
(`iw5sp.exe`, the Campaign/Survival x64 binary). Addresses below are
**reference-only, not usable directly**:

```cpp
WEAK symbol<void(unsigned int notifyListOwnerId, unsigned int stringValue,
    VariableValue* top)> VM_Notify{0x569720};
WEAK symbol<unsigned int(const char* str, unsigned int user)>
    SL_GetString{0x5649E0};
WEAK symbol<int(const char* filename, unsigned int str)>
    Scr_GetFunctionHandle{0x5618A0};
WEAK symbol<unsigned int(int handle, unsigned int objId,
    unsigned int paramcount)> Scr_ExecThreadInternal{0x56E1C0};
WEAK symbol<unsigned int(int localId, const char* pos,
    unsigned int paramcount)> VM_Execute{0x56DFE0};
```

## Status summary

| Primitive | x64 SP status |
|---|---|
| `Scr_GetFunctionHandle` | **Found, high confidence** — see below. Splits into two real functions, not a single 1:1 match. |
| `Scr_LoadScript` | **Found, high confidence** (round 2) — `FUN_140252210`, decisive evidence (real error strings). See below. |
| `SL_GetString` | Strong candidate (`FUN_140255e60`), corroborated further in round 2 (a case-insensitive wrapper around it is exactly the shape a field-name lookup needs) — still not independently confirmed as the fully generic path. |
| `VM_Notify` | **Found, DEFINITIVE confidence** (round 3) — `FUN_140261e10`, found via the real `notify` opcode's own handler. See below. |
| `VM_Execute` | **Found, DEFINITIVE confidence** (round 2, coordinator direct) — `FUN_14025e950`. See below. |
| `Scr_ExecThreadInternal` | **Found, high confidence** (round 4) — `FUN_140256bd0`/`FUN_140256890`. See below. |

## `Scr_LoadScript` — found, high confidence (round 2)

**Decisive evidence**: `FUN_140252210` logs `"Could not find script '%s'"` on
failure and `"MAX_PRECACHE_ENTRIES exceeded"` when precaching a script's own
dependencies, then runs the bytecode fixup pass (`FUN_14025b7f0`) and
resolves imports (`FUN_140251de0`) — both already-confirmed real members of
this VM cluster. This is genuinely the real script-load entry point, not a
guess from shape alone.

**Two real caller wrappers found**, forming the complete, public-facing
load path: `FUN_140252910`/`FUN_140252ac0` — both build a `"%s.gsc"`
filename, check a hashtable for an already-loaded copy, and only call
`FUN_140252210` on a genuine cache miss. Together, `FUN_140252910`/
`FUN_140252ac0` (cache-check, public entry) -> `FUN_140252210`
(`Scr_LoadScript` itself) -> `FUN_14025b7f0`/`FUN_140251de0` (fixup/import
resolution) is the complete, real, x64 script-loading subsystem, filename
in, cached-or-freshly-loaded script out.

## `Scr_GetFunctionHandle` — found, high confidence

**Real evidence chain, not guessed**: `FUN_140251de0`/`FUN_140251fc0` were
found via `MultiStringScan.java` against the literal error string
`"unknown function"` (3 real references). `FUN_140251fc0`'s sole caller,
`FUN_14025b7f0`, is a real GSC **bytecode fixup/resolution pass** — a
byte-stream walker with a switch over ~50 distinct opcode values (2-13
byte operand widths per opcode), patching resolved IDs back into the
bytecode in place at script-load time. `FUN_140251fc0` is called from
exactly the opcode groups `0x28-0x2a`/`0x65` and `0x2b-0x2e` — almost
certainly `CallBuiltin`/`CallFunc`/`CallFuncThreaded`-style "call a
function by name" instructions.

**The public API splits into two real x64 functions, not one**:
- `FUN_140255e60(const char* str, int flag)` — resolves a filename (read
  from the bytecode stream by `FUN_14025d410`, either an inline short
  index or a raw embedded string) into an internal file-context ID.
- `FUN_140251fc0(int fileContextId, uint funcNameStringId, byte* outSlot)`
  — resolves a function name (as an already-interned string ID) WITHIN
  that file context into a callable handle, writing the result to
  `outSlot`.

Together these two calls (`FUN_140255e60` then `FUN_140251fc0`) do the
same real work as the public `Scr_GetFunctionHandle(filename, str)` API,
just split across the load-time bytecode-fixup call sequence rather than
exposed as one function taking a raw filename string directly.

**Important limitation**: `FUN_140251fc0` has exactly ONE real caller
(`FUN_14025b7f0`, the bytecode fixup pass) — this is genuinely internal
script-LOAD-TIME linking logic, not an already-exposed "call this at
runtime from outside" API the way a plugin author would want. **This does
not block using it** — it's still a real, callable native x64 function
with a known signature; this project's own code can call it directly the
same way the bytecode fixup pass does, we just can't assume an EXISTING
external caller already proves the calling convention is safe from an
async/hook context (the same caution x86's own 2026-07-15 VM_Notify
investigation already flagged: "needs the live GSC VM's own execution
stack in a consistent state ... real engineering risk to call safely from
an independent, asynchronous per-frame hook rather than from within a
genuine GSC callback context" — that caution applies here too and hasn't
been resolved).

**Real next step, not yet done**: trace `FUN_14025b7f0`'s OWN callers (the
thing that invokes the bytecode fixup pass itself) to find whether there's
a more genuinely "public-facing" trigger point higher up the call chain
that would be safer to hook/call from than the raw internal resolver
functions.

## `SL_GetString` — strong candidate, not confirmed

`FUN_140255e60(const char* str, int flag)` (found as a byproduct of the
`Scr_GetFunctionHandle` investigation above) is flagged as a strong
candidate — it's the function that turns a raw string first encountered in
the bytecode stream into a small integer ID, which is exactly
`SL_GetString`'s real job. **Not independently verified** — its one
confirmed call site (from `FUN_14025d410`, resolving a FILENAME
specifically) might mean it's a filename-specific resolver rather than the
fully generic string-interning primitive every kind of script string
(function names, notify strings, dvar names, etc.) goes through. Needs a
dedicated caller-count check (`FindCallers.java`) and/or tracing a case
where it's clearly called with something OTHER than a filename before
this can be called confirmed.

**Ruled out this same investigation**: `FUN_1402574e0` (an early candidate
from the same loader-cluster neighborhood) is confirmed NOT `SL_GetString`
— it's a generic integer-keyed hashtable accessor (`(param_1*0x65 +
param_2*2) & 0xffff` hash, bucket-chain walk with move-to-front caching,
`ushort` handle out), never touches a `const char*` at all. 70 real
distinct callers throughout the binary (`FindCallers.java`) — far too
widely used and too generic-shaped to be any ONE of these specific VM
primitives; it's a shared low-level utility (hash-table lookup, likely the
same "object field/child variable" lookup scheme GSC's own documented
architecture uses) that many different higher-level functions call INTO,
`SL_GetString`/`VM_Notify` very possibly among them, but it is not either
one itself.

## `VM_Notify` — FOUND, definitive confidence (round 3)

**Rounds 1-2 (string-anchoring and caller-tracing) both came back empty**,
20 total string anchors tried across two passes, `FUN_140255e60`'s own 17
callers individually traced with no match — see the git history for the
full list if useful; not repeated here since round 3 superseded the
approach entirely.

**Round 3, the approach that actually worked: use a real, external,
independently-published GSC opcode table instead of guessing.**
`xensik/gsc-tool` (a real, actively-maintained open-source GSC compiler/
decompiler this project's own CLAUDE.md already lists as its standard GSC
decompilation tool) ships real per-engine, per-platform opcode tables as
plain source code — `src/gsc/engine/iw5_pc_code.cpp` (fetched via `gh api`
against the repo's real `dev` branch, its actual default branch) contains
the literal line:
```cpp
{ 0x51, opcode::OP_notify },
```
**`notify` is a genuine bytecode OPCODE (byte value `0x51`/81), not a
builtin function call** — confirmed directly from the real compiler's own
source, not guessed. This immediately let us jump straight to
`case 0x51:` inside the confirmed interpreter loop (`FUN_14025e950`,
found in the previous round) and read the REAL notify handler directly:

```c
case 0x51:
    ...
    fVar12 = *pfVar17;                  // pop: the notify target (self/entity)
    ...
    fVar11 = DAT_142476a18[-4];         // pop: the interned notify string ID
    DAT_142476a18 = DAT_142476a18 + -8;
    ...
    FUN_140261e10(fVar12, fVar11, DAT_142476a18);   // <-- the real call
```

**`FUN_140261e10` is `VM_Notify`'s real x64 equivalent** — the three
arguments match the real published signature exactly: `(entity/owner ID,
interned string ID, VM-stack-pointer-as-params)` maps directly onto
`VM_Notify(unsigned int notifyListOwnerId, unsigned int stringValue,
VariableValue* top)`. Found via the most direct evidence this
investigation could have asked for: the actual bytecode handler for the
actual opcode, not an inference from string anchors or caller shapes.

**Methodological note for any future primitive-hunting in this VM**: this
approach (find the real opcode byte value from `gsc-tool`'s own source,
then jump straight to that `case` inside the confirmed interpreter loop)
is dramatically more direct and reliable than string-anchoring or
structural guessing, and should be the FIRST technique tried for any
future GSC-VM primitive this project needs, now that the interpreter
itself is confirmed and mapped. `gsc-tool`'s `iw5_pc_code.cpp` (opcodes)
and `iw5_pc_meth.cpp`/`iw5_pc_func.cpp` (builtin methods/functions, a
SEPARATE table from opcodes, dispatched differently — `notify` is NOT in
these, confirming it's a true opcode not a builtin) are both real,
directly fetchable via `gh api repos/xensik/gsc-tool/contents/<path>?ref=dev`.

## `VM_Execute` — FOUND, definitive confidence (round 2)

**First attempt** (a fork) came back empty: confirmed `Scr_LoadScript`'s
own full call chain (see above, found as a direct byproduct) is LOAD-TIME
only and does not lead toward the runtime interpreter. Two runtime-
specific string anchors tried (`"stack overflow"`, `"script stack
overflow"`) — both exist as raw strings but have ZERO references via
either Ghidra's own xref database or a raw byte-level LEA scan — the same
wall this whole investigation kept hitting. The fork's own identified
"strongest remaining lead, not yet tried" was a function-SIZE sweep.

**That lead was followed immediately and it worked.** New reusable tool,
`re_notes/ghidra_scripts/LargestFuncs.java` (lists the N largest defined
functions in the program by real `Function.getBody().getNumAddresses()`
size — a genuine, reusable technique for any future "find the big
dispatch loop" RE task, not a one-off). Run against the whole binary:
`FUN_14025e950` is the **4th-largest function in the entire binary**
(12,626 bytes) and sits **directly adjacent** to the already-confirmed VM
cluster (`0x1402510xx`-`0x1402590xx` range).

**Decompiled and DEFINITIVELY CONFIRMED as the real interpreter loop, not
just a size-based guess**: the decompile is unambiguous fetch-decode-
execute-loop shape —
```c
pfVar25 = pfVar24;                    // fetch: current instruction pointer
bVar1 = *(byte *)pfVar25;             // fetch: read the opcode byte
pfVar23 = (float *)((longlong)pfVar25 + 1);  // advance past the opcode
switch(bVar1) {                       // decode+dispatch on the opcode
  case 0: ... goto switchD_14025ea4c_caseD_4f;  // execute, then loop back to fetch
  case 1: ... goto switchD_14025ea4c_caseD_4f;
  ...
```
233 real `case` labels, densely numbered from 0 — a genuine opcode table,
not a sparse/coincidental switch. The stack operations push/pop in
16-byte chunks (`pfVar17 + 4` floats = 16 bytes) matching a classic
`VariableValue`-sized VM value slot; several cases show clear conditional-
jump shapes (`if (fVar14 == 0.0) goto ...`), exactly what GSC's own
`jumpOnFalse`/`jumpOnTrue`-style bytecode instructions need. Cross-
reference: calls `FUN_1402574e0` (the already-confirmed 70-caller generic
hashtable accessor from the `SL_GetString`/`Scr_GetFunctionHandle`
investigation above) — exactly consistent with a real interpreter needing
variable/field lookups mid-execution. The function's own decompiled
signature shows zero formal parameters (`void FUN_14025e950(void)`) —
consistent with (not contradicting) a hyper-optimized, hand-tuned
interpreter hot loop using a custom register-passing convention Ghidra's
`-noanalysis` pass didn't recognize, the same class of finding this
project's own x86 per-frame usercmd-builder functions already
established ("every function in this chain uses custom register-passed
args, not a clean stack/fastcall signature").

**Real, immediate practical value**: this is the actual function every
single GSC bytecode instruction in the entire game runs through, for
every script, every mode, every frame a thread advances. Confirming it
opens up genuinely new RE angles beyond the original ready-up/buy-station
question — e.g. a targeted breakpoint (once live debugging is viable
again) on a SPECIFIC opcode case inside this switch would show exactly
which script is executing that instruction, a far more surgical
diagnostic than anything tried so far this session.

## `Scr_ExecThreadInternal` — FOUND, high confidence (round 4)

**Same winning technique as `VM_Notify`**: `gsc-tool`'s real opcode table
(`iw5_pc_code.cpp`) confirms the "call on a new thread" family are their
own distinct opcodes, not a flag on the plain call opcodes:

```cpp
{ 0x24, opcode::OP_ScriptLocalThreadCall },
{ 0x25, opcode::OP_ScriptLocalChildThreadCall },
{ 0x26, opcode::OP_ScriptLocalMethodThreadCall },
{ 0x27, opcode::OP_ScriptLocalMethodChildThreadCall },
{ 0x2B, opcode::OP_ScriptFarThreadCall },
{ 0x2C, opcode::OP_ScriptFarChildThreadCall },
{ 0x2D, opcode::OP_ScriptFarMethodThreadCall },
{ 0x2E, opcode::OP_ScriptFarMethodChildThreadCall },
```

`case 0x2b` (`OP_ScriptFarThreadCall`) in the confirmed interpreter
(`FUN_14025e950`) does exactly what a thread-spawning call should:
checks the live concurrent-thread count against a real limit
(`DAT_14246a330 < 0x1f`, i.e. 31), resolves the function handle
(`FUN_140257880`), then calls one of two functions depending on whether
that limit is close:

- `FUN_140256bd0(handle)` — the normal path. Allocates a new thread
  control block (`FUN_140256c30()`), tags it with a real type value
  (`0xf`/15), zeroes its state, and stores the resolved function handle
  into it.
- `FUN_140256890(handle, param_2)` — the near-limit path. Does the exact
  same allocation/initialization, but FIRST walks a real cleanup pass
  over already-finished/expired threads (freeing their slots) before
  creating the new one — the natural, sensible behavior for "we're near
  the concurrent-thread cap, reclaim dead threads before spawning
  another."

**`FUN_140256bd0`/`FUN_140256890` are `Scr_ExecThreadInternal`'s real x64
equivalent** — genuinely creating and initializing a new script thread
running a given function handle, matching the real published signature's
core job (`Scr_ExecThreadInternal(int handle, unsigned int objId,
unsigned int paramcount)`) even though only the handle argument is
visible in the `-noanalysis` decompile (the same custom-register-passing-
convention gap affecting every other function in this VM cluster).

**All five core GSC-VM primitives now have real, evidence-based x64
addresses**: `Scr_GetFunctionHandle` (`FUN_140255e60` + `FUN_140251fc0`),
`Scr_LoadScript` (`FUN_140252210`), `SL_GetString` (`FUN_140255e60`,
strong candidate), `VM_Execute` (`FUN_14025e950`, definitive), `VM_Notify`
(`FUN_140261e10`, definitive), `Scr_ExecThreadInternal`
(`FUN_140256bd0`/`FUN_140256890`, high confidence). This is a real,
usable foundation for GSC-VM interaction going forward — the whole
reason this investigation thread started.

## `VM_Notify` — first real live hook installed, 2026-09-17

Picked up directly where this doc's own "real, usable foundation" summary
left off — built a real x64 runtime AOB signature for `FUN_140261e10`
(`DumpSigBytes`-class tooling, a fresh `CreateFuncAndDumpSig.java` script
since this project's `-noanalysis` Ghidra imports don't have a `Function`
object at this address without one) and installed a live MinHook detour
(`Hook_VmNotify`, `proxy_d3d9/src/analog_input_hooks_x64.cpp`) via
`InstallAnalogInputHooksX64()` — automatically SP-only, matching every
other gameplay hook's own existing gate.

**Real, useful finding along the way**: `VM_Notify`'s function prologue
(`MOV [RSP+0x18],R8` / `[RSP+0x10],EDX` / `[RSP+0x8],ECX`) is the standard
Microsoft x64 shadow-space save sequence — RCX/RDX/R8 map directly onto the
three published arguments (`notifyListOwnerId`, `stringValue`, `top`). This
is a genuinely normal calling convention, NOT the custom register-passing
convention this codebase's own usercmd-pipeline functions needed raw
`__asm` trampolines for — a plain C++ MinHook detour with a matching
`__fastcall` signature is safe here.

**Scope, deliberately minimal**: read-only, log-and-call-through, zero
injected behavior — logs the first 50 real fires in full (owner ID +
interned string ID) then a periodic heartbeat, matching this codebase's
own standing rate-limiting lesson (issue #87). Does not call `VM_Notify`,
does not touch the VM stack, does not inject anything new — squarely
inside what the 2026-09-16 policy reversal actually unblocked (reading
live state), not the still-excluded "inject new behavior" class.

Build-verified (genuine x64 `dumpbin` confirmation), deployed to the live
install; **not yet live-tested**. The real, practical next step once a
session captures live notify traffic: correlate the observed
`(ownerId, stringId)` pairs against known in-game actions during a
Survival session specifically, to see whether the ready-up trigger (the
original open mystery from issue #5 -- no native call was ever found for
it on either architecture, only the x86-era synthetic-F5 workaround) shows
up as an identifiable, repeatable pattern in the traffic.

## Cross-reference

- Real x86-era prior art: `re_notes/iw5sp.md` (~line 1190-1230), the
  original 2026-07-15 investigation that found the published Plutonium
  signatures and the real risk caveat about calling `VM_Notify` from an
  async hook context — never resolved for `iw5sp.exe` itself (x86 or
  x64), only the reference project's own MP addresses were known.
- `re_notes/known_issues.md` issue #89 — the Survival scoreboard feature
  this whole GSC-VM access effort is partly motivated by unblocking.
- `re_notes/gsc_interaction_risk_assessment.md` — the risk assessment that
  led to the 2026-09-16 policy reversal enabling this investigation at
  all.

## STANDING CAUTION, 2026-09-16 -- do not match against the raw English ready-up/buy-station strings found via memory dump; that's the exact class of bug v0.3.1 fixed

Direct user catch: "did we learn our lesson from x86 hardcoded language
assumptions and the issues which made glyphs not work on other pcs" --
referring to a real, previously-shipped x86 bug (Version Timeline
2026-08-06/09, v0.3.1) where controller-glyph icons silently failed to
match for any non-English game language, because detection compared
against a hardcoded English literal instead of the real, live-resolved
localized string. Fixed at the time by resolving templates against the
game's own real localization system (`g_getLocalizedString`) instead.

**This project's own already-shipped x64 substitutions correctly avoid
this** (Mantle/Pickup/Reload/menu corner hints all resolve their own
templates live via `g_getLocalizedStringX64("PLATFORM_MANTLE")`-style key
lookups, matching the post-v0.3.1 pattern, not hardcoded text).

**The ready-up template found THIS session
(`"Press ^3[{skip}]^7 to ready up: &&1"`) is different and genuinely
risky if misused**: it was pulled directly from a live memory-dump
capture, not resolved via any localization key -- there is no confirmed
`PLATFORM_*`-style key behind it (per this project's own already-
documented finding that ready-up's text is "Survival-script-driven, not
in `code_post_gfx.str` at all"). **If this exact English string is ever
used as a match/detection target for a real implementation, it would
silently fail for every non-English game language -- the EXACT bug class
v0.3.1 already fixed once.** This is precisely why x86's own solution for
ready-up/buy-station never used string matching in the first place -- it
uses font-name detection (`IsGameplayHintFont`) instead, specifically
because no language-independent key was ever found for these two hints.
x64 does not yet have that same safety net (`Font_s.fontName`'s real
offset remains unresolved, a standing, independently-investigated-twice
negative result).

**Binding guidance for any future implementation based on tonight's
findings**: do not build ready-up/buy-station detection around matching
this specific English string. Either (a) find a real, language-
independent key/identifier behind this text (not yet attempted -- the
struct-array/header work from earlier tonight, or the GSC-VM access this
whole thread is chasing, might reveal one), or (b) resolve x64's
`Font_s.fontName` offset so the same font-based detection x86 already
uses safely can be ported, or (c) if genuinely building a from-scratch
replacement (the same "rebuild, don't fix" pattern already used for the
missing "get to cover" warning), source the DISPLAYED text from the
game's own real localization resolver at the point of use, never from a
hardcoded copy of what was captured in one specific (English) session.
