#pragma once

// Embeds the same bundled texture-name basemap the live mod itself embeds
// (proxy_d3d9/resource.h's own IDR_TEXTURE_BASEMAP, proxy_d3d9/bundled_data/
// texture_names_basemap.txt) -- a separate, independent embed (its own
// resource ID space, its own .rc entry) rather than a shared/reused one,
// matching this project's own "never link against or depend on
// proxy_d3d9-specific build artifacts" convention for standalone tools. Used
// here purely for REAL COVERAGE PROGRESS reporting (how many of the ~17,000
// known real textures already have a cache entry) -- this tool never forces
// anything to load, it only reports against this list.
#define IDR_TEXTURE_BASEMAP 1

// Real-ESRGAN model weights, embedded directly into this exe (2026-10-01,
// "zero manual setup" -- see main.cpp's own header comment) so the whole
// tool is a single file: this exe gets embedded into d3d9.dll in turn and
// extracted into the game's own install directory automatically, so it
// needs no separate realesrgan_models\ folder shipped alongside it for
// that to work. Self-extracted to realesrgan_models\ next to this exe's
// own real path on first run (EnsureModelsExtracted in main.cpp) --
// TextureUpscaleNcnn::EnsureModelLoaded still reads from that real file
// path unmodified, so nothing in the shared pipeline code needed to change.
// Gated the same way as proxy_d3d9's own DXVK/NVIDIA embeds
// (MW3NCP_EMBED_REALESRGAN_MODEL, TextureCacheBuilder.vcxproj) -- only
// defined when the real, gitignored model files are actually present
// locally, so a fresh clone/CI build without them vendored yet simply
// skips embedding instead of failing.
#define IDR_REALESRGAN_BIN   2
#define IDR_REALESRGAN_PARAM 3

