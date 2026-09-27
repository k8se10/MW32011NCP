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
2. ✅ Draw path deep dive: scene passes, draw-surf lists, key layout, render targets, code materials, post-FX chain, frame submit (§7–§8)
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

## 7. Draw path (stage 2a — partial, committed early) 🟢

### 7.1 Corrected roles of the two scene functions

- **`FUN_14018e0d0` = `RB_DrawView(view)`** — the real scene render for the
  active view (not only a shadow pre-pass). Order:
  1. Upload per-frame dynamic vertex blocks (`FUN_1401a0a70` lock of the VB at
     `0x1415f30f0`, memcpy, `FUN_1401a0960` unlock) for `backEnd+0x41930` entries.
  2. `FUN_1401901c0(backEnd+0x4193C)`.
  3. Main view only (`view+0x1B0 == 1`): **sun shadow maps** when the sun gate
     `FUN_1401d2b20` passes → `FUN_1401978e0`; **spot shadows** when
     `backEnd+0x42200` (spot-shadow count) is nonzero → `FUN_140196910`
     (the per-light loop containing `FUN_140196ad0`, the proxy's
     `kPerLightShadowDispatchSignature` target).
  4. `FUN_1401c7150` (static-model cache update), profiler marker.
  5. If `view+0x1B4` (float-Z / depth needed): bind target **5**, `Clear(6 =
     ZBUFFER|STENCIL)`, `FUN_14018dcd0(5, …)` = depth / float-Z pass.
  6. Bind the scene target `view+0x9D0` and `Clear(7)` with the fog/clear colour
     (`FUN_1401e8330`), or, when `view+0x9DC` is set, take the alternate
     clear/bind path `FUN_14018e010` (target 2, flash/blur colour state).
  7. `FUN_1401b1d40` → opaque/lit list (callback `FUN_1401b1e80` →
     `FUN_14018bfb0`). It applies the dvar at `0x141884bc0` as a colour-constant
     tweak (`state+0x10F0..0x10FC`).
  8. If `r_depthPrepass` (dvar `0x141884e58`) is nonzero → `FUN_14018dcd0(view
     target, …, prepass==2)`.
  9. `FUN_1401b1f00` (callback `FUN_1401b1e80`, second lit list) and
     `FUN_1401b1fa0` (callback `FUN_1401b2040` → `FUN_14018c1f0`, emissive /
     trans list).
  10. SSAO: `FUN_140197670` returns true when the view allows it and the SSAO
      enable `0x1418854b9` is set → full-res `FUN_140196df0` (bind target 14,
      projection rebuild, `FUN_140196f50` blur chain) or downsampled
      `FUN_140196e80`.
  11. `FUN_14018ea20` rebinds the view target and view parms for what follows.
- **`FUN_14018e720` = `RB_PostFxAndFinish`** — for every view whose state
  `view+0x9D0 == 2` (rendered into `$scene`), runs **scene post-FX
  `FUN_1401939f0`** then the **motion-blur trigger `FUN_14018def0`**. Split
  views with exclusion rects go through `FUN_140194130`. It then prepares
  view parms, viewport and the 3D projection for the RC_* list, for the main
  and secondary (`backEnd+0x41A38`) view sets.

### 7.2 Draw-list dispatch

- **`FUN_1401de730(callback, ctx, state, list)`** saves the 3040-byte prim
  state at `0x1415e7090`, zeroes a 0xA00 scratch block, runs `callback`, then
  restores the state. Every list pass goes through it with a local copy of
  `GfxCmdBufState` seeded from the view (`FUN_1401e0e80(state, view+0x340, 1)`,
  target `FUN_1401dff30`, viewport `view+0x150`).
- List drawers:
  - `FUN_14018bfb0` — single sorted list.
  - `FUN_14018c650` — **k-way merge**. Up to 6 per-type sorted sources are
    registered (`FUN_140184090` … `FUN_1401843b0`, one per surface type, with
    the prepass flag selecting extra sources). Each carries
    `{key, nextFn, drawFn}` and the lowest sort key is always drawn next.
    This is the IW `R_DrawSurfs` over per-type lists.
  - `FUN_14018c1f0` — emissive/trans list.
- The per-surface-type draw functions then call pass setup
  (`FUN_1401dbb70`/`FUN_1401dbef0`) and the single draw funnel
  `FUN_1401de7d0` (§4).

Stage 2b below resolves the open items from this section.

---

## 8. Draw path, stage 2b — sources, key layout, render targets, post-FX, submit 🟢

### 8.1 The seven draw-surf sources of the k-way merge

`FUN_14018c650` builds a merge context and calls one registration routine per
non-empty list. Each routine is a leaf (no `.pdata`) that appends a 24-byte
entry at `ctx + 0x130 + n*24`:

| Field | Offset in entry | Meaning |
|---|---|---|
| type tag | `+0x00` (`ctx+0x130`) | passed in `edx` by the caller |
| sort key | `+0x04` (`ctx+0x134`) | the 6-bit primary sort key of the list head |
| `nextFn` | `+0x08` (`ctx+0x138`) | advances the list and refreshes the key |
| `drawFn` | `+0x10` (`ctx+0x140`) | draws the head run |

`ctx+4` is the source count. The merge always draws the source with the
lowest key next, which gives IW's global primary-sort-key order across all
list types.

| # | Register | List (`begin`/`end` in ctx) | Key source | `drawFn` → drawer → inner loop | Vertex decl set | What it draws |
|---|---|---|---|---|---|---|
| 1 | `FUN_140184090` | `+0x08/+0x10`, 4-byte items | item `u16 @+2` → 64-bit draw-surf table `[*0x141888648 + 0x90]`, bits 57–62 | `0x140184990` → `FUN_140199760` → `FUN_1401b1540`/`FUN_1401b1660` | `VERTDECL_WORLD + worldVertFormat` | BSP surfaces through an index list copied into a dynamic IB (`FUN_1401daf10` from `[*0x141888660 + 0xA0]`): world pre-tessellated batches 🟡 |
| 2 | `FUN_140184110` | `+0xB8/+0xC0`, raw 64-bit draw-surfs | bits 57–62 of the item | `0x140184c20` → surfType jump table `0x14040e5b0` | per type | generic draw-surfs: brush models, XModels, code meshes (see 8.2) |
| 3 | `FUN_140184190` | `+0x20/+0x28`, 4-byte items | `item & 0x3FFF` → byte table `0x1415e5200` | `0x140184f30` → `FUN_140199080` → `FUN_140197ba0`/`FUN_140197d70` | `VERTDECL_WORLD + worldVertFormat` | BSP surfaces from the static world VB (stride **44** = `GfxWorldVertex`) and the world IB (`SetIndices` when `backEnd+0x419E0` changes) |
| 4 | `FUN_140184210` | `+0x80/+0x88` | `(item >> 16) & 0xFFF` → `0x1415e5200` | `0x140185000` → `FUN_1401993e0` → `FUN_1401b34f0`/`FUN_1401b3670` | `VERTDECL_STATICMODELCACHE` (15) | cached static models (smodel cache VB `0x1415f30f0`, stride 32, IB `0x1415f30e8`) |
| 5 | `FUN_1401842a0` | `+0xA0/+0xA8` | same | `0x1401850e0` → `FUN_1401994c0` → `FUN_1401b3800` (→ `FUN_1401b3e50`)/`FUN_1401b3870` | 15 | cached static models, second variant (instanced path) 🟡 |
| 6 | `FUN_140184330` | `+0x40/+0x48` | same | `0x1401851c0` → `FUN_1401995a0` → `FUN_1401b3b80`/`FUN_1401b3d20` | `VERTDECL_PACKED` (1) | rigid static models from their own XSurface VB/IB (`FUN_1400a6020`/`FUN_1400a61f0`), per-instance data at `[0x141887c30]+0x340 + idx*88` |
| 7 | `FUN_1401843b0` | `+0x60/+0x68` | same | `0x1401852a0` → `FUN_140199690` → `FUN_1401b38f0`/`FUN_1401b3ad0` | 1 | skinned static models: vertices copied into the dynamic ring VB `0x1415f31a8` (stride 32, wraps to 0) and drawn from there |

- `0x1415e5200` is a byte per sorted material (12-bit index; 14-bit for the
  BSP list): the material's primary sort key, precomputed so the merge
  doesn't have to dereference materials.
- Every `drawFn` follows the same shape: `FUN_1401b2210` (select material
  from the packed key; returns 0 to skip), `FUN_1401b2450` (select
  technique), `R_SetupPass(0)` (`FUN_1401dbb70`), the drawer; then, when the
  technique has a second pass (`technique+0xA != 1`), `FUN_1401b2450` and
  `R_SetupPass(1)` again and a second drawer call. Each drawer has two inner
  loops, one for technique type 9 and one for everything else.
- Drawers 1 and 3–5 set `state+0x1778` (stream-source cache) to
  `0x1415e5140` and drawers 6–7 set it to `3`, then set the vertex type in
  `state+0x120` and call `R_SetVertexShaderAndDecl` (`FUN_1401dbef0`).

### 8.2 64-bit draw-surf key layout

Read directly off `0x140184c20`, `FUN_140184090` and `FUN_140184110`:

| Bits | Field | Evidence |
|---|---|---|
| 57–62 | primary sort key (6) | `shr 0x39; and 0x3f` in all merge sources 🟢 |
| 53–56 | surfType (4) | `shr 0x35; and 0xf` → jump table index 🟢 |
| 52 | 1-bit flag carried into bit 31 of the packed material word 🟢 (meaning 🔴) |
| 44–51 | scene light index (8) | `shr 0x2c; movzx al` 🟢 |
| 43 | 1-bit flag (hero lighting in the IW5 layout) | `shr 0x2b; and 1` 🟢/🟡 |
| 29–40 | material sorted index (12) | `shr 0x1d; and 0xfff` 🟢 |
| 24 | has-GfxEntity-index flag, only consulted for surfTypes 7 and 9 | `bt rdx, 0x18` after `surfType-7 ∈ {0,2}` 🟢/🟡 |
| 0–23 | object id (16) + reflection probe index (8) | IW5 lineage 🟡 |

The generic list's surfType table `0x14040e5b0` has only entries 6–9 filled:
6 `FUN_140198480`, 7 `FUN_140199890`, 8 `FUN_140199a70`, 9 `FUN_140199de0`.
In the IW4/IW5 enum these are `SF_BMODEL`, `SF_XMODEL_RIGID`,
`SF_XMODEL_SKINNED`, `SF_CODEMESH`. World triangles (0–1) and static models
(2–5) never reach this list, because they have the dedicated sources above.

### 8.3 Render targets (IDs recovered from the binary) 🟢

Name table `0x1404d0740` (used by the creation helper `FUN_1401d4440` for
debug names):

| ID | Name | Seen in |
|---|---|---|
| 0 | `R_RENDERTARGET_SAVED_SCREEN` | |
| 1 | `R_RENDERTARGET_FRAME_BUFFER` | post-FX output (`FUN_1401939f0` binds 1 throughout) |
| 2 | `R_RENDERTARGET_SCENE` | alternate clear path `FUN_14018e010` |
| 3 | `R_RENDERTARGET_RESOLVED_POST_SUN` | |
| 4 | `R_RENDERTARGET_RESOLVED_SCENE` | |
| 5 | `R_RENDERTARGET_FLOAT_Z` | depth / float-Z pass in `RB_DrawView` |
| 6, 7 | `R_RENDERTARGET_PINGPONG_0/1` | |
| 8, 9 | `R_RENDERTARGET_POST_EFFECT_0/1` | |
| 10, 11 | `R_RENDERTARGET_SHADOWMAP_LARGE/SMALL` | sun / spot shadow maps |
| 12 | `R_RENDERTARGET_SSAO` | |
| 13 | `R_RENDERTARGET_SSAO_BLURRED` | |
| 14 | `R_RENDERTARGET_SSAO_FLOAT_Z` | full-res SSAO path `FUN_140196df0` |

- Table `0x141bb6e00`, 32 bytes per ID: `{GfxImage* image, IDirect3DSurface9*
  color, IDirect3DSurface9* depthStencil, u32 width, u32 height}`. The
  creation helper `FUN_1401d4440(…, w, h, format, isColor, rt)` allocates the
  image (`FUN_1401b8c80` + `FUN_1401b92d0`), takes surface level 0
  (`FUN_1401b91b0`) and stores it as colour (`+0x08`) or depth (`+0x10`).
  All targets are created by `FUN_1401d4f60`, called from `FUN_1401bd730`
  during device setup.
- **`FUN_1401dff30(state, id)`** only records the target for the frontend
  state: `state[0x181C/4..]` = `{0x140421ad0[id], width, height}`.
- **`FUN_1401dfd80(ctx, id)` = `R_SetRenderTarget(id)`**: no-op if `id` is
  already current (`prim+0xBD8`, 16 = none). It clears the sRGB-write state
  bit (render state 194) if set, unbinds any sampler still bound to this
  target's image (stages 0–15 and vertex samplers 241+ via `SetTexture(…,
  NULL)`), then `SetRenderTarget(0, color)` (+0x128) and
  `SetDepthStencilSurface(depth)` (+0x138) only when they differ from the
  current target's. It then resets the viewport cache (`bd0/bd4` = size,
  `184 = -1`, `188 = 0`, `18c = 1.0`) and marks the projection dirty
  (`state+0x17C0 = 0`, `+0x181C = 1`).

### 8.4 Engine code materials (`rgp`) 🟢

Every engine-owned material is loaded by name from the pair table at
`0x14041fb80` (`{const char* name, Material** dest}`), so post-FX,
shadow and debug passes can be named precisely. Selection:

| Global | Material | Global | Material |
|---|---|---|---|
| `0x141887b58` | `$default` | `0x141887c40` | `blur_apply` |
| `0x141887b60` | `white` | `0x141887c48`/`c50` | `blur_apply_film` / `_color2` |
| `0x141887b88` | `shadowclear` | `0x141887c38` | `feedbackreplace` |
| `0x141887b98`/`ba0` | `w/shadowcaster`, `m/shadowcaster` | `0x141887c58` | `cinematic` |
| `0x141887bb0` | `depthprepass` | `0x141887c60`/`c68`/`c70` | `dof_downsample`, `dof_near_coc`, `small_blur` |
| `0x141887bf8`/`c00` | `stencilshadow`, `stencildisplay` | `0x141887c78`/`c80`/`c88` | `postfx`, `postfx_color`, `postfx_color2` |
| `0x141887c08` | `floatz_display` | `0x141887c98`/`ca0`/`ca8` | `postfx_dof`, `_color`, `_color2` |
| `0x141887c10` | `color_channel_mixer` | `0x141887cb8`…`ce8` | `postfx_grain*` (6 variants) |
| `0x141887c18`/`c20` | `frame_color_debug`, `frame_alpha_debug` | `0x141887cf8`…`d38` | `ssao_calc_slow/fast`, `ssao_apply_*`, `ssao_zdownsample` |
| `0x141887dd0`/`dd8` | `shellshock`, `shellshock_flashed` | `0x141887d40`…`dc0` | `filter_symmetric_1..8` and `_lin` variants |
| `0x141887de0`/`de8` | `glow_consistent_setup` / `_color2` | `0x141887df0` | `glow_apply_bloom` |
| `0x141887df8` | `m/thermalbody_default` | `0x141887e00` | `watersheeting_color_distort_blur` |
| `0x141887e10` | `nightvision_grain` | `0x141887e18` | `digital_distort` |

### 8.5 Post-FX chain `FUN_1401939f0(view, …)` 🟢/🟡

Runs per view that rendered into `$scene`, called from `RB_PostFxAndFinish`.
Full-screen material quads are drawn by `FUN_14018bbc0(material, …)`.

1. Seed `GfxCmdBufState` from the view, clear the 0xA00 scratch block, and
   select `R_RENDERTARGET_FRAME_BUFFER` (1). Set the resolved-scene code
   image `0x1415e9000` to the image of target `view+0xA10`.
2. **Depth of field / blur parameters** (`view+0x32C`): cache focus
   near/far/bias (`view+0x300..0x310`, compared with
   `0x1415e8e30..0x1415e8e3c`, bumping a generation counter when they change),
   re-select target 1, then the water-sheeting distortion pass
   (`FUN_140189990(watersheeting_color_distort_blur)`), then the DOF
   downsample/blur via `FUN_1401949c0` / the plain composite `FUN_14018bbc0`.
3. **Bloom/glow** (`view+0x264`, glow dvar `0x141884f80`, and the two glow
   intensities `view+0x270/0x274`): `FUN_14018fb70` (glow setup and
   downsample into the ping-pong targets), re-select target 1, then
   `glow_apply_bloom` composite.
4. **Digital distortion** when its amount is > 0 (`digital_distort`).
5. **Film blur** when `blur_apply_film` (or `_color2` for the second view
   set) is loaded and the blur radius > 0: `FUN_14018f190` (blur into the
   ping-pong targets) and `FUN_14018b960` composite.
6. **Night vision** (`FUN_140194910` decides) → `nightvision_grain`; a
   further overlay pass for the thermal/pip case (`0x141887e08`).
7. Re-select target 1. Debug views driven by `0x141884e30`
   (1 = frame colour/alpha debug via `frame_color_debug`/`frame_alpha_debug`,
   rebuilding the projection with `FUN_1401e13e0`; 2 = `FUN_1401945a0`) and
   `0x141884e38` (float-Z display).

Motion blur is triggered separately afterwards (`FUN_14018def0`, §7.1).

### 8.6 Frame submit (the mailbox producer) 🟢

- **`FUN_14024aa90(backEndData)` = submit to render thread**: `lock inc` of a
  frame counter (`0x1420058ac`), `backEndData` → mailbox `0x142005990`, then
  `SetEvent(0x142005988)` and `SetEvent(0x142005980)`. The backend takes the
  pointer with `InterlockedExchange` (`0x14024a393`, inside `FUN_14024a390`).
- **`FUN_140249f80` = synchronise with the backend**:
  `WaitForSingleObject(0x142005978, INFINITE)`, `ResetEvent(0x142005970)`,
  `SetEvent(0x142005988)`, `WaitForSingleObject(0x142005968, INFINITE)`. The
  backend's side of the handshake is `FUN_14024a650` (called from
  `RB_RenderThread`): `ResetEvent(0x142005978)`, `SetEvent(0x142005968)`.
  Callers are the frontend's "must own the device now" points:
  `FUN_1401d3360`, `FUN_1401d2bb0`, `FUN_1401d3950` and three sites in
  `FUN_1401d3d30`.
- **`FUN_14024a2a0`** = "is the current thread the render thread"
  (`GetCurrentThreadId() == [0x142005824]`), used by 30 functions as an
  ownership assert.
- **`FUN_1401d32a0(flags)` = begin/issue**: sets `backEnd+0x41A6C` = SMP
  enabled (two dvars `0x141884d60`/`0x141efb808`, and not `0x1418854ca`),
  stores `flags` at `+0x41A68` (bit 1 → `FUN_1401bd650`). In SMP mode it
  acquires the next frontend buffer (`FUN_14024a240` → `0x141896ba0`) and
  submits the current one (`FUN_14024aa90`).
- **`FUN_1401d3360()` = end-of-frame issue**: marks the frame complete
  (`+0x41A7C = 1`). In SMP mode it submits if not already submitted. Without
  SMP it runs the backend **inline on the main thread**: device check
  `FUN_1401bc190`, then `FUN_1401866a0`, `FUN_140186cc0`, `FUN_140186a60` and
  the end-of-frame/swap `FUN_1401898c0(flags)`. It also latches
  `0x141888624` = "command buffer used ≥ 0x410000 bytes" (`backEnd+0x419C0`).
- Callers: the client screen update `FUN_140083700` (issue once, end twice)
  and the shadow pipeline state machine `FUN_1401d6ed0`.

---

*Next stage: UI/menu pipeline end to end (§9).*
