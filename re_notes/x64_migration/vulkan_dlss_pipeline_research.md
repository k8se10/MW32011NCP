# Vulkan/DXVK + native DLSS (Streamline) integration — research (started 2026-09-23)

**Purpose**: deep, dedicated research supporting the locked architecture decision
recorded in `known_issues_x64.md` issue #2 — a real, native `[Video] GraphicsApi`
selector (`Legacy D3D9` default / `Vulkan` opt-in), where `Vulkan` mode runs an
in-process DXVK D3D9-to-Vulkan translation layer feeding NVIDIA's real Streamline
SDK directly, with NO ReShade and NO third-party addon framework (RenoDX,
DLSS5-Feeder, LumeniteFX) in the chain — this project's own existing native D3D9
hook already makes that middleware redundant, per direct user correction this
same session. This doc is the technical deep-dive backing that decision;
`known_issues_x64.md` issue #2 stays the short, current-status summary.

---

## 1. Real, confirmed prior art directly on THIS game

Two release files from the "MW3 Remastered" Nexus mod (mod #10,
`nexusmods.com/callofdutymodernwarfare3/mods/10`) were directly inspected this
session (full trail in `known_issues_x64.md` issue #2):

- **Base file**: a stock, unmodified ReShade `d3d9.dll` + the public
  `reshade-shaders` community repo + one preset config. Zero custom code, zero
  DLSS. The "Ray Tracing Global Illumination" claim is a real mischaracterization
  — MartysMods RTGI is a screen-space depth-buffer ray-marching SSGI
  approximation, not real ray tracing.
- **"With DLSS 5" file**: genuinely real — a full NVIDIA Streamline SDK payload
  (`sl.common.dll`, `sl.dlss.dll`, `sl.dlss_d.dll`, `sl.dlss_g.dll`,
  `sl.dlss_nr.dll`, `sl.interposer.dll`, `sl.nis.dll`), real NGX runtime DLLs
  (`nvngx_dlssnr.dll` alone is 165MB, consistent with a real NN weight payload),
  and a real RenoDX build (`renodx-dlss.addon64`). This is **direct, confirmed
  proof DLSS/Streamline can be made to function against MW3's own post-2026-09-03
  x64 binary specifically** — not an inference by analogy to a different engine,
  actual working prior art on our exact target.

RenoDX's own README directly confirms why it needs ReShade: "Using Reshade
simplifies all the hooks necessary to tap into DirectX without worrying about
patching version-specific exe files." This project already has that hook —
signature-scanned, verified, already coexisting with every other gameplay/visual
hook this project has installed since 2026-09-03. The reason RenoDX needs ReShade
does not apply to us.

## 2. NVIDIA Streamline — the real integration target

Researched directly via `gh api`/`curl` against `github.com/NVIDIA-RTX/Streamline`
(also mirrored at `github.com/NVIDIAGameWorks/Streamline`), reading
`docs/ProgrammingGuide.md` (1557 lines) in full.

### 2.1 Vulkan is a genuine first-class path, not a D3D11/12 afterthought

Confirmed directly (§2.5 "HOW TO PROVIDE CORRECT DEVICE"): a host application
using native Vulkan has two real options — let Streamline's own `vkCreateInstance`/
`vkCreateDevice` proxies manage everything, OR use native Vulkan calls and
**manually inform Streamline via `slSetVulkanInfo`**. The second option is exactly
what a DXVK-backed integration needs: DXVK creates its own real Vulkan
instance/device/queues through native Vulkan API calls; this project would call
`slSetVulkanInfo` itself once DXVK's handles are available, registering Streamline
against a host-managed Vulkan device with zero D3D11/D3D12 involvement anywhere in
the chain. This is a real, explicitly documented, officially-supported
integration shape — not a hack or an unsupported edge case.

A dedicated `ProgrammingGuideManualHooking.md` exists specifically for
integrators who don't want Streamline's own automatic interposer/hooking
behavior — flagged in the main guide as "the optimal and recommended way of
integrating SL" for exactly our situation (an application, or in our case
DXVK acting on our behalf, that already owns its own device-creation and
hook-installation sequence). **Not yet read in full — real next step.**

### 2.2 Resource tagging — the real per-frame data contract

`slSetTagForFrame` (the current API; the older `slSetTag` is deprecated) is how a
host provides Streamline with the buffers DLSS needs, tagged by a `BufferType`
enum. The relevant tags for basic DLSS Super Resolution:

- **`kBufferTypeDepth`** — "Must be suitable to use with clipToPrevClip
  transformation." Real, promising lead already on record: this project's own
  render-target table (`renderer_architecture_map.md` section 5b) already found
  `$floatz`, a real linear/float depth buffer target that exists in the native
  engine's own render-target inventory — a real candidate depth source, possibly
  without needing new native RE work to locate one from scratch.
- **`kBufferTypeMotionVectors`** — the one piece with no existing native source
  (confirmed absent from the render-target table) and no third-party shader
  being reused (LumeniteFX's LumaFlow was explicitly cut along with ReShade,
  per this session's own correction). **REAL, MAJOR UNLOCK, found this session
  (2026-09-23) reading `sl_consts.h`/`sl_matrix_helpers.h` directly**:
  Streamline has a real, first-class, explicitly-documented mode for exactly
  this situation — see the new §2.5 below. This does not eliminate the need
  for a motion-vector buffer, but it means "from-scratch motion-vector
  reconstruction" can mean "camera-only reprojection math this project
  already has the real inputs for," not "a full per-object optical-flow
  implementation" — a materially smaller, more tractable piece of real work
  than this doc previously scoped it as.
- **`kBufferTypeHUDLessColor`** — "Color buffer with all post-processing effects
  applied but without any UI/HUD elements." A real, easy-to-miss requirement:
  DLSS must never see the HUD, or it will try to temporally reconstruct crisp
  UI text as if it were noisy scene geometry. This project's own existing
  full-screen capture/composite pipeline (already used by FSR/motion blur/SMAA)
  is already HUD-aware and already sequences its own passes relative to the
  native HUD draw — a real, existing advantage over a naive raw-backbuffer grab.
- **`kBufferTypeScalingInputColor`/`kBufferTypeScalingOutputColor`** — the
  actual low-res-in/high-res-out pair for the upscale pass itself.

Every tag has a real `ResourceLifecycle` requirement (`eOnlyValidNow` /
`eValidUntilPresent` / `eValidUntilEvaluate`) governing how long Streamline is
allowed to assume the tagged resource stays valid — real correctness
constraints to respect once implementation starts, not just a data hand-off.

### 2.3 Frame tracking and the still-unresolved jitter requirement

`slGetNewFrameToken` is called once per simulated frame; the returned token is
threaded through every subsequent SL call for that frame (including from async
render/present threads) — a real, explicit "frame identity" contract, not
implicit frame-counting. Straightforward to integrate given this project's own
existing per-frame hook structure.

**Real, substantial progress this session (2026-09-23), following direct
instruction to keep researching jitter and motion vectors** — both blockers
are now precisely specified from real, primary sources (`docs/ProgrammingGuideDLSS.md`,
`include/sl_consts.h`, `include/sl_matrix_helpers.h`, all read in full this
pass) plus one real external precedent. Neither is "solved" — the actual
native RE (finding IW5's own projection-matrix-build function and hooking it)
is still real, unstarted work — but the previously-open question "what exactly
does Streamline need, and in what form" is now answered precisely, not just in
general terms.

**Jitter — the exact contract, confirmed directly from `sl_consts.h`/`ProgrammingGuide.md` §2.11.1:**
- `sl::Constants::jitterOffset` (a `float2`, pixel-space) is a **separate**
  field from the projection matrices — the matrices handed to Streamline
  (`cameraViewToClip`, `clipToPrevClip`, `prevClipToClip`) must explicitly
  **NOT** contain the jitter baked in ("must NOT contain temporal AA jitter
  offset... should be provided as the additional parameter
  `Constants::jitterOffset`"). This means two logically separate things have
  to happen every frame, not one: (1) the ACTUAL low-res render pass itself
  must use a jittered projection matrix (confirmed by `sl_consts.h`'s own
  comment on `kBufferTypeScalingInputColor`: "Color buffer containing
  **jittered** input data for the image scaling pass"), while (2) the
  `Constants` struct handed to `slSetConstants` reports the same frame's
  jitter value separately AND carries the clean, un-jittered matrices for
  Streamline's own internal reprojection math. A naive "just add the offset to
  the matrix I already send SL" implementation would be wrong on both counts.
- Troubleshooting section (`ProgrammingGuide.md` §14, "jitter offset values
  are in pixel space") independently confirms the same unit convention.
- **Real, standard formula for computing the actual per-frame offset**
  (canonical technique, not SL-specific — Alex Tardif's widely-cited TAA
  reference, `alextardif.com/TAA.html`, read directly this session): a
  Halton(2,3) low-discrepancy sequence, cycled over a window (8 samples is a
  common, reasonable default), each sample mapped to `[-1,1]` via
  `2*Halton(i,base)-1`, then divided by the render-resolution dimension
  (`jitterX = haltonX / renderWidth`, `jitterY = haltonY / renderHeight`) to
  get a genuine sub-pixel NDC offset — added into the projection matrix's
  `[2][0]`/`[2][1]` (row-major) translation-ish terms for the actual jittered
  render pass, while the SAME per-frame `(jitterX, jitterY)` pair (converted
  to pixel space per SL's own convention) is what gets reported via
  `Constants::jitterOffset`. **Real, easy-to-miss correctness requirement,
  same source**: any velocity/motion-vector math must explicitly SUBTRACT
  each frame's own jitter before differencing current vs. previous
  clip-space position — a static camera/object must still resolve to a
  zero motion vector once jitter is un-applied, or results ghost/blur; get
  this wrong and both a from-scratch TAA test and DLSS itself will visibly
  smear even on a completely still frame.
- **Real, separate, commonly-required companion setting not yet in this
  doc**: negative mipmap LOD bias on texture sampling while rendering at
  reduced internal resolution (a standard DLSS/TAAU best practice, roughly
  `log2(renderWidth / outputWidth) - 1.0`) — without it, textures read at
  the correct jittered UV but the wrong (too-blurry) mip level for the
  target's real output resolution. Real, cheap, likely tractable via this
  project's own existing `SetSamplerState`-adjacent hook surface once
  `Vulkan` mode exists — flagged here as a real requirement, not yet
  scoped as its own work item.
- **Real methodology finding, from a live open-source precedent
  (`github.com/nicolas-maman/ae3d`, issue #324, "DLSS through NVIDIA
  Streamline on Vulkan (needs motion vectors and a jittered projection
  first)")**: that project — a different small custom engine independently
  working through this exact same integration shape (jitter + motion
  vectors + Streamline) — explicitly plans to implement its OWN plain TAA
  pass first, using it purely as a correctness test rig for jitter and
  motion vectors, before attempting DLSS proper. Their own stated reasoning
  (direct quote via this session's own read of the issue): incorrect motion
  vectors cause visible TAA ghosting, making a from-scratch TAA
  implementation a fast, cheap, directly-observable validation loop —
  versus debugging jitter/motion-vector correctness indirectly through
  DLSS's own opaque NGX network output, where a bad result could be jitter,
  motion vectors, buffer tagging, OR the network itself. **Worth adopting as
  this project's own staged plan once `Vulkan` mode's DXVK foundation
  exists**: build and validate a simple from-scratch reprojection/TAA pass
  against this project's own jitter+mvec output before wiring Streamline at
  all — reuses the same real buffers either way (depth, jittered color,
  motion vectors, clean matrices), and turns "is DLSS broken" into "is our
  own TAA broken," a much easier bug class to isolate.
- **Real, still-unstarted native RE**: finding and hooking IW5's own
  projection-matrix-build function to (a) inject the jitter offset into the
  actual render pass and (b) capture the clean, un-jittered matrix + camera
  basis vectors for `Constants`. One real, not-yet-confirmed lead already on
  record from this project's own renderer-architecture mapping the same day
  (`renderer_architecture_map.md` §3): `FUN_1401d8f70`, called from the
  per-player frame-setup chain (`FUN_1401d7480`), "computes FOV-scale-derived
  aspect constants written into `param_1+0x9ec`/`+0x9e8`/`+0x9e4`, real
  floating-point camera math" — a real, plausible candidate for where
  FOV/aspect/projection setup happens, not yet decompiled in enough depth to
  confirm it owns the actual projection-matrix construction (vs. just
  consuming FOV to compute aspect-ratio-adjacent constants used elsewhere).
  **Two real, architecturally distinct injection strategies, either viable,
  neither yet chosen**: (1) hook this native CPU-side function (or whichever
  one actually builds the matrix) and modify the matrix in place before it's
  uploaded — more invasive, needs the real function found via further
  decompile; (2) hook at the D3D9 API boundary instead (`SetVertexShaderConstantF`,
  intercepting whichever constant-register range the engine's own vertex
  shaders read the projection matrix from) — lower native-RE burden (no need
  to fully understand the CPU-side math, just intercept the value already
  being uploaded to the GPU), and architecturally consistent with this
  project's own existing pattern of hooking at the D3D9 device-call boundary
  rather than deep inside engine internals wherever that's sufficient. Real
  next step, not yet started: identify the actual constant-register index
  IW5's shaders use for the projection matrix (a live-debugger or RenderDoc-
  frame-capture task, not a static-analysis one).

### 2.4 Signing/security — real, non-optional requirements

All production SL modules ship digitally signed by NVIDIA (two signatures: a
standard Windows Store cert, plus a custom NVIDIA cert as a hardened fallback).
The guide is explicit: **self-built or "development" SL DLLs are unsigned and
must never ship** — production integrations must use NVIDIA's own prebuilt,
signed `sl.*.dll` binaries as-is. This project's own redistribution plan (if any)
needs to respect this directly — bundling the real, signed NVIDIA binaries, not
attempting to rebuild Streamline from source for a shipped release.

**License terms — read in full this session (2026-09-23), real, actionable findings.**
Streamline's own SDK/source (the repo itself — headers, sample/interposer
code) is under a plain, permissive MIT-style license (`license.txt`, read
directly) — one real carve-out, irrelevant to this project: `sl_nvperf.h`/
`sl_nvperf.dll` (the bundled NSight Perf profiling component this project has
no reason to use) falls under a separate, stricter NSight Perf SDK License.
**This is NOT the same license that governs the actual redistributable
binaries** (`sl.dlss.dll`, the NGX runtime DLLs) — those fall under NVIDIA's
own real "RTX SDKs License" / DLSS SDK EULA (confirmed via NVIDIA's own
published terms, `NVIDIA/DLSS` repo's `LICENSE.txt`, read directly this
session), which is a genuinely different, more restrictive agreement:

- **Redistribution of the compiled binaries alongside this project's own
  application is real and permitted**, but conditionally, not
  unconditionally: the license requires the host application have "material
  additional functionality, beyond the included portions of the SDK" and
  explicitly forbids distributing the SDK "as a stand-alone product." Both
  conditions are trivially satisfied here — this project is a full
  controller/visual-enhancement mod with DLSS support as one feature among
  many, never a bare DLSS-DLL repackage.
- **A real, concrete, not-yet-actioned requirement**: "You are required to
  notify NVIDIA prior to commercial release of an application... that
  incorporates, or is based on, the DLSS SDK, NGX SDK." **Genuinely
  unresolved for this project's specific situation**: this mod is free and
  never sold (this repo's own `LICENSE`'s core restriction), which may or
  may not count as a "commercial release" under NVIDIA's own definition —
  not assumed either way. Real, honest next step before ever shipping
  `Vulkan` mode's DLSS support publicly: either find NVIDIA's own written
  definition of "commercial release" for this clause, or contact
  NVIDIA directly (the license's own stated channel,
  `nvidia-rtx-license-questions@nvidia.com`, surfaced via this session's own
  research) to confirm whether a free, non-commercial community mod needs
  this notification at all. Not something to guess past.
- **A real, new, actionable requirement not previously on record**: mandatory
  "NVIDIA Marks on splash screens, in the about box of the application (if
  present), and in credits" wherever DLSS/NGX is integrated — a real
  attribution obligation, the same general shape as this project's own
  already-satisfied MinHook/HDE credit requirement (`CLAUDE.md` §6), just a
  new one to add once this ships, not yet implemented.
- **Confirmed, consistent with existing policy either way**: no reverse-
  engineering, decompiling, or disassembling of the SDK's compiled binaries,
  and no removing copyright/proprietary notices — this project has never
  needed or intended to RE the DLSS/NGX binaries themselves (unlike the
  base game's own binaries, which are not NVIDIA's IP and aren't covered by
  this clause at all), so this is a non-issue in practice, just worth having
  on record precisely.
- **A real, structural tension worth flagging precisely, not glossing over**:
  the EULA states "You may not use the SDK in any manner that would cause it
  to become subject to an open source software license." This project's own
  repo `LICENSE` is a real, freely-forkable/modifiable license (see
  `CLAUDE.md`'s own "Repository structure" note on nested components with
  their own separate licenses — `tools/iw5oat`'s GPLv3, `security/`'s
  permissive-but-distinct license). **The same already-established,
  already-precedented pattern this project uses for those nested components
  applies here directly**: the redistributed NVIDIA binaries (and any
  Streamline SDK source this project links against, if ever built from
  source rather than used as prebuilt binaries) must be clearly scoped as
  separately-licensed, proprietary, NOT re-licensed or folded into this
  repo's own free-to-fork grant — a documentation/licensing-file task, not a
  technical one, but a real one to get right before shipping, not an
  afterthought.

The `EnableNvidiaSigOverride.reg` file bundled with MW3 Remastered's own DLSS
variant is consistent with this signing model — very likely a workaround for a
DIFFERENT Windows-level signature-enforcement point (not SL's own internal
`WinVerifyTrust` check, which the guide says the interposer handles
automatically), possibly related to loading the NGX runtime DLLs specifically
in a context NVIDIA's own driver doesn't recognize as an officially-integrated
title. Not yet independently confirmed — flagged as a real open question.

### 2.7 `ProgrammingGuideManualHooking.md` — read in full this session, one real, previously-unresearched integration requirement found: Vulkan swapchain interception

**Real, direct confirmation of this project's own already-planned approach**:
manual hooking (`PreferenceFlag::eUseManualHooking`) is explicitly the
documented path for "an application... that already own[s] its own
device-creation and hook-installation sequence" — exactly this project's own
situation with DXVK owning real Vulkan device/instance creation, not
Streamline's own automatic global interposer. Device/instance creation
proxying (`vkCreateInstanceProxy`/`vkCreateDeviceProxy`) is confirmed
**optional** — skippable entirely if the host (DXVK, on this project's
behalf) creates its own real Vulkan instance/device and this project then
calls `slSetVulkanInfo` manually afterward, exactly the shape already
recorded in §2.1.

**The one real, new, previously-unresearched requirement, found reading this
guide in full**: unlike instance/device creation, **Vulkan swapchain-related
calls are listed as MANDATORY hooks, not optional** —
`eVulkan_CreateSwapchainKHR`, `eVulkan_DestroySwapchainKHR`,
`eVulkan_GetSwapchainImagesKHR`, `eVulkan_AcquireNextImageKHR`,
`eVulkan_Present`/`eVulkan_QueuePresentKHR`, `eVulkan_DeviceWaitIdle`,
`eVulkan_CreateWin32SurfaceKHR`, `eVulkan_DestroySurfaceKHR` must all
route through `sl.interposer.dll`'s own proxied functions, obtained via
`vkGetDeviceProcAddr`/`vkGetInstanceProcAddr` resolved from
`sl.interposer.dll` instead of the real `vulkan-1.dll`. **This is a real
integration knot specific to routing through DXVK**: DXVK owns its own
internal swapchain creation and present calls entirely — this project's own
code never calls those Vulkan functions directly, DXVK does, internally,
using whatever `vulkan-1.dll` it resolves at its own load time. Getting SL's
mandatory swapchain hooks into that path without patching DXVK's own source
needs DXVK's own `vulkan-1.dll` resolution to land on `sl.interposer.dll`
instead — **a real, promising, DIRECT extension of this project's own
already-proven core technique**: the same Windows DLL-search-order proxy
trick this project's entire `d3d9.dll` injection already relies on (a proxy
DLL sitting ahead of the real system one, forwarding everything through)
should apply identically to `vulkan-1.dll` if `sl.interposer.dll` is placed/
renamed to intercept that exact load — no DXVK source patching needed, if
this holds. **Not yet confirmed**: whether DXVK resolves `vulkan-1.dll` via
a plain, redirectable `LoadLibrary`/import-table lookup (matching the
`d3d9.dll` case exactly) or some other mechanism — real, cheap, concrete
next step once `Vulkan` mode implementation starts, not yet attempted.

**A second, real, separate requirement for the "we own instance/device
creation" branch specifically** (§5.2.1): before creating the Vulkan
instance/device, the host must call `slGetFeatureRequirements` per enabled
SL feature (DLSS included) and manually fold the returned required instance/
device extensions, `VkPhysicalDeviceVulkan12Features`/`...Vulkan13Features`,
and extra graphics/compute queue counts into whatever creates the real
device — for this project, DXVK's own device creation, not this project's
own code, since DXVK is what actually calls `vkCreateDevice`. **Real, honest
open question, not yet researched**: whether DXVK exposes any real
extension-injection mechanism (environment variables, `dxvk.conf` entries, a
build-time patch) that could satisfy this without a genuine DXVK source
modification — the earlier DXVK `dxvk.conf` key research (section 4.3) found
real config keys for other purposes but did not specifically check for a
device-extension-injection option; real next step, not yet done.

### 2.5 Camera-only motion vectors — a real, documented Streamline mode, and a direct connection to this project's own existing motion-blur data

**The core finding, confirmed directly from `include/sl_consts.h` (both the
header itself and the identical struct documented in `ProgrammingGuide.md`
§2.11.1) — read in full this session for the first time**: `sl::Constants`
carries a `Boolean cameraMotionIncluded` field, documented as "Specifies if
camera motion is included in the MVec buffer," alongside
`motionVectorsInvalidValue` ("This is only required if `cameraMotionIncluded`
is set to false and SL needs to compute it"). Read together, this describes a
real, first-class, explicitly-supported mode: **the host is not required to
supply a motion-vector buffer that already accounts for camera movement.** If
`cameraMotionIncluded` is set to `eFalse`, Streamline computes the
camera-induced portion of motion itself — from the same `clipToPrevClip`/
`prevClipToClip` matrices already required for jitter/reprojection (§2.3
above) plus the depth buffer already being tagged (`kBufferTypeDepth`) — and
combines it with whatever (if anything) the host's own motion-vector buffer
supplies for object-level motion. **This means a functioning, real
`kBufferTypeMotionVectors` tag does not require solving per-object motion at
all as a first step.**

**A real, viable staged implementation this unlocks, not previously
scoped**:
1. **Stage 1 — camera-only**: tag a motion-vector buffer that is genuinely
   all-zero (or simply don't populate per-object motion at all, per whatever
   `motionVectorsInvalidValue` convention is chosen), set
   `cameraMotionIncluded = eFalse`, and supply real, correct camera
   matrices/vectors every frame. Streamline reconstructs full-quality motion
   vectors for the static world and background purely from camera transform +
   depth — genuinely correct, not an approximation, for anything that isn't
   itself moving independently of the camera (which is the overwhelming
   majority of any given frame's pixels in a shooter — level geometry,
   terrain, static props). This is a REAL, complete, shippable motion-vector
   solution for a first `Vulkan`-mode release, not a stopgap being framed as
   more than it is.
2. **Stage 2 — object motion, deferred, real future work**: dynamic
   entities (other players, vehicles, projectiles) would still reconstruct as
   if static relative to the camera (a visible smear/ghost trail specifically
   on fast-moving foreground objects during camera-relative motion) until a
   real per-object motion-vector source is added — the actual "from-scratch
   optical-flow or engine-side per-object velocity" work this doc originally
   scoped as the whole problem. Deferred, not solved, but now correctly
   framed as an INCREMENTAL quality improvement on top of an already-working
   baseline, not a blocking prerequisite for shipping DLSS support at all.

**Direct connection to this project's own already-shipped code, worth
recording precisely**: this project's own visual-enhancement suite already
computes real, live, per-frame camera-only motion data for an unrelated
feature — motion blur (`v0.3.5`, issue #95, "camera-only motion blur driven
by this project's own real per-frame look data"), which works from exactly
the same category of input (per-tick camera angle/orientation delta) that
`sl::Constants`'s `cameraPos`/`cameraUp`/`cameraRight`/`cameraFwd` fields and
`sl_matrix_helpers.h`'s own `calcCameraToPrevCamera`/`recalculateCameraMatrices`
reference implementation need. **`sl_matrix_helpers.h` (read in full this
session, MIT-licensed, part of the public Streamline SDK) is real, working,
directly-adaptable reference code** — `calcCameraToPrevCamera` computes a
numerically-stable (camera-centered, avoiding the large-world-translation
float32 precision loss a naive `invert(prevViewMatrix) * currentViewMatrix`
would hit) previous-to-current camera transform from nothing but a
camera-to-world matrix for the current and previous frame, and
`recalculateCameraMatrices` chains that into the exact `clipToPrevClip`/
`prevClipToClip` matrices `Constants` needs, from nothing but per-frame
camera position + forward/right/up basis vectors and the projection matrix —
the same shape of data this project's own controller-look injection code
already produces every tick for a completely different purpose. This does
not mean the existing motion-blur code can be reused unmodified (it drives a
different, screen-space blur effect, not a matrix contract), but it confirms
the REAL, primary inputs Stage 1 above needs are already the project's own
native currency, not a new category of data requiring fresh native RE to
obtain — only the matrix-construction math (present here, ready to adapt)
and the actual jitter/projection hook point (§2.3's own still-open native RE
item) are the real remaining work.

### 2.6 REVISED PRIORITY 2026-09-23, direct instruction ("we want proper motion vectors... we may have to surface them off of engine data") — real, per-object motion sourced from IW5's own engine data, not camera-only alone

§2.5's camera-only mode is real and stays correct for the majority of any
frame (static world geometry) — but the user's own direct correction is
right: it is not "proper" motion vectors on its own. Every dynamic entity
(other players, AI, vehicles, dropped weapons, thrown grenades) would
reconstruct as if rigidly attached to the camera, producing a real, visible
smear/ghost trail on exactly the pixels a player's eye is most drawn to. This
section researches what "proper" requires and how this project's own already-
mapped renderer architecture makes it tractable, not a from-scratch problem.

**The exact Streamline semantics for combining camera and object motion —
re-read precisely this pass, `ProgrammingGuide.md` line 763**: the buffer tag
itself is documented as carrying "**Object and optional camera** motion
vectors" — confirming a real, deliberate, two-tier design, not an
all-or-nothing choice. Read together with `cameraMotionIncluded`/
`motionVectorsInvalidValue` (§2.5): when `cameraMotionIncluded = eFalse`,
**the buffer we supply is read as the OBJECT-ONLY contribution** — Streamline
computes the camera-induced term itself (from `clipToPrevClip`/depth) and,
per-pixel, either uses our object value where we've written real data, or
falls back to pure camera motion wherever our buffer holds the declared
`motionVectorsInvalidValue` sentinel. **This is the real, correct
architecture for this project specifically**: we do NOT need to compute or
understand camera motion inside any custom per-object shader work at all —
that stays entirely Streamline's problem, handled once, correctly, for the
whole frame via §2.5's baseline. Real, from-scratch native work is only
needed to populate the OBJECT-relative residual, and only at the pixels
genuinely dynamic entities cover — a real, legitimate scope reduction, not
a compromise: **the render passes needing real per-object velocity output
are the same, comparatively small subset of each frame's draw calls that
were already going to be the interesting/dynamic ones**, not a second full-
scene redraw. (The exact per-pixel combination arithmetic beyond this —
whether SL adds our object value to its own camera term or fully replaces it
at populated pixels — is not spelled out to full mathematical precision in
either doc read this pass; treat as a real, standard "hybrid object+camera
motion vector" contract per the comment's own wording, but confirm the
precise formula against `ProgrammingGuideDLSS_RR.md`/DLSS-FG's own docs or a
live test before trusting output correctness — flagged honestly as unread
this pass, not assumed.)

**What "surfacing real per-object motion off of engine data" concretely
means, and why it doesn't require decompiling IW5's own shaders (a real,
much larger undertaking) — the standard, industry-established technique,
adapted to this project's own already-proven "hook at the D3D9 API boundary,
not deep engine internals" philosophy (§2.3):**

1. **Capture, don't compute.** This project does not need to independently
   figure out how any given entity is animating or moving — the real game
   engine already computes and uploads the exact transform (and, for skinned
   meshes, the exact bone-matrix palette) that produces this frame's correct
   pose, via ordinary `IDirect3DDevice9::SetVertexShaderConstantF` calls this
   project can already intercept (the same device-call boundary its own
   existing hooks already sit on). The only new work is RETAINING a copy of
   those same per-draw constants from the PREVIOUS frame, keyed by a stable
   per-object identity, so both poses are available together when the
   current frame's velocity pass runs.
2. **Replay, don't modify.** Rather than patching IW5's own real, precompiled
   vertex shaders (a genuinely large undertaking — extracting and
   reassembling D3D9 Shader Model 3 bytecode, a real, documented but
   nontrivial format, and one this project's own `tools/iw5oat` fastfile work
   already has some real, hard-won familiarity with extracting assets from,
   though not shader bytecode specifically), bind the SAME real vertex/index
   buffers the game already uses for a given draw to a second, small,
   **hand-authored** vertex/pixel shader pair this project compiles itself
   (ordinary, source-controlled HLSL, not reverse-engineered bytecode) that
   does nothing but transform position (and, for skinned draws, apply the
   identical bone-palette blend using the captured bone matrices) through
   BOTH the current and the retained previous-frame transform, and writes
   clip-space velocity (jitter already removed per §2.3's own correctness
   requirement) to a dedicated render target. This is the same standard
   technique real modern engines use for their own native "velocity pass" —
   not a hack specific to this project's own constrained situation.
3. **Real, honest complexity: skinned/animated draws need the real bone
   palette, not just a per-object rigid transform.** A pure per-object rigid
   transform diff (cheap, no skinning math needed) is CORRECT for
   non-animated dynamic draws (vehicles, dropped weapons, thrown grenades,
   doors) but WRONG for animated characters — a rigidly-diffed player model
   would show the torso's own motion applied uniformly to swinging arms/legs
   too, a real, visible artifact. Getting characters right needs the actual
   per-vertex bone blend replicated in the hand-authored shader, using the
   SAME captured current+previous bone-matrix-palette constants the real
   draw already uploads — this project's own renderer-architecture mapping
   (`renderer_architecture_map.md` §5b) already found a real, named
   `skin model` render-command-buffer stage (tag `0x10`, distinct from the
   generic `cell dyn model` stage, `0x2`) — direct, already-on-record
   confirmation that skinned draws are already a first-class, separately-
   identifiable category in IW5's own command stream, not something this
   project would need to newly discover the existence of. **Not yet
   confirmed**: the exact vertex-format/bone-count-per-vertex convention this
   engine's skinning shaders expect — real, scoped native RE, not started.
4. **Stable per-object identity across frames — RESOLVED, 2026-09-23,
   `renderer_architecture_map.md` §7.** Draw ORDER is not a safe key —
   state-sorting and visibility culling can reorder or drop draws frame to
   frame, so a "previous frame's transform" cache keyed by draw index would
   silently mismatch the wrong object's history to the wrong current draw.
   The real answer, found by fully tracing `add scene ent` (stage `0x11`,
   now located and decompiled — `FUN_1401e9f70`'s own `case 0x11`): the
   engine ALREADY maintains exactly the identity this project needs — five
   separate entity-category arrays, each indexed by a plain integer (0 to a
   per-category count), each gated by a parallel "active/visible this
   frame" boolean array the engine itself already updates every frame. A
   newly-active index (inactive last frame, active now) is directly
   observable from that same boolean array, mapping 1:1 onto Streamline's
   own `motionVectorsInvalidValue` sentinel case (item 5 below) — the
   engine's own "did this entity just appear" signal doubles as exactly the
   signal this project's own cache needs to know when NOT to trust a
   previous-frame entry. **Real, concrete design this unlocks**: key the
   capture/retain transform cache by `(entityCategory, arrayIndex)`,
   reading the same active-flag array `add scene ent` already consults.
   **Not yet determined**: which of the five categories is "dynamic/skinned
   models" specifically (the strongest candidate, by stride/field shape, is
   the one at `0x141c23028`, stride `0x90`) — real, cheap, next
   confirmation step, not a blocker. This closes what was the single most
   important unconfirmed lead for this whole feature.
5. **Real, standard handling for objects with no valid previous-frame
   history** (just spawned, just entered view, or the very first frame after
   a level load / camera cut): write `motionVectorsInvalidValue` at those
   pixels rather than a wrong or zero-length vector, exactly the sentinel
   `sl::Constants` already reserves for this. `Constants::reset` (a real,
   separate field, confirmed present in `sl_consts.h`, not previously
   documented in this file) is the frame-level equivalent — set on a hard
   discontinuity (level load, teleport, cutscene cut) to tell Streamline "the
   previous frame has no connection to this one," which this project's own
   already-existing level-load/menu-state detection (used elsewhere for the
   visual-enhancement suite's own safety gates, e.g. FSR/motion blur's
   `clcState`-based gating, issues #103/#104) is directly reusable for.
6. **Real, standard scope reduction already common industry practice, not a
   shortcut specific to this project**: alpha-blended/transparent draws are
   routinely excluded from motion-vector generation in real engines (no
   single well-defined "depth" for blended surfaces makes correct velocity
   ill-defined for them anyway) — a legitimate, precedented way to keep this
   project's own first real implementation's scope bounded to opaque,
   skinned-or-rigid dynamic draws specifically.

**Net, honest scope for "proper" motion vectors**: a real, multi-part
native-RE-plus-new-shader-authoring effort — larger than §2.5's camera-only
baseline, but concretely bounded and, per point 4 above, likely resting on
engine infrastructure (stable per-entity draw identity, a distinct skinned-
draw command category) this project's own renderer mapping has already found
real, direct evidence of rather than having to discover from zero. Real next
steps, in dependency order: (a) decompile enough of `gen drawsurfs`/the
actual draw-submission path to confirm a stable per-entity key exists in the
command stream and learn its real shape; (b) decompile the `skin model`
command handler to learn IW5's real vertex-format/bone-palette convention;
(c) author the hand-written current+previous velocity vertex/pixel shader
pair; (d) wire the capture-and-retain cache for per-draw constants, keyed by
whatever (a) finds. None of this is native-RE-blocked in principle — it's
real, scoped, sequenced work, not an open question about whether it's
possible.

**Step (a) substantially advanced, same day (2026-09-23)**: `gen drawsurfs`
is no longer an unlocated stage — it's fully traced to a real function and
case (`FUN_1401e9f70`, `switch(stageId)` case `0x12`, on a dedicated
2-worker job-pool thread draining a shared stage-event ring buffer), full
chain documented in `renderer_architecture_map.md` §7. Its own code, read
this pass, is draw-surface-array-SLOT bookkeeping, not an obvious per-entity
walk with a visible stable handle — the real per-entity identity this
project needs is now believed to live one stage earlier, in case `0x11`
(`add scene ent`) or case `0x03` (`cell scene ent`), not yet read with the
same scrutiny. Step (a) is therefore precisely re-scoped, not blocked: read
those two specific cases (raw dump already captured,
`stage_listener_invoker2.txt`, case `0x11` at line ~1368) for a per-entity
loop and whatever identity it assigns/reads per entity.

## 3. RenoDX's real per-game catalog — confirms the engine class is achievable, no direct MW3 precedent exists

Directly listed `src/games/` in `github.com/clshortfuse/renodx` (main branch,
not the wiki, which failed to clone cleanly this pass) — a real catalog of
250+ individually-integrated titles. **No MW3 (2011) or IW5-engine entry exists**
— this project would be genuine, first-of-its-kind native RE work for this
specific engine, not following an existing trodden path. Two real, meaningfully
comparable precedents DO exist in the catalog:

- **`callofduty_t7_blackops3`** — a related CoD-lineage engine (Treyarch's own
  fork of the shared IW/CoD engine tree), real evidence this general engine
  family has been successfully integrated before, even though Treyarch and
  Infinity Ward's engines have diverged significantly over the years.
- **`specopstheline`** — Spec Ops: The Line (2012, Yager Development, Unreal
  Engine 3), a genuinely comparable console-generation, shader-based
  (not fixed-function) title — real evidence this specific ERA/CLASS of
  engine (not just modern titles) is a real, achievable RenoDX integration
  target, reinforcing the general finding from the RTX Remix research
  (fixed-function-only is Remix's own specific limitation, not a universal
  "old games can't get DLSS" wall).

## 4. DXVK — full research landed

A dedicated research agent investigated DXVK (`github.com/doitsujin/dxvk`) in
the same depth as sections 1-3, via `gh api`/issue search. Real, decisive
findings, one of which meaningfully corrects this project's own assumed
integration architecture.

### 4.1 Native-Windows drop-in usage — real, but explicitly unofficial

The dedicated wiki page "Windows" states plainly: "The DXVK developers do not
officially support running DXVK on Windows. Many issues fall outside their
control." Real setup gotchas are documented (Microsoft's own "OpenCL, OpenGL &
Vulkan Compatibility Pack" can break DXVK; anti-cheat and overlay software —
Steam/Epic/Uplay overlays, GeForce Experience, RTSS, OBS — are known
troublemakers). Community native-Windows use for offline/retro titles is
common in practice, but no citable "this is a blessed, supported workflow"
statement exists — this project should describe it precisely as real but
community-only, not "production-proven the way Proton is" (that framing was
this project's own earlier overclaim, corrected here).

### 4.2 Shader-compile-storm stuttering — real, unresolved, and specifically hits this engine's own lineage

Not hypothetical. Real, still-open DXVK issues directly against CoD-family
titles: **#4594** ("D3D9: Call of Duty: Modern Warfare 2 (2009) / Excessive
Shader Compilation," open) — 20-60s freezes, hundreds of shader-compile log
lines, confirmed DXVK-specific (does not reproduce with WineD3D), a 400MB+
apitrace before even reaching the main menu, unresolved. **#3049** ("Call of
Duty: Modern Warfare Remastered: Shader compile conflict?", open) — real,
game-triggered shader-precompile stutter under DXVK, community workaround is
an unofficial "DXVK-ASYNC" fork never merged upstream; a DXVK dev noted D3D11
gives applications no manual shader-cache API, meaning D3D9-era precompilation
assumptions don't map cleanly onto DXVK's own pipeline-compile model. Further
same-era pattern: **#4924** (CoD: World at War crashes with
`GraphicsPipelineLibrary=Auto/True`), **#4882** (CoD: Black Ops "feels
unsmooth" since DXVK 2.5), **#4800**/**#5231** (CoD2 crash on launch, one
still open).

**Direct user connection, worth recording precisely**: this project's own
render-scale-gated stutter investigation this session (issue #4) already
concluded a genuine native engine stability limit at large render-target
sizes — and the x86 line separately had real crashes above ~250% render scale
root-caused to `ForceD3D9On12` driver corruption (issue #105, a different
mechanism, since removed). This DXVK finding is a THIRD, independent data
point in the same broader pattern: this engine family has demonstrated real
instability under multiple, genuinely different kinds of load (large
render-target memory/bandwidth; `ForceD3D9On12`'s own driver-level
corruption; now DXVK's shader-pipeline-compile volume) — three different root
mechanisms, not the same bug, but a real, recurring shape worth taking
seriously as a risk factor for `Vulkan` mode specifically, not dismissed as
unrelated. **Not confirmed to be the literal same bug as issue #4** — different
investigators, different technical layers (native D3D9 render-target sizing
vs. DXVK's own Vulkan pipeline JIT-compilation), no shared root cause
established — but the pattern itself (this engine's real stability envelope
gets hit by several independent kinds of stress) is now evidenced three times,
not once.

### 4.3 D3D9-specific `dxvk.conf` keys (real, confirmed set)

`deferSurfaceCreation`, `maxFrameLatency`, `maxFrameRate`, `presentInterval`,
`samplerAnisotropy`, `samplerLodBias`, `clampNegativeLodBias`,
`forceSampleRateShading`, `shaderModel` (0-3), `dpiAware`, `lenientClear`,
`maxAvailableMemory` (feeds `GetAvailableTextureMem`), `memoryTrackTest`,
`floatEmulation`, `deviceLocalConstantBuffers`, `supportCubeDepthFormats`,
`supportDFFormats`, `useD32forD24`, `supportX4R4G4B4`, `disableA8RT`,
`forceSamplerTypeSpecConstants`, `forceAspectRatio`, `forceRefreshRate`,
`modeCountCompatibility`, `enumerateByDisplays`, `cachedWriteOnlyBuffers`,
`seamlessCubes`, `textureMemory` (MB, 0=disable), `deviceLossOnFocusLoss`,
`countLosableResources`, `extraFrontbuffer`, `useFP16`,
`ignoreDefaultBufferLockRange`. Loaded via `DXVK_CONFIG_FILE` or the
`DXVK_CONFIG` env var.

### 4.4 Real D3D9-specific Vulkan interop API — answers the open `slSetVulkanInfo` question from section 2.1

`src/d3d9/d3d9_interop.h`/`.cpp` implement a real `D3D9VkInteropInterface`/
`D3D9VkInteropDevice` API (merged via **PR #4506**, "[d3d9] Add device import
interop API"), exposing `GetDeviceCreateInfo`, `QueryDeviceExtensions`,
`QueryDeviceQueues`, `QueryDeviceFeatures`, and `ImportDevice` — a host can
query DXVK's own required Vulkan device-creation parameters, create/import the
device itself, and hand it back. The PR author reports real, working use
(tested on AMD/Nvidia/Linux and Nvidia/Windows, MSVC and Mingw builds) running
a custom deferred renderer "atop our DX9 renderer" through this exact path —
directly relevant, working precedent for feeding DXVK's own Vulkan device to
`slSetVulkanInfo`. **Caveat**: this is a fairly recent addition (PR discussion
dated late 2024) — must verify presence in whatever DXVK version/commit this
project pins to before relying on it.

### 4.5 The export-forwarding architecture assumption was WRONG — real correction

This project's original assumption (§ earlier this session, and this doc's own
first draft) — that our proxy would forward calls to a renamed DXVK `d3d9.dll`
the same way it currently forwards to the real system `d3d9.dll` — **is
explicitly rejected by DXVK's own maintainer**. Issue **#2772** ("Using an
identical named proxy dll at the same time as dxvk"), doitsujin's own words:
**"DXVK needs to be the last DLL in the chain so I don't really see how this
would work,"** and: **"DXVK is the D3D9 implementation, we can't simply go
ahead and call someone else's Direct3DCreate9 instead of ours and expect that
to somehow still work."** A community-suggested rename+hex-patch workaround
(from WineD3D-era practice) was flagged by its own author as untested against
DXVK specifically — no verified, working rename path exists.

**The real, correct architecture, with genuine working precedent**: issue
**#1498** documents a D3D8-to-D3D9 wrapper successfully loading ON TOP of
DXVK's real `d3d9.dll` (confirmed via DXVK's own log output) — i.e. a layer
that itself calls DXVK's real `Direct3DCreate9` directly and wraps/hooks the
`IDirect3D9`/`IDirect3DDevice9` objects DXVK hands back, rather than forwarding
opaque exports through a renamed copy. Further real precedent: ReShade and
Special K (another D3D9 hooking/injection tool, same class as this project's
own MinHook-based vtable hooks) both hook the same device surface
(`EndScene`/`Present`/`Reset`) on top of DXVK in practice — real, if sometimes
version-bumpy, compatibility (issues **#3995**, **#4776**, **#5889**,
**#4145**, **#4295** — fixable interaction bugs across DXVK version bumps,
not categorical incompatibility).

**Corrected plan**: `Vulkan` mode's own `Direct3DCreate9`/`CreateDevice`
handling must explicitly `LoadLibrary` a renamed DXVK `d3d9.dll`, call ITS
`Direct3DCreate9` export directly (not blind export-table forwarding — this
project already does explicit resolve-and-call for its other real function
calls, so this is a smaller change than it first appears), and install this
project's own existing MinHook vtable hooks on the `IDirect3D9`/
`IDirect3DDevice9` objects DXVK returns — structurally identical to what this
project's hooks already do against the real system `d3d9.dll` today, just
pointed at DXVK's own implementation instead. `Legacy D3D9` mode is completely
unaffected — it keeps using the real system `d3d9.dll` exactly as today.

### 4.6 Real question raised, direct user point: ship this as a plugin DLL instead, so it "isn't the d3d9.dll"?

**Doesn't work as a detection-avoidance mechanism, for a real, hard technical
reason**: Windows loads exactly one file named `d3d9.dll` per process (from
the game's own install directory, standard DLL search order), and the game
calls THAT file's own `Direct3DCreate9` export directly at startup. Something
has to physically be that file and intercept device creation at that exact
moment, before any device exists — this project's own existing plugin system
loads plugins AFTER the main mod's own device/hooks already exist (a
deliberate, correct design for what plugins are actually for), which is
architecturally too late to ever intercept device creation itself. There is
no way to route the real DXVK integration through the existing plugin loader
— whatever handles `Vulkan` mode must be reachable from the real `d3d9.dll`'s
own `Direct3DCreate9`, full stop. The on-disk module VAC observes as
"`d3d9.dll`" is necessarily not the genuine Microsoft one either way — that's
already true of this project's own CURRENT `Legacy D3D9`-only proxy too (see
section 5's own real track-record discussion), and doesn't change based on
where the DXVK-specific logic physically lives.

**Where the underlying instinct is still genuinely right**: real isolation
value exists, just not the value originally hoped for. Rather than growing
this project's own main `d3d9.dll` to include the full DXVK/Streamline
integration directly, the main proxy's `Direct3DCreate9` handling can stay
almost exactly as it is today for the default `Legacy D3D9` path (same file,
same already-2-months-clean behavior), with one small added branch: if
`Vulkan` mode is selected, `LoadLibrary` a SEPARATE, clearly-labeled auxiliary
DLL early (NOT via the plugin loader — a direct, explicit load from within
`Direct3DCreate9` itself, before any device exists) and call ITS exported
`Direct3DCreate9` instead, with all the new, large, unverified DXVK/Streamline
integration code living entirely in that separate file. Real, legitimate
benefits this actually delivers: the already-trusted core module's own diff
stays small and easy to audit; the new, higher-risk surface is isolated,
independently removable, and impossible to accidentally activate for anyone
who hasn't explicitly opted into `Vulkan` mode; and it keeps this project's
own established "opt-in feature = separate, inspectable unit" convention
(matching how the security component already ships as a separate,
"greenlit" plugin DLL) even though the LOADING mechanism itself can't be the
literal plugin API for this specific piece.

## 5. VAC/ban risk for `Vulkan` mode — real research, a genuine correction to an assumption almost made

**A wrong argument was almost made here and is recorded so it isn't repeated**:
the intuitive case ("DXVK is safe because Steam Deck plays VAC-secured games
via Proton/DXVK constantly with no incident") does **NOT** transfer to this
project's own planned native-Windows deployment. Real, primary-source evidence
from DXVK's own maintainers, PR **#4223** ("[meta] Add more information about
anti-cheats," merged into DXVK's own README):

> "Most of the time, if a game runs without any issues via Proton, it probably
> also means it's safe to use DXVK with it... For most games, the worst
> scenario you can typically get [via Proton] is being kicked from the match
> due to anti-cheat support not being enabled. In those cases, it is very
> likely that the game is detecting and preventing **Wine**, rather than DXVK
> itself."
>
> "**On Windows, you are still using at your own risk**, since you are not
> running under Proton/Wine (which is expected to manipulate Direct3D
> libraries). Some games may simply refuse to launch when DLL replacement is
> detected. Others may allow it at first, but posteriorly kick or ban the user
> mid-match."

The Steam Deck safety signal exists BECAUSE DXVK is the ubiquitous, expected
default in that environment — an anti-cheat system tolerating Wine tolerates
DXVK as a side effect, it isn't independently vetting DXVK's own D3D9
implementation as safe. On native Windows, a replaced `d3d9.dll` is NOT the
expected state of the world, and DXVK's own maintainers explicitly decline to
extend the Steam Deck safety argument to that case. This is directly
consistent with — real, independent corroboration of — this project's own
already-established VAC research (`CLAUDE.md`'s "CORRECTED 2026-07-20" entry):
VAC is signature/heuristic-based, and an unexpected D3D-library replacement is
exactly the class of anomaly that kind of detection is built to notice.

DXVK's own doc also cites a real, concrete false-positive precedent (Apex
Legends players wrongly banned for Linux/Wine use, later reversed) — worth
recording as a real risk *category*, not just "caught cheating": detection
heuristics can misfire even absent any actual cheat behavior.

**A real, genuinely relevant counterpoint, direct user point**: this
project's own existing proxy `d3d9.dll` already does deep, invasive render-
pipeline manipulation — `CreateDevice`/`EndScene`/`Reset` vtable hooking,
signature-scanned code-cave detours into native engine functions, full-screen
capture/composite, render-target size overrides — with a real, on-point track
record: 2+ months of public use (since v0.1.0-prealpha, 2026-07-15) including
live streams and real MP sessions, **zero reported VAC bans attributable to
this mod**. This is more directly relevant than DXVK's own necessarily broad,
conservative, cross-thousands-of-titles disclaimer (written to cover far more
aggressive kernel-level anti-cheats like EAC/BattlEye, not calibrated to this
specific game's own comparatively old, likely under-maintained VAC signature
set — this project's own existing research already concludes that). This real
track record is genuine evidence that narrows the ambient worry about VAC's
practical (not theoretical-maximum) enforcement against this specific game.

**Re-read against this project's own existing VAC research, direct
instruction ("check the security research and corroborate claims to the new
binary as who knows maybe they added some") — two things that sharpen, not
soften, the picture above:**

**(a) The ENB depth-of-modification finding (`known_issues.md`, 2026-07-20
VAC research pass) already puts this project's EXISTING technique closer to
a real-ban-history risk category than the "2 months, zero bans" framing
alone suggests.** That research's own decisive finding, re-read this
session rather than assumed still valid: the real distinguishing risk
factor across known cases is DEPTH of modification, not loading method.
ReShade (visual-only, reads/writes only its own post-process buffers) has
no proven ban history. ENB — a comparable-depth `d3d9.dll`/`opengl32.dll`
proxy that "goes deeper" than pure visual post-processing — has real,
documented ban history on CS/HL-family titles. This project's own core
technique (direct `usercmd_t`/`kbutton_t` struct writes into the live
engine, not just visual buffers) is structurally closer to ENB's category
than ReShade's clean one. **This means the 2-month/zero-bans track record
is genuinely reassuring evidence about THIS specific game's practical VAC
enforcement (an old, likely under-maintained signature set, real and
worth weighing) — but it was never evidence that the base technique sits
in a low-risk category by design.** Both facts stand together: real clean
track record on a real non-trivial-by-category technique, not a low-risk
technique with a track record that merely confirms the obvious.

**(b) The full cross-surface risk matrix (`known_issues.md`, same pass)
still applies, unchanged, and matters directly for scoping `Vulkan` mode's
own rollout**: Solo Campaign is near-zero risk — Valve's own partner
documentation states VAC does nothing in single-player. Survival online
co-op is LOW, closer to solo than to MP — MW3's own Steam Community FAQ
states directly that VAC-banned accounts can still play "Campaign and Spec
Ops... with no restrictions," meaning VAC's enforcement doesn't reach this
mode at all, not just that it's lightly enforced there. Multiplayer is the
one surface where the risk is real, confirmed-active, and non-theoretical.
The hooking technique's own risk (per (a) above) is surface-independent —
it's the SAME technique everywhere — but VAC's actual ENFORCEMENT is not,
which is exactly why gating `Vulkan` mode SP-only (§5 mitigation plan,
below) is doing real risk-reduction work, not just a conservative default:
it keeps the feature entirely off the one surface where either VAC or
`bdAntiCheat` (Demonware's separate system, confirmed unchanged in the
current x64 binaries — see `known_issues_x64.md` issue #7) can actually act
on anything.

**Net correction to this section's own earlier framing**: the "real track
record... narrows the ambient worry" language above is still true, but
should not be read as "so the base technique is probably fine" — under (a),
the base technique was already the deeper, ENB-adjacent category before
`Vulkan` mode is even considered. `Vulkan` mode's full-module-replacement
risk (below) stacks on TOP of that already-non-trivial baseline, not onto
a clean one.

**The real distinction that still stands, and why the track record doesn't
fully retire the new risk**: everything in that 2-month track record hooks
*within* the real, expected system `d3d9.dll` — the module VAC observes is
still the genuine one, same base identity, just with vtable slots redirected.
`Vulkan` mode is a categorically different change: the `d3d9.dll` itself would
no longer be the real one at all, a structurally different implementation
entirely (DXVK's own real translation layer). That's a new risk surface the
existing track record has simply never tested, since the existing technique
never touches the module's own identity. **Both facts belong in this record
together** — the real track record is a genuine reason not to treat this with
DXVK's own maximum-caution framing, but it narrows the general worry about
this game's VAC, it doesn't specifically clear the full-module-replacement
risk category `Vulkan` mode introduces.

**Real, honest mitigation plan, matching this project's own already-trusted
pattern rather than a new argument that the risk is low**:
1. **`Vulkan` mode gated SP-only initially — IMPLEMENTED, 2026-09-23**,
   mirroring this project's own established rollout pattern for every other
   major feature (Sprint, D-pad, menu navigation, MP itself) — VAC's
   confirmed-active surface is `iw5mp.exe`; keeping this mode out of MP
   entirely removes it from VAC's actual enforcement domain to start, not
   just in theory. Direct instruction, same day: "make sure its
   conditional only SP for now... this is qol not an essential feature for
   mp, so until we have real precedent from sp we will bring it to mp."
   Two independent, redundant enforcement points, both build-verified: `d3d9_hook.cpp`'s
   `IsGraphicsApiVulkanModeAllowed()` (checked at `CreateDevice` time) and
   `dllmain.cpp`'s `TryLoadVendoredDxvk()` (checked before `d3d9.dll` is
   even loaded, the real point where DXVK would be substituted in). The
   `GraphicsApi` config value itself stays a single global setting (not
   per-binary) — a player CAN set `Vulkan` while playing MP, it simply has
   no effect there yet, logged clearly either way.
2. **Default OFF, explicit opt-in with a real, honest risk acknowledgment** —
   same shape as the existing MP VAC-risk gate, worded from DXVK's own direct
   disclosure above rather than a softened or reassuring paraphrase.
3. **The real, defensible distinction to state precisely**: the technique
   itself is not a cheat — no gameplay-entity memory reads, no competitive
   advantage, a rendering-only translation layer, consistent with this
   project's own hard-line "never read live gameplay memory" policy that
   already governs everything else this mod does. The open risk is
   detection/signature-matching (an unexpected DLL replacement), not that the
   technique confers any real advantage — these are different claims and
   should never be blurred together in how this is described to players.
4. **If MP support for `Vulkan` mode is ever considered**, it needs its own
   explicit, separate decision and acknowledgment step — this is not covered
   by extending the existing MP-controller-input opt-in policy automatically,
   any more than the reverse would be.

## 6. The 720p-lock connection — a real, existing advantage

Direct user point, worth recording precisely: MW3's own internal render
resolution isn't just often low, it's effectively LOCKED to a fixed low-res
tier regardless of display size (`SAVED_SCREEN` clamped at 1280x720, issue #88,
2026-08-25's own finding). This is exactly the "render low internally, present
high externally" shape DLSS's whole value proposition is built around — and
this project already has the exact mechanism a DLSS integration would need to
control it: `Hook_RenderResCompute` (the same hook `InternalRenderScalePercent`
already uses) overrides the engine's own requested internal render-target size
before the native computation runs. A DLSS integration would reuse this
directly — set the internal resolution to whatever a given DLSS quality mode
(Quality/Balanced/Performance) wants relative to the real output resolution,
rather than needing new native RE work to find and override that sizing logic
from scratch. Real, existing groundwork this project already has, not a new
blocker.

**Real, exact confirmation, 2026-09-24** (`renderer_map.md` §6, from the same
session's renderer-mapping/full-Ghidra-analysis work): the actual render
target a DLSS upscale pass would read its low-res input from is now known
by name and by decompile, not just "somewhere the render-scale hook
controls." `FUN_1401d4040` creates `R_RENDERTARGET_SCENE` (the primary
scene color target) and its depth-stencil, sized directly from
`_DAT_141888670`/`674` — the exact same unclamped globals
`Hook_RenderResCompute`/`FUN_1401bd1d0` already write. Its own real caller
is `FUN_1401d44f0`, an already-known function from the SAVED_SCREEN
tier-clamp work. This closes the "which target, exactly" half of the DLSS
input question with a real, decompiled answer rather than an inferred one.

## 7. Open questions, honestly tracked

1. ~~DXVK's real license, native-Windows precedent, and Vulkan-handle interop
   mechanism~~ — **RESOLVED, section 4**: native-Windows use is real but
   unofficial; a real interop API (`D3D9VkInteropInterface`) exists for the
   Vulkan-handle question, version-pin caveat noted; DXVK's own zlib license
   confirmed earlier. **New open item from this research, not previously
   tracked**: the export-forwarding architecture must change (section 4.5) —
   real implementation work, not just a docs update, once this is built.
2. **Sub-pixel jitter injection — RESOLVED, 2026-09-24. The real projection-matrix-build function is found: `FUN_1401e13e0`, operating on the fixed global render-state struct `&DAT_1415e7c70`.** A 4-round chase (siblings ruled out → `SetVertexShaderConstantF` dispatcher traced → the WORLD×X auto-constant combine found → the writer of the combine's second operand traced) ended in a decisive, disassembly-confirmed find via a whole-binary displacement-reference scan (not decompile-guesswork): `FUN_1401e13e0(longlong param_1)`, gated by a dirty-flag check, resolves the current viewport's render-target dimensions (`FUN_1401df1b0`) and computes classic `tan(fov/2)`-style X/Y-scale terms (`fVar13 = DAT_1403e3e6c`, a real FOV-derived constant) plus near/far-plane-derived Z terms (`DAT_1403e416c`), writing the real D3D9 perspective-projection matrix — row0 `(2*fVar11, 0, 0, 0)`, row1 `(0, fVar12, 0, 0)`, row2/3 packed with the depth-range terms — in duplicate at struct offsets `+0x14d0..+0x1508` and `+0x1510..+0x1548`. (The `+0x1490..+0x14c8` block traced in the prior round turned out to be a separate fixed constant-table copy, not the projection matrix itself — a real correction to that round's own hypothesis, but the chase's overall direction was still correct, since it led straight to the real function via the same struct.)
   - **Struct identity confirmed**: `param_1` is not per-material — every real caller passes the single fixed global `&DAT_1415e7c70`, the canonical per-frame render-state struct. Two real callers found: `FUN_14018e720` calls `FUN_1401e13e0` **twice per frame** — once after the shadow-map-pass viewport setup, once after the main scene-pass viewport setup — exactly the once-per-viewport-per-frame cadence a jitter hook needs (and a real, useful bonus: this confirms the shadow pass and the main scene pass each get their own independent projection-matrix build, relevant if jitter should ever need to be scoped to the main pass only). `FUN_1401950a0` calls it too, in a scene-submission/present-adjacent context.
   - **Real hook point, now concrete, and LIVE-CONFIRMED, 2026-09-24**: `Hook_ProjectionMatrixBuild` (`analog_input_hooks_x64.cpp`), a read-only, log-and-call-through diagnostic hook on the real signature-scanned `FUN_1401e13e0`, is build-verified, deployed, and confirmed live during actual play — 26 heartbeat log lines captured (fire count into the 100,000s), row0/row1 matching the predicted `[Xscale,0,0,0]`/`[0,Yscale,0,0]` shape exactly across several distinct real zoom/FOV states, clean session end, zero crashes/exceptions. This is now a fully confirmed hook point, not just a plausible RE lead.
   - **REVISED, 2026-09-24 — the full 16-float matrix layout is now mapped, and it is NOT the textbook D3D9 perspective form, which blocks the naive jitter write.** A full decompile of `FUN_1401e13e0` (not just its prologue) found: Row0 `[2*fVar11,0,0,0]` (X-scale) and Row1 `[0,fVar12,0,0]` (Y-scale) confirmed exactly as the live diagnostic already showed; **Row2 (`+0x14f0`) is entirely zero** (unlike the standard `[0,0,zf/(zf-zn),1]` form); Row3 (`+0x1500`) packs `[fVar1-fVar11, fVar13/height+fVar13, 1.0f, 1.0f]` — near/far-derived terms plus two hardcoded `1.0f` sentinels, not the standard row3 shape either. This is a genuine, nonstandard, engine-specific layout — strong evidence the vertex shader consumes these 16 floats via custom scalar/per-component reads, not a literal `mul(pos, mat4)` dot product, meaning the standard "jitter goes in `M[2][0]`/`M[2][1]`" convention cannot be assumed to transfer here. Writing jitter into the currently-zero Row2 slots (the positionally obvious choice) has no evidence the shader even reads Row2 at all.
   - **Real, separate correction found in the same pass**: `+0x1490..+0x14c8` (traced in round 3 as the WORLD-combine's second operand) is confirmed to be a **static, unconditional copy from a fixed global constant table** — zero dependence on FOV/viewport size, NOT the per-frame camera matrix. The real per-frame-varying matrix is `+0x14d0`/`+0x1510` (exactly what the diagnostic hook already logs and live-validated) — round 3's lead was chasing an unrelated static matrix, corrected here, though nothing shipped was affected since the diagnostic hook was independently validated against the right target.
   - **Real, ready-to-use confirmation**: viewport width/height (needed for the Halton(2,3) phase-count formula, `8*(displayWidth/renderWidth)^2`) are directly available from `FUN_1401df1b0`'s own output (`local_b0`=width, `local_ac`=height) — no further RE needed for this piece. A tangential bonus finding: `FUN_1401df1b0` also contains a previously-undocumented smooth render-scale-transition interpolation, relevant to any future `InternalRenderScalePercent` work, not this item.
   - **RESOLVED, 2026-09-24 — the real jitter-injection offset found via a real, in-house empirical technique (no RenderDoc/PIX/shader-bytecode tooling needed after all).** Rather than standing up new live-shader-inspection tooling, this project built a strictly opt-in, one-candidate-per-process-launch visual probe (`ProjectionMatrixJitterProbeEnabled`/`ProjectionMatrixJitterProbeCandidateIndex`, `Hook_ProjectionMatrixBuild`) and worked through all 8 plausible non-scale float slots one relaunch at a time, watching for real visual effects live. Full, decisive result: Row0[2]/[3] and Row1[2]/[3] are load-bearing but wrong (feed output depth/W from screen position — real but unusable for jitter, see the "future creative-effects lead" note below); **Row2[0]/[1] are confirmed genuinely dead** (zero visible effect under two independently-tested mechanisms, matching the earlier full-decompile finding that Row2 is entirely unused); **Row3[0] (`+0x1500`) and Row3[1] (`+0x1504`) are confirmed, live, real screen-space translation terms** — direct user confirmation of a clean, uniform full-screen slide with no artifacts: "confirmed full screen left right" (Row3[0]) and "up down this time" (Row3[1]). This is the real, empirically-proven jitter-injection target on BOTH duplicate copies (`+0x1500`/`+0x1504` and `+0x1540`/`+0x1544`).
   - **Real, honest remaining gap before this is production-ready**: the QUANTITATIVE relationship between a written perturbation value and the actual number of screen pixels shifted is not yet calibrated — the probe's own 0.25 amplitude already produced a dramatic full-screen slide, meaning the real per-unit-to-pixel scale factor is small and currently unknown; a genuine sub-pixel Halton(2,3) jitter offset needs this calibrated before it's written for real, not guessed. Also unresolved: which of the three real callers (`FUN_14018e720`'s two call sites, `FUN_1401950a0`) is the true "per-rendered-frame, main-view" one to gate jitter to (excluding the shadow pass) — the earlier return-address-based gate attempt never produced any observed activity and was never root-caused, so the real implementation (below) applies uniformly to every call for now, a known, flagged simplification.
   - **A real, unplanned "future creative-effects" bonus lead from this same investigation**: Row0[3]/Row1[3] (columns W) make the perspective-divide denominator depend on screen position instead of depth, producing a clean, controllable radial "warp speed"/vanishing-point effect — a real, legitimate candidate for a deliberate visual effect (killstreak activation, near-death vignette, etc.) using an already-confirmed-safe write target, not scoped as a work item, recorded here so it isn't lost.
   - **Real implementation status**: `Hook_ProjectionMatrixBuild` now writes an actual Halton(2,3) jitter offset into `+0x1500`/`+0x1504` (both duplicate copies) — see `analog_input_hooks_x64.cpp`. Gated behind `[Video] ProjectionJitterEnabled` (default OFF) — deliberately not enabled by default, since (a) the pixel-scale calibration above is still an honest placeholder, and (b) there is no temporal accumulation/resolve pass in this project yet to consume real jitter, so enabling this today would just add a faint, unresolved shimmer with no benefit, exactly like the earlier `ProjectionMatrixJitterProbeEnabled` diagnostic's own documented caveat.
3. **Motion-vector reconstruction — REVISED AGAIN 2026-09-23, direct instruction to prioritize real, engine-sourced per-object motion, not the camera-only baseline alone ("we want proper motion vectors... we may have to surface them off of engine data").** Camera-only reprojection (section 2.5) stays real and correct for static world geometry, and Streamline's own buffer semantics ("Object and **optional** camera motion vectors," `ProgrammingGuide.md` line 763) confirm the two combine rather than being an either/or choice — but "proper" now means real, from-scratch work to surface actual per-object (and, for characters, per-bone) motion from IW5's own engine data (section 2.6): capturing the real current+previous-frame vertex-shader constants (transform, and bone-matrix palette for skinned draws) the game already uploads via `SetVertexShaderConstantF`, and replaying the same real vertex/index buffers through a small, hand-authored velocity shader pair — not patching IW5's own precompiled shaders, and not a generic optical-flow approximation. **REVISED AGAIN, 2026-09-24 — the per-entity-identity question is now RESOLVED, but a real, honest complication surfaced alongside it, not previously flagged.** `cell scene ent` (stage `0x03`) is confirmed (direct offset arithmetic, `renderer_map.md` §5) to share the exact same array/index space as `add scene ent`'s (stage `0x11`) largest category — a plain integer index per entity category, gated by the engine's own already-maintained "active this frame" flag, which doubles as the real motion-vector-invalid sentinel (a newly-active index has no valid previous-frame transform). **This is the real, stable per-entity identity this feature needs — settled, not something to re-derive.** The complication: the earlier working assumption that `FUN_1401c7aa0` (largest record, `0x90`-byte stride, a transform-adjacent field) is specifically "dynamic/skinned models" turned out to be built on a wrong premise — `FUN_1401c7aa0` and its two siblings all tail-call into a SHARED function (`FUN_1401c7660`) that's genuinely a spatial-cell REGISTRATION + nearest-reverb-zone system, not a draw-dispatch path. `FUN_1401c7aa0` remains the best-supported candidate on parameter-shape grounds alone, but this is now a real, weaker, unconfirmed claim — don't carry forward the old confidence level. **Net effect on buildability**: Stage 1 (camera-only reprojection, section 2.5) is fully specified and buildable today, independent of any of this. Stage 2 (real per-object motion) has its identity-scheme question closed, but still has two real open pieces before it's actually buildable: (a) confirming which category is really "dynamic models" given the reframing above, and (b) the skinned-draw bone-palette vertex-format convention (`skin model`, stage `0x10`) — never decompiled, exactly as open as before this session. Neither is a blocker in principle, both are real, scoped, not-yet-done RE tasks — Stage 2 should not be treated as ready.
4. **Streamline/DLSS SDK license terms — RESOLVED, section 2.4.** Read in
   full this session, real distinct terms from two different licenses (the
   SDK/repo's own permissive MIT-style one vs. the actual redistributable
   binaries' own RTX SDKs License/DLSS EULA). Redistribution alongside this
   project's own application is real and permitted (material-additional-
   functionality + no-standalone-distribution conditions both trivially
   satisfied). **One real, genuinely unresolved item carried forward, not
   closed**: whether a free, never-sold community mod counts as a
   "commercial release" under NVIDIA's own pre-release notification clause —
   needs a direct answer from NVIDIA's own stated contact channel before
   `Vulkan` mode's DLSS support ever ships publicly, not assumed either way.
   Also real and new: a mandatory NVIDIA-Marks attribution/credits
   requirement, and a real licensing-structure task (keep the redistributed
   binaries under their own separate license, same precedented pattern this
   project already uses for `tools/iw5oat`'s GPLv3 and `security/`'s own
   license, never folded into this repo's own free-to-fork grant).
5. **`slSetVulkanInfo`'s own exact requirements — RESOLVED, section 2.7.**
   `ProgrammingGuideManualHooking.md` read in full this session. Instance/
   device-creation proxying is confirmed optional (DXVK can own real Vulkan
   device creation, this project calls `slSetVulkanInfo` manually after) —
   but §5.2.1's own requirement to call `slGetFeatureRequirements` and fold
   the returned extensions/features/queue counts into whatever creates the
   real device (DXVK, not this project's own code) is real and not yet
   solved — see item 7 below, a new item this reading surfaced.
6. **The real hook-ordering/coexistence question**: where in this project's
   own existing `CreateDevice`/`EndScene`/`Reset` hook sequence a DXVK-backed
   Vulkan device would need to be created, and whether this project's own
   existing hooks (which currently assume a real D3D9 device/swapchain) need
   restructuring for `Vulkan` mode specifically, versus staying untouched for
   the `Legacy D3D9` default path.
7. **NEW, 2026-09-23 — real Vulkan swapchain-interception and DXVK-extension-injection questions, section 2.7.** Streamline's own manual-hooking guide marks Vulkan swapchain calls (`vkCreateSwapchainKHR`/`vkAcquireNextImageKHR`/`vkQueuePresentKHR`/etc.) as MANDATORY to route through `sl.interposer.dll`'s own proxies — calls DXVK makes internally, not this project's own code. The real, promising, not-yet-confirmed hypothesis: the same DLL-search-order proxy technique this project's entire `d3d9.dll` injection already relies on should apply identically to `vulkan-1.dll` if `sl.interposer.dll` is placed/renamed to intercept DXVK's own load of it — genuinely cheap to test once `Vulkan` mode implementation starts, not yet attempted. Separately, real and unresearched: whether DXVK exposes any extension-injection mechanism (env vars, `dxvk.conf`, or a real source patch) to satisfy Streamline's own required Vulkan 1.2/1.3 features and extra queues at device-creation time.
8. **NEW, 2026-09-24 — FSR and RT status re-checked against the same session's full renderer-mapping/analysis work.** Direct instruction to continue the renderer-replacement roadmap ("lets continue with our full renderer replacement by adding our next steps into the visual enhancement suite, RT, DLSS, FSR etc") prompted a deliberate re-check of all four fronts, not just DLSS.
   - **RT: the "structurally blocked" conclusion (RTX Remix is fixed-function-only, IW5 is shader-based) still holds unchanged.** Nothing about the real command-buffer/backend split or render-target architecture mapped this session bears on that specific mismatch — a real frontend/backend split doesn't make an engine fixed-function.
   - **One genuinely new, real, LONG-TERM angle worth recording (explicitly not a near-term work item)**: once `Vulkan` mode exists and DXVK is presenting a real native Vulkan device, a hybrid/screen-space RT effect (e.g. ray-traced reflections via `VK_KHR_ray_query` against the existing depth buffer) becomes at least theoretically approachable as a genuine Vulkan-native compute pass layered on DXVK's own output — categorically different from Remix-style full path-tracing/geometry replacement (still blocked), closer to the same tier of thing the native SSAO implementation found dormant in the binary this session (section 6's sibling finding, `renderer_architecture_map.md` §5b) already occupies. Not scoped, not sequenced, a real idea for a future pass once `Vulkan` mode itself ships — recorded here so it isn't lost, not promoted to a work item.

9. **REVISED SAME DAY, real scope escalation: "FSR gets full fsr 4 upgrade, not the crappy sharpening only 1.1 were using."** Item 8's original "FSR: no new next step" verdict was correct for staying at FSR 1.0 RCAS, but that's not the actual goal — real research (WebSearch/WebFetch against AMD's own GPUOpen docs and the FidelityFX SDK GitHub repo, not assumed) found **FSR 4 itself is genuinely not viable for this project, on two independent, verified, hard blockers**:
   - **API mismatch**: FSR 4 is DirectX 12-only (`HLSL CS_6_6`, confirmed directly from AMD's own FSR4 technique manual, no Vulkan backend exists at all). This project's entire modernization path is D3D9→Vulkan via DXVK — there is no D3D12 anywhere in the plan, and building one would be a whole separate, unplanned architecture.
   - **Hardware exclusivity**: FSR 4 is RDNA4-only (AMD Radeon RX 9000-series and above, confirmed directly from AMD's own FSR4 GPUOpen release announcement) — not NVIDIA, not Intel, not even older/mainstream AMD GPUs. Unlike FSR 1.0's RCAS (pure open reference math this project already hand-ported directly into its own shader pipeline), FSR 4 is a real trained ML model distributed only as a signed proprietary binary (confirmed: "prebuilt, signed DLLs," "limited source" only) — its actual model weights cannot be hand-ported the way FSR 1.0 was, only used as-is via AMD's own DX12-only SDK.
   - **Real target instead, confirmed by direct instruction ("FSR3,1 is good"): FSR 3.1**, AMD's vendor-agnostic *temporal* upscaler — real open reference algorithm (not a proprietary trained model), runs on any GPU, not AMD-exclusive. Its real data requirements (confirmed directly from AMD's own FSR 3.1 technique manual): sub-pixel jitter applied during rendering (same real contract already fully specified for DLSS, section 2.3), 2D screen-space motion vectors (same real per-entity-identity/motion-vector work already underway for DLSS, section 2.6), and a single-float depth buffer. **This is genuinely good news, not just a fallback**: the jitter-injection and motion-vector RE work already in progress for DLSS/Streamline serves FSR 3.1 too — one investment, not two separate efforts. AMD's own current FidelityFX SDK 2.x (the version bundling FSR4) lists Vulkan support as a live, open "known issue" for the packaged SDK itself — genuinely irrelevant either way, since the real integration path here (matching FSR 1.0's own precedent) is hand-porting FSR 3.1's own published reference shader math directly into this project's existing full-screen-pass pipeline, not depending on AMD's own SDK/backend abstraction layer at all.
   - **FSR 4 recorded as a real, closed, hardware/API-gated dead end** — same honest treatment as the RTX Remix finding, not silently dropped. If AMD ever ships a genuinely vendor-agnostic or Vulkan-capable version of the same ML-upscaling tier, revisit; not expected on the current evidence.
   - **Real next step, not yet started**: port FSR 3.1's own real reference algorithm (once its own technique manual is read in the same depth the FSR 1.0/RCAS math already was) into this project's shader pipeline, on top of the jitter/motion-vector groundwork shared with DLSS above.

10. **NEW, 2026-09-24 — the jitter-injection candidate (`FUN_1401d8f70`) is decompiled and RULED OUT; a real bonus lead surfaces instead.** Full decompile: this function computes small integer tier selectors and texel-size-reciprocal floats, indexing into the exact same render-target/view surface-pointer array `FUN_1401dfd80` (the render-view activator) reads from — real, decompile-confirmed shape of a **shadow-map quality-tier/render-target selection function**, not camera/projection math at all. This is very likely the real function behind `known_issues.md` issue #107's own explicitly-unlocated "shadow-map resolution" search (shadow map *creation* was found via the generic render-target table, section 5b of `renderer_architecture_map.md`, but resolution/quality-tier control specifically was searched for and never found) — a genuine, unplanned bonus lead, worth a dedicated pass in its own right, separate from the DLSS jitter question. **The fallback trace also came up empty**: `FUN_1401d9a10`→`FUN_1401d9130`→`FUN_1401da230` (the other named "view/camera-adjacent" sibling in `FUN_1401d7480`'s chain) is confirmed to be **fog/DOF/vignette/color-grade parameter marshaling** (a literal diagnostic string, `"Depth of field used..."`, confirms this), not camera setup — a real correction to `renderer_architecture_map.md` §3's own "view/camera/FOV-adjacent setup" characterization, which was too generous for at least this branch.
    - **Round 2, same day: the 5 remaining `FUN_1401d7480`-chain siblings decompiled — none is the matrix builder, but the SetVertexShaderConstantF call-site scan found the real dispatcher family instead.** `FUN_1401d8910` (a viewport-rect cache, not a 4x4 matrix), `FUN_1401d3660` (a trivial dirty-flag dispatch), `FUN_1401d9b40`/`FUN_1401d5c10` (audio-listener-position notify dispatches, vec3 not matrix), and `FUN_1401d7940` (material/pass-name registration) are all ruled out. A direct `CALL[reg+disp32]` scan of vtable slot `0x2F0` (`SetVertexShaderConstantF`) found only 6 real call sites, all routing through one shared generic per-material shader-constant dirty-update dispatcher (`FUN_1401db1d0`/`db3b0`/`db5c0`/`dba50`/`dbd20`) — confirming there is no separately-spottable "upload the projection matrix" call site; it goes through the same generic per-constant path as every other shader constant. That dispatcher's "codegen" branch resolves through `FUN_1401deda0`, a genuine id-Tech-style **auto-constant resolver** (`switch` on codegen-constant-type index, dirty-cached, matching the classic WORLDMATRIX/WORLDVIEWMATRIX/WORLDVIEWPROJECTIONMATRIX auto-constant pattern).
    - **Round 3, same day: `FUN_1401deda0`'s case 4/8 decompiled — a real, confirmed WORLD × [cached matrix] combine found, the strongest lead yet.** Case `4` (`FUN_1401de4c0`) is a straight 64-byte/16-float copy refreshing a cached **WORLD** matrix at `param_1+0x100`. Case `8` (`FUN_1401de520`) calls a confirmed real 4x4×4x4 matrix-multiply function (`FUN_1402ba1c0`, every one of its 16 output terms is a genuine 4-term row·column dot product) combining `param_1+0x100` (WORLD) × `param_1+0x1490` (an unidentified cached matrix) → `param_1+0x200`. By the standard id-Tech auto-constant naming convention (WORLD × X = uploaded combine result), **`param_1+0x1490` is the strongest candidate yet found for the cached VIEWPROJECTION (or VIEW) matrix** — i.e. very likely the real pre-jitter camera matrix, with the actual jitter-injection point being whichever function WRITES `param_1+0x1490`. The full `FUN_1401deda0` switch is now fully mapped: case `0` (8-float table copy, already ruled out), case `0xc` (translation/billboard offset math, not camera), case `0x10` → `FUN_1401dec40` (untraced), cases `0x18/0x1c/0x24/0x28/0x30/0x34` → a parameterized `FUN_1401de5c0`/`FUN_1401de630` family (untraced, likely per-axis/per-light-type variants). **The real projection-matrix-build function is still not conclusively found, but the chase has now narrowed to one concrete next step**: trace the writer of `param_1+0x1490`. 3 genuine rounds in on this specific chase, well under this project's own 5-6+ "Fresh Perspective" threshold — round 4 (tracing that writer, plus identifying `param_1`'s own real origin/identity) is in progress as of this entry.

11. **NEW, 2026-09-24 — the "which entity category is dynamic models" question gets real new evidence, still not confirmed.** Two of the four original leads chased: (1) whether `skin model` (stage `0x10`) touches any of the three category record arrays directly — clean negative, zero references found. (2) A real, new finding in `cell scene ent` (stage `0x03`): the `0x90`-stride category (`FUN_1401c7aa0`, the existing best-supported candidate) gets measurably more elaborate culling treatment than the `0x58`-stride one — two separate frustum-plane-test loops instead of one, plus a branch to one of two distinct downstream handlers based on a struct flag. Both downstream handlers were decompiled and confirmed to be generic (the already-known stage-requeue mechanism, and a generic secondary-visibility-test utility) — **no bone/skeleton/animation-specific content found in either**, so this is real supporting evidence, not confirmation. `FUN_1401c7aa0` remains the best-supported candidate, marginally more so than before, still genuinely unconfirmed. **Not yet attempted**: tracing the three "active this frame" flag-array writers back to their real source (the most likely angle to actually settle this), and direct struct-field diffing against `IW5::XAssetType`-tagged asset references.
    - **Round 3, same day: a real correction, not a confirmation.** Traced the category-1 active-flag array's own writer chain to the real per-frame entity-category dispatcher, `FUN_1401a5830` — confirmed it walks FIVE distinct categories (not three), each with its own active-flag test, category-specific visibility function, and category-specific submission function. A THIRD category (distinct from the previously-favored `0x141c23028`/stride-`0x90` category 1) was found with unambiguous, inline real 3D transform math — for each active entity it fetches a 7-float rotation-basis+translation block (`FUN_1402bc220`) and computes a textbook bone/tag-attachment world transform (`local_c0 = local_84*fVar1 + local_90*fVar2 + local_78*fVar3 + translation`, done explicitly, not hidden in a callee) before submitting via the same draw path as categories 2/4/5. This is real transform math, stronger evidence than the culling-complexity inference categories 1's own case rested on — but it points at a DIFFERENT category, correcting rather than confirming the prior lead. This category's own base isn't a flat `DAT_` array like the others; it resolves indirectly through `*(longlong*)(DAT_141887c30+0x278)`/`+0x380` (likely a per-level attachment/tag-list table, not yet confirmed). **Real, concrete next round**: decompile whatever populates `DAT_141887c30+0x278`/`+0x380` and check whether it's keyed to `XAssetType` xmodel/xanim references — not exhausted, round 3 of 5-6+, a clear next lead exists.
    - **Round 4, same day: category 3 fully decoded and CORRECTED again — it's a tag-attachment subsystem, not the base dynamic-model array.** `DAT_141887c30` is a fixed global struct; `+0x278` is a 6-byte-stride attachment-record array (each record's `+4` ushort is the PARENT entity/slot index), `+0x380` is that array's own active-flag bytes (indexed by the same ushort). Two more real arrays feed the transform: `DAT_141ef56a0` (stride `0x20`, local offset/rotation, converted to a 3x3 basis via `FUN_1402bc220`) and `DAT_141ef5690` (stride `0x78`, whose `+0x20` field is a pointer into the PARENT entity's own live data, read at `+0x164/+0x168/+0x16c`). The full inline transform (`childBasis` dot `parentBasis` plus `childTranslation`) is a textbook parent-child tag-attachment combine (weapon-in-hand, scope-on-weapon, turret-on-vehicle, helmet-on-head) — real, needs correct motion-vector handling since it moves with its parent, but it is architecturally DIFFERENT from whatever holds the base skinned-character records; not the "one true dynamic-models category" this question is chasing. **One real, decisive-looking, not-yet-confirmed lead for round 5**: the parent-pointer dereference at `+0x164/+0x168/+0x16c` is too far into the target (364 bytes) to be a category-1 record (stride only 0x90 = 144 bytes) — strongly suggesting it targets the parent's own live BONE-MATRIX PALETTE (offset ~0x164, roughly bone index 7 in a packed array). If confirmed, finding what populates/owns that palette pointer would likely answer "which category is the base skinned-model array" directly (the palette owner IS that category's record) AND close the `skin model` (stage `0x10`) bone-convention question from section 2.6 point 3 in the same pass. Not chased further this round — a real, clear next lead, not a dead end, round 4 of 5-6+.
    - **Round 5, same day, done directly (no fork): a genuine partial dead end on the specific approach tried, honestly recorded.** Scanned for real writers of `DAT_141887c30` itself (the base whose `+0x278`/`+0x380` fields feed category 3) via a whole-binary reference scan. Found exactly 3 real write sites (`FUN_1401bdf00`, `FUN_14019f850`, `FUN_14019f4a0`) — all three decompiled, and all three are teardown/reset helpers (each zeroes `DAT_141887c30` alongside a whole cluster of related per-level globals, `DAT_141888640/648/650/658/660` etc., consistent with a level-unload/state-reset sweep) — **none of the three actually WRITES a real nonzero pointer into it.** The real "set to a live value" writer was not found among these three, meaning it likely uses an addressing form the reference scan's write-heuristic doesn't catch (e.g. assigned indirectly through a different base register/offset chain rather than a direct `MOV [DAT_141887c30], reg`). This specific angle (find the writer of the base pointer itself) is a genuine, real dead end as attempted — not proof the question is unanswerable, but this exact technique didn't resolve it. **Not yet tried**: search for the real allocation/init site instead (a function that `malloc`/engine-allocator's a block sized to match category 3's real struct layout, likely findable via the SIZE of what gets zeroed alongside it, or by searching for the bone-matrix-palette pointer read at the parent's `+0x164` directly rather than working backward from `DAT_141887c30`). Round 5 of 5-6+ — approaching, not yet at, the project's own "Fresh Perspective" threshold; the next round should try a different angle (the `+0x164` bone-palette lead directly) rather than another variant of "find who writes this specific global."

12. **NEW, 2026-09-24 — FSR 3.1's REAL pass structure, buffer contract, and jitter formula, found directly from AMD's own published source (`GPUOpen-LibrariesAndSDKs/FidelityFX-SDK`, `Kits/FidelityFX/upscalers/fsr3/`), closing the gap the technique manual itself explicitly declined to document.** A direct `WebFetch` of AMD's own FSR3.1 upscaler manual page returned real buffer/jitter facts but explicitly stated it does NOT document internal pass structure or named techniques ("this is an integration guide... not an algorithm reference") — rather than accept that gap, went straight to the real, MIT-licensed (confirmed via file header: "Copyright (C) 2026 Advanced Micro Devices... Permission is hereby granted, free of charge... to deal in the Software without restriction") HLSL/C++ source itself via `gh api`, the same real-primary-source standard this project already holds for every other RE claim.
   - **Real pass list, `ffx_fsr3upscaler_*` shader files** (`Kits/FidelityFX/upscalers/fsr3/internal/shaders/`): `prepare_inputs_pass` → `prepare_reactivity_pass` (reactive-mask handling) → `luma_pyramid_pass` (compute-luminance mip chain, downstream of SPD) → `shading_change_pass` + `shading_change_pyramid_pass` → `luma_instability_pass` → `accumulate_pass` (the real temporal-history reprojection/blend core) → `rcas_pass` (the exact same sharpening filter this project already hand-ported for FSR 1.0 — direct code reuse opportunity, not a new algorithm to learn). Legacy `ffx_fsr2_*` passes (`depth_clip_pass`, `reconstruct_previous_depth_pass`, `lock_pass`, `autogen_reactive_pass`, `tcr_autogen_pass`, `compute_luminance_pyramid_pass`, `debug_blit`) sit alongside and are still real, live dependencies of the 3.1 pipeline (FSR 3.1 upscaling is a revision/extension of the FSR2 temporal core, not a clean-room replacement of it — confirmed by the shared file layout itself).
   - **Real buffer contract** (from the technique-manual fetch, cross-checked against the same fields' real use in the shader headers): color (app format), single-channel `R32_FLOAT` depth, 2-channel `FLOAT` screen-space motion vectors, optional `R8_UNORM` reactive mask, optional `R8_UNORM` transparency-and-composition mask, `R32_FLOAT` 1×1 exposure value, single upscaled color output at present resolution — the exact same shape (color/depth/motion-vectors/reactive-mask/exposure) as the DLSS/Streamline contract already fully specified in section 2.2, confirming section 9's "one investment, not two separate efforts" framing was correct, not just hopeful.
   - **Real jitter formula, found in the actual host-side C++ source** (`ffx_fsr3upscaler.cpp`, functions `ffxFsr3UpscalerGetJitterPhaseCount`/`ffxFsr3UpscalerGetJitterOffset`), not just described in prose: `jitterPhaseCount = 8 * (displayWidth / renderWidth)^2` (a real, exact formula — e.g. 2x upscale → 32-phase sequence, matching the manual's own quoted 18-72-phase range for 1.5x-3.0x quality tiers), and the offset itself is a genuine Halton(2,3) low-discrepancy sequence (`halton(index, 2)` for X, `halton(index, 3)` for Y, each `- 0.5f` to center on the pixel), cycling via `index % phaseCount`. This is the identical Halton(2,3) sequence already established for DLSS (section 2.3) — a single shared jitter generator can drive both upscalers, real confirmation, not assumption.
   - **Real scope/complexity estimate for a hand-port** (direct line counts via `gh api` + `curl`, not guessed): the core algorithm math lives in `Kits/FidelityFX/upscalers/fsr3/include/gpu/fsr3upscaler/` (12 headers, real per-technique separation: `accumulate.h` 172 lines, `common.h` 403, `upsample.h` 644, `reproject.h` 79, `sample.h` 628, `prepare_inputs.h` 152, `prepare_reactivity.h` 283, `rcas.h` 112, `luma_pyramid.h` 192, `luma_instability.h` 115, `shading_change.h` 68, `shading_change_pyramid.h` 297 — **≈3,145 lines of real algorithm math**), plus 7 thin `.hlsl` pass-entry wrapper files (≈467 combined lines) that just bind resources and call into the headers above. **Total ≈3,600 real lines** — roughly 20-30x the size of the single-pass FSR 1.0 RCAS port this project already shipped, a real, honest complexity jump (a genuine multi-pass temporal accumulator with its own history/lock/reactive-mask/luminance-pyramid machinery, not a single stateless filter), but still fully open, hand-portable HLSL/HLSL-adjacent reference math under a real permissive license — not a black-box SDK dependency the way FSR 4's signed ML binary would have been.
   - **Real next step, concrete and scoped**: once the shared jitter-injection hook (item 2/10 above) and per-entity motion-vector identity work (item 3/11 above) land for DLSS, the same data feeds a hand-port of these 12 headers directly into this project's own shader pipeline, starting with `accumulate.h`/`reproject.h`/`sample.h` (the real temporal core) and finishing with `rcas.h` (near-zero new work, given the existing FSR 1.0 RCAS port). No further blocking research question remains for FSR 3.1 specifically — this item closes the "read the FSR 3.1 docs" task from the 2026-09-24 "lets read docs on all" instruction.

13. **NEW, 2026-09-24 — DLSS hardware-tier compatibility, CLOSED, real per-feature gating design now specified — direct response to the explicit hard requirement "DLSS must have all versions working (so it's not restricted to only top end nvidia gpus)".** Real research against NVIDIA's own documentation (the DLSS Supported Cards guide, cross-checked against `github.com/NVIDIAGameWorks/Streamline`'s own `ProgrammingGuide.md`) confirms this requirement is fully satisfiable, not a real blocker:
    - **Super Resolution and DLAA (the native-scale variant of the same network) work on EVERY RTX generation, 20-series through 50-series** — no special hardware beyond generic Tensor Cores. This is the real, correct baseline feature for this project's default `Vulkan` mode DLSS path — never gated behind anything beyond "has an RTX GPU at all."
    - **Ray Reconstruction** also runs on the same Tensor Cores, RTX 20-series and up — no extra hardware dependency, relevant if this project's own future RT roadmap (item 8 above) ever lands.
    - **Frame Generation** originally shipped RTX 40-series-only (needs Ada Lovelace's Optical Flow Accelerator); NVIDIA's own "DLSS 4" update (Jan 2025) extended standard Frame Generation down to RTX 30-series. **Multi Frame Generation** (the newest, top tier — up to 5 generated frames per rendered frame) needs 5th-gen Tensor Cores and is RTX 50-series-exclusive.
    - **Real, confirmed runtime capability-query API — exactly the mechanism this requirement needs**: `slIsFeatureSupported(sl::Feature feature, const sl::AdapterInfo& adapterInfo)`, called per-feature against the real GPU's LUID, returns `eOk` if supported or a specific real error otherwise (`eErrorOSOutOfDate`/`eErrorDriverOutOfDate`/`eErrorNoSupportedAdapterFound`/`eErrorAdapterNotSupported`). A companion call, `slGetFeatureRequirements(sl::Feature feature, sl::FeatureRequirements& requirements)`, returns the real per-feature rendering-API/viewport/extension requirements.
    - **Minimum driver version, confirmed**: NVIDIA driver 512.15+ (stated directly in the Streamline SDK's own README).
    - **Real, concrete design this closes out**: gate each Streamline feature independently via `slIsFeatureSupported` at `Vulkan` mode init time (per-adapter, not a single blanket capability flag), default-enable Super Resolution/DLAA as the one universal baseline (RTX 20+), and expose Frame Generation/Multi Frame Generation as separate opt-in toggles that cleanly report "unsupported on your GPU" and no-op on older hardware rather than blocking the whole DLSS feature set behind the newest tier. This directly satisfies the stated requirement — no player with any real RTX card is locked out of the core upscaling feature, and newer owners get the extra tiers additively, not as a precondition for the rest. No further open research question remains here; this is now a real, specified implementation design, not a research gap.
