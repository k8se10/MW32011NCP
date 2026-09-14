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
