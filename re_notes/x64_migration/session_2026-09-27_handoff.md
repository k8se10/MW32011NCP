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
| `b1d4fa455`, `dd251f4f6` | Stage 3: the UI/menu pipeline end to end (init/load, context, state machine, paint, input, the 89 script commands, the 356-op expression language, RC emitters). Correction: `FUN_1401d2930` = `PROJECTION_SET(3D)` |
| `b43d97a5a` | Stage 4: `rt_design_dxvk.md`, the ray-tracing design on the DXVK fork |
| `dd85406aa` | Stage 5: `mp_port_plan.md` + `mp_twins_2026-09-27.txt` (18 HIGH SP→MP twins) |

**All five renderer-reference stages are now complete.** Still waiting on
the user:
- the dev-only anti-anti-debug switch and the memory-limit hooks (§C below);
- MP decisions (`mp_port_plan.md` §7: aim slowdown in MP, defaults);
- go-ahead to start implementing any of: the RT milestone 0/1 fork patch,
  the MP step 1 descriptor refactor, the memory-limit hooks.

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
  - the projection jitter point is `FUN_1401e13e0`;
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
