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

## Known open question, not yet fixed by any of these scripts

Some generated structs (`MaterialVertexStreamRouting::decl[VERTDECL_COUNT]`,
`MaterialPixelShaderProgram::ps`, `MaterialVertexShaderProgram::vs`) declare
real pointer/pointer-array fields that the corresponding `FillStruct_*`
function never reads from the wire at all. Whether these are genuinely
serialized on disk (as raw, uninitialized-until-runtime memory, matching
this format's general "dump the in-memory struct as-is" convention) or are
runtime-only fields the wire format never included — which would mean
`sizeof(Type)` (used by every `LoadWithFill(sizeof(Type))` call) is
counting bytes that were never actually on the wire, desyncing every read
after it — is not yet resolved. Confirmed live: this is the actual reason
`MaterialTechniqueSet`'s own `MaterialPass` → `MaterialVertexDeclaration`/
`MaterialPixelShader` chain still fails after all four scripts above,
across all three real retail zones tested (`code_post_gfx.ff`, `common.ff`,
`hamburg.ff` — all three now fail at this exact same point, not three
different points, which is itself real progress). Resolving this
definitively needs native Ghidra RE against `iw5sp.exe`'s own zone-content
reader (the same technique that resolved the outer dispatch-record/header
bugs — see `re_notes/x64_migration/fastfile_format_research.md` §5) rather
than continued guessing from field names; a direct experiment excluding
`decl[]` from the read size was tried and made the failure mode *worse*
(a segfault instead of a caught bounds exception), so it was not kept.

## Scope

`*_load_db.cpp` only (the LOAD path — what `Unlinker`'s own extraction
needs). `*_write_db.cpp` (re-packing zones, used by `Linker`) has the same
bug class but is out of scope for this fork's current need.
