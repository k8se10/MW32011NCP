# Patch Notes — MW32011DXVK

Real, notable changes made in this fork, per release, on top of the real
upstream [doitsujin/dxvk](https://github.com/doitsujin/dxvk) this project is
forked from. This fork's own versioning and release cadence is independent
of the sibling `MW32011NCP` project's own `-x64` releases — see `README.md`
for the real reasoning. See `re_notes/known_issues.md` for the full
investigation/reverse-engineering trail behind each entry.

---

## Unreleased

**Summary:** First patch on top of upstream `v3.1.1`: an opt-in
`dxvk.enableNvCudaInteropNative` option that lets a native-Windows host
integrating DLSS get the `VK_NVX_*` extensions DLSS needs on DXVK's Vulkan
device. Off by default, so behavior is unchanged unless it is set.

### What's New
1. **`dxvk.enableNvCudaInteropNative` (default `False`).** Upstream enables
   `VK_NVX_binary_import`/`VK_NVX_image_view_handle` only under winevulkan,
   so a native-Windows host running NVIDIA Streamline against DXVK's own
   `VkDevice` can never get them — DXVK owns `vkCreateDevice`. Setting this
   option to `True` allows them on native drivers too; the existing 32-bit,
   safe-mode and `dxvk.enableNvCudaInterop` conditions still apply, and a
   failed device creation still falls back to safe mode without them. Not
   game-specific. Not yet built or live-tested. See
   `re_notes/known_issues.md` issue #2.

### Groundwork
1. **Issue #1 resolved as not a DXVK bug.** A motion-blur post-process pass
   in the sibling `MW32011NCP` project produced no visible effect under this
   DXVK build; the real bug was in `MW32011NCP`'s own game-logic code, so no
   patch landed for it. The native-Windows build toolchain
   (MSYS2/MinGW-w64/Meson/Ninja/glslang) set up during that investigation
   stays in place.
