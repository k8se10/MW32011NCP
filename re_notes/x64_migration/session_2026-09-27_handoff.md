# Session handoff — 2026-09-27 (binary RE marathon)

A complete record of this session's work and every open thread, written
before a context compaction so nothing is lost. Branch:
`claude/confident-mendel-hyoqkw`.

## Standing rules confirmed this session

- **No `Claude-Session:` / session URLs in commit messages** (CLAUDE.md §9).
  One commit violated this and was reworded and force-pushed; all later
  commits comply. Keep `Co-Authored-By: Claude …` only.
- Commit prefixes follow CLAUDE.md §9 (`docs:`, `feat:`, `fix:`, …).
- **Commit in stages:** write findings to docs and commit after each block of
  work, so token limits can't lose knowledge (direct instruction).
- The user guides step by step. Behaviour-changing mod features need the
  user's go-ahead. Read-only diagnostics and docs can go ahead.

## Delivered and committed this session

| Commit topic | File(s) |
|---|---|
| x86 vs x64 provenance (no console-code port; GDK-targeted rebuild: `isgdk`/`issteam` hard-wired 0/1, Miles→XAudio2/X3DAudio, new multi-platform DemonWare SDK incl. `bdPlatformStreamSocket-xboxone.cpp`, `outrun` workspace, VS2019 16.11 PGO); dev-console/debug surface unchanged (`monkeytoy` SP=1/MP=0 both builds); **SteamStub 3.1 anti-debug root cause** (`NoDebuggerCheck` clear; `IsDebuggerPresent` → error 0x54; `NtSetInformationThread(ThreadHideFromDebugger)` on the main thread; 10 s timing check → 0x53) | `binary_provenance_and_antidebug.md` |
| Memory "ceiling": no 4 GB wall; fixed footprint (textures `D3DPOOL_MANAGED` → RAM copy; DXVK x64 never evicts); real limits: 298 MB + 2 MB zone pool (signed-32 checks, OOM exit, raisable to <2 GB), 26 MB per loose `.iwi`, DWORD VRAM detection, 10 MB Hunk, fixed-count render arrays | `memory_ceiling_analysis.md` |
| Renderer end-to-end reference, stages 1 + 2a | `renderer_end_to_end.md` |
| Audio regression reopened, with full static findings | `known_issues_x64.md` issue #10, 2026-09-27 subsection |
| Reusable analysis tooling | `provenance_scripts/` (see below) |
| SP/MP resolution of every proxy signature | `signature_resolution_sp_mp_2026-09-27.txt` |
| Backend RC_* jump-table dump (SP) | `rc_command_table_sp.json` |

## Tooling (now in the repo, so it survives container resets)

`re_notes/x64_migration/provenance_scripts/`:
- `pehdr.py`, `imports.py`, `cstr.py`: PE headers, import diff, strings→VA.
- `steamstub.py`, `drmp.py`: SteamStub header/flags; decrypt the DRM payload
  for **local analysis only** (never commit its output).
- `xref.py`: RIP-relative references with containing function.
- `disall.py`: full linear disassembly listing per `.pdata` function.
- `dec.py`: **angr decompiler** (x64, `.pdata`-bounded). Needs `pip install
  angr` in a venv, because Debian's system pip breaks the install
  (`python3 -m venv venv && venv/bin/pip install angr pefile capstone`).
- `dec32.py` + `cg32.py`: x86 decompile / call graph (the x86 build has no `.pdata`).
- `parentfn.py`: resolve chained `.pdata` fragments to the parent function
  (many "functions with 0 callers" are really fragments).
- `d3dcalls.py`/`d3dcalls2.py`: D3D9 device vtable call-site map.
- `sigscan.py`: resolve all proxy signatures in SP and MP.
- `vacalls.py`, `callargs.py`: argument-constant recovery at call sites.
- `bss.py`: largest static arrays.
- Ghidra is not downloadable in the cloud container (GitHub releases are
  blocked by egress policy). angr covers decompilation.

## Update after the compaction (same day)

Work continued autonomously per direct instruction ("keep going until all
tasks have been completed ... write up in batches"). Everything below is
committed on `claude/confident-mendel-hyoqkw`.

| Commit | Content |
|---|---|
| `6df133be8` | Audio: dup-pair diagnostic (stacks, QPC, handles, DUP detection) in `Hook_PlaySoundAlias`. XAudio2 layer cleared of voice leaks (pick → stop/destroy → create). Per-frame loop re-issue identified as by design. Build not verified here (no Windows toolchain), clang MSVC-mode syntax check passed. **Parked by the user** (no live testing available): resume with one session and `grep DUP`. |
| `95668b8dd`, `59ea90c64` | Renderer stage 2b: the 7 merge sources, the 64-bit draw-surf key, all 15 render-target IDs with real names, the code-material table, the post-FX chain, the frame submit/sync handshake |
| `b1d4fa455`, `dd251f4f6` | Stage 3: the UI/menu pipeline end to end (init/load, context, state machine, paint, input, the 89 script commands, the 356-op expression language, RC emitters). Correction: `FUN_1401d2930` = `PROJECTION_SET(2D)` (op 25 modes corrected in ROUND 24) |
| `b43d97a5a` | Stage 4: `rt_design_dxvk.md`, the ray-tracing design on the DXVK fork |
| `dd85406aa` | Stage 5: `mp_port_plan.md` + `mp_twins_2026-09-27.txt` (18 HIGH SP→MP twins) |

**All five renderer-reference stages are now complete.**

### Summary of the post-compaction findings

**Audio (parked by the user)**
- The dup-pair diagnostic (`6df133be8`) is read-only and has **never been
  compiled**: there's no Windows toolchain in the cloud container, and CI
  only builds `main`/PRs. It was syntax-checked with clang in MSVC mode
  against stand-in Win32 declarations, and its format strings were checked
  by hand.
- The XAudio2 layer is cleared. Loaded-sound start picks a channel
  (`FUN_140270570`), stops and destroys that channel's old voice
  (`FUN_14030f150`), then creates the new one (`FUN_14030dea0`). The
  source-voice array has only two writers. So duplicate voices must be
  **started twice upstream** of `FUN_140274100`.
- The per-frame re-issue of entity loop sounds (`FUN_14003f3d0`) is by
  design; the sound system deduplicates it.
- **Resume:** one play session with AI gunfire and a red barrel, then
  `grep DUP proxy_d3d9.log`. If nothing fires while the echo is audible, the
  next targets are `FUN_140270570` and the restart pair
  `FUN_140279880`/`FUN_1402799e0`.

**Renderer stage 2b** (`renderer_end_to_end.md` §8)
- The 7 k-way-merge sources: BSP pre-tessellated, BSP world VB, cached
  static models (×2), rigid static models, skinned static models, and
  generic draw-surfs.
- The 64-bit draw-surf key: primary sort key in bits 57–62, surfType 53–56,
  light 44–51, material 29–40. The surfType table fills only types 6–9.
- All 15 `R_RENDERTARGET_*` IDs with the engine's own names (table
  `0x1404d0740`).
- The engine code-material table (`0x14041fb80`).
- The post-FX order (`FUN_1401939f0`), and the frame submit/sync handshake
  with its exact event handles.

**Renderer stage 3: UI/menu pipeline** (§9)
- Init and menu load: `ui/code.txt` → `menus.txt` → `patch_menus.txt`.
- The UI context `0x142605050` and the menu stack; the active-menu state
  machine.
- The paint chain: `UI_Refresh` → `Menu_PaintAll` → `Menu_Paint` → item
  painters → text/pic leaves → RC emitters (op 17/9, header constants
  recovered).
- The input path: `CL_KeyEvent` → `UI_KeyEvent` → `Menu_HandleKey` → the
  event interpreter (89 commands).
- The expression language: 356 ops, evaluator `FUN_14028c670`, executor
  `FUN_14028f3c0`.
- Correction: `FUN_1401d2930` only emits `PROJECTION_SET(2D)` (mode 0 = 2D, corrected in ROUND 24); it isn't a
  generic allocator.

**Renderer stage 4: ray tracing** (`rt_design_dxvk.md`, §10)
- One generic, opt-in fork change: `dxvk.enableRayQueryInterop` (acceleration
  structure, ray query, deferred host operations). Everything else lives in
  the proxy, using the Streamline interop seam.
- Geometry is read from zone memory (the 44/32/88-byte strides are confirmed
  in the binary).
- Milestone 1 is RTAO written into `R_RENDERTARGET_SSAO_BLURRED`, so the
  engine composites it with no shader changes.
- RT sun shadows come second, gated on validating how the IW5 sun-shadow
  shaders sample the shadow map. Bounded shader replacement is the fallback.

**Renderer stage 5: MP port** (`mp_port_plan.md`, `mp_twins_2026-09-27.txt`, §11)
- 31 of 63 signatures already hit in MP.
- For the other 32, the static matcher gives 18 HIGH twins (Pmove, sprint,
  missile steering, `UI_KeyEvent`, `UI_Refresh`, the wait-coalescing sleeps,
  the IWD read, post-FX, saved-screen capture, the audio functions), plus 2
  MED, 4 LOW, 2 rejected and 6 open.
- The real work is the per-feature MP data-offset audit.
- Planned structure: per-exe hook descriptors that fail closed, with SP
  unchanged.

### DLSS above 100% render scale: black viewport (continued from another session)

- Branch fast-forwarded to `main` (`b255bad96`) first, since the earlier
  work was already merged.
- `a35cb5481`:
  - fixes the one-frame lag between the DLAA options and the output
    texture (the NGX "Output subrect … exceed" error);
  - adds a read-only readback of the tagged input colour and the DLSS output
    centre pixels.
- Full write-up: `vulkan_dlss_pipeline_research.md`, ROUND 21.
- **Next:** build on Windows, run once at `InternalRenderScalePercent=200`,
  and read the `[x64-streamline-readback]` lines:
  - input black → the colour-input selection;
  - output black → DLAA at this size;
  - both fine → the composite `StretchRect`.
- The earlier "GENERAL layout" theory is unlikely: the ≤100% path uses the
  identical texture and composite and works.

**Update (ROUND 22):** the black viewport was traced statically. Five
engine targets (FLOAT_Z, PINGPONG_0/1, POST_EFFECT_0/1) are created at the
supersampled size, and post-FX binds the glow/blur ones after the scene, so
the size-only "last match" fed DLSS a bloom/blur intermediate. The input is
now selected by engine render-target ID via the existing `R_SetRenderTarget`
hook. Needs a Windows build and one test at 200%.

**Update (ROUND 23):** live test showed the scene target (id 2) is 4x MSAA,
and the engine's resolved copies (ids 3/4) are StretchRect destinations that
are never bound, so DLSS had no input. The proxy now resolves the MSAA scene
into its own single-sampled texture before evaluate, and records only
single-sampled depth. Needs a Windows build and a test at 200%.

**Update (ROUND 24):** ROUND 23 was live-confirmed (DLAA visible at 200%).
The new ghosting came from synthesized DLSS camera constants: `cg_fov` used as
the true horizontal FOV, guessed near/far, read in the engine's 2D projection
setter. The constants are now built once per frame from the scene view's real
`GfxViewParms` (`*(state+0x1790)`), with the legacy builder as fallback. Needs a
Windows build and a pan test at 200%.

### Decisions needed from the user (all open, with recommendations)

1. **Aim slowdown near targets in MP** (`adsCloseRangeSlowdownStrength`
   and any target-based slowdown). **Recommendation: keep it SP-only** for
   competitive integrity. Port analog look and ADS sensitivity scaling only.
   (`mp_port_plan.md` §5/§7)
2. **Default state of MP features once ported.** **Recommendation: all off
   by default**, each behind its own config key, documented as
   "use at your own risk online", validated in private matches first.
   Alternative: controller features auto-on when a controller is detected.
   (`mp_port_plan.md` §5/§7)
3. **The two older offers (§C below):**
   - (a) a **dev-only anti-anti-debug switch**: hook `NtSetInformationThread`
     class 0x11 and clear `PEB->BeingDebugged` from `DllMain`, before the
     SteamStub stub runs. Off by default, never in release builds;
   - (b) **memory-limit config options** `ZonePoolMB` (≤2032),
     `ImageScratchMB` and DXGI VRAM detection, vanilla by default, with a
     zone-pool usage diagnostic first.
4. **What to implement first.** Options:
   - (a) the **DXVK fork RT change** (`rt_design_dxvk.md` §2, step 1 of §8);
   - (b) the **MP per-exe hook-descriptor refactor** (`mp_port_plan.md` §6
     step 1, SP unchanged);
   - (c) the **memory-limit options** (3b).

   No recommendation recorded; it's the user's priority call.

## Open threads, in the user's priority order (as of before the compaction)

### A. URGENT — audio "small room" echo (issue #10, reopened)
Where things stand: the backend has no real reverb (wet = plain gain, fake
spread stage), and the user confirmed the echo is **duplicated voices**. The
duplicate has been seen live (the same 37-channel batch twice in one tick, via
wrapper return `0x14027402B`), but the caller above wrapper `FUN_140273fd0` is
not captured yet. **Next:**
1. Deeper `Hook_PlaySoundAlias` stack plus dup-pair detection (read-only).
2. One user session.
3. Fix the source, diffing against x86 (`FUN_006b2e00` callers,
   `FUN_006b3430`/`FUN_004bd5b0` event-driven vs the x64 polling pair
   `FUN_140279880`/`FUN_1402799e0`).
4. Then optionally a real XAudio2 reverb submix (built-in 2.7 reverb, EAX/I3DL2
   presets, dry/wet-send semantics) and retiring `ReverbWetScale`/sqrt(N).

### B. Renderer end-to-end doc — remaining stages
- 2b: name the six draw-surf sources registered by `FUN_140184090`…
  `FUN_1401843b0` (the iterator struct offsets are misread in the decompile;
  re-derive from disassembly), the draw-surf key bitfield, post-FX internals
  `FUN_1401939f0`, render-target activation IDs (2/5/14 → names via
  `FUN_1401dff30`/`FUN_1401dfd80`), and the frontend producer of the mailbox
  (`R_IssueRenderCommands`: the writer of `0x142005990`, found via
  `xref.py`).
- 3: **UI/menu pipeline end to end from the binary** (added by the user):
  menu asset load → UI context/menu stack → key routing (`FUN_1402aac50`,
  `FUN_14029baa0`) → menu expression evaluator (switch in `FUN_1402dc130`,
  operator table `0x140552980` MP / `0x1404d2b20` SP) → item paint → RC_*
  ops 1–24 (renderer doc §3) → backend. Merge `ui_draw_pipeline_map.md`,
  `ui_text_flow_map.md`, `drawtext_hook_x64.md`.
- 4: **Real in-engine RT design on the DXVK fork** (added by the user). Key
  enablers already established:
  - world geometry is one static VB/IB kept in zone memory (`GfxWorldDraw`),
    with a material per `GfxSurface`, and static models carry full placements
    (`GfxStaticModelDrawInst`);
  - a single draw funnel `FUN_1401de7d0` plus pass setup
    `FUN_1401dbb70`/`FUN_1401dbef0` expose material, technique, pass,
    declaration and streams for every draw;
  - code constants already hold view/proj/viewProj/invViewProj/world
    matrices, and `$floatz` linear depth and resolved-scene samplers exist;
  - the real view/projection are in the current `GfxViewParms` at `*(state+0x1790)` (`FUN_1401e13e0` is the 2D ortho setter, not a 3D jitter point; ROUND 24);
  - DXVK builds the Vulkan device, so ray-query / ray-tracing-pipeline
    extensions can be enabled there.

  Plan to write: BLAS for static world/smodels at load, BLAS refits for
  skinned (skinned-cache VB), TLAS per frame from `add scene ent`
  categories, RT shadows/AO/reflections as passes injected around
  `RB_DrawView`, denoising (NRD or DLSS Ray Reconstruction via the
  Streamline already integrated), and constraints (SM3 shaders, no G-buffer
  → normals must be reconstructed or captured, fixed-count limits).
- 5: **MP port plan** (added by the user): port the whole controller pipeline
  plus the performance fixes to MP. Starting data:
  `signature_resolution_sp_mp_2026-09-27.txt` (which SP signatures already
  hit in MP and which need MP-specific RE). Rendering, perf and UI hooks
  mostly resolve; movement/Pmove/weapon/menu-input hooks mostly don't.

### C. Pending user decisions (offered, not answered yet)
- Dev-only anti-anti-debug switch in the proxy (`DllMain` runs before the
  SteamStub stub: hook `NtSetInformationThread` class 0x11 and clear
  `PEB->BeingDebugged`). Off by default, never in release builds.
- Memory-limit hooks as config options, vanilla by default: `ZonePoolMB`
  (replace `FUN_1402c9130`/`FUN_14032a2c0`, ≤2032), `ImageScratchMB`
  (`FUN_1401ba830`/`FUN_1401e0470`), DXGI-based VRAM detection
  (`FUN_1401e7e10`/`FUN_14020cfe0`), plus a zone-pool usage diagnostic first.

## Corrections made this session (apply everywhere)
- `FUN_1401d83a0`'s caller is `FUN_140078f80` (direct). The old
  "data reference" was `.pdata`.
- `0x14040e630` is the whole-frame backend RC_* command table (25 ops), not
  a HUD-only stream. Op 13 (`FUN_14018b6c0`) is `STRETCH_RAW`.
- `Hook_RenderResCompute`'s target `FUN_1401bd1d0` is `R_CreateDevice`
  (device creation plus resolution tiers).
- `FUN_14018e0d0` is `RB_DrawView` (the full per-view scene render), and
  `FUN_14018e720` is post-FX/motion blur plus 2D setup.
- SP `.data` is 63.5 MB (MP 132 MB), not a gigabyte.
- The old SP string `"Debugger is present."` is DirectShow HRESULT text,
  not an anti-debug check.
