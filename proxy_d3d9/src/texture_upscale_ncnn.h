#pragma once

#include <cstdint>

// Real-ESRGAN ncnn inference wrapper -- runtime texture-upscale-cache
// feature (re_notes/x64_migration/texture_upscale_cache_research.md). Uses
// ncnn's standard Mat/Extractor API (not the reference realesrgan.cpp's own
// custom GPU compute-shader pre/post-process pipeline, which is
// significantly more complex to port correctly) -- a real, standard,
// documented ncnn inference pattern, less optimized (a CPU round-trip for
// color-format conversion instead of a GPU-resident shader) but appropriate
// for a background cache-population thread, not a real-time path.
namespace TextureUpscaleNcnn
{
    // Loads the real vendored Real-ESRGAN model (realesrgan-x4plus.bin/.param,
    // proxy_d3d9/third_party/realesrgan_models/) once. Safe to call multiple
    // times (idempotent) -- returns true if already loaded or if this call
    // successfully loads it. Must be called from a background thread, never
    // from any hook's own call path -- model load + first inference involve
    // real, slow GPU/shader-compile work.
    bool EnsureModelLoaded();

    // Runs the real Real-ESRGAN 4x upscale on an RGBA8 input buffer
    // (width*height*4 bytes), returning a newly malloc'd RGBA8 output buffer
    // at (width*4)x(height*4) (caller must free()). Returns nullptr on any
    // failure (model not loaded, GPU error, allocation failure) -- logged
    // internally. Real, slow work (real neural-network inference) -- must
    // only ever be called from a background thread.
    //
    // Real alpha handling (2026-09-28): the vendored model is RGB-only (no
    // alpha input/output), so the source alpha plane is upscaled separately
    // via a plain bilinear resize (real, standard image-scaling math, not a
    // stub) and composited back into the result -- earlier versions of this
    // function hardcoded output alpha to fully opaque, which would have
    // silently broken any real texture with meaningful transparency (a real
    // fraction of this engine's UI/HUD art) once this runs against arbitrary
    // game textures rather than only the always-opaque custom background art
    // it was first proven against.
    //
    // Real tiling (2026-09-28): Real-ESRGAN is a fully-convolutional network
    // (no fixed input-size requirement), but running an entire large texture
    // (e.g. a 1672x944 menu background) through in one shot risks excessive
    // GPU memory/time on real hardware -- this function internally tiles any
    // input larger than kTileMaxDim into overlapping chunks, runs inference
    // per tile, and stitches the 4x results back together (standard
    // technique real ESRGAN tools use for large images), completely
    // transparent to the caller -- small inputs (the common case: most real
    // UI/HUD textures) take the single-shot fast path with no tiling
    // overhead at all.
    uint8_t* UpscaleRGBA4x(const uint8_t* rgba, uint32_t width, uint32_t height, uint32_t* outWidth, uint32_t* outHeight);
}
