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

## Blocker resolution, 2026-09-28: hook the engine's own asset-load layer, not DXVK's D3D9 layer

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

## Status — scoping complete, no code written yet

**Locked design (2026-09-28):**
- **Target**: source asset textures (`.iwd`/`.ff` `GfxImage` assets) only —
  never the backbuffer/render targets (already handled by
  `InternalRenderScalePercent`, a separate mechanism).
- **Never modify `.iwd`/`.ff` on disk, ever** — runtime interception only,
  same "read-only game install, only our own injected code writes
  anything" policy as the rest of this project.
- **Hook point: the native engine's `GfxImage`→D3D9-texture realization
  function**, not DXVK/D3D9's own `CreateTexture`/`UpdateTextureFromBuffer`
  — gives a real, stable, free cache key (the asset's own name) BEFORE any
  texture is created, letting a cache hit's upscaled dimensions be passed
  into `CreateTexture` from the start. No DxvkImage resize/recreate
  machinery needed. **Not yet located in `iw5sp.exe`** — real next RE step.
- **Cache key**: `GfxImage` name + a model-version/scale-factor stamp
  (exact format still open, small remaining design work).
- **Upscaler**: Real-ESRGAN-ncnn-vulkan (MIT/BSD, model weights BSD-3-Clause,
  both verified), Vulkan-compute-native, no CUDA/ONNX dependency — vendor
  as a nested subtree, same pattern as `MW32011DXVK`/MinHook/Streamline.
  NVIDIA RTX Neural Texture Compression checked and ruled out as a
  competing primary path (solves VRAM footprint, not detail addition) —
  possible later complementary use for compressing the cache itself.
- **Compressed (DXT/BC) source textures**: decode once at the `GfxImage`
  hook layer before upscaling; re-encode vs. upload-uncompressed is a real
  open cost/quality tradeoff, not a blocker.
- **First-use cost**: background-thread upscale queue (serve original
  texture until the cached upscale is ready, swap in on completion),
  reusing this project's own established one-thread-per-job convention —
  not yet designed in detail.
- **Disk footprint / default state**: strictly opt-in, off by default,
  same as every other pre-1.0 experimental feature — exact cache location
  and any "which textures actually get touched" accounting still open.

**Real next steps, in order**: (1) locate the native `GfxImage`-to-D3D9-
texture realization function in `iw5sp.exe` (the actual hook point — this
is the one piece every other design decision above depends on), (2)
prototype ncnn-vulkan completely standalone (a small test harness, not
wired into the mod) to confirm it runs against this machine's real GPU via
Vulkan, (3) design the background-thread queue and cache file format, (4)
only then write the actual hook + substitution code.
