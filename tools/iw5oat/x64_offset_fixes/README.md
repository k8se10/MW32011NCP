# x64_offset_fixes

Post-processing scripts that patch ZoneCodeGenerator's generated per-asset
loader code (`build/src/ZoneCode/Game/IW5/XAssets/*/*_load_db.cpp`) for x64.

## Why this exists

`ZoneCodeGenerator` (upstream OpenAssetTools' own code-generation tool,
driven by `src/ZoneCode/Game/IW5/IW5_Commands.txt`) has zero x64 awareness
— it emits literal byte offsets for every field read
(`fillAccessor.Fill(field, N)`), computed assuming x86 layout (4-byte
pointers, x86 alignment/padding rules), regardless of which architecture
the surrounding C++ actually gets compiled for. That's wrong for this
fork's real target: the current MW3 (2011) retail build's zone files are
genuinely x64 (see `re_notes/x64_migration/fastfile_format_research.md`
in the parent repo), so every one of those hardcoded x86 offsets silently
misreads real x64 zone content — the root cause behind `Unlinker`
segfaulting/misparsing on any real retail zone.

The real, permanent fix belongs in `ZoneCodeGenerator`/`IW5_Commands.txt`
itself (generate offsetof-based code directly, for any target arch) — a
substantially bigger undertaking, not done yet. Until then, these scripts
are the actual fix: they rewrite every literal offset ZoneCodeGenerator
emits into a `offsetof(Type, field)`/`sizeof(Type)` expression instead,
letting the real x64 MSVC compiler compute the correct value rather than
trusting a number ZoneCodeGenerator precomputed for the wrong architecture.

## This is a **repeatable step, not a one-time patch**

`build/` is gitignored — regenerated build output, not tracked source (see
`.gitignore` in this directory's parent). Every time `ZoneCodeGenerator` is
re-run for real (a fresh `generate.bat`/`premake5` pass, or after editing
`IW5_Commands.txt`/`IW5_ZoneCode.h`), it overwrites these generated
`.cpp`/`.h` files with fresh x86-offset output — silently discarding
whatever these scripts already fixed. **Run the full sequence again any
time `ZoneCodeGenerator` has regenerated IW5's output**, before building
`UnlinkerCli` (or any other target that links `ZoneLoading`/`ZoneWriting`
for IW5).

## Usage

```
cd tools/iw5oat/x64_offset_fixes
python run_all.py            # dry run -- prints every change, writes nothing
python run_all.py --apply    # applies the full sequence
```

Each step can also be run individually (same `--apply` flag) — useful when
iterating on one script, or verifying a single step's own before/after
diff. Run them in numeric order; later steps fix real gaps earlier steps'
own regexes miss (including one genuine cross-function-boundary
false-positive `01`'s own struct-array pattern can produce — see `03`'s
own docstring).

| Script | Fixes |
|---|---|
| `01_fix_field_offsets.py` | The bulk pass — every `fillAccessor.Fill\*`/`FillArray`/`LoadWithFill` literal offset across all ~46 asset types' generated loaders, rewritten to `offsetof()`/`sizeof()`. |
| `02_fix_loadwithfill_mismatch.py` | Repairs 10 sites where `01`'s struct-array regex fixed the paired `FillStruct` call but left the matching `LoadWithFill` literal untouched. |
| `03_fix_ptrarrayfill_crosscontam.py` | Repairs a real cross-function-boundary false positive in `01`'s own struct-array regex (it can pair a pointer-array's `LoadWithFill` with an unrelated struct's `FillStruct` call elsewhere in the file). |
| `04_fix_dynamicfill_sizes.py` | Fixes the `LoadDynamicFill_<Type>` family's own `AppendToFill(N)` literals — a distinct call shape from `01`'s targets, needed for variable-length assets (e.g. `MaterialTechnique`'s own `passArray[]` tail). |

`common.py` is shared infrastructure (not a standalone script) — path
resolution and the `var<Suffix>` → real struct type name lookup every
script above depends on (derived by scanning the generated headers
directly, not hand-maintained).

## Resolved: the decl[]/ps/vs question

**Earlier revisions of this README flagged `MaterialVertexStreamRouting::
decl[]`/`MaterialPixelShaderProgram::ps`/`MaterialVertexShaderProgram::vs`
as an open question — this is now resolved.** Direct Ghidra decompile of
`iw5sp.exe`'s own real fill functions confirms all three are genuinely on
the wire: every native read size matches `sizeof(Type)` computed *with*
the field included (`MaterialPixelShader`/`MaterialVertexShader`: 32
bytes; `MaterialVertexDeclaration`: 176 bytes). They're never explicitly
filled with a meaningful value because they're runtime-only D3D
shader-object caches populated after loading, not because they're absent
from the struct — the scripts above already handle this correctly by
trusting `sizeof()`. A related bug found via the same technique
(`Material::subMaterials`, a genuinely phantom trailing field, unlike
these three) was found and fixed directly in `IW5_Assets.h` — see
`re_notes/x64_migration/fastfile_format_research.md` §5.9 for the full
trail.

## Known open issue, not yet resolved

`MaterialTechniqueSet`'s own `MaterialPass` → `MaterialVertexDeclaration`/
`MaterialVertexShader`/`MaterialPixelShader` chain still fails on all
three real retail zones tested (`code_post_gfx.ff`, `common.ff`,
`hamburg.ff` — all three fail at the exact same point, real progress even
though not fully resolved). A byte-exact hex dump proved the corruption
is isolated to exactly the 8 bytes of `MaterialPixelShader::name` —
every other field in the same read, and `MaterialVertexShader`'s entire
header read immediately before it, are byte-perfect. An earlier theory
(shader bytecode reading from the wrong `XFILE_BLOCK_*`) was tested and
disproven with direct evidence — blocks only affect where output lands,
never which stream bytes get read. A second theory — that this fork's
assumed struct field order/layout for `MaterialPixelShader`/
`MaterialVertexShader`/`MaterialPass` might be wrong — was also tested
and disproven, this time via fresh decompile of the *original x86*
`iw5sp.exe` (not just x64-side reasoning): x86's own native
`Load_MaterialPixelShader`/`Load_MaterialVertexShader` are byte-for-byte
structurally identical to each other and match this fork's field order
exactly. The root cause is still unidentified despite many rounds of
native-decompile verification on both architectures; paused per this
project's own standing persistence-threshold principle rather than
continuing to re-derive the same conclusions. See
`re_notes/x64_migration/fastfile_format_research.md` §5.10 and §5.13 for
the full trail and concrete next-step recommendations (live debugging or
a raw hex-editor comparison — genuinely different techniques, not more
decompile cross-referencing).

## Scope

`*_load_db.cpp` only (the LOAD path — what `Unlinker`'s own extraction
needs). `*_write_db.cpp` (re-packing zones, used by `Linker`) has the same
bug class but is out of scope for this fork's current need.

## A separate, permanent hand-fix: `SpeakerMap`/`MSSChannelMap`/`MSSSpeakerLevels` (2026-09-16)

Not part of this directory's own automated scripts (this bug isn't an
`offsetof()`/`sizeof()` computation error -- it's a fundamentally wrong
struct SHAPE, which no amount of offset/size correction can fix). Root
cause and full evidence trail: `re_notes/x64_migration/fastfile_format_research.md`
§5.40 (parent repo).

**What was wrong**: the real native `SpeakerMap` is 64 bytes on the wire
(a 16-byte header + `channelMaps[2]` of 24-byte entries, each holding 2
packed 12-byte sub-entries), not the 416 bytes this fork's own
`MSSSpeakerLevels`/`MSSChannelMap`/`SpeakerMap` struct declarations
computed (`channelMaps[2][2]` of a 100-byte `MSSChannelMap` with 6
16-byte `MSSSpeakerLevels` entries, each with an inline `float[2]`
instead of a real dynamically-allocated pointer). Every `speakerMap`
field load over-read the stream by 352 bytes, silently desyncing every
asset read afterward -- the actual root cause of the long-standing
"invalid block N" real-content-zone extraction failures this project has
chased since `fastfile_format_research.md` §5.21.

**What's fixed where**:
- `src/Common/Game/IW5/IW5_Assets.h` (tracked source, permanent) -- the
  corrected struct shapes, with `#pragma pack(1)` on the two inner
  structs (the real pointer sits unaligned at byte offset 4) and
  `static_assert`s pinning the real 12/24/64-byte sizes.
- `Loader_snd_alias_list_t::FillStruct_SpeakerMap`/`Load_SpeakerMap`
  (`XAssets/snd_alias_list_t/snd_alias_list_t_iw5_load_db.cpp`) -- **NOT
  in this directory's automated scripts, and NOT in tracked source either
  (that whole per-asset generated directory is gitignored)**. This
  function needed genuinely new loading logic (a real per-entry dynamic
  "levels" array resolution, matching native's own unconditional-nonzero
  check -- see the source comment for the full rationale), not a
  mechanical offset/size rewrite, so it was hand-patched directly in the
  `build/` output. **This hand-patch WILL be silently lost on the next
  `ZoneCodeGenerator` regenerate**, same as this directory's own scripted
  fixes -- unlike those, there's no script to re-run for this one yet
  (a real candidate for a future `06_fix_speakermap_shape.py`, not
  written this pass). If a future session finds `SpeakerMap`-related
  crashes have come back, check whether `FillStruct_SpeakerMap` still has
  the flat `channelMaps` array + the `Load_SpeakerMap` per-entry `levels`
  resolution loop, or whether a regenerate silently reverted it back to
  the old, wrong `channelMaps[0]`/`channelMaps[1]` two-call shape.

**Verified**: `sp_intro.ff`/`sp_prague.ff`/`sp_ny_harbor.ff` still load
with 0 errors (no regression). `common_survival.ff`/`code_post_gfx.ff`/
`hamburg.ff`/`common.ff` now progress further into their own streams than
before this fix, past the point this bug used to corrupt -- into a
separate, already-tracked forward-reference issue
(`fastfile_format_research.md` §5.21), not this bug recurring.
