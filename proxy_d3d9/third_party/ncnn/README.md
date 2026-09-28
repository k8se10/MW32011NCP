# ncnn (vendored)

Real, official, prebuilt [Tencent/ncnn](https://github.com/Tencent/ncnn)
Windows VS2022 x64 static SDK, Vulkan-enabled — a high-performance neural
network inference framework with a native Vulkan compute backend, no
CUDA/DirectML/ONNX Runtime dependency.

**Why this is here**: groundwork for the runtime AI texture-upscale-cache
feature (`re_notes/x64_migration/texture_upscale_cache_research.md`) —
this project's chosen upscaler, Real-ESRGAN-ncnn-vulkan, is built on ncnn's
own Vulkan inference engine. Vendored the same way this project vendors
every other real, precedented third-party dependency (MinHook, the
`MW32011DXVK` fork, NVIDIA Streamline's headers) rather than reinventing
GPU inference from scratch.

**Version**: `20260526` (ncnn's own release tag). Pinned, not "latest" —
see `fetch_ncnn.bat`'s own header comment to update.

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

## What's committed vs. fetched

- `include/ncnn/` — real ncnn headers, small (~900KB), **committed**.
- `lib/`, `bin/` — real prebuilt static libraries (~46MB combined for x64),
  **gitignored, never committed** (size, matching this project's own
  hard-learned lesson about large binaries this session — see
  `.gitignore`'s own comment for the specific incidents that taught it).
  Run `fetch_ncnn.bat` to populate these locally before building anything
  that links against ncnn.

## Model weights

Not part of this vendoring at all — the actual Real-ESRGAN model weights
(BSD-3-Clause, verified separately in
`texture_upscale_cache_research.md`) are a distinct concern from the
inference engine itself and will be vendored separately once the
substitution/inference code that consumes them exists.
