# x64 migration — Dvar_FindVar + GetEffectiveFov, ADS-FOV look-slowdown + Survival ready-up gate (2026-09-13)

Closes two real, previously-documented gaps: `GetLookAccelerationScaleX64`'s own
comment ("the ADS-FOV look-slowdown... needs an x64 equivalent of the hardcoded
GetEffectiveFov/Dvar_FindVar addresses -- genuinely unresolved RE targets, not
yet found") and `SendSyntheticF5X64`'s own comment (fires unconditionally,
omitting x86's `IsInSurvivalMode()` gate for the same reason). Both are the
same underlying RE gap — `Dvar_FindVar`'s x64 equivalent — so this pass
resolves it once and wires both consumers in the same session.

## Dvar_FindVar equivalent — already found by an earlier parallel pass, independently re-verified here

`re_notes/x64_migration/actionslot_dvarhelpers_x64.md` (2026-09-03) already
identified `FUN_1402c3890` as the real x64 `Dvar_FindVar`-equivalent, plus
`FUN_1402c3b10` (GetDvarInt/Bool-equivalent) and `FUN_1402c3b50`
(GetDvarString-equivalent) — High confidence per that doc, but flagged
"NOT live-verified." This session independently re-confirmed all three via a
**fresh** decompile + disassembly pass (not just trusting the earlier doc):

```
undefined8 * FUN_1402c3890(char *param_1)   // name in RCX, standard x64 convention
{
    ... real hash-bucket dvar-table lookup, unchanged from the earlier doc's own trace ...
}
```

Disassembly of `FUN_1402c3b10` (the int/bool getter) confirms the calling
convention directly: `CALL 0x1402c3890` sits at its very first instruction
(after `SUB RSP,0x28`), with **zero register setup** beforehand — RCX (the
caller's own first argument) passes straight through unmodified. No custom
register convention at all, unlike x86's `FUN_0062abe0` (EDI=name, needed
`real_settings.cpp`/`analog_input_hooks.cpp`'s own hand-written `__asm`
blocks).

Same disassembly pass reconfirms the value offset is `+0x10` (not x86's
`+0xc`): `MOVZX EAX, byte ptr [RAX+0xc]` for the type tag, `MOV EAX, dword ptr
[RCX+0x10]` for the raw int/bool value.

**New finding this session, not in the earlier doc**: `FUN_1402c3b10`'s type
dispatch is the OPPOSITE of what its own name suggests. Its condition
(`1 < (byte)(tag-5)`) calls a generic to-int conversion helper
(`FUN_140396f34`) for every type tag **except** 5 and 6 — for tags 5/6
(float/string) it returns the raw dword/pointer at `+0x10` completely
unconverted. This means `FUN_1402c3b10` is **not safe to use for reading a
float dvar as a float** (calling it on `cg_fov` would hand back the float's
raw bit pattern reinterpreted as whatever the conversion path does to
non-5/6 types — untested and irrelevant, since the 5/6 branch bypasses that
path entirely and returns the bits unconverted, but relying on that
undocumented branch behavior instead of reading the float directly ourselves
would be exactly the kind of "trust an assumption instead of checking"
mistake `CLAUDE.md` already flags as a real, previously-hit failure mode).
**Decision: read floats directly at the confirmed `+0x10` offset ourselves**
(`GetDvarFloatX64`, `analog_input_hooks_x64.cpp`), matching x86's own
established pattern of type-specific getters that never share logic across
dvar types (`real_settings.cpp`'s own `GetDvarBool`/`Float`/`String` header
comment). `FUN_1402c3b50` (`GetDvarString`-equivalent) was independently
re-disassembled too and is safe to call as-is — its own type check (tag==6
→ string-table indirection through `+0x48`, else raw pointer at `+0x10`) is
exactly the kind of per-type handling `FUN_1402c3b10` should have had for
non-int types and didn't.

## GetEffectiveFov equivalent — found via the established dvar-value-discovery chain

Method, per `CLAUDE.md`'s own documented chain (broad string sweep → exact-
string xref → decompile the registration call → xref the storage handle →
decompile the real consumer(s)):

1. `RawStringScan.java` on `"cg_fov"` → one reference, in `FUN_14004a870`
   (a large dvar-registration batch function). Decompiling it found the real
   cached dvar handle: `DAT_14062ce60 = FUN_1402c4650("cg_fov", ...)` (and
   `DAT_14062ce68` = `cg_fov1`, `DAT_14062ce80` = `cg_fovNonVehAdd`, both in
   the same batch).
2. A second registration batch (`FUN_140135b50`, found via string sweeps on
   `cg_fovScale`/`cg_fovMin`) supplied the remaining handles:
   `DAT_14062ce78` = `cg_fovScale`, `DAT_14062ce88`/`DAT_14062ce90` =
   `cg_playerFovScale0`/`1`, `DAT_14062ce70` = `cg_fovMin`.
3. `DescribeRefs.java` on `DAT_14062ce60` (the `cg_fov` handle) found only
   5 real references project-wide: one write (its own registration) and four
   reads, in `FUN_140004170` (an unrelated entity/screen-space culling
   function — false lead, ruled out by inspection), `FUN_14005d7a0` (a
   large multiplayer/UI-teardown function — also unrelated, only touches
   `cg_fov` in passing), `FUN_140068a80`, and `FUN_140069e60`.
4. `FUN_140069e60` is the real match. Disassembled in full
   (`re_notes/x64_migration/fun_140069e60_disasm.txt`): playerIndex arrives
   in ECX (`MOV EDI,ECX` at entry, no other setup — standard x64 convention,
   no custom register tricks needed, matching x86's own "no custom register
   convention" finding for `FUN_004b0580`), and the body implements, in
   order: an alt-scope weapon-FOV path gated by
   `TEST byte ptr [0x14052a098],0x4` (the SAME bit-2/mask-0x4 convention
   x86's `DAT_00984b9c` check uses, a different address, same bit); a raw
   `cg_fov`/`cg_fov1` dvar read (selected by playerIndex) as the base value
   when no alt path/override applies; a real time-based lerp
   (`DAT_140547e54` countdown against `DAT_14052a070`, the frame-delta
   global) blending the previous effective value toward
   `targetFov * cg_fovScale` — the real "transition system" x86's own
   comment names as driven by `set_lerp_fov`/`set_pip_fov`/`set_turret_fov`
   (independently confirmed: those three GSC method-name strings all
   converge on the same call chain via `FUN_1400595c0`, and `FUN_140069e60`'s
   own sibling helper `FUN_140068a80` — also found via the `DAT_14062ce60`
   xref sweep — is that transition system's own "bake the finished
   transition back into the raw dvar" step, confirming the two functions are
   part of the same subsystem); a `cg_playerFovScale0`/`1` multiply, gated by
   a per-entity/turret table walk (stride `0x2670` over
   `DAT_140647a08`/`DAT_14064c6e8`); a conditional `cg_fovNonVehAdd` add;
   and a final `cg_fovMin` floor clamp plus a hardcoded max-FOV ceiling
   (`[0x1403f2cb8]`). Returns in XMM0 (plain `float`, the standard x64 ABI
   float-return register — not x86's x87 `float10`/`ST(0)`).
   **Every single term matches x86's own `FUN_004b0580` documented formula
   component-for-component** ("blends base FOV (cg_fov/cg_fov1) toward the
   current weapon's real ADS zoom target..., applies cg_fovScale's
   transition system... and cg_fovNonVehAdd/cg_fovMin") — this is a
   structural, multi-point confirmation, not a guess from the function's
   proximity to a string.
5. Confirmed read-only: no store instructions to any of the transition-state
   globals (`DAT_140547e54`/`e48`/`e4c`/`e50`/`e58`) appear anywhere in
   `FUN_140069e60`'s own disassembly — matches x86's "pure query, no
   observed side effects" note. Only 2 real callers project-wide
   (`FUN_140068a80`, `FUN_1400595c0`, both internal to the FOV-transition
   subsystem itself) — this project's own external call is a new, third
   caller, exactly the same relationship x86's project has with
   `FUN_004b0580`.

## Confidence

| Target | Confidence | Live-verified |
|---|---|---|
| `FUN_1402c3890` (Dvar_FindVar) | High — independently re-confirmed via fresh decompile+disasm this session, on top of the earlier pass's own finding | No |
| `FUN_1402c3b50` (GetDvarString) | High — independently re-confirmed via fresh disasm this session | No |
| Value offset `+0x10` | High — confirmed a third independent way (GetDvarFloatX64/GetEffectiveFovX64 both read it directly) | No |
| `FUN_140069e60` (GetEffectiveFov) | High — every dvar-handle term in its disassembly matches x86's documented formula component-for-component, not a name-only guess | No |
| `FUN_1402c3b10` (generic int/bool getter) | High confidence in what it does, but NOT used by this pass's own new code (float dvars read directly instead — see above) | No |

**Not live-tested.** Build-verified only (x64 `/t:Rebuild`, 0 errors) as of
this entry. Live-test both consumers before treating either as fully
confirmed, per this project's own "Verify Live" standard (`CLAUDE.md` §10.4):
- ADS-FOV look-slowdown: aim down sights with a zoom optic (ACOG/sniper) on
  x64 and confirm controller look sensitivity slows proportionally, matching
  the x86 feel (issue #8/#44's already-confirmed-correct x86 math, now
  ported).
- Survival ready-up gate: confirm Y/weapnext-hold in Survival still ready-up
  fires correctly (issue #1's own x64 Sprint/weapnext work already
  live-confirmed the unconditional-fire version works — this change only
  narrows WHEN it fires, so a regression here would show as ready-up no
  longer firing in Survival, or `IsInSurvivalModeX64()` returning false
  during an actual Survival match).

## Implementation

`analog_input_hooks_x64.cpp`, in a new anonymous-namespace block immediately
before `Hook_MovementTick` (placed there rather than earlier in the file so it
can use the already-declared `kLocalClientIndexX64`/`g_adsHeldX64`
without a forward declaration):
- `FindDvarX64Raw` (private function pointer, `FUN_1402c3890`)
- `GetDvarFloatX64(name)` — reads `+0x10` directly, does not call
  `FUN_1402c3b10` (see the type-dispatch finding above)
- `GetDvarStringX64Raw`/`GetDvarStringX64(name)` (`FUN_1402c3b50`, called
  as-is)
- `GetEffectiveFovX64Raw`/`GetEffectiveFovX64(playerIndex)` (`FUN_140069e60`)
- `GetAdsLookRateScaleX64()` — byte-for-byte port of x86's
  `GetAdsLookRateScale` formula (power-curve scale + issue #44's close-range
  taper), now wired into `Hook_MovementTick`'s Look pre-hook
  (`GetAdsLookRateScaleX64() * GetLookAccelerationScaleX64()`, replacing the
  bare `GetLookAccelerationScaleX64()` sharedScale)
- `IsInSurvivalModeX64()` — byte-for-byte port of x86's `IsInSurvivalMode`
  (`GetDvarStringX64("mapname")` + the same `"so_survival_"` prefix check),
  wired at `SendSyntheticF5X64`'s one call site (Hook_MovementTick's
  Y/weapnext hold-edge block), matching x86's own call-site gate structure
  exactly rather than gating inside `SendSyntheticF5X64` itself.
