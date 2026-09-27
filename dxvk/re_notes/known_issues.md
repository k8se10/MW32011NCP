# Known Issues — MW32011DXVK

Real, tracked issues specific to fitting DXVK to Call of Duty: Modern
Warfare 3 (2011)'s IW5 engine. See `README.md` for why this fork exists
and its own real scope. Same conventions as the sibling `MW32011NCP`
project's own issue trackers: a `**Status:**` line first, dated
investigation rounds after, `issue #N` cross-reference form.

---

## Index

- [#1](#1-motion-blur-post-process-pass-produces-no-visible-effect) — Motion blur post-process pass produces no visible effect — **Resolved (not a DXVK bug)**
- [#2](#2-dlss-required-vk_nvx-extensions-never-enabled-on-native-windows) — DLSS-required `VK_NVX_*` extensions never enabled on native Windows — **Partially Resolved**
- [#3](#3-iw5-tessellation-needs-a-standalone-render-pass-bridge) — IW5 tessellation needs a standalone render-pass bridge — **Investigating**

---

## #1: Motion blur post-process pass produces no visible effect

**Status: Resolved (not a DXVK bug) — root cause was in `MW32011NCP`'s own game-logic code, fixed there, unrelated to this fork's own source. See the "Resolution" section at the bottom for the full story; the investigation trail below is preserved as-written for the real, useful DXVK-source-level findings it turned up along the way.**

### Summary

`MW32011NCP` (the sibling controller/enhancement mod this DXVK build serves)
ships a real, opt-in `[Video] MotionBlurEnabled` full-screen post-process
pass: a camera-only directional blur, drawn once per frame as a
pre-transformed (`D3DFVF_XYZRHW`) screen-space quad via `DrawPrimitiveUP`,
sampling a captured copy of the real backbuffer through a real, compiled
`ps_2_0` pixel shader.

With `[Video] GraphicsApi=Vulkan` selected (loading this DXVK build in
place of the real system `d3d9.dll`), the game launches and renders
correctly — main menu, in-level gameplay, and `[Video]
InternalRenderScalePercent` (internal render-resolution upscaling) all
confirmed working live, 2026-09-23. Motion blur specifically does not:
the pass runs every real frame with no error of any kind, but produces
**zero visible effect** on screen.

### What's confirmed, via direct investigation on the `MW32011NCP` side

All of the following were confirmed via real diagnostic logging and/or
direct reads of this project's own DXVK source (not assumed):

1. **The pass genuinely executes every frame.** `MW32011NCP`'s own gating
   logic (menu-active / in-level / native `clcState` checks — pure
   native-engine-memory reads, unrelated to which D3D9 backend is active)
   passes, and `DrawFullScreenPass` (the shared capture+quad-draw function
   motion blur, FSR sharpening, and SMAA all use) is genuinely reached and
   called, confirmed via a one-time diagnostic log line.
2. **The pixel shader compiles successfully.** `CreatePixelShader` against
   this DXVK build's own `Direct3DCreate9`-returned device succeeds for the
   real, precompiled `ps_2_0` motion-blur bytecode — no failure logged.
3. **`SetFVF`/vertex declaration is not the cause.** Read directly from
   this fork's own `src/d3d9/d3d9_device.cpp`:
   `D3D9DeviceEx::DrawPrimitiveUP` requires a non-null bound vertex
   declaration to succeed at all (`D3DERR_INVALIDCALL` otherwise), and
   `D3D9DeviceEx::SetFVF` genuinely creates/binds a real, correctly-typed
   `D3D9VertexDecl` for a non-zero FVF value. `MW32011NCP` tried explicitly
   calling `SetFVF(D3DFVF_XYZRHW|D3DFVF_DIFFUSE|D3DFVF_TEX1)` (matching the
   quad's own real vertex layout) immediately before the draw, live-tested
   it — **no visible change**. Reverted on the game-mod side (real, if
   modest, state-mutation risk with zero confirmed benefit) but the source
   read itself stands: declaration state was provably correct during that
   test and still produced nothing, ruling this class of cause out.
4. **The fixed-function-vertex + programmable-pixel-shader combination is
   a real, designed-for DXVK code path, not inherently broken.** Confirmed
   via `D3D9DeviceEx::UseProgrammableVS()`/`UseProgrammablePS()`: a bound
   `D3DFVF_XYZRHW` (`HasPositionT`) declaration correctly routes vertex
   processing through DXVK's own fixed-function emulation
   (`src/d3d9/shaders/d3d9_fixed_function_vert.vert`), independent of
   whether a real programmable pixel shader is also bound (it is, here) —
   this exact combination is real, legitimate D3D9 usage DXVK is built to
   support, not an edge case being hit by accident.
5. **`D3DRS_FOGENABLE` was tried and live-disproven as the cause.** This
   fork's own `UpdateFog()` (`d3d9_device.cpp`) carries a direct comment
   flagging pre-transformed (`PositionT`) vertices as a genuine W-fog edge
   case ("PositionT also implies W fog, some D3D6 jank apparently relies
   on that") — a real, source-grounded lead, since the quad's `rhw=1.0` on
   every vertex is a degenerate input for that computation. `MW32011NCP`
   added an explicit fog-disable (save/force-off/restore) around this
   pass, live-tested it — **"confirmed still no-op."** Kept on the
   game-mod side regardless (a full-screen post-process pass has no
   business being affected by ambient fog state, a real correctness
   improvement independent of whether it was ever the actual cause here),
   but ruled out as the explanation.

### Real, not-yet-tried next steps

- **A genuine live/debug session against this fork's own build**, once a
  working build toolchain exists (see the project-wide "Real status"
  section of `README.md` — Meson build setup is real, unstarted
  groundwork), to observe the actual Vulkan calls DXVK emits for this
  specific draw call and pipeline state, rather than continuing to reason
  about it from source alone.
- **The actual backbuffer-capture step** (`StretchRect` from the real
  swapchain backbuffer into an offscreen, same-size/same-format,
  point-filtered texture) has not yet been directly investigated on the
  DXVK side — real, remaining candidate, not yet ruled in or out.
- **A genuinely useful, zero-code-risk diagnostic already available on the
  `MW32011NCP` side, not yet run**: `[Video] SmaaEnabled=1` +
  `SmaaDebugView=3` ("plain capture-redraw test, no SMAA math") exercises
  the exact same shared capture/quad-draw pipeline, independent of motion
  blur's own pixel-shader math. If that also shows no visible change, it
  isolates the bug to the shared capture/composite pipeline itself, not
  anything motion-blur-specific.

### Resolution, 2026-09-23 (same day) — not a DXVK bug at all

A real, working native-Windows DXVK build toolchain was set up (MSYS2 +
MinGW-w64 GCC + Meson + Ninja + glslang, plus the pinned Vulkan-Headers/
SPIRV-Headers/libdisplay-info/dxbc-spirv submodule commits this fork's
`git subtree` merge preserved as gitlinks but never fetched), producing a
genuine, working, diagnostic-instrumented build of this fork
(`Logger::warn` added to `D3D9DeviceEx::DrawPrimitiveUP` in
`src/d3d9/d3d9_device.cpp`, since reverted — this project's own diagnostic
build was a real, live-tested artifact, not a permanent source change).
Live-testing that build did not itself reveal the cause, but it enabled the
decisive test: **keyboard/mouse motion blur was confirmed working
correctly under this exact DXVK build** ("blur works on vulkan"). If this
fork's own D3D9-to-Vulkan translation were the real cause, K+M motion blur
would show the identical symptom controller did — it didn't. This
conclusively rules out DXVK/the Vulkan backend as the cause.

The real bug was in `MW32011NCP`'s own `proxy_d3d9/src/analog_input_hooks_x64.cpp`
(`Hook_MovementTick`) — a same-day commit (`fa3ae124f`) had replaced the
previously-working controller/gyro-only direct motion-blur delta capture
with a universal pre/post-native-call accumulator diff, intending to add
real K+M support. It did fix K+M, but broke controller: a real, large
single-tick controller-stick delta does not round-trip through the native
compressed usercmd angle-pack step the same way a small, natural mouse
delta does, so the diff under-reported the true controller-intended delta.
Fixed on the `MW32011NCP` side with a hybrid capture (direct controller/
gyro values when they contributed this tick, the diff technique only when
neither did) — see `MW32011NCP/re_notes/known_issues_x64.md` issue #2's
own newest round for the complete root-cause and fix record. Live-confirmed
fixed by the user the same day.

**What this means for this fork going forward**: no real, confirmed
MW3/IW5-specific DXVK patch exists yet. The real, useful outcome of this
investigation is the now-working DXVK build toolchain itself (genuine
groundwork for whatever future MW3-specific DXVK quirk actually does turn
up — see `CLAUDE.md`'s own framing: "the fork will stay useful for future
issues and bugs"), plus five real, source-grounded DXVK behaviors now
directly confirmed correct for this engine's usage pattern (see "What's
confirmed" above) that a future investigation won't need to re-derive.

Also corrected here: the "VULKAN 60 FPS 16.6ms" on-screen corner text
originally cited elsewhere in this project's docs as "DXVK's own built-in
HUD indicator" is a real misattribution — it's RivaTuner Statistics
Server's own overlay, unrelated to DXVK (this fork's own real HUD, gated
behind the `DXVK_HUD` environment variable, was never enabled during this
investigation).

### Cross-references

- `MW32011NCP/re_notes/known_issues_x64.md` issue #2 — the full,
  chronological live-test/investigation trail on the game-mod side,
  including the real positive finding (frame pacing/smoothness under
  DXVK reported as "the best the game has ever felt") independent of this
  specific bug, and the complete root-cause/fix record for the real bug.
- `MW32011NCP/re_notes/x64_migration/vulkan_dlss_pipeline_research.md` —
  the broader Vulkan/DLSS architecture research this DXVK integration
  serves.

---

## #2: DLSS-required `VK_NVX_*` extensions never enabled on native Windows

**Status: Partially Resolved — `dxvk.enableNvCudaInteropNative` built and live-confirmed working (both extensions report `1`, previously `0`). A second, separate patch (`DXVK_VULKAN_LOADER_OVERRIDE`) for Streamline's mandatory swapchain hooks is written and build-verified, not yet live-tested. The actual `slGetFeatureRequirements(DLSS)` call still failed on the same live run (`eErrorFeatureMissing`) — root cause was unrelated to this issue (the DLSS plugin binaries were never deployed alongside the interposer; fixed on the `MW32011NCP` side via embedding, 2026-09-24) — a fresh live run with that fix in place hasn't happened yet.**

### Summary

`MW32011NCP`'s `[Video] GraphicsApi=Vulkan` mode integrates NVIDIA
Streamline (DLSS) directly against the `VkDevice` this DXVK build creates,
registering it via `slSetVulkanInfo`. DLSS on Vulkan needs
`VK_NVX_binary_import` and `VK_NVX_image_view_handle` enabled on that
device — DXVK's own `dxvk.conf` documents them as the "VK_NVX_* extensions
that are required for DLSS".

Upstream `v3.1.1` disables both unconditionally unless running under
winevulkan (`DxvkDeviceCapabilities::disableUnusedFeatures`,
`src/dxvk/dxvk_device_info.cpp`: `!env::isWineVulkan()` in the same
condition as the 32-bit/safe-mode/`enableNvCudaInterop` checks). Upstream
only expects DLSS through dxvk-nvapi under Proton/Wine. On native Windows
no config key or environment variable turns them back on, and a host
cannot add device extensions from outside: DXVK calls `vkCreateDevice`
itself, and this version has no device-import interop API (no
`ImportDevice`/`QueryDeviceExtensions` in `src/d3d9/d3d9_interfaces.h`).

### Patch (2026-09-24)

New option `dxvk.enableNvCudaInteropNative` (`bool`, default `False`,
`src/dxvk/dxvk_options.h`/`.cpp`, documented in `dxvk.conf`). The gate
becomes "winevulkan OR this option", with every other existing condition
unchanged. If device creation fails with the extensions enabled, DXVK's
existing safe-mode retry (`DxvkAdapter::createDevice`) creates the device
without them.

- **Not game-specific**: off by default, so behavior is identical to
  upstream for every game unless a host sets it (e.g. via `DXVK_CONFIG`).
  No executable-detection gate is needed because nothing IW5-specific
  changes. Candidate for proposing upstream.
- **Live-confirmed 2026-09-24**: built via the real MinGW/Meson/Ninja
  toolchain, swapped into `MW32011NCP`'s deployed install, and run against
  the live game with `DXVK_CONFIG=dxvk.enableNvCudaInteropNative=True`.
  The real DXVK device-info log showed both `VK_NVX_binary_import` and
  `VK_NVX_image_view_handle` reporting `1` (previously `0` against stock
  `v3.1.1`) — the gate change works as designed.

### Second patch (2026-09-24): `DXVK_VULKAN_LOADER_OVERRIDE`

DXVK resolves `vkGetInstanceProcAddr` from a plain
`LoadLibraryA("vulkan-1.dll")` (`src/vulkan/vulkan_loader.cpp`) — Streamline's
manual-hooking mode still needs its own `sl.interposer.dll` acting as the
actual Vulkan loader for its mandatory swapchain/present hooks
(`ProgrammingGuideManualHooking.md` section 2.7); `slSetVulkanInfo` alone
isn't sufficient for that. New env var `DXVK_VULKAN_LOADER_OVERRIDE`: when
set to an absolute path, tried via `LoadLibraryA` first, before the normal
winevulkan/vulkan-1 search — unset/empty behaves identically to upstream. A
path rather than a bare name, since a host that extracts/embeds
`sl.interposer.dll` to a private location (as `MW32011NCP` now does, via
`%LOCALAPPDATA%`) can't rely on the normal DLL search order finding it. Not
game-specific, no executable-detection gate needed, same reasoning as the
first patch. `MW32011NCP`'s own `TryLoadVendoredDxvk()` sets this env var
(alongside `DXVK_CONFIG`) whenever `StreamlineEnabled=1`, before DXVK's own
`d3d9.dll` is loaded.

- **Build-verified**: real ninja build, confirmed via a string check on the
  built `d3d9.dll`.
- **Not yet live-tested**: needs a real Streamline session to confirm
  `sl.interposer.dll` actually intercepts DXVK's Vulkan calls correctly
  through this path, rather than just loading without error.

### Remaining work under this issue

- Live-test `DXVK_VULKAN_LOADER_OVERRIDE` against a real Streamline
  session (confirm interception, not just successful load).
- Re-run the live test now that `MW32011NCP`'s own DLSS-plugin deployment
  gap is fixed (`sl.dlss.dll`/`nvngx_dlss.dll` were vendored but never
  reaching the deployed `streamline/` folder — now embedded and extracted
  automatically) — confirm `slGetFeatureRequirements(DLSS)` succeeds and
  inspect the real reported queue/extension/feature requirements.
- Any further Vulkan 1.2/1.3 features or queues Streamline reports as
  required (logged by `MW32011NCP` at init) that DXVK does not already
  enable.

### Cross-references

- `MW32011NCP/re_notes/x64_migration/vulkan_dlss_pipeline_research.md`
  sections 2.7 and 4.4 — the Streamline integration this serves.
- `MW32011NCP/proxy_d3d9/src/streamline_integration_x64.cpp` — the host
  side (`slSetVulkanInfo`, DLSS requirements logging).

## #3: IW5 tessellation needs a standalone render-pass bridge

**Status:** Investigating (2026-09-27) — no tessellation implementation is
included. DXVK now has a fail-closed SP identity gate and an opt-in MinHook
render-pass bridge prototype. The user-provided SP executable was previously
verified in place: its `.text` contains one dispatcher-signature match; the
same signature also matches MP, so the distinct SP PE identity gate is
required. The bridge is not yet live-tested and its stored pass ID is not
consumed by draw classification. Pass-level selection is not
object/static-prop classification.

### Native Windows x64 baseline, 2026-09-27

The clean DXVK subtree was built with its own Meson Release configuration,
not the parent project's MSVC build. The pinned DXVK build submodules were
initialized; Meson 1.10.1, Ninja 1.13.2, MinGW-w64 GCC/G++ 15.2.0, and the
installed Vulkan SDK `glslang` were used. The full default x64 build
completed all 321 Ninja actions and installed `d3d9.dll` (10,798,988 bytes)
outside the repository.

The first Meson setup attempts exposed a toolchain naming mismatch rather
than a compiler failure: the checked-in `build-win64.txt` expects
`x86_64-w64-mingw32-ar`, `-strip`, and `-windres`, while this MSYS2
installation supplies `ar`, `strip`, and `windres`. A temporary copy of
the cross file with those three tool names adapted was kept outside the
repository; the tracked cross file was not changed.

The built and vendored DLLs are both AMD64 PE files and have identical
named D3D9 exports (17 names, with matching ordinals); both contain the
DXVK 3.1.1 version string. The generated DLL is not byte-identical
(10,798,988 bytes vs. 7,286,798 bytes), and its import table differs
because the local MinGW toolchain links against `msvcrt.dll` while the
vendored release imports UCRT API-set DLLs. The fork-only CUDA interop
option is `false` by default and the loader override is inactive when its
environment variable is empty, so neither changes default behavior.
This establishes a buildable v3.1.1 baseline and matching D3D9 ABI surface,
not a live gameplay equivalence test; no game was launched or deployed from
this worktree.

The generated artifact's SHA-256 is
`0B750E29C71F519DFE197C59F7B05DCF3435B295889BF400D54D1E2DF7A06F96`;
the vendored DLL's is
`C126316F7478EAFF1AD8087702EDA5930F2400187B9C8B3592219C34F6632780`.

### Confirmed renderer and translation path

The renderer architecture map establishes that IW5 prepares visible scene
entities and generates draw surfaces on its scene-worker path, while HUD
elements are driven through a separate per-frame UI path
(`re_notes/x64_migration/renderer_architecture_map.md`,
`re_notes/x64_migration/ui_draw_pipeline_map.md`). The game is D3D9/Shader
Model 3 and makes no tessellation-stage calls; tessellation would therefore
be new synthesized behavior, not an existing game stage to enable.

At the DXVK boundary, `D3D9DeviceEx::DrawPrimitive` and
`DrawIndexedPrimitive` (`src/d3d9/d3d9_device.cpp`, around lines 3023 and
3072) receive only D3D primitive type, vertex/index ranges, and counts.
They do not carry an IW5 entity, material role, render-stage ID, or
world/UI classification. However, that is not the only relevant signal:
IW5's render-target dispatcher has a call-scoped pass ID, and its target bind
is synchronous with the D3D9 `SetRenderTarget` call. This makes render-pass
state a concrete candidate for distinguishing scene rendering from later
post-FX/UI work, even though per-draw object identity remains absent.

### NCP render-pass signal and DXVK feasibility correction

The existing NCP implementation uses two distinct engine-side signals:

- `Hook_RenderResCompute` in
  `MW32011NCP/proxy_d3d9/src/analog_input_hooks_x64.cpp` hooks
  `FUN_1401bd1d0` and changes the requested width/height to
  `native * InternalRenderScalePercent / 100` before the engine computes
  render-target sizes. This controls resolution only; it does not classify
  render passes or geometry.
- `Hook_RenderViewSelectDiag` hooks IW5's `FUN_1401dfd80(ctx, id)`,
  documented in `renderer_end_to_end.md` §8.3 as `R_SetRenderTarget(id)`.
  It publishes `g_engineRenderTargetSelectIdX64 = id` only while calling the
  original function, then restores `-1`. That function synchronously binds
  the corresponding D3D9 color/depth surfaces. NCP's
  `Hook_SetRenderTargetX64` calls `TagColorResourceForFrame` during that bind;
  the tagger reads the call-scoped ID through
  `TryGetEngineRenderTargetSelectX64` and accepts color-target IDs 1–4
  (`FRAME_BUFFER`, `SCENE`, `RESOLVED_POST_SUN`, `RESOLVED_SCENE`), rejecting
  saved-screen, depth, shadow, SSAO, and post-FX intermediate IDs. It also
  checks the expected scaled dimensions. This is a semantic target-ID signal
  beyond render-scale heuristics, but the accepted set is for DLSS color
  resources, not a ready-made geometry filter: ID 1 is `FRAME_BUFFER`, which
  the renderer reference identifies as the post-FX output target. Dimensions
  alone were already shown to collide because post-FX intermediates share the
  scene resolution.

DXVK has the other half of a possible bridge: `D3D9DeviceEx::SetRenderTarget`
updates `m_state.renderTargets[index]` in `SetRenderTargetInternal`, and
`DrawIndexedPrimitive` runs on the same device state with that attachment
bound. The IW5 renderer reference shows `RB_DrawView` binds the view's scene
target before opaque/lit and emissive/transparent draw lists, while
`RB_PostFxAndFinish` then binds `FRAME_BUFFER` for post-FX; the frame's 2D
UI/HUD commands execute after the 3D/post-FX work. A plausible tighter
candidate is the scene target ID selected for the `RB_DrawView` geometry
lists: the renderer reference identifies
`view+0x9D0 == 2` as rendering into `$scene`, and the alternate clear path
also binds target 2. That is a better candidate than all IDs 1–4 accepted for
DLSS color.
DXVK target state could then gate draws while that target is bound, subject
to confirming the exact target/ID sequence at the draw calls. This is not
yet verified end to end and does not distinguish BSP, static models, rigid
entities, skinned models, or individual static props: these categories share
the scene view's draw-list execution, and the render-target ID is a pass ID,
not an object ID.

The hook and `TryGetEngineRenderTargetSelectX64` remain NCP-only and are not
consumed by this fork. DXVK now independently scans the main executable's
`.text` and uses upstream MinHook to intercept the dispatcher only after the
supported SP identity check and only when
`d3d9.iw5RenderPassBridge=True`. The detour scopes the current ID in
thread-local state while it calls the original function unchanged; DXVK
consumes that ID synchronously in `SetRenderTargetInternal` and stores it
beside its active RT0 state. Any later untagged RT0 bind clears the ID to
`-1`. This establishes a pass-state association in source, not live behavior
across game passes. The NCP temporary call-scoped variable itself is not
imported or linked. The bridge must still be exercised in-game across scene,
shadow, post-FX and UI/HUD passes before this state can be treated as a
reliable draw selector. A scene-target match alone does not distinguish
static props from other draws.

### Standalone hook/build-gate feasibility, 2026-09-27

The NCP reference `analog_input_hooks_x64.cpp` provides concrete RE evidence:
its `kRenderViewSelectSignature` is reported in
`signature_resolution_sp_mp_2026-09-27.txt` as exactly one match in SP at
`FUN_1401dfd80` and exactly one in MP at `FUN_140204f50`. The signature is
therefore a function locator, not an SP build discriminator; copying it
alone would violate the SP-only gate. NCP's separate binary-provenance
record (`known_issues_x64.md`, 2026-09-16 PE-identity comparison) reports
the supported live SP executable identity as AMD64, PE timestamp
`0x6A743A58`, `SizeOfImage` `0x044BE000`; its SHA-256 differs from the
compared reference binary despite matching those PE fields and independently
checked code bytes. This is repository-recorded NCP evidence only: the
corresponding executable is not present in this DXVK worktree, so neither
the metadata nor pattern uniqueness was revalidated here. No file-version
resource or cryptographic identity for the supported installed SP executable
is recorded in this subtree. For this task the user supplied the actual SP
executable for in-place inspection (it was not copied into or modified in
the repository). Its PE header matches the recorded tuple exactly:
AMD64 (`Machine=0x8664`), timestamp `0x6A743A58`, PE32+ (`Magic=0x020B`),
`SizeOfImage=0x044BE000`, seven sections. The NCP render-view signature was
scanned against executable sections and matched once in `.text` at RVA
`0x1DFD80` (file offset `0x1DF180`), corresponding to the documented
`FUN_1401dfd80`. This directly verifies the SP-side locator and fingerprint;
it does not establish SP-only uniqueness, since the same signature matches
MP according to the NCP scan record.

DXVK now has a utility helper in `src/util/iw5_build_identity.{h,cpp}`.
`hasSupportedSpImageIdentity` is a bounded, pure PE-header parser that checks
DOS/NT signatures, header ranges, AMD64, PE32+, timestamp and `SizeOfImage`.
`isSupportedSpExecutable` additionally checks the case-insensitive
`iw5sp.exe` main-module name, then uses `VirtualQuery` to require a readable
committed image mapping before parsing. Malformed, truncated, non-Windows,
wrong-name and mismatched-image cases fail closed. The bridge calls the
process-level check before resolving or installing its hook. The helper and
bridge add no NCP include/link/runtime dependency.

The pure parser also passed a temporary standalone Windows compile/run test:
it accepted a synthetic PE header matching the supported tuple and rejected
null/truncated input, malformed offsets/signatures, wrong machine/timestamp/
magic/image size, and an undersized optional header. This is not a registered
Meson test target; no utility test harness exists in this subtree. The
user-provided executable's PE tuple was verified separately in place, not
through that synthetic test.

DXVK's `SetRenderTargetInternal` and `DrawIndexedPrimitive` retain the active
surface/device state, but no existing DXVK symbol exposes the engine's `id`
parameter from `FUN_1401dfd80`. The bridge therefore vendors upstream MinHook
v1.3.4 from commit `c3fcafdc10146beb5919319d0683e44e3c30d537`, retaining its
BSD license and HDE notices in `third_party/minhook/LICENSE.txt`. No NCP
wrapper/source is included. It is gated by the new opt-in
`d3d9.iw5RenderPassBridge` option (default `False`), then by the exact SP
executable identity; it scans only the readable executable `.text` section
and refuses a missing or non-unique signature. Hook setup errors are logged.
The original dispatcher is called with the original context and ID. A
thread-local scoped ID is exposed only during that synchronous call, and
`SetRenderTargetInternal` stores it alongside the active render-target state
for RT0. If the option is off or any gate fails, the hook is not installed
and the stored ID remains `-1`.

The callback signature/return type matches the NCP reference typedef
`void (__fastcall*)(void*, int)` for `FUN_1401dfd80(ctx, id)`; Windows x64
uses the platform register ABI, and the NCP implementation's call-scoped
publication plus D3D9 bind is the evidence for the same-thread handoff. The
DXVK bridge has compiled and linked, but MinHook installation and live
render-target transitions have not been exercised in the game process.
Offline synthetic tests accept the supported PE tuple and reject malformed,
truncated and non-SP identity inputs; the signature scanner tests unique,
missing, ambiguous, invalid-pattern and invalid-range results. The actual
SP binary was previously scanned in place; the current checkout does not
contain it for a repeat run. `DrawIndexedPrimitive` reads the associated ID
and emits a rate-limited debug sample for ID 2, whose renderer reference
meaning is `R_RENDERTARGET_SCENE`; that code has not run in the game here.
The sample records translated VS/PS debug names, vertex declaration element
count and blend-weight/index flags, and each used stream's buffer size,
usage flags, and stride. These fields are intended to support static-vs-
skinned draw investigation; they are diagnostic evidence, not a proven
static-object classifier. Sampling is limited to the first five target-2
indexed draws and then every 2,000th such draw. The debug names are shader
cache identifiers, not shader contents; the diagnostic does not dump vertex
or index data, alter draw state, or select tessellation stages. It runs only
when the experimental bridge option is enabled and the SP-gated ID handoff
is active.
The stored pass ID is not yet used to classify or alter draws or select a
tessellation pipeline; it is only pass-state plus optional diagnostics. The
module is pinned for the process lifetime after successful hook activation so
its detour/trampoline cannot point into an unloaded DXVK DLL. Partial
installation failures remove the hook and release owned state.

`DecodeInputAssemblyState` in `src/d3d9/d3d9_util.cpp` maps point, line,
and triangle list/strip/fan inputs to the corresponding Vulkan primitive
topologies. `SetNPatchMode` only stores a segment value; `DrawRectPatch`
and `DrawTriPatch` are explicit stubs that log a warning and return
`D3D_OK` (`src/d3d9/d3d9_device.cpp`, around lines 3007 and 4068-4088).
These legacy APIs are not a usable IW5 tessellation path, and the requested
smooth/PN-triangle subdivision is a different feature.

`SetNPatchMode` only stores `m_state.nPatchSegments`; neither
`DrawPrimitive` nor `DrawIndexedPrimitive` consults it to enable hardware
tessellation. The legacy patch-draw stubs do not provide an existing IW5
tessellation path. Buffer allocation and usage flags do not identify an
immutable static prop, skinned model, or UI/HUD draw. A render-target/pass
gate is a promising scene-pass filter, but it is not a substitute for
per-object classification if implementation must target one static
nonanimated prop.

### Vulkan tessellation-stage feasibility, 2026-09-27

DXVK's generic Vulkan graphics path already supports tessellation stages:
`DxvkGraphicsPipelineShaders` holds VS/TCS/TES/GS/FS
(`src/dxvk/dxvk_graphics.h`), and `DxvkGraphicsPipeline::createOptimizedPipeline`
adds TCS/TES stage infos and patch state (`src/dxvk/dxvk_graphics.cpp`).
`validatePipelineState` requires TCS and TES exactly when the input topology
is `PATCH_LIST`. D3D11 already creates and binds hull/domain shaders
(`src/d3d11/d3d11_device.cpp`, `src/d3d11/d3d11_context.cpp`), confirming
the generic Vulkan pipeline and shader machinery is present.

The D3D9 frontend does not use that path: `PrepareDraw` binds translated
vertex/pixel shaders and applies D3D9 list/strip/fan topology;
`DrawIndexedPrimitive` queues indexed commands without patch topology. No
D3D9 TCS/TES shader objects or synthetic stages are bound. Stage I/O
compatibility is checked using each neighboring shader's metadata, so fixed
interface modules cannot be inserted safely for arbitrary IW5 vertex/pixel
shaders. PN evaluation requires positions and normals in a known coordinate
space; the actual candidate IW5 vertex shader and its output semantics have
not been identified. ID 2 gates a whole scene target, not one prop; the
scene includes multiple draw lists and may include animated geometry.

An external, temporary shader feasibility prototype was compiled and linked
with the installed `glslangValidator`: VS/TCS/TES/FS GLSL with PN-triangle
cubic position and normal evaluation. All four SPIR-V modules passed
`spirv-val --target-env vulkan1.2`. The initial patch-output-array
interface attempt failed cross-stage linking; the prototype was corrected
to pass three control points through TCS and evaluate PN control points in
TES. A separate temporary native Vulkan probe loaded those four SPIR-V
modules and successfully called `vkCreateGraphicsPipelines` on the NVIDIA
GeForce RTX 2080 Ti, with a three-control-point `PATCH_LIST`, tessellation
enabled, and a compatible render pass/pipeline layout. This confirms the
driver accepts this actual TCS/TES graphics-pipeline configuration; the
probe did not issue a draw and is not DXVK pipeline integration or game
output. Probe source and shader modules remain outside the repository.

The exact DXVK D3D9 integration seam is constrained by existing validation.
`DxvkGraphicsPipeline::validatePipelineState` requires both TCS and TES with
`PATCH_LIST`, checks the TCS patch-vertex count against the input-assembly
state, and validates the fragment shader's inputs against the TES outputs
when TES is present. D3D9's `PrepareDraw` binds the translated VS/PS and
`ApplyPrimitiveType` submits ordinary triangle topologies. A generated
pass-through/flat TES therefore has to preserve the live VS output interface
expected by each PS; a fixed prototype interface cannot be bound to
arbitrary game shaders. DXVK has VS/PS shader metadata for such validation,
but no D3D9-side synthetic-stage generator or selector for one verified
static draw.

The next D3D9 implementation gate is to identify one static nonanimated
draw and capture its translated VS/PS interface; RT0/ID 2 alone is only a
whole-scene pass and includes unknown geometry classes. Until the draw
selector and interface are established, wiring the prototype would either
fail pipeline validation or alter unverified scene geometry. No tessellation
stages are wired into the D3D9 frontend; the bridge remains off by default.

### Local runtime/content availability, 2026-09-27

A read-only check found the supplied SP executable in the local MW3
installation. It is 5,625,400 bytes with SHA-256
`A97D2BBC7E495E4CF1A3B7E9A021E25B2C7B461D63F2B88D3CFE8782E8DC1023`;
its PE identity matches the supported tuple above (AMD64, PE32+,
timestamp `0x6A743A58`, `SizeOfImage=0x044BE000`). The installation contains
153 English `.ff` packages (about 5.13 GB), 935 `.ff` packages total (about
16.0 GB), and 24 `.iwd` archives (about 9.40 GB). This establishes that
substantial game content is present, but not that a live client launch
succeeds. The local Steam manifest identifies app 42750 as the Dedicated
Server and records only its small server depot; it does not establish the
client's launch configuration. No `iw5sp` process was running during the
check. The only file under `runtime_asset_capture` was a scrollbar texture;
there is no captured geometry or draw/shader evidence to classify a static
prop from offline.

The available IW5 OpenAssetTools documentation says `XModel` can be dumped
to common model formats, but `XModelSurfs`, `MaterialTechniqueSet`,
`MaterialPixelShader`, `MaterialVertexShader`, and
`MaterialVertexDeclaration` are unsupported for IW5. No local
`tools/iw5oat` executable was present to inspect its supported dump path.
Thus the installed FF/IWD packages alone provide no verified way to map a
static model to its runtime D3D9 draw, translated shader pair, declaration,
or buffer usage. The shader and vertex-declaration assets that would supply
the matching layout are specifically among the unsupported types; XModel
dump support by itself cannot establish the D3D9 runtime binding. No package
was extracted or copied into the DXVK tree, and no MW3/NCP asset code or
dependency was added to DXVK.

No game launch, installation change, or DLL deployment was performed.
The supplied CLAUDE §§2–4 rules require asking before process injection;
the live-capture request was issued, but the user was unavailable and no
approval was received. Therefore RT2 static-prop identification and
validation against the original VS/PS remain blocked on that approval.
The bridge commit and standalone native Vulkan pipeline proof do not claim a
D3D9 tessellation implementation; no TCS/TES stage is wired into the D3D9
frontend.
