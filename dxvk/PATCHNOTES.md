# Patch Notes — MW32011DXVK

Real, notable changes made in this fork, per release, on top of the real
upstream [doitsujin/dxvk](https://github.com/doitsujin/dxvk) this project is
forked from. This fork's own versioning and release cadence is independent
of the sibling `MW32011NCP` project's own `-x64` releases — see `README.md`
for the real reasoning. See `re_notes/known_issues.md` for the full
investigation/reverse-engineering trail behind each entry.

---

## Unreleased

**Summary:** Two patches on top of upstream `v3.1.1` are build/host
prerequisites for NVIDIA Streamline/DLSS integration: an opt-in
`dxvk.enableNvCudaInteropNative` option (live-confirmed enabling the real
`VK_NVX_*` extensions DLSS needs), and `DXVK_VULKAN_LOADER_OVERRIDE`, an
opt-in env var that lets a host point DXVK's Vulkan loader at a specific
file instead of the normal winevulkan/vulkan-1 search — needed so
Streamline's own `sl.interposer.dll` can sit in front of DXVK's
`vkCreateInstance`/`vkCreateDevice` calls for its mandatory swapchain hooks.
Both off by default, so behavior is unchanged unless set. An opt-in,
experimental IW5-SP render-pass bridge and a D3D9 tessellation prototype are
also present -- the prototype now builds clean and does genuine PN-triangle
(Phong) position bending using real per-vertex normals, not flat
subdivision, but has never been exercised in a live game process. Not a
verified working feature yet.

### What's New
1. **`dxvk.enableNvCudaInteropNative` (default `False`).** Upstream enables
   `VK_NVX_binary_import`/`VK_NVX_image_view_handle` only under winevulkan,
   so a native-Windows host running NVIDIA Streamline against DXVK's own
   `VkDevice` can never get them — DXVK owns `vkCreateDevice`. Setting this
   option to `True` allows them on native drivers too; the existing 32-bit,
   safe-mode and `dxvk.enableNvCudaInterop` conditions still apply, and a
   failed device creation still falls back to safe mode without them. Not
   game-specific. **Live-confirmed 2026-09-24**: both extensions report `1`
   (previously `0`) in a real DXVK device-info log with this option set. See
   `re_notes/known_issues.md` issue #2.
2. **`DXVK_VULKAN_LOADER_OVERRIDE` (env var, empty/unset by default).** When
   set to an absolute path, `loadVulkanLibrary()` (`src/vulkan/vulkan_loader.cpp`)
   tries `LoadLibraryA` on that exact path first, before falling back to the
   normal `winevulkan.dll`/`vulkan-1.dll` search — unchanged when unset. Real
   motivation: NVIDIA Streamline's manual-hooking mode still needs its own
   `sl.interposer.dll` acting as the actual Vulkan loader the host's
   `vkGetInstanceProcAddr` resolves through, so it can intercept the
   swapchain/present entry points DLSS needs (`ProgrammingGuideManualHooking.md`
   section 2.7) — `slSetVulkanInfo` alone (already wired on the `MW32011NCP`
   side) isn't sufficient for that. A path, not a bare DLL name, since a host
   that extracts/embeds `sl.interposer.dll` to a private location (as
   `MW32011NCP` now does) can't rely on the normal DLL search order finding
   it. Not game-specific. Not yet build-verified against a real Streamline
   session (built clean; the interposer itself hasn't been exercised through
   this path live yet).
3. **`d3d9.iw5RenderPassBridge` (default `False`, Windows only).** When
   explicitly enabled, this experimental path first requires the verified
   `iw5sp.exe` PE identity and then a unique render-target dispatcher
   signature before using vendored MinHook to scope the engine's target ID
   around its original call. DXVK associates that ID with its RT0 state and
   can emit rate-limited diagnostics for indexed draws in scene target 2.
   Source also contains an experimental TCS/TES attempt gated by this option,
   target 2, indexed triangle-list draws, and a conservative vertex-layout/
   buffer eligibility check. It uses a fixed tessellation level; position is
   now bent using real, genuine PN-triangle (Phong/curved-triangle) cubic
   Bezier evaluation against the real per-vertex normal when the vertex
   shader has one (falling back to flat/linear blend otherwise), every other
   varying still uses flat barycentric interpolation. It builds clean as of
   2026-09-30 but does not identify a verified static prop and has not been
   game-tested. Do not treat it as a working feature. The bridge itself has
   not been live-tested. See `re_notes/known_issues.md` issue #3.

### Groundwork
1. **Issue #1 resolved as not a DXVK bug.** A motion-blur post-process pass
   in the sibling `MW32011NCP` project produced no visible effect under this
   DXVK build; the real bug was in `MW32011NCP`'s own game-logic code, so no
   patch landed for it. The native-Windows build toolchain
   (MSYS2/MinGW-w64/Meson/Ninja/glslang) set up during that investigation
   stays in place.
2. **Vulkan tessellation-stage driver proof (2026-09-27).** A temporary
   native Vulkan probe successfully created a graphics pipeline on an
   NVIDIA GeForce RTX 2080 Ti using the compiled VS/TCS/TES/FS prototype,
   three-control-point patches, and a compatible render pass. This validates
   driver-level pipeline creation only: no draw was issued, no D3D9 frontend
   path was changed, and no game output is claimed. See issue #3 in
   `re_notes/known_issues.md`.
3. **D3D9 tessellation integration attempt (2026-09-27), fixed and upgraded to real PN-triangle math (2026-09-30).** The current
   source synthesizes per-shader TCS/TES modules and switches an eligible
   indexed draw to three-control-point patches. The original 2026-09-27
   compile failure (`ir::Type::getBaseType()` called without its required
   argument) is fixed. The TES was also restructured from flat linear
   interpolation of every VS output into genuine PN-triangle position
   bending (real Vlachos et al. cubic Bezier control-point construction
   using the vertex shader's own real NORMAL output, when present) -- the
   real "honest first milestone" this feature's own original handoff
   scoped, not the fake/flat placeholder. Build-verified: the full fork
   builds 35/35 targets clean via a real, working native-Windows
   Meson/MinGW toolchain. Still an unverified experiment; no game
   validation or static-prop identification has occurred, and this DXVK
   build is not what `MW32011NCP` currently loads live.
