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
| `SL_GetString` | Strong candidate found (`FUN_140255e60`), not independently verified as the fully generic string-interning path. |
| `VM_Notify` | Not found. Loader-cluster neighborhood ruled out as its home. |
| `Scr_ExecThreadInternal`/`VM_Execute` | Not yet attempted. |

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

## `VM_Notify` — not found this pass

Ten plausible error-string anchors tried (`"too many notify"`, `"notify
list"`, `"bad entity"`, `"invalid entity"`, `"null entity"`, `"not a valid
notify"`, `"max notify"`, `"notify queue"`, `"notify string"`, `"invalid
object"`) — zero hits in `iw5sp.exe`. The immediate loader-cluster
neighborhood (`FUN_1402574e0` and its neighbors) is ruled out as
`VM_Notify`'s own home — confirmed to be shared, generic table
infrastructure instead (see above). No x86-era address was ever found for
this project's own SP binary either (x86-era research, `re_notes/
iw5sp.md`, found the real published signature via the same Plutonium
plugin but never located it in `iw5sp.exe` itself, x86 or x64).

**Real next steps, not yet tried**: (1) a structural sweep for a
3-parameter `(uint, uint, pointer)` function that indexes into an
entity/object array then walks a linked list comparing a stored string ID
against the passed-in one — the real shape `VM_Notify` should have,
independent of any string anchor; (2) once `SL_GetString`'s real candidate
(`FUN_140255e60`) is confirmed, trace ITS OWN callers for one that also
takes an entity-ID-shaped argument, since `notify`'s own compiled bytecode
almost certainly resolves its string argument through the same string-ID
path before calling into `VM_Notify`.

## `Scr_ExecThreadInternal`/`VM_Execute` — not yet attempted

No investigation done yet this session. The real bytecode INTERPRETER loop
(`VM_Execute`) is likely one of the largest functions in the binary (a
huge opcode-dispatch switch/jump table) — `FUN_14025b7f0` (the bytecode
FIXUP pass found above, which walks the SAME ~50 opcodes at LOAD time, not
execution time) is a structurally related but DIFFERENT function; worth
checking whether it shares any obvious neighbor with the real runtime
interpreter, but this hasn't been attempted.

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
