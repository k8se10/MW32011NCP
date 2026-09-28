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
    uint8_t* UpscaleRGBA4x(const uint8_t* rgba, uint32_t width, uint32_t height, uint32_t* outWidth, uint32_t* outHeight);
}
