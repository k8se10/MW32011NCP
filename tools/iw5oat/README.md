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

**Usable for GSC/rawfile extraction on a real, growing set of retail
zones — no longer limited to script-only content.** As of 2026-09-16,
`sp_intro.ff`, `sp_prague.ff`, `so_trainer2_so_deltacamp.ff`, `sp_dubai.ff`,
and `sp_ny_harbor.ff` all load and extract **completely cleanly (0
warnings, 0 errors)** through `Unlinker.exe`. A much larger set of
previously-crashing zones (`hamburg.ff`, `common.ff`, `code_post_gfx.ff`,
`common_survival.ff`, and others) now load substantially further than
before and no longer crash outright, but don't yet reach a fully clean
0-warning finish — see item 5 below for the current, specific remaining
blocker. This status has moved through two real, separate investigation
arcs since the fork was first created (the Material/shader chain, and the
Sound/`LoadedSound` chain) — both are summarized below rather than left
as one static snapshot, since both are now substantially resolved or
well-understood.

The real x64 fix has gone through five real phases. The first four are
done; the fifth is the current, actively-investigated remaining blocker:

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
3. **The Material/shader chain — fully resolved (2026-09-14).** The
   `decl[]`/`ps`/`vs`/`Material::subMaterials` question, a real 32-bit-vs-
   64-bit-wide offset-pointer encoding bug (`MaterialPixelShader::name`'s
   own corruption, root-caused to this fork's `ConvertOffsetToPointerNative`
   assuming every already-resolved offset pointer is 64-bit wide when the
   real engine's own generic resolution primitive is genuinely 32-bit for
   this class of reference), and the `materialHandles`/`XModel` write/read
   registration-asymmetry bug were all found and fixed via direct native
   x64 decompile cross-checks — zero regressions across a full 39-zone
   sweep. This entire chain, once the dominant blocker for every real
   Material-referencing zone, is closed. Full trail:
   `fastfile_format_research.md` §5.13-§5.28.
4. **The Sound/`snd_alias_list_t`/`LoadedSound` chain — largely resolved.**
   A second, separate investigation arc (§5.29-§5.43) found and fixed a
   forward-reference-before-write ordering bug (converted from a hard
   `throw` aborting the whole zone load to graceful warn-and-degrade,
   matched by real short-read detection hardening), a genuine
   `LoadedSound`/`MssSound` struct-layout bug, and — the single largest
   breakthrough of this whole investigation — a **wrong wire shape for
   `SpeakerMap`/`MSSChannelMap`/`MSSSpeakerLevels`** (this fork's own
   struct declarations, inherited from upstream's x86-era assumptions,
   read 416 bytes where the real native format reads exactly 64), found
   via `FUN_140096980`/`FUN_140096a50`'s own decompile and confirmed to
   explain the "invalid block N" symptom that had recurred across 40+
   investigation rounds. Fixed in the tracked struct header
   (`src/Common/Game/IW5/IW5_Assets.h`); live-verified real progress on
   every previously-blocked zone. Full trail: `fastfile_format_research.md`
   §5.40-§5.41.
5. **Currently open**: a `LoadedSound` alias-miss during
   `snd_alias_list_t`'s own dependency-sharing resolution — a shared
   `LoadedSound` reference between two array elements fails to resolve via
   either of this fork's own alias/pointer-redirect maps, even though it's
   in-range and already-written. Two independent live-tested attempts to
   degrade this specific fallback (matching the already-proven-safe
   pattern used elsewhere in this file) both caused a real segfault a
   different, further downstream — most recently narrowed to a corrupted
   `std::string` discovered during a hash-table walk, whose actual
   insertion point hasn't been identified yet. One real, independently
   valuable bug WAS found and fixed along the way (`AssetInfoCollector`
   was missing a null-name guard `AssetLoader` already had). Not yet
   resolved — kept as a hard `throw` rather than shipping a change that's
   been live-tested to crash twice. Full trail: `fastfile_format_research.md`
   §5.44-§5.47.

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
