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
Both off by default, so behavior is unchanged unless set. A third,
experimental IW5-SP-only render-pass bridge prerequisite is also included;
it is disabled by default and does not enable tessellation.

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
   explicitly enabled, this experimental bridge first requires the verified
   `iw5sp.exe` PE identity and then a unique render-target dispatcher
   signature before using vendored MinHook to scope the engine's target ID
   around its original call. DXVK associates that ID with its RT0 state and
   can emit a diagnostic for the first five and then every 2,000th indexed
   draw in scene target 2, including VS/PS cache names, declaration blend
   flags, and used vertex-buffer size/usage/stride; it does not dump buffer
   contents or change draw state. This is an IW5-specific research
   prerequisite, not tessellation: no TCS/TES stages are injected, and the
   hook has not been live-tested. See `re_notes/known_issues.md` issue #3.

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
