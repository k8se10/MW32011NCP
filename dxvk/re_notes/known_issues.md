# Known Issues — MW32011DXVK

Real, tracked issues specific to fitting DXVK to Call of Duty: Modern
Warfare 3 (2011)'s IW5 engine. See `README.md` for why this fork exists
and its own real scope. Same conventions as the sibling `MW32011NCP`
project's own issue trackers: a `**Status:**` line first, dated
investigation rounds after, `issue #N` cross-reference form.

---

## Index

- [#1](#1-motion-blur-post-process-pass-produces-no-visible-effect) — Motion blur post-process pass produces no visible effect — **Resolved (not a DXVK bug)**

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
