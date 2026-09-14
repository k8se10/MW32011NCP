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
