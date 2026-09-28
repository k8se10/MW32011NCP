# Runtime AI texture upscaling with a persistent local cache — scoping (started 2026-09-28)

**Goal, as stated by the user**: a "shader-compile-cache" model applied to
textures instead of shaders — on first real use of a given texture (keyed to
this mod/game version), run it through a real AI upscaler once, cache the
upscaled result to disk, and serve the cached result on every subsequent
load instead of re-running the upscaler. **Vulkan-only, by direct
instruction** ("we only care to support ths via vulkan") — no `LegacyD3D9`
implementation is in scope. This is additive to the existing visual-
enhancement suite, not a replacement for anything currently shipped.

## RTX Remix — closed, corrected here after being wrongly cited in chat

Mid-conversation, RTX Remix was raised as a real precedent for this feature.
That was wrong and already contradicted by this project's own prior
research (`known_issues_x64.md`, `renderer_architecture_map.md` §"Real
scope correction"): **RTX Remix is confirmed, directly from NVIDIA's own
repo README, to be fixed-function-pipeline-only** ("an end-to-end platform
for remastering DirectX 8 and 9 games with fixed function pipelines"). IW5
is a fully shader-based renderer (real material/technique/shader-asset
systems throughout, per this project's own renderer map) — the same
structural mismatch that blocks GTA IV's own Remix attempts. Remix's own
texture-upscale/replace subsystem depends on its draw-call-capture-and-
hash-replace pipeline, which itself depends on the fixed-function
assumption. **Not applicable here, not a someday item — closed.** This
document proposes a genuinely different, IW5-appropriate approach instead.

## Real precedent for THIS approach: ncnn-vulkan (Real-ESRGAN-ncnn-vulkan)

Verified via web search, 2026-09-28 (not assumed from training-data
recall, per this project's own "verify before trusting" standard):
- **`xinntao/Real-ESRGAN-ncnn-vulkan`** (github.com/xinntao/Real-ESRGAN-ncnn-vulkan)
  — a real, actively-referenced, cross-platform C/C++ implementation of
  Real-ESRGAN built on Tencent's **ncnn** inference framework with a
  **native Vulkan compute backend** — no CUDA/DirectML/ONNX Runtime
  dependency, which matters directly for the "Vulkan-only" constraint:
  this tool's own upscale inference already runs as Vulkan compute
  shaders, the same graphics API this mod's `GraphicsApi=Vulkan` mode
  already uses via the vendored `MW32011DXVK` fork.
- **License: MIT/BSD** per the project's own README — compatible with this
  project's existing vendoring pattern (MinHook BSD-2-Clause, DXVK
  zlib/libpng, Streamline headers MIT). **Model weights verified 2026-09-28**:
  the real pretrained `.pth` weights (`RealESRGAN_x4plus.pth` etc., Xintao
  Wang, first released 2021-07-22) are BSD-3-Clause per `xinntao/Real-ESRGAN`'s
  own `LICENSE` file — same permissive tier as MinHook, clear to vendor once
  the actual `.bin`/`.param` ncnn-converted weight files (not the raw `.pth`)
  are confirmed to carry the same terms from whichever ncnn-format mirror is
  used.
- This is the same "vendor a real, existing, precedented open-source tool
  as a nested component" pattern already used for `MW32011DXVK` (a real
  `dxvk` fork) and the NVIDIA Streamline SDK headers — not a new category
  of dependency for this project.

## Real hook point, confirmed present in the vendored DXVK fork

`dxvk/src/d3d9/d3d9_common_texture.h` — `class D3D9CommonTexture` (line 74)
owns a real `Rc<DxvkImage>` (`GetImage()`, line 137), DXVK's own Vulkan
image handle wrapping whatever D3D9 texture the game created. This is the
real, addressable interception point: whenever a D3D9 texture is created
and its pixel data first uploaded (the corresponding `UploadData`-class
path in `d3d9_common_texture.cpp`, not yet read in full this pass), that's
where a cache-lookup-or-upscale-and-cache step would need to sit, keyed off
the source texture's own content hash. This still needs a full read of
`d3d9_common_texture.cpp`'s real upload path (staging buffer construction,
mip levels, compressed-format handling — MW3's own textures are very likely
DXT-compressed, which changes what "upscale" even operates on: decompress
first, upscale, re-encode or store uncompressed) before any code is
written — **not done yet, real next step**.

## NVIDIA-native alternative checked, genuinely doesn't apply here

Following the RT roadmap's own DLSS-preferred/FSR-fallback pattern, checked
whether an equivalent NVIDIA-native path exists for texture upscaling
specifically (2026-09-28 web verification, not assumed). **NVIDIA RTX
Neural Texture Compression** (`NVIDIA-RTX/RTXNTC`, real, current, GTC 2026)
is the closest real SDK, but it solves a different problem: NTC reduces
VRAM footprint by reconstructing textures at runtime from a small trained
neural representation of the ORIGINAL texture — it doesn't add detail a
source texture never had, the way an ESRGAN-class upscaler does. **No
real "prefer NVIDIA, fall back to ncnn-vulkan" split exists for this
feature** the way it does for RT reconstruction (DLSS Ray Reconstruction
vs. FSR 3.1 genuinely compete for the same job; NTC and ESRGAN-class
upscaling don't). Real-ESRGAN-ncnn-vulkan remains the single primary
approach. NTC is worth flagging as a possible later, complementary
optimization — compressing the CACHE this feature builds (the upscaled
2K+ textures) to cut its own disk/VRAM cost — not a competing path to the
upscale step itself; not scoped further here.

## Real open design questions (none answered yet — this is scoping, not a plan)

1. **Compressed-texture handling — confirmed a real, present case, RESOLVED
   at the design level (not yet implemented).** `dxvk/src/d3d9/d3d9_common_texture.cpp`
   (`D3D9CommonTexture` creation path, ~line 177) explicitly validates
   `IsDXTFormat(pDesc->Format)` against block-alignment before creating the
   image — DXT/BC-compressed textures are a real path this fork already
   handles. Confirmed via `D3D9DeviceEx::UpdateTextureFromBuffer` (below):
   the bytes reaching this hook point for a DXT texture are still
   compressed blocks, not decoded pixels — but since this feature now hooks
   at the engine's own `GfxImage` layer (see "Blocker resolution" below),
   BEFORE the D3D9 texture even exists, the real decision point moves
   upstream too: decode the source block-compressed data once at that
   layer, upscale, then re-encode or upload uncompressed when the
   upscaled-size `CreateTexture` call is made. Still an open cost/quality
   tradeoff (re-encode vs. more VRAM), but no longer entangled with the
   DXVK-layer timing problem.
2. **Cache key — RESOLVED.** The real `GfxImage` asset name (see "Blocker
   resolution" below) is the cache key, not a content hash — stable, known
   before any texture upload happens, free (already exists in the engine,
   no new bookkeeping). Still needs a model-version/scale-factor stamp
   alongside it so a future upscaler-model change or scale-factor change
   invalidates old cache entries correctly, mirroring how
   `DxvkPipelineCache`-style caches invalidate — that stamp's exact format
   is real but small remaining design work, not a blocker.
3. **First-use cost and hitching.** Running a real ESRGAN-class model
   inline on the frame that first needs a given texture will stall that
   frame significantly (this is explicitly NOT a per-frame-budget cost like
   DLSS, it's a one-time bulk cost) — needs either a background-thread
   upscale queue (serve the original texture until the cached upscale is
   ready, swap in on completion) or an explicit one-time "building texture
   cache..." pass before gameplay starts. This project's own standing
   background-thread architecture convention (`CLAUDE.md` §1, "event-driven,
   one thread per job") is the right model to reuse here, not a new pattern.
4. **Disk footprint.** MW3's own texture set is large; a full upscaled
   cache at even 2x could be many GB. Needs a real accounting of how many
   distinct textures actually get touched in a real playthrough before
   committing to an unconditional "cache everything" design — likely wants
   to stay strictly opt-in, off by default, same as every other pre-1.0
   experimental feature in this project.
5. **Where the cache lives.** Likely beside the game install (matching
   `mw3ncp_config.ini`'s own convention) or under the project's own APPDATA-
   style location — not yet decided.

## Sequencing decision, 2026-09-28

**Texture upscaling goes first, RT after.** Direct instruction. Starting
the real next steps below now.

## Scope clarification, 2026-09-28: source asset textures, not the backbuffer

**This feature targets the game's own source textures loaded from disk
(`.iwd`/`.ff` assets — diffuse maps, normal maps, UI art, etc.), not the
backbuffer or any render target.** `D3D9CommonTexture`/`UpdateTextureFromBuffer`
(below) is specifically the path for textures the game fills via
`LockRect`/write/`UnlockRect` then uploads to the GPU once via a staging
buffer — real, static asset content. Render targets and the backbuffer
never go through this path at all (they're rendered into directly by the
GPU every frame) and are already handled by this project's existing,
completely separate `InternalRenderScalePercent` mechanism. No overlap
between the two — recording this explicitly so a future session doesn't
conflate "upscale the game's textures" with "upscale the render
resolution," which this project already has a solved, shipped answer for.

## Locked constraint, 2026-09-28: never modify `.iwd`/`.ff` archives, runtime interception only

**Direct instruction: `.iwd` files are integrity-checked, so they must
never be overwritten or pre-patched with upscaled textures on disk, even
at runtime.** The only sanctioned approach is reading through this
project's own interception mechanism (hooking the real upload path,
substituting the cached upscale in memory as it reaches the GPU) — never
touching the source archive itself. This is the same "the game install is
read-only reference material, the only sanctioned write is our own proxy
DLL" policy this project already applies to `main/`/`zone/`/the executables
(`CLAUDE.md` §2, Key Principle 2) and the same reasoning already on record
for never hex-patching the executables — extends cleanly to `.iwd`/`.ff`
asset archives too, not a new category of restriction. **This locks in
option (a)/(c) from the blocker below as the only real path forward**: the
upscale substitution has to happen at the `D3D9CommonTexture`/upload layer
(in-memory, per-session), never as a pre-processing pass that rewrites the
archives players' own game installs already have integrity-checked — ruling
out any design that assumed disk-level asset replacement as a shortcut.

## CORRECTION, 2026-09-28: the "clean GfxImage-name hook" proposed below does not safely exist — reuse this project's own already-built, already-proven correlation mechanism instead

The design further below (hook a single native "GfxImage becomes a D3D9
texture" function to get a name before `CreateTexture`) was proposed
without first checking this project's own prior RE on exactly this
question. It already exists, on x86, and already ran into the real reason
that clean hook point doesn't exist: **`re_notes/iw5sp.md`'s "Runtime
material/texture capture" section (2026-08-17)** traced the real per-image
load path below a material's own name (`FUN_0047a2f0` → `FUN_005511c0` →
`FUN_00585ae0`) and found it bottoms out in an `unaff_ESI` **implicit-
register-passed** hash-bucket residency check — the same calling-convention
hazard this project already treats as too risky to hook blind elsewhere in
the codebase — and even then, it "does not obviously create a texture
itself," reading more like an "is this image resident, mark it wanted if
not" check, quite possibly feeding an **async streaming request**, not a
synchronous `CreateTexture` call at all. That investigation explicitly
decided **not** to chase this further, for good reason — this document's
"locate the GfxImage realization function" plan would very likely
rediscover the exact same dead end on x64.

**The real, already-built, already-shipped (opt-in, `[Experimental]
CaptureRuntimeMenuAssets`) answer instead: correlate by call stack/call-
order, not by finding one perfect name-bearing hook.** `FindOrLoadAsset`
(`FUN_004ff000` on x86, `__cdecl(int assetType, const char* name, int
flag)`, a real generic "find or load an asset of type N by name"
function used for every asset type by numeric ID — material is type `5`)
is hooked; while a load of the relevant type is on the stack, its real
`name` is pushed onto a small fixed-depth stack (`asset_capture.cpp`).
Separately, `IDirect3DDevice9::CreateTexture` (vtable index 23) is hooked
on the real device. Any texture created while a matching load is on the
capture stack gets attributed to that name — a correlation, not a
guarantee of a 1:1 single-hook mapping, but one this project's own "never
associate a texture with a name it isn't confident about" discipline
already applies correctly (no capture outside a confirmed matching
`FindOrLoadAsset` call on the stack). **This mechanism is real, already
implemented, and already deployed for material-name capture — it has
NOT been ported to x64** (`analog_input_hooks_x64.cpp` has no
`FindOrLoadAsset`/`Hook_FindOrLoadAsset` equivalent; `asset_capture.cpp`
itself has no arch guards, so it likely already builds on x64, but its
x86-only `FindOrLoadAsset` hook wiring in `analog_input_hooks.cpp` means
it currently does nothing useful there).

**What this changes for the texture-upscale cache key**: the granularity
is MATERIAL name (assetType `5`), not a raw `GfxImage` name — a material
can reference several distinct textures (diffuse/normal/specular/etc.), so
the real, practical cache key is likely `materialName` + a stable index
for which texture-creation-within-that-material-load this is (first,
second, third `CreateTexture` call seen while that material's name is on
the capture stack), not a clean single image identifier. This is workable
(the same material load should request the same textures in the same
order run to run) but is a real, small design cost compared to the
"free, perfect identifier" the earlier framing assumed — worth being
honest about rather than repeating the overclaim.

**Real next step, correctly scoped now**: find `FindOrLoadAsset`'s x64
twin (a real, bounded, already-precedented RE task — this project has
already successfully found several x64 structural twins of x86 dispatch
functions this way, e.g. the MP kbutton-dispatcher twin) and port
`asset_capture.cpp`'s existing hook-and-correlate mechanism to x64,
rather than hunting for a brand-new, cleaner hook point this project's
own prior research already shows likely doesn't safely exist. Also worth
checking, before assuming material-level granularity is the ceiling:
whether a real numeric assetType value for images exists and is ever
passed through `FindOrLoadAsset` independently of a material's own load
(not confirmed either way this pass) — if images DO get their own
`FindOrLoadAsset(imageType, name, flag)` calls, that would give a cleaner,
image-level cache key for free, same mechanism, no material-index
guessing needed. Not yet checked.

### First real hunt round, 2026-09-28 — real tooling bug found and fixed, target not yet located

Traced x64's confirmed `SetMenuState` (`FUN_14029f3f0`) and its confirmed
`OpenMenuByName` twin (`FUN_1402ad950`, per `known_issues_x64.md`) as the
most promising existing lead, since x86's own trace showed
`FindOrLoadAsset(0x1a /*menu*/, name, 1)` runs unconditionally just before
this exact step for every menu open. Fully decompiled `FUN_14029f3f0`
(confirms all ten `SetMenuState` cases call `FUN_1402ad950(ctx, name)`
directly, matching the known_issues_x64.md summary exactly) and
`FUN_1402ad950` itself, which turned out to only call two functions:
`FUN_1402acef0` (a linear array scan matching x86's own `FUN_00486990`
"already registered?" check, NOT `FindOrLoadAsset`) and `FUN_1402ad560`
(real menu-stack push/activate logic, notify chains, screen-focus
management — also not `FindOrLoadAsset`). **Neither call visibly
interns/loads anything from a name-keyed asset pool** — `SetMenuState`'s
real x64 chain, as currently mapped, does not show the
`FindOrLoadAsset`-equivalent step x86 always takes first. Two real
possibilities, not yet distinguished: (a) x64 restructured menu-asset
loading to happen in bulk earlier (e.g. at zone/level load), so by the
time this runtime open-path runs the menu is already guaranteed resident
and there's genuinely no per-open intern call left to find here, or (b)
the call exists but lives inside `FUN_1402acef0` itself in a form not yet
traced carefully enough, or is reached through a path this pass didn't
follow.

**Real, durable side-result: found and fixed a genuine bug in this
project's own `re_notes/ghidra_scripts/FindDirectCallers.java`** — its
computed call target was masked to 32 bits (`& 0xFFFFFFFFL`), which
silently zeroed out the real x64 image base (`0x140000000`+) and made
every x64 target comparison fail with zero matches and no error, even for
a call independently confirmed to exist by decompiling the caller
directly (`SetMenuState` visibly calls `FUN_1402ad950` 28 times; the
buggy script reported 0 before the fix, 28 after). Harmless for x86 (a
32-bit address masked to 32 bits is a no-op) but a real, previously-
unnoticed silent-failure trap for any x64 caller-hunt using this script —
fixed in place, benefits every future RE session using this tool, not
just this investigation.

**Status: genuinely still open, not a dead end yet, paused here rather
than dug further this pass.** No `FindOrLoadAsset` x64 twin located.
Real next angle for whoever picks this up: check the material path
instead of the menu path (materials are the actually-relevant asset type
for this feature anyway, per the "cache key" section above) — trace a
known x64 material-loading entry point the same way, or check possibility
(a) above directly by searching for a bulk/startup-time asset-registration
pass on x64 that x86 doesn't have.

### Second round, same day — a real capability unlock, and the lock-signature is the next concrete angle

**Found a full-analysis Ghidra project already sitting on disk**:
`re_notes/x64_migration/ghidra_project_x64_analyzed/iw5sp_x64_full.gpr`
(from the 2026-09-24 renderer-architecture mapping session, per
`CLAUDE.md`'s own Version Timeline — "two full (non-`-noanalysis`) Ghidra
analysis passes run against BOTH binaries same day"). Opening it with
`-process` (not `-import`) makes Ghidra's own reference manager actually
work — confirmed by re-running `DecompileAndCallersAt.java` (the
reference-manager-based caller tool that returned nothing under a fresh
`-noanalysis` import) against `FUN_1402ad950` and getting all 37 real
callers back correctly, matching the raw-byte-scan result exactly. **This
is a real, reusable capability for every future x64 RE task in this
project, not just this hunt** — `-noanalysis` raw-byte-scan workarounds
(`FindDirectCallers.java` etc.) are still valid but no longer the only
option; a full analysis already exists and should be tried first via
`-process` before reaching for a raw scan.

**Confirmed `InterlockedIncrement`/`InterlockedDecrement` are NOT real
imports in the x64 binary** (`grep -i interlocked
x64_imports_full_dumpbin.txt` — no match), meaning `FindOrLoadAsset`'s x86
lock pattern is compiled as inlined `lock`-prefixed instructions on x64,
not a callable import — ruling out an import-xref-based search for it.
**Real next concrete angle, not yet attempted**: a raw byte-pattern scan
for the `lock inc`/`lock xadd` instruction encoding (`F0 FF` / `F0 0F C1`)
across `.text`, filtered to matches near a spin-wait-shaped loop and a
large switch/dispatch call — the same structural-signature technique
already proven for other central dispatchers in this project's history.
Paused here for tonight; the full-analysis project being confirmed usable
is the headline result of this round.

### Third round, same day — `FindOrLoadAsset`'s x64 twin FOUND and CONFIRMED, plus a major bonus: the real numeric `assetType` for images

Built `re_notes/ghidra_scripts/FindLockPrefixHotFuncs.java` (new, reusable
tooling): raw-byte-scans `.text` for `LOCK`-prefixed `INC`/`DEC`/`XADD`/
`CMPXCHG` instructions, finds each hit's containing function (via the
full-analysis project opened with `-process`, so real function boundaries
exist), and ranks candidates by real caller count via the reference
manager — the same "a widely-shared lock stands out with a high caller
count" reasoning x86's own 59-caller `FindOrLoadAsset` finding already
established. Found 784 distinct lock-containing functions; filtered to
27 with 10+ callers.

**`FUN_1400a5a20` (58 callers, remarkably close to x86's confirmed 59)
is `FindOrLoadAsset`'s real x64 twin — confirmed, not just plausible**:
- **Signature match**: `undefined8 FUN_1400a5a20(int param_1, undefined8
  param_2, int param_3)` — exactly `(int assetType, const char* name, int
  flag)`.
- **Lock/spin-wait pattern match**: `LOCK(); DAT_140c5cee8 =
  DAT_140c5cee8 + 1; UNLOCK(); while (DAT_140c5ceec != 0) { Sleep(0); }`
  — structurally identical to x86's documented
  `InterlockedIncrement/Decrement` + spin-wait-via-`Sleep` shape.
  `FUN_1400a5950(param_1, param_2)` is the cache-hit name lookup (x86's
  `FUN_00585400` twin); on a miss with `param_3` (the `flag` argument)
  set, `FUN_1400a54c0(param_1, param_2)` does the real load/create.
- **`FUN_1400a54c0` independently confirmed as the per-asset-type
  dispatch/create function** (x86's `FUN_004b6b70` twin) via a real,
  literal embedded error string: `"Could not load default asset '%s' for
  asset type '%s'.\nTried to load asset '%s'."`, formatted with entries
  from two parallel tables indexed by the same `assetType` integer — a
  function-pointer jump table (`DAT_1404c3240`, per-type load callbacks)
  and a string-name table (`PTR_s_physpreset_1404c2430`, real type names
  for the error message).

**Dumped that real string-name table directly** (`DumpRawQwords.java`,
`0x1404c2430`-`0x1404c2598`) — a complete, real, x64 `assetType`→name
mapping, confirmed identical in ordering to x86's `IW5_Assets.h`-matching
scheme (index `5` = `"material"`, matching x86's already-confirmed `5`;
index `0x19` = `"menufile"`, matching x86's already-confirmed `0x19` for
menuList): `0`=physpreset, `1`=phys_collmap, `2`=xanim, `3`=xmodelsurfs,
`4`=xmodel, `5`=material, `6`=pixelshader, `7`=vertexshader,
`8`=vertexdecl, `9`=techset, **`0xa`=image**, `0xb`=sound, `0xc`=sndcurve,
`0xd`=loaded_sound, `0xe`=col_map_sp, ... `0x19`=menufile, `0x1c`=attachment,
`0x1d`=weapon, and more through `addon_map_ents`.

**This resolves the earlier granularity compromise entirely.** The
original design worried the real cache key would have to be "material
name + texture-slot index" because the x86 dead-end investigation only
ever showed a name at the MATERIAL level. **Confirmed now: images get
their own independent `FindOrLoadAsset(0xa, name, flag)` calls**, the
same real interning mechanism materials use — meaning the actual cache
key can be the real, individual image asset name directly, exactly the
"free, perfect identifier" the very first (pre-correction) design pass
assumed, now genuinely verified rather than assumed. The port target for
`asset_capture.cpp`'s existing hook-and-correlate mechanism is confirmed:
hook `FUN_1400a5a20` (x64 `FindOrLoadAsset`), push the real name onto the
capture stack while `assetType==0xa`, correlate against the existing
`CreateTexture` vtable hook exactly as the x86 material-capture code
already does for `assetType==5`.

**Not yet done, real next steps**: (1) confirm `FUN_1400a5a20` behaves
identically for real image loads via a live test (build a diagnostic-only
hook, log every `assetType==0xa` name seen, deploy, have the user play —
per this project's own "never launch the game myself" convention), (2)
locate/confirm x64's `CreateTexture` vtable hook is already wired
correctly for x64 (the existing `asset_capture.cpp` install call is
arch-neutral per earlier findings, so likely already fine, but not
independently re-verified this pass), (3) only then port the actual
`Hook_FindOrLoadAsset` wiring from `analog_input_hooks.cpp` (x86-only) to
`analog_input_hooks_x64.cpp`. This is real, concrete, unblocked next work
for the texture-upscale feature — the single biggest open dependency from
every earlier scoping round is now resolved.

## Blocker resolution, 2026-09-28: hook the engine's own asset-load layer, not DXVK's D3D9 layer (SUPERSEDED by the correction above — a specific GfxImage-realization hook is not confirmed safe to exist; the material-name/CreateTexture correlation approach above is the real plan)

The chicken-and-egg problem above only exists because `D3D9CommonTexture`'s
own `CreateTexture` call carries no identity — D3D9 itself has no named-
resource concept, just width/height/format/usage, so DXVK genuinely cannot
know which texture this is until content arrives. **The fix is to not rely
on DXVK/D3D9 for identity at all**: IW5's own real asset type for this is
`GfxImage` (confirmed as a real, already-understood IW5 asset type via this
project's own `tools/iw5oat` fastfile work, `fastfile_format_research.md`)
— a real, named engine asset (standard CoD-engine `GfxImage` struct: a
stable `name` field, dimensions, and image data) that exists and is fully
identified BEFORE the engine ever calls down into D3D9's `CreateTexture` to
realize it as a GPU resource. **This is the same RE methodology this
project already uses for everything else** (hook the real native engine
function, not the D3D9/DXVK translation layer, whenever asset identity
matters — the exact pattern behind every signature-scanned gameplay hook
in this codebase) — applying it here instead of trying to solve identity
inside DXVK resolves the blocker cleanly: **hook the native engine
function that reads a loaded `GfxImage` and hands its pixel data down to
be realized as a D3D9 texture** (not yet located — real next RE step, x64
signature unknown). At that point the real, stable asset name is already
known, so the cache lookup happens BEFORE `CreateTexture` is ever called:
on a cache hit, the upscaled dimensions get passed into `CreateTexture`
from the start (no DxvkImage resize/recreate needed at all — option (a)
from the original three is no longer necessary); on a cache miss, the
texture is created at its real original size as normal, and the upscale-
and-cache step runs asynchronously against the uploaded content afterward
(background-thread architecture per `CLAUDE.md` §1's own established
convention) for next time. **This makes option (c) (a pre-upload,
non-content-based cache key) the real answer, not a fallback** — the
`GfxImage` name IS exactly that stable, pre-upload identifier, and it's
free (already exists in the engine, no new bookkeeping invented). Real
next step: locate this project's own equivalent of "where GfxImage pixel
data becomes a D3D9 texture" in `iw5sp.exe` — likely near the existing
signature-scanned render/asset-realization functions this project's other
RE work has already touched, not yet searched for specifically.

## Real upload path found (`D3D9DeviceEx::UpdateTextureFromBuffer`) — and a genuine architectural blocker it surfaces (historical: DXVK-layer-only framing, superseded by the resolution above)

Read `dxvk/src/d3d9/d3d9_device.cpp`'s real upload chain in full:
`FlushImage` → `UpdateTextureFromBuffer` (~line 5286) is the actual place
D3D9-application-provided texel data reaches the GPU: it reads the raw
source bytes via `MapTexture`, and for the common (non-converted) path —
**confirmed to include DXT/BC-compressed formats, which flow through this
same generic path using `formatInfo->blockSize`/`elementSize`, not a
separate compressed-specific branch** — packs them (`util::packImageData`)
into a staging buffer and issues `ctx->copyBufferToImage` into the
destination `DxvkImage`. This settles the compressed-texture question from
item 1 below: **the bytes seen at this hook point for a DXT texture are
still compressed blocks**, not decoded pixels — any upscaler needs its own
decode step first, confirming the original concern was real, not
hypothetical.

**Real, previously-unidentified blocker, more significant than the
compressed-format question**: the destination `DxvkImage` (`pResource->
GetImage()`) is **not created here** — it's created once, up front, at
`D3D9CommonTexture` CONSTRUCTION time (`CreatePrimaryImage`, confirmed the
only assignment site for the `m_image` member in
`d3d9_common_texture.cpp`, no recreate/resize path found anywhere in that
file), sized directly from the D3D9 application's own requested
`pDesc->Width`/`Height`. By the time `UpdateTextureFromBuffer` runs and
the actual pixel/block content (and therefore any content-hash cache key)
is even knowable, **the image is already allocated at the ORIGINAL,
non-upscaled dimensions** — texture sampling itself doesn't care about
absolute resolution (a bigger image sampled with the same 0..1 UV space is
exactly how real texture upscaling already works at the GPU level, so
that part is fine), but there is no existing mechanism in this fork to
allocate the image bigger than what D3D9 asked for, because the size
decision happens before the content that would drive a cache lookup is
ever seen. **This is a real chicken-and-egg problem, not a detail to wave
past**: caching keyed by content hash (item 2 below) can only be resolved
AFTER the first upload, but the image size has to be decided BEFORE it.
Three honest options, none yet chosen: (a) recreate/replace the
`DxvkImage` at upscaled size on a cache hit detected during the first
upload (real, nontrivial new DXVK-internal-resource-recreation code — no
existing precedent for it in this fork was found this pass), (b) accept
this feature can only re-detail textures AT THEIR ORIGINAL DECLARED SIZE
(sharper/denoised, not actually higher pixel-count) unless (a) is solved
— which undercuts the actual stated goal ("cache them then have 2k+
assets"), or (c) key the cache off something knowable before upload (e.g.
a stable per-resource identifier this layer doesn't currently have
visibility into, not a content hash) so the upscaled size can be decided
at creation time instead of after the fact. **Real next step, before any
further design work here: determine which of (a)/(c) is actually
buildable** — this changes the entire shape of the feature and needs to
be resolved before the cache-key design in item 2 below is finalized.

## Status — scoping complete, REAL production hook point FOUND and LIVE-CONFIRMED, no substitution/upscale code written yet

**Locked design (2026-09-28, final update after live-testing
`FUN_1401bae80` against a real, full session):**
- **Target: ALL real `GfxImage` assets, menu/UI AND gameplay world/
  character/weapon content alike, in one unified feature — the earlier
  phase 1 (menus)/phase 2 (gameplay) split is OBSOLETE.** Live-confirmed:
  a 3636-hit real session captured both UI/HUD names
  (`scrollbar_arrow_up`, `flashbangicon`) AND real gameplay material
  names (`chemwar_russian_headgear_b_nml`/`_col`,
  `russian_military_shotgun_a_nml`/`_col`) through the exact same hook.
  Never touches the backbuffer/render targets (already handled by
  `InternalRenderScalePercent`, a separate mechanism).
- **Never modify `.iwd`/`.ff` on disk, ever** — runtime interception only,
  same "read-only game install, only our own injected code writes
  anything" policy as the rest of this project.
- **Hook point: CONFIRMED LIVE, this is the real production target —
  `FUN_1401bae80`** in `iw5sp.exe` (x64), a genuine, universal, per-image
  FILE LOADER (not the asset-interning layer): builds a real
  `"images/<name>"` path from a name pointer at a fixed struct offset
  (`param_1 + 0x20`), opens/reads the real `.iwi` container file,
  validates its own real magic header. Found by tracing real
  `CreateTexture` callers backward (not forward from asset-loading);
  live-tested across two real sessions (3636 hits in the fuller one) —
  every name read back clean and correct, covering both UI and gameplay
  content in one universal chain. `FindOrLoadAsset`'s own real x64 twin
  (`FUN_1400a5a20`, confirmed via signature/lock-pattern/caller-count
  match, `assetType 0xa`=`"image"` dumped from a real table at
  `0x1404c2430`) is ALSO real and working, and stays in the tree as an
  independently useful diagnostic/correlation tool — just no longer the
  feature's primary hook, since it only ever sees the smaller subset of
  images requested by name at runtime, not the universal set
  `FUN_1401bae80` sees.
- **Cache key**: the real image name read at `FUN_1401bae80`'s own
  `param_1 + 0x20` (confirmed available and correct) + a model-version/
  scale-factor stamp (exact format still open, small remaining design
  work).
- **Upscaler**: Real-ESRGAN-ncnn-vulkan (MIT/BSD, model weights BSD-3-Clause,
  both verified), Vulkan-compute-native, no CUDA/ONNX dependency — vendor
  as a nested subtree, same pattern as `MW32011DXVK`/MinHook/Streamline.
  NVIDIA RTX Neural Texture Compression checked and ruled out as a
  competing primary path (solves VRAM footprint, not detail addition) —
  possible later complementary use for compressing the cache itself.
- **Compressed (DXT/BC) source textures**: `FUN_1401bae80` reads and
  validates the raw `.iwi` file itself, so the real decode/decompression
  step is right there in the same function — decode once at this layer
  before upscaling; re-encode vs. upload-uncompressed is a real open
  cost/quality tradeoff, not a blocker.
- **First-use cost**: background-thread upscale queue (serve original
  texture until the cached upscale is ready, swap in on completion),
  reusing this project's own established one-thread-per-job convention —
  not yet designed in detail.
- **Disk footprint / default state**: strictly opt-in, off by default,
  same as every other pre-1.0 experimental feature — exact cache location
  and any "which textures actually get touched" accounting still open.

**Real next steps, in order**: (1) **DONE — LIVE-CONFIRMED, 2026-09-28.**
`Hook_FindOrLoadAssetX64`/`InstallFindOrLoadAssetImageDiagHookX64`
(`analog_input_hooks_x64.cpp`) installed and enabled cleanly, fired
exactly as designed: 19 real hits captured (18 image loads + the install
confirmation line), all real, correct, recognizable IW-engine built-in
default/placeholder image names — `$white`, `$black`, `$black_3d`,
`$black_cube`, `$gray`, `$identitynormalmap` (each seen 3 times,
consistent with the engine's own default-asset registration running
multiple times at startup/menu load). **This is definitive proof the x64
twin (`FUN_1400a5a20`) and the `assetType==0xa` correlation are both
correct, live, not just statically inferred** — no crash, no garbage
data, real names matching exactly the kind of asset the per-type
dispatch's own embedded error string (`"Could not load default asset
'%s'..."`) already predicted would exist for this type. **Scope split,
2026-09-28: the user confirmed this session was a real in-level play
session, not startup/menu-only, and that only built-in defaults showing
up (no real level content) is expected — `FindOrLoadAsset` is the right
mechanism for MENU/UI texture upscaling specifically ("we will use for
upscaling menus but still" [need real level content solved separately]).
This resolves the "is this a gap or a real blocker" question from the
first-draft framing below: it's neither — it's a real, correct scope
boundary. `FindOrLoadAsset(assetType==0xa)` is confirmed viable for
menu/UI images (matches x86's own precedent that menus/materials are
looked up by name through this exact function, `re_notes/iw5sp.md`'s
"Runtime material/texture capture" section) and ships as a real
sub-feature on its own. Real, in-level GAMEPLAY texture content (world
diffuse/normal maps etc.) most likely loads through a separate bulk
zone-load path (a `DB_LinkXAssetEntry`-class function registering a
level's own `GfxImage` entries directly from its `.ff`, without an
interning "find or load by name" call) — genuinely unresolved, a
separate, harder RE target for a future round, not blocking menu-texture
upscaling from proceeding now.**

**Phase 2 groundwork started same day, direct instruction ("we need to
trace the real callers by tracing back from the already-established
backbuffer")**: rather than a second forward-tracing attempt from the
asset-loading side, this traces backward from the real, already-hooked,
already-confirmed-working `CreateTexture` device call every texture
(regardless of load path) must eventually reach. New
`AssetCapture_RecordCreateTextureFirstNCaller`/
`AssetCapture_DumpFirstNCreateTextureCallersIfDue` (`asset_capture.cpp`/
`.h`) captures a real call stack for the first 40 `CreateTexture` calls
this session UNCONDITIONALLY — not gated behind the existing storm
diagnostic's 200-calls-in-200ms burst threshold, which ordinary/steady
level loading may never cross — resolved to module-relative offsets
against the real game module for direct Ghidra lookup once dumped.
Build-verified (x64 Release, 0 errors), deployed; **not yet live-tested**
— needs a fresh in-level session so the buffer actually fills and dumps.
**LIVE-TESTED SAME DAY — major breakthrough, likely resolves Phase 2
entirely.** The user played a real in-level session; the diagnostic
fired, all 40 calls captured. Real, decisive finding: **the exact same
6-frame call chain (`0x1401BA17B → 0x1401BAB85 → 0x1401BADEC`/`0x1401BAE04
→ 0x1401BAFF0 → 0x1401B949D → 0x14009251B`) is shared by BOTH the tiny
built-in-default-shaped textures (64x64, 16x16, 1x1) AND real, clearly
non-default level content (256x256, 1024x256, 512x256, all real DXT1/
DXT3/DXT5 FourCCs)** — proving this chain is a universal choke point
every image passes through, regardless of whether it arrived via
`FindOrLoadAsset` or a separate bulk zone-load path. Decompiling the
shared frames found the real answer: **`FUN_1401bae80`** (frame[3],
`0x1401BAFF0` falls inside its body) is a genuine, generic, per-image
FILE LOADER — not decode/upload plumbing:
```c
iVar2 = FUN_1402ca430(local_48, 0x40, "%s%s%s", "images/",
                       *(undefined8 *)(param_1 + 0x20), &DAT_1404138e4);
if ((iVar2 < 0) || (lVar3 = (*param_2)(local_48, &local_res8), lVar3 < 0))
    return 0;
// ... validates the real "IWi" + version-8 magic header on the loaded file
if (local_68!='I' || local_67!='W' || local_66!='i' || local_65!='\b') { ... }
```
This builds the real `"images/<name>"` file path from a name pointer at
a FIXED struct offset (`param_1 + 0x20`) and opens/reads the real `.iwi`
container file from disk, validating its actual magic header before
decoding. **This is a stronger, more universal hook point than
`FindOrLoadAsset` for this feature**: it fires for every real image file
load regardless of caller (default registration, `FindOrLoadAsset`-driven
lookup, or whatever bulk zone-load path feeds level content into this
same chain), and gives BOTH the real name (at a known, fixed offset) AND
direct access to the raw file/decode pipeline in one place — closer to
"the" real answer than any prior candidate in this document. **Not yet
confirmed which of frames 0-2 (`0x1401BA17B`/`FUN_1401ba0c0`,
`0x1401BAB85`/`FUN_1401bab10`, `0x1401BADEC`) is the actual entry point
worth hooking vs. `FUN_1401bae80` itself** — `FUN_1401ba0c0` turned out to
be DXT-format-specific mip-decode dispatch (calls different sub-decoders
per compression type), not name-bearing; `FUN_1401bae80` is the real
name+file-load function and the strongest current hook candidate.

**(2) DONE, deployed, awaiting live confirmation.**
`Hook_ImageFileLoadX64`/`InstallImageFileLoadDiagHookX64`
(`analog_input_hooks_x64.cpp`) hooks `FUN_1401bae80` directly (signature
independently verified unique via a wildcard-aware regex scan against
the offline binary — two genuine RIP-relative LEA operands wildcarded,
the RSP-relative stack-spill MOVs kept fixed per the same false-positive
lesson already documented elsewhere in this project). Reads the real
name pointer at `param_1 + 0x20` SEH-guarded before calling through
(matching `PLUGIN_API.md`'s own `ReadMemory` convention for raw-offset
struct reads whose safety isn't yet independently proven for every
caller), logs the first 400 hits. Confirmed standard MS x64 fastcall via
the prologue's own `mov rbx,rdx` — plain MinHook C++ detour. Build-
verified (x64 Release, 0 errors), deployed.

**(1) and (3) CONFIRMED, 2026-09-28 — this is the headline result of the
whole texture-upscale-cache scoping effort.** Two real sessions checked.
First (400-hit cap, since removed): all real, clean, sane names, but
every one UI/HUD/menu-adjacent (`scrollbar_arrow_up`, `checkbox_current`,
`flashbangicon`, `rain_drop`) — the cap had filled during early UI/HUD
population. Cap removed (`Hook_ImageFileLoadX64`, same "unbound it for
dev data" treatment `Hook_FindOrLoadAssetX64` already got), then a fuller
session captured 3636 real hits. **Real, unambiguous GAMEPLAY
character/weapon material names appear throughout**: `chemwar_russian_
headgear_b_nml`/`_col`, `chemwar_russian_alpha_a_nml`/`_col`,
`russian_military_shotgun_a_nml`/`_col`, `russian_naval_eye_a_col` —
`_nml`=normal map, `_col`=diffuse/color map, the standard real-asset
naming convention for actual game-content character/weapon skins, not
UI. **This resolves the phase 1/phase 2 split entirely — `FUN_1401bae80`
is confirmed live to be the universal answer for BOTH menu/UI textures
AND real gameplay world/character/weapon content, in a single hook.** No
separate mechanism is needed for gameplay textures after all; the
earlier `FindOrLoadAsset`-only design's inability to see level content
was a real limitation of THAT specific mechanism, not of the feature as
a whole — this hook sits upstream of it and sees everything.
**Genuine bonus confirmation, same data**: `~`-prefixed, hash-suffixed
names (`~chemwar_russian_alpha_a_spc-~b33ebc5a`,
`~russian_military_shotgun_a_s~f7f003ff`) are real, live proof of this
project's own earlier prediction (`re_notes/iw5sp.md`'s "Runtime
material/texture capture" section, 2026-08-17) that some real material/
image names are procedurally generated at runtime and can never be
extracted by static zone-dump analysis alone — directly observed here,
not just theorized. **This single hook is now the confirmed, real,
production hook point for the entire texture-upscale-cache feature** —
the earlier two-phase design (menus now, gameplay later) is obsolete;
both ship together, off the same mechanism.

**Scale factor, locked 2026-09-28: a percentage/multiplier of each
texture's OWN original resolution, not a fixed absolute target.** Direct
instruction ("lets do it on a percentage or multiplier of the original
res") — matches this project's own already-established
`InternalRenderScalePercent` convention exactly (a relative scale, not a
hardcoded resolution), and is the only sane design here anyway given
`GfxImage` assets span everything from 1x1 utility textures to large
world diffuse maps — a single fixed target resolution would be wrong for
the vast majority of them. **Real config key, direct instruction: `[Video] TextureRenderRes`, a
multiplier-string value (e.g. `4x`)** — not a plain percentage integer
like `InternalRenderScalePercent` uses; a distinct, purpose-fit format
for this feature specifically. Applied uniformly per-texture at
cache-build time (every cached texture gets upscaled by this same
multiplier relative to its own original size, not a fixed absolute
target).

**Real architectural simplification, found while designing the
substitution mechanism**: the earlier "chicken-and-egg" problem (the
destination texture's size has to be decided before its content is known)
assumed substitution had to happen at or after the D3D9/DXVK
`CreateTexture` call. **`FUN_1401bae80` runs BEFORE any D3D9 texture
exists at all** — it's the raw file loader, not the GPU-resource creator.
This means the real, clean substitution point is the FILE-READ level, not
the texture-creation level: intercept the raw bytes this function reads
(or the path it constructs, `"images/<name>"`) and, on a cache hit,
supply an upscaled `.iwi`-FORMATTED replacement (same real file format,
larger real dimensions baked into ITS OWN header) instead of the
original file. `FUN_1401bae80`'s own existing, already-correct decode/
validate/mip-extract logic then handles everything downstream exactly as
it already does for any real, larger IWI file — no reimplementation of
IWI parsing, no DXVK-side image-resize logic, no separate "tell
`CreateTexture` a bigger size" mechanism needed at all, since whatever
calls `CreateTexture` further downstream will naturally request the
larger size because THIS function reported it from the substituted
file's own real header. **This is a real, load-bearing design win** — it
was worth re-deriving now that the confirmed hook point sits upstream of
D3D9 entirely, rather than assuming the old constraint (found against a
downstream DXVK-layer hook) still applies unchanged. **CORRECTION, same day, from a fuller decompile of `FUN_1401bae80`'s
real body**: the "substitute the raw `.iwi` file bytes and let the
existing header-driven logic handle everything for free" framing above
is WRONG, or at least incomplete — real pixel width/height are NOT read
from the 32-byte `.iwi` header this function reads at all. That header
(`local_68`/`local_67`/`local_66`/`local_65` = the real `"IWi"` magic +
version-8 byte; `local_64` = a flags byte; `local_5e`/`local_5c` = two
shorts, likely streamed/unstreamed mip-count fields; `local_58[4]` = four
ints, confirmed via `iVar2 = local_58[uVar8]` and the corruption check
`local_58[0] != lVar3` (`lVar3` = the real file's own total byte size) to
be PER-MIP-LEVEL COMPRESSED DATA SIZES, not dimensions at all — matches
IWI's real streaming design (only the smallest few mip levels' byte
sizes are stored for on-demand streaming). **Real pixel dimensions are
written elsewhere**, onto `param_1` itself, by `FUN_1401ba0c0`
(`*(short*)(param_1+0x18)=width`, `+0x1a`=height, `+0x1c`=depth) — a
sibling function in the same decode chain, fed by data that itself
traces back to the `GfxImage` asset's OWN pre-populated fields, set at
zone/fastfile-parse time, BEFORE `FUN_1401bae80` ever runs. **This means
substituting the `.iwi` file's raw bytes alone does NOT resize the
eventual texture** — the width/height driving `CreateTexture` are
already fixed by the time this function runs. **The real fix is still
available from this same hook, just requires one more explicit step**:
since the hook already owns `param_1` (the real struct pointer), it can
directly overwrite `param_1+0x18`/`+0x1a` (and whatever mip-count/size
fields the substituted, upscaled `.iwi` payload's own real header
correctly describes) to the real upscaled dimensions, in addition to
supplying the upscaled pixel/mip data itself — more explicit work than
last turn's overclaim, but still fully achievable at this one hook point,
no DXVK-side resize machinery needed. **Real next steps, updated**: (1)
map `param_1`'s own struct layout precisely (confirmed fields so far:
`+0xb`=a flags byte, `+0xc`=current/target mip level, `+0x18`/`+0x1a`/
`+0x1c`=width/height/depth, `+0x20`=the real image name pointer) and
confirm exactly which fields the substitution needs to set, (2) RE
`param_2`'s own real implementation (the file-read callback — presumably
reads from the real `.iwd`/`.ff` archive, not a loose disk file) to
understand what "supplying a substituted file" would actually require,
prototype ncnn-vulkan
completely standalone (a small test harness, not wired into the mod) to
confirm it runs against this machine's real GPU via Vulkan, (4) design the
background-thread queue and cache file format, (5) only then write the
actual hook + substitution code. The `FindOrLoadAsset` hook
(`Hook_FindOrLoadAssetX64`) stays in the tree as a real, working,
independently-useful diagnostic/correlation tool, just no longer the
feature's own primary/only hook point.

### File-read callback traced — genuinely deeper than expected, paused here

`FUN_1401bae80`'s own two real callers (`FUN_1401bae70`/`FUN_1401bb260`,
both trivial one-line wrappers) supply two different concrete callbacks:
`FUN_1402b4240` (tries the path as given, and on failure resolves a
second candidate path via `FUN_1402edc90` — a real, generic
search-path/prefix-resolution helper — and retries) and `FUN_1402b4180`
(sets a global flag then calls straight through). **Both ultimately call
the same real function, `FUN_1402b42c0`** — which turned out to be a
genuine, non-trivial VIRTUAL FILE SYSTEM layer, not a simple file-open:
it hashes the requested path, walks a real linked-list hash table
(`DAT_1426563e0`) of already-loaded zones' own asset entries doing a
case-insensitive path comparison, and only falls through to (presumably)
real disk I/O deeper in the function if no zone already owns that path —
this is a real "check if a zone already has this asset resident before
touching disk" mechanism, considerably more involved than anticipated.
**Paused the deep-dive here rather than fully mapping this VFS layer in
one pass** — real, additional, non-trivial RE scope; a natural
checkpoint to confirm direction with the user before spending several
more rounds on general file-system internals that go beyond what the
substitution design strictly needs. **What's still needed, concretely,
once resumed**: not necessarily the full VFS mechanism — just confirming
whether a loose, correctly-named `.iwi` file placed in the right search
path is picked up by this existing mechanism ahead of the zone's own
copy (which would mean the substitution could be a simple "drop a file"
operation using the game's own existing override support, no hook-level
byte substitution needed at all). That specific question is NOT yet
answered and is the real, valuable thing to resolve next, rather than
mapping every branch of `FUN_1402b42c0` itself.

### Both halves done, 2026-09-28 — static confirmed, empirical hook deployed

**Static half, done**: decompiled `FUN_1403ac410` (reached from
`FUN_1402b42c0`'s mod-folder-prefixed branch) and confirmed it's a
genuine CRT `fopen`+`fread` wrapper — real `_LocaleUpdate` construction,
real errno-style checks against `0xfde9`. This is real, standard disk
file I/O, not zone-archive-only access. Separately confirmed the classic
CoD `fs_game` mod-folder dvar string exists in the binary
(`grep`-equivalent byte search, 1 match) — converging evidence this
engine supports the standard mod-folder override convention.

**Empirical half, deployed**: `Hook_LooseFileOpenX64`/
`InstallLooseFileOpenDiagHookX64` (`analog_input_hooks_x64.cpp`) hooks
`FUN_1403ac410` directly, logging the real resolved path string it's
asked to open for every real image load. Signature independently
verified unique against the offline binary. Build-verified (x64 Release,
0 errors), deployed. **Not yet live-tested** — needs a real session so
the log can show the actual resolved paths, which will reveal whether
(and how) a loose file could override zone content for this feature.

### Empirical result, 2026-09-28 — DECISIVE NEGATIVE: the loose-file override does NOT fire under default conditions

Checked `proxy_d3d9.log`: `[x64-loosefileopen-diag]` logged ONLY its own
"installed" line — `FUN_1403ac410` never fired even once, despite the
same session logging 4481 real hits on `FUN_1401bae80`
(`[x64-imagefileload-diag]`), confirming this isn't a broken hook (the
signature resolved and installed fine; it simply never got called).
**Real conclusion: the mod-folder/loose-file CRT `fopen` path inside
`FUN_1402b42c0` is never reached under default play** — almost certainly
because `fs_game` is empty (the default, no mod folder active), so
`FUN_1402b42c0`'s own mod-folder-prefixed branch never even attempts a
path/open in this configuration. **This closes the "maybe substitution
is free, just drop a file" hope from earlier this session — it is NOT
free under normal conditions.** Setting `fs_game` ourselves to force this
path is not a good option either (changes other real engine behavior
tied to that dvar, a much bigger and riskier lever than intended). **Real
design conclusion**: the texture-upscale-cache feature needs an ACTIVE
hook-based substitution after all — most likely at `FUN_1401bae80`
itself (already hooked, already confirmed to own `param_1` and see the
real name before the zone-archive read happens), not a passive
file-drop. This is now the settled design; no more time should be spent
chasing the loose-file-override shortcut for this feature.

## ncnn vendored; standalone Vulkan smoke test PASSED; a real CRT-linkage decision now needed before wiring into the mod

**Vendored**: real, official, prebuilt `Tencent/ncnn` Windows VS2022 x64
static SDK (tag `20260526`, Vulkan-enabled — confirmed via `command.h`/
`gpu.h`/`vulkan_header_fix.h` plus bundled `glslang`/`SPIRV` shader-compile
libs, not a CPU-only build). Headers committed
(`proxy_d3d9/third_party/ncnn/include/`); the ~46MB static libs are
gitignored and fetched via a new `fetch_ncnn.bat`, same size-driven
(not license-driven — both ncnn's own BSD-3-Clause and glslang's own
permissive terms are fine) treatment this project already gives large
binaries after this session's own repeated lessons about committing them.

**Standalone smoke test: PASSED, real GPU confirmed.** A small,
deliberately-standalone test program (`ncnn::get_gpu_count()`/
`get_gpu_info()`/`get_gpu_device()`, not wired into the mod) correctly
detected BOTH real GPUs on this dev machine via Vulkan — an NVIDIA
RTX 2080 Ti (`vendor_id=0x10de`, 8 compute queues, real fp16/int8/bf16 and
cooperative-matrix capability flags) and an AMD Radeon integrated GPU
(`vendor_id=0x1002`, 4 compute queues) — and successfully acquired a real
`VulkanDevice` handle for GPU 0. This is genuine, live confirmation the
vendored ncnn build works correctly with Vulkan on real, mixed hardware,
not just a "did it link" check.

**Real, unresolved architectural decision found in the process**: the
vendored `ncnn.lib` is built with the DYNAMIC CRT (`/MD`) — confirmed the
hard way, via a real `LNK2038: mismatch detected for 'RuntimeLibrary'`
linker error against a default `/MT`-flagged test compile, fixed for the
standalone test by adding `/MD`. **This project's own `proxy_d3d9.vcxproj`
uses the STATIC CRT** (`RuntimeLibrary=MultiThreaded` for Release) — the
same mismatch would block a direct `ncnn.lib` link into the main mod as-is.
Three real options, none chosen yet:
1. **Switch `proxy_d3d9.vcxproj` itself to the dynamic CRT** (`/MD`,
   `MultiThreadedDLL`) — simplest, but a real, project-wide behavior
   change: the shipped `d3d9.dll` would then depend on the MSVC
   redistributable being present on a player's system, rather than being
   fully self-contained the way it is today. Worth flagging as a real
   trade-off, not a free choice.
2. **Rebuild ncnn from source with `/MT`** to match the mod's existing
   static-CRT convention exactly — more setup work (ncnn's own CMake
   build, not just downloading a prebuilt zip), but keeps the mod's
   current "one self-contained DLL, no external CRT dependency" property
   intact.
3. **Load ncnn as its own genuinely separate DLL**, communicating through
   ncnn's own real C ABI (`c_api.h`, present in the vendored headers) —
   matches this project's existing precedent for `MW32011DXVK`/NVIDIA
   Streamline (separate shipped binaries, not statically linked into the
   main `d3d9.dll`), sidesteps the CRT question entirely since each DLL
   carries its own CRT, but means shipping an additional real binary
   alongside the mod for the first time for a non-graphics-API component.

**Not yet decided — real next step**: pick one of these three before
writing any real integration code, since it changes both the build setup
and (for option 1) a real, player-visible dependency change worth a
direct decision rather than a quiet default.

## Rebuilding ncnn from source with /MT (user's chosen option), in progress

Direct decision, via `AskUserQuestion`: rebuild ncnn from source with the
static CRT rather than switching the mod to `/MD` or shipping ncnn as a
separate DLL — keeps `d3d9.dll` fully self-contained, no new player-visible
dependency.

Real, concrete steps taken: cloned `Tencent/ncnn` source at the same
pinned tag (`20260526`) as the prebuilt SDK already vendored, initialized
the real `glslang` submodule (Vulkan shader-compile dependency, not
bundled in a shallow clone by default), then hit and fixed a genuine
Windows path-length issue (MSBuild's FileTracker failing under the deeply
nested scratchpad temp path — moved the source/build to a short path,
`D:\ncnn_src`, not a project-tree location, purely a local build
workspace). Confirmed via `NCNN_BUILD_WITH_STATIC_CRT` (ncnn's own real
CMake option, default OFF, which is why the official prebuilt SDK uses
`/MD`) and `NCNN_VULKAN=ON` in `CMakeCache.txt` after configuring with
Visual Studio 18 2026's own generator. Real, useful side note: ncnn
implements its own in-house Vulkan function loader rather than statically
linking `vulkan-1.lib` — the explicit `Vulkan_LIBRARY`/`Vulkan_INCLUDE_DIR`
hints passed to CMake were reported unused for exactly this reason, not
an error; likely means the eventual mod integration won't need to link
`vulkan-1.lib` directly either, since ncnn loads it dynamically itself at
runtime the same way DXVK's own Vulkan loader does.

Release x64 build kicked off; not yet complete as of this entry. Once
done: re-run the standalone smoke test against this newly-built,
`/MT`-matching `ncnn.lib` to confirm it still detects both real GPUs
correctly (same test program already proven against the prebuilt `/MD`
build), then vendor this `/MT` build in place of the prebuilt one.

## RESOLVED — ncnn now vendored as a from-source, /MT, CRT-compatible build

Real from-source build completed and confirmed working: `NCNN_VULKAN=ON`,
`NCNN_BUILD_WITH_STATIC_CRT=ON`, x64 Release, Visual Studio 18 2026
generator. Re-ran the standalone smoke test against the new `ncnn.lib` —
identical result to the earlier `/MD` prebuilt test (both real GPUs
correctly detected via Vulkan, working `VulkanDevice` acquired) — and it
now links cleanly under `/MT` with zero CRT mismatch. `proxy_d3d9/third_party/ncnn/`
updated: `build_ncnn.bat` (real build-from-source script, replaces the old
`fetch_ncnn.bat` prebuilt-zip downloader) and the four CMake-generated
headers refreshed from this exact build. The CRT-linkage question raised
earlier this session is now fully closed — `ncnn.lib` is ready to link
directly into `proxy_d3d9.vcxproj` with no build-setting changes needed.

**Vendoring/prototyping phase is now complete.** Everything the original
"real next steps" list called for is done: hook point found and
live-confirmed (`FUN_1401bae80`), cache key confirmed, scale-factor config
decided (`TextureRenderRes=4x`), the loose-file-override question closed
(decisively no), and ncnn-vulkan vendored and proven working in a
CRT-compatible form. What remains is genuine implementation, not more
scoping/RE: design the actual cache file format and background-thread
queue, then write the real hook + substitution code at `FUN_1401bae80`
(overwriting `param_1`'s width/height fields per the earlier corrected
design, decoding/upscaling/re-encoding the `.iwi` payload).

## DEFINITIVE HEADER LAYOUT, byte-verified via raw disassembly — reverses the prior "must overwrite param_1 manually" correction

Before writing any real substitution code, went back to verify the exact
byte layout of `FUN_1401bae80`'s own 32-byte `.iwi` header read, since
`FUN_1401bab10` (reached via the mip-decode dispatch) turned out to read
short fields directly from that same header — a discrepancy with the
prior round's conclusion that width/height live only on `param_1`, set
independently of the file. Pulled the RAW DISASSEMBLY (not just decompiler
pseudocode, which had already misled this investigation once) of
`FUN_1401bae80` to get exact stack-relative byte offsets, cross-referenced
against `FUN_1401bab10`'s own field reads.

**Real, disassembly-confirmed 32-byte `.iwi` header layout** (offsets
relative to the header's own base):
- `0x00-0x03`: `"IWi"` + version byte `0x08` (real magic, already known)
- `0x04`: flags byte (already known, `local_64`)
- `0x05-0x09`: unaccounted/padding (not needed for this feature)
- **`0x0A-0x0B`: WIDTH** (int16) — confirmed via `FUN_1401bab10` reading
  `*(short*)(param_2+0xa)` and feeding it into
  `*(short*)(param_1+0x18) = width` (`FUN_1401ba0c0`'s own write, already
  mapped as the width field two rounds ago)
- **`0x0C-0x0D`: HEIGHT** (int16) — `*(short*)(param_2+0xc)` →
  `*(short*)(param_1+0x1a)`
- **`0x0E-0x0F`: DEPTH** (int16) — `*(short*)(param_2+0xe)` →
  `*(short*)(param_1+0x1c)`
- `0x10-0x1F`: four int32 per-mip-level compressed data sizes (already
  confirmed, `local_58[0..3]`)

All three dimension shorts get right-shifted by the current streamed mip
level (`FUN_1401bab10`'s own `>> (bVar1 & 0x1f)`) to compute each
individual mip's own real dimensions from this one base width/height/
depth — standard mip-chain math, confirmed correct and expected.

**This REVERSES the prior round's correction, which was itself wrong.**
Width/height/depth are NOT set independently on `param_1` from separate
zone-load-time `GfxImage` metadata — they're read DIRECTLY from this same
32-byte header, the exact header this hook already fully controls (reads,
validates, and could substitute). **The original, very first design idea
from earlier this session — supply a real, correctly-formatted, larger
`.iwi` payload and let `FUN_1401bae80`'s own existing decode logic handle
everything downstream "for free" — was correct all along.** No manual
`param_1+0x18/0x1a/0x1c` overwrite is needed; a substituted `.iwi` file
with its own real, bigger width/height baked into header offsets
`0xA`/`0xC` (plus correctly recomputed per-mip sizes at `0x10-0x1F` for
the new resolution) is sufficient on its own.

**Real lesson worth keeping**: decompiler pseudocode variable-naming
inference (`local_58`/`local_5e` etc.) led this investigation to a wrong
conclusion once already (assuming `local_58[4]` was the ONLY structured
data in the header, missing that `0xA`/`0xC`/`0xE` were separately
meaningful dimension fields) — the raw disassembly with real stack
offsets is what actually resolved it correctly, cross-referenced against
a second function's own field reads into the same buffer. This project's
own standing "trust native RE over assumption" discipline paid off here.

**Design status: the cache/substitution mechanism is now fully specified
and simpler than previously thought.** Real next step: design the cache
file's own real `.iwi` container format (reuse the real header/mip-size
layout just mapped, computing new per-mip sizes for the upscaled
resolution) and the substitution point (intercepting the raw bytes
`FUN_1401bae80` reads via `local_res8`/the read callback, before its own
header-validate/decode logic runs).

## FINAL substitution architecture — clean, two-hook design, no VFS spoofing needed

Resolved the last open question: `local_res8` (the "handle" the read
callback returns) is a real INTEGER INDEX into a global file-handle table
(`DAT_1426563f0`/`DAT_142656418`, 0x138-byte stride — the SAME table
`FUN_1402b42c0`'s own zone-lookup path indexes), not a raw memory pointer.
`FUN_1402b5ec0(dest, size, handleIndex)` is the shared low-level "read N
bytes via this handle" primitive both the 32-byte header read and the
mip-data read (inside `FUN_1401bae80`) call — it dispatches per-handle to
either a real streaming read (`FUN_14023f680`) or a different, likely
in-memory-backed path (`FUN_1403157c0`), selected by a flag in the handle
record. **Real, deliberate design decision: don't try to understand or
spoof this handle-table's internal record layout at all.** Instead:

1. **`Hook_ImageFileLoadX64`** (already built, currently diagnostic-only)
   gains real logic: on entry, check the real image name (`param_1+0x20`)
   against the upscale cache. On a hit, arm a small, scoped "active
   substitution" state (a pointer+length into the cached, real,
   correctly-formatted `.iwi` blob, plus a read cursor) before calling
   through to the unmodified real `FUN_1401bae80`. Cleared again once
   that call returns.
2. **A new hook on `FUN_1402b5ec0`** (the shared byte-read primitive):
   when the "active substitution" state is armed, ignore the real handle
   entirely and `memcpy` from the cached buffer at the current cursor
   instead, advancing it and returning the real byte count (matching the
   real function's own return convention) — otherwise call straight
   through, unmodified, exactly as today.

This reuses `FUN_1401bae80`'s own already-correct, already-live-verified
decode/header-validate/mip-dispatch logic completely unmodified — the
cached `.iwi` blob just needs to be a real, valid, correctly-formatted
file matching the byte layout mapped above (magic, flags, real upscaled
width/height/depth at `0xA`/`0xC`/`0xE`, correctly recomputed per-mip
sizes at `0x10-0x1F`, followed by the real encoded mip data). No
DXVK/D3D9-layer resize logic, no manual `param_1` field writes, no
understanding of the real VFS/handle-table internals needed at all — the
same "hook a shared low-level primitive, gate behavior via outer-scope
state" pattern this project already uses successfully elsewhere (e.g.
`asset_capture.cpp`'s own material-name capture-stack correlation).

**This is now a complete, concrete, implementable design** — every open
architectural question from this entire research effort is resolved.
Real remaining work is pure implementation: (1) the cache file format on
disk (keyed by the real image name, storing a ready-to-substitute `.iwi`
blob), (2) the actual ncnn-based upscale-and-encode pipeline that
produces those cached blobs (background thread, first-use-miss handling),
(3) the two hooks described above, wired together.

## `.iwi` encoder implemented and verified byte-exact

Found and reused a much better source than more binary RE: this
project's own `tools/iw5oat` fork (OpenAssetTools) already has a complete,
real, tested IWI-v8 writer (`IwiWriter8.cpp`/`IwiTypes.h`) — exactly the
format version this game uses. Its `image::iwi8::IwiHeader` struct
independently cross-validates this session's own disassembly-derived
header layout byte-for-byte (`flags`(u32)/`format`/`unused`/
`dimensions[3]`/`fileSizeForPicmip[4]`), and its real write logic
resolved two details pure disassembly hadn't fully pinned down:
`fileSizeForPicmip` entries are a CUMULATIVE running total (header size +
all mips written so far), not each mip's own individual size, and mips
are written smallest-first.

Ported into a standalone, no-STL (`malloc`-based, matching
`asset_capture.cpp`'s own established shipped-code convention) encoder:
`proxy_d3d9/src/texture_upscale_iwi_writer.h`/`.cpp`,
`TextureUpscaleIwi::EncodeIwi8()`. Verified via a standalone test program
(not shipped) — every output byte checked against hand-computed expected
values (magic/version, format, width/height/depth, all four cumulative
`fileSizeForPicmip` entries, mip data ordering) and all matched exactly.
Build-verified as part of the main `proxy_d3d9.vcxproj` (x64 Release, 0
errors, `static_assert`s on the real 28-byte header size passed).

**Not yet wired into anything** — this is the encoder only. Real
remaining pieces: the ncnn-based upscale pipeline that decodes a real
source `.iwi`'s mip data, runs it through Real-ESRGAN, and re-encodes DXT
mips to feed this encoder; the cache file format/lookup on disk; and the
two substitution hooks (`Hook_ImageFileLoadX64`'s real cache-check logic,
the new hook on `FUN_1402b5ec0`) designed in the round above.

## Cache lookup/storage implemented and verified working

`proxy_d3d9/src/texture_upscale_cache.h`/`.cpp`
(`TextureUpscaleCache::TryLoadCachedUpscaledIwi`/`StoreUpscaledIwi`):
real image name + scale multiplier maps to a real `.iwi` file under
`<gameDir>\texture_upscale_cache\` (the filename itself IS the cache
key — sanitized name + scale, so a `TextureRenderRes` change naturally
invalidates old entries by no longer matching any filename, no explicit
invalidation pass needed). Read side deliberately synchronous (a hit
replaces a real synchronous disk/archive read the game already does at
this exact call site, so no new stall risk); write side uses a
temp-file-then-atomic-rename pattern so the synchronous reader can never
observe a half-written cache file, matching `asset_capture.cpp`'s own
established write-safety discipline without needing a second background
queue here (the real caller — the not-yet-built ncnn pipeline — already
has to run on its own background thread given inference cost).

Verified via a standalone round-trip test: miss before store, correct
store, exact byte match on read-back, a different scale factor correctly
misses, and filename sanitization confirmed correct for a name containing
`/`, `~`, and `-` (matching real observed name shapes from the earlier
live diagnostic rounds). Build-verified as part of the main
`proxy_d3d9.vcxproj`.

**Two of the four remaining implementation pieces are now done** (the
`.iwi` encoder, the cache lookup/storage). Remaining: the ncnn-based
upscale pipeline (decode source mips → Real-ESRGAN inference → re-encode
DXT mips → `EncodeIwi8` → `StoreUpscaledIwi`) and the two substitution
hooks (`Hook_ImageFileLoadX64`'s real cache-check call, the new hook on
`FUN_1402b5ec0`).

## Substitution hooks wired end to end; a real, live-testable synthetic test entry is staged

`Hook_ImageFileLoadX64` now does a real cache check (`TextureUpscaleCache::
TryLoadCachedUpscaledIwi`) and arms a `thread_local` substitution buffer;
the new `Hook_ReadBytesSubstitutionX64` (`FUN_1402b5ec0`) serves bytes from
it. Both hooks build-verified and deployed. `g_modConfig.textureRenderRes
> 1` gates the whole mechanism, so this is completely inert at the
default `1x`.

**Staged a real, live-testable synthetic entry** (a one-off tool, not part
of the shipped mod, `make_test_cache_entry.cpp`): a deliberately obvious
32x32 solid bright magenta, uncompressed RGBA, single-mip test texture,
stored via the real `StoreUpscaledIwi` path as
`texture_upscale_cache\3_cursor3_4x.iwi` — targeting `"3_cursor3"`, a
real, confirmed-live, highly visible UI element (the custom cursor, hit
#1 in the earlier full-session `FUN_1401bae80` diagnostic). Set
`TextureRenderRes=4x` live in `mw3ncp_config.ini`. This deliberately
tests the PLUMBING, not upscale quality/correctness — no ncnn involved at
all yet — if the substitution hooks work correctly, the mouse cursor
should render as a solid magenta square instead of its real art the next
time the game runs. **Not yet live-tested** — needs the user to launch
and confirm (or report a crash/wrong behavior, either a real, valuable
result at this stage).
