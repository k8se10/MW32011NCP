# Real-ESRGAN ncnn models (vendored, fetched separately)

Real, official `realesrgan-x4plus.bin`/`.param` — the standard, real
Real-ESRGAN 4x upscale model in ncnn's own native inference format,
matching this project's `TextureRenderRes=4x` texture-upscale-cache
feature exactly.

**Source**: [xinntao/Real-ESRGAN](https://github.com/xinntao/Real-ESRGAN)'s
own `v0.2.5.0` release (2022-04-24) — confirmed to bundle real, separate
`.param`/`.bin` files (unlike the `xinntao/Real-ESRGAN-ncnn-vulkan` fork's
own releases, which embed the model directly inside the CLI `.exe` and
don't ship it as separate files usable by another program).

**License**: BSD-3-Clause (verified 2026-09-28, see
`re_notes/x64_migration/texture_upscale_cache_research.md`).

**Gitignored, not committed** (33MB) — pure size, matching this project's
own established treatment for every other large vendored binary this
session (ncnn's own static libs, the NVIDIA Streamline binaries). Run
`fetch_models.bat` to download it locally before building anything that
uses the upscale pipeline.
