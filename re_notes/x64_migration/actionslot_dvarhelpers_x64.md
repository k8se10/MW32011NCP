# x64 migration — D-pad actionslot + generic dvar helpers (2026-09-03)

Parallel fork of the x64 migration RE work (issue #111). Scope: relocate the
generic dvar read/write API (used throughout nearly every feature in this
codebase) and check whether D-pad actionslot needs its own dedicated
mechanism on x64 or reuses the bind-index KeyDown/KeyUp setter another
parallel pass already found (`FUN_14007eaf0`, see `README.md` section 1e).

**Signature-scan reminder** (this project's own standing policy, reversed
2026-08-25→2026-09-03, see `CLAUDE.md` §5/§10.3): every address below is a
coordinate for TODAY's RE work against this specific binary, meant to inform
a future byte-pattern signature resolved once at startup and cached — not a
value to hardcode directly into shipped code.

## Generic dvar API — HIGH CONFIDENCE, cross-validated 3+ independent ways, NOT live-verified

Real x86 originals (from `analog_input_hooks.cpp`/`real_settings.cpp`, current
source, not memory): `SetDvarByName` @ `0x005396b0`, `GetDvarString` @
`0x00498ec0`, `SetDvarBool` @ `0x0044d700`, `SetDvarFloat` @ `0x005513c0`,
and `GetDvarInt`'s own internal target `FUN_0062abe0` (the real
`Dvar_FindVar`-equivalent, x86 custom convention: name in EDI, not the
stack — needed hand-written `__asm` to call).

**Method**: `RawStringScan.java` on `"cl_paused"` (already run by the parent
pass, `re_notes/x64_migration/cl_paused_x64.txt`) found 16 DATA references
across 11 functions. Decompiled three of them
(`re_notes/x64_migration/dvar_decomp_clpaused_users.txt`):

- **`FUN_14023d890`** — a giant dvar-registration batch function (same family
  as the one the parent pass already found for `cl_yawspeed`/`m_pitch`/etc.,
  a different one, this one covers `com_maxfps`/`com_timescale`/`timescale`/
  `fixedtime`/`cl_paused`/etc.). Confirms `cl_paused`'s real registration
  call and, critically, is where the real **setter** first showed up:
  `FUN_1402c5b30("cl_paused", 0)`.
- **`FUN_14029f3f0`** (a menu-state dispatch function, `switch(param_2)` over
  numeric menu-state codes — plausibly related to the pause-menu/`SetMenuState`
  cluster another parallel fork is covering, not chased further here to stay
  in scope) calls `FUN_1402c5b30("cl_paused", uVar3)` **twice more**,
  independently confirming the same setter.
- **`FUN_14029baa0`** calls `FUN_1402c5b30("cl_paused", 0)` a fourth time.

That's the same real function (`FUN_1402c5b30`) called with the exact
`(name, value)` shape four separate times across three unrelated call sites —
about as solid as static cross-validation gets without live testing.

**Decompiled the core trio directly**
(`re_notes/x64_migration/dvar_core_api_x64.txt`,
`re_notes/x64_migration/dvar_findvar_x64.txt`,
`re_notes/x64_migration/dvar_findvar_disasm_x64.txt`):

```c
// The real Dvar_FindVar-equivalent. Standard x64 calling convention (name in
// RCX, confirmed via disassembly showing CALL 0x1402c3890 immediately at
// function entry with NO register setup beforehand -- RCX passes straight
// through unmodified). NO custom register tricks needed at all, unlike x86's
// FUN_0062abe0 (EDI=name, required inline __asm to call correctly).
undefined8 *FUN_1402c3890(char *name)
{
    // real hash: seed 0x77, accumulates tolower(c)*runningMultiplier per char,
    // & 0x3ff into a 1024-bucket table at &DAT_1426f3c00, walks a linked list
    // via entry[0xb] (offset 0x58) comparing names via FUN_1402ca760 (the
    // same strcmp-equivalent the bind-name-table resolver, FUN_14007eff0,
    // already uses elsewhere this session).
}

// GetDvarInt/GetDvarBool-equivalent
ulonglong FUN_1402c3b10(void /* name in RCX, forwarded to FUN_1402c3890 */)
{
    lVar1 = FUN_1402c3890(name);
    if (!lVar1) return 0;
    if (1 < (byte)(*(char*)(lVar1+0xc) - 5)) {       // type tag at +0xc: 5/6 = float/string
        return FUN_140396f34(*(undefined8*)(lVar1+0x10));  // non-int type -> real conversion call
    }
    return *(uint*)(lVar1 + 0x10);                    // <-- VALUE at +0x10, not +0xc
}

// GetDvarString-equivalent
undefined1 *FUN_1402c3b50(void /* name in RCX */)
{
    lVar1 = FUN_1402c3890(name);
    if (lVar1 != 0) {
        if (*(char*)(lVar1+0xc) != 6) return *(undefined1**)(lVar1+0x10);  // <-- +0x10 again
        // type 6 (string) has an extra indirection through a string table at +0x48
        if (*(int*)(lVar1+0x40) != 0)
            return *(undefined1**)(*(longlong*)(lVar1+0x48) + *(int*)(lVar1+0x10)*8);
    }
    return &DAT_1403e6bdb;  // empty-string sentinel, same one the bind-name table's index-0 slot uses
}

// SetDvarByName-equivalent -- (name, intValue), confirmed by 4 independent real call sites above
void FUN_1402c5b30(undefined8 name, undefined4 value)
{
    lVar1 = FUN_1402c3890(name);
    ... // real set-value dispatch, delegates to FUN_1402c5f30 for the actual write
}
```

**Real, concrete ABI finding, not assumed**: the raw dvar value moved from
x86's confirmed `+0xc` offset to **`+0x10`** on x64 — independently confirmed
in both the int/bool getter and the string getter, plus disassembly showing
the exact same offset in the compiled code (`MOVZX EAX, byte ptr [RAX+0xc]`
for the type tag, `MOV RCX, qword ptr [RCX+0x10]` for the value). This is
exactly the "don't assume +0xc carries over, x64 struct alignment can shift
it" risk the parent directive flagged — confirmed real, not hypothetical. A
type-tag byte at `+0xc` is also new/more visible here than the x86 notes
described (x86's `GetDvarString`/`GetDvarInt` split existed specifically to
avoid needing this kind of tag check; x64's single `FUN_1402c3890` return
struct seems to expose the tag directly, which could mean ONE getter
function suffices for x64 where x86 needed several type-specific ones — real
simplification, matching the same pattern the parent's `FUN_14007eaf0`
finding showed for buttons).

**Not yet found**: dedicated `SetDvarFloat`/`SetDvarBool`-specific x64
functions (x86 had these as separate functions from `SetDvarByName`). Given
`FUN_1402c5b30` takes a plain `undefined4` value and the callers observed
pass plain integers, it's plausible this ONE setter handles bool/int/float
generically (matching the type-tag-driven getter pattern) rather than
needing separate float/bool variants — but this is not confirmed, only
inferred from the pattern. Real next step: find a real float-dvar SET call
site (e.g. something touching `sensitivity`/`cl_yawspeed`) and check whether
it also goes through `FUN_1402c5b30` or a different function.

## D-pad actionslot — NOT independently confirmed this pass, staying in scope but honest about the gap

x86 used a dedicated `ActionSlotDown`/`ActionSlotUp` pair (`FUN_00410ad0`/
`FUN_0044ec40`), found via a materially different investigative path (a raw
per-slot keycode dispatch table) than the bind-name-table mechanism the
`+attack`/`+toggleads_throw` work used. The bind-name table itself (already
fully mapped by the parent pass, `re_notes/x64_migration/kbutton_table_x64.txt`)
does contain `+actionslot 1`-`4` entries, with computable indices via the
same `(entryAddr - 1404c1870) / 8` formula:

| Bind | Entry address | Computed index |
|---|---|---|
| `+actionslot 1` | `1404c18e8` | 15 |
| `+actionslot 2` | `1404c18f8` | 17 |
| `+actionslot 3` | `1404c1908` | 19 |
| `+actionslot 4` | `1404c1918` | 21 |

Given `FUN_14007eaf0` (the parent pass's KeyDown/KeyUp setter) is a plain
`(playerIndex, bindIndex, isDown)` call with no bind-type-specific branching
visible in its own decompile, it's PLAUSIBLE actionslot on x64 needs no
separate dedicated function at all — just calling that same setter with
indices 15/17/19/21. **This is a structural inference, not an independently
traced/decompiled confirmation** — I did not trace an actual code path from
"D-pad button press" to `FUN_14007eaf0` with one of these specific indices
during this pass (ran out of scope budget prioritizing the higher-confidence
dvar API work above, which had much stronger multi-source cross-validation
available). Real next step: find the actual per-slot loadout-driven dispatch
logic (x86's own note: "data-driven per-slot behavior based on loadout" —
this suggests real logic beyond a flat KeyDown/KeyUp call exists, since
actionslot behavior depends on what's actually equipped) and confirm whether
it still funnels through `FUN_14007eaf0` or has its own separate mechanism.

## Summary

| Target | Confidence | Live-verified |
|---|---|---|
| `FUN_1402c3890` (Dvar_FindVar) | High — clean disasm, standard calling convention | No |
| `FUN_1402c3b10` (GetDvarInt/Bool) | High — direct decompile + disasm | No |
| `FUN_1402c3b50` (GetDvarString) | High — direct decompile | No |
| `FUN_1402c5b30` (SetDvarByName) | High — 4 independent real call sites | No |
| Value offset `+0x10` (was `+0xc` on x86) | High — confirmed 2 ways | No |
| Separate SetDvarBool/SetDvarFloat | NOT FOUND — plausibly unified into `FUN_1402c5b30` | No |
| D-pad actionslot indices (15/17/19/21) | Medium — table math only, dispatch path not traced | No |
| Actionslot reuses `FUN_14007eaf0` | Low-medium — structural inference only | No |
