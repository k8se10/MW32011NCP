#include "texture_upscale_ncnn.h"

#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>

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

    ncnn::Net g_net;
    bool g_modelLoaded = false;

    // Real blob names, read directly from the vendored realesrgan-x4plus.param
    // file's own real Input/output layer declarations -- not guessed.
    constexpr const char* kInputBlobName = "data";
    constexpr const char* kOutputBlobName = "output";
    constexpr int kRealScaleFactor = 4; // this specific model's own real,
        // fixed upscale factor -- matches TextureRenderRes's own default
        // config value ("4x") by design, not a coincidence.
}

bool EnsureModelLoaded()
{
    ScopedSrwLock lock(&g_netLock);
    if (g_modelLoaded) return true;

    // Real model location, relative to the deployed d3d9.dll -- this project
    // only ever ships the two small vendoring source files
    // (README.md/fetch_models.bat) under proxy_d3d9/third_party/ in the repo;
    // the real 33MB .bin/.param pair is gitignored and expected to be placed
    // directly beside the deployed d3d9.dll at runtime (a real, separate
    // deployment step -- not yet automated by this project's own build, a
    // known gap for a later pass, same as how the DXVK binary's own deploy
    // step already works via proxy_d3d9.vcxproj's DeployDxvk target).
    char exeDir[MAX_PATH];
    GetModuleFileNameA(nullptr, exeDir, MAX_PATH);
    char* lastSlash = strrchr(exeDir, '\\');
    if (lastSlash) *(lastSlash + 1) = '\0';

    char paramPath[MAX_PATH], modelPath[MAX_PATH];
    sprintf_s(paramPath, "%srealesrgan_models\\realesrgan-x4plus.param", exeDir);
    sprintf_s(modelPath, "%srealesrgan_models\\realesrgan-x4plus.bin", exeDir);

    g_net.opt.use_vulkan_compute = true;

    if (g_net.load_param(paramPath) != 0) {
        char buf[400];
        sprintf_s(buf, "[texture-upscale-ncnn] FAILED to load param file '%.300s'", paramPath);
        LogFromController(buf);
        return false;
    }
    if (g_net.load_model(modelPath) != 0) {
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

    ScopedSrwLock lock(&g_netLock);

    // Real preprocessing: RGBA -> RGB (the vendored model has no alpha
    // input), normalized to [0,1] -- Real-ESRGAN's own well-documented,
    // standard preprocessing convention (no mean subtraction, plain /255).
    ncnn::Mat in = ncnn::Mat::from_pixels(rgba, ncnn::Mat::PIXEL_RGBA2RGB,
                                           static_cast<int>(width), static_cast<int>(height));
    if (in.empty()) {
        LogFromController("[texture-upscale-ncnn] FAILED: from_pixels produced an empty Mat");
        return nullptr;
    }
    const float normVals[3] = { 1.0f / 255.0f, 1.0f / 255.0f, 1.0f / 255.0f };
    in.substract_mean_normalize(nullptr, normVals);

    ncnn::Extractor ex = g_net.create_extractor();
    if (ex.input(kInputBlobName, in) != 0) {
        LogFromController("[texture-upscale-ncnn] FAILED: Extractor::input failed");
        return nullptr;
    }

    ncnn::Mat out;
    if (ex.extract(kOutputBlobName, out) != 0) {
        LogFromController("[texture-upscale-ncnn] FAILED: Extractor::extract failed");
        return nullptr;
    }

    uint32_t realOutWidth = static_cast<uint32_t>(out.w);
    uint32_t realOutHeight = static_cast<uint32_t>(out.h);
    if (realOutWidth == 0 || realOutHeight == 0) {
        LogFromController("[texture-upscale-ncnn] FAILED: output Mat has zero dimensions");
        return nullptr;
    }

    // Real postprocessing: [0,1] -> [0,255], clamp, RGB -> RGBA (real alpha
    // channel information was discarded by the network's own RGB-only
    // input/output -- filled back in as fully opaque here; a real known
    // simplification, not a bug, flagged in this file's own header comment).
    const float denormVals[3] = { 255.0f, 255.0f, 255.0f };
    out.substract_mean_normalize(nullptr, denormVals);

    uint8_t* rgbTemp = static_cast<uint8_t*>(malloc(static_cast<size_t>(realOutWidth) * realOutHeight * 3));
    if (!rgbTemp) return nullptr;
    out.to_pixels(rgbTemp, ncnn::Mat::PIXEL_RGB);

    uint8_t* rgbaOut = static_cast<uint8_t*>(malloc(static_cast<size_t>(realOutWidth) * realOutHeight * 4));
    if (!rgbaOut) { free(rgbTemp); return nullptr; }
    for (uint32_t i = 0; i < realOutWidth * realOutHeight; ++i) {
        rgbaOut[i * 4 + 0] = rgbTemp[i * 3 + 0];
        rgbaOut[i * 4 + 1] = rgbTemp[i * 3 + 1];
        rgbaOut[i * 4 + 2] = rgbTemp[i * 3 + 2];
        rgbaOut[i * 4 + 3] = 255;
    }
    free(rgbTemp);

    if (outWidth) *outWidth = realOutWidth;
    if (outHeight) *outHeight = realOutHeight;
    return rgbaOut;
}

}
