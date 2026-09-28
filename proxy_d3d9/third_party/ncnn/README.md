# ncnn (vendored, built from source with the static CRT)

Real [Tencent/ncnn](https://github.com/Tencent/ncnn) — a high-performance
neural network inference framework with a native Vulkan compute backend,
no CUDA/DirectML/ONNX Runtime dependency.

**Why this is here**: groundwork for the runtime AI texture-upscale-cache
feature (`re_notes/x64_migration/texture_upscale_cache_research.md`) —
this project's chosen upscaler, Real-ESRGAN-ncnn-vulkan, is built on ncnn's
own Vulkan inference engine. Vendored the same way this project vendors
every other real, precedented third-party dependency (MinHook, the
`MW32011DXVK` fork, NVIDIA Streamline's headers) rather than reinventing
GPU inference from scratch.

**Version**: `20260526` (ncnn's own release tag), pinned. Bump `NCNN_TAG`
in `build_ncnn.bat` to update.

**Built from source, not the official prebuilt SDK — direct decision,
2026-09-28.** The official prebuilt Windows SDK zip uses the DYNAMIC CRT
(`/MD`), which mismatches this project's own `proxy_d3d9.vcxproj`
(`/MT`, `RuntimeLibrary=MultiThreaded`) and fails to link with a real
`LNK2038` runtime-library-mismatch error — confirmed the hard way while
scoping this feature. Rather than switch the mod itself to `/MD` (a real,
new, player-visible MSVC-redistributable dependency `d3d9.dll` doesn't
have today) or ship ncnn as a separate DLL, the chosen fix is building
ncnn from its own real source with its own real `NCNN_BUILD_WITH_STATIC_CRT`
CMake option — keeps `d3d9.dll` fully self-contained. Run
`build_ncnn.bat` to (re)produce the libs; see that script's own header
comment for the full rationale and a real, hit-and-fixed Windows
path-length gotcha (build in a short path, not deep in a temp directory).

**License**: ncnn itself is BSD-3-Clause. The bundled Vulkan-shader-compile
support libraries (`glslang.lib`, `glslang-default-resource-limits.lib`,
`SPIRV.lib`, `GenericCodeGen.lib`, `MachineIndependent.lib`,
`OSDependent.lib`, all part of Khronos Group's `glslang` project) carry
their own real, separately-attributed, also-permissive (BSD/MIT-style)
licenses — verify the exact current terms directly from
[KhronosGroup/glslang](https://github.com/KhronosGroup/glslang) before any
public release attribution pass, same standing diligence this project
already applies to every other vendored dependency (see the main
`CLAUDE.md` §6 licensing section).

**Vulkan linking note**: ncnn implements its own in-house Vulkan function
loader rather than statically linking `vulkan-1.lib` at build time — it
loads the real system Vulkan loader dynamically at runtime instead, the
same general approach DXVK's own Vulkan loader uses. `vulkan-1.lib`
still needs to be present at LINK time for any consumer of ncnn (the
standalone smoke test that verified this build links it explicitly), but
ncnn's own build doesn't produce or embed a static dependency on it.

**Live-verified, 2026-09-28**: a standalone smoke test (not wired into the
mod — deliberately kept separate per this project's own "prototype
standalone before wiring in" convention) confirmed this exact `/MT` build
correctly detects real GPUs via Vulkan on the dev machine (an NVIDIA RTX
2080 Ti and an AMD Radeon integrated GPU, both enumerated with correct
capability flags) and successfully acquires a working `VulkanDevice`
handle — not just "did it link," genuine functional confirmation.

## What's committed vs. built locally

- `include/ncnn/` — real ncnn headers (~900KB), **committed**. Four of
  these (`layer_shader_type_enum.h`, `layer_type_enum.h`,
  `ncnn_export.h`, `platform.h`) are CMake-generated and embed real
  build-specific info (e.g. a version string) — `build_ncnn.bat`
  re-copies these from each fresh build so they stay consistent with
  whatever `lib/` currently contains.
- `lib/` — real static libraries (~80MB combined for x64, static-CRT
  build), **gitignored, never committed** (pure size concern — both
  ncnn's own BSD-3-Clause and the bundled glslang/SPIRV licenses are
  permissive; matches this project's own hard-learned lesson this
  session about committing large binaries). Run `build_ncnn.bat` to
  produce these locally before building anything that links against
  ncnn.

## Model weights

Not part of this vendoring at all — the actual Real-ESRGAN model weights
(BSD-3-Clause, verified separately in
`texture_upscale_cache_research.md`) are a distinct concern from the
inference engine itself and will be vendored separately once the
substitution/inference code that consumes them exists.
