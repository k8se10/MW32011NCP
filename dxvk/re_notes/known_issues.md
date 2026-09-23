# Known Issues — MW32011DXVK

Real, tracked issues specific to fitting DXVK to Call of Duty: Modern
Warfare 3 (2011)'s IW5 engine. See `README.md` for why this fork exists
and its own real scope. Same conventions as the sibling `MW32011NCP`
project's own issue trackers: a `**Status:**` line first, dated
investigation rounds after, `issue #N` cross-reference form.

---

## Index

- [#1](#1-motion-blur-post-process-pass-produces-no-visible-effect) — Motion blur post-process pass produces no visible effect — **Investigating**

---

## #1: Motion blur post-process pass produces no visible effect

**Status: Investigating.**

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

### Cross-references

- `MW32011NCP/re_notes/known_issues_x64.md` issue #2 — the full,
  chronological live-test/investigation trail on the game-mod side,
  including the real positive finding (frame pacing/smoothness under
  DXVK reported as "the best the game has ever felt") independent of this
  specific bug.
- `MW32011NCP/re_notes/x64_migration/vulkan_dlss_pipeline_research.md` —
  the broader Vulkan/DLSS architecture research this DXVK integration
  serves.
