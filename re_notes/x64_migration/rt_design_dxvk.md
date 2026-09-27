# Real in-engine ray tracing on the MW32011DXVK fork — design (2026-09-27)

**Status: design, not implemented.** Stage 4 of the renderer reference
(`renderer_end_to_end.md` §10 links here). Every engine fact below is backed
by that reference (section numbers in brackets). Every DXVK fact was read from
the vendored fork in `dxvk/` (upstream v3.1.1 plus the two Streamline patches).

**Scope and rules this design follows:**
- SP x64 only, and only in `[Video] GraphicsApi=Vulkan` mode (the only mode
  where a Vulkan device exists). MP comes later, after SP precedent, like
  every other Vulkan feature in this project.
- The fork stays standalone (fork `README.md`): any fork change is a small,
  generic, opt-in capability, never MW3-specific logic. All engine knowledge
  lives in the proxy.
- No shader rewrites of the game's SM3 material shaders in the first
  milestones. RT results are fed back through inputs the engine already
  consumes (the SSAO render target, the sun shadow lookup).
- Signature-scanned hooks only (CLAUDE.md §5/§10.3). Addresses here are RE
  coordinates.
- Every feature is off by default, with a clean fallback when the GPU or
  driver lacks ray query.

---

## 1. What already exists (why this is feasible)

| Need | Already available | Where |
|---|---|---|
| Vulkan device, queue and instance of the D3D9 device | `ID3D9VkInteropDevice::GetVulkanHandles` / `GetSubmissionQueue` | `proxy_d3d9/src/dxvk_interop_x64.h`, `streamline_integration_x64.cpp` |
| Safe submission of our own Vulkan work between DXVK's | `FlushRenderingCommands` → `LockSubmissionQueue` → `vkQueueSubmit` → `ReleaseSubmissionQueue` (the fix for the 2026-09-27 `DEVICE_LOST`) | `streamline_evaluate_x64.cpp` |
| `VkImage` behind any D3D9 surface (depth, colour) | `ID3D9VkInteropTexture::GetVulkanImageInfo`, plus a view cache (`GetOrCreateViewX64`) | `streamline_resources_x64.cpp` (`TagDepthResourceForFrame`) |
| Per-pixel motion vectors | camera plus per-object motion for DLSS | `streamline_camera_x64.cpp`, `streamline_object_motion_x64.cpp` |
| Camera matrices | the current view's `GfxViewParms` at `*(GfxCmdBufState+0x1790)` (view +0x00, projection +0x40, viewProj +0x80, invViewProj +0xC0), set by `FUN_1401e0880`; already read by the DLSS camera path (ROUND 24) | [§3 op 25], `streamline_camera_x64.cpp` |
| Linear depth | `R_RENDERTARGET_FLOAT_Z` (id 5), built in `RB_DrawView` step 5 | [§7.1, §8.3] |
| An AO input the engine already composites | `R_RENDERTARGET_SSAO` / `SSAO_BLURRED` (ids 12/13), applied by `ssao_apply_fullres`/`_downsampled` | [§7.1 step 10, §8.3, §8.4] |
| Buffer device addresses | `bufferDeviceAddress` is a **required** feature in DXVK 3.x, and every `DxvkBuffer` gets `SHADER_DEVICE_ADDRESS` | `dxvk_device_info.cpp:914`, `dxvk_buffer.cpp:31` |
| CPU copies of all static geometry | zone memory stays resident while a level is loaded | §3 below |

What's missing:
- **Ray tracing on the device.** `VK_KHR_acceleration_structure`,
  `VK_KHR_ray_query` and `VK_KHR_deferred_host_operations` are not enabled,
  and the loader (`src/vulkan/vulkan_loader.h`) has no acceleration-structure
  entry points. DXVK owns `vkCreateDevice`, so only the fork can add them.
  That's the single fork change (§2).
- Everything else, RT itself included, lives in the proxy.

## 2. Fork change: opt-in ray-tracing interop (generic, upstreamable)

Same shape as the existing `dxvk.enableNvCudaInteropNative` patch.

1. **Option** `dxvk.enableRayQueryInterop` (bool, default `False`) in
   `dxvk_options.{h,cpp}`.
2. **Extensions and features** in `dxvk_device_info.{h,cpp}`, added to the
   `HANDLE_EXT` lists and the feature chain:
   - `VK_KHR_deferred_host_operations` (a hard dependency of acceleration
     structures);
   - `VK_KHR_acceleration_structure`: `accelerationStructure`, plus
     `descriptorBindingAccelerationStructureUpdateAfterBind` if supported;
   - `VK_KHR_ray_query`: `rayQuery`.
   - Enable them only when the option is on, the platform is not 32-bit, and
     not in safe mode, all three are supported, and `bufferDeviceAddress` is
     present. Otherwise force the features to `VK_FALSE`, so a failed device
     creation still falls back to safe mode as today.
   - Log the result in the existing device-info dump (`accelerationStructure=`,
     `rayQuery=`).
3. **Loader** (`vulkan_loader.h`): add `vkCreateAccelerationStructureKHR`,
   `vkDestroyAccelerationStructureKHR`,
   `vkGetAccelerationStructureBuildSizesKHR`,
   `vkCmdBuildAccelerationStructuresKHR`,
   `vkGetAccelerationStructureDeviceAddressKHR`,
   `vkCmdWriteAccelerationStructuresPropertiesKHR`, under the new
   extension's guard. DXVK itself never calls them. They're there so the
   fork's own diagnostics can verify the device, and they document intent.
   The proxy resolves its own pointers through `vkGetDeviceProcAddr`, as it
   already does for Streamline.
4. **No change to DXVK's rendering.** DXVK never builds or traces anything.
   It just creates a device on which a host can.

This is generic ("let an interop host use ray query on DXVK's device") and
is a candidate to offer upstream. Fork version bump plus a `PATCHNOTES.md`
entry.

## 3. Geometry sources (all CPU-resident, no GPU readback needed)

| Category | Engine data | Layout (iw5oat `IW5_Assets.h`, strides confirmed in the binary) | BLAS strategy |
|---|---|---|---|
| **World (BSP)** | `GfxWorld::draw` → `GfxWorldDraw { … vertexCount; vd.vertices (GfxWorldVertex*); … indexCount; indices (u16*) }`; surfaces `GfxSurface { srfTriangles_t tris {vertexLayerData, firstVertex, vertexCount, triCount, baseIndex}; Material* material; … }` | `GfxWorldVertex` = **44 bytes**, `xyz` at +0 (drawer 3 binds stride 44 [§8.1]) | Build once per level load. One BLAS for all opaque world surfaces (one geometry per material class, so opacity flags differ), plus a separate non-opaque BLAS for alpha-tested surfaces (§6.3). Vertex format `R32G32B32_SFLOAT`, stride 44, index `UINT16` with `baseIndex` offsets. |
| **Static models** | `GfxStaticModelDrawInst[]` at `[*0x141887c30] + 0x340` (stride **88**, confirmed by drawers 6/7 [§8.1]): `placement {origin[3], axis[3][3], scale}`, `XModel* model`, … | `XSurface { …, vertCount, triCount, triIndices (XSurfaceTri16*), verts0 (GfxPackedVertex*, 32 bytes, xyz at +0) }` | One BLAS per unique `XModel` surface set (LOD 0), built lazily the first time a model is seen, cached per zone. One TLAS instance per draw inst, transform = `axis * scale`, translation = `origin`. |
| **Brush models / rigid XModels** (scene entities: doors, vehicles, props) | generic draw-surf list surfTypes 6/7 [§8.2]; entity poses from the frontend scene-entity list | same `XSurface` data | BLAS per model as above. Per-frame TLAS instances from the entity's world matrix. |
| **Skinned XModels** (characters, animated props) | skinned verts are CPU-built each frame and copied into the dynamic ring VB `0x1415f31a8` (drawer 7 [§8.1]) and the skinned cache | 32-byte packed verts | Capture the CPU vertex block at the engine's copy point (the `memcpy` into the locked ring VB in `FUN_1401b38f0` and its skinned-entity sibling), upload to a proxy-owned buffer, **refit** (`VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE`) a per-entity BLAS each frame. Budget: ≤ 16 characters refit per frame, beyond that the previous frame's BLAS is reused. |
| **Viewmodel** (first-person weapon/arms) | drawn in its own view | — | **Excluded** from the TLAS by instance mask: it shouldn't cast world shadows or AO onto the scene. |
| **FX, particles, code meshes, decals** | surfType 9 and the emissive list | — | Excluded (not solid geometry). |

**Why CPU sources rather than DXVK's buffers:** D3D9 has no buffer interop
in DXVK (only textures have `ID3D9VkInteropTexture`), and the static data is
already in zone memory with known layouts. Uploading it once per level into
proxy-owned `VkBuffer`s (usage `ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY |
SHADER_DEVICE_ADDRESS | TRANSFER_DST`) avoids any fork change beyond §2 and
any dependency on DXVK's internal buffer lifetime.

**Level lifetime:** build at the end of level load (the proxy already detects
level state: `TryGetInLevelFlagX64` / `TryGetClcStateX64`). Destroy on level
unload, before the zone memory is freed. The world BLAS build runs on the
DXVK queue during the load screen, with compaction
(`vkCmdWriteAccelerationStructuresPropertiesKHR` + copy-compact), so the
runtime footprint is minimal.

## 4. Frame integration (where RT work runs)

`RB_DrawView` (`FUN_14018e0d0`) pass order [§7.1] gives two natural
insertion points, both on the backend thread (the only thread that touches
D3D9 [§0]):

```
RB_DrawView
  1 dynamic VB upload
  3 sun / spot shadow maps
  5 float-Z pass → R_RENDERTARGET_FLOAT_Z (5)        ← (A) after this: depth ready
  6 bind scene target, clear
  7 lit opaque list                                  ← consumes the sun shadow term
  8 depth prepass (optional)
  9 second lit list, emissive/trans
 10 SSAO → R_RENDERTARGET_SSAO (12)/(14) → apply     ← (B) replace the SSAO calc
 11 rebind view
RB_PostFxAndFinish → post-FX [§8.5]
```

- **(A) after the float-Z pass:** trace the **sun shadow mask** (and
  optionally spot-light masks) so they're ready before the lit list samples
  shadows.
- **(B) in place of the SSAO calc:** trace **RTAO** into the SSAO target. The
  engine's own `ssao_apply_*` material then composites it exactly as it does
  SSAO today.

**Per-insertion-point mechanics** (the proven Streamline pattern):
1. `FlushRenderingCommands()`, so DXVK submits everything recorded so far,
   including the float-Z pass.
2. Record one proxy command buffer:
   - a barrier from all prior work;
   - TLAS update (instance buffer written from the CPU this frame);
   - the ray-query compute dispatches;
   - the denoiser passes;
   - layout transitions on the output image.
3. `LockSubmissionQueue()` → `vkQueueSubmit` → `ReleaseSubmissionQueue()`.

Same-queue submission order plus the initial full barrier gives correct
ordering against DXVK's preceding work. Output images are D3D9 textures
created through the device (so DXVK tracks them), and we transition them
with `TransitionTextureLayout` back to the layout DXVK expects, exactly as
the DLSS output path does.

**Hook points** (signature-scanned):
- **(A):** a detour on the float-Z helper `FUN_14018dcd0` when called with
  target 5, or on `RB_DrawView` with the insertion done after that call site.
  The detour calls the original, then runs the RT shadow pass.
- **(B):** a detour on the SSAO path: `FUN_140197670` (the "SSAO allowed"
  test) and the full-res calc `FUN_140196df0`. When RTAO is enabled and ready,
  run RTAO into target 12/14 instead of calling the calc, then let the
  engine's blur/apply continue (or skip the blur, since RTAO has its own
  denoiser, by writing straight to `SSAO_BLURRED` (13)).
- **Per-frame TLAS build:** frontend end-of-frame (`FUN_1401d3360`, the
  end-of-frame issue [§8.6]) snapshots entity instance transforms, so the
  backend frame and the TLAS always describe the same frame. Double-buffered,
  like `backEndData`.

**Cost of the extra flushes:** one or two additional DXVK submissions per
frame, the same order as the existing DLSS evaluate path. Acceptable on the
GPUs that support ray query at all.

## 5. The passes

All passes are compute shaders using `GL_EXT_ray_query` (GLSL, compiled
offline with glslang to SPIR-V and embedded as headers, the same way the
proxy already embeds its HLSL passes: `fsr_rcas_ps.h`, `smaa_*_ps.h`).
Inputs: depth (`VkImage` via interop, as the Streamline depth tag does),
camera constants, the TLAS.

### 5.1 World position and normal without a G-buffer

The engine has no normal buffer (SM3 forward renderer). Two options,
chosen by config:
- **Reconstructed (default):** world position from depth plus
  `invViewProj`; normal from depth derivatives using the least-discontinuity
  neighbour (the standard 3-tap/5-tap reconstruction). Cheap; artefacts at
  silhouettes are hidden by the denoiser's edge stopping.
- **Primary-ray exact:** one extra ray per pixel from the camera through
  the pixel, `rayQueryGetIntersectionTriangleVertexPositionsEXT`-free
  variant. Fetch the three triangle vertices through the hit's instance and
  primitive IDs from a proxy-owned vertex-address table (BDA), compute the
  geometric normal. Exact, costs a ray. Needs
  `VK_KHR_ray_tracing_position_fetch` for the cheap path, where available.

### 5.2 RT ambient occlusion (milestone 1)

- 1 ray/pixel at internal resolution (so DLSS upscales the result like
  everything else), cosine-weighted hemisphere, max distance
  `RtAoRadius` (default 96 units ≈ the SSAO radius in world units),
  blue-noise sequence per frame. Instance mask = world, smodels, entities.
- Output a single-channel AO into `R_RENDERTARGET_SSAO_BLURRED` (13) after
  denoising (§6), in the value convention the `ssao_apply_*` shader expects
  (1 = unoccluded). **Must validate** that convention by reading one frame of
  the engine's SSAO target (RenderDoc capture path already exists:
  `renderdoc_capture_x64.cpp`).
- Why first: zero shader changes, a clear visual win, and it exercises every
  piece of infrastructure (BLAS/TLAS, interop sync, denoiser, level
  lifetime).

### 5.3 RT sun shadows (milestone 2)

- 1 ray/pixel toward the sun direction (from the sun light's code constant,
  the same one the shadow pass uses), with a small cone angle for soft
  penumbra. Instance mask excludes the viewmodel.
- **Feeding it back without touching material shaders:** lit techniques
  sample the sun shadow through the code sampler `SHADOWMAP_SUN` (6) with
  `SHADOW_LOOKUP_MATRIX` (0x60) [§5.2]. Plan: bind a screen-space visibility
  texture to that sampler, and set the lookup matrix to the view-projection
  with its z row replaced so the compare reference is a constant 0.5. The
  hardware depth compare then yields visibility = texel (1 lit / 0 shadowed)
  per pixel.
- **Hypothesis to validate before building:**
  1. How the IW5 sun-shadow pixel shaders sample: `tex2Dproj` with hardware
     PCF, or manual taps. Disassemble the `LIT_SUN_SHADOW*` technique pixel
     shaders from `code_post_gfx.ff` with the iw5oat tooling.
  2. How they select between the two cascades (the 0x1800×0x400 /
     0x3000×0x800 atlas partitions seen in `FUN_14019b960`). The matrix trick
     only works if the partition selection can also be neutralised through
     code constants.
- If either fails, the fallback is shader replacement for the
  `LIT_SUN_SHADOW*` pixel shaders only (a bounded set, keyed by bytecode
  hash at `CreatePixelShader`), which sample the mask directly.
- The native sun shadow-map pass (`FUN_1401978e0`) can then be skipped when
  RT shadows are active, which **recovers** the cost the shadow pass
  currently takes (relevant to the issue #4 performance work).

### 5.4 Spot-light shadows (milestone 3)

Same mechanism for the spot shadow sampler `SHADOWMAP_SPOT` (7). One ray
per pixel per shadowed spot light in range (the engine already limits these;
the proxy's per-light shadow dispatch hook `kPerLightShadowDispatchSignature`
already enumerates them).

### 5.5 Reflections (milestone 4, research)

Specular reflection needs material roughness and albedo the forward
renderer never writes out. Possible route: a screen-space-plus-RT hybrid for
the reflection-probe term (`REFLECTION_PROBE` sampler 0x1A), shading hits
from the resolved scene colour (`RESOLVED_SCENE`) when on screen and from the
nearest reflection probe otherwise. Research item, not scheduled.

## 6. Denoising

### 6.1 Built-in (milestones 1–3)

- **Temporal accumulation:** reprojection with the existing motion vectors
  (DLSS path), depth plus normal similarity rejection, history clamp,
  α = 0.1.
- **Spatial:** 2–3 iterations of a depth- and normal-aware à-trous
  filter.
- AO and shadow are single-channel, so this is cheap and entirely under the
  proxy's control. It needs two history images per signal (created as D3D9
  textures via the device, like the DLSS output).

### 6.2 Later options

- **NVIDIA NRD** (open source; Vulkan via NRI): SIGMA for shadows, REBLUR
  for AO. Higher quality, larger integration.
- **DLSS Ray Reconstruction** through the Streamline integration that
  already exists. It needs normals/roughness/albedo guides the engine doesn't
  produce, so it only becomes viable after §5.1's exact normals plus
  approximations for the rest.

### 6.3 Alpha-tested geometry (foliage, fences)

- **Milestone 1:** alpha-tested world surfaces go into a separate
  non-opaque geometry that AO and shadow rays **skip**
  (`gl_RayFlagsOpaqueEXT` plus a cull mask), so foliage doesn't cast solid
  black blobs.
- **Milestone 2:** proper alpha testing in the ray query candidate loop:
  fetch UVs and sample the material's colour-map alpha. That needs a bindless
  table of the materials' `IDirect3DTexture9` `VkImage`s, obtained through
  `ID3D9VkInteropTexture` per texture at level load, or
  `VK_EXT_opacity_micromap` where supported.

## 7. Proxy module layout (planned files)

| File | Role |
|---|---|
| `rt_device_x64.{h,cpp}` | capability probe (extensions/features reported by the device), function pointers via `vkGetDeviceProcAddr`, memory helpers, the shared submit helper (the Flush/Lock/Submit/Release sequence, factored out of `streamline_evaluate_x64.cpp` so both use one implementation) |
| `rt_scene_x64.{h,cpp}` | level-lifetime geometry: world BLAS, smodel BLAS cache, per-frame TLAS, skinned refits, instance snapshotting at end of frame |
| `rt_passes_x64.{h,cpp}` | RTAO / RT shadow dispatches, the denoiser, the output images |
| `rt_hooks_x64.cpp` | the signature-scanned detours (float-Z, SSAO, end of frame, skinned vertex capture), resolved once at startup |
| `rt_shaders/*.comp` + generated `rt_*_cs.h` | GLSL sources and embedded SPIR-V |

**Config** (`mw3ncp_config.ini`, `[Video]`, all off by default, SP x64,
Vulkan mode only):
- `RayTracedAO` = 0/1;
- `RayTracedSunShadows` = 0/1;
- `RtQuality` = 0 low (½-res trace), 1 medium (full-res, 1 spp), 2 high
  (full-res, 2 spp);
- `RtAoRadius` (units);
- `RtExactNormals` = 0/1.

**Diagnostics:** a one-time capability log line; BLAS/TLAS sizes and build
times at level load; per-frame GPU timings through the existing
`gpu_timing_probe_x64.cpp` timestamp path.

## 8. Implementation order and acceptance criteria

1. **Fork §2.**
   - Build: the device-info log shows `accelerationStructure=1 rayQuery=1`
     with the option on, and identical behaviour with it off.
   - Tag a fork release.
2. **`rt_device_x64`:** capability probe plus the shared submit helper.
   Streamline evaluate switched to the shared helper, with no behaviour
   change (the regression check is DLSS still working).
3. **`rt_scene_x64`:** world BLAS at level load, with timing and memory
   logged, plus a debug view that traces primary rays and writes hit
   distance to a debug target. **Acceptance:** the debug view lines up
   exactly with the rasterised world (this validates the vertex data, the
   index offsets and the camera matrices).
4. **Static model and entity instances** added to the TLAS. Same debug-view
   acceptance, with props present and correctly placed.
5. **RTAO → SSAO target** plus the denoiser. **Acceptance:** stable,
   noise-free AO in motion; SSAO off/on/RTAO comparison screenshots; GPU
   cost within budget (≤ 2 ms at 1440p internal on the reference RTX 2080 Ti).
6. **Skinned refits:** characters occlude and receive AO.
7. **RT sun shadows:** validate the hypotheses in §5.3 first, then implement
   the matrix trick or the bounded shader replacement. Skip the native sun
   shadow pass when active.
8. Spot shadows, alpha testing and quality tiers.

## 9. Risks and constraints

- **Ray-query-capable GPUs only** (RTX 20+, RX 6000+, Arc). Everything
  degrades to the vanilla path when unavailable.
- **Mid-frame flushes** add DXVK submissions. Mitigation: at most two
  insertion points; skip RT when the frame is menu-only
  (`IsMenuActiveX64_Exported`).
- **Zone memory lifetime:** BLAS inputs must be consumed before level
  unload. The proxy must tear down on the same event that frees the zone.
- **Fixed-count engine arrays** (`MAX_DRAWSURFS` etc.
  [memory_ceiling_analysis.md §2]) are unaffected, because RT doesn't go
  through the draw-surf lists. The TLAS instance count is bounded by the
  static model count (from `GfxWorld`) plus the per-frame entity cap.
- **Anti-cheat:** SP only, no MP until the VAC analysis in
  `vulkan_dlss_pipeline_research.md` §5 is revisited for this feature.
- **The fork's standalone rule:** the fork change is generic and carries no
  MW3 knowledge. It can't break other games, because it's opt-in.
