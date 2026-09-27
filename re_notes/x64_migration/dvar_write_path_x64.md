# x64 dvar write path (iw5sp.exe) — RE + implementation

**Date:** 2026-09-27 · **Closes (pending live test):** `known_issues_x64.md` issue #6, and the dvar half of
`x64_feature_parity_audit.md` row #64 · **Code:** `proxy_d3d9/src/dvar_write_x64.cpp/.h`,
`real_settings.cpp` (x64 branches), `analog_input_hooks_x64.cpp` (`FindDvarX64_Exported`, `SetDvarIntX64`)

Status: 🟡 build-ready, syntax-checked (clang, MSVC mode, both arches), **not compiled with MSVC, not
live-tested**.

---

## 1. Why the old search came up empty

The previous pass listed 19 direct callers of `Dvar_FindVar` (`FUN_1402c3890`), read the 8-function
cluster after it (a getter family), found no writes, and concluded that the setter must resolve dvars some
other way. Two things were missed:

- `Dvar_FindVar` has a **second entry point**: a 5-byte `JMP` thunk at `0x1402c3980`, used by 30 more
  callers (menu script, console commands, cgame).
- The setters **do** call `Dvar_FindVar` directly. They sit further down the same module
  (`0x1402c52b0`–`0x1402c6272`). The true direct-caller count is 20, and the setters are among them
  (`0x1402c5300`, `0x1402c5770`, `0x1402c5900`, `0x1402c5990`, `0x1402c5b30`, `0x1402c5e60`).

This pass walked **down** from known SET entry points instead:

1. The menu script command table has the entry `{"setdvar", FUN_1402a3180}` at `0x140434270`. Its handler
   parses two tokens and ends in `FUN_1402c5900(name, valueString)`.
2. `FUN_1402c5900` = `Dvar_SetFromStringByName`:
   - calls `Dvar_FindVar`;
   - if found, calls `Dvar_SetFromStringFromSource(dvar, str, 0)`;
   - if not found, calls `Dvar_RegisterString(name, 7, 0x100, str, "External Dvar")`.

## 2. The setter family

Every setter funnels into one sink, **`FUN_1402c5f30` = `Dvar_SetVariant(dvar, const DvarValue*, source)`**.

| Address | Function | Notes |
|---|---|---|
| `0x1402c5f30` | `Dvar_SetVariant(dvar, value*, source)` | The single write sink (see §3) |
| `0x1402c52b0` | `Dvar_SetBool(dvar, bool)` | source 0; type 0 direct, else `"1"`/`"0"` string |
| `0x1402c5300` | `Dvar_SetBoolByName(name, bool)` | creates the dvar if missing |
| `0x1402c53d0` | `Dvar_SetBoolFromSource(dvar, bool, source)` | |
| `0x1402c5ad0` | `Dvar_SetInt(dvar, int)` | source 0; types 5/6 direct, else `"%i"` string |
| `0x1402c5b30` | `Dvar_SetIntByName(name, int)` | the old `SetDvarIntX64` target |
| `0x1402c5c20` | `Dvar_SetIntFromSource(dvar, int, source)` | |
| `0x1402c5700` | `Dvar_SetFloat(dvar, float)` | source 0; float in XMM1; type 1 direct, else `"%g"` string |
| `0x1402c5770` | `Dvar_SetFloatByName(name, float)` | |
| `0x1402c5870` | `Dvar_SetFloatFromSource(dvar, float, source)` | |
| `0x1402c5e00` | `Dvar_SetString(dvar, str)` | type 7 copy, else an enum lookup by name |
| `0x1402c5e60` | `Dvar_SetStringByName(name, str)` | |
| `0x1402c5a30` | `Dvar_SetFromStringFromSource(dvar, str, source)` | Parses via `Dvar_StringToValue` (`0x1402c6670`); correct for every type |
| `0x1402c5900` | `Dvar_SetFromStringByName(name, str)` | What the `setdvar` menu script uses |
| `0x1402c5990` | `Dvar_SetFromStringByNameFromSource(name, str, source)` | |
| `0x1402c5080` | `Dvar_Reset(dvar, source)` | Writes the reset value (+0x30) |
| `0x1402c61e0` | `Dvar_SetVec3(dvar, x, y, z)` | |
| `0x1402c5420` | `Dvar_SetColor(dvar, r, g, b, a)` | |
| `0x1402c56b0` | `Dvar_SetDomainFunc(dvar, fn)` | Stores +0x50 and re-validates |
| `0x1402c5c90` | `Dvar_SetLatchedValue(dvar, value*)` | Writes +0x20 only |

Signatures for `Dvar_SetBool`, `Dvar_SetInt`, `Dvar_SetFloat` and `Dvar_SetFromStringFromSource` are each
unique in iw5sp.exe. They are also unique in iw5mp.exe, at `0x140326330`, `0x140326a60`, `0x140326720` and
`0x1403269c0`.

### dvar_t layout (x64, stride 0x60)

| Offset | Field |
|---|---|
| `+0x00` | name |
| `+0x08` | flags |
| `+0x0C` | type (u8) |
| `+0x10` | current value |
| `+0x20` | latched value |
| `+0x30` | reset value |
| `+0x40` | domain (enum: +0x40 count, +0x48 strings) |
| `+0x50` | domain callback |

Type tags: 0 bool · 1 float · 2 vec2 · 3 vec3 · 4 vec4 · 5 int · 6 enum · 7 string · 8 color · 9 vec3-color.

Correction to earlier notes: `GetDvarStringX64`'s comment calls type 6 "string". It is **enum**: the +0x48
indirection is the enum label table. Type 7 is string.

## 3. `Dvar_SetVariant`: the three things that matter

1. **Domain check first** (`FUN_1402c6970`, a jump table on the type). For a non-enum dvar, an
   out-of-range value is dropped silently. Then the domain callback (+0x50) can veto the write.
2. **Source-dependent protection.** These checks apply only when source is 1 (external/console) or
   2 (script):
   - flags & `0x2800` (read-only / write-protected) blocks the write;
   - flag `4` (cheat) blocks it unless cheats are enabled;
   - flag `2` (latched) diverts the write to `Dvar_SetLatchedValue`.

   **Source 0 (internal) skips all of that** and writes current and latched together. This is the same
   "param_5 = 0" semantics x86's wrappers have.
3. **Main-thread gate.** If `flags & ~tls[+0x24]` is non-zero, `FUN_14024a250` is called
   (`GetCurrentThreadId() == DAT_142005820`). On any other thread the function **returns without
   writing**: no error, no log. On the main thread the flags are OR-ed into the TLS modified mask
   (+0x20). The config writer uses that mask.

### The type-mismatch trap

When the dvar isn't the setter's native type, `Dvar_SetBool`, `Dvar_SetInt` and `Dvar_SetFloat` do **not**
convert. They sprintf the value and pass a `DvarValue` whose `.string` is that stack buffer. Only a string
dvar can consume that.

For an int dvar, the domain check compares the raw 32-bit slot (the low half of a pointer) against
[min, max], and the write is dropped. `r_texFilterAnisoMax`/`Min` are `Dvar_RegisterInt` (`FUN_1402c46d0`,
range 1..16, defaults 16/1), so `SetDvarFloat("r_texFilterAnisoMax", 16.0f)` through `Dvar_SetFloat` could
never land.

The x86 wrapper probably has the same problem, since x86's `SetDvarFloat` is a `…FloatByName`. That's
unverified here, and x86 is archived.

## 4. Main-thread drain point

- `FUN_14023ce80` = `Com_Frame`: a `_setjmp` error-recovery wrapper whose sole caller is the main loop,
  `FUN_1402ef310`.
- `FUN_14023cf20` = **the Com_Frame body**, called once per frame from that wrapper.
  - It runs in menus and in gameplay.
  - Its first action: if the TLS modified mask has `DVAR_ARCHIVE` (1), write `players2/config.cfg`
    (the strings at `0x140427ee0`/`0x14040bb68`).
  - It then reads `com_maxfps` and runs the frame.
- Draining at the top of this body therefore applies writes on the main thread and persists archived dvars
  in that same frame.
- The previous drain (`Hook_MovementTick`) stopped in the pause menu and didn't run at all in the main
  menu.

The Com_Frame body signature has **no match in iw5mp.exe**. MP's frame function is laid out differently:
`com_maxfps` is registered at `0x140270cc0` there, and its reader hasn't been traced. So the write path is
SP only for now.

## 5. Implementation (`dvar_write_x64.cpp`)

- **Resolved by signature:** four setters plus the Com_Frame body. It also needs `Dvar_FindVar` from
  `analog_input_hooks_x64.cpp`, which it checks via `IsFindDvarX64Resolved()`. All five must resolve, and
  the hook must enable, or the path stays off. Each `Queue*` call then fails with a single logged reason.
- **`Queue{Bool,Int,Float,String}WriteX64` can be called from any thread:**
  - The queue holds 64 slots behind an SRWLOCK.
  - Writes are coalesced by case-insensitive name, so the last write wins.
  - Names up to 63 chars, strings up to 255.
- **`Hook_ComFrameBodyX64`:**
  - The fast path is one interlocked read per frame.
  - On a non-empty queue it moves the entries to a main-thread-only buffer under the lock, releases it, and
    applies each entry.
- **Type-aware dispatch** on the dvar's real type tag:
  - the native setter when the types agree;
  - a numeric conversion between bool, int and float (`lroundf` for float→int);
  - `Dvar_SetFromStringFromSource(dvar, text, 0)` for everything else;
  - string writes always go through `Dvar_SetFromStringFromSource`.
- **An unregistered name is skipped and logged**, not created. The `…ByName` setters would register a stray
  external string dvar.
- **Each write logs one line:**
  `[x64-dvarwrite] <name> = <requested> -> read-back <live value> : OK | MISMATCH (value rejected by the dvar's domain) | applied | SKIPPED (no such dvar) | FAILED (…)`.
  The engine calls are SEH-guarded.
- **Callers routed through it:**
  - `real_settings.cpp`'s `SetDvarBool`/`SetDvarFloat`/`SetDvarString` on x64. This covers the Force*
    options and `vanilla_settings_sync.cpp`.
  - `SetDvarIntX64`, the F4 `ai_disableSpawn` toggle. Its old single-slot queue used hardcoded addresses
    for `Dvar_FindVar` and the main-thread-id global; it's removed.
- **x64 reads** in `real_settings.cpp`'s `GetDvarBool`/`GetDvarFloat`/`GetDvarString`:
  - These previously always returned 0/nullptr: the x86 `__asm` FindDvar compiled to `return nullptr`.
  - They now use `FindDvarX64_Exported` plus a type-correct read.
  - `GetDvarString` returns a value only for string and enum dvars. The engine getter returns the raw +0x10
    slot for other types, which isn't a pointer.

## 6. What the Force* options now write (registration flags read from `FUN_1401b50e0`)

| Option | Dvar | Registered as | Flags | Live effect expected? |
|---|---|---|---|---|
| ForceAnisotropicFiltering | `r_texFilterAnisoMax` / `r_texFilterAnisoMin` | int 1..16 (defaults 16 / 1) | 1 (archive) | Read every frame by `RB_ExecuteFrame` (`FUN_14018a240`), also by `FUN_1401866a0`/`FUN_1401e0120`. Should apply live and persist to config.cfg. Note that Max already defaults to 16, so the visible change comes from Min 1→16. |
| ForceHighQualityShadows | `sm_fastSunShadow` | bool, default 1 | 4 (cheat) | The cheat flag only blocks console sets; the internal source bypasses it. Not latched. Whether the sun-shadow code re-reads it per frame is unverified. |
| ForceHighQualityLighting | `r_cacheModelLighting` / `r_cacheSModelLighting` | bool, default 1 | 0 | Not latched. Per-frame consumption unverified. |

None of the five is latched, so no `vid_restart` is needed for the value itself.

## 7. Live test checklist

1. Log at launch: five `[x64-dvarwrite] … resolved @` lines, then `dvar write path live`.
2. Enable ForceAnisotropicFiltering. Expect `r_texFilterAnisoMax = 16 -> read-back 16 : OK` and
   `r_texFilterAnisoMin = 16 -> read-back 16 : OK`. After quitting, `players2/config.cfg` should contain
   `seta r_texFilterAnisoMin "16"`.
3. Enable the shadow and lighting toggles. Expect `sm_fastSunShadow = 0 -> read-back 0 : OK`, and the same
   for the two cache dvars.
4. F4 (glyph editor mode) should give `ai_disableSpawn = 1 -> … : OK`, now in the pause menu as well.
5. Custom Options vanilla tabs should show real current values, and edits should produce `[x64-dvarwrite]`
   lines.

## 8. Unlocked next

- **Native SSAO** (`r_ssao*`, `renderer_architecture_map.md` §5b) can now be driven from the proxy.
  `r_ssao` is `Dvar_RegisterEnum` (`FUN_1402c45d0`, at `0x140197518`): default index 1, flags **3**
  (archive + latched). An internal write sets the live value directly. Because the dvar is latched, the
  renderer probably consumes it at init (SSAO render-target allocation), so turning it on likely needs a
  `vid_restart` path as well. Behaviour-changing, so it needs the user's go-ahead.
- **The MP drain point.** Trace MP's `com_maxfps` reader to its Com_Frame body, then enable the same module
  there.
