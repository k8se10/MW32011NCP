#include "texture_upscale_ncnn.h"

#ifndef NOMINMAX
#define NOMINMAX // real MSVC C2589/C2059 fix: windows.h's own min/max macros
                 // collide with std::min/std::clamp used below -- must be
                 // defined before windows.h is included.
#endif
#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <algorithm>

#include "net.h"
#include "mat.h"

extern void LogFromController(const char* msg); // defined in dllmain.cpp

namespace TextureUpscaleNcnn
{
namespace
{
    SRWLOCK g_netLock = SRWLOCK_INIT; // model load + inference are not
        // documented as thread-safe for concurrent Extractor use on the same
        // ncnn::Net; this project's own background-thread design (one
        // upscale job at a time is the expected real usage) makes a single
        // lock sufficient, not a real throughput bottleneck. SRWLOCK, not
        // std::mutex, matching this project's own established convention
        // (every other lock in this codebase is SRWLOCK/CRITICAL_SECTION).

    // Minimal scope-exit RAII helper for SRWLOCK -- avoids needing a manual
    // Release call at every one of EnsureModelLoaded's own early-return
    // paths. Not std::lock_guard (this project's own non-STL shipped-code
    // convention), just a tiny local wrapper around the same real WinAPI
    // calls every other lock in this codebase already uses directly.
    struct ScopedSrwLock
    {
        SRWLOCK* lock;
        explicit ScopedSrwLock(SRWLOCK* l) : lock(l) { AcquireSRWLockExclusive(lock); }
        ~ScopedSrwLock() { ReleaseSRWLockExclusive(lock); }
        ScopedSrwLock(const ScopedSrwLock&) = delete;
        ScopedSrwLock& operator=(const ScopedSrwLock&) = delete;
    };

    // Real fix, 2026-09-28: a real live/standalone test crashed during
    // process exit, AFTER all real inference work already completed and was
    // verified correct -- a plain static ncnn::Net's own C++ destructor runs
    // during normal static-object teardown, which can race against ncnn's
    // own internal Vulkan global cleanup (also happening via its own static
    // destructors) in an unpredictable order -- a known class of issue for
    // programs mixing global C++ objects with Vulkan/GPU teardown, not a bug
    // in this project's own code. Fixed by deliberately leaking it: a
    // background-thread singleton that lives for the whole process/DLL
    // lifetime doesn't NEED clean teardown at exit anyway (the OS reclaims
    // GPU resources on process exit regardless), so never destructing it at
    // all sidesteps the ordering race entirely rather than trying to control
    // an ordering this project doesn't own (ncnn's own internal statics).
    ncnn::Net* g_net = nullptr;
    bool g_modelLoaded = false;

    // Real blob names, read directly from the vendored realesrgan-x4plus.param
    // file's own real Input/output layer declarations -- not guessed.
    constexpr const char* kInputBlobName = "data";
    constexpr const char* kOutputBlobName = "output";
    constexpr int kRealScaleFactor = 4; // this specific model's own real,
        // fixed upscale factor -- matches TextureRenderRes's own default
        // config value ("4x") by design, not a coincidence.

    // Real tiling constants (2026-09-28). kTileMaxDim is an input-space
    // (pre-upscale) size -- a 256x256 input tile produces a 1024x1024 4x
    // output tile, a safe, real-world-proven size for consumer GPU VRAM
    // budgets with this model class. kTileOverlap is the real per-edge
    // overlap (input space) used to hide convolution border artifacts at
    // tile seams -- standard technique, matching what real tiled-ESRGAN
    // tools (e.g. Real-ESRGAN-ncnn-vulkan's own --tilesize option) use.
    constexpr int kTileMaxDim = 256;
    constexpr int kTileOverlap = 16;

    // Runs the real Real-ESRGAN inference on ONE already-RGB, already-sized
    // tile. Caller owns the returned buffer (malloc'd, tileWidth*4 x
    // tileHeight*4 x 3 bytes RGB, no alpha -- alpha is handled once, at the
    // whole-image level, by the public UpscaleRGBA4x below, not per-tile).
    //
    // Safe to call concurrently from multiple worker threads (2026-09-29,
    // real parallelism added to drain a genuinely large real-world backlog
    // faster) -- this function already creates its own, fresh, purely
    // local `ncnn::Extractor` on every single call and never mutates
    // `g_net` itself (only ever reads it, after load) -- exactly ncnn's own
    // documented safe concurrency model ("Extractor is not thread-safe
    // itself, but multiple threads may each create and use their own
    // Extractor from the same already-loaded Net concurrently"). The only
    // real synchronization this feature ever needed was around the ONE-TIME
    // model load, which EnsureModelLoaded already handles with its own
    // internal lock -- see UpscaleRGBA4x's own comment for why the old
    // whole-call lock was removed.
    uint8_t* RunInferenceOnTileRgb(const uint8_t* rgb, int tileWidth, int tileHeight, int* outTileWidth, int* outTileHeight)
    {
        // Same real, already-proven ncnn::Mat::from_pixels API this file's
        // original single-shot implementation used (from_pixels makes its
        // own internal planar copy, so `rgb` is never mutated by this call) --
        // just PIXEL_RGB instead of PIXEL_RGBA2RGB, since `rgb` here is
        // already a tight 3-channel buffer (alpha was already split off by
        // the caller, UpscaleRGBA4x, before tiling).
        ncnn::Mat in = ncnn::Mat::from_pixels(rgb, ncnn::Mat::PIXEL_RGB, tileWidth, tileHeight);
        if (in.empty()) {
            LogFromController("[texture-upscale-ncnn] FAILED: from_pixels produced an empty Mat");
            return nullptr;
        }
        const float normVals[3] = { 1.0f / 255.0f, 1.0f / 255.0f, 1.0f / 255.0f };
        in.substract_mean_normalize(nullptr, normVals);

        ncnn::Extractor ex = g_net->create_extractor();
        if (ex.input(kInputBlobName, in) != 0) {
            LogFromController("[texture-upscale-ncnn] FAILED: Extractor::input failed");
            return nullptr;
        }

        ncnn::Mat out;
        if (ex.extract(kOutputBlobName, out) != 0) {
            LogFromController("[texture-upscale-ncnn] FAILED: Extractor::extract failed");
            return nullptr;
        }
        if (out.w <= 0 || out.h <= 0) {
            LogFromController("[texture-upscale-ncnn] FAILED: output Mat has zero dimensions");
            return nullptr;
        }

        const float denormVals[3] = { 255.0f, 255.0f, 255.0f };
        out.substract_mean_normalize(nullptr, denormVals);

        uint8_t* rgbOut = static_cast<uint8_t*>(malloc(static_cast<size_t>(out.w) * out.h * 3));
        if (!rgbOut) return nullptr;
        out.to_pixels(rgbOut, ncnn::Mat::PIXEL_RGB);

        *outTileWidth = out.w;
        *outTileHeight = out.h;
        return rgbOut;
    }

    // Plain bilinear resize of a single-channel (alpha) plane -- real,
    // standard image-scaling math, used because the vendored Real-ESRGAN
    // model has no alpha channel at all (RGB-only input/output). Good
    // enough for an alpha mask (which is rarely as detail-critical as the
    // color channels this GAN model specializes in), and far cheaper than
    // running a second full inference pass just for alpha.
    void BilinearResizePlane(const uint8_t* src, int srcW, int srcH, uint8_t* dst, int dstW, int dstH)
    {
        if (srcW <= 0 || srcH <= 0 || dstW <= 0 || dstH <= 0) return;
        for (int y = 0; y < dstH; ++y) {
            float srcYf = (static_cast<float>(y) + 0.5f) * (static_cast<float>(srcH) / dstH) - 0.5f;
            int y0 = static_cast<int>(std::floor(srcYf));
            float fy = srcYf - static_cast<float>(y0);
            int y0c = std::clamp(y0, 0, srcH - 1);
            int y1c = std::clamp(y0 + 1, 0, srcH - 1);
            for (int x = 0; x < dstW; ++x) {
                float srcXf = (static_cast<float>(x) + 0.5f) * (static_cast<float>(srcW) / dstW) - 0.5f;
                int x0 = static_cast<int>(std::floor(srcXf));
                float fx = srcXf - static_cast<float>(x0);
                int x0c = std::clamp(x0, 0, srcW - 1);
                int x1c = std::clamp(x0 + 1, 0, srcW - 1);

                float v00 = src[static_cast<size_t>(y0c) * srcW + x0c];
                float v01 = src[static_cast<size_t>(y0c) * srcW + x1c];
                float v10 = src[static_cast<size_t>(y1c) * srcW + x0c];
                float v11 = src[static_cast<size_t>(y1c) * srcW + x1c];
                float top = v00 + (v01 - v00) * fx;
                float bot = v10 + (v11 - v10) * fx;
                float val = top + (bot - top) * fy;
                dst[static_cast<size_t>(y) * dstW + x] = static_cast<uint8_t>(std::clamp(val, 0.0f, 255.0f));
            }
        }
    }
}

bool EnsureModelLoaded()
{
    ScopedSrwLock lock(&g_netLock);
    if (g_modelLoaded) return true;

    // Real model location, relative to the deployed d3d9.dll -- deployed
    // automatically by proxy_d3d9.vcxproj's DeployTextureUpscaleAssets
    // target (mirrors the DXVK binary's own DeployDxvk target).
    char exeDir[MAX_PATH];
    GetModuleFileNameA(nullptr, exeDir, MAX_PATH);
    char* lastSlash = strrchr(exeDir, '\\');
    if (lastSlash) *(lastSlash + 1) = '\0';

    char paramPath[MAX_PATH], modelPath[MAX_PATH];
    sprintf_s(paramPath, "%srealesrgan_models\\realesrgan-x4plus.param", exeDir);
    sprintf_s(modelPath, "%srealesrgan_models\\realesrgan-x4plus.bin", exeDir);

    g_net = new ncnn::Net(); // deliberately leaked, never delete'd -- see this
        // file's own header comment above g_net's declaration for the real
        // exit-crash reasoning this sidesteps.
    g_net->opt.use_vulkan_compute = true;

    if (g_net->load_param(paramPath) != 0) {
        char buf[400];
        sprintf_s(buf, "[texture-upscale-ncnn] FAILED to load param file '%.300s'", paramPath);
        LogFromController(buf);
        return false;
    }
    if (g_net->load_model(modelPath) != 0) {
        char buf[400];
        sprintf_s(buf, "[texture-upscale-ncnn] FAILED to load model file '%.300s'", modelPath);
        LogFromController(buf);
        return false;
    }

    g_modelLoaded = true;
    LogFromController("[texture-upscale-ncnn] Real-ESRGAN x4plus model loaded successfully.");
    return true;
}

uint8_t* UpscaleRGBA4x(const uint8_t* rgba, uint32_t width, uint32_t height, uint32_t* outWidth, uint32_t* outHeight)
{
    if (outWidth) *outWidth = 0;
    if (outHeight) *outHeight = 0;
    if (!rgba || width == 0 || height == 0) return nullptr;
    if (!EnsureModelLoaded()) return nullptr;

    // Real parallelism (2026-09-29, direct instruction: "we need
    // parralellism for sure") -- the lock that used to wrap this entire
    // function's body is REMOVED here. It was never actually protecting
    // anything past model load: `g_net` is read-only from this point on
    // (RunInferenceOnTileRgb's own comment explains why concurrent
    // Extractor use from multiple threads on one already-loaded Net is
    // ncnn's own documented-safe pattern), and every buffer below
    // (srcRgb/srcAlpha/rgbaOut/etc.) is local to this call, never shared
    // across threads. EnsureModelLoaded above still has its own internal
    // lock guaranteeing the one-time load itself stays race-free.
    const int w = static_cast<int>(width), h = static_cast<int>(height);
    const int outW = w * kRealScaleFactor, outH = h * kRealScaleFactor;

    // Real preprocessing shared by both paths below: split the input into a
    // planar RGB buffer (for the model) and a separate alpha plane (handled
    // by BilinearResizePlane, never fed to the network -- see this file's
    // own header comment on why).
    uint8_t* srcRgb = static_cast<uint8_t*>(malloc(static_cast<size_t>(w) * h * 3));
    uint8_t* srcAlpha = static_cast<uint8_t*>(malloc(static_cast<size_t>(w) * h));
    if (!srcRgb || !srcAlpha) { free(srcRgb); free(srcAlpha); return nullptr; }
    for (int i = 0; i < w * h; ++i) {
        srcRgb[i * 3 + 0] = rgba[i * 4 + 0];
        srcRgb[i * 3 + 1] = rgba[i * 4 + 1];
        srcRgb[i * 3 + 2] = rgba[i * 4 + 2];
        srcAlpha[i] = rgba[i * 4 + 3];
    }

    uint8_t* rgbaOut = static_cast<uint8_t*>(malloc(static_cast<size_t>(outW) * outH * 4));
    if (!rgbaOut) { free(srcRgb); free(srcAlpha); return nullptr; }

    bool ok = true;
    if (w <= kTileMaxDim && h <= kTileMaxDim) {
        // Fast path -- the common case (most real UI/HUD textures are far
        // smaller than kTileMaxDim). No tiling overhead at all.
        int realOutW = 0, realOutH = 0;
        uint8_t* rgbResult = RunInferenceOnTileRgb(srcRgb, w, h, &realOutW, &realOutH);
        if (!rgbResult || realOutW != outW || realOutH != outH) {
            // A real mismatch here (model produced a different scale factor
            // than expected) is a real, logged failure, not silently patched
            // over -- this project's own "verify live, don't assume"
            // standard applies to this feature's own math too.
            if (rgbResult) {
                char buf[200];
                sprintf_s(buf, "[texture-upscale-ncnn] FAILED: expected %dx%d output, got %dx%d",
                    outW, outH, realOutW, realOutH);
                LogFromController(buf);
                free(rgbResult);
            }
            ok = false;
        } else {
            for (int i = 0; i < outW * outH; ++i) {
                rgbaOut[i * 4 + 0] = rgbResult[i * 3 + 0];
                rgbaOut[i * 4 + 1] = rgbResult[i * 3 + 1];
                rgbaOut[i * 4 + 2] = rgbResult[i * 3 + 2];
            }
            free(rgbResult);
        }
    } else {
        // Real tiling path (2026-09-28) -- see this file's own header
        // comment above UpscaleRGBA4x's declaration for the rationale.
        // Tiles are laid out on a simple grid; each tile is expanded by
        // kTileOverlap pixels on every internal edge (clamped at the real
        // image boundary, where there's no neighboring tile to blend
        // against and the full edge is kept), inferred independently, then
        // the overlap border is cropped back off (scaled by kRealScaleFactor)
        // before the tile's own "core" region is pasted into the final
        // output buffer at its correct location.
        for (int tileY = 0; tileY < h && ok; tileY += kTileMaxDim) {
            for (int tileX = 0; tileX < w && ok; tileX += kTileMaxDim) {
                int coreW = std::min(kTileMaxDim, w - tileX);
                int coreH = std::min(kTileMaxDim, h - tileY);

                int padX0 = std::min(kTileOverlap, tileX);
                int padY0 = std::min(kTileOverlap, tileY);
                int padX1 = std::min(kTileOverlap, w - (tileX + coreW));
                int padY1 = std::min(kTileOverlap, h - (tileY + coreH));

                int srcTileX = tileX - padX0, srcTileY = tileY - padY0;
                int srcTileW = coreW + padX0 + padX1, srcTileH = coreH + padY0 + padY1;

                uint8_t* tileBuf = static_cast<uint8_t*>(malloc(static_cast<size_t>(srcTileW) * srcTileH * 3));
                if (!tileBuf) { ok = false; break; }
                for (int ty = 0; ty < srcTileH; ++ty) {
                    memcpy(tileBuf + static_cast<size_t>(ty) * srcTileW * 3,
                           srcRgb + (static_cast<size_t>(srcTileY + ty) * w + srcTileX) * 3,
                           static_cast<size_t>(srcTileW) * 3);
                }

                int tileOutW = 0, tileOutH = 0;
                uint8_t* tileResult = RunInferenceOnTileRgb(tileBuf, srcTileW, srcTileH, &tileOutW, &tileOutH);
                free(tileBuf);
                if (!tileResult || tileOutW != srcTileW * kRealScaleFactor || tileOutH != srcTileH * kRealScaleFactor) {
                    char buf[200];
                    sprintf_s(buf, "[texture-upscale-ncnn] FAILED: tile at (%d,%d) produced unexpected output size", tileX, tileY);
                    LogFromController(buf);
                    free(tileResult);
                    ok = false;
                    break;
                }

                // Crop the overlap border off (scaled) and paste the core
                // region into the final output buffer.
                int cropX0 = padX0 * kRealScaleFactor, cropY0 = padY0 * kRealScaleFactor;
                int coreOutW = coreW * kRealScaleFactor, coreOutH = coreH * kRealScaleFactor;
                int dstX0 = tileX * kRealScaleFactor, dstY0 = tileY * kRealScaleFactor;
                for (int ty = 0; ty < coreOutH; ++ty) {
                    const uint8_t* srcRow = tileResult + (static_cast<size_t>(cropY0 + ty) * tileOutW + cropX0) * 3;
                    for (int tx = 0; tx < coreOutW; ++tx) {
                        size_t dstIdx = (static_cast<size_t>(dstY0 + ty) * outW + (dstX0 + tx)) * 4;
                        rgbaOut[dstIdx + 0] = srcRow[tx * 3 + 0];
                        rgbaOut[dstIdx + 1] = srcRow[tx * 3 + 1];
                        rgbaOut[dstIdx + 2] = srcRow[tx * 3 + 2];
                    }
                }
                free(tileResult);
            }
        }
    }

    free(srcRgb);

    if (!ok) {
        free(srcAlpha);
        free(rgbaOut);
        return nullptr;
    }

    // Real alpha compositing (2026-09-28) -- see this file's own header
    // comment on why this is a separate bilinear pass rather than model
    // output.
    uint8_t* dstAlpha = static_cast<uint8_t*>(malloc(static_cast<size_t>(outW) * outH));
    if (dstAlpha) {
        BilinearResizePlane(srcAlpha, w, h, dstAlpha, outW, outH);
        for (int i = 0; i < outW * outH; ++i) rgbaOut[i * 4 + 3] = dstAlpha[i];
        free(dstAlpha);
    } else {
        for (int i = 0; i < outW * outH; ++i) rgbaOut[i * 4 + 3] = 255; // degrade
            // gracefully on real OOM -- opaque fallback, matching the old
            // unconditional behavior, rather than leaving alpha
            // uninitialized garbage.
    }
    // Real, critical leak fix (2026-09-29): srcAlpha was allocated at the top
    // of this function and used above, but was only ever freed on the early
    // (!ok) failure path -- every real SUCCESSFUL call leaked the entire
    // source alpha plane (width*height bytes, several MB for large real
    // world textures). Live-reported: privateBytesMB climbed from 263MB to
    // over 10GB in one real session, eventually surfacing as the native
    // engine's own "Needed to allocate at least 32.0 MB to load images"
    // error on a Dome level load -- this is the real root cause.
    free(srcAlpha);

    if (outWidth) *outWidth = static_cast<uint32_t>(outW);
    if (outHeight) *outHeight = static_cast<uint32_t>(outH);
    return rgbaOut;
}

}
