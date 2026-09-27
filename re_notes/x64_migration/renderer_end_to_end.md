# IW5 x64 renderer — end-to-end reference (SP `iw5sp.exe`, 2026-09-27)

**Status: living document, written in committed stages.** This is the single
consolidated "how a frame gets from game code to the screen" reference for the
x64 build. It folds in everything already established in
[`renderer_map.md`](renderer_map.md) (distilled summary),
[`renderer_architecture_map.md`](renderer_architecture_map.md) (raw RE log),
[`ui_draw_pipeline_map.md`](ui_draw_pipeline_map.md),
[`vulkan_dlss_pipeline_research.md`](vulkan_dlss_pipeline_research.md),
`known_issues_x64.md` issue #4 and the proxy's own hook comments, plus a fresh
static pass (angr per-function decompiles bounded by `.pdata`, capstone
call-site/vtable scans).

When this document and an older note disagree, this document reflects the newer
evidence. The corrections are called out inline (**Correction:**).

**Confidence key:** 🟢 read directly off a decompile or disassembly this pass,
or live-confirmed earlier. 🟡 inferred from shape, naming or the IW4/IW5 lineage.
🔴 open.

**Address caveat:** VAs are for the current Steam x64 build, at the fixed image
base `0x140000000` (no ASLR). Shipped hooks must keep using signature scans
(CLAUDE.md §5/§10.3). Names like `RB_ExecuteFrame` are descriptive labels
chosen to match id-Tech/IW naming, not recovered symbols.

**Stage plan:**
1. ✅ Device lifecycle, frame orchestration, backend command list, D3D9 usage map, material/draw funnel (this commit)
2. Draw path deep dive: scene passes, draw-surf lists, shadows, post-FX chain, render targets
3. UI/menu pipeline end to end
4. Ray tracing design on the DXVK fork
5. MP port plan (controller + performance fixes)

---

## 0. One-page picture

```
MAIN THREAD (frontend)                                   BACKEND THREAD (RB)
──────────────────────                                   ───────────────────
Com_Frame → client frame
  FUN_140078f80  (view setup wrapper)
    FUN_1401d83a0  per-frame render entry  ─┐
      FUN_1401d2930  R_AddCmd (8-byte slot) │  queues typed RC_* commands into
      FUN_1401d7480  per-player view setup  │  the frame's backEndData command
        stage markers 1..6 → worker slots 2/3 list (+0x41A48)
          (cull, dpvs, shadow casters, gen drawsurfs)
      HUD/menus/text → RC_* 2D commands     │
  frame submitted to 1-slot mailbox ───────►│ 0x142005990 (InterlockedExchange64)
                                            │ events 0x142005968 / 0x142005978
                                            ▼
                                   FUN_14018a5a0  RB_RenderThread (loop)
                                     FUN_14024a390  take backEndData
                                     FUN_14018a240  RB_ExecuteFrame
                                       BeginScene (+0x148), gamma
                                       FUN_14018e0d0  per-view pre-pass (shadow maps)   [via thunk 14018dee0]
                                       FUN_14018e720  RB_Draw3D (scene, post-FX, blur)  [via thunk 14018ded0]
                                       reset cmdBufState, bind view 1
                                       for op in cmdlist: table_0x14040e630[op](&p)   (25 RC_* ops)
                                       EndScene (+0x150)
                                     FUN_14018b840  end-of-frame / swap (Present path)
```

Everything that touches D3D9 is on the backend thread, with two exceptions:
resource creation/reset, and the device-health polling described below.
`CreateDevice` is called with `D3DCREATE_MULTITHREADED` for this reason.

---

## 1. Device lifecycle 🟢

### 1.1 Init chain

| Step | Function | What it does |
|---|---|---|
| `R_Init` | `FUN_1401bbea0` | `FUN_1401b50e0` registers all `r_*`/`sm_*` dvars; clears renderer globals; `FUN_1401bcf30` (create device); `FUN_14018a210`, `FUN_1401e87f0` (post-device init); then `TestCooperativeLevel` (`+0x18`) and, unless lost, `FUN_1401950a0` (a GPU capability/warm-up routine that also calls `BeginScene`/`EndScene`/`GetDeviceCaps`). Writes the final resolution/aspect block back to the caller. |
| `R_CreateDeviceAndWindow` | `FUN_1401bcf30` | If the device exists: just re-derives quality (`FUN_1401b9980` picmip, `FUN_1401c48f0`, `FUN_1401c0550`, `FUN_1401b7420`). Else: `Direct3DCreate9(32)` through thunk `FUN_140316a48` → global `IDirect3D9*` **`0x1418886c8`**. Picks the adapter whose monitor contains (`vid_xpos`,`vid_ypos`) (`GetAdapterCount` `+0x20`, `GetAdapterMonitor` `+0x78`, `EnumDisplayMonitors`) → `0x1418886d8`. Runs caps (`FUN_1401be0f0`) and mode enumeration (`FUN_1401bc8a0`), then loops `FUN_1401bda40` (fill window params) + `FUN_1401bc780` (create window + device). **The failure fallback ladder** is: fullscreen→windowed, lower `0x141884d90` (AA), drop refresh >60, shrink resolution toward 640×480, three retries, then fatal. |
| Window | `FUN_1401bc780` | `AdjustWindowRectEx` + `CreateWindowExW(class "I", L"Call of Duty®: Modern Warfare® 3")`: `WS_POPUP` for fullscreen, `0x96000000` for borderless/windowed-fullscreen, `0x00C80000` for windowed. On success calls `FUN_1401bd1d0`. |
| `R_CreateDevice` | `FUN_1401bd1d0` (**the function the proxy hooks as `Hook_RenderResCompute`**) | Picks the depth format (`FUN_1401d3fb0(21)` → `0x1418886f0`), builds present params (`FUN_1401bd920`), and calls `CreateDevice` via `FUN_1401bc560(hwnd, 0x46, &pp)`. Then computes the resolution tiers (§1.3) and aspect globals, runs `FUN_1401bc660` (device resources), `FUN_1401a1be0`, `FUN_140197790`, picmip, and registers the window in the window list (`0x1418886c0+0x3580`, 24-byte entries, count `0x14188bc38`). |
| `CreateDevice` | `FUN_1401bc560` | `IDirect3D9::CreateDevice` (`+0x80`)(adapter, `D3DDEVTYPE_HAL`, hwnd, **`0x46` = `HARDWARE_VERTEXPROCESSING \| MULTITHREADED \| FPU_PRESERVE`**, pp, **`&device` → `0x1418886d0`**). Retries 20× with `Sleep(100)`, then falls back to adapter 0. Afterwards `GetAdapterDisplayMode` (`+0x40`) → desktop size `0x1418886e8/ec`. |
| Device resources | `FUN_1401bc660` | Render targets (`FUN_1401d44e0` → `FUN_1401d4040` etc.), `FUN_140195570`, default textures/buffers (`FUN_1401d2e00`, `FUN_1401c8170`, `FUN_1401e2100`), dynamic VB/IB (`FUN_1401a0060`, `FUN_1401a01f0`). **Creates 1 + 32 `D3DQUERYTYPE_EVENT` queries** (`CreateQuery` `+0x3B0`, type 8) at `0x14188bc58` and `0x14188bb08[32]`, used as a GPU fence ring. Then `FUN_140194f00`/`FUN_140196340`, and probes **occlusion query** support (type 9) → `0x1418854c0`. |

### 1.2 Present parameters (`FUN_1401bd920`) 🟢

```
BackBufferWidth/Height = native W/H (window struct +0x28/+0x2C)
BackBufferFormat       = D3DFMT_A8R8G8B8 (21)      BackBufferCount = 1
MultiSampleType        = NONE                      SwapEffect      = DISCARD
hDeviceWindow          = hwnd
Windowed               = 1 (windowed/borderless) → refresh 0 | 0 (fullscreen) → refresh = r_displayRefresh
EnableAutoDepthStencil = FALSE (format field still set to the chosen depth format)
PresentationInterval   = r_vsync ? D3DPRESENT_INTERVAL_ONE : D3DPRESENT_INTERVAL_IMMEDIATE
```

MSAA is **not** applied on the swap chain. `CheckDeviceMultiSampleType`
(`IDirect3D9 +0x58`) finds the highest supported sample count from the
requested value downward, caps the quality level by dvar `0x141884d88`, and
stores the result in `0x14188bc18/1c`. It's applied to the `$scene` render
target, which is resolved afterwards (renderer_map §6). That matters for
DLSS/RT, because the swap chain is always single-sample.

### 1.3 Resolution tiers (inside `FUN_1401bd1d0`) 🟢

- Requested W/H → `0x141888670/674` (unclamped, drives the scene RTs; this is
  what `InternalRenderScalePercent` rewrites).
- Native W/H → `0x14188868c/690` (permanent).
- Tier-clamped W/H → `0x141888678/67c`, with an "is scaled" flag at
  `0x141888688`: width floor 1280 (h 720), 1600×1200 when aspect ≈ 4:3
  (`0x3FAAAAAB`), 1920×1080 when aspect ≈ 16:9 (`0x3FE38E39`), else
  1920×1200, clamped to native.
- Aspect `0x141888698`, pixel aspect `0x14188869c`, monitor aspect
  `0x1418886a0` (1.0 when windowed mode 2).

### 1.4 Reset / device loss 🟢

- `R_ResetDevice` = `FUN_1401bc21b` (the only `Reset` `+0x80` call site): release all
  `D3DPOOL_DEFAULT` resources (`FUN_1401d3950`, `FUN_1401a1130`, `FUN_1401c8260`, …),
  rebuild pp (`FUN_1401bd920`), `Reset`. On failure: `"Couldn't reset a lost
  Direct3D device - IDirect3DDevice9::Reset returned 0x%08x (%s)"` (fatal). Then
  `FUN_1401bc660` recreates resources and re-seeds backend state
  (`FUN_1401e0e80(&cmdBufState 0x1415e7c70, &srcState 0x1415e6a00)`,
  `FUN_1401dfb10(device)`, `FUN_1401e1280`, bind view 1, `FUN_1401950a0`).
- `TestCooperativeLevel` is polled from `FUN_1401bc190`, `FUN_1401bc1de`,
  `FUN_1401b9760` and `R_Init`. The backend frame (`FUN_14018a240`) bails out
  early if `FUN_1401bc190` reports the device unusable.
- The backend thread has its own device-lost loop (`0x1418854ca`/`0x1418854d0`
  flags), stepping about every 33 ms until the device comes back (§2.2).

### 1.5 Shutdown 🟢

`FUN_1401bdf00` (`R_Shutdown(destroyWindow)`) flushes the backend, frees
renderer subsystems, zeroes the scene globals, destroys every window in the
window list, then `Release`s the device (`0x1418886d0`) and `IDirect3D9`
(`0x1418886c8`).

### 1.6 Global handles

| Global | Meaning |
|---|---|
| `0x1418886c8` | `IDirect3D9*` |
| `0x1418886d0` | `IDirect3DDevice9*` (also cached at `cmdBufState+0x110`) |
| `0x1418886d8` | adapter ordinal |
| `0x1418886e8/ec` | desktop mode W/H |
| `0x1418886f0` | depth-stencil format |
| `0x14188bc30` | "device lost / resetting" flag |
| `0x14188bc31` | "inside BeginScene/EndScene" flag |
| `0x141885488` | "no-D3D" mode (dedicated/null renderer — skips resource creation) |

---

## 2. Frame orchestration 🟢

### 2.1 Frontend (main thread)

- `FUN_140078f80` stores the refdef origin/axes (`0x14064ff30..44`), builds
  the view (`FUN_1401d7780`), and calls **`FUN_1401d83a0`** (per-frame render
  entry). **Correction:** the "data reference `0x14445093c`" older notes gave
  as its only caller is its own `.pdata` unwind entry. The real caller is this
  direct call.
- `FUN_1401d83a0` computes per-view rectangles from the resolution globals,
  queues commands through **`FUN_1401d2930`** (`R_AddCmd`; context
  `0x141896b98`), and runs **`FUN_1401d7480`** (per-player view setup: shadow
  tier select `FUN_1401d8f70`, fog/DOF/vignette marshalling `FUN_1401d9a10`,
  stage markers 1–6, HUD tick `FUN_140039f40`).
- Scene determination runs on **worker slots 2/3** (`FUN_1401ea560` →
  `FUN_1401e9c40` → `FUN_1401e9f70` `switch(stage)`) with the stage names in
  renderer_map §4. `add scene ent` (0x11) and `gen drawsurfs` (0x12) produce
  the draw-surf lists the backend consumes.

### 2.2 Handoff and backend thread

- **Mailbox:** `FUN_14024a390` = `InterlockedExchange64(&0x142005990, 0)`
  returns the pending **backEndData** pointer (a 1-slot queue). Events:
  `FUN_14024a650` = `ResetEvent(0x142005978); SetEvent(0x142005968)`,
  `FUN_14024a640` = `SetEvent(0x142005978)`. The frontend blocks on these
  before it can submit the next frame, so it runs at most one frame ahead of
  the backend. (The producer side, `R_IssueRenderCommands`, is traced in stage 2.)
- **`FUN_14018a5a0` = `RB_RenderThread`** (worker slot 2's job, spawned from
  the thread-pool helper). Loop:
  1. wait (`FUN_14024a8c0`, or a profiler-wrapped wait when dvar `0x141efb808` is set);
  2. take a frame → **`FUN_14018a240`**;
  3. signal the frontend;
  4. device-lost branch: poll `FUN_1400839c0` with ~33 ms pacing until
     `0x1418854d0` clears.

  The live `[render-thread-diag]` finding (EndScene on a dedicated thread,
  with rare hand-backs to main correlating with 100–165 ms spikes) is this
  thread.

### 2.3 `FUN_14018a240` = `RB_ExecuteFrame(backEndData)` 🟢

backEndData fields used (byte offsets):

| Offset | Meaning |
|---|---|
| `+0x41A68` | frame flags: bit0 = begin frame (BeginScene + gamma), bit1 = draw frame |
| `+0x41A7C` | "has drawable content" |
| `+0x41A34` / `+0x41A38` | 3D view / pre-pass counts |
| `+0x41A30`, `+0x41A40` | active view index / view array (stride **0x14C0** — the same per-player view stride the frontend uses) |
| `+0x41A48` | **RC_\* command list** |

Sequence:
1. Stash `backEndData` in `0x1415e5078`; `0x1415e6480 = 1` (busy).
2. bit0: `BeginScene` (`+0x148`); if any gamma dvar is modified
   (`0x141884b10..b30`) → `FUN_1401e0120` (`SetGammaRamp`); `FUN_14019a750`,
   `FUN_1401a0cd0(0)`.
3. If views exist → `FUN_14018e0d0` (per-view pre-pass: sun/spot shadow-map
   generation — contains the double-pass light loop that matches x86
   `FUN_0049d6a0`).
4. bit1 and content: if views → **`FUN_14018e720` = `RB_Draw3D`** (projection
   build for the shadow pass and the scene pass, scene draw lists, scene post-FX
   `FUN_1401939f0`, motion-blur trigger `FUN_14018def0`). Then reset
   `cmdBufState` (`FUN_1401e0e80`), zero `0x1415e7220[0xA00]`, set default
   state (`FUN_1401dff30`), **bind view 1** (`FUN_1401dfd80(pass=1)` — the
   extra x64 activation the proxy can skip), `FUN_140197760`.
5. **Execute the RC_\* list** (§3).
6. Flush the pending tess batch (`FUN_140196280` while `0x1415e504c` ≠ 0),
   unbind the stream (`0x1415e71a0` `SetIndices` `+0x340`, NULL), `EndScene`
   (`+0x150`).
7. `FUN_14018b840` (end-of-frame: StretchRect/Sleep(1)-polled swap path, the
   wait-coalescing target), `FUN_1401bd8a0/8d0`. If `r_fullscreen`/`r_mode`-class
   dvars changed (`0x141884b00/af8/b08`), restart video.

### 2.4 Present 🟢

The only `Present` call site is **`FUN_1401b9930`**: for each registered
window whose state byte is 3..5, it calls the swap/present method (`+0x88`).
It's reached from the end-of-frame path.

---

## 3. The backend command list (RC_\*) 🟢

Every 2D/UI/HUD draw, and several scene-state changes, are **commands**
appended by the frontend and executed in order by `RB_ExecuteFrame` through
the **jump table at `0x14040e630`** (entry 0 = NULL = end of list). Command
header: `u16 id; u16 byteSize;` — each handler advances `*pp += byteSize`.
Most handlers first flush the pending 2D tess batch (`FUN_140196280`).

| Op | Handler | Name (IW-lineage) | Evidence |
|---|---|---|---|
| 1 | `FUN_14018ad00` | `SET_MATERIAL_COLOR` | 4 floats → material-colour code constant `0x1415e8d10..1c`, bumps its version `0x1415e9300` |
| 2 | `FUN_14018a8c0` | set 2×vec4 code constants (light/params) | writes `0x1415e8a70..8c` + version counters `0x1415e92ac/ae` |
| 3 | `FUN_14018a780` | `SAVE_SCREEN` | proxy `kSavedScreenCaptureSignature`: StretchRect into `$savedscreen`, gated by per-consumer generation stamps `0x141887e20[]` vs `0x1415e9438` |
| 4 | `FUN_14018a870` | `SAVE_SCREEN_SECTION` / stamp | stamps `0x141887e20[consumer] = 0x1415e9438` |
| 5 | `FUN_140186c60` | `CLEAR_SCREEN` | calls the `Clear` funnel `FUN_1401de320` |
| 6 | `FUN_14018ae50` | `SET_VIEWPORT` | `FUN_1401e06b0(&cmdBufState, rect)` |
| 7 | `FUN_14018ad80` | set scissor / clip rect | clamps the rect against the current viewport (`0x1415e7c58/60`) |
| 8 | `FUN_140186c20` | reset clip / render state | `FUN_1401df280(&0x1415e7090)` |
| 9 | `FUN_14018ae90` | `STRETCH_PIC` | material + x,y,w,h,s0,t0,s1,t1 + colour → quad builder `FUN_140188750` |
| 10 | `FUN_14018af10` | `STRETCH_PIC_FLIP_ST` | same shape → `FUN_140188970` |
| 11 | `FUN_14018b3a0` | `STRETCH_PIC_ROTATE_XY` | sin/cos (`FUN_14039ca20/cfa0`) + tess |
| 12 | `FUN_14018af90` | `STRETCH_PIC_ROTATE_ST` | sin/cos + tess |
| 13 | `FUN_14018b6c0` | `STRETCH_RAW` | proxy `kScreenCaptureCmdSignature`: CPU per-pixel copy into an offscreen surface + StretchRect (render-scale² CPU cost) |
| 14 | `FUN_140188360` | `DRAW_QUAD_PIC` | 4 verts / 6 idx into tess, batch limits 5450 verts / 0x100000 idx |
| 15 | `FUN_140188540` | `DRAW_FULL_SCREEN_COLORED_QUAD` | same tess path |
| 16 | `FUN_140187200` | `DRAW_TEXT_2D` (simple) 🟡 | material/font → `FUN_140187130` |
| 17 | `FUN_140188df0` | `DRAW_TEXT_2D` (rotated/styled) 🟡 | sin/cos, glyph path `FUN_140185450` |
| 18 | `FUN_140188f10` | `DRAW_TEXT_3D` 🟡 | `FUN_140188f60(origin, …)` |
| 19 | `FUN_140186770` | `BLEND_SAVED_SCREEN_BLURRED` | reads the saved-screen generation window (`0x1415e9438 - 0x141887e20[i] < duration`) |
| 20 | `FUN_1401868f0` | `BLEND_SAVED_SCREEN_FLASHED` | flash intensity `0x1415e9484`, `$savedscreen` via `0x141bb6e00` |
| 21 | `FUN_140188320` | `DRAW_POINTS` 🟡 | 2D/3D by `+7 == 2` → `FUN_140187c00`/`FUN_140187ec0` |
| 22 | `FUN_140187bc0` | `DRAW_LINES` 🟡 | 2D/3D → `FUN_140187250`/`FUN_140187530` |
| 23 | `FUN_140189270` | `DRAW_TRIANGLES` | material + indexed verts → `FUN_140189310` |
| 24 | `FUN_14018aa10` | set code constant (vec4 by index) | writes `cmdBufState+0xE00+idx*16`, bumps version `+0x163C+idx*2` |
| 25 | `FUN_14018a1a0` | `PROJECTION_SET` | 0 → 3D projection `FUN_1401e13e0` (the DLSS jitter hook target), 1 → 2D ortho `FUN_1401e1640` |

The UI/HUD layer (stage 3) is therefore just a producer of ops 1–24. The
"opcode 13 per-pixel copy" found during issue #4 is `STRETCH_RAW`.

---

## 4. D3D9 usage map 🟢

Each device method is reached through a small number of wrapper functions,
which gives a precise, complete list of choke points. Coverage: calls through
the global `0x1418886d0` plus calls through the cached device at
`cmdBufState+0x110`, plus direct vtable-offset greps for the draw-family
methods.

| Method (vtable offset) | Call sites |
|---|---|
| `DrawIndexedPrimitive` (+0x290) | **`FUN_1401de7d0` only** (34 callers): `(dev, D3DPT_TRIANGLELIST, 0, 0, numVerts=a[0], startIndex=a[2], primCount=a[1])` |
| `DrawPrimitiveUP` (+0x298) | `FUN_14019a3b3` (debug/immediate path) |
| `DrawPrimitive` | none |
| `Clear` (+0x158) | **`FUN_1401de320` only** (9 callers; float colour → packed ARGB, optional rect) |
| `SetRenderState` | `FUN_1401dde7a/ddf0b/de0ab` (state-bits appliers), `FUN_1401df280/2d0/310`, `FUN_1401dfdac`, `FUN_1401e1280`, `FUN_140196ad0`, `FUN_1401b40e0` |
| `SetSamplerState` (+0x228) | `FUN_1401df5c0`, `FUN_1401e0d60` |
| `SetTexture` | `FUN_1401de6e0`, `FUN_1401dfdac`, `FUN_1401dff70` |
| `SetVertexShader` / `SetVertexDeclaration` | **`FUN_1401dbef0`** (18 callers), `FUN_1401dba10`, `FUN_140196020`, `FUN_140195ff0` |
| `SetPixelShader` | **`FUN_1401dbb70`** (26 callers), `FUN_1401db760`, `FUN_140195fc0` |
| `SetVertexShaderConstantF` (+0x2F0) | `FUN_1401db1d0`, `FUN_1401db3b0`, `FUN_1401db600`, `FUN_1401dba50`, `FUN_1401dbd5a` |
| `SetPixelShaderConstantF` (+0x368) | `FUN_1401db076` |
| `SetStreamSource` | `FUN_1401dfc10` (4 callers), `FUN_1401b4fa0` |
| `SetIndices` (+0x340) | 11 functions, including `RB_ExecuteFrame`, the model/world draw loops `FUN_140198050/140198e40/140199080/1401991d0` and `FUN_1401b3150/3b80/3e50/4fa0` |
| `SetViewport` / `SetScissorRect` | `FUN_1401ddc80`, `FUN_1401de993`, `FUN_1401e05d6`, `FUN_1401df750` |
| `SetRenderTarget` / `SetDepthStencilSurface` | `FUN_1401dfd80` (view activator, 37 callers) |
| `StretchRect` | `FUN_14018a7cf` (save screen), `FUN_14018b6c0` (raw), `FUN_14018b840` (end-of-frame), `FUN_14018dc20` |
| `CreateTexture` | `FUN_1401b8e40` (`Create2DTexture`; **static textures → `D3DPOOL_MANAGED`**), `FUN_1401b8d10` |
| `CreateCubeTexture` / `CreateVolumeTexture` | `FUN_1401b90d0`, `FUN_1401b94c8` |
| `CreateVertexBuffer` / `CreateIndexBuffer` | `FUN_14019fe80`, `FUN_14019ff90` / `FUN_14019ff00`, `FUN_1401a0060`, `FUN_1401a01f0` |
| `CreateRenderTarget` / `CreateDepthStencilSurface` | `FUN_1401d4040` / `FUN_1401d439e`, `FUN_1401d4532`, `FUN_1401d4db8` |
| `CreateVertexShader` / `CreatePixelShader` | `FUN_1401c45c2`, `FUN_1401c4f4a` / `FUN_1401c4491`, `FUN_1401c4e09` |
| `CreateVertexDeclaration` | `FUN_1401e18cd`, `FUN_1401e1aec` |
| `CreateQuery` | `FUN_1401bc660` (events), `FUN_140194f00` |
| `UpdateTexture` | `FUN_1401c7629` |
| `GetAvailableTextureMem` | `FUN_1401e7e10` (VRAM probe — see `memory_ceiling_analysis.md`) |
| `SetGammaRamp` | `FUN_14018aac0` |
| `Reset` / `Present` / `BeginScene` / `EndScene` | `FUN_1401bc21b` / `FUN_1401b9930` / `FUN_14018a240` (+ init/recovery `FUN_1401866a0`, `FUN_1401950c7`, `FUN_1401bc430`) |

**Why this matters:** a single `DrawIndexedPrimitive` wrapper, and one
function each for pass shader binding, means **every scene draw's full context
is observable at two choke points**: which material, technique, pass, vertex
declaration, stream buffers and index range. That's the foundation the RT
design (stage 4) and any future renderer work build on.

---

## 5. Material system and draw funnel 🟢

The IW5 asset layouts in `tools/iw5oat/src/Common/Game/IW5/IW5_Assets.h`
match the decompiled accesses exactly: `Material` is 0x80 bytes;
`stateBitsEntry[54]` @ `+0x20`; `techniqueSet` @ `+0x60`; `textureTable` @ `+0x68`;
`constantTable` @ `+0x70`; `stateBitsTable` @ `+0x78`. `MaterialPass` is 0x28 bytes.

### 5.1 Backend command-buffer state (`GfxCmdBufState`, main instance `0x1415e7c70`)

| Offset | Field |
|---|---|
| `+0x110` | `IDirect3DDevice9*` |
| `+0x120` | vertex-declaration index (vertex type) |
| `+0x158` | current `IDirect3DVertexDeclaration9*` |
| `+0x160` | current `Material*` |
| `+0x168` | current `MaterialTechniqueType` |
| `+0x170` | current `MaterialTechnique*` |
| `+0x178` | current `MaterialPass*` (`technique + 0x10 + pass*0x28`) |
| `+0x180` | current pass index |
| `+0xB94` / `+0xB98` | cached state-bits words 0/1 |
| `+0xBA8` | cached pixel shader |
| `+0xBB0` | cached vertex shader |
| `+0xE00 + i*16` | code-constant vec4 table (op 24) |
| `+0x14D0` / `+0x1510` | projection matrix (16 floats, nonstandard layout — see the proxy's `kProjectionMatrixBuildSignature` comment) |
| `+0x17C0` | projection dirty flag |

A second state block at `0x1415e7090` is used by the 2D/prim path (ops 8, 21,
22). Source/code-constant state lives at `0x1415e6a00`.

### 5.2 Per-pass setup 🟢

- **`FUN_1401dbb70` = `R_SetupPass(state, passIndex)`** (26 callers):
  `pass = technique + (passIndex*5+2)*8`. It looks up
  `material->stateBitsTable[material->stateBitsEntry[techType] + passIndex]`
  → applies loadBits word 0 (`FUN_1401dde50`) and word 1 (`FUN_1401de090`) only
  if changed, then `SetPixelShader(pass->pixelShader->prog.ps)` if changed, then
  **stable** shader args
  `FUN_1401db3b0(args + (perPrim+perObj)*16, stableArgCount)`.
- **`FUN_1401dbef0` = `R_SetVertexShaderAndDecl(state)`** (18 callers):
  `decl = pass->vertexDecl->routing.decl[state->vertDeclType]`
  (`SetVertexDeclaration` `+0x2B8` if changed; error `"Vertex type %i doesn't
  have the information used by shader %s in material %s"`), then
  `SetVertexShader(pass->vertexShader->prog.vs)` (`+0x2E0`) if changed.
- **`FUN_1401de7d0` = `R_DrawIndexedPrimitive(state, {numVerts, primCount, startIndex})`**.
- Shader arguments (`MaterialShaderArgument {u16 type; u16 dest; union u}`) are
  split into per-prim / per-object / stable groups. The code-constant sources
  (`CONST_SRC_CODE_*`) include `VIEW_MATRIX` 0x54, `PROJECTION_MATRIX` 0x58,
  `VIEW_PROJECTION_MATRIX` 0x5C, `INVERSE_VIEW_PROJECTION_MATRIX` 0x5D,
  `SHADOW_LOOKUP_MATRIX` 0x60, `WORLD_MATRIX0` 0x68,
  `WORLD_VIEW_PROJECTION_MATRIX0` 0x70. Code samplers (`TEXTURE_SRC_CODE_*`)
  include `SHADOWMAP_SUN` 6, `SHADOWMAP_SPOT` 7, `RESOLVED_SCENE` 0xA,
  `FLOATZ` 0x10, `REFLECTION_PROBE` 0x1A. So the engine already feeds its
  shaders the full matrices, a linear depth (`$floatz`) and a resolved scene
  colour. Stage 4 relies on these.

### 5.3 Technique types (54, `MaterialTechniqueSet::techniques[54]`)

`DEPTH_PREPASS` 0, `BUILD_FLOAT_Z` 1, `BUILD_SHADOWMAP_DEPTH` 2,
`BUILD_SHADOWMAP_COLOR` 3, `UNLIT` 4, `EMISSIVE*` 5–8, `LIT*` 0x09–0x18 (sun /
sun-shadow / spot / spot-shadow / cucoloris / omni variants, each with a DFOG
twin), `LIT_INSTANCED*` 0x19–0x28, `LIGHT_SPOT`/`OMNI`/`SPOT_SHADOW*` 0x29–0x2C,
`FAKELIGHT_*`, `SUNLIGHT_PREVIEW`, `CASE_TEXTURE`, `WIREFRAME_*`, `THERMAL`
0x33, `DEBUG_BUMPMAP*`. A draw's technique is chosen per pass type: the depth
prepass uses 0; float-Z building uses 1; shadow maps use 2; the lit opaque pass
uses 0x0B–0x18 by light; emissive/trans use 5–8; thermal vision uses 0x33.

---

## 6. Corrections to older notes (this stage)

1. `FUN_1401d83a0`'s caller is `FUN_140078f80` (direct call), not a
   function-pointer table. The old "data reference" was `.pdata`.
2. The "2D/HUD command stream" jump table `0x14040e630` is **the** backend
   command list for the whole frame (25 ops), executed by `RB_ExecuteFrame`.
   The 3D scene is drawn from the same function via `FUN_14018e0d0` /
   `FUN_14018e720` before the list runs.
3. `Hook_RenderResCompute`'s target `FUN_1401bd1d0` is `R_CreateDevice` (it
   creates the device, then computes the tiers), not only a resolution helper.
4. Opcode 13 (`FUN_14018b6c0`) is `STRETCH_RAW`.

---

*Next stage: draw path deep dive (scene passes inside `FUN_14018e720`,
draw-surf list layout, shadow pipeline, post-FX chain, render-target
lifecycle).*
