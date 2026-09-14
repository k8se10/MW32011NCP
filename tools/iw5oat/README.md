# iw5oat

**iw5oat** is an IW5-only fork of [OpenAssetTools](https://github.com/Laupetin/OpenAssetTools)
(OAT), maintained as part of [MW32011NCP](https://github.com/k8se10/MW32011NCP)
("Native Community Patches" for *Call of Duty: Modern Warfare 3* (2011)).
It exists for one reason: upstream OAT's `Unlinker`/`Linker` cannot load
any real-content zone (`.ff`) from the game's current retail build, and
that isn't close to a quick fix.

## Why this fork exists

On 2026-09-03, Activision shipped a genuine x86→x64 recompile of MW3
(2011) — both `iw5sp.exe`/`iw5mp.exe` went from 32-bit to 64-bit
binaries. Since then, upstream OAT's `Unlinker` reproducibly segfaults
loading any zone with real content from the current retail install
(confirmed independent of zone size, from 650KB to 195MB, and independent
of which assets are requested).

Root-caused, not just observed — full write-up with raw evidence:
[`re_notes/x64_migration/fastfile_format_research.md`](../../re_notes/x64_migration/fastfile_format_research.md)
in the parent project. Summary:

- The outer FastFile container (`IWffu100` magic, version, zlib
  compression) is **unchanged** — confirmed by hex-dumping real retail
  zone headers directly, not assumed.
- The **decompressed internal content now uses 64-bit-wide struct fields**
  (consistent with the executables' own architecture change) where
  upstream OAT's IW5 loader hardcodes `GameWordSize::ARCH_32` — see
  `src/ZoneLoading/Game/IW5/ZoneLoaderFactoryIW5.cpp`'s
  `InspectZoneHeader()`. Parsing 64-bit data with 32-bit struct
  definitions desyncs every offset after the first real pointer, which is
  exactly the segfault.
- `GameWordSize::ARCH_64` exists as a bare enum value in upstream OAT and
  is **never branched on anywhere in the entire codebase**, in any game
  loader, including T6 (a game with a real 64-bit console release). There
  is no partial upstream work to build on for this — confirmed by
  grepping the full source, not assumed.

Given that, and a real release-timeline constraint, waiting on an
upstream or community fix wasn't realistic. This fork exists to build
exactly the support this project actually needs, directly.

## Current status

The real x64 fix has three parts, the first two done and confirmed live,
the third still open:

1. **The outer dispatch-record/header bugs — fixed.** `iw5sp.exe`'s own
   zone-loading code was decompiled directly (Ghidra) to find the real
   x64 per-asset dispatch record (16 bytes, not OAT's assumed 8) and the
   real `XAssetList` header (32 bytes, not 16) — both now correct in
   `ZoneLoadingIW5`. Full trail:
   [`re_notes/x64_migration/fastfile_format_research.md`](../../re_notes/x64_migration/fastfile_format_research.md)
   §5.
2. **Every generated per-asset-type loader's own field offsets — fixed,
   via automated tooling, not hand-patching.** `ZoneCodeGenerator`'s
   generated code hardcodes x86-layout byte offsets for every one of the
   ~46 IW5 asset types, not just the outer dispatch — the same bug class,
   one level deeper. Rather than hand-deriving and hand-patching each
   type's own offset table, a set of scripts (**[`x64_offset_fixes/`](x64_offset_fixes/)**
   — see its own README) rewrite every literal offset into a
   compiler-verified `offsetof()`/`sizeof()` expression, letting the real
   x64 compiler compute the correct value instead of trusting a number
   `ZoneCodeGenerator` precomputed for the wrong architecture. **These
   scripts have to be re-run after every real `ZoneCodeGenerator`
   invocation** — their target is gitignored, regenerated build output,
   not tracked source; see that README for the exact sequence.
3. **Still open**: a small number of generated structs
   (`MaterialVertexStreamRouting::decl[]`, `MaterialPixelShaderProgram::ps`,
   `MaterialVertexShaderProgram::vs`) declare real pointer fields that are
   never read from the wire by their own `FillStruct_*` function — whether
   these are genuinely on-disk (raw serialized memory, matching this
   format's own convention) or runtime-only fields the wire format never
   included isn't resolved yet. This is the actual reason `Unlinker`
   still fails partway through real retail zones (confirmed: the same
   `MaterialPass` → shader-asset chain, on all three zones tested) —
   see `x64_offset_fixes/README.md`'s own "Known open question" section
   for the full detail and what it'll take to resolve (native Ghidra RE,
   not more guessing).

**Confirmed via direct testing against real retail zones from the live
game install** (`hamburg.ff`, `common.ff`, `code_post_gfx.ff`): the
original unconditional segfault is gone — all three now load through
decompression, the block-size header, and the entire dispatch loop with
no crash, failing instead with a clean, structured error partway through
real content. That's real progress (a crash vs. a legible, bounded
failure) even though full extraction isn't there yet.

## Scope: IW5 only, deliberately

Upstream OAT is a genuine multi-game project (IW3/IW4/IW5/QOS/T4/T5/T6).
**This fork's own development focus is IW5 (MW3, 2011) exclusively** —
this project has no use for, and does not intend to maintain, the other
games' support long-term. The rest of upstream's codebase (other games'
loaders, the shared build/codegen infrastructure) is currently still
present, inherited as-is from the fork point — removing it wholesale is
future cleanup, not done yet, and not a priority ahead of the actual IW5
x64 work. `docs/SupportedAssetTypes.md` still reflects upstream's own
multi-game state for now; treat any non-IW5 content in this fork as
unmaintained reference, not something this project is actively keeping in
sync with upstream.

Forked as a history-preserving merge, so this directory's own git history
still traces back to the real upstream commits; licensing set up (see
Legal below). See "Current status" above for where the actual x64 work
stands.

## Relationship to upstream

This is a fork, not an independent rewrite — where upstream's own code is
still correct for this project's needs (the outer zone-container parsing,
inflate/hash-skip handling, the build system, the general asset-loading
architecture), it's reused as-is rather than reinvented. Real credit
belongs with [Laupetin](https://github.com/Laupetin) and OpenAssetTools'
other contributors for that foundation. Diverging from upstream is scoped
narrowly to what MW3's own x64 recompile actually broke, not a general
"do it our own way" rewrite.

## Building

Same build system as upstream (Premake-generated project files) — see
`generate.bat` (Windows, Visual Studio) / `generate.sh` (Linux). After a
fresh `ZoneCodeGenerator` run for IW5 (part of the normal build, or run
standalone), apply **[`x64_offset_fixes/`](x64_offset_fixes/)** before
building `UnlinkerCli` (or anything else linking `ZoneLoading`/`ZoneWriting`
for IW5) — see that directory's own README for the exact command.
Skipping this step silently rebuilds against `ZoneCodeGenerator`'s raw,
x86-offset output, reintroducing the bugs described above.

## Legal

This fork remains licensed under [GPLv3](./LICENSE), same as upstream —
see this repository's own root `LICENSE` file for how this coexists with
`MW32011NCP`'s own separate, more permissive license (GPLv3 does not
permit layering additional restrictions on top of it, so this component
cannot be relicensed under NCP's own terms).

Extracting the contents of game files does not grant you any rights to
them. All rights remain with their respective owners.
