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

## 5. Scoped plan for in-house tooling — an MVP, not a full OpenAssetTools replacement

**This project's own actual need is narrow**: GSC/rawfile extraction to
support the standing GSC-first RE methodology (`CLAUDE.md`'s own
directive) — not full asset extraction parity (models, materials, sounds,
etc.) the way OpenAssetTools targets generally. Recommending a real,
achievable MVP rather than reproducing OpenAssetTools' full scope:

- **A minimal, from-scratch zone-header parser**: read the confirmed-
  unchanged outer header (§1), inflate the zlib payload (standard, any
  language's zlib binding works), then parse ONLY the fields needed to
  walk to `scriptfile`/`rawfile`-type asset entries specifically — not
  every asset type in the zone.
- **x64 struct widths for the FEW asset-adjacent structs this actually
  touches** (the top-level `XFile`/zone-content header, the asset-type
  dispatch table, and the `scriptfile`/`rawfile` asset struct itself) —
  derived the same way every other x64 struct in this project has been
  this session: decompile `iw5sp.exe`'s own real zone-loading code in
  Ghidra (the game itself is the ground truth — it successfully loads
  these exact files every time it launches) rather than guessing at
  widened offsets from the x86 struct alone. **Not started yet** — this
  is the actual next RE step, genuinely substantial but far smaller than
  full asset-type coverage.
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
- `Laupetin/OpenAssetTools` — shallow-cloned to a scratchpad temp
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
