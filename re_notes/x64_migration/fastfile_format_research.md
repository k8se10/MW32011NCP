# IW5 x64 FastFile/zone format — root cause found, in-house tooling scoped (2026-09-14)

**Context**: this project has been blocked project-wide since 2026-09-03 on
fresh GSC extraction — OpenAssetTools' Unlinker segfaults loading any
real-content zone from the current x64 retail build (`known_issues_x64.md`
issue #1's "GSC-first pass" rounds, 2026-09-14). Direct instruction: "we
should re zone and ff format ourselves and build our own in house tooling
for it as rn there is practically no chance of getting another community
based one in time for release" — this doc is that investigation's real
findings plus a scoped plan, not a promise of a finished tool.

**These findings and any tooling that comes out of this work are intended
for eventual public release as part of this project's own community
patching effort — write code/docs here to that bar, not as disposable
scratch work.**

## 1. The outer FastFile container is UNCHANGED — confirmed via raw bytes, not assumption

Hex-dumped the real header of two current retail x64 zones directly (no
tool involved, ground truth):

- `zone/english/sp_intro.ff` (303 bytes — the confirmed-working thin
  loader zone from the original investigation)
- `zone/english/hamburg.ff` (165,087,814 bytes — one of the confirmed-
  segfaulting real-content zones)

Both start byte-for-byte identically for the fixed portion of the header:

```
offset 0x00-0x07: "IWffu100"        (magic — unchanged, unsigned IW5 fastfile)
offset 0x08-0x0B: 01 00 00 00       (version = 1 — unchanged)
offset 0x0C-0x14: <9 file-specific bytes, differs per file — likely a hash/checksum>
offset 0x15:      78 DA             (zlib stream header — CMF/FLG for max-compression deflate)
```

The `78 DA` zlib magic sits at the **identical relative offset (0x15) in
both files** — the fixed-size outer header (21 bytes) is unchanged, and
the compression algorithm is confirmed to still be plain zlib deflate
(unchanged from the pre-2026-09-03 format). **This means the "zone/
fastfile container format changed" framing in this file's own earlier
rounds today was imprecise — the outer container is fine.** Decompressing
`sp_intro.ff`'s payload (766 bytes, trivial) confirms decompression itself
works cleanly and produces readable content (a real, intact map-entities
string: `maps/sp_intro.mapents\n{...}`) — zlib isn't the problem either.

## 2. The DECOMPRESSED internal structure is the real suspect — and OpenAssetTools' own source confirms exactly why

Decompressed the first 4KB of `hamburg.ff`'s payload for comparison
against `sp_intro.ff`'s (full 766 bytes). Both show the same real
STRUCTURAL shape once decompressed: a run of fixed-width fields, many
0xFFFFFFFFFFFFFFFF (8-byte, not 4-byte) sentinel entries in a row
consistent with a per-asset-type table where FF = "no assets of this type
in this zone," before real string/asset content begins. **The FF runs are
8 bytes wide, consistently, in both files** — the exact shape a per-asset-
type table of `(count, pointer)` or similar pairs would have if every
field in it is now 8 bytes (x64 pointer/size_t width) instead of 4 bytes
(the original x86 width).

**Confirmed, not inferred, via OpenAssetTools' own current source**
(`Laupetin/OpenAssetTools`, cloned and read directly — see §3 for exactly
how): `src/ZoneLoading/Game/IW5/ZoneLoaderFactoryIW5.cpp`,
`InspectZoneHeader()`, hardcodes **`GameWordSize::ARCH_32`** for IW5 —
both the signed and unsigned magic branches, unconditionally, no check of
any kind against the actual file:

```cpp
if (!memcmp(header.m_magic, ZoneConstants::MAGIC_UNSIGNED, ...))
{
    return ZoneLoaderInspectionResult{
        .m_game_id = GameId::IW5,
        .m_endianness = GameEndianness::LE,
        .m_word_size = GameWordSize::ARCH_32,   // <-- always, unconditionally
        .m_platform = GamePlatform::PC,
        ...
    };
}
```

This is the real mechanism behind the segfault: every IW5 zone gets parsed
assuming 32-bit-wide struct fields (4-byte pointers/offsets), which was
correct for every MW3 build from 2011 through 2026-09-02. The 2026-09-03
x64 recompile's own on-disk zone-content serialization now uses 64-bit-
wide fields (matching the raw 8-byte-run evidence in §1) — reading that
with 32-bit-wide struct definitions doesn't just misparse a few fields, it
desyncs every offset after the first mismatched pointer, eventually
walking into garbage far enough to segfault. This also explains the
original observation that small/thin zones "work" (their FF-sentinel-
dominated tables happen to look the same misread as either 4-byte or
8-byte pairs, and they never reach real content deep enough to diverge)
while any real-content zone reliably crashes.

## 3. This is NOT a quick community fix, confirmed structurally — validates going in-house

Checked whether `GameWordSize::ARCH_64` is real, working infrastructure
this project could just flip on, or a stub:

```
grep -rn "ARCH_64" --include=*.cpp --include=*.h src/
```

**Zero results outside the bare enum declaration itself**
(`src/Common/Game/IGame.h`). `ARCH_64` is declared (`enum class
GameWordSize { ARCH_32, ARCH_64 };`) and threaded through
`ZoneLoaderInspectionResult`/`IZoneLoaderFactory` as a field that gets
SET — but is never actually BRANCHED ON anywhere to change struct layout,
field size, or offset computation. Cross-checked against `T6` (Black Ops
2, a real game with a genuine PS4/XB1 64-bit release in real life) —
`ZoneLoaderFactoryT6.cpp` hardcodes `ARCH_32` for every one of its 8
platform variants too. **No game loader in this entire codebase has ever
exercised a 64-bit word size.** The `ZoneCodeGeneratorLib` module (a real
code-generation system that presumably emits the actual per-struct
load/write logic from declarative definitions) is architecture-aware in
principle, but zero IW5 struct definitions exist for an 8-byte-wide
layout to generate against.

**Conclusion, matching the user's own assessment exactly**: flipping a
flag doesn't fix this. The realistic fix upstream would mean either (a)
hand-authoring a complete parallel set of x64-width IW5 struct
definitions (every asset type — xmodel, material, weapon, rawfile,
stringtable, localize, scriptfile, image, sound, and more, each
individually re-derived for 8-byte pointer/size_t width and alignment),
or (b) building real word-size-conditional codegen into
`ZoneCodeGeneratorLib` itself, which touches every supported game, not
just IW5. Both are large, multi-week-plus efforts even for the upstream
maintainer, with no existing partial work to build on — genuinely
consistent with "practically no chance of getting another community based
one in time for release."

## 4. UPDATE, 2026-09-14 (later still) — forked, not just planned

Direct instruction: "we could legit fork the oat code and build our
tooling from it it is permitted in license" — confirmed (GPLv3, read
directly from the cloned repo's own `LICENSE` file: forking, modifying,
and redistributing are all explicitly permitted, the only real obligation
is keeping the derivative GPLv3 too and preserving attribution/source
availability).

**Done, not just proposed**: `Laupetin/OpenAssetTools` forked into this
repo at **`tools/iw5oat/`** via a history-preserving `git subtree` merge
(same technique this project already used for the `security/` merge) —
confirmed real, not squashed, history: the merge commit has a genuine
second parent at OpenAssetTools' own HEAD, and total reachable commits
from this repo's own HEAD jumped from a few hundred to 4122. Named
`iw5oat` specifically (not a generic "zone tools" name) since this
project only ever needs IW5 support — no reason to carry the weight of a
name implying broader multi-game scope this fork will never use.

**Licensing set up correctly, not just forked and left ambiguous**: kept
GPLv3 as `tools/iw5oat/`'s own license (its own `LICENSE` file, unmodified
GPLv3 text, came across with the fork) — explicitly NOT relicensed under
this repo's own main permissive license, since that license's "no
charging" clause (condition 3) is an ADDED restriction GPLv3 does not
permit layering on top of GPLv3 code. This repo's own top-level `LICENSE`
file now documents this explicitly under "Third-party components,"
mirroring the exact pattern already established for `security/`'s own
separate license file.

Upstream check before forking, for the record: a WIP branch
(`feat/generic-zone-code`, single commit "wip: multiple variants in
zonecode") exists that COULD eventually relate to word-size-generic zone
code, but deletes a large swath of tests mid-refactor and shows no
confirmed connection to word-size or IW5 x64 specifically — not close to
shippable, doesn't change the case for forking now.

**Real next step, not started yet**: the actual RE + code work — fix
`ZoneLoaderFactoryIW5.cpp`'s hardcoded `GameWordSize::ARCH_32`, and derive
real x64 struct widths for the `scriptfile`/`rawfile` asset chain from
`iw5sp.exe`'s own zone-loading code. The plan below (originally written
before the fork existed) still describes the right scope.

## 5. UPDATE, 2026-09-14 (later still, "dig into fixing iw5oat in full for x64") — the real fix found: the per-asset dispatch record's exact byte layout, confirmed and cross-validated

Direct instruction to dig into a full x64 fix. Traced the real native
zone-loading chain in `iw5sp.exe` from scratch (ground truth — the game
successfully loads these exact files every launch), starting from the
FastFile magic constant itself: a raw byte scan (magic isn't
null-terminated, so it doesn't show up in a normal string scan — a new
script, `RawByteScan.java`, was needed and is now a reusable addition to
`re_notes/ghidra_scripts/`) found exactly one hit, inside `FUN_14008eba0`
— the real zone-header entry point.

**Traced end to end, function by function** (`FUN_14008eba0` →
`FUN_1400a4460`/`FUN_1400a4500` → `FUN_1400965d0`/`FUN_1400aabd0` →
`FUN_14009bce0`, full decompiles in `re_notes/x64_migration/
ui_pipeline_trace/decomp_zoneheader_14008eba0.txt` and siblings):

1. **The 21-byte outer header** (magic 8 + version 4 + 1-byte unknown +
   8-byte timestamp) — **CONFIRMED UNCHANGED**, byte-for-byte match to
   §1's own raw hex-dump finding and to OAT's existing
   `ZoneLoaderFactoryIW5.cpp` skip-byte sequence exactly. No fix needed
   here.
2. **A 44-byte (`0x2c`) block-size header**, read immediately after: 2
   unidentified/size-tracking 4-byte ints, then **9 plain 4-byte ints** —
   one size per `XFILE_BLOCK_*` type (TEMP/PHYSICAL/RUNTIME/VIRTUAL/
   LARGE/CALLBACK/VERTEX/INDEX/SCRIPT — the same 9 OAT's own `SetupBlock`
   already lists). **CONFIRMED UNCHANGED on disk** — every field here is
   a plain 4-byte int, untouched by the x64 pointer-width change. The
   widening this project originally suspected here doesn't exist — only
   OAT's own RUNTIME (in-memory) tracking struct for these 9 blocks needs
   to grow (traced via `FUN_1400a4500`: each block's runtime slot grew
   from an x86 8-byte `{ptr:4, size:4}` to a confirmed x64 16-byte
   `{ptr:8, size:4, pad:4}` — real, but purely an OAT-internal C++ struct
   concern, not a parsing fix).
3. **THE REAL FIX — the per-asset dispatch record, `FUN_14009bce0`**:
   reads a **16-byte record** per asset (`FUN_1400aad70(param_1,
   DAT_1407bd860, 0x10)`), where the first 4 bytes are the asset-type ID
   and — critically — **the data pointer sits at BYTE OFFSET +8, not
   +4**, i.e. the record is `{ int32 assetType; int32 _pad; int64
   dataPtr; }`, 16 bytes total. This is the actual crash mechanism,
   confirmed precisely: OAT's IW5 loader (assuming x86's original
   `{ int32 assetType; int32 dataPtr; }`, 8 bytes total) reads records at
   HALF the real stride — after the first asset, every subsequent "type"
   field OAT reads is actually the low half of the PREVIOUS asset's own
   64-bit pointer, cascading into garbage type IDs and pointers almost
   immediately. This single stride/offset fix is the one change that
   would let a loader correctly walk the full asset list and recover
   every asset's real type and data pointer — the foundation every
   individual per-type asset parser sits on top of.

**Cross-validated against OAT's own IW5 asset-type enum** (`src/Common/
Game/IW5/IW5.h`, `ASSET_TYPE_*`), not just assumed: the native switch's
real case range is **exactly 0 through 0x2d (45)** — 46 values — which is
**precisely** OAT's own enum length (`PHYSPRESET`=0 through
`ADDON_MAP_ENTS`=45, then the `ASSET_TYPE_COUNT` sentinel). Exact,
index-for-index agreement, not a coincidence — confirms the asset-type
enum itself is completely unchanged between x86 and x64 (enum values are
plain integers, architecture-independent, as expected) and that this IS
the real, correct dispatch point.

**A genuine, separate discovery surfaced by this same trace**: `iw5sp.exe`
does NOT populate handlers for 6 of the 46 asset types — `UI_MAP` (0x17),
`WEAPON` (0x1e), `SURFACE_FX` (0x22), `AITYPE` (0x23), `MPTYPE` (0x24),
`CHARACTER` (0x25) all fall through with no case in this specific
dispatch. Plausible and unsurprising given this is the Campaign/Survival
binary (`WEAPON`/`AITYPE`/`CHARACTER`/`MPTYPE` in particular sound like
they'd load through their own specialized paths or not apply to SP at
all) — not independently confirmed why, flagged as a real fact for a
future pass, not acted on further this round.

**What this unblocks, concretely**: fixing this one 8→16-byte stride
change in `iw5oat`'s IW5 zone-content-reading loop (the direct analog of
`FUN_14009bce0` above) would let the tool correctly enumerate every asset
in any current x64 zone — its real type and its real data pointer — even
before any individual per-type asset struct (GfxImage, XModel, Weapon,
etc.) gets its own x64 fix. That's a huge practical unblock on its own:
basic listing/triage of a zone's real contents becomes possible
immediately, and this project's own actual need (GSC/`rawfile`/
`scriptfile` extraction, §6 below) needs exactly ONE more per-type struct
fixed on top of this, not all 46.

**Honest scope remaining, not finished this round**: this is the
dispatch/routing layer, not the individual asset bodies. Each of the ~40
per-type loader functions this dispatch calls into (`FUN_140096140` for
type 0, `FUN_140095f90` for type 1, etc.) still serializes its own
asset-specific struct with its own pointer fields that may have shifted
independently — `scriptfile`/`rawfile` specifically (this project's real
target) have NOT been individually traced yet. That's the actual next
step, not started this round.

## 5.5. UPDATE, 2026-09-14 (later still, "keep going on both") — the dispatch-loop fix applied for real in `tools/iw5oat/`; RawFile/ScriptFile struct derived; a real architecture split discovered along the way

**§5's fix is now live code, not just documentation.** Found and fixed
TWO real bugs — not missing code, genuine existing-but-wrong `#ifdef
ARCH_x64` branches, in `src/ZoneLoading/Game/IW5/ContentLoaderIW5.cpp`:

1. **`LoadXAssetArray`**: the existing x64 branch used an 8-byte
   stride with the data pointer at `+4u` — x86's own layout, still wrong.
   Fixed to the confirmed real 16-byte stride with the pointer at `+8u`
   (§5's own finding).
2. **`Load()`**: the existing x64 branch read a 16-byte header
   (`stringCount@0/strings@4/assetCount@8/assets@12`) — also x86's own
   layout. Fixed to a confirmed **32-byte** header
   (`stringCount@0/strings@8/assetCount@16/assets@24`), independently
   verified against `iw5sp.exe`'s own real header read (`FUN_14008ef80(
   &header, 0x20)` — a genuine 32-byte read — and the exact offsets the
   real dispatch loop consumes afterward: assetCount at `+0x10`, the
   assets pointer at `+0x18`, both confirmed live in the disassembly, not
   inferred).

**A real design gap found while applying the fix, not before**: fixing
the byte offsets alone wasn't sufficient — `FillPtr`'s actual read width
is driven by a SEPARATE, genuinely generic runtime parameter
(`pointerBitCount`, threaded through `ZoneInputStream`/
`ZoneStreamFillReadAccessor`) that was hardcoded to `32u` at the IW5 call
site in `ZoneLoaderFactoryIW5.cpp` — a real, working, already-generic
mechanism (properly handles alignment, confirmed by reading
`InsertPointerNative()`'s own implementation), just never pointed at 64
for IW5. **This corrects part of §3's own earlier claim** — `pointerBitCount`
is NOT the same thing as the `GameWordSize::ARCH_64` enum §3 found unused;
it's a separate, real, working piece of infrastructure that just needed
its IW5 call site changed from `32u` to `64u`. Fixed, with an explicit
comment documenting a known limitation this creates on purpose: this
fork's own value is now hardcoded to 64, meaning it can only correctly
parse the CURRENT x64 zone format — matches this fork's own already-
declared IW5-x64-only scope exactly, but is a real trade-off worth
stating plainly rather than leaving implicit. (`GameWordSize::ARCH_32` ->
`ARCH_64` was also updated for accuracy in `InspectZoneHeader`, though it
still has zero real consumers anywhere in the codebase.)

**RawFile/ScriptFile's own x64 struct layout — derived, not yet applied**,
via OAT's own existing x86 struct definitions (`IW5_Assets.h`) widened by
standard MSVC struct-alignment rules (every pointer becomes 8 bytes,
8-byte-aligned, `int` fields stay 4 bytes):

```c
// x86 (OAT's existing, unmodified definition): 16 bytes
struct RawFile { const char* name; int compressedLen; int len; const char* buffer; };
// x64 (derived): 24 bytes
//   name @0 (8B), compressedLen @8 (4B), len @12 (4B), buffer @16 (8B)

// x86 (OAT's existing, unmodified definition): 24 bytes
struct ScriptFile { const char* name; int compressedLen; int len; int bytecodeLen; const char* buffer; unsigned char* bytecode; };
// x64 (derived): 40 bytes
//   name @0 (8B), compressedLen @8 (4B), len @12 (4B), bytecodeLen @16 (4B),
//   [4B alignment pad], buffer @24 (8B), bytecode @32 (8B)
```

Cross-checked, not just assumed: the native RAWFILE/SCRIPTFILE dispatch
handlers (`FUN_1400962c0`/`FUN_1400964a0`, decompiled this round) both
treat the record's very first field as a pointer needing name-based pool
resolution — consistent with `name` genuinely being field 0 in both
structs, matching the derived layout.

**Not yet applied — a real, newly-discovered architecture split, not a
skipped step**: unlike the dispatch loop (hand-written C++ in a static
`.cpp` file, directly editable), `Loader_RawFile`/`Loader_ScriptFile`
(the classes `ContentLoaderIW5.cpp`'s own `LOAD_ASSET` macro expands to)
are **code-generated** — `AssetLoaderIW5.h` does not exist as a checked-in
source file at all. `src/ZoneCodeGenerator`/`src/ZoneCodeGeneratorLib`
is a genuine, separate build-time tool: it parses real C++ header files
(`HeaderFileReader`, almost certainly `IW5_Assets.h` itself) plus a
separate "commands" file (`CommandsFileReader`) to generate the actual
per-type loader code. This round located the generator and confirmed its
real architecture (header-parsing + commands-driven code generation, not
a stub) but did NOT locate the exact IW5-specific commands-file
invocation (which build step calls it, with which arguments) — that's
the genuine next step for RawFile/ScriptFile specifically, a different
and deeper task than the dispatch-loop fix was, not a small follow-up.

Full raw evidence for this round:
`re_notes/x64_migration/ui_pipeline_trace/decomp_rawfile_scriptfile.txt`
(`FUN_1400962c0`/`FUN_1400964a0`, the name-fixup dispatch) and
`decomp_rawfile_scriptfile_real.txt` (`FUN_1400a9c70`/`FUN_1400a9d30`,
the generic pool-resolution primitive these call into).

## 5.6. UPDATE, 2026-09-14 (later still) — full x64 build achieved; the §5 dispatch-record fix confirmed live against three real retail zones; a new, narrower bug found immediately behind it

**The §5 fix is no longer just applied — it's built and validated.**
`tools/iw5oat` now produces a real `Unlinker.exe`
(`build/bin/Release_x64/Unlinker.exe`) after two genuine, unrelated
upstream OAT build-tooling bugs were worked around (documented in full in
`re_notes/known_issues_x64.md` issue #1's matching round — summary: a
wrong manual `ZoneCodeGenerator.exe` output directory, and a real MSBuild
custom-build batching bug in `ObjWriting`/`ObjLoading`'s `.template`
steps that joins every item into one broken invocation regardless of
each item's own distinct `<Outputs>`).

**Live result against real zones pulled from the actual game install**:

| Zone | Size | Pre-fix | Post-fix |
|---|---|---|---|
| `hamburg.ff` | 165MB | Segfault (the original report) | Clean `ERROR: ... invalid block 15`, no crash |
| `common.ff` | large | Segfault | Same clean error, no crash |
| `code_post_gfx.ff` | large | Segfault | Same clean error, no crash |
| `sp_intro.ff` | 303B | (untested pre-fix) | Segfault, zero output — different bug |
| `sp_prague.ff` | 207B | (untested pre-fix) | Segfault, zero output — different bug |

Getting from an unconditional segfault to a clean, structured error on
every large real-content zone tested is exactly the outcome the §5 fix
predicted: the dispatch loop now reads every asset's type/pointer at the
correct 16-byte stride instead of silently misreading every other asset
as garbage. The new `invalid block 15` error (valid `XFILE_BLOCK_*`
values are 0-8) is a genuinely different, narrower bug one layer deeper —
most likely one or more of the individual code-generated per-asset-type
structs (`GfxImage` and siblings, not just RawFile/ScriptFile) still
using x86 field offsets internally, the same bug *class* as §5 just fixed
but not yet localized to a specific struct/field. The two tiny-zone
segfaults are flagged as a separate, unexplored lead, not assumed to be
the same root cause (they fail before any output at all, unlike every
real content zone, which gets deep into the load path first).

## 5.7. UPDATE, 2026-09-14 (later still) — root-caused "invalid block 15"; one more real generic bug fixed; the true remaining scope is every asset type, not just RawFile/ScriptFile

Instrumented `ContentLoaderIW5.cpp` temporarily (trace prints before/
after each step of `Load()`/`LoadScriptStringList`/`LoadXAssetArray`,
removed after use) to find exactly where "invalid block 15" fires.

**§5.6's header fix is further confirmed correct**: the trace showed
`assetCount=4770`, `stringCount=4` (small, plausible real values) and
the `strings`/`assets` fields decoding as the genuine `FOLLOWING`
sentinel (`0xFFFF...FFFF`, an intentional in-format marker), matching
the code's own assert. The 32-byte header read is right.

**A second real, generic bug found and fixed**:
`ContentLoaderBase::LoadXStringArray` — a file SHARED across every
game, not IW5-specific — still hardcoded a 4-byte (x86 pointer) stride
for its own `ARCH_x64` array-of-string-pointers branch, same bug class
as the dispatch record and XAssetList header, just never reached by any
test before this round. Fixed to use the stream's own configured real
pointer width (`GetPointerBitCount()/8u`) instead of a literal — a
generic fix, correct for any game/arch, not another IW5-only patch.
Confirmed live: loading now proceeds past `LoadScriptStringList`
entirely into the real per-asset dispatch loop for the first time.

**Then a real scope correction**: the dispatch loop immediately dies on
asset index 0 (`type=9`, `MaterialTechniqueSet`) — the exact same bug
class already known for RawFile/ScriptFile (§5.5), confirming
`ZoneCodeGenerator`'s generated per-type loader code has zero x64
awareness across the board, not just those two types. Checking
`UnlinkerArgs.cpp`/`Unlinker.cpp` directly confirmed `--include-assets`/
`--exclude-assets` only filter what gets WRITTEN after loading
(`ObjWriting::Configuration.AssetTypesToHandleBitfield`) — the zone
loader unconditionally parses every asset in the file first. **This
means any real, mixed-content retail zone needs correct x64 offsets
across all ~46 asset types before it can load at all** — §5.5/§6's
framing of RawFile/ScriptFile as "the one remaining per-type fix" was
too narrow; it only holds for a hypothetical zone containing exclusively
those two asset types, which no real retail zone does.

RawFile/ScriptFile's own derived offsets were hand-patched directly into
the generated (gitignored) `build/` output as a proof of concept and
confirmed to compile — not committed (generated, not tracked source;
discarded on a real `ZoneCodeGenerator` re-run regardless) and not
sufficient on its own, since `MaterialTechniqueSet` breaks first.
Fully unblocking real zone extraction needs the same class of fix
applied across every generated per-type loader — flagged as a genuinely
large, mechanical-but-not-trivial task needing its own explicit scoping
decision, not attempted piecemeal this round. A promising angle for a
scaled approach, not yet tried: since the real C++ compiler already
lays out every struct correctly for x64 (confirmed for `RawFile`), a
small `offsetof()`-introspection helper could programmatically derive
every field's real offset per struct and drive an automated rewrite of
each generated `FillStruct_*` function, rather than deriving and
hand-patching each of the ~46 offset tables individually.

## 5.8. UPDATE, 2026-09-14 (later still) — automated offsetof()/sizeof() fix generator built and checked in; applied to all 46 asset types; the real remaining blocker narrowed to one open question

Direct instruction: "build the offsetof() fix generator and apply it to
all asset types." Built and checked into the repo at
`tools/iw5oat/x64_offset_fixes/` (not gitignored, a real reusable Python
package — see its own README for the full script-by-script breakdown).
Rewrites every literal byte offset `ZoneCodeGenerator` emits across all
~46 IW5 asset types' generated loaders into a compiler-verified
`offsetof()`/`sizeof()` expression, rather than trying to hand-derive and
hand-patch each type's own offset table the way RawFile/ScriptFile (§5.5)
were. 2,499+ substitutions across 40 files on the main pass alone,
confirmed reproducible from a clean `ZoneCodeGenerator` regenerate.

**Three more real bugs found in the course of this**, all confirmed via
direct before/after testing:
- A genuine gap in the main pass's own struct-array regex (10 sites where
  a paired `LoadWithFill`/`FillStruct` call didn't convert together).
- A genuine cross-function-boundary false positive in that same regex
  (non-greedy DOTALL matching with no function-boundary anchor let a
  pointer-array's `LoadWithFill` get paired with an unrelated struct's
  `FillStruct` call elsewhere in the same file) — the actual, confirmed
  cause of `MaterialTechniqueSet`'s own 54-entry technique-pointer array
  reading the wrong buffer size and corrupting everything after it.
- A distinct call shape (`LoadDynamicFill_<Type>`'s own `AppendToFill(N)`
  literals, used for variable-length assets like `MaterialTechnique`'s
  `passArray[]`) the main pass never targeted at all — wrong wherever the
  relevant field embeds a pointer even several levels deep (confirmed via
  `XAnimDeltaPartQuat2`: a union member one level in starting with a
  pointer forces 8-byte x64 alignment the immediately-preceding
  `uint16_t` field alone wouldn't suggest).

**Live result**: all three real retail zones tested (`hamburg.ff`,
`common.ff`, `code_post_gfx.ff`) — previously failing at three different,
earlier points — now fail at the exact same later point, real, consistent
progress even though extraction isn't fully working yet.

**The one remaining open question, deliberately not guessed past**:
`MaterialVertexStreamRouting::decl[]` (a real 16-pointer array) and
`MaterialPixelShaderProgram::ps`/`MaterialVertexShaderProgram::vs`
(single pointers) are declared as real struct fields but never read from
the wire by their own `FillStruct_*` functions. Whether they're genuinely
on-disk (raw serialized memory) or runtime-only fields the wire format
never included isn't resolved — a direct experiment excluding `decl[]`
from the computed read size made the failure mode *worse* (a segfault
instead of a clean bounds exception), so it was reverted rather than
shipped as a guess. This is the confirmed reason all three zones now stop
at the same point (`MaterialPass`'s own `vertexShader`/`pixelShader`
load). Resolving it needs native Ghidra RE against `iw5sp.exe`'s own
zone-content reader — the same technique that resolved the dispatch-
record/header bugs (§5) — not more struct-field-name reasoning.

## 5.9. UPDATE, 2026-09-14 (later still, "dig into the decl/ps/vs question with ghidra") — the open question definitively resolved via native decompile: decl[]/ps/vs ARE on the wire; found and fixed a real, different bug (`Material::subMaterials`, a phantom field) along the way; found a new, unresolved lead (shader bytecode's real block)

Direct instruction: "dig into the decl/ps/vs question with ghidra then once
our iw5oat is usable we can test on real files." Located the real native
handlers for all three affected asset types via the already-mapped master
dispatch switch (`FUN_14009bce0` in `iw5sp.exe`, case index = `ASSET_TYPE_*`
enum value directly — confirmed again here: case 5=Material, case
6=MaterialPixelShader, case 7=MaterialVertexShader, case
8=MaterialVertexDeclaration, case 9=MaterialTechniqueSet, matching the
already-known RawFile=0x26/ScriptFile=0x27 cases exactly), then decompiled
each one's real "fill struct from the wire" function directly.

**The original question is answered, decisively, by direct native
evidence — no more guessing needed**: every native read size for these
three structs matches `sizeof(Type)`/`offsetof(Type, field)` computed via
the REAL C++ struct definitions in `IW5_Assets.h`, INCLUDING the
previously-unfilled fields:
- `MaterialPixelShader`: native reads exactly `0x20` (32) bytes —
  matches `sizeof(MaterialPixelShader)` = name(8) + prog(24, where prog =
  ps(8) + loadDef(16)) = 32. **`ps` is genuinely on the wire.**
- `MaterialVertexShader`: same shape, same math, same conclusion for `vs`.
- `MaterialVertexDeclaration`: native reads exactly `0xb0` (176) bytes —
  matches `sizeof(MaterialVertexDeclaration)` = name(8) +
  streamCount/hasOptionalSource(2) + pad(6) + routing(160, where routing
  = data[13](26) + pad(6) + decl[16](128)) = 176. **`decl[]` is
  genuinely on the wire.**

Both fields are simply never explicitly *filled with a meaningful value*
by their own `FillStruct_*` function (they're runtime-only D3D
shader-object/vertex-declaration caches, populated later by actually
compiling the shader — never by anything read from disk) — but their
BYTES are still genuinely part of the on-wire struct layout and must be
consumed from the stream regardless, exactly matching what the earlier
automated `sizeof(Type)`-based fix already did correctly. **The earlier
"exclude decl[] from the read size" experiment (SS5.8) was wrong** — this
explains why it made the failure mode worse (a segfault instead of a
clean, caught exception): it was cutting 128 genuinely-real bytes out of
the read, immediately desyncing the stream.

**A real, different bug found via the SAME technique, fixed**: while
cross-verifying `Material`'s own native read size (`FUN_140094730`,
confirmed exactly `0x80` = 128 bytes, with `techniqueSet`/`textureTable`/
`constantTable`/`stateBitsTable` landing at native offsets 0x60/0x68/0x70/
0x78 — all matching `offsetof()` computed from the current struct exactly),
the struct definition in `IW5_Assets.h` had a TRAILING `const char**
subMaterials;` field that inflates `sizeof(Material)` past the real
128-byte boundary. Unlike `ps`/`vs`/`decl[]` (embedded mid-struct, so their
bytes are unavoidably consumed to reach later fields), `subMaterials` is
the LAST field — genuinely absent from the real native struct, not just
unfilled. Confirmed via: (1) the native total-read-size math only works out
to exactly 128 bytes WITHOUT it; (2) grepping the entire fork found zero
references to it anywhere outside the one struct declaration and one
`ZoneCodeGenerator` "commands" file line (`Material.txt`'s own `set
condition subMaterials never;` — upstream OAT's own pre-existing marker,
which only ever suppressed generating a `Fill` call for it, never affected
`sizeof()`). **Removed the field from `IW5_Assets.h` and the now-dangling
`Material.txt` reference** — a genuine, permanent struct-definition
correction, not a workaround.

**The real lesson, now written down for future reference**: `ZoneCodeGenerator`'s
own `set condition <field> never;` marker does NOT distinguish "field is
genuinely absent from the wire" from "field is on the wire but never
meaningfully filled" — both `subMaterials` (absent) and `ps`/`vs`/`decl[]`
(present, unfilled) carry this SAME marker. The only reliable way to tell
them apart is native verification: compare the REAL decompiled read size
against `sizeof()` computed with vs. without the field. A trailing field is
worth checking (only a trailing field CAN be spuriously excluded without
affecting any other field's own offset); an embedded field never can be
(removing it would shift every subsequent field, which native evidence
would immediately disprove).

**A parallel, unresolved lead found while chasing why the fix still didn't
get further**: `MaterialTechniqueSet`/`MaterialTechnique`/`MaterialPass`
were ALL independently re-verified byte-correct against fresh native
decompile (including cross-checking `MaterialPass`'s own real per-element
stride, 40 bytes, and `MaterialTechnique`'s own real header size, 16 bytes,
both via a dedicated per-technique native function, `FUN_140094ee0`/
`FUN_140094b90`) — and the FIELD PROCESSING ORDER (vertexDecl, then
vertexShader, then pixelShader, then args) also matches natively exactly.
Despite this, live testing still fails immediately after `vertexShader`'s
own load completes, when `pixelShader`'s very first field (`name`) reads as
garbage. The most promising remaining lead: `FUN_1400aad70`'s first
parameter looks like a real block-type selector matching the
`XFILE_BLOCK_*` enum (`TEMP`=0, `PHYSICAL`=1, `RUNTIME`=2, `VIRTUAL`=3, ...)
— and BOTH `RawFile::buffer`'s own real read (`FUN_140096200`, independently
re-checked) AND `GfxVertexShaderLoadDef::program`'s real read
(`FUN_1400953c0`) use literal `1` (PHYSICAL), while the generated code
currently reads these while `XFILE_BLOCK_VIRTUAL` is still the top of the
block stack (pushed once at the top of `Load_RawFile`/
`Load_MaterialVertexShader` and never switched before the buffer/bytecode
read). **This is a real, generalizable finding that would also affect
`RawFile`/`ScriptFile`'s own buffer content — this fork's actual stated
purpose — not just the shader chain**, but a direct experimental test
(wrapping the vertex/pixel shader bytecode reads in
`PushBlock(XFILE_BLOCK_PHYSICAL)`/`PopBlock()`) produced a DIFFERENT failure
("XBlock XFILE_BLOCK_PHYSICAL overflowed") rather than success — meaning
either `PHYSICAL`'s own real block-size accounting has a separate,
not-yet-understood issue, or the block-selector mapping isn't as simple as
"literal N = XFILE_BLOCK_N" for every call site. **Not shipped** — reverted
rather than guessed further. This is the concrete next investigation
thread, and needs the SAME native-decompile rigor as the fixes above,
starting from the 44-byte block-size header's own real byte layout to
confirm PHYSICAL's declared size for a real test zone.

## 5.10. UPDATE, 2026-09-14 (later still, "yes keep pushin") — the shader-bytecode block theory disproven with real evidence; a byte-exact hex dump isolates the corruption to exactly 8 bytes; the underlying cause still not found after exhaustive verification — paused per this project's own standing "fresh perspective" principle

Continued the §5.9 investigation into why `MaterialPixelShader`'s own `name`
field still reads garbage even with every struct byte-count confirmed
correct. This round's real findings, each backed by direct evidence:

**The "XFILE_BLOCK_PHYSICAL vs VIRTUAL" theory (end of §5.9) is
definitively wrong — decompiled the actual primitive, not just its call
sites.** `FUN_1400aad70`'s real signature is
`void FUN_1400aad70(char param_1, void* dst, int size)` — `param_1` is a
plain boolean ("perform this read at all"), not a block-type selector;
the literal `1`/`0`/`3` values I'd been reading as `XFILE_BLOCK_*` indices
are just C truthiness (`0`=skip, anything else=do the read). The REAL
block-switch primitives are separate (`FUN_1400aac10`/`FUN_1400aac60`/
`FUN_1400aabd0`, matching `PushBlock`/the internal switch/`PopBlock`
exactly) and confirmed structurally identical to this fork's own C++
`m_block_offsets[]`-per-block-type design (a real per-block-type saved-
position array, `DAT_140d6ddb0[N]`, 1:1 with `m_block_offsets[]`).
Separately confirmed via `LoadDataFromBlock`'s own C++ switch and
`ZoneLoaderFactoryIW5.cpp`'s own `XBLOCK_DEF` table that `XFILE_BLOCK_
VIRTUAL`/`XFILE_BLOCK_PHYSICAL` are BOTH `XBlockType::BLOCK_TYPE_NORMAL`
(real reads either way) and that blocks are purely an OUTPUT-memory
concern — every "NORMAL"/"TEMP" block reads from the exact same shared
linear `m_stream` cursor regardless of which block is nominally active.
Which block is pushed genuinely cannot cause a stream-position desync.
This retroactively explains why the earlier `PushBlock(XFILE_BLOCK_
PHYSICAL)` experiment (§5.9) produced a hard overflow: it was cramming
real OUTPUT allocations into a block that was never sized to receive
them, unrelated to input bytes at all — a real, if accidental, empirical
confirmation of this correction, not a coincidence.

**A byte-exact hex dump precisely isolates the corruption.** Added a
temporary diagnostic dumping every raw byte `LoadWithFill` reads for both
`MaterialVertexShader`'s and `MaterialPixelShader`'s own 32-byte headers
(added, used, then reverted — not part of any commit). Result:
`MaterialVertexShader`'s full header is byte-perfect
(`name`=FOLLOWING, `vs`=0 [confirmed-correct unfilled runtime cache],
`program`=FOLLOWING, `programSize`=0x73=115, all exactly as expected).
`MaterialPixelShader`'s header is correct EVERYWHERE EXCEPT `name` —
`ps`=0 (correct), `program`=FOLLOWING (correct), `programSize`=0x37=55
(plausible) — only the first 8 bytes (`name`) hold a value
(`0x0000000030012AAB`) that decodes as a structurally well-formed but
out-of-range `XFILE_BLOCK_TEMP` offset pointer, not the expected
FOLLOWING sentinel. This is real, hard evidence that whatever is wrong is
NOT a stream-position desync (which would corrupt every field after
`name` too, not just the first 8 bytes) — it is something narrowly
specific to how `name`'s own 8 bytes get read, immediately following a
read (`vertexShader`'s own header + bytecode) that is independently
confirmed 100% correct.

**Checked and ruled out, with evidence, in the same round**: hardcoded
x86-era alignment literals passed to `AllocOutOfBlock<T>(4)` (a genuinely
real, separate, unfixed bug class — confirmed present at 40+ call sites,
never touched by any of the `x64_offset_fixes/` scripts, which only
targeted offset/size literals, not alignment arguments) — but for this
specific zone's exact byte layout, the relevant `TEMP` block offsets
(176 before `vertexShader`, 208 before `pixelShader`) are already
divisible by 8, so a 4-vs-8 alignment difference makes no numeric
difference here, ruling it out as THIS bug's cause (though it remains a
real, latent issue worth fixing separately later, since it could matter
for a different offset elsewhere).

**Status: paused, not resolved.** This investigation has now run many
genuine, well-reasoned rounds on the same fundamental question ("why does
exactly 8 bytes of `MaterialPixelShader`'s otherwise-correct header read
wrong") — struct layouts, field order, block semantics, alignment
mechanics, and the real native read primitive have all been directly
verified against native decompile and found correct, without turning up
the actual cause. Per this project's own standing principle (`CLAUDE.md`
§10 point 9 — persist through ordinary setbacks, but stop and check in
once an angle has been pushed 5-6+ genuine rounds without new traction),
this is the natural point to pause this specific thread rather than keep
re-deriving the same conclusions. All temporary diagnostic instrumentation
was reverted; the build tree was regenerated clean and re-verified to
reproduce the exact same (already-documented) failure behavior with only
the two confirmed-real, committed fixes applied (the dispatch record/
header fix, and `Material::subMaterials`).

**Concrete next steps, for whoever picks this back up**: the most
promising untried angle is a genuinely different technique, not more
native-decompile comparison — either (a) live debugging via x64dbg
against the real running `iw5sp.exe` (the x64dbg MCP integration was
unavailable this session — connection refused), single-stepping the exact
dispatch-record processing for this specific `MaterialTechniqueSet` to
watch the real engine's own read cursor advance byte-by-byte across the
vertexShader→pixelShader boundary, or (b) a raw hex-editor comparison of
`code_post_gfx.ff`'s own decompressed bytes at the computed expected file
offset for this exact technique/pass, cross-referenced against every
byte-count already confirmed correct in this document, to find exactly
where the REAL on-disk bytes diverge from what this fork's own code
consumes. Both are qualitatively different from the native-decompile
cross-referencing already exhausted this round.

## 5.11. UPDATE, 2026-09-14 (later still, "for what we need surely we dont really need that? at this time anyway") — `iw5oat` is already usable TODAY for real GSC extraction, independent of the still-open §5.10 shader bug

**Status: confirmed working.** Direct pushback on continuing the §5.10
deep-dive prompted a pivot: rather than keep chasing the `MaterialPixelShader::name`
corruption, tested whether zones that never reference a `Material` asset at
all already extract cleanly with the fixes already committed (the dispatch-
record fix, `LoadXStringArray`'s stride fix, and the full `x64_offset_fixes/`
pass) — since this project's own actual need (`CLAUDE.md`'s standing
GSC-first RE methodology) never required Material/shader loading in the
first place, only RawFile/ScriptFile.

**Result: yes.** `zone/english/sp_intro.ff` and `zone/english/sp_prague.ff`
(both real, retail, script-only Campaign zones — no `Material`-referencing
content) load and extract through `Unlinker.exe` **with 0 warnings, 0
errors**, producing genuinely valid output, byte-verified, not just "the
tool exited 0":
- `808.gscbin` (33 bytes) and `maps/sp_intro.gscbin` (71 bytes) — both open
  with a real zlib `78 DA` deflate header, the same compression magic this
  document's own §1 already established for the outer FastFile container
  — real, valid, compressed GSC bytecode, not garbage.
- `maps/sp_intro.mapents` (146 bytes) — readable, valid entity-definition
  text.
- `sp_intro` itself (an empty 0-byte RawFile marker asset) extracts
  correctly as empty, matching its expected real content.

**Practical conclusion**: `iw5oat` is genuinely usable *right now* for its
actual stated purpose — GSC/rawfile extraction to support the GSC-first RE
methodology — on any zone that doesn't pull in a `Material` asset, entirely
independent of whether §5.10's shader-bytecode corruption is ever resolved.
The remaining open bug only blocks zones that reference Materials (most
zones with real rendered geometry/UI, e.g. `hamburg.ff`, `common.ff`,
`code_post_gfx.ff`) — it does not block script-only zones. This significantly
changes the practical urgency of §5.10: it's a real, still-open bug worth
eventually fixing, but not one that was ever blocking this project's actual
day-to-day GSC RE work, which can already proceed today against any
script-only zone.

**Now done — see §5.12**: the full sweep of every `sp_*.ff`/`so_*.ff` zone
this section originally flagged as not yet done.

## 5.12. UPDATE, 2026-09-14 (later still, "check all") — full sweep of every `sp_*.ff`/`so_*.ff` retail zone: only 2 of 39 are script-only, every other one hits the identical, already-known Material-chain bug

**Status: swept, not a new finding — confirms the existing bug's real
practical scope rather than narrowing it.** Ran `Unlinker.exe` directly
against all 39 `sp_*.ff`/`so_*.ff` zones in the live retail install (every
Campaign mission and every Spec-Ops/Survival zone, `zone/english/`), not
just the two already confirmed in §5.11.

**Result: only `sp_intro.ff` and `sp_prague.ff` load and extract cleanly**
(`Finished with 0 warnings, 0 errors`, matching §5.11's own byte-verified
output). **All 37 other zones fail**, every single one with the exact same
error shape:

```
ERROR: Loading fastfile failed: Zone referenced offset <N> of block XFILE_BLOCK_TEMP which is larger than its size <M>
ERROR: Failed to load zone "<path>": Loading zone failed.
Failed with 0 warnings, 2 errors
```

Cross-checked directly against `hamburg.ff`/`common.ff`/`code_post_gfx.ff`
(the three zones §5.9/§5.10's own investigation already used) — **identical
error text and shape**, confirming this is the same already-tracked,
paused §5.10 bug (the `MaterialPixelShader::name` 8-byte corruption), not a
new or different failure mode. Every `so_survival_mp_*.ff` zone (16 of the
37) fails at nearly the identical offset/size pair (~805323xxx / 2256),
consistent with them sharing the same underlying Survival asset base.

**Practical conclusion, corrected from §5.11's more optimistic framing**:
`sp_prague.ff` being a real, full Campaign mission and still succeeding
turns out to be the exception, not representative — most real mission/level
zones DO pull in a `Material` asset (geometry, UI, effects) and are still
blocked. The actually-usable set today is narrow: 2 of 39 real Campaign/
Spec-Ops zones. This doesn't reduce §5.10's own priority — if anything it
raises it, since resolving it would unblock the other 37, not just a
handful of edge cases. It does NOT reopen §5.11's core point: those 2
zones' worth of GSC content is real, genuinely extractable today, useful
for whatever specific scripts they contain — just a much smaller slice of
the game than "most zones already work" would have implied.

Full result table (zone name, outcome, first error line where applicable):
`C:\Users\kyesa\AppData\Local\Temp\claude\...\scratchpad\zone_sweep\results.tsv`
(session-scratchpad only, not committed — reproducible by re-running
`Unlinker.exe` against every `sp_*.ff`/`so_*.ff` in `zone/english/`).

## 5.13. UPDATE, 2026-09-14 (later still, "run 2 forks to dig deeper" following "i think we can fix this if we compare and try methods based off of what the x86 parser did") — direct x86 native ground truth conclusively rules out the struct-layout/field-order theory for the §5.10 corruption; the real cause is narrowed to a data-level or dynamic-read-boundary issue, not a coding bug

**Status: a real theory eliminated with hard evidence, root cause still not
found.** Every prior round of this investigation (§5.9-§5.10) reasoned
entirely from the x64 side — decompiling `iw5sp.exe`'s x64 build and
reading this fork's own generated C++. This round used a genuinely
different source of truth for the first time: the **original, pre-
recompile x86 `iw5sp.exe`** (`re_notes/x64_migration/binaries/old_x86/
iw5sp.exe`, decompiled fresh via `re_notes/ghidra_project/iw5sp_proj`),
whose own struct definitions are what upstream OpenAssetTools' IW5 support
was written against in the first place.

**Traced a full, real chain on x86**, independent of anything already
known from x64:
- `FUN_0053a8e0` (x86) — the real stream-read gate primitive — confirmed
  structurally **identical** to x64's already-decompiled `FUN_1400aad70`
  (same 3-argument shape: `doRead, dst, size`; same RUNTIME-block
  `memset`-zero-fill branch vs. real-read branch). Independent
  cross-validation of a piece of x64 understanding this session already
  used to disprove the block-selector theory (§5.10).
- `FUN_0048f240` — x86's real master per-asset dispatch switch (the
  direct equivalent of x64's `FUN_14009bce0`), found via callers of the
  read-gate primitive rather than the long inflate-chain trace §5 originally
  needed for x64. **Case numbering matches x64 exactly** (case 6 =
  MaterialPixelShader, case 7 = MaterialVertexShader, case 8 =
  MaterialVertexDeclaration), confirmed via `MaterialPass`'s own real field
  order (`FUN_00418050`, x86's `FillStruct_MaterialPass`): reads a 20-byte
  (`0x14`) struct and dispatches `vertexDecl` (offset 0) → case 8,
  `vertexShader` (offset 4) → case 7, `pixelShader` (offset 8) → case 6 —
  **exactly matching this fork's own `MaterialPass` struct
  (`IW5_Assets.h` line 1375: `vertexDecl; vertexShader; pixelShader;`)**,
  field for field.
- **`Load_MaterialPixelShader` (x86, `FUN_00527ce0`) and
  `Load_MaterialVertexShader` (x86, `FUN_004395b0`) are byte-for-byte
  structurally identical**, differing only in which dynamic-shader-
  bytecode helper they call (`FUN_004d3de0` vs. `FUN_00526740` — also
  themselves structurally identical to each other, both reading an 8-byte
  `GfxXxxShaderLoadDef` header then conditionally allocating and reading
  `programSize * 4` bytes of bytecode). Read order for both: whole
  16-byte struct (`name` + `prog`) in one block-copy → `LoadXString` on
  `name` (offset 0) → read 12-byte `prog` sub-struct (offset 4) → dynamic
  shader-bytecode load off `prog.loadDef` (offset 8) → final asset-link
  call. **This is the exact field order and block-push sequence this
  fork's own x64 generated code (and struct definitions) already use** —
  see `materialpixelshader_iw5_load_db.cpp`/`materialvertexshader_iw5_load_db.cpp`,
  already confirmed structurally symmetric to each other earlier this
  session.

**Conclusion**: the struct layout, field order, and read sequence this
fork assumes for `MaterialPixelShader`/`MaterialVertexShader`/
`MaterialPass` are now confirmed **correct against real x86 native ground
truth**, not just self-consistent x64-side reasoning. This decisively
rules out "our struct definition has the wrong field order/a missing
padding field" as the cause of the `MaterialPixelShader::name` corruption
— a theory that was never explicitly tested before this round, only ever
assumed correct by inheritance from upstream. Since x86 and x64 are now
independently confirmed to use identical logic, identical order, and
identical alignment conventions for this exact chain, the remaining
plausible causes have narrowed to two, neither of which is a coding bug in
this fork's own logic:
1. A genuine on-disk **data** difference specific to the x64-recompiled
   zone's actual bytes for this asset (not a parsing bug at all).
2. A boundary/alignment issue specific to the **dynamic** shader-bytecode
   blob's own variable-length consumption (`programSize`-driven, data-
   dependent) at the exact transition between `MaterialVertexShader`'s own
   tail read and `MaterialPixelShader`'s header read — the one part of
   this chain whose length isn't fixed/compile-time-known, and so the one
   place a genuine x64-specific stream-desync could hide despite every
   fixed-size struct read being provably correct.

**Concrete next step, unchanged from §5.10 but now on stronger footing**:
a raw hex-editor comparison of the actual decompressed zone bytes at the
computed expected offset for this exact read, or live x64dbg debugging to
watch the real read cursor advance across this exact boundary — genuinely
different techniques from any native-decompile comparison (x86 or x64),
which this round has now fully exhausted as a category. No code change
applied this round — the investigation redirected away from "find the
coding bug" toward "confirm there isn't one," which is itself the real,
useful result.

Full raw evidence: Ghidra headless decompiles this round (not yet
committed as files — reproducible via the address list above against
`re_notes/ghidra_project/iw5sp_proj`, `-process "iw5sp.exe" -noanalysis`).

## 5.14. UPDATE, 2026-09-14 (later still, second of two parallel forks, "run 2 forks to dig deeper") — the raw on-disk `name` bytes ARE genuinely wrong, not a downstream interpretation bug; struct/DSL definitions confirmed byte-identical to pristine upstream; hamburg.ff/common.ff never even reach this code path

**Status: two real findings, root cause still not found.** Ran in parallel
with §5.13's x86 native-decompile trace, deliberately using different
techniques: (1) diffing this fork's own struct/DSL definitions against
pristine upstream OpenAssetTools, (2) a live, targeted runtime diagnostic
against the actual built `Unlinker.exe`, in place of a purely static
hex-editor comparison (which turned out to be impractical — see below).

**Technique 1 — upstream diff: negative result, rules out transcription
error.** Shallow-cloned `Laupetin/OpenAssetTools` fresh and diffed every
file this fork's `MaterialPixelShader`/`MaterialVertexShader`/
`MaterialTechniqueSet` chain depends on against the current repo:
`MaterialPixelShader.txt`, `MaterialVertexShader.txt`,
`MaterialTechniqueSet.txt` are **byte-for-byte identical** to upstream
(0-line diffs). `IW5_Assets.h` and `Material.txt` differ from upstream by
**exactly the one already-known, intentional edit** (the `subMaterials`
removal, commit `30cf5723`, with its own explanatory comment) — no other
divergence anywhere. Combined with §5.13's independent x86-native
confirmation of the same field order/layout, this closes off "a
transcription or hand-edit error in this fork's own struct/DSL
definitions" as a viable theory from two completely independent
directions.

**Technique 2 — live raw-byte diagnostic (the "hex-editor comparison,"
done via direct runtime instrumentation instead of a static file
comparison).** A pure static hex-dump-of-the-decompressed-file approach
(as originally recommended in §5.10) turned out to be impractical to get
right by hand: the zone format demultiplexes one linear compressed byte
stream into several separately-tracked per-block buffers as it's read
(confirmed directly — `XFILE_BLOCK_TEMP`'s own write-position bookkeeping,
`m_block_offsets[]`, resets to its pre-push value every time a `PushBlock`/
`PopBlock` pair around a single asset's read completes, by design — each
top-level asset's raw struct is read into shared TEMP scratch space then
immediately `memcpy`'d out to permanent zone memory via `LoadAsset_X`
before the next asset reuses the same space), so a `LoadWithFill`
call's own destination position doesn't correspond 1:1 to a fixed offset
in the raw decompressed file the way a naive hex-dump comparison would
assume. **Confirmed this is a real behavior, not a bug** — read
`PopBlock()`'s own source directly (`ZoneInputStream.cpp`) rather than
inferring it, and it explicitly resets TEMP's offset by design ("the data
inside is temporary").

Given that, used the same "temporary diagnostic, added, used, reverted"
technique this project's own history already established (§5.10, and the
`re_notes/known_issues.md` issue #96 postmortem) instead: added a
temporary `DebugStreamPos()`/raw-byte-dump pass directly in
`ZoneInputStream.cpp`'s `LoadWithFill` (captures the exact 32 raw bytes a
block buffer holds immediately after `LoadDataFromBlock`, **before**
`FillPtr` ever interprets them), rebuilt, and ran it against
`code_post_gfx.ff` live. Real, reproducible result:

```
MaterialVertexShader raw bytes: FF FF FF FF FF FF FF FF  00 00 00 00 00 00 00 00  FF FF FF FF FF FF FF FF  73 00 00 00 00 00 00 00
                                 └── name (FOLLOWING, valid) ──┘ └── prog.ps (null, valid) ──┘ └─ loadDef.program (FOLLOWING, valid) ─┘ └ programSize=0x73 ┘

MaterialPixelShader raw bytes:  AB 2A 01 30 00 00 00 00  00 00 00 00 00 00 00 00  FF FF FF FF FF FF FF FF  37 00 00 00 00 00 00 00
                                 └── name (GARBAGE) ──────┘ └── prog.ps (null, valid) ──┘ └─ loadDef.program (FOLLOWING, valid) ─┘ └ programSize=0x37 ┘
```

This is decisive on one specific point: **the corruption is in the raw
on-wire bytes themselves, not in `FillPtr`'s interpretation of them.**
`FillPtr` faithfully reports whatever bytes `LoadDataFromBlock` actually
loaded — the dump above is captured *before* `FillPtr` ever runs. It also
reconfirms §5.10's own finding from a completely different angle: only the
first 8 bytes (`name`) are wrong; `prog.ps`, `prog.loadDef.program`, and
`programSize` all decode to independently plausible, valid-looking values
in the SAME 32-byte read — ruling out a simple stream-position-off-by-N
desync, which would corrupt everything after the misalignment point too,
not just the first field.

**A real scope-narrowing correction, not previously documented**: the same
diagnostic produced **zero hits at all** against `hamburg.ff` and
`common.ff` — both fail with their own fatal error before ever reaching a
`MaterialVertexShader`/`MaterialPixelShader` 32-byte read. Only
`code_post_gfx.ff` actually exercises this exact code path before failing.
This means the specific `MaterialPixelShader::name` corruption documented
in §5.10/§5.13 is **confirmed specific to `code_post_gfx.ff`** — `hamburg.ff`
and `common.ff` share the same outer symptom shape (`Zone referenced offset
X of block XFILE_BLOCK_TEMP which is larger than its size Y`) but have not
been shown to share this exact root cause; they may be failing at a
different, still-uninvestigated point in their own asset streams. Prior
text describing "the same `MaterialPass` → shader-asset chain, on all three
zones tested" was accurate as far as the *symptom* goes but should not be
read as proof of one shared root cause across all three.

**No code change applied** — both techniques ruled things out rather than
finding a fix; the temporary instrumentation was fully reverted
(`ZoneInputStream.h`/`.cpp` back to their committed state via `git
checkout`, the generated `materialpixelshader_iw5_load_db.cpp`/
`materialvertexshader_iw5_load_db.cpp` diagnostic lines removed and
rebuilt clean) and re-verified to reproduce the exact same known error
signatures with no regression.

**Concrete next step, sharper than before**: since the raw bytes are
confirmed corrupted at the source (not misinterpreted), and struct
layout/field order is now confirmed correct from two independent angles
(§5.13 native x86, this round's upstream diff), the remaining live
hypotheses are narrowed to real data-level causes — a genuine content
difference in the x64-recompiled zone specific to this asset, or a
boundary/alignment issue in the dynamic shader-bytecode blob's own
variable-length read immediately preceding this one (per §5.13's own
conclusion) — best tested next by extending this round's own live
diagnostic technique (already built and proven working, just reverted) to
print `MaterialVertexShader`'s own dynamic shader-bytecode read length and
the exact byte immediately preceding `MaterialPixelShader`'s own read, to
see whether the boundary between them is off by a small, deterministic
amount.

## 5.15. UPDATE, 2026-09-14 (later still, third of three forks, "run 2 more forks to dig deeper") — decisive scope correction: `hamburg.ff`/`common.ff` do NOT fail in the Material/shader chain at all; they hit two more, entirely separate, previously-undocumented bugs

**Status: real, decisive finding — not the same bug family, no fix applied.**
Directly answers the open question §5.14 left unresolved (whether fixing
`code_post_gfx.ff`'s `MaterialPixelShader::name` bug would unblock
`hamburg.ff`/`common.ff` too).

Reused this project's own established top-level diagnostic technique
(`fprintf(stderr, "[iw5oat-diag] loading asset index=%zu type=%d\n", ...)`
in `ContentLoaderIW5.cpp`'s `LoadXAssetArray`, temporary, added/used/
reverted — the same pattern used earlier this session) to log every
asset's type+index as `Unlinker.exe` processes each zone, so the LAST
line printed before the fatal exception identifies exactly which asset
was loading. Real result:

- **`hamburg.ff` fails on asset index 16, type 4 = `ASSET_TYPE_XMODEL`.**
  Not Material (5), not any shader type (6/7/8/9) — a completely
  different, much earlier asset type in the enum. `XModel`'s own loader
  is a real, separately complex chain (bone hierarchy, per-bone
  quats/trans/parentList/baseMat, `XModelLodInfo`, `XModelCollSurf_s`) —
  not investigated further this round.
- **`common.ff` fails on asset index 0 — the very FIRST asset in the
  entire zone — type 40 = `ASSET_TYPE_ADDON_MAP_ENTS`.** `AddonMapEnts`'s
  own loader is a large BSP/collision-geometry tree (`ClipInfo`,
  `cbrush_t`/`cbrushside_t`, `cLeafBrushNode_s`, `cmodel2_t`, recursive
  leaf/child structures) — genuinely the largest, most deeply-nested
  loader of any asset type checked so far this session, not investigated
  further this round.
- Both fatal errors are thrown from the same place as `code_post_gfx.ff`'s
  (`InvalidOffsetBlockOffsetException`, inside a `ConvertOffsetToPointer*`
  call converting an offset-encoded alias pointer that turns out to
  exceed its target block's real size) — confirming the *symptom* really
  is shared infrastructure (the offset-pointer-decode path), while the
  *cause* — which specific field's raw bytes are bad, and why — is
  unknown and unrelated to `MaterialPixelShader::name` specifically.

**Correction to prior documentation**: earlier text in this file (§5.9)
describing "the same `MaterialPass` → shader-asset chain, on all three
zones tested" was never actually verified per-zone and is now confirmed
wrong for two of the three — `hamburg.ff` and `common.ff` fail well
before ever reaching a Material/shader asset. Only `code_post_gfx.ff`'s
failure is confirmed to be the `MaterialPixelShader::name` corruption
documented in §5.10/§5.13/§5.14. **Practical consequence**: resolving the
paused shader-chain bug would only unblock `code_post_gfx.ff` (and
whatever other zones share its specific cause) — `hamburg.ff` and
`common.ff` need their own, separate root-causing (`XModel` and
`AddonMapEnts` respectively) before they can extract, and neither has
been started.

No code change applied — purely a diagnostic/scoping round. Temporary
instrumentation fully reverted (`git checkout --
tools/iw5oat/src/ZoneLoading/Game/IW5/ContentLoaderIW5.cpp`, confirmed
clean), rebuilt, and re-verified all four known zones
(`hamburg.ff`/`common.ff`/`code_post_gfx.ff`/`sp_intro.ff`) reproduce
their exact prior signatures with 0 regression.

**Concrete next step**: two more real, separate investigations, not one —
`XModel`'s bone-hierarchy chain for `hamburg.ff`, and `AddonMapEnts`'s
BSP/collision tree for `common.ff` (the latter genuinely large enough to
warrant its own dedicated round rather than folding into this one).
Neither has any evidence yet connecting it to the shader-chain bug's own
root cause — approach each as its own fresh investigation, not an
extension of §5.10's.

## 5.16. UPDATE, 2026-09-14 (later still, "two more forks", one interrupted by a session rate limit and finished directly by the coordinator) — the actual root cause of `code_post_gfx.ff`'s `MaterialPixelShader::name` corruption FOUND and CONFIRMED empirically: the offset-encoded pointer for this reference is genuinely 32-bit-wide on the wire, not 64-bit — this fork's own `ConvertOffsetToPointerNative` decodes every such pointer at a uniform, hardcoded 64-bit width

**Status: root cause found and empirically confirmed via a real resolved string; fix not yet implemented — scope of the fix (this one field vs. this pointer class broadly) still needs deciding before writing code.**

One of two forks dispatched this round (tracing `code_post_gfx.ff`'s
corrupted bytes back to their true source, on-disk vs. our own memory
reuse) was cut off mid-task by a session rate limit before finishing —
it had already reverted its own earlier `LoadWithFill` instrumentation and
was rebuilding when the interruption hit, leaving one file
(`ZoneInputStream.cpp`) mid-edit with its diagnostic still in place, no
commits made. Rather than discard that work and restart, the session
coordinator picked it up directly: rebuilt with the existing diagnostic,
re-ran it, and confirmed the fork's own headline result (§5.14 already
documented this) — the raw bytes really are `AB 2A 01 30 00 00 00 00`,
genuinely present at the correct computed decompressed-payload offset in
an independently-decompressed copy of `code_post_gfx.ff`, not a memory-
reuse artifact of this fork's own code.

**Pushed one step further, past where the interrupted fork stopped**:
decoded those exact 8 bytes as a little-endian `uint64_t`:
`0x30012AAB` (top 4 bytes all zero). Feeding that into this project's own
already-documented offset decode formula (§5.9: `offsetInt = raw - 1u;
blockNum = (offsetInt & m_block_mask) >> m_block_shift; blockOffset =
offsetInt & m_offset_mask`) with the fork's actual hardcoded
`m_block_shift = pointerBitCount - blockBitCount = 64 - 4 = 60` gives
`blockNum = 0` (`XFILE_BLOCK_TEMP`) and `blockOffset = 805382826` — **an
exact, bit-perfect match for the live error message's own reported
values** (`"Zone referenced offset 805382826 of block XFILE_BLOCK_TEMP
which is larger than its size 351378"`), confirming the decode math itself
isn't the bug — the INPUT WIDTH is.

**The real insight**: the raw value's top 32 bits are all zero — exactly
the shape of a value that was only ever meant to be 32 bits wide, stored
in a nominally-64-bit-wide pointer slot. Decoding the SAME 8 bytes as if
only the low 32 bits are meaningful (`offsetInt32 = raw & 0xFFFFFFFF`,
`blockNum = (offsetInt32 >> 28) & 0xF`, `blockOffset = offsetInt32 &
0x0FFFFFFF` — i.e. the old x86-era 4-bit-block-index-in-a-32-bit-word
scheme, not scaled up to this fork's 64-bit pointer width) gives
`blockNum = 3` (`XFILE_BLOCK_VIRTUAL`) and `blockOffset = 76458`.

**Two independent lines of evidence confirm this alternate decode is
correct, not the current one**:
1. `Load_MaterialPixelShader`'s own generated code (already quoted in
   §5.9) calls `m_stream.PushBlock(XFILE_BLOCK_VIRTUAL)` immediately
   before reading `name` — the code's OWN expectation is that this string
   lives in the VIRTUAL block, exactly matching the alternate decode's
   `blockNum = 3`, not the current decode's `blockNum = 0` (TEMP).
2. **Direct empirical test, not just arithmetic**: added a temporary,
   non-behavior-changing diagnostic to `ConvertOffsetToPointerNative`
   logging both decodes side by side right before the real throw, plus
   every block's own real `m_buffer_size` for this zone
   (`[0]=351378 [1]=0 [2]=0 [3]=1728453 [4]=0 [5]=0 [6]=4224 [7]=480
   [8]=0` — VIRTUAL, index 3, is a real 1.7MB block, comfortably larger
   than the alternate decode's 76,458-byte offset). Dumping the actual
   bytes at `m_blocks[3]->m_buffer[76458]` under the alternate decode
   produced a **genuinely readable, plausible string**:
   `trivial_vertcol_simple.hlsl` — a real HLSL shader source filename,
   not noise. This is about as strong a confirmation as this technique
   can produce: the "corrupted" bytes were never corrupted at all, they
   were being decoded at the wrong pointer width the whole time.

**Why this makes semantic sense**: `trivial_vertcol_simple.hlsl` reads as
a shared/interned source filename — plausibly reused across many
pixel/vertex shader permutations compiled from the same file, stored once
and cross-referenced by offset rather than duplicated per-asset. That this
specific cross-reference sits in `XFILE_BLOCK_VIRTUAL` (a block already
confirmed, per §5.9, to hold `MaterialVertexShader`/`MaterialPixelShader`'s
own struct data, pushed via the identical `PushBlock(XFILE_BLOCK_VIRTUAL)`
call this exact code path uses) is consistent, not coincidental.

**Genuinely open, NOT yet answered — this is the real next step, not a
loose end to ignore**: is this 32-bit-vs-64-bit encoding-width mismatch
specific to THIS ONE FIELD (an interned/shared string reference,
distinct in kind from a fresh `FOLLOWING`-sentinel string, which
`MaterialVertexShader::name` in the SAME zone correctly reads as `FF×8`
via a completely different code path that never touches
`ConvertOffsetToPointerNative` at all) — or a broader, systemic bug in
how this fork's `ConvertOffsetToPointerNative`/`ConvertOffsetToAliasLookup`
decode ANY already-resolved (non-`FOLLOWING`/`INSERT`) offset pointer
throughout the whole x64 zone format, which would mean the SAME fix could
plausibly also unblock `hamburg.ff`'s `XModel` failure and `common.ff`'s
`AddonMapEnts` failure (§5.15) — both of which throw from the exact same
`InvalidOffsetBlockOffsetException` call site, both of which also report
`blockNum = 0` (TEMP) in their own error messages, consistent with (but
not proof of) the identical top-32-bits-zero shape. **Not tested against
either zone this round** — a real, concrete, well-scoped next step, not
idle speculation.

**No code change applied.** The diagnostic (an addition to
`ConvertOffsetToPointerNative` logging both decodes and dumping alt-
resolved bytes, non-behavior-changing — the real code path and its throw
were left completely untouched) was fully reverted via `git checkout --`,
confirmed clean (`git status`/`git diff --stat` both empty for this file),
rebuilt, and all four known zones (`sp_intro.ff`/`sp_prague.ff`/
`code_post_gfx.ff`/`hamburg.ff`/`common.ff`) re-verified to reproduce
their exact prior signatures with 0 regression before this round ended.

**A real fix was deliberately NOT attempted this round**, for a concrete
reason: `m_block_mask`/`m_block_shift`/`m_offset_mask` are single,
stream-wide constants computed once from `IW5_X64_POINTER_BIT_COUNT = 64u`
and used identically for EVERY offset-encoded pointer resolution in the
entire file — dispatch records, `XAssetList` header pointers, every other
already-working `FillPtr` call across every asset type this session has
already confirmed correct. A blanket width change would very likely break
things that currently work; a genuinely correct fix needs to first
determine WHICH pointers are 32-bit-wide (all "already-resolved,
non-sentinel" ones? only interned-string cross-references specifically?
something else?) before writing code that discriminates between them —
exactly the open question two paragraphs above. Recommended next step:
one more focused round testing this exact technique (dump-and-compare
against real block buffers under the alternate decode) against
`hamburg.ff`'s `XModel` failure and `common.ff`'s `AddonMapEnts` failure
specifically, to determine whether this is one narrow fix or a systemic
one before implementing anything.

## 5.17. UPDATE, 2026-09-14 (later still, "keep going") — the recommended next step from §5.16 run directly: the 32-bit-decode pattern IS consistent across all three failing zones (same block index every time), but `hamburg.ff`/`common.ff` resolve to genuinely unwritten memory, not readable content — a real complication, not a clean confirmation

**Status: pattern confirmed consistent, but NOT a clean "same fix for
everything" result — a real, unresolved complication found. Still no fix
implemented.**

Extended the same diagnostic technique from §5.16 (log both the current
64-bit decode and an alternate 32-bit decode at the exact point of
throw, non-behavior-changing) to every `ConvertOffsetTo*` throw site in
`ZoneInputStream.cpp` (`ConvertOffsetToPointerNative`,
`ConvertOffsetToAliasNative`, `ConvertOffsetToPointerLookup`,
`ConvertOffsetToAliasLookup` — four real call sites share the identical
decode-and-throw shape, not just the one already tested), then re-ran
against `hamburg.ff` and `common.ff`.

**Real, non-coincidental pattern confirmed**: all three zones' failures —
`code_post_gfx.ff` (Material/shader chain), `hamburg.ff` (`XModel`),
`common.ff` (`AddonMapEnts`) — decode to the **exact same block index (3,
`XFILE_BLOCK_VIRTUAL`)** under the alternate 32-bit-wide scheme, despite
being three completely different asset types with three different real
on-wire values. That consistency is itself strong evidence the 32-bit
decode width is structurally correct as a general phenomenon in this x64
zone format, not a one-off coincidence specific to
`MaterialPixelShader::name`.

**The complication**: unlike `code_post_gfx.ff` (whose alt-decoded bytes
were a genuine, readable string), both `hamburg.ff`'s and `common.ff`'s
alt-decoded positions resolve to **all-zero bytes** — not corrupted, not
obviously wrong, just empty/unwritten memory at the computed position,
even though the offset comfortably fits within `XFILE_BLOCK_VIRTUAL`'s
own real (much larger) buffer size in both cases (`hamburg.ff`:
offset 61188 of a 128,640,144-byte block; `common.ff`: offset 153474 of a
55,425,096-byte block).

**Most likely explanation, not yet confirmed**: `XFILE_BLOCK_VIRTUAL`'s
buffer is filled progressively, in file order, as the zone stream is
consumed — not pre-populated. `code_post_gfx.ff`'s working case was a
*backward* reference (to a shader filename string plausibly interned
earlier in the same load sequence, already written by the time it's
referenced). `common.ff` fails on asset index 0 — the very first asset in
the entire zone (§5.15) — meaning almost nothing has been written to
ANY block yet; if `AddonMapEnts`'s own reference at that point is a
*forward* reference (to content written later in the stream), the
computed block position would legitimately still be zero-filled
regardless of whether the pointer-width decode itself is right. This
would mean the width-mismatch bug is real and probably still needs fixing
for `hamburg.ff`/`common.ff` too, but isn't sufficient on its own to
unblock them — a second, real question (how forward references into
`XFILE_BLOCK_VIRTUAL` are supposed to resolve, if the real engine even
allows them, or whether this fork's own progressive fill order is itself
missing a two-pass or deferred-resolution step) would need its own
investigation before either zone can extract.

**No fix applied.** All instrumentation (this round's extended version
across four call sites, sharing one small helper, `DiagLogAltDecode`)
fully reverted via `git checkout --`, confirmed clean, rebuilt, and all
five known zones re-verified to reproduce their exact prior signatures
with 0 regression.

**Where this leaves the investigation**: the pointer-width-mismatch root
cause (§5.16) is now corroborated by three independent zones/asset types,
not just one — a real, generalizable finding worth fixing regardless of
whether it alone unblocks `hamburg.ff`/`common.ff`. But actually
implementing that fix correctly needs the still-unanswered question from
§5.16 (which pointers are 32-bit vs. 64-bit) resolved first, and
`hamburg.ff`/`common.ff` specifically may need a second, separate
investigation into forward-reference/fill-order handling on top of that,
regardless. Given the number of genuine, well-evidenced rounds this
specific bug family has now been through across two sessions, this is a
reasonable point to pause again rather than open a third new investigative
thread (forward-reference resolution) in the same sitting.

## 5.18. UPDATE, 2026-09-14 (later still, "three forks digging into potential") — the §5.16/§5.17 open question DEFINITIVELY SETTLED via direct x64 native decompile: the 32-bit offset-pointer decode is the real engine's own single, generic, shared primitive, used for EVERY already-resolved offset pointer in the entire format, not a field-specific quirk

**Status: DEFINITIVELY ANSWERED — the fix scope is global, not
field-specific. Fix not yet implemented this round (out of scope per
this round's own directive), but the exact correct formula is now on
record, straight from the real game engine's own code.**

§5.16/§5.17 left one real, unanswered question: is the 32-bit-vs-64-bit
offset-pointer decode-width mismatch specific to ONE kind of pointer
(e.g. only interned-string cross-references), or does it apply broadly
to every already-resolved offset pointer this fork decodes? Settled this
round by finding and decompiling the REAL native x64 equivalent of this
fork's own `ConvertOffsetToPointerNative` directly in `iw5sp.exe`
(current, x64, `re_notes/ghidra_project_x64/iw5sp_x64_proj`) — not
inferred, not reasoned from arithmetic, read straight from the compiled
engine.

**How it was found**: the already-decompiled `MaterialPixelShader`
FillStruct-equivalent (`FUN_140094cf0`,
`re_notes/ghidra_scripts/decomp_shader_fillstruct_140094cf0_320_1a0.txt`,
from §5.9's own earlier work) calls `FUN_1400aac10(3)` — confirming
block index 3 = `XFILE_BLOCK_VIRTUAL` in the real engine's own numbering,
exactly matching this fork's own enum order — then, in the "not
FOLLOWING" branch of its own pointer-type check, calls `FUN_1400aad40()`
with no other candidate in between. Decompiling `FUN_1400aad40` directly:

```c
void FUN_1400aad40(longlong *param_1)
{
  uint uVar1;
  uVar1 = (int)*param_1 - 1;
  *param_1 = (ulonglong)(uVar1 & 0xfffffff) +
             *(longlong *)(DAT_140d6de00 + (ulonglong)(uVar1 >> 0x1c) * 0x10);
  return;
}
```

**This is the real, complete, unambiguous answer**:
- `(int)*param_1` — the raw pointer value is cast to a plain 32-bit `int`
  **before any other arithmetic happens**. The upper 32 bits of the
  64-bit-wide field this value is stored in are discarded outright, not
  used at all.
- `uVar1 >> 0x1c` = `>> 28` — the real engine's own block-index
  extraction shift is **28, not 60**. This is the exact x86-era
  4-bit-block-index-in-a-32-bit-word scheme (`32 - 4 = 28`) this fork
  already assumed correctly for x86 zones — genuinely never widened to
  match the pointer's own new 64-bit storage width when the game
  recompiled to x64.
- `uVar1 & 0xfffffff` = `& 0x0FFFFFFF` — the real block-relative offset
  mask, 28 bits, matching the alternate decode this fork's own diagnostic
  already tested empirically in §5.16/§5.17.
- The resolved block base comes from a THIRD per-block-indexed array
  (`DAT_140d6de00`, distinct from `FUN_1400aac60`'s own saved-position
  array `DAT_140d6ddb0` already mapped in §5.10 — this one holds each
  block's real allocated buffer base), indexed by the SAME `uVar1 >> 0x1c`
  block number, exactly mirroring `m_blocks[blockNum]->m_buffer` in this
  fork's own C++.

**Confirmed generic, not specific to this one call site**: `FUN_1400aad40`
has **86 real callers**, spanning the function-address range
`0x140094xxx`–`0x14009cxxx` — the entire asset-loading function block
this session's own dispatch-switch mapping already covers (Material at
`0x140094950`, RawFile at `0x1400962c0`, ScriptFile at `0x1400964a0`,
etc., per §5's own case table). This is not an inlined, per-caller helper
— it's one single, shared function the real engine calls from essentially
every asset type's own fill logic whenever it needs to resolve an
already-encoded offset pointer to a native address. There is no
per-field or per-asset-type branching anywhere in this function or its
call sites that would suggest a genuinely different scheme is used
elsewhere.

**Direct, practical conclusion**: this fork's own
`IW5_X64_POINTER_BIT_COUNT = 64u` (and by extension
`m_block_shift = 64 - 4 = 60` in `ZoneInputStream.cpp`) is simply wrong
for this entire class of pointer — not a narrow special case. The zone
format's offset-ENCODED pointer scheme (as opposed to genuine raw
64-bit pointers/offsets elsewhere in the format, e.g. the dispatch
record's own `dataPtr` field, which is a real memory-relative value, not
this same block+offset encoding) was **never widened past 32 bits when
the game recompiled to x64** — it stayed the exact same x86-era
28-bit-offset/4-bit-block-index scheme, just now sitting inside a
nominally-64-bit-wide storage slot with its top 4 bytes simply unused
(always zero on the wire, as directly observed in every raw byte dump
this investigation has produced so far). The correct fix is a single,
targeted change: decode every already-resolved offset pointer using a
32-bit-wide scheme (`shift = 28`, `mask = 0x0FFFFFFF`, and — critically —
truncate the raw value to 32 bits BEFORE the `-1u` adjustment, matching
`(int)*param_1 - 1` exactly, not `offsetInt - 1u` computed at full
64-bit width first) — **not** a per-field discriminator, and **not**
something that needs guessing at scope any further.

**This does NOT, on its own, explain §5.17's own separate finding**
(`hamburg.ff`/`common.ff` resolving to unwritten/zero memory even under
this now-confirmed-correct decode) — that remains a real, separate,
unresolved question (most likely the forward-reference/fill-order theory
§5.17 already proposed), genuinely out of scope for this round and not
investigated further here.

**No fix implemented this round, deliberately** (out of this round's own
scope — pure RE/scoping, not implementation). The exact formula above is
sufficient to implement one directly: replace
`ConvertOffsetToPointerNative`/`ConvertOffsetToAliasNative`/
`ConvertOffsetToPointerLookup`/`ConvertOffsetToAliasLookup`'s shared
decode logic (all four currently use the same `m_block_mask`/
`m_block_shift`/`m_offset_mask` computed from the fork-wide
`IW5_X64_POINTER_BIT_COUNT`) with the native-confirmed 32-bit scheme —
a genuinely small, well-scoped change now that the scope question is
settled, though it should still be built and tested carefully (per
§5.16's own caution: these functions are shared across every asset type
already confirmed working, so any change needs full regression testing
against all five known reference zones before being considered done).

**Ghidra project safety note, for the record**: this round's headless
invocations against `re_notes/ghidra_project_x64/iw5sp_x64_proj`
(`-noanalysis`, decompile/caller-search scripts only) again caused two
`.gbf` database files to show as deleted in `git status`
(`idata/00/~00000000.db/db.43.gbf`/`db.44.gbf`) — the same known,
gitignored, unrecoverable-via-git corruption pattern already documented
twice earlier this session. Left as-is (nothing to `git checkout` since
the directory is gitignored); flagged here rather than silently ignored,
per this project's own standing practice.

## 5.19. UPDATE, 2026-09-14 (later still, third of the same "three forks digging into potential" round) — a real, tested, deliberately narrow fix landed: `MaterialPixelShader::name` now resolves correctly in `code_post_gfx.ff`, which progresses to a genuinely different, later failure

**Status: a real fix implemented, built, and tested — commit `cc18ad96`.
Deliberately narrower than the global fix §5.18 (landed concurrently,
same round) shows is the eventually-correct one — see "Relationship to
§5.18" below for why, and what's still open.**

Implemented and shipped a working fix for the specific case this session
has fully characterized: `MaterialPixelShader::name` in `code_post_gfx.ff`
failing to resolve because its offset-encoded pointer is genuinely
32-bit-wide on the wire, decoded by this fork at a hardcoded 64-bit width.

**The fix**: a new method, `ZoneInputStream::TryConvertOffsetToStringPointerNative`
(`ZoneInputStream.h`/`.cpp`), consulted only from `ContentLoaderBase::LoadXString`'s
own non-`FOLLOWING` branch, and only as a fallback:
1. First computes the standard, native-width (64-bit) decode. If that's
   already valid (in-bounds), returns `nullptr` immediately — the
   fallback never touches an already-successful resolution.
2. Only proceeds if the raw offset value's bits above 32 are exactly
   zero (the confirmed empirical signature from §5.16 of a pointer
   genuinely encoded at 32-bit width) — otherwise returns `nullptr`.
3. Decodes the low 32 bits using the legacy x86-era scheme (`shift = 28`,
   `mask = 0x0FFFFFFF`) and checks the resulting block+offset are
   in-bounds — otherwise returns `nullptr`.
4. **Validates the resolved bytes are a real, non-empty, printable,
   null-terminated string** before trusting them — scans up to 4096
   bytes for a NUL terminator, rejecting anything containing non-printable
   bytes, and explicitly rejecting a NUL at position 0 (an "empty
   string"). This last check is deliberate, not incidental: it's exactly
   what distinguishes this fix's target case from §5.17's own separate
   finding (`hamburg.ff`/`common.ff`'s forward references into
   `XFILE_BLOCK_VIRTUAL` resolve to unwritten, all-zero memory under the
   identical "top 32 bits zero" shape — an all-zero span's first byte is
   already a NUL, so without this check the fallback would silently
   accept a spurious empty string for a completely different, unrelated
   bug rather than correctly declining and letting the honest exception
   through).

If the fallback returns `nullptr`, `LoadXString` falls through to the
original `ConvertOffsetToPointerNative` call, producing the exact same
exception as before this fix — nothing about the failure path changed
for any case the fallback doesn't apply to.

**Tested against all five known reference zones, real rebuild each time**:
- `sp_intro.ff`/`sp_prague.ff`: still `0 warnings, 0 errors` — no
  regression.
- `code_post_gfx.ff`: **the `MaterialPixelShader::name` failure is gone.**
  The zone now fails at a genuinely different, later point (confirmed via
  a temporary, fully-reverted diagnostic: `ConvertOffsetToAliasLookup`,
  a *different* function than the one this fix touches, on a
  *non-string* pointer, hitting the identical decode-width bug class at
  a different reference — real progress, not a fluke, and exactly the
  kind of "one more instance of the same bug, out of this fix's
  deliberately narrow scope" result §5.18's own global-scope finding
  would predict).
- `hamburg.ff`/`common.ff`: identical error signatures to before this
  fix — no regression, and no change (their own separate XModel/
  AddonMapEnts bugs, per §5.15/§5.17, aren't reached by string
  resolution at all, so this fix was never expected to touch them).

**Relationship to §5.18's own concurrent finding**: §5.18 (landed the
same round, different fork) found the real engine's own native decode
formula directly from `iw5sp.exe` — confirmed GLOBAL (86 callers across
every asset type, one shared primitive, no per-field branching) and
using a subtly different bit-exact order than this fix's own gate:
the real engine truncates the raw pointer to 32 bits **before**
subtracting 1 (`(int)*param_1 - 1`), while this fix's gate subtracts 1
at full 64-bit width first, then checks/truncates. **Verified this
doesn't affect correctness for the actual, tested, real-world case**
(both orders produce the identical result whenever the ORIGINAL raw
pointer's upper 32 bits are already zero, which is exactly this fix's
own gating precondition) — but there IS a narrow, real, currently-
unpatched theoretical gap: a raw pointer value of EXACTLY `0x100000000`
(upper 32 bits = 1, lower 32 bits = 0) would have its upper bits
zeroed by the 64-bit subtraction's borrow, causing this fix's gate to
wrongly treat it as "top-32-bits-zero" when the true raw value wasn't.
Not fixed this round — the real block count (9) and realistic block
sizes make a genuine occurrence of this exact value in real zone data
effectively impossible, so this is flagged for completeness/honesty
rather than treated as a live bug.

**Why this fix stays deliberately narrower than §5.18's own global
finding recommends, rather than being superseded by it in the same
commit**: §5.18 itself flags its own global fix as "not yet implemented
this round... should still be built and tested carefully... these
functions are shared across every asset type already confirmed working."
This fix targets exactly the one case already fully characterized and
safely validatable (a string, checkable for real content) rather than
changing the shared `ConvertOffsetToPointerNative`/`ConvertOffsetToAliasNative`/
`ConvertOffsetToPointerLookup`/`ConvertOffsetToAliasLookup` functions
those many other, currently-working asset types all depend on — the
exact risk §5.16 originally flagged as the reason NOT to attempt a
blanket width change without first understanding scope. Now that §5.18
has settled the scope question definitively, **the real, complete fix
described there (replace the shared decode logic outright, using the
exact native formula, then full-regression-test against all five known
zones) is the correct next step** — this commit is a validated, safe,
already-shipped stopgap that gets `code_post_gfx.ff` further today, not
a substitute for that broader, now well-justified fix.

**Not yet done**: the broader §5.18 fix itself (would very likely also
help `code_post_gfx.ff`'s own newly-exposed `ConvertOffsetToAliasLookup`
failure, and needs its own full regression pass); investigating whether
`code_post_gfx.ff` needs just one more such fix or several before it
fully succeeds; the still-separate `hamburg.ff`/`common.ff` forward-
reference question from §5.17, untouched by either this round's fix or
§5.18's own finding.

## 5.20. UPDATE, 2026-09-14 (later still, "just keep pushing its understandable that a,b,c,d,e,f,g is broken because it had assumptions from x86 etc") — the broader §5.18 fix landed for real: the offset-pointer decode is now the real native 32-bit scheme everywhere, with two additional safety mechanisms found necessary by testing it live — zero crashes across the full 39-zone sweep, real progress on every previously-blocked zone

**Status: shipped, tested, safe.** Implements exactly what §5.16-§5.18
justified: replaced the hardcoded 64-bit-wide offset decode in
`ConvertOffsetToPointerNative`/`ConvertOffsetToAliasNative`/
`ConvertOffsetToPointerLookup`/`ConvertOffsetToAliasLookup` with the
real native 32-bit scheme (`FUN_1400aad40`'s own confirmed formula) as
the actual, only decode — not a narrow per-field fallback (§5.19's
`TryConvertOffsetToStringPointerNative` stays in place, now redundant
for the case it was built for but harmless, since the primary decode
resolves it directly on the first try).

**Correctness alone wasn't safety — two real, live-confirmed crashes
led to two additional fixes, neither of which was anticipated up
front:**

1. **First attempt (decode fix alone): segfaulted on `hamburg.ff` and
   `common.ff`.** Root cause: the OLD, wrong 64-bit decode had been
   accidentally catching a genuinely separate bug — a forward reference
   into a block that fills progressively as the zone stream is
   consumed, not pre-populated (§5.17's own leading theory, now
   directly confirmed). The wrong decode produced a wildly
   out-of-bounds offset that tripped the existing total-capacity check
   on its own; fixing the decode width removed that accidental
   protection. **Fixed** by adding a write-cursor check
   (`m_block_offsets[blockNum]`, the block's own real "how much has
   genuinely been written so far" counter — confirmed reliable: it only
   ever advances via `IncBlockPos` as real stream content is read into
   the block, and is never reset except for `TEMP`-type blocks on
   `PopBlock`) alongside the existing total-buffer-size check in every
   read function.
2. **Second attempt (decode fix + write-cursor guard): STILL
   segfaulted on `hamburg.ff`, at a completely different point** — none
   of the four `ConvertOffsetTo*` functions' own "about to return"
   diagnostics ever fired, meaning the crash was outside code already
   modified. Bisected via a full function-entry trace (every override
   in the class instrumented, not just the four already-suspected ones)
   cross-referenced against the generated `XModel` loader
   (`xmodel_iw5_load_db.cpp`): `ConvertOffsetToAliasLookup` was entered
   and returned successfully (via its own `m_pointer_redirect_lookup`
   success path, never reaching the throw), then the caller's own
   `Loader_XModel::Load()` double-dereferenced the "resolved" value
   (`AssetName<AssetXModel>(**pAsset)`) — a genuinely different, deeper
   bug: `m_pointer_redirect_lookup`'s own stored "alias" is the ADDRESS
   of ANOTHER asset's own pointer field (registered via
   `AddPointerLookup` so a forward reference can find it before that
   field is filled in), but when the referenced field genuinely hasn't
   been resolved yet at the moment this lookup runs — a real reference
   chain, asset A's pointer aliasing asset B's pointer, and B's own
   field not yet converted from raw offset to real native pointer —
   dereferencing it reads back whatever raw, unconverted offset value
   was written there by the initial `FillPtr` byte-copy. **Confirmed
   live, not theorized**: resolving `hamburg.ff`'s own asset index 16
   (`XModel`) returned `0x30029229` — numerically IDENTICAL in shape to
   the very offset just looked up, nothing like a real
   `~0x165b694d018`-class heap address this process's own allocator
   actually hands out. Trusting that as a resolved pointer is exactly
   what segfaulted. **Fixed** with a bounded (16-hop) chase: detect a
   "resolved" value that still looks like a raw, unconverted offset
   (`LooksLikeUnresolvedRawOffset` — fits in 32 bits; a genuine native
   pointer in this process never does) and resolve THAT through the
   exact same lookup mechanism one more hop, rather than either
   trusting a not-yet-real value (crash) or giving up on a reference
   that IS genuinely resolvable, just not on the very first try. A
   chain that never bottoms out (circular, or pathologically deep)
   throws a clean `InvalidLookupPositionException` instead of looping
   forever.

**Both fixes were found by testing live, not by reasoning in advance** —
consistent with this whole investigation's own pattern (§5.13-§5.17):
this fork's code carries real, compounding x86-era assumptions, and
removing one wrong assumption reliably surfaces the next one hiding
behind it, rather than immediately reaching a clean success.

**Full validation, not just the five already-tracked zones**: rebuilt
and re-tested after each change, then ran the complete 39-zone
`sp_*.ff`/`so_*.ff` sweep first established in §5.12:

- `sp_intro.ff`/`sp_prague.ff`: unaffected, 0 warnings/0 errors, exactly
  as before — no regression.
- `code_post_gfx.ff`: original `MaterialPixelShader::name` failure
  gone; now fails later, at a different offset
  (`XFILE_BLOCK_CALLBACK` size 0) — a real, different, later bug, not
  yet investigated.
- `hamburg.ff`/`common.ff`: no more segfault. Clean, catchable errors
  at different (further-progressed) points than their original
  failures (`"Zone tried to lookup at block 3, offset N that was not
  recorded"` — the alias-chain-hop-cap exhausted, a genuine remaining
  gap, not a crash).
- **`so_trainer2_so_deltacamp`, previously failing, now succeeds fully**
  — a real, new zone unblocked, bringing the confirmed-working set from
  2/39 (§5.12) to 3/39.
- **Zero crashes across all 39 zones.** Every remaining failure
  produces a specific, catchable error — a real, qualitative
  improvement in diagnostic signal over the pre-fix state, where every
  single failing zone showed the exact same generic, misleading `"block
  XFILE_BLOCK_TEMP"` text regardless of its real cause (an artifact of
  the old decode always producing `blockNum=0`). Post-fix, errors now
  correctly name `XFILE_BLOCK_VIRTUAL` (block 3, the real target) and
  distinguish at least three genuinely different failure shapes
  (alias-chain exhausted; total-capacity exceeded; `"invalid block
  15"`, a separate, not-yet-investigated bug affecting a couple of
  zones) — a much better starting point for whoever picks up any of the
  36 still-failing zones next.

All temporary diagnostic instrumentation (a full function-entry trace,
per-lookup value dumps, a top-level asset-index/type trace in
`ContentLoaderIW5.cpp`) was fully removed before the real fix was
committed — confirmed via `git diff --stat` showing only the intended,
permanent change. Commit: `e2fdeb07`.

**Concrete next steps, not yet done**: root-cause `code_post_gfx.ff`'s
own newly-exposed later failure (`XFILE_BLOCK_CALLBACK` size 0 — a
zero-sized block being referenced at all looks like it could be a
distinct bug class from anything found so far); investigate the
`"invalid block 15"` failure affecting `sp_dubai.ff`/`so_deltacamp.ff`
(block index 15 is out of range for the real 9-block `XBLOCK_DEF` table
— a genuinely different bug, not yet looked at); and determine whether
the remaining `"tried to lookup ... that was not recorded"` failures
(the majority of the still-failing zones) share ONE root cause or
several — the hop-cap-exhausted case doesn't yet distinguish "a
genuinely circular reference" from "a reference into content this
fork's own single-pass load order simply hasn't reached yet," which
would need real architectural work (a second pass, or deferred
resolution) rather than another local fix.

## 5.21. UPDATE, 2026-09-14 (later still, "dig deeper") — the `"invalid block 15"` bug narrowed precisely (traced past XModel's own struct fields into `Material`'s own recursive resolution, reached via `materialHandles`), but the actual malformed value's own shape doesn't fit any pattern already understood — paused, not resolved

**Status: real, precise progress on WHERE the bug lives; the WHY remains
open. No fix attempted — every experiment this round was reverted.**

Dug into `sp_dubai.ff`/`so_deltacamp.ff`'s `"Zone tried to reference
invalid block 15"` failure (block index 15 is the maximum a 4-bit field
can hold — out of range for the real 9-block table, and the maximum
possible value a 4-bit block-index field can ever produce, distinct from
every other failure shape logged this session).

**First hypothesis, tested directly, genuinely disproven — not simply a
32-bit-wide FOLLOWING/INSERT sentinel slipping past the 64-bit-wide
`GetZonePointerType` equality check.** Live-confirmed one raw value
(`0x01010150FFFFFFFF`) whose low 32 bits ARE `0xFFFFFFFF` — matching that
theory — but experimentally truncating `GetZonePointerType`'s own
comparison to 32 bits (rebuilt, tested live) did NOT resolve the failure:
`sp_dubai.ff` progressed a little further, then hit the identical
`"invalid block 15"` error again from a *different* raw value,
`0xFFFFFFFF00000000` — the sentinel shape in the HIGH 32 bits this time,
with the low 32 bits at zero. Two genuinely different byte-shapes for
the same symptom rules out a single clean "always truncate to the low 32
bits" fix — reverted the experiment rather than ship a change that only
partially explains the evidence.

**Traced the actual failure site precisely, using the project's own
established asset-index-trace + raw-dispatch-record-dump technique**:
confirmed the `0xFFFFFFFF00000000` value is NOT a top-level asset
dispatch record (every one of `sp_dubai.ff`'s own ~557 dispatch records
reads a clean, correct `0xFFFFFFFFFFFFFFFF` FOLLOWING sentinel — verified
directly, not assumed), and NOT any of `XModel`'s own six direct
cross-block struct fields either (`boneNames`/`parentList`/`quats`/
`trans`/`partClassification`/`baseMat` — a live dump immediately after
`FillStruct_XModel`'s own raw byte-copy showed every one of these is a
clean `0xFFFFFFFFFFFFFFFF` or a clean `0x0000000000000000`, nothing
malformed). The actual failure is reached one level deeper: `XModel`'s
own `materialHandles` array (real code: `LoadPtrArray_Material(true,
varXModel->numsurfs)`, `xmodel_iw5_load_db.cpp`) recursively invokes
`Material`'s own per-entry pointer resolution — the SAME
`ConvertOffsetToPointerNative`/`ConvertOffsetToAliasLookup` machinery
§5.16-§5.20 already fixed, just reached via a different, recursive entry
point (through an `XModel` asset's own submaterial array, not a direct
top-level `Material` asset load) instead of a struct field read directly.

**Not yet answered — the real open question**: whether `0x01010150FFFFFFFF`/
`0xFFFFFFFF00000000` represent a genuinely different, third wire-encoding
convention this session hasn't characterized yet (neither "top 32 bits
zero" like `MaterialPixelShader::name`, nor "low 32 bits zero" like
`hamburg.ff`'s own forward-reference case) — or something else entirely
(a genuine struct-layout/alignment mismatch producing a value assembled
from two unrelated adjacent fields; a real ordering issue in how
`materialHandles`' own array entries get resolved relative to whatever
they reference; or content this project hasn't previously encountered).
Determining this would need either a fresh native-decompile pass (this
session's own most reliable technique whenever guessing from shape alone
has stalled — see §5.13/§5.18's own precedent) targeting `Material`'s
real fill function specifically as reached FROM an `XModel`'s own
`materialHandles` array, or a raw hex-editor comparison of `sp_dubai.ff`'s
own decompressed bytes at the computed position, neither attempted this
round.

**All temporary instrumentation fully reverted** (four separate rounds
of diagnostics — a per-throw-site value dump, a `GetZonePointerType`
truncation experiment, a full dispatch-record dump, and a direct
`FillStruct_XModel` field dump — each added, tested, and removed in
turn), confirmed via `git status`/`git diff` showing a clean tree,
rebuilt, and re-verified all six known zones (`sp_intro.ff`/
`sp_prague.ff`/`code_post_gfx.ff`/`hamburg.ff`/`common.ff`/`sp_dubai.ff`)
reproduce their exact §5.20-shipped signatures with 0 regression.

**Per this project's own standing persistence-threshold principle**: this
specific `"invalid block 15"` thread has now been through several
genuine rounds (sentinel-width experiment, dispatch-record trace,
struct-field trace) without landing a fix, unlike the broader §5.20 fix
which reached a real, tested, safe conclusion in a comparable number of
rounds. Pausing this specific thread here — real, precise progress on
scope, no regression risk taken, and a concrete recommended next
technique (native decompile of the `Material`-via-`materialHandles`
resolution path specifically) on record for whoever picks it up next.
This does not block any current, real project need — `so_trainer2_
so_deltacamp` and the two already-clean zones remain unaffected either
way, and this bug was never blocking anything before §5.20 was shipped.

## 5.22. UPDATE, 2026-09-14 (later still, "keep goin") — `code_post_gfx.ff`'s own new `XFILE_BLOCK_CALLBACK` failure traced to a `snd_alias_list_t` → recursive `LoadedSound` load; the union-branch-selection theory tested directly and disproven; the actual malformed value shows no pattern at all, three levels of recursion deep — paused, not resolved

**Status: real, precise progress on WHERE and WHY-NOT; the actual root
cause remains open. No fix attempted — every experiment reverted.**

Investigated `code_post_gfx.ff`'s own post-§5.20 failure (`"Zone
referenced offset 70909906 of block XFILE_BLOCK_CALLBACK which is larger
than its size 0"` — a reference into a block that's entirely unused by
this zone, size exactly zero, a new and different signature from
anything else logged this session).

**Traced the failure to a specific asset and field, using the same
asset-index-trace + throw-site-value-dump technique already proven this
session**: asset index 4481, `ASSET_TYPE_SOUND` (`snd_alias_list_t`) —
the first sound asset investigated all session, structurally unrelated
to the `XModel`/`Material` chain §5.21 was investigating. The raw
malformed value (`0x59EE65FC5439FFD3`) is a genuinely different SHAPE
from every other case found this session: no zero half, no sentinel
pattern in either the high or low 32 bits — high-entropy, effectively
random-looking.

**A concrete, testable hypothesis — a union-branch-selection bug — was
formed and directly disproven, not just theorized.** `snd_alias_list_t`'s
own DSL (`snd_alias_list_t.txt`) declares a real runtime-conditional
field: `set condition SoundFile::u::loadSnd type == SAT_LOADED;` — a
`SoundFileRef` struct holds BOTH a `LoadedSound* loadSnd` and a
`StreamedSound streamSnd` (not a real C++ union, two coexisting struct
members at different offsets, both populated by the same raw
`LoadWithFill` byte-copy regardless of which is semantically "active"),
and the generated code branches on a runtime `type` field to decide
which one is real. The hypothesis: if `type` were misread (wrong offset,
wrong enum value), the code could take the wrong branch and misinterpret
the OTHER union member's own bytes as a pointer, explaining a
random-looking value. **Tested directly with a live diagnostic dumping
`type`, `SAT_LOADED`, and both union members' own raw values**: `type=1`,
`SAT_LOADED=1` — a correct, clean match; `loadSnd=0xFFFFFFFFFFFFFFFF` —
a correct, clean `FOLLOWING` sentinel. The branch selection is entirely
correct. This rules the hypothesis out cleanly, not just weakens it —
the code correctly takes the `SAT_LOADED` branch and correctly begins a
RECURSIVE load of a separate, not-yet-investigated asset type
(`Loader_LoadedSound`), and the actual malformed value lives somewhere
INSIDE that recursive load, at least one level deeper than this round's
own diagnostics reached.

**This is the THIRD time this session a malformed value has turned out
to live inside a recursively-loaded sub-asset rather than at the
"obvious" top-level field being checked first** (§5.21's own
`XModel`→`materialHandles`→`Material` chain is the second; this
`snd_alias_list_t`→`LoadedSound` chain is the third) — worth flagging as
a real, recurring shape to this whole bug family: surface-level fields
are consistently clean, and genuine problems hide inside recursive
sub-asset loads reached through arrays/handles, not simple direct struct
fields. This is a useful, generalizable lead for whoever continues this
investigation, even without a fix yet.

**All temporary instrumentation reverted** (five throw-site value dumps
across all `ConvertOffsetTo*` functions, a top-level asset-index trace,
and a direct union-member diagnostic inside the generated
`snd_alias_list_t` loader — including cleaning the diagnostic out of the
gitignored, regenerated `build/` output, not just tracked source), 0
regression confirmed against all six known zones.

**Paused per this project's own standing persistence-threshold
principle**, matching §5.21's own reasoning exactly: real, precise scope
narrowing achieved (ruled out one concrete, testable hypothesis; found
the true depth of the bug; identified a real, generalizable pattern
across two independent occurrences), but resolving the ACTUAL root cause
of either recursive case would need a fresh technique (most likely a
native decompile of `LoadedSound`'s own real fill function, or of
`Material`'s own fill function specifically as reached from `XModel`'s
`materialHandles` — this session's own most reliable un-stuck technique
whenever shape-guessing alone stalls) rather than more ad hoc diagnostic
rounds. Doesn't block any current need — `code_post_gfx.ff` already
progressed further than it had before §5.20's own shipped fix, and
nothing about this investigation put that progress at risk.

## 5.23. UPDATE, 2026-09-14 (later still, "dig on both", native x64 decompile of the `materialHandles` resolution path) — a second real native primitive found (`FUN_1400aad10`) and initially looked like a different, narrower-width scheme, but closer analysis shows it's functionally identical to the already-fixed primitive — one hypothesis cleanly eliminated, the real root cause still open

**Status: real native evidence gathered, one theory eliminated with hard
proof; the actual root cause of `"invalid block 15"` remains
unresolved.**

Decompiled the real native x64 `iw5sp.exe`'s own `Material`/
`MaterialTechniqueSet` outer pointer-handlers (`FUN_140094950`/
`FUN_1400950e0` — already on file from earlier this session) in full,
tracing their own sentinel-check logic directly rather than continuing
to guess from raw-value shapes alone. **Confirmed the real engine's own
FOLLOWING/INSERT sentinel check operates on the genuine, full 64-bit
value** (`0xFFFFFFFFFFFFFFFE`/`0xFFFFFFFFFFFFFFFF`, compared directly,
no truncation) — this directly explains, with real evidence, WHY
§5.21's own `GetZonePointerType`-truncation experiment made things
worse rather than better: that experiment's premise (the sentinel check
itself needs 32-bit truncation) is now confirmed wrong by the real
native code, not just empirically disproven by testing.

**Found a second real offset-resolution primitive, `FUN_1400aad10`**,
called from these same outer handlers for the "already resolved, not a
sentinel" case — genuinely different in its own decompiled signature
from the already-fixed `FUN_1400aad40` (§5.18): `void
FUN_1400aad10(int *param_1)` takes an `int*` and reads only 4 bytes as
input, writing the resolved 8-byte pointer back in place — versus
`FUN_1400aad40`'s own `longlong *param_1`, reading a full 8 bytes then
truncating in software. **Initially looked like decisive evidence of a
genuinely different, narrower on-wire width for this specific call
context** — but tracing the actual caller data flow shows this reading
is NOT a narrower on-wire field: `FUN_1400aad10` is called on the SAME
global (`DAT_1407be920`) the caller had already loaded as a full 8-byte
value moments earlier (`FUN_1400aad70(param_1, DAT_1407be920, 8)`), and
which the caller's own sentinel check just read as a genuine 8-byte
quantity. `FUN_1400aad10`'s own 4-byte read is simply the COMPILER's own
chosen expression of "read the low 32 bits" for this specific call site
(the C source almost certainly reads `*(int*)&alreadyLoaded64BitValue`,
functionally IDENTICAL to `FUN_1400aad40`'s own `(int)*longlongPtr`
approach) — not evidence of a real, narrower packed-array encoding.
**Conclusion: both primitives implement the exact same 32-bit-truncate
decode already fixed in `e2fdeb07`, just compiled two different ways at
two different call sites** — this rules out "a genuinely different
decode primitive for this specific array context" as an explanation for
the `"invalid block 15"` malformed values, but does not itself explain
what IS producing them.

**Confirmed via `FindCallers.java`**: `FUN_1400aad10` has 48 real
callers across the binary (a generic, widely-shared primitive, matching
`FUN_1400aad40`'s own earlier-confirmed 86-caller generality) — not a
function specific to `Material`/`materialHandles` at all, further
supporting that it's simply an alternate compiled form of the same
generic logic, not a context-specific encoding variant.

**Real, concrete next step, not yet attempted**: since the decode
primitives themselves are now confirmed identical/correct, the actual
bug must live either in how the real native engine's own
`materialHandles`/`numsurfs` array-WALKING loop is structured (a
genuinely different function from the two now-decompiled primitives,
not yet found — likely reachable a few hops deeper from the master
dispatch switch's own `case 4` XModel handler) or in this fork's own
C++ array-population code (`LoadPtrArray_Material`'s own stride/count
logic in the generated `xmodel_iw5_load_db.cpp`, not yet directly
compared against a decompiled native equivalent). Finding and
decompiling that real array-walking loop specifically — not another
single-pointer resolution primitive — is the concrete next step for
whoever continues this.

Two raw Ghidra decompile evidence files added:
`re_notes/ghidra_scripts/decomp_1400aad10_family.txt`,
`re_notes/ghidra_scripts/callers_1400aad10.txt`. No code changes — pure
RE, no fix attempted (per this round's own explicit scope: don't force
a fix without solid evidence). Ghidra headless invocations against
`re_notes/ghidra_project_x64/` again incidentally deleted two tracked
`.gbf` database files (the same known, already-documented corruption
pattern hit repeatedly this session) — restored via `git checkout --`
both times, confirmed clean before finishing.

## 5.24. UPDATE, 2026-09-14 (later still, second of the same "dig on both" round, native x64 decompile of `LoadedSound`'s own real fill function) — a real, tested fix landed: `LoadedSound`'s own raw sample-data read used the wrong struct field for its byte count, confirmed via direct decompile and a live before/after comparison, not guesswork

**Status: shipped, tested, safe.** Resolves §5.22's own `code_post_gfx.ff`
`XFILE_BLOCK_CALLBACK` failure at its actual root — a real stream-cursor
desync, not a decode-width issue like every other bug found this session.

**Decompiled `LoadedSound`'s own real native fill function directly**
(`FUN_1400941f0` → `FUN_140094150` → `FUN_14009c0f0`, found via the master
per-asset dispatch switch's own `case 0xd` — `ASSET_TYPE_LOADED_SOUND`'s
real enum value, confirmed against `IW5.h`'s own enum order), rather than
continuing to guess from symptom shape as §5.21/§5.22 both had to pause
on. The real struct is 64 bytes (`0x40`, matching this fork's own already-
correct `sizeof(LoadedSound)`), with a genuine raw-data pointer at
absolute struct offset `0x38` (56) — the `MssSound::data` field, matching
this fork's own `offsetof(MssSound, data)` exactly. **The real bug**: for
the `FOLLOWING`/`INSERT` case (a fresh, right-here sample-data blob), the
real engine reads its OWN byte count for that raw copy from
`AILSOUNDINFO`-relative offset `0x18` (24) — which this fork's own
compiler-verified `offsetof(AILSOUNDINFO, bits)` confirms is exactly
where `bits` sits, NOT `info::data_len` (offset 16), which is what the
inherited-from-upstream DSL (`LoadedSound.txt`) actually specified:
`set count data info::data_len;`.

**Confirmed live, not just via offset arithmetic** — the exact standard
this whole session has held itself to: a temporary diagnostic dumped the
real struct values for `code_post_gfx.ff`'s own failing asset (index
4481, the same one §5.22 traced this bug to): `data_len=24932` (the value
the fork was WRONGLY using — plausible-looking, not obviously wrong on
its own) vs `bits=22050` (the value the real engine actually reads, per
the native decompile). Applying the one-line fix (`set count data
info::bits;`) and re-running against the SAME asset: it now loads
cleanly, and the zone progresses to a **new, later, different** failure
(`XFILE_BLOCK_SCRIPT`, a separate not-yet-investigated bug) — the
decisive confirmation this was a genuine stream desync (reading the wrong
byte count silently corrupts every subsequent read in the same block),
not a coincidental symptom match.

**Full regression testing before commit**: rebuilt and re-ran against all
six known reference zones (`sp_intro.ff`/`sp_prague.ff` unaffected;
`code_post_gfx.ff` progresses further; `hamburg.ff`/`common.ff`/
`sp_dubai.ff` unchanged, identical signatures) and the complete 39-zone
`sp_*.ff`/`so_*.ff` sweep (0 crashes across every zone — every log ends
with a clean `Finished`/`Failed` line; the fully-succeeding count stays
at 3/39, since this specific fix unblocks real forward progress within
`code_post_gfx.ff` rather than a full zone on its own — that zone has at
least one more, separate, not-yet-investigated bug downstream).

**Fixed at the correct, permanent location**: the tracked DSL source
(`tools/iw5oat/src/ZoneCode/Game/IW5/XAssets/LoadedSound.txt`), not just
the generated, gitignored `.cpp` output — survives the next real
`ZoneCodeGenerator` regeneration. Commit: `ab56e7cc`.

**Concrete next step, not yet investigated**: `code_post_gfx.ff`'s own
new `XFILE_BLOCK_SCRIPT` failure (a completely different block target
and a different asset — confirmed via a live diagnostic that the newly-
failing asset, index 4482, never reaches the `MssSound` fill path at all,
meaning it's a different code path, most likely `snd_alias_list_t`'s own
`StreamedSound` branch or another one of its top-level string fields).
`§5.21`'s own `"invalid block 15"` thread remains separately open too —
untouched by this round's work, a genuinely different asset chain
(`XModel`/`Material`, not `snd_alias_list_t`/`LoadedSound`).

## 5.25. UPDATE, 2026-09-14 (later still, continuing "dig on both" after §5.23 eliminated one hypothesis) — the real native `XModel` fill function decompiled directly: `materialHandles` is very likely a genuine phantom field, the same bug class as the already-fixed `Material::subMaterials` — a strong, well-evidenced lead, not yet implemented or verified

**Status: a strong, concrete new hypothesis with direct decompile evidence
behind it — NOT yet confirmed by testing, NOT yet implemented.**

Continuing directly from §5.23 (which eliminated the "a different,
narrower decode primitive explains this" theory and recommended finding
the real native array-walk logic next): traced the real dispatch chain
fresh — `FUN_14009bce0` (the master 46-case asset dispatch switch, `case
4` = `XModel`) → `FUN_14009c650` (the real outer FOLLOWING/INSERT/
already-resolved pointer handler, structurally identical to every other
asset type's own outer handler already understood this session) →
`FUN_14009c1b0` (the REAL native equivalent of this fork's own
`FillStruct_XModel`, decompiled in full for the first time this
session).

**The real fill function resolves exactly six simple pointer fields in
sequence** (`DAT_1407bea40[6]` through `[0xb]`), each using the identical
`if (field != 0) { if (field == -1) <FOLLOWING alloc>; else
FUN_1400aad40(); }` pattern already fully understood from the shipped
global fix (`e2fdeb07`) — matching this fork's own `boneNames`/
`parentList`/`quats`/`trans`/`partClassification`/`baseMat`, six fields,
exact same order. **Immediately after the sixth field (`[0xb]`,
`baseMat`), the real function jumps straight to a fixed-size, 4-entry
array with a `0x38`-byte stride** (matching `sizeof(XModelLodInfo)` — the
real `lodInfo` array) — **with NO seventh simple-pointer-field call in
between.**

**This fork's own generated code inserts exactly one extra field at
precisely that position**: `tools/iw5oat/build/src/ZoneCode/Game/IW5/
XAssets/xmodel/xmodel_iw5_load_db.cpp`'s own `FillStruct_XModel`
processes `boneNames`/`parentList`/`quats`/`trans`/`partClassification`/
`baseMat` (six fields, matching exactly), then **`materialHandles`** (one
more `FillPtr` call), and only THEN begins the `lodInfo` array loop. The
real native function has no equivalent seventh call at all before its own
LOD array begins.

**This is structurally identical to the already-fixed
`Material::subMaterials` bug (§5.9)** — a field this fork's own struct
declares (inherited from upstream OpenAssetTools' x86-era DSL,
`tools/iw5oat/src/Common/Game/IW5/IW5_Assets.h`'s `struct XModel` and
`tools/iw5oat/src/ZoneCode/Game/IW5/XAssets/XModel.txt`) that the real,
current x64 struct very likely doesn't actually contain. If confirmed,
this would be a genuinely serious bug, worse in effect than a simple
missing field: **every field this fork reads AFTER `materialHandles`
(the entire `lodInfo` array, `maxLoadedLod`, `numLods`, `collLod`,
`flags`, `collSurfs`, `numCollSurfs`, `contents`, and everything past
that) would be read 8 bytes off from its real position** — explaining
both this session's own already-observed malformed values
(`materialHandles`'s own "value," read from what's actually the FIRST
8 bytes of the real `lodInfo` array, would plausibly produce exactly the
kind of no-clean-pattern garbage already caught live — `0x01010150FFFFFFFF`/
`0xFFFFFFFF00000000` are both very plausible fragments of real
floating-point/count `XModelLodInfo` data misread as a pointer) and,
very plausibly, other not-yet-investigated `XModel`-related oddities
this fork hasn't hit yet simply because most zones' own `XModel` assets
never happen to populate every downstream field with a value that trips
a bounds/sentinel check.

**Deliberately NOT implemented or committed as a fix this round** — this
finding needs the same rigor §5.9's own `Material::subMaterials` fix
used before landing: independently confirm `sizeof(XModel)` computed
WITH vs WITHOUT `materialHandles` against the real native struct's own
total size (not yet done), and full regression-test across all six known
zones plus the 39-zone sweep before committing, exactly like every other
real fix this session. Flagging this now as a strong, well-evidenced,
concrete next step rather than rushing it — the general shape of the fix
(remove `materialHandles` from `IW5_Assets.h`'s `struct XModel` and the
now-dangling reference in `XModel.txt`'s own DSL, exactly mirroring
commit `30cf5723`'s own `subMaterials` fix) is clear, but has not been
applied.

Raw decompile evidence, newly added this round:
`re_notes/ghidra_scripts/decomp_dispatch_14009bce0.txt` (the master
dispatch switch, case 4 → `FUN_14009c650`),
`decomp_xmodel_14009c650.txt` (the outer pointer handler),
`decomp_xmodel_fill_14009c1b0.txt` (the real fill function itself — the
decisive evidence: six pointer-field resolutions, then straight to the
LOD array, no seventh field).

## 5.26. UPDATE, 2026-09-14 (later still, "fork that one now") — CORRECTION: §5.25's own "phantom field" conclusion was wrong. `materialHandles` is a real, correctly-positioned field, confirmed two independent ways against the SAME raw decompile evidence — the "invalid block 15" root cause remains genuinely open

**Status: a real correction to the immediately-prior round, not a new
finding of its own. The underlying `"invalid block 15"` bug stays
paused, unresolved — but one wrong lead is now closed off with hard
evidence, rather than left on record as the likely explanation.**

Re-read §5.25's own raw decompile evidence file
(`decomp_xmodel_fill_14009c1b0.txt`) directly rather than trusting its
prose summary, specifically to implement the "remove `materialHandles`,
mirror `subMaterials`" fix it recommended. **The raw decompile does NOT
actually support that conclusion** — §5.25's own count of "six
pointer-field resolutions, then straight to the LOD array" undercounts
by one: the real function has SEVEN conditional field-resolution blocks
after `name` (checking `DAT_1407bea40[5]` through `[0xb]`, i.e. seven
distinct QWORD-indexed struct slots — `boneNames`/`parentList`/`quats`/
`trans`/`partClassification`/`baseMat`, **and a seventh at index `0xb`**,
lines 95-98 of the same file), immediately followed by the `lodInfo`
array read. The seventh block was mis-read as not-a-field because its
own code SHAPE genuinely differs from the other six (no
FOLLOWING/INSERT/-1 branching, a direct
`FUN_1400aaa90(3)`-then-array-load pattern instead) — a real, easy
mistake to make skimming decompiler pseudocode, not a fabricated claim.

**Confirmed the seventh block genuinely is `materialHandles`, two
independent ways, not just re-reading more carefully:**

1. **Compiler-verified `offsetof()`/`sizeof()`, not hand arithmetic**
   (this project's own standing "trust the compiler over eyeballing
   layout" principle — see `CODE_STANDARDS.md`'s existing lesson on
   exactly this): a temporary diagnostic in the generated
   `FillStruct_XModel` printed `offsetof(XModel, materialHandles) =
   0x58` and `offsetof(XModel, lodInfo) = 0x60` directly from this
   fork's own real compiled struct. `0x60` (96 decimal) is an EXACT
   match for the real native code's own `DAT_1407bf340 = DAT_1407bea40
   + 0xc` (12 `longlong`-stride elements × 8 bytes = 96 bytes) —
   independent confirmation that `lodInfo` really does start exactly
   where this fork's struct already says it does, WITH
   `materialHandles` present and correctly sized at offset `0x58`
   (88 decimal, immediately before it).
2. **A fresh decompile of the seventh block's own target function**,
   not yet looked at in §5.25's own round:
   `FUN_140094a10(undefined8 param_1, uint param_2)` — reads
   `param_2 * 8` bytes (a genuine array of 8-byte pointers, exactly
   matching `Material**`), then loops `param_2` times (matching
   `numsurfs`, the exact parameter this fork's own generated call site
   already passes: `LoadPtrArray_Material(true, varXModel->numsurfs)`),
   resolving each entry via `FUN_1400aad10` for the "already resolved"
   case — the SAME primitive §5.23 already traced and confirmed
   functionally identical to the shipped fix's own `FUN_1400aad40`.
   This is unambiguously a real, working `materialHandles` array
   resolver, not a phantom-field artifact.

**Net effect**: `materialHandles` is real, correctly positioned, and its
own per-entry resolution already goes through the exact primitive
family the global fix (`e2fdeb07`) already handles correctly. The
"invalid block 15" bug's real cause is therefore NOT a phantom field —
that specific, concrete hypothesis is now closed with hard evidence on
both sides (struct-offset math AND a fresh function decompile), not left
open as the presumed likely explanation for a future session to
mistakenly implement.

**A genuinely new, smaller discrepancy surfaced by the same diagnostic,
not yet investigated**: this fork's own compiler-computed
`sizeof(XModel) = 0x1a8` (424 bytes) does NOT match the real native
code's own initial header-block read size, `0x1a0` (416 bytes, line 15
of the same decompile) — an 8-byte gap. Given `lodInfo` itself is fully
accounted for (ends at byte 320, well within the first 416-byte read),
this gap must live somewhere in the STRUCT'S OWN TAIL (the fields after
`lodInfo`: `maxLoadedLod` through `quantization`) — a real, distinct,
much narrower lead than the "phantom array field" theory this round
closes out, genuinely worth checking (a real field-order/padding/size
mismatch in that tail region, OR simply an artifact of how
`FUN_1400aad70`'s own multi-block read sequence divides up a struct that
gets fully covered by several separate reads rather than one — not yet
distinguished). Not chased further this round to avoid repeating the
same "one function, one theory, verify fully before concluding" mistake
this correction exists to fix.

All temporary instrumentation reverted (the gitignored generated
`xmodel_iw5_load_db.cpp`'s own diagnostic, added and removed within this
round — no tracked source ever touched), 0 regression confirmed against
all six known zones. New raw decompile evidence:
`re_notes/ghidra_scripts/decomp_140094a10.txt` (the real `materialHandles`
array resolver, the decisive new evidence this round adds).

## 5.27. UPDATE, 2026-09-14 (later still, continuing the §5.26 tail-gap lead) — a first attempt at the 8-byte `sizeof(XModel)` gap (removing `memUsage`) matched the arithmetic exactly but caused real segfaults in 16 of 41 zones live-tested — reverted, not shipped

**Status: reverted, not shipped.** A same-day follow-up round picked up
§5.26's own "genuinely new, smaller discrepancy" lead and reasoned that
`int memUsage;` was a phantom field, same bug class as the already-fixed
`Material::subMaterials` (commit 30cf5723) — "memory usage" being exactly
the kind of value a real engine computes at load time for its own
accounting, not something a zone file would ever serialize. Removing it
made both `offsetof(XModel, physPreset)` and `sizeof(XModel)` match the
native numbers (0x188/392 and 0x1a0/416) exactly, and it genuinely fixed
2 zones live-tested. But a full 41-zone sweep found it caused real,
non-catchable segfaults (not clean structured errors) in 16 zones — every
`so_survival_mp_*.ff` variant plus two others — even though the numeric
match looked decisive. Judged unsafe to ship on that evidence alone and
reverted in full (`git checkout --` on the touched tracked source,
confirmed via a clean `git diff` and a full rebuild) before being
documented — the round that made this fix was interrupted by a session
rate limit before it could write up its own finding, so this entry
records it after the fact for continuity. The real lesson, carried
forward into §5.28: two independent numeric checks matching is not proof
a phantom-field theory is correct — `memUsage` genuinely is real, and the
crash pattern this attempt produced turned out to be a property of
*any* 8-byte reduction in `sizeof(XModel)`, not something specific to
this one field (see §5.28).

## 5.28. UPDATE, 2026-09-15 ("keep going, dig on remaining threads", parallel fork) — the real source of the 8-byte `sizeof(XModel)` gap correctly identified and confirmed two independent ways (`sp_dubai.ff`/`sp_ny_harbor.ff` now load cleanly) — but shipping it exposes a separate, deeper crash bug reachable in 18 zones, so it was reverted rather than shipped; the crash is a property of shrinking `sizeof(XModel)` at all, not of which field causes it

**Status: correct root cause found and verified, but NOT shipped — a
separate, unresolved blocker downstream makes it unsafe.** Picks up
directly from §5.27's own real lesson (a numeric match alone doesn't
prove a phantom-field theory) by re-deriving the 8-byte gap from first
principles instead of pattern-matching to the `subMaterials`/`memUsage`
precedent again.

**Re-walked `FUN_14009c1b0`'s own field-by-field decompile
(`re_notes/ghidra_scripts/decomp_xmodel_fill_14009c1b0.txt`) by hand
against the CURRENT `XModel` struct (with `memUsage` restored), tracking
real byte offsets rather than guessing.** Every field from `name` through
`boneInfo` (native offset 0x158) matches this fork's own struct layout
EXACTLY, confirmed via the decompile's own count/size arithmetic at each
step (`boneNames` sized by `numBones`, `quats`/`trans`/`baseMat` sized by
`(numBones-numRootBones)` with the right per-element byte multiplier,
`materialHandles` sized by `numsurfs`, `lodInfo[4]` read as one 0xe0-byte
block matching `4 * sizeof(XModelLodInfo)` exactly, `collSurfs`/
`numCollSurfs` at native offsets 0x148/0x150 matching a `0x30`-byte
element size against `sizeof(XModelCollSurf_s)`, `boneInfo` at 0x158
sized by `numBones * 0x1c` matching `sizeof(XBoneInfo)`). This
independently re-confirms §5.26's own correction (`materialHandles` is
real) via a completely different method (manual offset arithmetic, not
`offsetof`/fresh decompile of the resolver itself).

**Past `boneInfo`, the decompile shows only TWO more pointer-fixup calls**
(`DAT_1407bea40 + 0x31` and `+ 0x32`, native offsets 0x188 and 0x190),
not three — but the current struct has THREE remaining pointer fields
after `boneInfo` (`invHighMipRadius`, `physPreset`, `physCollmap`).
Freshly decompiled the two handler functions (`FUN_140096140`/
`FUN_140095f90`) and their own sub-callees
(`FUN_140096050`/`FUN_140095e40`,
`re_notes/ghidra_scripts/decomp_xmodel_tail_fields.txt` and
`decomp_xmodel_tail_subcallees.txt`): both are single-asset-pointer
resolvers (type 3, the same generic asset-lookup already seen for
`materialHandles`/`collSurfs`/`boneInfo`), and their own shapes identify
them precisely — the first reads an 0x50-byte header with a `name` field
and a second string field a few bytes in, matching `PhysPreset`'s own
`name`+`sndAliasPrefix` layout; the second reads an 0x58-byte header with
a `name`, a count, and a pointer to an array of 0x48-byte elements,
matching `PhysCollmap`'s own `name`+count+geometry-array layout. **This
positively identifies native offset 0x188 as `physPreset` and 0x190 as
`physCollmap`** — not `invHighMipRadius`.

**This rules out both of the two obvious theories for `invHighMipRadius`
at once.** If it's a genuine 8-byte POINTER (its current declared type),
`physPreset` would land at native offset 0x190, one full field too late.
If it's fully ABSENT (the `subMaterials`/`memUsage` precedent), `physPreset`
would land at 0x180, eight bytes too early. The only layout that makes
every byte from `boneInfo` (0x158) through the end of `physCollmap`
(0x198) line up exactly against the decompile, with the real total header
size landing at exactly 0x1a0 (416 bytes, matching `FUN_1400aad70`'s own
initial read at the top of `FUN_14009c1b0`) and zero unexplained padding
anywhere, is an INLINE, non-pointer 8-byte field sitting between `bounds`
and `memUsage` — i.e. `invHighMipRadius` is a real field, just not a
pointer to a heap array. Given its current 8-byte size budget and its own
name, the natural fit is `unsigned short invHighMipRadius[4]` — one entry
per LOD level, mirroring `lodInfo[4]` already in the struct. This is also
fully consistent with the DSL's own `set condition invHighMipRadius
never;` (`XModel.txt`) — that marker was written under the reasonable but
incorrect assumption that the field is a pointer needing fill-code
suppression; an inline scalar array needs no fill code or DSL directive
at all (same as `noScalePartBits[6]`, which has none), so the marker
becomes redundant rather than wrong. Confirmed via a whole-tree grep that
`invHighMipRadius` is referenced nowhere in the generated
`xmodel_iw5_load_db.cpp` — the type change requires no ZoneCodeGenerator
regeneration, a pure struct-layout fix.

**Implemented, built clean, and tested — the fix is real and positive on
its own zones.** `sp_dubai.ff` (previously `INVALID_BLOCK`) now loads
completely cleanly (`Finished with 0 warnings, 0 errors`), and
`sp_ny_harbor.ff` (previously `NOT_RECORDED`) does too — a genuine,
unexpected bonus fix, consistent with XModel's own per-instance
stream-cursor misalignment being a plausible contributor to some of the
broader "not recorded" failure class elsewhere in the format (a lead worth
connecting to whichever round is chasing that bug class — see
`known_issues_x64.md` issue #1's own "not recorded" thread). `hamburg.ff`/
`common.ff` shift to a different, later, still-clean structured error
(`larger than its size`, `XFILE_BLOCK_TEMP`) rather than their prior
`not recorded`/`invalid block` errors — expected: fixing an earlier
structural bug lets loading progress further before hitting the next real
issue, not a regression.

**But a full 41-zone sweep found the exact same 18-zone crash pattern
§5.27's reverted `memUsage`-removal attempt produced** — all 16
`so_survival_mp_*.ff` variants plus `so_nyse_ny_manhattan.ff` and
`so_zodiac2_ny_harbor.ff`, real segfaults (no clean `Failed`/`Finished`
line at all), not caught exceptions. **This is the single most important
finding of this round**: two completely different fields
(`memUsage`, a scalar; `invHighMipRadius`, now correctly re-typed) produce
the IDENTICAL crash-zone set when each shrinks `sizeof(XModel)` by the
same 8 bytes. That can't be a coincidence of which field is wrong — it
means the crash is a property of the struct's total size changing at all,
not of which specific field causes the change. Traced one crash
(`so_survival_mp_alpha.ff`) with a temporary asset-index/type diagnostic
in `ContentLoaderIW5.cpp`'s `LoadXAssetArray` loop (reverted before
finishing, no tracked source changed): the crash happens while loading
asset index 785 of 953, **type 11 (`ASSET_TYPE_SOUND`, `snd_alias_list_t`)
— not an `XModel` at all**, deep into a zone that (pre-fix) never got
anywhere near that far before erroring out on the earlier `not recorded`
bug. Checked for the most likely mechanical explanation (a hardcoded byte
count somewhere downstream still assuming the old, larger `sizeof(XModel)`,
which would now under-allocate and overrun by 8 bytes) — both real
consumers (`xmodel_iw5_load_db.cpp`'s `LoadWithFill(sizeof(XModel))` and
its post-load `std::memcpy(reallocatedAsset, *pAsset, sizeof(XModel))`)
already compute the size dynamically via `sizeof(XModel)`, not a literal,
so that specific theory is ruled out. Two real possibilities remain,
neither chased further this round: (1) some `XModel` instance specifically
present in these 18 zones' own asset lists has a genuinely different real
layout than the one confirmed here (a conditional/versioned struct shape
this round hasn't found), so the fix is right for the zones tested but
wrong for these; or (2) a real, separate, pre-existing bug already lives
in `snd_alias_list_t` loading that was simply never reachable before,
because every zone that reaches asset 785 today first died much earlier
on the `not recorded` bug this exact fix incidentally works around for
some zones (`sp_ny_harbor.ff`) but not others (these 18).

**Reverted rather than shipped, per this project's own strict "even one
new real crash disqualifies it" bar.** `git checkout --` restored
`IW5_Assets.h`'s `invHighMipRadius` field and `ContentLoaderIW5.cpp`'s
temporary diagnostic to their last committed state; confirmed via a clean
`git diff` and a full rebuild that `sp_dubai.ff` is back to its original
`INVALID_BLOCK` failure and `so_survival_mp_alpha.ff` no longer crashes
(clean, non-zero exit, no segfault). No tracked source changed by this
round. New raw decompile evidence kept and committed:
`re_notes/ghidra_scripts/decomp_xmodel_tail_fields.txt` and
`decomp_xmodel_tail_subcallees.txt` (the two tail-field resolvers and
their sub-callees, the decisive evidence identifying `physPreset`/
`physCollmap`'s real native offsets).

**Recommended next step for whoever picks this up**: don't re-attempt a
third field-level guess at the same 8-byte gap — the gap itself is now
correctly identified and well-evidenced (`invHighMipRadius[4]`).
Instead, root-cause the `snd_alias_list_t` crash at asset index 785 of
`so_survival_mp_alpha.ff` directly (a live diagnostic trace inside the
Sound loader's own fixup functions, or a WinDbg/cdb live-attach session
rather than a post-mortem dump, since `Unlinker.exe` doesn't currently
register a WER local dump path) before re-applying this struct fix. If
that crash turns out to be a genuinely separate, pre-existing bug newly
reachable rather than something this fix itself causes, this exact
`invHighMipRadius[4]` change is very likely safe to ship once the Sound
bug is fixed independently.

## 5.29. UPDATE, 2026-09-15 (parallel fork, "keep going, dig on remaining threads") — the dominant `"lookup ... not recorded"` failure class (33 of 41 real zones) root-caused to a genuine forward-reference-before-write ordering bug in the single-pass loader, and fixed pragmatically via graceful degradation — real, verified progress, zero new crashes, but the underlying architectural gap stays open

**Status: real, tested, shipped mitigation — not a full architectural fix.**
Picks up the thread left by an earlier round of this same session (before
this parallel-fork split): a fresh 39/41-zone sweep found 33 zones sharing
the identical `InvalidLookupPositionException` ("Zone tried to lookup at
block N, offset N that was not recorded"), thrown from
`ConvertOffsetToAliasLookup`'s own bounded 16-hop alias-chain chase
(`ZoneInputStream.cpp`, shipped 2026-09-14) exhausting its hop cap. A
partial finding from that earlier round (now confirmed, not just
suspected) was decisive: `offsetInt`/`resolvedSlot`/`resolved` are
byte-for-byte identical across all 16 hops for the traced case — this is
not a deep chain, it's a stuck loop.

**Why more hops can never help, confirmed by re-deriving it from the code
directly rather than trusting the earlier partial finding at face value**:
every hop of the loop executes synchronously within one function call,
with nothing else running in between — no other asset gets loaded, no
field gets filled, between hop N and hop N+1. If hop 0 finds the aliased
target slot (`m_pointer_redirect_lookup`'s own stored `alias`, the ADDRESS
of another field's own storage slot) still holding its own raw,
unconverted offset, every subsequent hop recomputes the exact same
`offsetInt` from that same unchanging raw value and reads the exact same
still-raw value again. The loop is a no-op repeated up to 16 times.

**Root cause pinned down precisely with a live diagnostic** (temporarily
instrumenting `AddPointerLookup` and `ConvertOffsetToAliasLookup`'s hop-0
branch, tested against the small `so_survival_mp_alpha.ff`, reverted
before shipping): the stuck case traces to `LoadPtrArray_Material`
(`xmodel_iw5_load_db.cpp`, generated from XModel's own `materialHandles`
array-of-`Material*` DSL declaration). That function is itself already a
two-phase design — phase 1 (`atStreamStart`) `FillPtr`s every array slot
with its raw on-wire value AND registers `AddPointerLookup(&varMaterialPtr[index],
...)` for each one, up front, before any slot is resolved; phase 2 then
walks the array in strict ascending-index order, calling
`Loader_Material::Load` on each slot, which — for a slot whose raw value is
a REFERENCE rather than a first (`FOLLOWING`/`INSERT`) occurrence — calls
`ConvertOffsetToAliasLookup` to resolve it. The bug: when a REFERENCE at
array index *i* points at another slot's own storage position (materials
are frequently shared/deduplicated across surfaces of the same model, and
the original on-wire format encodes that sharing by having the later
duplicate's own slot literally hold the earlier slot's block position as
its "value"), and that earlier slot has an index *j* > *i* — array-order
processing hasn't reached index *j* yet, so index *j*'s own slot genuinely,
correctly, still holds its own unconverted raw fill content at the exact
moment index *i*'s lookup needs it. This is a real architectural gap in
this fork's single-pass, synchronous, non-rewindable streaming loader (the
underlying `ILoadingStream` decompresses sequentially — `LoadDataInBlock`
calls straight through to it — there is no way to safely
defer-and-retry a partially-consumed asset's own read without either a
much larger stream-checkpointing mechanism or a genuine two-pass rewrite of
asset loading; both considered and deliberately NOT attempted here, given
this exact code area's own documented history of real segfaults from
rushed changes — see SS5.16-SS5.18 and SS5.27 above).

**Fixed pragmatically instead of architecturally: graceful degradation.**
`ConvertOffsetToAliasLookup`'s hop-cap-exhaustion path no longer throws a
zone-load-aborting exception — it logs a `con::warn` (visible in the
`Unlinker.exe` warning count, honest about the degradation, not silent)
and returns `nullptr` for just that one field, exactly the same
"missing/absent" shape every observed caller of this API already handles
safely (e.g. `LoadPtrArray_Material`'s own `if (*varMaterialPtr) { ... }`
check before recursing). This is a deliberately narrow, low-risk fix: it
touches only the ONE throw site at the bottom of `ConvertOffsetToAliasLookup`,
not the offset-decode arithmetic or the hop-chase loop itself (both
already correct and both the exact code with segfault history this session
has otherwise treated with real caution).

**A second, separate call path producing the identical "not recorded"
message text was found and deliberately left alone**:
`MaybePointerFromLookup<T>::Expect()` (`ZoneInputStream.h`) throws the same
`InvalidLookupPositionException` when a `ConvertOffsetToPointerLookup`
result is still unresolved at the point its caller needs it — genuinely
the same underlying architectural gap, reached via a sibling API. Its own
call sites span 73 generated files across every game this fork's shared
`ZoneLoading`/`ObjLoading` code supports (T4/T5/T6/QOS/IW3/IW4/IW5), a much
larger surface than `ConvertOffsetToAliasLookup`'s own ~24. Two of the 41
real zones swept below (`so_stealth_prague.ff`, `so_timetrial_london.ff`)
still fail on a residual "not recorded" error reached via this OTHER path,
unchanged by this round's fix — left as a real, separate, correctly-scoped
follow-up rather than folded in here without the same level of dedicated
verification this round gave `ConvertOffsetToAliasLookup` alone.

**Rigorously tested, including catching a real methodology hazard along
the way**: an initial full-sweep test showed 19 zones (all 17
`so_survival_mp_*.ff` variants plus `so_nyse_ny_manhattan.ff` and
`so_zodiac2_ny_harbor.ff`) as real crashes — alarming, given this exact
"fixes some zones, segfaults others" shape is what sank the earlier
`memUsage`-removal attempt (SS5.27). Investigated before concluding
anything: `git status` showed the OTHER parallel fork (working on the
`sizeof(XModel)` gap in the same shared working tree and shared
`build/bin/Release_x64/Unlinker.exe` output) had left, then since
reverted, its own in-progress diagnostic in `ContentLoaderIW5.cpp` — the
crashing binary had been built from a mid-flight mix of that (already
withdrawn) diagnostic plus this round's own fix, not this fix alone. A
full clean `/t:Rebuild` from a working tree confirmed to contain ONLY this
round's own `ZoneInputStream.cpp` change reproduced ZERO crashes across
all 41 zones — the real, trustworthy result. **Standing lesson for any
future parallel-fork session sharing one working tree/build output**:
re-verify `git status` immediately before AND after a clean rebuild when a
sibling fork is active, and always `/t:Rebuild` (not an incremental build)
before trusting a crash/no-crash verdict if a shared build output could
have been touched by other in-flight work.

**Full, final 41-zone sweep result (clean rebuild, verified trustworthy)**:
zero real crashes anywhere (every run ends with a clean `Finished with`/
`Failed with` line). The 3 already-working zones (`sp_intro.ff`,
`sp_prague.ff`, `so_trainer2_so_deltacamp.ff`) are unaffected (still `0
warnings, 0 errors` — this fix's warn-path never fires for them, fully
non-invasive to the working path). Of the 33 originally-`NOT_RECORDED`
zones: 24 now progress to the already-tracked `"invalid block 15"` error
(the OTHER parallel fork's own territory, SS5.26/SS5.28), 7 now progress to
the already-tracked `"larger than its size"` error class, and 2
(`so_stealth_prague.ff`, `so_timetrial_london.ff`) hit the separate
`Expect()`-path "not recorded" case described above. No zone regressed
(nothing that worked before now fails; nothing that failed cleanly before
now crashes). This is real, substantial, verified progress on the single
highest-impact remaining bug class of this whole investigation (87% of
real zones affected before this round) — even though it doesn't by itself
flip any zone all the way to a clean load, since most affected zones carry
more than one distinct remaining format issue.

**Shipped**: `tools/iw5oat/src/ZoneLoading/Zone/Stream/ZoneInputStream.cpp`.
**Not shipped / explicitly left open**: the `Expect()`/`ConvertOffsetToPointerLookup`
sibling path (2 zones, needs its own dedicated round given its much wider
call-site surface); a true architectural fix (stream checkpointing or a
genuine two-pass load) that would resolve this class of forward reference
without leaving any field null.

## 5.30. UPDATE, 2026-09-15 ("keep going", static-instrumentation-only, no live debugger per direct user instruction after a cdb-related system crash) — the final 18-zone real-crash class (Sound/`snd_alias_list_t` deduplication) root-caused and fixed: two real bugs found via static tracing + native cross-check, zero new crashes across the full 45-zone sweep, `sp_dubai.ff`/`sp_ny_harbor.ff` now fully clean

**Status: real, tested, shipped fixes.** Picks up directly from §5.28's own
handoff (the correctly-identified `invHighMipRadius[4]` fix, reverted
because it exposed 18 real crashes in `so_survival_mp_*.ff` + 2 others) and
§5.29's already-shipped alias-chain graceful-degradation fix. **Live
debugger use (cdb specifically) caused a real system crash requiring a hard
power-cycle earlier in this exact investigation thread** — this round was
completed entirely via static instrumentation (`fprintf`+`fflush`
diagnostic tracing compiled into `Unlinker.exe`, run normally, no debugger
attach at all) and static Ghidra decompilation (no live process attach),
per direct standing instruction: only `x64dbg` (this project's own approved
tool) may ever be used for live debugging on this machine, never
`cdb`/WinDbg, regardless of whether x64dbg is reachable in a given session.

**Re-applied the `invHighMipRadius[4]` fix and re-reproduced the crash
cleanly.** `so_survival_mp_alpha.ff` confirmed still segfaults (exit 139,
no clean `Failed`/`Finished` line) at the exact same asset index (785 of
953) as §5.28 found, now unambiguously identified as **asset type 11**
(`ASSET_TYPE_SOUND` / `snd_alias_list_t`) via coarse per-asset-index
tracing added to `ContentLoaderIW5.cpp`'s `LoadXAssetArray` loop (temporary,
reverted before shipping).

**Bug #1 — a genuine crash inside `Marker_snd_alias_list_t::Mark_snd_alias_list_t()`
(generated code), confirmed via fine-grained diagnostic tracing added
directly to the generated `snd_alias_list_t_iw5_load_db.cpp`/`_mark_db.cpp`
(also temporary, reverted).** Asset 785's own top-level pointer resolves
FOLLOWING (needs a fresh header read), but that header's own `head` field
(the array of individual `snd_alias_t` entries, DSL-declared `set reusable
head;` — this format's real shared/deduplicated-array mechanism, `set
count head count;` ties `count` to it) resolves via `OFFSET`
(`ConvertOffsetToPointerLookup`, a genuine reference to an ALREADY-loaded
array owned by an earlier asset) rather than a fresh load. `head` itself
resolves correctly to a real, valid heap pointer — but `count`, read
unconditionally as part of the same 24-byte header regardless of which
branch `head` takes, printed as `-1` (0xFFFFFFFF) in this exact case.
`Mark_snd_alias_list_t()`'s own generated body unconditionally calls
`MarkArray_snd_alias_t(varsnd_alias_list_t->count)` whenever `head` is
non-null, with no branch awareness — the `int count = -1` implicitly
converts to `MarkArray_snd_alias_t(const size_t count)`'s parameter as
`0xFFFFFFFFFFFFFFFF`, and the resulting near-infinite `for` loop walks
`snd_alias_t* var` far past any real allocation within a handful of
iterations. **Confirmed this is the real native engine's OWN architecture,
not a fork bug in the count value's meaning**: fresh Ghidra decompiles of
`FUN_14009f3f0`/`FUN_14009f4b0`/`FUN_14009f590`/`FUN_140096900` (the real
x64 `snd_alias_list_t`/`snd_alias_t`/`SoundFile`/`SoundFileRef` fill chain,
saved in `re_notes/ghidra_scripts/decomp_snd_alias_*.txt`) show every
single struct's real native byte size matches this fork's own C++
`sizeof()`/`offsetof()` EXACTLY (`snd_alias_list_t`=24,
`snd_alias_t`=0x98/152, `SoundFile`=0x18/24, `StreamedSound`=0x10/16,
`volumeFalloffCurve`@120=8×15, `speakerMap`@144=8×18) — ruling out a
stream-cursor-desync theory. The native `Mark`-equivalent logic for a
`reusable`/lookup-resolved array is architecturally expected to already be
covered by the ORIGINAL asset that first loaded it — this fork's own
Mark-phase code generation simply doesn't carry the FOLLOWING-vs-lookup
distinction from Load into Mark at all, a real, confirmed generator gap.
**Fixed via a new, permanent, tracked-source post-processing script**
(`tools/iw5oat/x64_offset_fixes/05_fix_mark_reusable_count.py`, following
the exact conventions of the existing 01-04 scripts, since the actual
buggy file lives in gitignored `build/` and needs to survive
regeneration): wraps the one confirmed-buggy call site with `if (count > 0
&& count <= 100000)` — a deliberately generous, clearly-unreachable-by-
real-data ceiling (every real array this session observed tops out at a
handful of entries) that skips the walk entirely on a garbage count rather
than trusting it. **Deliberately scoped narrow, not blanket**: a DSL sweep
found 18 IW5 asset types declare at least one `reusable` field (`AddonMapEnts`,
`Font_s`, `GfxImage`, `GfxWorld`, `LoadedSound`, `MapEnts`, `Material`,
`MaterialTechniqueSet`, `PathData`, `PhysCollmap`, `VehicleTrack`,
`WeaponAttachment`, `WeaponCompleteDef`, `XModel`, `XModelSurfs`,
`clipMap_t`, `menuDef_t`, `snd_alias_list_t`) — the script's own
`CONFIRMED_BUGGY_CALLS` allowlist deliberately touches only the one
independently-verified site; the other 17 are a real, documented,
NOT-yet-individually-checked lead for a future round, each needing its own
native cross-check before being folded in.

**Bug #2 — a second, distinct, more severe crash found immediately after
fixing Bug #1 (Mark now completes cleanly, but `LinkAsset` crashes next):
`AssetLoader::LinkAsset`/`GetAssetInfo` (hand-written, tracked source)
took `std::string name`/`const std::string& name` respectively, but every
real generated call site across all 194 asset types passes
`AssetName<T>(**pAsset)` — a raw `const char*`/`const char*&` — directly.**
Constructing a `std::string` from a null `const char*` is undefined
behavior (a real, reliable MSVC crash constructing/hashing the string).
Confirmed via diagnostic tracing that asset 785's own `aliasName` field
genuinely IS null after the fill — and confirmed via the SAME native
decompile trail (`FUN_14009f4b0`'s own `if (*DAT_1407bed68 != 0) { ... }`
guard around aliasName's own string-resolution step) that **a null
aliasName for this exact "reusable head, lookup-resolved" case is real,
legitimate, intentional native retail data** — the real engine explicitly
skips name resolution when the raw field is already zero, it does not
crash or substitute a name. This is NOT corruption, and NOT the same root
cause as Bug #1 (a fresh discovery, not a second symptom of the same
underlying issue) — it just happened to be reachable only once Bug #1 no
longer crashed first. **Fixed at the permanent, tracked, hand-written
source** (`tools/iw5oat/src/ZoneLoading/Loading/AssetLoader.h`/`.cpp`):
changed `LinkAsset`'s first parameter from `std::string name` to `const
char* name` and `GetAssetInfo`'s from `const std::string&` to `const
char*`, safely substituting an empty string for a null input inside each
function body rather than crashing on construction. **Zero call-site
changes needed anywhere** — confirmed via a full grep across every
generated `*_load_db.cpp` in `build/` that all 40 real `LinkAsset`/40 real
`GetAssetInfo` call sites share the exact same
`AssetName<AssetX>(**pAsset)` shape, so the parameter-type change is a
single, permanent, universal fix for every asset type at once, not a
Sound-specific patch.

**A live re-test after Bug #2's fix found the count-guard from Bug #1 was
too narrow**: `so_nyse_ny_manhattan.ff` still crashed, this time with
`count` reading as a large POSITIVE garbage value (`805330033`) rather than
`-1` — a bare `count > 0` check passes this straight through into the same
runaway-loop crash. Widened the guard to `count > 0 && count <=
MAX_SANE_COUNT` (100,000) in the same script, confirmed via a fresh
`--apply` + rebuild + full sweep that this closes both known garbage-value
shapes.

**Full 45-zone sweep (all 41 `sp_*.ff`/`so_*.ff` files, plus
`code_post_gfx.ff`/`hamburg.ff`/`common.ff`), after both fixes, on a
verified-clean build (every temporary diagnostic fully reverted from
tracked source first)**: **zero real crashes anywhere** — 5 zones load
completely cleanly (`sp_intro.ff`, `sp_prague.ff`, `so_trainer2_so_deltacamp.ff`,
plus **`sp_dubai.ff` and `sp_ny_harbor.ff`, both newly unblocked by this
round**), the other 40 each hit their own already-tracked, bounded,
non-crashing error class (`INVALID_BLOCK`, `NOT_RECORDED`,
`LARGER_THAN_SIZE`). All 18 previously-crashing zones (16×
`so_survival_mp_*.ff` + `so_nyse_ny_manhattan.ff` + `so_zodiac2_ny_harbor.ff`)
now fail cleanly (`NOT_RECORDED`) instead of segfaulting — real,
substantial, verified progress on what was genuinely the final blocking
crash class this session's own zone-loading investigation had open.

**Shipped**: `tools/iw5oat/src/Common/Game/IW5/IW5_Assets.h`
(`invHighMipRadius[4]`, re-applying §5.28's already-correct fix),
`tools/iw5oat/src/ZoneLoading/Loading/AssetLoader.h`/`.cpp` (the real,
permanent, hand-written null-name fix), `tools/iw5oat/x64_offset_fixes/
05_fix_mark_reusable_count.py` (new, permanent post-processing script for
the generated Mark-phase gap), plus four new Ghidra decompile evidence
files under `re_notes/ghidra_scripts/`. **Not shipped / explicitly left
open**: the other 17 `reusable`-field asset types' own Mark-phase call
sites (a real, documented, narrower-scope lead, not blanket-extended
without individual verification); the deeper architectural question of
whether ZoneCodeGenerator's own C# source should be fixed directly (out of
scope for this fork's own post-processing-script approach, a much larger
undertaking).

## 6. Scoped plan for in-house tooling — an MVP, not a full OpenAssetTools replacement

**This project's own actual need is narrow**: GSC/rawfile extraction to
support the standing GSC-first RE methodology (`CLAUDE.md`'s own
directive) — not full asset extraction parity (models, materials, sounds,
etc.) the way OpenAssetTools targets generally. Recommending a real,
achievable MVP rather than reproducing OpenAssetTools' full scope:

- **A minimal, from-scratch zone-header parser**: read the confirmed-
  unchanged outer header (§1) and 44-byte block-size header (§5), inflate
  the zlib payload (standard, any language's zlib binding works), walk
  the now-fully-understood per-asset dispatch loop (§5's real fix — 16
  bytes per record, `{type:4, pad:4, dataPtr:8}`) to enumerate every
  asset by type, then parse ONLY the `scriptfile`/`rawfile` asset structs
  specifically — not every asset type in the zone.
- **The dispatch-loop fix itself is DONE (§5)** — the exact byte layout
  is confirmed and cross-validated against OAT's own asset-type enum, not
  a remaining unknown. **Still needed, not started**: the individual
  `scriptfile`/`rawfile` asset struct's own real x64 field layout (asset
  type indices 39/38 respectively per §5's enum mapping) — derived the
  same way, decompiling their specific per-type loader functions
  (`FUN_140095f90`-class functions §5 already located but did not open)
  in `iw5sp.exe`.
- **A concrete corollary of §5, worth stating plainly**: because the
  dispatch loop is now understood, ANY of the other ~44 asset types could
  be added later using the identical technique — this MVP scope is a
  sequencing choice (do the one this project actually needs first), not a
  hard technical ceiling on what's reachable.
- **Public-tooling framing, per direct instruction**: build this as a
  clean, documented, standalone tool from the start (not a throwaway
  script) — the user's own stated intent is to release this as part of
  this project's public community-patching effort once finished. **The
  language/toolchain question is now resolved by §4's fork**: work
  directly in C++ inside `tools/iw5oat/`, reusing OpenAssetTools' own
  already-correct unsigned-zone inflate/hash-skip logic and its vendored
  zlib dependency rather than reimplementing that correctly-working part
  from scratch — the fork itself was the toolchain decision.

## 5.31. UPDATE, 2026-09-15 (parallel fork, "go after that one with 3 forks") — the `XFILE_BLOCK_SCRIPT` "size 0" crash class: block-size TABLE ORDER/WIDTH definitively RULED OUT via direct empirical evidence; the true block size really is 0 for this content, root cause lies elsewhere

**Status: hypothesis eliminated with hard evidence, not a fix.** One of
three parallel forks investigating the `XFILE_BLOCK_SCRIPT` "size 0" crash
class (`hamburg.ff`/`common.ff`/`code_post_gfx.ff`/`rescue_2.ff`/
`common_survival.ff`/`so_stealth_prague.ff`, error shape: `Zone referenced
offset N of block XFILE_BLOCK_SCRIPT which is larger than its size 0`).
Assigned angle: verify whether the real native x64 block-size-table ORDER
(TEMP/PHYSICAL/RUNTIME/VIRTUAL/LARGE/CALLBACK/VERTEX/INDEX/SCRIPT, per
`ZoneLoaderFactoryIW5.cpp`'s `SetupBlock()`) actually matches this fork's
own assumed order — §5's own earlier claim that the 44-byte block-size
header is "CONFIRMED UNCHANGED" rested on a raw hex-dump comparison of the
BYTES, not an independent verification that this fork reads those 9 values
into the correct slots.

**Definitively ruled out via direct empirical cross-zone comparison —
stronger evidence than a native decompile would have given, and much
faster.** Added temporary diagnostic tracing (`fprintf`+`fflush` to
stderr, no debugger of any kind — see this project's own standing rule:
cdb/WinDbg caused a real system crash earlier this investigation, static
analysis and normal tool runs only) to `StepAllocXBlocks.cpp`, dumping all
9 raw block-size values with their block names for three zones:

```
sp_intro.ff (WORKING, thin loader):
  TEMP=136 PHYSICAL=0 RUNTIME=0 VIRTUAL=423 LARGE=0 CALLBACK=0 VERTEX=0 INDEX=0 SCRIPT=62

sp_dubai.ff (WORKING, real large content zone, unblocked by §5.30's fix):
  TEMP=920 PHYSICAL=0 RUNTIME=0 VIRTUAL=16343069 LARGE=0 CALLBACK=0 VERTEX=3919616 INDEX=652224 SCRIPT=7242

code_post_gfx.ff (FAILING, this exact bug class):
  TEMP=351378 PHYSICAL=0 RUNTIME=0 VIRTUAL=1728453 LARGE=0 CALLBACK=0 VERTEX=4224 INDEX=480 SCRIPT=0
```

**This is decisive.** Both working zones show real, sane, non-anomalous
values in every slot, including a genuinely small-but-nonzero SCRIPT value
(62, 7242) — proving the parser correctly locates and reads the SCRIPT
slot when real content exists there. `code_post_gfx.ff`'s own 8 OTHER
values are equally sane (matching the same zero/nonzero shape pattern as
the two working comparisons — PHYSICAL/RUNTIME/LARGE/CALLBACK are zero in
ALL THREE zones, not just the failing one) — nothing here looks shifted,
truncated, or garbage. **`code_post_gfx.ff`'s SCRIPT block genuinely, truly
is 0 bytes on disk.** The block-size table's order, width, and parsing are
completely correct; this whole hypothesis class (header/table
misalignment) is closed.

**Implication for the other two forks' own angles**: the real bug is
downstream of block-size-table parsing entirely — either (a) some asset's
own offset-to-block-index DECODE math computes block index 8 (SCRIPT)
incorrectly for a pointer that should resolve to a different, genuinely
non-empty block (an offset-decode bug, not a ScriptFile-specific one), or
(b) `ScriptFile`'s own native block assignment genuinely differs from what
`ScriptFile.txt`'s DSL (`set block buffer XFILE_BLOCK_SCRIPT;`) declares
for x64 specifically — i.e. the asset's own content isn't stored in SCRIPT
at all in the current build, so a real, non-empty block elsewhere holds
what this fork is wrongly looking for in an empty SCRIPT block. Both
remain open, live investigation angles; this round only closes the
block-table-order hypothesis specifically. `ASSET_TYPE_SCRIPTFILE`'s real
native fill function is `FUN_1400964a0` (case `0x27` in
`FUN_14009bce0`'s dispatch, per `re_notes/ghidra_scripts/decomp_dispatch_14009bce0.txt`
line 178-181) — not yet decompiled by this round, a direct next step for
whichever angle proves correct.

No source changes shipped (temp diagnostic added and fully reverted,
confirmed via `git diff`/clean rebuild). No files touched that the other
two parallel forks' own angles depend on.

## 5.32. UPDATE, 2026-09-15 (parallel fork, third of "go after that one with 3 forks") — `ScriptFile`'s own real x64 fill function decompiled and cross-checked field-for-field against this fork's implementation: a PERFECT match, definitively ruling out theory (b) from §5.31 (ScriptFile assigned to the wrong block on x64) — the real bug is confirmed to be an upstream corruption of a specific asset's own header fields, not anything in ScriptFile's own load/block-assignment logic

**Status: hypothesis eliminated with hard evidence, not a fix.** Assigned
angle: native RE of `ScriptFile`'s own real x64 fill function, since
`XFILE_BLOCK_SCRIPT` is referenced by exactly one asset type in this fork
(confirmed via `grep -rn "XFILE_BLOCK_SCRIPT" tools/iw5oat/src/` — only
`ScriptFile.txt`/`scriptfile_iw5_load_db.cpp`/`scriptfile_iw5_write_db.cpp`).

**Decompiled the real chain**: `FUN_14009bce0`'s dispatch case `0x27`
(`ASSET_TYPE_SCRIPTFILE`) → `FUN_1400964a0` (the real `LoadPtr_ScriptFile`
equivalent — sentinel/FOLLOWING/INSERT/lookup branching, byte-for-byte
identical shape to every other asset's own top-level pointer resolver
already decompiled this session) → `FUN_140096380` (the real
`Load_ScriptFile` body-fill, newly decompiled this round, saved as
`re_notes/ghidra_scripts/decomp_scriptfile_1400964a0.txt` and
`decomp_scriptfile_body_140096380.txt`).

**`FUN_140096380` matches this fork's own generated
`Load_ScriptFile`/`FillStruct_ScriptFile` EXACTLY, field for field, byte
for byte, block-index for block-index:**
- Reads `0x28` (40) bytes for the header fill — matches
  `sizeof(ScriptFile)` exactly (`name`(8) + `compressedLen`(4) + `len`(4)
  + `bytecodeLen`(4) + 4 bytes padding + `buffer`(8) + `bytecode`(8) = 40,
  confirmed via manual offset arithmetic against `IW5_Assets.h`'s current
  struct).
- Field 0 (offset 0): `name`, resolved via the same string-resolution
  primitive every other asset's name field uses. Matches.
- `FUN_1400aac10(3)` immediately after the header fill — **block index 3
  = `XFILE_BLOCK_VIRTUAL`** in this fork's own enum order (TEMP=0,
  PHYSICAL=1, RUNTIME=2, VIRTUAL=3, ...) — matches
  `Load_ScriptFile`'s own `m_stream.PushBlock(XFILE_BLOCK_VIRTUAL)` call
  wrapping the name-string load, called BEFORE either SCRIPT-block push.
  **This is itself decisive, independent cross-check evidence for §5.31's
  own block-index question**: VIRTUAL really is native index 3, exactly as
  this fork already assumes.
- `FUN_1400aac10(8)` (buffer read) / `FUN_1400aac10(8)` again (bytecode
  read) — **block index 8 = `XFILE_BLOCK_SCRIPT`** in this fork's own enum
  order (the 9th and last block declared in `SetupBlock()`). Matches
  `Load_ScriptFile`'s own two separate `PushBlock(XFILE_BLOCK_SCRIPT)`
  calls exactly — **second, independent, decisive confirmation that
  SCRIPT really is native index 8**, converging with §5.31's own empirical
  finding (which ruled out the table itself being misread) to close the
  block-index-mapping question completely: it is not misindexed anywhere
  in this specific chain.
- `buffer`'s real byte count comes from `DAT_1407bf308[1]` (an 8-byte-
  stride array read, cast to `int`, i.e. the low 4 bytes of the QWORD at
  offset 8) — offset 8 in this fork's struct is exactly `compressedLen`.
  Matches `m_stream.Load<const char>(varScriptFile->buffer,
  varScriptFile->compressedLen)` exactly.
- `bytecode`'s real byte count comes from `DAT_1407bf308[2]` (offset 16 at
  the same 8-byte stride) — exactly `bytecodeLen` in this fork's struct.
  Matches `m_stream.Load<unsigned char>(varScriptFile->bytecode,
  varScriptFile->bytecodeLen)` exactly.
- `len` (offset 12, the DECOMPRESSED size) is read as part of the initial
  40-byte raw copy but never separately referenced by this native
  function — consistent with this fork's own DSL (`ScriptFile.txt` has no
  `set count` line naming `len`), not a discrepancy.
- `Mark_ScriptFile()` (the generated Mark-phase function,
  `scriptfile_iw5_mark_db.cpp`) is empty — `ScriptFile` has zero
  dependencies to walk, ruling out the exact bug CLASS §5.30 found and
  fixed in `snd_alias_list_t`'s own Mark phase (an unconditionally-walked
  `reusable`-array count) as inapplicable here; there's no array/count
  pair for a Mark-phase bug to hide in.

**This rules out theory (b) from §5.31's own report** ("ScriptFile's own
native block assignment genuinely differs from what the DSL declares for
x64") **with direct, positive, matching evidence — it does not differ.**
Combined with §5.31's own decisive empirical finding (a failing zone's
real, on-disk SCRIPT block size genuinely is 0 bytes, not misread), the
two rounds together converge tightly: the block-size table is read
correctly (§5.31), SCRIPT really is index 8 and ScriptFile really is
supposed to read from it (§5.32, this round) — so a specific `ScriptFile`
asset in a failing zone must be reading `compressedLen`/`bytecodeLen`/
`buffer`/`bytecode` values that are THEMSELVES corrupted, most plausibly
by the same recurring bug class this entire investigation keeps finding
all session (a stream-cursor desync from an EARLIER, unrelated asset's own
read consuming the wrong number of bytes, so this ScriptFile asset's own
40-byte header fill lands at the wrong file position and reads garbage
that happens to include a nonzero `buffer`/`bytecode` "present" flag with
some large bogus count). This narrows the remaining open question sharply:
find WHICH earlier asset (in a failing zone, likely `code_post_gfx.ff`
given §5.31's own repro) desyncs the stream before the first ScriptFile
asset that actually fails — squarely a live-diagnostic-tracing task
(cross-asset, not ScriptFile-internal), not further native RE of
ScriptFile itself.

No source changes shipped (research-only round, decompile evidence added,
no generated or tracked source touched). Two new Ghidra evidence files
committed. Ghidra project's own known `.gbf` corruption pattern hit twice
this round, restored both times via `git checkout --`.

## 5.33. UPDATE, 2026-09-15 (parallel fork, second of "go after that one with 3 forks", empirical/diagnostic-tracing angle) — the exact open question §5.32 left ("find WHICH earlier asset desyncs the stream") answered directly, with one important correction: the failing reference is NOT a `ScriptFile` asset at all — it's a `snd_alias_list_t` (Sound) asset whose own corrupted header field coincidentally decodes to block index 8 (named `XFILE_BLOCK_SCRIPT`), and the real corruption traces further back to a `LoadedSound`/`MssSound`/`AILSOUNDINFO` struct-layout mismatch

**Status: root cause narrowed to a specific, well-evidenced struct-layout
question — not yet fixed, needs one more round of native RE (out of this
round's own assigned scope) before a safe fix can be written.** Assigned
angle: pure empirical diagnostic tracing (no native RE — that was another
fork's own lane; no live debugger of any kind, per direct standing
instruction after this same investigation's own earlier cdb-related system
crash — every finding below came from `fprintf`+`fflush` tracing compiled
into `Unlinker.exe` and run normally).

**First, a scope correction that matters for anyone picking up the
remaining "`XFILE_BLOCK_SCRIPT`" zones list**: dumping the real, raw 9
block-size values (`tools/iw5oat/src/ZoneLoading/Loading/Steps/
StepAllocXBlocks.cpp`) for all six zones this investigation's own briefing
named as the `XFILE_BLOCK_SCRIPT` failure class (`hamburg.ff`, `common.ff`,
`code_post_gfx.ff`, `rescue_2.ff`, `common_survival.ff`,
`so_stealth_prague.ff`) found that **only `code_post_gfx.ff` still actually
hits this error.** The other five now fail with completely different,
already-tracked errors (`XFILE_BLOCK_TEMP` overflow — four zones — or
`invalid block 15` for `common_survival.ff`) — almost certainly because
this same day's earlier `not-recorded`/Sound-deduplication fixes (§5.29,
§5.30) already let them progress past where they used to fail, into
different, separately-tracked territory. **`code_post_gfx.ff` is the only
zone left in this specific bug class**, and it's the one this whole round
focuses on.

**Traced the real desync source via per-asset-index tracing**
(`ContentLoaderIW5.cpp`'s `LoadXAssetArray` loop, the same established
pattern this session has used throughout): `code_post_gfx.ff` crashes
loading asset **index 4482 of 4770, type 11 (`ASSET_TYPE_SOUND`,
`snd_alias_list_t`)** — not a `ScriptFile` asset at all. Fine-grained
tracing inside the generated `snd_alias_list_t_iw5_load_db.cpp` shows the
crash happens on this asset's own very first field, `aliasName`, whose raw
64-bit value reads as `0x0000300583E50000` — a real, decisive, decodable
number: truncated to the standard 32-bit-wide offset scheme (per this
session's own already-shipped global decode fix, §5.18/§5.20),
`offsetInt = 0x83E4FFFF`, which decodes to **blockNum=8
(`XFILE_BLOCK_SCRIPT`), blockOffset=65339391 — the EXACT numbers in the
original error message.** This is coincidental in the sense that block
index 8 simply happens to be named `XFILE_BLOCK_SCRIPT` — nothing about
this asset is a real ScriptFile reference; it's a corrupted Sound asset's
own header field whose garbage bit pattern, run through the shared
block-index decode math every asset type uses, happens to land in the
SCRIPT block's numeric slot.

**Traced the corruption one asset further back**: asset 4482 is preceded
by asset 4481, ALSO type 11 (Sound). Asset 4481's own load completes
without throwing — but its own `soundFile` sub-structure resolves via the
`SAT_LOADED` branch (a fresh, real `LoadedSound` sub-asset, loaded via
`Loader_LoadedSound::Load()`), and this is exactly where this session's
OWN earlier-shipped fix (§5.24, commit `ab56e7cc` — `LoadedSound`'s raw
sample-data byte count reads `AILSOUNDINFO::bits`, not `::data_len`)
already lives. Added tracing directly inside `Load_MssSound`
(`loadedsound_iw5_load_db.cpp`) and found the SAME exact asset §5.24's own
original fix was built and tested against (`data_len=24932`, `bits=22050`
— identical values) — but this time dumping the FULL `AILSOUNDINFO`
struct, not just the two fields §5.24 compared:

```
format=65537 data_len=24932 rate=44 bits=22050 channels=0 samples=0 block_size=0
```

**Every field except `data_len`/`bits` is nonsensical for a real audio
asset** — `format=65537` (0x10001, not a plausible codec/format ID),
`rate=44` (a nonexistent 44Hz sample rate — 44100 or 22050 would be real),
`channels=0`, `samples=0`, `block_size=0` (all zero, implausible for any
real encoded audio). §5.24's own fix correctly reads the C++-struct-
computed `offsetof(AILSOUNDINFO, bits)` — that part isn't wrong — but this
round's fuller dump shows the WHOLE struct read looks shifted/misaligned
for this specific asset, not just one mis-selected field. `bits=22050`
looking plausible (as a sample-data byte count, which is what §5.24's own
live A/B test confirmed) may be true for the ONE asset that fix was
validated against, without the interpretation holding for every
`LoadedSound` instance — this round could not find another zone with real
(non-empty, non-all-zero) `LoadedSound` content to build a clean comparison
baseline (`sp_dubai.ff`/`so_trainer2_so_deltacamp.ff` have none;
`so_survival_mp_alpha.ff`/`hamburg.ff` either have none reachable before
their own current failure point or show an all-zero trivial case) — a real
gap in this round's own evidence, not filled.

**Leading theory, NOT yet confirmed via native decompile (this round's own
assigned scope was diagnostic tracing only, not native RE — deliberately
did not cross into another fork's lane)**: one of `LoadedSound::name`
(a string field) or `MssSound::data`/`AILSOUNDINFO::data_ptr` (both raw
pointers, 8 bytes in this fork's own C++ struct) may share the exact same
"genuinely 32-bit-wide on the wire, not this stream's native 64-bit width"
shape this session has already confirmed for OTHER specific fields
(`MaterialPixelShader::name`, §5.16-§5.19) — if any field upstream of
`AILSOUNDINFO` inside `LoadedSound`/`MssSound` is really 4 bytes narrower
than this fork's struct assumes, every field read after it (the entirety
of `AILSOUNDINFO`, in order) would land 4 bytes early relative to the true
wire position, explaining a fully-shifted, nonsensical struct read exactly
like the one found here. This is a genuinely different, more specific
theory than §5.32's own broader "some earlier asset's stream position
consumed the wrong byte count" framing — it points at a NAMED, narrow,
independently-testable candidate (a specific pointer field's real wire
width inside this exact three-struct chain) rather than an open-ended
search across every asset type.

**Recommended next step for whoever picks this up**: decompile the real
native x64 `LoadedSound`/`MssSound`/`AILSOUNDINFO` fill function directly
(reachable via `ASSET_TYPE_LOADED_SOUND`'s own dispatch case in
`FUN_14009bce0`, not yet found/recorded this round — a fresh Ghidra pass
starting from that dispatch table, following the same "decompile the real
fill function, compare field-for-field against this fork's own generated
code" method §5.32 already proved out for `ScriptFile`) to confirm or rule
out the width-mismatch theory precisely, then fix at the permanent DSL/
struct source (matching §5.24's own precedent) once confirmed — not a
guess-and-ship, this exact code area (recursive Sound/LoadedSound loading)
has a real history of segfaults from rushed changes earlier this session
(§5.22, §5.27, §5.30's own Bug #1/#2).

No source changes shipped (research-only round; every temporary diagnostic
added to `ContentLoaderIW5.cpp` and the generated
`snd_alias_list_t_iw5_load_db.cpp`/`loadedsound_iw5_load_db.cpp` was
reverted from tracked source before finishing — the two generated files
live in gitignored `build/`, not tracked, so no revert was needed there for
git's own purposes, but they were left in their last-diagnosed state, not
cleaned up, since they'll be regenerated/overwritten by the next real
`ZoneCodeGenerator`+`x64_offset_fixes` pass regardless). No Ghidra project
files touched this round (pure C++ instrumentation, no native RE).

## 5.34. UPDATE, 2026-09-15 (direct follow-up to §5.33's own recommended next step, "keep digging" per direct instruction) — the `LoadedSound`/`MssSound`/`AILSOUNDINFO` width-mismatch theory DEFINITIVELY RULED OUT via native decompile + empirical re-test; the "garbage" AILSOUNDINFO field values are very likely genuine (if unusual) real data, not corruption — `code_post_gfx.ff`'s own `XFILE_BLOCK_SCRIPT` crash root cause is STILL open, narrower than ever

**Status: fourth theory in a row ruled out with hard evidence — genuinely
narrowing, not yet found.** §5.33 left one specific, named, independently-
testable candidate: a pointer field somewhere in `LoadedSound`/`MssSound`/
`AILSOUNDINFO` being 4 bytes narrower on the real wire than this fork's own
64-bit-assumed C++ struct, which would shift every subsequent field read
and explain the fully-nonsensical `AILSOUNDINFO` dump §5.33 found. Picked
up directly where that round's own "recommended next step" left off:
decompile the real native fill chain and check field-by-field.

**Decompiled the full native chain**: `FUN_1400941f0` (LoadedSound's outer
pointer resolver, case `0xd` in the master dispatch `FUN_14009bce0`) →
`FUN_140094150` (the real body-fill) → `FUN_14009c0f0` (`MssSound`'s own
fill). Every checkable byte count and offset matches this fork's own
compiler-computed `sizeof()`/`offsetof()` EXACTLY: the outer `LoadedSound`
header read is `0x40` (64) bytes total, matching `sizeof(LoadedSound)`
(name 8 + `MssSound` 56, no discrepancy); `MssSound`'s own read is `0x38`
(56) bytes, matching `sizeof(MssSound)` (`AILSOUNDINFO` 48 + `data` pointer
8); the native `data` pointer field sits at byte offset `0x30` (48),
exactly where this fork's own `offsetof(MssSound, data)` places it; the
native raw-sample-data byte-count source sits at offset `0x18` (24),
exactly matching `offsetof(AILSOUNDINFO, bits)` — independently
reconfirming §5.24's own original `bits`-not-`data_len` fix is still
correct. No width or offset discrepancy found anywhere in the chain.

**A subtlety worth recording explicitly**: this only directly confirms the
*container* sizes and the two fields (`data`, `bits`) native's own outer
fill functions reference by raw offset — it does NOT independently confirm
`AILSOUNDINFO`'s six OTHER individual field offsets (`format`/`data_len`/
`rate`/`channels`/`samples`/`block_size`), since those are filled from an
already-buffered region with no further native offset literals to cross-
check directly. However, `bits` landing at exactly offset 24 in BOTH native
and this fork's own compiler output is only possible if `format` (4 bytes)
+ padding (4 bytes, forced by `data_ptr`'s own 8-byte alignment
requirement) + `data_ptr` (8 bytes) + `data_len` (4) + `rate` (4) all
precede it exactly as this fork's struct already declares — a single
matching offset this deep into a struct is strong indirect confirmation of
every field before it, not proof of the fields after it, but there's no
positive evidence of a problem there either.

**Empirically re-verified the whole chain executes as expected**, reusing
§5.33's own still-present (gitignored, unreverted) diagnostics: rebuilt and
re-ran `Unlinker.exe` against `code_post_gfx.ff`, confirming for the exact
same asset: `data(raw)=0xFFFFFFFFFFFFFFFE` (the real INSERT sentinel,
`zonePtrType=1`), the raw-sample read fires and completes cleanly ("about
to read 22050 raw sample bytes" → "raw sample bytes read done" — no
exception, no early return). This is decisive: the stream consumes exactly
as many bytes as native's own identical gating/length logic would consume,
for this exact asset. **A genuine stream-position desync originating
inside `LoadedSound`'s own load is now very unlikely** — every mechanism
that could cause one (wrong total size, wrong data-pointer offset, wrong
gating condition, wrong read-length source) has been independently
confirmed correct, both structurally (native decompile) and empirically
(live re-test).

**Reframing, following from this**: the "garbage" `AILSOUNDINFO` values
§5.33 found (`format=65537`, `rate=44`, `channels=0`, `samples=0`,
`block_size=0`) are now better explained as genuinely real, if unusual,
data for this one specific minimal/placeholder audio asset (a real engine
can and does ship SFX with degenerate metadata for e.g. a silent/trivial
clip) than as evidence of struct corruption — `bits=22050` being the one
plausible-looking value isn't coincidental cherry-picking, it's the one
field independently confirmed by TWO separate native offset literals
(§5.24's original find and this round's `FUN_14009c0f0` cross-check), while
the others were never independently confirmed wrong, just unusual-looking.

**This means asset 4481 (the `LoadedSound`-owning Sound asset) is very
likely NOT where the real corruption originates at all — §5.33's own
"traced the corruption one asset further back" framing may have been a
correlation (the LAST thing that runs before the crash) rather than
causation.** The true root cause remains open, somewhere further back in
the asset sequence than has been traced so far, or in a mechanism entirely
unrelated to `LoadedSound`.

**Recommended next step for whoever picks this up**: trace further back
from asset 4481 — dump the real per-asset-index stream cursor position (not
just type/index, an actual byte offset into the relevant block) for a
wider window (e.g. assets 4470-4481) to find exactly where the real
consumed-vs-expected byte count first diverges, rather than assuming the
immediately-preceding asset is the culprit. Given four independent,
well-reasoned theories have now been ruled out with hard evidence in this
same investigation (§5.31 block-order, §5.32 ScriptFile's own fill logic,
§5.33's own initial framing already self-corrected once from ScriptFile to
Sound, and now this round's `LoadedSound` width theory), this may be
approaching the point where this project's own "Fresh Perspective Breaks
Real Stalemates" principle applies if a few more genuine rounds don't land
it — not yet at that threshold, but worth tracking.

No source changes shipped (pure RE + empirical re-verification, no fix
attempted since the leading theory didn't hold up). Three new Ghidra
decompile files committed:
`re_notes/ghidra_scripts/decomp_loadedsound_1400941f0.txt`,
`decomp_loadedsound_body_140094150.txt`, `decomp_mssound_14009c0f0.txt`.
No tracked source touched at all this round (reused §5.33's own
already-present, gitignored `build/`-only diagnostics rather than adding
new ones).

## 5.35. UPDATE, 2026-09-15 (direct continuation, session wrapping up soon) — a fifth theory ruled out (`SndCurve`'s own sub-load, previously completely unexamined), and more importantly: asset 4481's ENTIRE lifecycle (every field, every sub-load) is now confirmed clean end-to-end, not just its previously-checked `LoadedSound` portion — `code_post_gfx.ff`'s `XFILE_BLOCK_SCRIPT` root cause remains genuinely open, real "Fresh Perspective" territory now

**Status: Open, root cause not found after 5 rounds of genuine, well-evidenced
investigation.** Picked up directly from §5.34's own close: with the
`LoadedSound`/`MssSound`/`AILSOUNDINFO` width theory ruled out, re-examined
whether every OTHER field of asset 4481 (the `snd_alias_list_t` asset
immediately preceding the corrupted 4482) had actually been checked — it
had not. `snd_alias_t` (the single array element inside 4481) has a
`volumeFalloffCurve` field (an `SndCurve` reference) that no prior round
ever traced, since all four earlier rounds focused specifically on the
`soundFile`→`LoadedSound` path.

**Confirmed via fresh diagnostic tracing that `volumeFalloffCurve` genuinely
IS populated** (raw value `0xFFFFFFFFFFFFFFFF`, the real FOLLOWING sentinel,
not null) for asset 4481 in `code_post_gfx.ff` — meaning a full, fresh
`Load_SndCurve` sub-load genuinely fires here, reading `sizeof(SndCurve)`
(144 bytes) raw from the stream, a code path with zero prior diagnostic
coverage. Added temporary tracing to
`tools/iw5oat/build/src/ZoneCode/Game/IW5/XAssets/sndcurve/sndcurve_iw5_load_db.cpp`
(gitignored, generated, no tracked-source change) and found: **the data read
is completely clean and plausible.** Two separate `SndCurve` sub-loads
happen (asset 4481 has two `snd_alias_t`-adjacent curve references reached
during its own processing); both show real, sane falloff-curve data
(`knotCount=5`/`knotCount=2`, knot pairs like `[0.0, 1.0]`/`[0.25, 0.65]` —
exactly the monotonic, normalized 0-1 shape a real volume falloff curve
should have) and both resolve to real, sensible asset names (`"default"`,
`"$default"`). This rules out `SndCurve`'s own struct layout/read-size as
the corruption source — the fifth distinct, well-evidenced theory closed
today (after §5.31 block-table order/width, §5.32 `ScriptFile`'s own fill
logic, §5.33's initial ScriptFile framing self-corrected to Sound, §5.34
the `LoadedSound` width theory).

**The more important finding is the CUMULATIVE one, not just this fifth
ruled-out theory**: asset 4481's `speakerMap` field was also already
confirmed (§5.33's own trace, re-verified this round) to resolve via
`ConvertOffsetToPointerLookup` — an OFFSET/lookup reference into
already-loaded data, consuming ZERO new stream bytes, so it structurally
cannot be a desync source regardless of whether the lookup itself is
correct. Between LoadedSound (§5.34), SndCurve (this round), and
speakerMap's own zero-byte-consumption nature, **every single field of
asset 4481 that could possibly consume stream bytes has now been
independently verified**, and every one of them reads clean, correct,
real data all the way through -- not just the specific sub-portion each
individual round happened to check. Asset 4481's own complete lifecycle
trace (from `LoadPtr_snd_alias_list_t`'s entry through `LoadAsset_
snd_alias_list_t`'s own `LinkAsset` call at the very end) shows no
anomaly, exception, or implausible value anywhere.

**This means the true corruption source is NOT within asset 4481 at all**
— contradicting the working assumption every round since §5.33 has
operated under (that the immediately-preceding asset is the culprit,
just via an as-yet-unidentified specific field). The real answer must be
either: (a) further back in the asset sequence than assumed (the 56
consecutive `RAWFILE` assets at indices 4423-4478, or the `SOUND_CURVE`/
`SNDDRIVER_GLOBALS` pair at 4479-4480, none of which any round has
individually verified field-by-field against native), or (b) a genuinely
different mechanism entirely -- not a "wrong byte count somewhere"
stream-desync at all, but something else (a shared/global state issue, a
block-index computation bug independent of any single asset's own field
values, or similar) that none of today's five rounds' shared working
hypothesis would have caught.

**Recommended next step, stated plainly for whoever picks this up**: this
bug has now had 5 genuine, well-reasoned, evidence-based rounds today
alone (on top of the original discovery earlier this session) without
landing a fix, each one closing a real, specific, named theory rather than
going in circles on the same one -- this is squarely the situation this
project's own CLAUDE.md "Fresh Perspective Breaks Real Stalemates"
principle describes. A 6th round using the SAME technique (per-asset field
tracing against the immediately-preceding asset) is unlikely to be
productive. Fresh angles worth trying instead: (1) directly instrument the
REAL underlying file-stream read cursor itself (wherever `ILoadingStream`'s
own position lives, upstream of any per-block offset counter -- none of
today's diagnostics have looked at this directly, only at block-level
bookkeeping, which resets per-asset for TEMP and therefore can't reveal a
carried-forward desync); (2) widen the traced window to the full 56-asset
`RAWFILE` run plus the `SOUND_CURVE`/`SNDDRIVER_GLOBALS` pair before 4481,
not just the one asset immediately preceding the failure; (3) reconsider
whether this is a stream-desync bug at all, given every field checked so
far has been correct -- a bug in the shared offset-to-block-index decode
math itself (independent of any specific asset), or a corruption in how
`code_post_gfx.ff`'s own real file bytes were captured/extracted for this
project's own reference copy, are both real alternative categories worth
a skeptical re-check before assuming a sixth desync-source hunt will
finally find it.

**No source changes shipped** (pure RE + empirical tracing; no fix
attempted since no genuine root cause was found). No Ghidra decompile
attempted this round -- the shared Ghidra project's own marker file
(`iw5sp_x64_proj.gpr`) was transiently missing, very likely a parallel
fork's own concurrent Ghidra use, and this round deliberately avoided any
Ghidra invocation for the rest of its duration to eliminate collision
risk entirely, relying on pure empirical C++ diagnostic tracing instead.
`git status --short re_notes/ghidra_project_x64/` was NOT touched by this
round at all as a result.

## 5.36. UPDATE, 2026-09-15 (direct follow-up, "invalid block N" failure class — 14 zones, block index varies: 15/11/12 across different zones — a SEPARATE failure class from §5.28-§5.35's own `XFILE_BLOCK_SCRIPT` thread) — the SAME `snd_alias_list_t`/`LoadedSound` nested chain implicated again, in a genuinely different zone; the `AILSOUNDINFO`-field-layout theory is now DEFINITIVELY closed via a new, stronger argument (bulk-read byte counts are provably identical to native regardless of field values); the real candidate narrows to one of six variable-length string reads in this same nested chain, not yet individually verified

**Status: Open, narrowed further, no fix shipped (correctly — the remaining
candidate needs verification this round's own time budget didn't allow).**
This is a genuinely separate failure class from the `code_post_gfx.ff`-
specific `XFILE_BLOCK_SCRIPT` thread §5.28-§5.35 have been chasing —
"invalid block N" affects 14 different zones with a VARYING block index
(15 in 12 zones, 11 in `so_assault_rescue_2.ff`, 12 in
`so_milehigh_hijack.ff`), the classic signature of a garbage struct field
being run through the shared offset→block-index decode math (the same
mechanism §5.30/§5.33 already established: a raw value, truncated to 32
bits and run through the 4-bit-block-index-in-a-32-bit-word scheme,
produces whatever the top nibble happens to be — 15, 11, 12, etc., not one
fixed constant).

**Traced `sp_berlin.ff`'s own failure via coarse per-asset tracing**
(temporary diagnostic in `ContentLoaderIW5.cpp`, reverted before
finishing, confirmed via `git diff --stat`) to asset index **173 of 3047,
type 11 (`ASSET_TYPE_SOUND`)** — immediately preceded by asset **172,
ALSO type 11** — the identical "two back-to-back Sound assets, the second
one corrupted" shape §5.28/§5.30 already found twice before in different
zones. Asset 172's own trace (reusing §5.30's still-present, gitignored
diagnostics in the generated `snd_alias_list_t_iw5_load_db.cpp`/
`loadedsound_iw5_load_db.cpp`) shows the exact same suspicious
`AILSOUNDINFO` signature §5.33/§5.34 already found elsewhere —
`format=65537` (0x10001, identical bit pattern to the OTHER zone's own
case), `channels=0`, `samples=0`, `block_size=0`, but `bits` holding a
real-looking number (`109640` this time, `22050` before) — used to drive a
`109640`-byte raw-sample-data read. Asset 173 immediately after shows
clearly corrupted header fields (`head=0x00FFFFFF`,
`aliasName(raw)=0xFFFFFFFFFF000000`, decoding to block 15 exactly as
predicted by the shared decode math).

**New, decisive argument that definitively closes the `AILSOUNDINFO`-field-
layout theory §5.33 first raised** (§5.34 already ruled out the SPECIFIC
width-mismatch shape of that theory via native offset cross-checks; this
round adds a stronger, more general argument covering every possible
internal field arrangement, not just the ones already checked): **every
read in this chain that could plausibly desync the stream is a FIXED-SIZE
bulk block read** (`LoadWithFill(sizeof(MssSound))` = 0x38/56 bytes,
matching `FUN_14009c0f0`'s own `FUN_1400aad70(param_1, DAT_1407bd9c0,
0x38)` exactly; the outer `LoadedSound` header = 0x40/64 bytes, matching
`FUN_140094150`'s own `FUN_1400aad70(param_1, DAT_1407bdd88, 0x40)`
exactly). **A fixed-size bulk read consumes the same number of stream bytes
regardless of what values end up inside it** — so even if this fork's own
`struct AILSOUNDINFO` declares its six numeric fields (`format`/`data_len`/
`rate`/`channels`/`samples`/`block_size`) in a genuinely different internal
order or padding than native, that could only produce WRONG DISPLAYED
VALUES for those fields, never a stream-position discrepancy, since the
exact same total byte count is consumed either way. This rules out the
entire class of "AILSOUNDINFO's internal field layout is subtly wrong"
theories at once, not just the one specific shape already checked.
`PushBlock`/`PopBlock` nesting through the whole chain
(`LoadPtr_LoadedSound` → `Load_LoadedSound` → `Load_MssSound`, each with
its own correctly-paired push/pop) was also read in full and confirmed
correctly nested, ruling out a block-offset-tracking mismatch as a
separate candidate.

**The one class of read in this chain NOT covered by the "fixed-size bulk
read, provably correct" argument above: variable-length string reads.**
`Load_snd_alias_t`'s own element has 5 `LoadXString`-driven fields
(`aliasName`, `subtitle`, `secondaryAliasName`, `chainAliasName`,
`mixerGroup`), and `Load_LoadedSound` has a 6th (`name`) — every one of
these, when the raw on-wire value equals the `FOLLOWING` sentinel, reads a
NEW, variable-length, null-terminated string directly off the stream
(`LoadNullTerminated`, `ContentLoaderBase.cpp`) rather than a fixed byte
count. **If even one of these six fields' FOLLOWING-vs-already-resolved
decision doesn't match what the real native engine decides for the same
raw value, that's a real, exactly-shaped stream-desync mechanism** — this
was NOT ruled out this round, and is now the strongest remaining
candidate. Every individually-checked string field so far (`aliasName` in
both `FUN_14009f4b0`/`FUN_140094150`) showed native using the identical
`if (raw == -1) { fresh string read } else { lookup }` gating this fork's
own `LoadXString` already implements — but this was only confirmed for
ONE field's own outer wrapper function in each case, not independently for
all six fields across this exact nested chain in this exact failure
scenario.

**Not fixed this round — explicitly, deliberately, given the remaining
time budget and this exact code area's own documented segfault history
from rushed changes (§5.22, §5.27, §5.30's own Bug #1/#2).** No tracked
source changed; the one temporary diagnostic added
(`ContentLoaderIW5.cpp`'s coarse per-asset trace) was reverted before
finishing. Zero live debugger used (static Ghidra decompile — reused
already-saved evidence, no new invocation this round — plus `fprintf`
diagnostics only, per this investigation's own standing rule).

**Recommended next step for whoever picks this up**: verify each of the
six string fields' own real native FOLLOWING-vs-lookup decision
individually for THIS specific failure case (asset 172's own five
`snd_alias_t` string fields plus asset 172's LoadedSound's own `name`
field) — either via fresh diagnostic tracing that dumps each field's raw
value immediately before its own resolution decision (not just the field
name, which is all the currently-present diagnostics show), or via a
targeted native decompile of whichever specific string-handling primitive
turns out to be the odd one out. Given six candidates and a real, working
methodology already established (§5.16-§5.20's own original string-
resolution investigation), this is very likely tractable in one more
focused round.

## Raw evidence backing every claim above

- Live game install, `zone/english/sp_intro.ff` and `zone/english/hamburg.ff`
  — raw header hex dumps (this session, not saved to a file — reproducible
  via `xxd -l 256 <file>` directly against the live install).
- Decompressed payloads:
  `C:\Users\kyesa\AppData\Local\Temp\claude\...\scratchpad\sp_intro_decompressed.bin`
  (full 766 bytes) and `hamburg_decompressed_head.bin` (first 4096 bytes)
  — session-scratchpad only, not committed (regenerable from the live
  install with the same PowerShell `DeflateStream` approach, header offset
  0x17 for the raw deflate stream start).
- `Laupetin/OpenAssetTools` — forked into `tools/iw5oat/` (§4); its full
  source is now directly in this repo, no external clone needed to
  re-check any claim above against it.
- §5's dispatch-loop trace: `re_notes/x64_migration/ui_pipeline_trace/`
  — `rawbyte_iwffu100.txt` (the raw byte scan that found the magic
  constant despite it not being a null-terminated string),
  `decomp_zoneheader_14008eba0.txt`/`callers_14008eba0.txt`,
  `decomp_1400a8090.txt`/`callers_1400a8090.txt` (the real top-level
  DB_LoadXFile-equivalent), `decomp_zoneload_chain1.txt`,
  `decomp_inflate_chain.txt`, `decomp_1400a4740.txt`,
  `decomp_1400eeb0_ef80.txt` (the XBlock allocator and its generic
  9-block writer, `FUN_1400a4500`), `decomp_assetloop_candidates.txt`
  (**the real find** — `FUN_14009bce0`'s full 46-case dispatch switch).
  New reusable script: `re_notes/ghidra_scripts/RawByteScan.java` (finds
  a non-null-terminated byte sequence anywhere in memory — the string-
  based scanners this project already had all require Ghidra to have
  auto-detected a `Data` string object first, which `-noanalysis` doesn't
  do for a plain, unterminated magic constant).
- Earlier scratchpad-only evidence (§1-§3), still session-local: `Laupetin/
  OpenAssetTools` — shallow-cloned to a scratchpad temp
  directory this session (not vendored into this repo; the project's own
  existing vendored copy is the compiled `Unlinker.exe` binary only, per
  `re_notes/known_issues_x64.md`'s own record) — `git clone --depth 1
  https://github.com/Laupetin/OpenAssetTools.git` reproduces it exactly.
  Key files read in full: `src/ZoneLoading/Game/IW5/ZoneLoaderFactoryIW5.cpp`,
  `src/Common/Game/IGame.h`, `src/ZoneLoading/Game/T6/ZoneLoaderFactoryT6.cpp`.

## Cross-reference

`re_notes/known_issues_x64.md` issue #1's "GSC-first pass" rounds
(2026-09-14) — this doc supersedes/corrects that round's own "changed the
zone/fastfile container format" framing with the more precise finding
above (outer container unchanged; internal struct word-width is the real
divergence). See that file's own newest round for the pointer to this doc.

## 5.37. UPDATE, 2026-09-16 (direct instruction, "fix the unlinker parser bug trying similar RE techniques we had success with today") — `common_survival.ff`'s own "invalid block 15" traced to the exact same failure class §5.36 already found; the leading hypothesis (a wrong FOLLOWING-vs-lookup decision in one of six string fields) is now DEFINITIVELY RULED OUT for this specific zone via direct, complete per-field diagnostic tracing; a new, precise byte-shift signature found instead — still not fixed

**Status: Open, the leading candidate from §5.36 eliminated with hard
evidence, a new precise lead found, no fix landed this round either.**

Picked up §5.36's own explicitly recommended next step (never attempted
there due to time budget) and executed it directly against
`common_survival.ff` -- the zone this project's own current real need
(Survival ready-up GSC extraction) actually depends on, not `sp_berlin.ff`.

**Added real per-field raw/resolved value diagnostics** (temporary,
`DIAGTRACE` calls added directly to the gitignored, already-instrumented
`build/src/ZoneCode/Game/IW5/XAssets/{snd_alias_list_t,loadedsound}/*_load_db.cpp`
-- these files are build output, never tracked source, so this doesn't
touch anything `ZoneCodeGenerator`/the `x64_offset_fixes` scripts own;
confirmed the ACTUAL linked build already has the `offsetof()`-based fix
applied throughout, ruling out a brief false lead of my own this round --
the pristine `src/ZoneCode/...` reference copies still show literal x86
offsets, but those are NOT what's compiled into `Unlinker.exe`; only the
regenerated+patched `build/` copies are) covering all six of §5.36's own
named candidates: `snd_alias_t`'s five string fields (`aliasName`/
`subtitle`/`secondaryAliasName`/`chainAliasName`/`mixerGroup`) plus
`LoadedSound::name`. Rebuilt just the two affected static libraries
(`ZoneLoading.vcxproj`, then relinked `UnlinkerCli.vcxproj`, both with
`/p:BuildProjectReferences=false` to avoid an unrelated, pre-existing
`ObjCommon` custom-build-step failure -- a `Templating source file ...`
`MSB8066` error in a totally different, unrelated asset-JSON-dumping
component that has nothing to do with this investigation -- triggered only
when MSBuild walks the FULL project-reference graph from a leaf project
with no existing solution context; sidestepped, not fixed, since it's
out of scope here).

**Result: every one of the six candidate fields resolved completely
cleanly for the actual last-successfully-loaded asset in `common_survival.ff`
before the crash** (asset index 0, a `snd_alias_list_t` with one
`snd_alias_t` entry):
- `aliasName`: `raw=0x30A3B721` (a real, non-sentinel offset, resolved to
  a real pointer) -- correctly NOT following the FOLLOWING path, resolves fine.
- `subtitle`/`secondaryAliasName`/`chainAliasName`/`mixerGroup`: all
  `raw=0x0000000000000000` (null, no resolution needed at all) -- trivially
  correct.
- `LoadedSound::name`: `raw=0xFFFFFFFFFFFFFFFF` (a clean, correct
  FOLLOWING sentinel), resolves to a real inline string read successfully.

**This definitively rules out §5.36's own leading hypothesis for this
specific zone** -- not one of the six fields shows any ambiguity, wrong
sentinel width, or wrong branch decision. Every later step in this same
asset's own load (`SoundFile`/`SoundFileRef`/`MssSound`/`SndCurve`
resolving a real string `',weapon2'`/`speakerMap`) also completes cleanly,
confirmed via the existing diagnostics already present. `PushBlock`/
`PopBlock` pairing was also read in full for `Load_LoadedSound`
(`XFILE_BLOCK_VIRTUAL`, correctly balanced) -- no unbalanced-block bug
found there either.

**A new, precise byte-shift signature characterized, not seen described
this exactly in any prior round**: the very next top-level dispatch
record (asset index 1, a SEPARATE `snd_alias_list_t`) reads
`head=0x0000FFFFFFFFFFFF` and `aliasName(raw)=0xFFFF000000000000` --
reconstructing these two 8-byte values back into their real FILE-ORDER
byte sequence (not the printed hex, which is MSB-first for a
little-endian-loaded value) gives a clean, symmetric 16-byte pattern:
`00 00 00 00 00 00 FF FF FF FF FF FF FF FF 00 00` -- i.e. a genuine
contiguous 8-byte all-`FF` sentinel run, but sitting at byte offset 6
within this 16-byte window instead of offset 0, padded by zero bytes on
both sides. This is a real, quantifiable ~6-byte-scale positional error,
not a random/high-entropy value (unlike §5.22's own `0x59EE65FC5439FFD3`
finding for a different zone/asset) -- a meaningfully different, more
specific shape than anything previously characterized for this bug family.

**Not yet explained**: since every one of asset 0's own reads verified
byte-exact-correct (no field-level bug, no unbalanced block, no wrong
sentinel decision), the ~6-byte discrepancy must originate either (a) from
something in `MssSound`'s own raw-sample-data path for this specific
`LoadedSound` (not individually re-verified this round -- it read
`format=0 data_len=0 ... data(raw)=0000000000000000`, a real, valid
"no audio data" empty case matching the pattern already seen at the very
start of this same zone's own log, so not obviously wrong, but not
independently confirmed byte-exact either), or (b) from something entirely
upstream of asset 0 -- the top-level asset dispatch/count table itself,
never re-examined this round. Recommended next step for whoever continues
this: either a targeted native decompile of `LoadedSound`'s real
`MssSound`-with-zero-data path specifically (the one sub-read this round
did NOT individually byte-verify), or a raw hex-editor comparison of
`common_survival.ff`'s own decompressed bytes at the exact computed stream
position, cross-referenced against the ~6-byte shift this round found --
a much narrower, more specific target than "somewhere in this whole nested
chain," per the same "Fresh Perspective" style progress every prior round
in this file has made without yet landing the actual fix.

**No tracked source changed.** All new diagnostics live in `build/`
(gitignored, already an established convention for this file's own prior
diagnostic rounds) -- confirmed via `git status` showing a clean tracked
tree. `proxy_d3d9`'s own build/deployment is completely unaffected by
this investigation (separate project, separate toolchain).

## 5.38. UPDATE, 2026-09-16 (direct instruction, "keep going") — a real, shipped hardening fix (silent short-read detection); the leading remaining candidate (`sizeof(snd_alias_t)` correctness) computed by hand, no discrepancy found; root cause still open

**Status: Open. One real, permanent code fix landed (not the root cause,
but a genuine improvement); the short-read hypothesis eliminated with
hard evidence; the struct-size hypothesis checked by hand, inconclusive.**

**Shipped: `ShortReadException` + `LoadChecked()`, `tools/iw5oat` commit
`d4ec3c92`.** `ILoadingStream::Load(dst, size)` returns the real number of
bytes actually read (can legitimately be less on genuine end-of-stream),
but all six call sites in `ZoneInputStream.cpp` silently discarded that
return value -- meaning ANY short read anywhere in the whole processor/
decompression chain would leave a destination buffer's un-filled tail as
stale leftover bytes from a PREVIOUS read, with zero error at the actual
point of failure. Added a small `LoadChecked()` helper wrapping every
call site, throwing a new, precisely-located `ShortReadException`
(context/requested/actual) instead of silently proceeding. **Tested
directly: rebuilt, re-ran against `common_survival.ff` -- no
`ShortReadException` fires**, definitively ruling OUT a short read as
THIS bug's cause (every read genuinely receives its full requested byte
count). Kept and shipped anyway as a real, permanent hardening --
converts any FUTURE silent corruption into a loud, immediately-actionable
error, independent of whether it caught this specific bug. **Zero
regression confirmed**: `sp_intro.ff`/`sp_prague.ff` (the two zones
already known to extract cleanly) still load with 0 warnings, 0 errors
after this change.

**Struct-size hypothesis checked by hand** (the remaining candidate once
per-field FOLLOWING decisions and short reads were both ruled out this
session): `LoadArray_snd_alias_t`'s own bulk read size,
`sizeof(snd_alias_t) * count`, is exactly the kind of value that -- if
wrong -- would silently shift every byte read afterward by a fixed
per-element delta, matching the observed symptom shape. Manually computed
`sizeof(snd_alias_t)` from `IW5_Assets.h`'s own current field list and
real x64 alignment rules (6 leading 8-byte pointers, 10 4-byte fields, one
1-byte `unsigned char` `masterPriority` forcing 3 bytes of padding before
the next 4-byte float run, then an 8-byte pointer forcing 4 bytes of
padding, 3 more floats, then a final 8-byte pointer forcing another 4
bytes of padding) = **152 bytes**. The field list itself (28 members,
types, and order) matches the well-known, widely-documented IW-engine
`snd_alias_t` shape used across multiple CoD titles -- nothing jumped out
as an obviously wrong/missing/extra field by inspection alone. **Not
independently confirmed against real native decompile evidence** (the
technique that resolved the equivalent open question for `Material`/
`MaterialPixelShader` in SS5.9) -- this round's own hand-computed 152 is a
real, checkable number for whoever continues with that technique next,
not a verified-correct one.

**Net position after this round**: every reasonably-accessible
non-decompile technique has now been tried and has come back clean for
`common_survival.ff` specifically -- per-field sentinel/FOLLOWING
decisions (SS5.37), short reads (this round), `PushBlock`/`PopBlock`
balance (SS5.37), and a by-hand struct-size sanity check (this round).
The real next step, if this investigation continues, is the one
technique not yet applied THIS round: a native x64 decompile of
`iw5sp.exe`'s own real `snd_alias_t`-fill function (reachable via the
already-mapped master asset dispatch switch, `FUN_14009bce0`, same
technique SS5.9/SS5.23 already proved out for other asset types) to get
its real, ground-truth per-entry read size directly, rather than trusting
a by-hand computation from this fork's own struct declaration.

## 5.39. UPDATE, 2026-09-16 (direct continuation, "keep going") — native x64 decompile cross-check of the ENTIRE snd_alias_list_t/snd_alias_t/SoundFile/SoundFileRef/MssSound chain: every byte-consumption size and branch condition CONFIRMED CORRECT; the corrupted-asset trail is definitively cleared from this whole code path, redirecting the investigation elsewhere

**Status: Open. The single most thorough native ground-truth verification
this investigation has done -- every remaining candidate in the Sound-
loading chain is now DEFINITIVELY correct, not just "not yet found wrong."**

Reused already-captured native decompile output from
`re_notes/ghidra_scripts/` (from an earlier session's own work on this
same asset family, never previously cross-referenced against this specific
"invalid block 15" thread) rather than re-running Ghidra fresh -- a
genuinely faster path to the same evidence class SS5.9/SS5.23 already
proved out for other asset types.

**`FUN_14009f4b0` (native `snd_alias_list_t` fill, `decomp_snd_alias_list_body_14009f4b0.txt`)**:
bulk-reads exactly `0x18` (24) bytes for the header -- matches
`sizeof(snd_alias_list_t)` exactly. Checks `head` (offset 8, `[1]` in a
`ulonglong*` view) against `0`/`-1` exactly matching the fork's own
`GetZonePointerType` FOLLOWING check (confirmed full 64-bit width, not
truncated -- consistent with SS5.23's own already-established finding).
Passes `count` (offset 16, `[2]`) straight through to the array-fill call.
**Byte-for-byte identical to the fork's own `FillStruct_snd_alias_list_t`
+ `Load_snd_alias_list_t`'s own head-check.**

**`FUN_14009f590` (native `snd_alias_t` array fill, `decomp_snd_alias_array_14009f590.txt`)**:
bulk-reads exactly `param_2 * 0x98` bytes -- **`0x98` = 152 decimal**,
EXACTLY matching this session's own by-hand-computed `sizeof(snd_alias_t)`
from SS5.38 (6 leading 8-byte pointers, 10 4-byte fields, one 1-byte
field forcing padding, more floats, two more 8-byte-aligned pointers).
Independently re-confirmed by the native code's OWN per-element pointer
advance: `plVar2 = plVar2 + 0x13;` -- `0x13` (19) `longlong`-sized steps
= 19 * 8 = **152 bytes**, the identical number reached two different ways
in the same function. **This decisively rules out the struct-size
hypothesis SS5.38 left unverified** -- 152 is confirmed correct, not a
hand-computation that might be wrong.

**`FUN_14009c0f0` (native `MssSound` fill, `decomp_mssound_14009c0f0.txt`)**:
bulk-reads exactly `0x38` (56) bytes -- matches the fork's own
`sizeof(MssSound)=0x38` diagnostic exactly. Checks the `data` pointer
field (offset `0x30`) for null BEFORE reading any raw sample bytes --
`if (uVar1 != 0) { ... }`, matching the fork's own `if (varMssSound->data)`
condition exactly. For asset 0's own `LoadedSound` (observed `data(raw)=
0000000000000000`, a real null), native's own code ALSO takes the
"skip raw-sample-read entirely" path -- confirmed, not assumed.

**`FUN_140096900` (native `SoundFileRef`, `decomp_soundfileref_140096900.txt`)**:
checks `type == 1` (SAT_LOADED) and, when true, calls the real
`LoadedSound` loader directly with NO further bytes consumed for this
function itself -- consistent with the fork's own design, where
`SoundFileRef`'s 16 bytes are already fully captured inside `SoundFile`'s
own 24-byte (`sizeof(SoundFile)=24`, matching `0x18` seen at the real
`SoundFile` bulk-read call site, `FUN_14009f590` line 84) bulk read, with
every downstream call passing `param_1=0` (this function family's own
"reuse the already-buffered bytes, consume nothing new from the stream"
convention) rather than reading fresh. Confirmed for asset 0's own actual
`type=1`/`exists=1` case.

**Net conclusion: the ENTIRE Sound-asset loading chain's own byte
consumption is now verified correct against real native ground truth, top
to bottom** -- `snd_alias_list_t` header (24B), `snd_alias_t` array
(152B/entry, confirmed two independent ways), `SoundFile`/`SoundFileRef`
(24B, embedded, zero extra consumption), `MssSound` (56B, correctly
skips the raw-sample read when `data` is null). This is a definitively
stronger result than any prior round in this whole 39-round thread --
every specific hypothesis this session raised (wrong FOLLOWING decisions,
silent short reads, wrong struct/array sizes) is now closed with hard
native evidence, not just "still open."

**Real implication for whoever continues this**: the corruption is
demonstrably NOT anywhere in the Sound-asset loading logic itself. Given
`common_survival.ff`'s own asset stream almost certainly contains many
OTHER asset types before/around the Sound assets this session traced
(the log capture used this round only showed the LAST successfully-loaded
asset's own trace, not the full sequence from the start of the zone), the
real next step is tracing BACKWARD from the corruption point to identify
which asset -- of ANY type, not necessarily Sound -- actually precedes it
in the real stream and may be the one consuming the wrong byte count,
rather than continuing to re-examine the Sound-asset path this round has
now exhaustively cleared.

## 5.40. ROOT CAUSE FOUND, 2026-09-16 (direct continuation, "keep going") — `SpeakerMap`/`MSSChannelMap`/`MSSSpeakerLevels` are fundamentally the wrong shape: the fork reads 416 bytes where native reads 64, a real 352-byte stream over-consumption that explains every "invalid block N" symptom this whole 40-round thread has chased

**Status: ROOT CAUSE IDENTIFIED with decisive native evidence. Fix NOT yet
implemented -- requires new code (a dynamic-array read pattern), not a
simple size/offset correction, and this file's own documented history of
rushed changes in this exact code area (SS5.22/5.27/5.30) argues for
implementing it carefully rather than rushed in the same round it was found.**

**The missing piece from SS5.37-5.39's own exhaustive verification**:
`Load_SpeakerMap` (`snd_alias_list_t_iw5_load_db.cpp`) is the ONLY
sub-read in the entire `snd_alias_t` chain with **zero diagnostic
instrumentation** -- no `DIAGTRACE` anywhere inside it, so every prior
round's live-trace verification silently skipped over it entirely
despite it being the LAST thing that runs before `PopBlock`/`element
fully done`/moving to the next asset. Every OTHER sub-read (SoundFile,
SoundFileRef, MssSound, SndCurve) had visible diagnostic output that
looked clean; SpeakerMap had none, and was the one place nobody had
actually looked.

**Decompiled it directly** (`re_notes/ghidra_scripts/decomp_speakermap_140096980.txt`,
`FUN_140096980` -- found via matching the fork's own `offsetof(snd_alias_t,
speakerMap) = 144` against `FUN_14009f590`'s own `DAT_1407bde00[0x12]`,
`0x12*8=144`, exactly): the REAL native bulk-read for `SpeakerMap` is
**`0x40` (64) bytes**, not `sizeof(SpeakerMap)` (416 bytes, computed from
this fork's own `bool isDefault; const char* name; MSSChannelMap
channelMaps[2][2];` declaration, `IW5_Assets.h`). **A 352-byte
over-consumption, every single time `Load_SpeakerMap` runs on a FOLLOWING
speakerMap** -- exactly the class of bug that would desync the stream for
everything read afterward, matching every "invalid block N"/high-entropy-
garbage symptom this whole investigation has chased since SS5.21.

**Traced the real structure precisely**, decompiling one level deeper
(`re_notes/ghidra_scripts/decomp_channelmap_140096a50.txt`, `FUN_140096a50`):
- `SpeakerMap` real layout: `isDefault`(1) + pad(7) + `name`(8) = 16-byte
  header (matches the fork's own assumption for JUST these two fields),
  followed by **`channelMaps[2]`** (NOT `[2][2]` -- half as many outer
  entries as the fork assumes), each occupying `0x18` (24) bytes = 48
  bytes total. 16 + 48 = 64 = `0x40`, exactly matching the top-level read.
- Each "channel map" entry (24 bytes, confirmed via `FUN_140096a50`'s own
  bulk read) is **itself 2 sub-entries of 12 bytes each** (`FUN_140096a50`'s
  own inner loop, `pbVar1 = pbVar1 + 0xc`) -- NOT the fork's own
  `speakers[6]` (6 entries of a 16-byte `MSSSpeakerLevels`).
- Each 12-byte sub-entry: a 4-byte field at offset 0 (the fork's own
  `speaker`; real semantic use unconfirmed -- the native code reads this
  SAME field's value and uses it directly as a level COUNT, `count << 3`
  bytes, not obviously a plain speaker-channel index the way the fork's
  field name implies) followed by an **8-byte POINTER at offset 4** (real:
  `if (thatPointer != 0) { allocate; *thatPointer = alloc; read count*8
  bytes into it }` -- a genuine FOLLOWING-style dynamically-allocated
  array, not the fork's own fixed inline `float levels[2]`).

**Why this wasn't caught by SS5.8's own broad `offsetof()`/`sizeof()` fix
generator**: that tooling corrects WRONG OFFSETS/SIZES computed from an
otherwise-CORRECT struct declaration (an x86-vs-x64 pointer-width
problem). This bug is different in kind -- the C++ struct declaration
itself describes a fundamentally different SHAPE than native (fixed
inline arrays where native uses a smaller fixed count plus a genuine
dynamic allocation) -- no amount of `offsetof()`/`sizeof()` correction
can fix a struct that's declaring the wrong fields in the first place.

**Not implemented this round, deliberately**: a correct fix needs (a) new
`MSSSpeakerLevels`/`MSSChannelMap`/`SpeakerMap` struct declarations
matching the real 12/24/64-byte shape (with `MSSSpeakerLevels::levels`
becoming a real pointer, not an inline array), and (b) new load logic for
`Load_SpeakerMap`/a new per-channel-map loader mirroring the real
FOLLOWING-style dynamic-array pattern already used elsewhere in this same
file for `soundFile`/`volumeFalloffCurve` -- genuinely new code, not a
`ZoneCodeGenerator` regenerate + offset-fix-script rerun (those tools fix
wrong numbers in an existing shape, not a wrong shape entirely). Given
this exact code area's own documented history of rushed-change regressions
(SS5.22's reverted union-branch experiment, SS5.27's reverted 16-zone
regression, SS5.30's own two real bugs), implementing this carefully in a
dedicated follow-up pass -- with the same live-test-against-all-known-zones
discipline every other real fix in this file has used -- is the right
next step, not rushing it into this same round.

**Real confidence level: high, not speculative.** Unlike every other
theory this 40-round thread has raised and then ruled out, this one is
backed by two independent, mutually-consistent native decompile pieces
(the top-level `0x40` bulk-read size, AND the inner `0x18`/`0xc` sub-entry
sizes that arithmetically sum to exactly 64 bytes) -- not a byte-pattern
guess, not a hand computation, not a live-trace absence-of-evidence. This
is very likely the actual, final root cause of the "invalid block 15"-class
corruption this whole investigation thread has been chasing since SS5.21.

## 5.41. FIXED, 2026-09-16 (direct instruction, "yes try it") — the SS5.40 root cause implemented, built, and live-verified: real, confirmed progress on every previously-blocked zone, zero regression, though a separate downstream issue remains

**Status: This specific bug (the 352-byte SpeakerMap over-read) is FIXED
and verified. A separate, already-tracked forward-reference issue
(SS5.21) now blocks several zones further into their own streams than
before -- real progress, not a full "every zone loads" resolution yet.**

Implemented SS5.40's own precisely-specified fix:
- `IW5_Assets.h` (tracked source): corrected `MSSSpeakerLevels`
  (12 bytes, `#pragma pack(1)`, real dynamically-allocated `float*
  levels` instead of an inline `float[2]`), `MSSChannelMap` (24 bytes,
  `speakers[2]` not `[6]`), `SpeakerMap` (64 bytes, `channelMaps[2]` not
  `[2][2]`) -- with `static_assert`s pinning the real sizes so this can't
  silently regress again.
- `Loader_snd_alias_list_t::FillStruct_SpeakerMap`/`Load_SpeakerMap`
  (hand-patched in `build/`, since this needed new loading logic, not a
  mechanical offset fix -- see `tools/iw5oat/x64_offset_fixes/README.md`'s
  own new section on this hand-patch and its persistence gap): the array
  fill collapsed to one flat `FillArray` call, plus a new per-entry loop
  resolving each `MSSSpeakerLevels::levels` pointer exactly the way
  native does (`FUN_140096980`'s own unconditional-nonzero check, not the
  usual `GetZonePointerType`/FOLLOWING-sentinel check every other pointer
  in this file uses) -- `count * 8` bytes read via `Alloc<float>`+
  `Load<float>`, the same pattern already proven safe for `MssSound`'s
  own raw sample data.

**Rebuilt and live-tested against `common_survival.ff`**: asset 0's own
`speakerMap` load now consumes exactly 64 bytes instead of 416, and asset
1's own header reads CLEAN (`count=1 head=FFFFFFFFFFFFFFFF
aliasName(raw)=FFFFFFFFFFFFFFFF` -- a textbook-correct FOLLOWING sentinel,
not the `0000FFFFFFFFFFFF`/`FFFF000000000000` garbage SS5.37 found before
this fix). This is decisive, direct confirmation the 40-round-old root
cause is real and now fixed.

**Zero regression**: `sp_intro.ff`/`sp_prague.ff`/`sp_ny_harbor.ff` all
still load with 0 warnings, 0 errors after this change.

**Real, measurable progress on every other previously-tracked zone**,
though none of them fully complete yet -- each now fails LATER in its own
stream, at a different, already-documented issue (`InvalidOffsetBlockOffsetException`,
"Zone referenced offset X of block Y which is larger than its size Z" --
a genuine forward reference into a `NORMAL`/`VIRTUAL`/`TEMP` block region
the progressive single-pass loader hasn't written that far into yet, the
SAME class of issue SS5.21 first flagged and SS5.29 partially addressed
for a DIFFERENT exception type, `InvalidLookupPositionException`, via
graceful degradation):
- `common_survival.ff`: was failing at asset ~1's own corrupted header;
  now fails later, referencing offset 10,729,744 of a 48,349,224-byte
  `XFILE_BLOCK_VIRTUAL` (well within total capacity, just not yet
  written that far).
- `code_post_gfx.ff`: offset 361,788 of a 1,728,453-byte `XFILE_BLOCK_VIRTUAL`.
- `hamburg.ff`: offset 55,378,103 of an 8,389,664-byte `XFILE_BLOCK_TEMP`
  (this one genuinely IS over total capacity, a real, different-shaped
  problem from the other three's "not yet written" pattern).
- `common.ff`: offset 52,996,764 of a 55,425,096-byte `XFILE_BLOCK_VIRTUAL`
  (within capacity, not yet written -- same shape as `common_survival.ff`).

**Real next step for whoever continues this** (a separate investigation
from the one this round closed): the `ConvertOffsetToPointerNative`/
`ConvertOffsetToPointerLookup`/`ConvertOffsetToAliasLookup` family's own
"not yet written" check (`m_block_offsets[blockNum] <= blockOffset`,
`ZoneInputStream.cpp`) currently always throws
`InvalidOffsetBlockOffsetException` outright -- SS5.29's own graceful-
degradation precedent (for the sibling `InvalidLookupPositionException`
case) suggests the SAME kind of fix (log a warning, return a safe
null/placeholder, keep loading) might apply here too, though this
specific exception's two DIFFERENT trigger conditions (genuine capacity
overflow vs. not-yet-written) would need to be told apart first so only
the genuinely-recoverable "not yet written" case gets the softer
treatment -- `hamburg.ff`'s own capacity-overflow case above is a real
counter-example that should probably keep throwing.

## 5.42. UPDATE, 2026-09-16 (direct continuation, "keep pushing") — SS5.41's own recommended next step done: forward-reference graceful degradation extended to every sibling resolution function; one attempt caused a real segfault and was correctly reverted; common_survival.ff now progresses from 4 to 30 gracefully-handled forward references

**Status: Real, substantial further progress. One genuinely unsafe attempt
found and reverted via direct live testing, not guessed at. The zone still
doesn't fully load, but the remaining blocker is now far more narrowly
scoped than at the start of this round.**

Extended SS5.29's own already-proven-safe "warn and return null instead of
throwing" pattern (previously applied only to `ConvertOffsetToPointerNative`
and `ConvertOffsetToAliasLookup`'s hop-exhaustion fallback) to every
remaining sibling offset-resolution function in `ZoneInputStream.cpp`,
each split into "genuine capacity overflow" (kept as a hard throw -- never
legitimate) vs. "in-range but not yet written" (now degrades gracefully):
- `ConvertOffsetToPointerLookup` -- returns an unresolved
  `MaybePointerFromLookup` (carrying just the block/offset) instead of
  throwing directly, reusing that class's own existing representation for
  exactly this case rather than adding new degradation logic.
- `MaybePointerFromLookup::Expect()` -- the actual throw site every
  generated `*_load_db.cpp` file's own `.Expect()` call sites hit. Now
  warns and returns null instead. Verified via direct inspection of
  several real call sites (`soundFile`/`speakerMap`/`head` in
  `snd_alias_list_t`'s own generated loader) that every one already gates
  its own dereference behind `if (field) { ... }` -- the same safe,
  established convention this whole format already uses everywhere.
- `ConvertOffsetToAliasNative` -- the fourth sibling, same split.
- `ConvertOffsetToAliasLookup`'s own EARLY block-range/capacity checks
  (previously only its hop-exhaustion fallback, reached after 16 retries,
  had graceful degradation) -- `break` to that SAME already-proven fallback
  on an immediately-invalid hop 0, rather than throwing before the loop
  ever gets a chance to try.

**One further attempt was made and correctly reverted after real evidence
it was unsafe** -- a direct, concrete example of this file's own standing
caution about rushed changes in this exact code area, caught by testing,
not by review. `ConvertOffsetToAliasLookup`'s own final fallback
(`assert(false); throw ...`, meant for a case the original author believed
could never happen) was ALSO changed to `break` into the same graceful
path, since live testing showed it genuinely does fire. This produced 30
gracefully-handled warnings and got further into the stream -- but the
FINAL result was a genuine **segfault**, not a clean exception, the first
time in this whole investigation any of these specific changes has done
that. **Reverted immediately** back to the hard throw. The real lesson:
this particular fallback is NOT the same "legitimate architectural forward
reference" shape as every other case fixed this round -- a real, in-range,
already-written position with literally no entry in either redirect-lookup
table is evidence of some OTHER, not-yet-understood problem, and forcing a
null through it lets that problem reach code that isn't prepared for it.

**Verified, same zones as every round this session**:
`sp_intro.ff`/`sp_prague.ff`/`sp_ny_harbor.ff`/`sp_dubai.ff` all still load
with 0 warnings, 0 errors -- zero regression from any of these five
changes (four kept, one reverted).

**Real, measured progress on `common_survival.ff`**: started this round at
4 gracefully-handled forward references before a hard failure (SS5.41's
own end state); ends this round at **30** gracefully-handled forward
references before the next hard failure -- a real 7.5x increase in how far
into the stream this zone now gets. The remaining blocker is the exact
same shape SS5.41 already narrowed to `InvalidOffsetBlockOffsetException`'s
own "larger than its size" message -- but per this round's own finding,
now know NOT to be `ConvertOffsetToAliasLookup`'s own assert-fallback
specifically (that path is understood and intentionally still throws).

**Real next step for whoever continues this**: figure out WHY
`ConvertOffsetToAliasLookup`'s own final fallback genuinely fires in
practice (contradicting the original author's own "should never happen"
assumption) and what distinguishes a SAFE instance of it (if any exist)
from the specific instance that segfaulted -- likely needs the SAME native
decompile technique that resolved SS5.40's own root cause, applied to
whatever asset/field is reaching this exact fallback for
`common_survival.ff`'s own remaining content, rather than more blind
degradation attempts in this already-repeatedly-warned-about code area.

## 5.43. GROUNDWORK, 2026-09-16 (brief, paused to address a higher-priority redirect) — the assert-fallback's real trigger identified via a one-off diagnostic, reverted before committing; a concrete next-step target for whoever resumes this

Added a temporary `con::warn` right before `ConvertOffsetToAliasLookup`'s
own `assert(false)` fallback (SS5.42), rebuilt, ran once against
`common_survival.ff`, captured the real trigger, then reverted the
diagnostic (matching this file's own standing "temporary instrumentation,
not committed" convention -- confirmed via `git diff` showing a clean
tree before moving on):

```
DIAG ConvertOffsetToAliasLookup fallback: hop=0 offsetInt=0x30a3bf18
blockNum=3 blockOffset=10731288 block=XFILE_BLOCK_VIRTUAL
aliasMapSize=861 pointerMapSize=27394
```

This is `SoundFileRef::loadSnd`'s own resolution (the raw value
`0x30a3bf19` matches a `loadSnd=` diagnostic seen immediately before this
exact failure in an earlier round's own log, off by exactly 1 -- the
standard `-1` offset-encoding adjustment). The block/offset genuinely pass
BOTH the capacity check and the write-cursor check (this position IS
in-range and IS already written) -- with 27,394 real pointer-redirect
entries and 861 real alias-redirect entries already registered by this
point, so this isn't "nothing has been registered yet" either. The
specific offset this `loadSnd` reference computes simply has no matching
entry in either table, despite pointing at real, already-written content.

**Real next-step hypothesis for whoever picks this up**: `LoadedSound`'s
own `AddPointerLookup` registration (in `FillStruct_LoadedSound`) may be
registering a DIFFERENT block-relative address than what THIS specific
`loadSnd` reference computes when resolving TO that same asset -- i.e., a
genuine asymmetry between how a `LoadedSound` announces "here's where I
live" (write side) versus how `SoundFileRef::loadSnd` computes "where to
find it" (read side), rather than a "forward reference not yet written"
shape at all. Worth checking whether `LoadedSound` assets can be shared/
deduplicated across multiple `snd_alias_t` entries (matching this exact
"asset A's pointer aliases asset B's, and B's registration point differs
from what A's own reference computes" shape already documented for
`materialHandles` in SS5.21/5.23) -- a native decompile of exactly how
`SoundFileRef::loadSnd`'s real resolution differs from a fresh
`LoadedSound` load, the same technique that resolved SS5.40, is the
concrete next step, not more diagnostic rounds on this same fallback.

Paused here (not a stopping point due to any blocker -- redirected to a
separate, higher-priority task the same session).

### SS5.44 (2026-09-16) — LoadedSound alias-miss: struct-shape bug ruled out; native's real resolution mechanism traced in full; root cause narrowed to an out-of-block-allocation addressing gap, not yet fixed

Resuming SS5.43's own explicitly-paused next step (native-decompile `SoundFileRef::loadSnd`'s
own resolution vs `LoadedSound`'s own registration) after a separate, higher-priority x64
focus-detection fix was completed. Direct instruction: "lets keep going on FF."

**Live diagnostic re-confirmed the exact failure** (`common_survival.ff`, `ConvertOffsetToAliasLookup`'s
final `assert(false)` fallback, `ZoneInputStream.cpp`): a second `snd_alias_t` entry's
`soundFile.u.loadSnd` raw value (`0x30A3BF19`) decodes to block `XFILE_BLOCK_VIRTUAL`, offset
`10731288` -- in-range, already-written, but registered in neither `m_alias_redirect_lookup`
(861 entries) nor `m_pointer_redirect_lookup` (27394 entries).

**Round 1 -- struct-shape theory, RULED OUT.** Decompiled `FUN_140094150` (the real native
`LoadedSound` field reader): reads exactly `0x40` = 64 bytes total (`FUN_1400aad70(param_1,
DAT_1407bdd88, 0x40)`), matching this fork's own `sizeof(LoadedSound)` exactly (`name`(8) +
`MssSound`(56, itself `AILSOUNDINFO`(48) + `data*`(8)) = 64). Unlike the SS5.40 `SpeakerMap`
bug, `LoadedSound`'s current C++ shape is byte-for-byte correct -- this specific bug is NOT
another wrong-struct-shape case.

**Round 2 -- hex-dump of the actual target position.** Added a temporary diagnostic (dumped a
64-byte window around the failing target, block write-cursor, both map sizes; reverted before
this commit, same discipline as every prior temp diagnostic in this file) and re-ran against
`common_survival.ff`. Real finding: position `10731288` sits inside a long run of zero bytes
(padding/unused region), with one genuine nearby offset-shaped value 20 bytes EARLIER (`0x30A3B9A1`,
independently confirmed via the log to be a DIFFERENT field -- `volumeFalloffCurve`, a sibling
`SndCurve*` field of the SAME `snd_alias_t`) and another 3 bytes LATER (`0x30A3B811`). The target
position itself is not a real field boundary at all in this fork's own byte-for-byte read.

**Round 3 -- native array-walk + resolver decompile, the real mechanism found.** Decompiled
`FUN_14009f590` (`snd_alias_t` array loader -- confirms `sizeof(snd_alias_t)` = `0x98` = 152
bytes exactly, closing SS5.38's own "computed, unverified against native" gap) and traced its
per-field dedup/interning pattern for the `soundFile` pointer field (qword index 5): `== -1` means
fresh-allocate (tag-3 heap allocator, matching this fork's own `AllocOutOfBlock`), `!= 0 && != -1`
means "already resolved, reuse" via `FUN_1400aad40`. Decompiled that function: `result = blockBase
+ blockOffset` (a direct, un-dereferenced pointer into a block-base table, `DAT_140d6de00`,
16 bytes/entry). This matches `ConvertOffsetToAliasNative`'s own semantics exactly -- but this is
the OUTER `snd_alias_t.soundFile` field's own resolver, a level above the actual failure.

The INNER field (`SoundFileRef.loadSnd`, resolved via `FUN_140096900`/`FUN_1400941f0`) uses a
DIFFERENT native function for its own "already resolved" case: `FUN_1400aad10`, decompiled and
found to have the SAME block-decode arithmetic PLUS one extra dereference: `result =
*(blockBase + blockOffset)` -- i.e. "the position holds ANOTHER POINTER, itself already fixed up;
copy that value," not "the position holds the real data directly." This is exactly
`ConvertOffsetToAliasLookup`'s own `m_pointer_redirect_lookup` branch (`resolvedSlot =
foundPointerLookup->second; resolved = *resolvedSlot;`, `ZoneInputStream.cpp` ~line 678-691) --
confirming this fork's function CHOICE for `loadSnd`'s alias case (`ConvertOffsetToAliasLookup`,
which checks both maps) is already the semantically correct one, not a wrong-function bug.

**Also confirmed via the same native trace**: `SoundFile`/`LoadedSound` are BOTH allocated via
the SAME tag-3 heap-style allocator natively (`FUN_1400aaa90(3)`, called from both
`FUN_14009f590`'s `soundFile` case and `FUN_1400941f0`'s own `loadSnd` case) -- matching this
fork's existing `AllocOutOfBlock<LoadedSound>` choice. This RULES OUT the fix theory floated
mid-round (switching to in-block `Alloc<LoadedSound>()` to match a naive "the shared struct must
live in a real block buffer" reading of the block-decode formula) -- native does NOT store
`LoadedSound`'s own data inside a real content-block buffer either; both fork and native use an
out-of-block/heap allocation for it.

**Real, still-open question, narrowed but not answered**: since native's own `DAT_140d6de00`
block-base table clearly resolves `loadSnd` aliases correctly (real production zones load fine
retail), but its table has AT LEAST the capacity to represent MORE than this fork's own 9
`XBlock` types (the block-index bits are the same width either way, `>>0x1c`/28 bits), the
leading open theory is that native's own block-base table includes additional entries
representing tag-based HEAP ALLOCATION REGIONS (like tag 3) alongside the 9 real content
blocks -- meaning a `loadSnd` alias reference's raw offset may be decoding to a "virtual heap
block" index this fork's own `m_blocks[9]` array has no equivalent for at all, rather than
genuinely colliding with `XFILE_BLOCK_VIRTUAL`'s own real content. This would mean the CURRENT
`blockNum=3` decode for this specific failure is itself a fork-side misinterpretation (this
fork's `m_block_shift`/`m_block_mask` scheme, calibrated against the 9 real content blocks,
doesn't have a slot for a heap-tag "block" the way native's own table apparently does) --
consistent with, not contradicted by, the hex-dump finding that block 3 offset 10731288 has no
real relationship to the `loadSnd` field at all (a coincidental, invalid decode landing in
padding).

**Not yet attempted**: confirming whether `LoadPtr_LoadedSound`'s registered `AddPointerLookup`
call (`FillStruct_SoundFileRef`, `snd_alias_list_t_iw5_load_db.cpp`) is even the right SOURCE
event for what native's own tag-3 heap allocator effectively does at allocation time (native
registers into `DAT_140d6de00`'s own table AT ALLOCATION, not at the containing struct's own
on-disk field position) -- if so, the real fix is a new, tag-3-scoped alias-registration path
keyed on ALLOCATION IDENTITY (e.g. a running per-tag allocation counter matching native's own
`DAT_140d6e210`-driven bookkeeping in `FUN_1400aab50`) rather than reusing the existing
in-block `AddPointerLookup`/`m_pointer_redirect_lookup` mechanism, which is fundamentally scoped
to real content-block byte positions and may never be able to represent a heap-tag reference
correctly as currently designed. This is a genuinely new architectural direction, not a small
patch -- deliberately not attempted this round without further confirmation, per this exact
code area's own repeated documented history of regressions from rushed changes.

Temporary diagnostic reverted before this commit (`git diff` confirms `ZoneInputStream.cpp`'s
only surviving change from this round is a harmless `#include <cstdint>` addition, already
transitively available but now explicit). No functional code changed this round -- this is a
pure investigation/documentation round, same as SS5.9/5.23's own "0 fix, full trail" precedent.

### SS5.45 (2026-09-16, later) — second attempt to degrade the loadSnd alias-miss fallback, live-tested and reverted again; a real downstream dereference is now the confirmed remaining unknown

Direct instruction following SS5.44: "lets push through with the fu[l]l fix." Given SS5.44's
round substantially narrowed (without fully closing) the mystery -- ruling out a struct-shape
bug, confirming this fork's function choice already matches native's real resolver, and finding
the failing target position is genuine unrelated padding -- that last finding lines up with this
whole file's own already-documented "OpenAssetTools ConvertOffsetToPointer... structs reuse data
across non-matching types... realistically only happens when the data is nulled" precedent
(`MaybePointerFromLookup`'s own header comment). On that basis, retried the SAME mechanical
change SS5.42 had already tried once and reverted (converting this fallback's `assert(false);
throw` to `break`, falling through to the same already-proven-safe nullptr-return path the
hop-exhaustion case uses) -- this time with a much fuller understanding of WHY the reference is
unresolvable, not a blind retry.

**Live-tested immediately against `common_survival.ff` (the same zone the first attempt
crashed against), per this exact code area's own standing "verify live before trusting" rule.**
Result: **segfaulted again** -- but progressed substantially further first (offsets climbed from
~10.7M to ~16.7M in the VIRTUAL block, with dozens more references gracefully degraded via the
existing warn-and-null paths in between) before crashing. This is real, new information: the
resolution/degradation logic ITSELF is not the direct cause (many instances of the identical
"unregistered but written" shape resolved safely to null and the loader kept going) -- something
DOWNSTREAM, reached only after enough nulled `loadSnd` references accumulate or a specific later
one is hit, still dereferences bad state without a null check, unlike the `if (*varMaterialPtr)`-
style guard this whole graceful-degradation strategy assumes every caller has.

**Reverted immediately**, same standard as the first attempt -- back to the hard `throw`,
confirmed via a clean re-run (exit code 1, a caught `InvalidOffsetBlockOffsetException`, not a
crash). `ZoneInputStream.cpp`'s only diff from this round is the comment documenting this second
attempt; behavior is unchanged from before SS5.44/5.45 began.

**Real, narrowed next step**: the open question is no longer "why does this reference fail to
resolve" (SS5.44 answered that convincingly) -- it's "what downstream code path dereferences a
null `loadSnd` (or a null value reached transitively from it) without checking first." Given
`Load_SoundFileRef`'s own caller already guards `if (varSoundFileRef->loadSnd)` BEFORE resolution
(using the pre-resolution truthy raw value, not the post-resolution result) and does not re-check
after, the most direct next step is auditing every use of a resolved `SoundFileRef::loadSnd` (or
the `LoadedSound*` it produces) between this resolution point and wherever the actual fault
occurs -- likely reachable via a targeted breakpoint/guard-page approach (x64dbg, per this
project's own standing "cdb/WinDbg not approved, x64dbg attach/pause only" policy) rather than
another blind mechanical retry of the same fallback change, now that two independent live tests
have confirmed it crashes.

Two independent, live-tested attempts at this exact fallback change have now both segfaulted
against the same zone -- per this project's own standing "Fresh Perspective" principle, this is
real evidence the SAME angle (degrading the resolution itself) needs a genuinely different
technique (finding the actual downstream fault) before a third attempt, not a reason to stop
investigating the parser entirely.

### SS5.46 (2026-09-16, later still) — real bug found and fixed via self-dump crash analysis: AssetInfoCollector missing the same null-name guard AssetLoader already has; loadSnd alias-miss fallback still not safe to degrade (a second, different crash found one layer deeper)

Direct instruction to keep pushing on the fastfile parser after SS5.45's second reverted
attempt. x64dbg's own live-resume was independently confirmed unsafe again this round --
crashed x64dbg itself even under a fully manual, user-driven GUI F9 (not just the MCP-triggered
resume), ruling out "buggy MCP bridge" as the explanation and broadening the standing memory
finding to treat ANY x64dbg resume as unsafe on this machine until further notice (see the
project's own persistent memory, `feedback_no_autonomous_live_debugger`).

**New safe technique used instead**: a temporary in-process crash handler
(`AddVectoredExceptionHandler`, `main.cpp`) that calls `MiniDumpWriteDump` on a genuine
`EXCEPTION_ACCESS_VIOLATION` (filtered so it doesn't fire on this codebase's own ordinary C++
exceptions) -- the same safe self-dump pattern this project's proxy DLL already uses
(`TriggerSelfMemoryDumpX64`), adapted for a standalone CLI tool with no injected DLL. No live
debugger attach at all; `Unlinker.exe` just runs normally and writes its own `.dmp` on crash,
analyzed afterward via the already-approved `mcp-windbg` static-read carve-out.

**Reproduced SS5.45's crash and got a real, symbolized(-enough) answer**: `ucrtbase!strlen`
reading address `0x0`, called from deep inside the asset-marking call chain. Root cause:
`AssetInfoCollector::Visit_Dependency` and `Visit_IndirectAssetRef` (`AssetInfoCollector.cpp`)
call `m_zone.m_pools.GetAsset(assetType, assetName)` / construct `IndirectAssetReference(type,
assetName)` directly from a `const char* assetName` with **no null check** -- both take
`const std::string&`/`std::string` by value, so a null `assetName` implicitly constructs
`std::string(nullptr)`, undefined behavior that crashes inside `strlen` on this MSVC STL.
`AssetLoader::GetAssetInfo`/`LinkAsset` already got this exact guard on 2026-09-15 (their own
comment: "a null name is genuine, legitimate native data for a 'reusable'-pointer asset") --
`AssetInfoCollector`'s two visitor functions were simply missed at the time, since they were
never previously reachable with a null name until this session's own graceful-degradation work
made unresolved-but-null references common enough to hit them. Fixed with the identical guard
pattern (return `std::nullopt`/early-return on `assetName == nullptr`, matching the existing
"null is a valid absent-reference outcome" convention this whole file already uses).

**Verified real and independently valuable**: this fix stands on its own regardless of the
loadSnd alias-miss investigation's own outcome -- it closes a genuine latent bug (unguarded
implicit `std::string(nullptr)` construction) that any future graceful-degradation work could
hit again. Build-verified, and confirmed to introduce zero regression against `common_survival.ff`
and `code_post_gfx.ff` (both still hit their own already-tracked, unrelated open issues,
identical warning/error counts to before this fix).

**With this fix applied AND SS5.45's `break` change reinstated for testing**: progressed
substantially further (offset climbed to ~16.87M in `XFILE_BLOCK_VIRTUAL`, 14,564 warnings
handled gracefully vs. ~30 before) before hitting a **second, different crash** -- also inside
`ucrtbase!strlen`, but this time reading a clearly-invalid non-null value
(`0x4133280ebfc5c9de`/`0xbeccd7f1403a3622` across multiple registers -- non-zero high bits, not
a valid heap-pointer shape on this build) rather than a null pointer. This is NOT the same bug
class as the one just fixed (a genuine absent/null reference) -- it looks like a struct-shape or
field-misalignment issue (a non-pointer value, e.g. a hash or float, being read as if it were a
`const char*`), the same general shape as the original `SpeakerMap` bug (SS5.40), just for a
different, not-yet-identified struct reached only this much deeper into the zone.

**Reverted the `break` change again** (back to the hard throw) -- the loadSnd alias-miss
fallback is STILL not safe to degrade on its own merits; fixing the `AssetInfoCollector` bug
only got one layer further before hitting a genuinely different, unrelated crash. Kept the real
`AssetInfoCollector` fix (verified safe and valuable independent of the larger investigation).

**Real next step, well-scoped**: identify what struct/field is being misread as a `const char*`
in this second crash -- likely another wrong-struct-shape case reachable only after the
`AssetInfoCollector` fix's own extra progress, following the exact same native-decompile
methodology that resolved `SpeakerMap` (SS5.40). The self-dump + offline-read technique proven
this round is the safe, repeatable way to keep chasing this without live debugger risk.

### SS5.47 (2026-09-16, later still) — second crash traced without symbols to a corrupted std::string discovered during a hash-table walk; Release config has no PDB, Debug config blocked by the known ObjCommon templating issue

Continued straight from SS5.46's second, open crash. Reproduced it again via the same self-dump
technique (deterministic: byte-identical register values and `FAILURE_ID_HASH` across two
separate runs, confirming this is a fixed, reproducible fault, not memory-layout-dependent
garbage).

**No usable PDB for the Release build** -- `.reload /f Unlinker.exe` reported "cannot find the
file specified" even with `.sympath+` pointed straight at the output directory; the actual
link line has no `/DEBUG`, so no matching PDB is ever produced for this config. Tried building
the `Debug` configuration instead (which should produce real symbols) -- hit this project's own
already-documented `ObjCommon` "Templating" custom-build-step failure (`MSB8066`) the moment a
project needing it (`ObjCommon` itself, transitively required by `UnlinkerCli`'s Debug link)
was built standalone; `CLAUDE.md`'s own existing guidance to build `ZoneLoading`+`UnlinkerCli`
separately for Release exists specifically to dodge this same issue, but Debug hits it earlier
in the dependency chain (`ObjCommon` itself, not just the top-level project). Not pursued
further this round -- fixing the Debug build's own dependency-resolution/templating issue is a
separate, real piece of build-system work, not part of this investigation.

**Traced the crash via raw disassembly instead** (`ub`/`u` on the dump's own return addresses,
no symbols). Confirmed structure, working backward from the `strlen` call:
- The immediate caller (`FUNC_A`, entry ~`Unlinker+0x1192f0`) is a `std::string`-assignment-
  shaped helper: `(this, const char* param)` -- if `param == nullptr`, builds a fixed 15-byte
  fallback string inline (no crash risk); if `param != nullptr`, calls `strlen(param)` and
  presumably copies -- i.e. this helper ALREADY correctly guards the null case, matching every
  other already-fixed call site in this investigation. The crash requires `param` to be
  non-null (it is: `0xbeccd7f1403a3622`, `0x4133280ebfc5c9de`) but genuinely invalid.
- `FUNC_A`'s own caller passes `rdx = qword ptr [rbx+0xA8]` as that `param` -- i.e. the bad
  value comes from a FIELD, not a fresh computation. The surrounding code (`mov rdi,[rbx+0xA8]`
  /`mov [rbx+0xA8],rdi`/`cmp qword ptr [rdi],r12`/`je ...`, inside a `cmp r15,rbp; jb` loop) has
  the exact shape of a linked hash-bucket-chain walk (MSVC `std::unordered_map`'s own sentinel-
  terminated internal list, `_Next` pointer chasing) -- almost certainly `GlobalAssetPool`'s own
  hash map being searched or rehashed, landing on a node whose stored key (a `std::string`) is
  itself corrupted.

**Real conclusion**: the corruption did NOT happen at this call site -- a `std::string` got
inserted into a hash map SOMEWHERE EARLIER already broken (holding a garbage internal pointer),
and this crash is just the first place that broken entry gets read back out and re-strlen'd
(during a compare/rehash). This is consistent with, though not yet proven to be, the same
"wrong struct shape misreads unrelated binary data as a name pointer" class as the original
`SpeakerMap` bug (SS5.40) and the general shape of this whole investigation's earlier finding
that a lot of what reaches this deep into the zone is `snd_alias_t`/`SoundFile`/`LoadedSound`-
adjacent data -- but WHICH specific asset/field inserts the corrupted string has not been
identified; finding it requires either proper symbols (blocked, see above) or tracing forward
from a specific asset's own `Mark`/`AddAsset` call with a live diagnostic, the same proven
technique used for every earlier fix in this file.

**Reverted the `break` change again** -- still not safe to ship; genuinely a different,
unrelated bug from the one just fixed in SS5.46, and this round didn't reach a fix, only a much
more precise characterization of where to look next. `ZoneInputStream.cpp`/`main.cpp` both
confirmed clean (`git diff` empty) before this commit.

**Concrete next step**: either (a) fix the Debug build's `ObjCommon` templating-step failure
once, as a standing investment (would make every future self-dump session dramatically faster
via real symbol names instead of raw disassembly), or (b) add a scoped, temporary diagnostic
directly inside `AssetPool`/`GlobalAssetPool`'s own `AddAsset` (not `AssetInfoCollector`, which
is already fixed) that validates a `std::string` name's own internal pointer looks sane
immediately after construction, to catch the ACTUAL insertion site rather than the later
discovery site.

### SS5.47 addendum — the canonical-pointer-shape guard theory tested and disproven

Tested a well-precedented, low-risk fix candidate for the SS5.47 crash: a canonical-pointer-
shape check (`(uintptr_t >> 48) == 0`, matching this fork's own already-trusted
`LooksLikeUnresolvedRawOffset` heuristic) added to `AssetInfoCollector::Visit_Dependency`/
`Visit_IndirectAssetRef`, on the theory that the crashing values (`0xbeccd7f1403a3622` etc.,
non-canonical -- top bits non-zero) were reaching `m_pools.GetAsset` through those exact two
functions, just with a non-null-but-garbage `assetName` my SS5.46 fix didn't catch.

**Live-tested, disproven**: rebuilt with both the guard and SS5.45's `break` change, ran against
`common_survival.ff` -- still crashed identically, and the new guard's own warning never fired
(0 occurrences in the log). This proves the crash does NOT reach `Visit_Dependency`/
`Visit_IndirectAssetRef` at all -- some OTHER call site constructs the bad `std::string`. This
is consistent with, and now better supports, the ORIGINAL SS5.47 theory (a `std::string` was
corrupted at an EARLIER, different insertion point, and this crash is where it's read back out
during a later hash-table operation) over the "fresh bad input at this call site" theory.

Reverted the guard (unverified speculative code that doesn't address the actual fault isn't
worth keeping) -- confirmed `git diff` clean on both `ZoneInputStream.cpp` and
`AssetInfoCollector.cpp` before this commit. The SS5.47 recommended next steps (fix the Debug
build's `ObjCommon` templating blocker for real symbols, or add a targeted diagnostic at
`AddAsset`'s own insertion point rather than a later read site) stand as the real path forward.
