// TextureCacheBuilder -- standalone, offline, out-of-process texture-upscale
// cache generator (2026-10-01). Direct request, following a real root-cause
// finding: this session traced a live "Exceeded limit of 4448 'image' assets"
// failure (and a cascade of related crashes) to this project's own
// proactive, in-game, FindOrLoadAsset-based texture-fetch pump -- it forces
// real, PERMANENT native image-pool registrations for every name in the
// bundled basemap regardless of what the player is actually near, with no
// safe way to individually release them (the native engine only supports
// bulk, per-zone release, not per-asset). Direct conclusion: "im thinking a
// prerequisite etxernal app for esgran caching" -- do the heavy lifting
// (Real-ESRGAN upscaling, DXT re-encoding, compression, cache-file writing)
// in a completely separate, standalone process that never touches
// iw5sp.exe's own memory, threads, or native asset pool at all.
//
// REVISED SAME DAY -- real, direct correction to this tool's first design:
// the original version extracted textures itself by running the bundled
// Unlinker.exe (tools/iw5oat's own OpenAssetTools fork) against every real
// zone file. Direct correction: "oat fails on decompress tho that was the
// whole issue, we want to leverage the games actual pipeline" -- Unlinker
// has a long, already-documented history in this project of failing to
// parse many real zone files (re_notes/x64_migration/fastfile_format_
// research.md's many rounds chasing exactly this), so an extraction path
// built on it inherits the same incomplete coverage the in-game bulk
// precache orchestrator (TexturePrecacheOrchestrator) already has, for no
// real benefit. Final, corrected architecture, per direct instruction ("id
// rather us use the existing known asset list as base alone with
// caputring also unknown assets in gameplay"):
//   - The live mod's own SAFE organic capture paths (Hook_ImageFileLoadX64's
//     load-time capture, and Hook_SetTexture's viewport capture) are the
//     ONLY real asset-acquisition mechanism now -- they use the game's own
//     real, always-correct native asset loader, naturally capture BOTH
//     already-known and previously-unknown texture names as the player
//     actually encounters them, and were never the source of the pool-
//     exhaustion crash (only the proactive FORCING pump was). As of this
//     revision they write raw, un-upscaled bytes to a plain staging folder
//     (<gameDir>\texture_capture_staging\, see texture_capture_staging.h)
//     instead of handing off to a live in-process upscale worker.
//   - This tool's only job is to drain that staging folder: decode each
//     staged raw capture, run it through the same real pipeline as before,
//     and delete the staged file once cached. No Unlinker, no zone-file
//     parsing, no dependency on which zones currently happen to load
//     cleanly.
//   - The bundled texture-name basemap (~17,000 real names, embedded via
//     resource.h's own IDR_TEXTURE_BASEMAP, independent of proxy_d3d9's own
//     embed) is used ONLY for real coverage-progress reporting ("X/17008
//     known textures cached") -- never to force anything to load, matching
//     the exact distinction the live mod's own ProactiveLevelTexturePreload
//     feature already draws between "drives progress tracking" and "forces
//     native pool registration."
//
// Architecture: reuses the exact same proven pipeline modules the live mod
// already ships (texture_upscale_ncnn.cpp, texture_upscale_dxt_codec.cpp,
// texture_upscale_iwi_writer.cpp, texture_upscale_compress.cpp,
// texture_upscale_cache.cpp), compiled as-is with zero source
// modifications -- their only external dependency across all five files is
// a single `LogFromController` function, implemented fresh here. Writes
// into the SAME <gameDir>\texture_upscale_cache\ directory the live mod
// already reads from, so the in-game substitution hooks need zero changes
// at all -- only the GENERATION side moves out-of-process.
//
// Deployment convention: this exe is meant to be dropped into the game's
// own install directory (same place d3d9.dll lives), using the same
// GetModuleFileNameA-relative-path convention every other part of this
// project already follows. Run whenever convenient -- after a play session
// that captured new textures, or between sessions -- not injected into the
// game, not triggered by it, and no longer a "run once before playing"
// prerequisite (organic capture populates the staging folder DURING real
// play; this tool just processes whatever has accumulated there so far).

#include <windows.h>
#include <commctrl.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>

#include "../../proxy_d3d9/src/texture_upscale_ncnn.h"
#include "../../proxy_d3d9/src/texture_upscale_dxt_codec.h"
#include "../../proxy_d3d9/src/texture_upscale_iwi_writer.h"
#include "../../proxy_d3d9/src/texture_upscale_cache.h"
#include "resource.h"

#pragma comment(lib, "comctl32.lib")

// ---------------------------------------------------------------------------
// LogFromController -- the one real external dependency shared by the five
// reused pipeline modules. Writes to a real log file AND posts to the GUI's
// own log box (ownership of the heap copy transfers to the window procedure,
// which frees it after appending).
// ---------------------------------------------------------------------------
namespace
{
    HWND g_mainWnd = nullptr;
    CRITICAL_SECTION g_logLock;
    FILE* g_logFile = nullptr;
    constexpr UINT WM_APP_LOG = WM_APP + 1;
    constexpr UINT WM_APP_PROGRESS = WM_APP + 2;
    constexpr UINT WM_APP_DONE = WM_APP + 3;
}

void LogFromController(const char* msg)
{
    EnterCriticalSection(&g_logLock);
    if (!g_logFile) fopen_s(&g_logFile, "texture_cache_builder.log", "a");
    if (g_logFile) {
        fprintf(g_logFile, "%s\n", msg);
        fflush(g_logFile);
    }
    LeaveCriticalSection(&g_logLock);

    if (g_mainWnd) {
        char* copy = _strdup(msg);
        if (copy) PostMessageA(g_mainWnd, WM_APP_LOG, 0, reinterpret_cast<LPARAM>(copy));
    }
}

namespace
{
    // ---- Format helpers (ported from texture_upscale_worker.cpp, same real
    // logic, zero behavioral change -- see that file's own header comments
    // for the full rationale on each). ----
    bool IwiFormatToBlockFormat(int iwiFormat, TextureUpscaleDxt::BlockFormat* out)
    {
        switch (static_cast<TextureUpscaleIwi::Format>(iwiFormat)) {
            case TextureUpscaleIwi::Format::DXT1: *out = TextureUpscaleDxt::BlockFormat::BC1; return true;
            case TextureUpscaleIwi::Format::DXT3: *out = TextureUpscaleDxt::BlockFormat::BC2; return true;
            case TextureUpscaleIwi::Format::DXT5: *out = TextureUpscaleDxt::BlockFormat::BC3; return true;
            default: return false;
        }
    }

    bool RawBitmapBytesPerPixel(int iwiFormat, int* outBpp)
    {
        switch (static_cast<TextureUpscaleIwi::Format>(iwiFormat)) {
            case TextureUpscaleIwi::Format::BitmapRGBA: *outBpp = 4; return true;
            case TextureUpscaleIwi::Format::BitmapRGB: *outBpp = 3; return true;
            default: return false;
        }
    }

    volatile LONG g_cancelRequested = 0;
    volatile LONG g_totalQueued = 0;
    volatile LONG g_totalProcessed = 0;
    volatile LONG g_totalCached = 0;
    volatile LONG g_totalSkippedExisting = 0;
    volatile LONG g_totalFailed = 0;

    void PostProgress()
    {
        if (g_mainWnd) PostMessageA(g_mainWnd, WM_APP_PROGRESS, 0, 0);
    }

    // Real IWI-v8 header parse + real source mip-count detection, same exact
    // algorithm as TextureUpscaleWorker::QueueUpscaleJobFromIwiFile (ported,
    // not reinvented -- see that function's own header comment in
    // texture_upscale_worker.cpp for the full "why match the source's real
    // count" rationale: shipping more or fewer mip levels than the source
    // texture actually has causes real native engine crashes/corruption,
    // live-confirmed this same session).
    bool ParseIwiHeader(const uint8_t* data, uint32_t size, int* outFormat,
                         uint16_t* outWidth, uint16_t* outHeight, int* outSourceMipCount,
                         const uint8_t** outBaseMipData, uint32_t* outBaseMipSize)
    {
        constexpr uint32_t kIwiHeaderSize = 0x20;
        if (!data || size <= kIwiHeaderSize) return false;
        if (data[0] != 'I' || data[1] != 'W' || data[2] != 'i' || data[3] != 8) return false;

        int rawFormat = static_cast<int8_t>(data[0x08]);
        uint16_t width = *reinterpret_cast<const uint16_t*>(data + 0x0A);
        uint16_t height = *reinterpret_cast<const uint16_t*>(data + 0x0C);
        if (width == 0 || height == 0) return false;

        TextureUpscaleDxt::BlockFormat blockFmt;
        int rawBpp = 0;
        uint32_t baseMipSize = 0;
        if (IwiFormatToBlockFormat(rawFormat, &blockFmt)) {
            baseMipSize = TextureUpscaleDxt::CompressedSize(width, height, blockFmt);
        } else if (RawBitmapBytesPerPixel(rawFormat, &rawBpp)) {
            baseMipSize = static_cast<uint32_t>(width) * height * static_cast<uint32_t>(rawBpp);
        } else {
            return false; // genuinely unidentified format, out of scope
        }
        if (size < kIwiHeaderSize + baseMipSize) return false;

        int sourceMipCount = 0;
        {
            const uint32_t realPayloadSize = size - kIwiHeaderSize;
            uint32_t running = 0;
            uint32_t lw = width, lh = height;
            for (int i = 0; i < 64; ++i) {
                uint32_t levelSize;
                if (IwiFormatToBlockFormat(rawFormat, &blockFmt)) {
                    levelSize = TextureUpscaleDxt::CompressedSize(lw, lh, blockFmt);
                } else {
                    levelSize = lw * lh * static_cast<uint32_t>(rawBpp);
                }
                if (running + levelSize > realPayloadSize) break;
                running += levelSize;
                ++sourceMipCount;
                if (running == realPayloadSize) break;
                if (lw <= 1 && lh <= 1) break;
                lw = lw > 1 ? lw / 2 : 1;
                lh = lh > 1 ? lh / 2 : 1;
            }
        }

        *outFormat = rawFormat;
        *outWidth = width;
        *outHeight = height;
        *outSourceMipCount = sourceMipCount;
        *outBaseMipData = data + (size - baseMipSize);
        *outBaseMipSize = baseMipSize;
        return true;
    }

    // Processes one real texture end to end: decode -> Real-ESRGAN upscale ->
    // box-filter to the target scale -> generate a real mip chain matching
    // the SOURCE's own real mip count -> DXT5 re-encode each level ->
    // EncodeIwi8 -> compress -> store. Same real pipeline as
    // TextureUpscaleWorker::ProcessJob (ported, not reinvented), minus the
    // live-session-specific concerns (no memory-pressure gate needed -- this
    // process owns the whole machine while it runs; no queue/thread-pool
    // plumbing -- the caller already parallelizes across files).
    bool ProcessOneTexture(const char* assetName, int rawFormat, uint16_t width, uint16_t height,
                            int sourceMipCount, const uint8_t* baseMipData, uint32_t baseMipSize,
                            int scaleMultiplier)
    {
        TextureUpscaleDxt::BlockFormat decodeFmt;
        int rawBpp = 0;
        uint8_t* rgba = nullptr;
        if (IwiFormatToBlockFormat(rawFormat, &decodeFmt)) {
            rgba = TextureUpscaleDxt::DecodeToRGBA(baseMipData, width, height, decodeFmt);
        } else if (RawBitmapBytesPerPixel(rawFormat, &rawBpp)) {
            uint64_t pixelCount = static_cast<uint64_t>(width) * height;
            rgba = static_cast<uint8_t*>(malloc(pixelCount * 4));
            if (rgba) {
                if (rawBpp == 4) {
                    for (uint64_t i = 0; i < pixelCount; ++i) {
                        rgba[i * 4 + 0] = baseMipData[i * 4 + 2]; // BGRA -> RGBA
                        rgba[i * 4 + 1] = baseMipData[i * 4 + 1];
                        rgba[i * 4 + 2] = baseMipData[i * 4 + 0];
                        rgba[i * 4 + 3] = baseMipData[i * 4 + 3];
                    }
                } else { // rawBpp == 3
                    for (uint64_t i = 0; i < pixelCount; ++i) {
                        rgba[i * 4 + 0] = baseMipData[i * 3 + 2];
                        rgba[i * 4 + 1] = baseMipData[i * 3 + 1];
                        rgba[i * 4 + 2] = baseMipData[i * 3 + 0];
                        rgba[i * 4 + 3] = 255;
                    }
                }
            }
        }
        if (!rgba) {
            char buf[300];
            sprintf_s(buf, "[builder] '%.200s': decode failed", assetName);
            LogFromController(buf);
            return false;
        }

        uint32_t netOutW = 0, netOutH = 0;
        uint8_t* netOut = TextureUpscaleNcnn::UpscaleRGBA4x(rgba, width, height, &netOutW, &netOutH);
        free(rgba);
        if (!netOut) {
            char buf[300];
            sprintf_s(buf, "[builder] '%.200s': UpscaleRGBA4x failed", assetName);
            LogFromController(buf);
            return false;
        }

        uint32_t finalW = static_cast<uint32_t>(width) * static_cast<uint32_t>(scaleMultiplier);
        uint32_t finalH = static_cast<uint32_t>(height) * static_cast<uint32_t>(scaleMultiplier);
        uint8_t* finalRgba = netOut;
        bool freeFinalRgba = false;
        if (scaleMultiplier != 4) {
            uint32_t boxX = netOutW / finalW, boxY = netOutH / finalH;
            uint8_t* boxed = static_cast<uint8_t*>(malloc(static_cast<size_t>(finalW) * finalH * 4));
            if (boxed) {
                for (uint32_t y = 0; y < finalH; ++y) {
                    for (uint32_t x = 0; x < finalW; ++x) {
                        uint32_t sums[4] = {0, 0, 0, 0};
                        uint32_t sampleCount = boxX * boxY;
                        for (uint32_t by = 0; by < boxY; ++by) {
                            for (uint32_t bx = 0; bx < boxX; ++bx) {
                                uint32_t srcX = x * boxX + bx, srcY = y * boxY + by;
                                const uint8_t* p = netOut + (static_cast<size_t>(srcY) * netOutW + srcX) * 4;
                                sums[0] += p[0]; sums[1] += p[1]; sums[2] += p[2]; sums[3] += p[3];
                            }
                        }
                        uint8_t* dst = boxed + (static_cast<size_t>(y) * finalW + x) * 4;
                        dst[0] = static_cast<uint8_t>(sums[0] / sampleCount);
                        dst[1] = static_cast<uint8_t>(sums[1] / sampleCount);
                        dst[2] = static_cast<uint8_t>(sums[2] / sampleCount);
                        dst[3] = static_cast<uint8_t>(sums[3] / sampleCount);
                    }
                }
                finalRgba = boxed;
                freeFinalRgba = true;
            }
        }
        const uint32_t actualW = (freeFinalRgba || scaleMultiplier == 4) ? finalW : netOutW;
        const uint32_t actualH = (freeFinalRgba || scaleMultiplier == 4) ? finalH : netOutH;

        // Real mip chain matching the SOURCE's own real mip count -- see
        // ParseIwiHeader's own header comment for why. kMaxMipLevels is a
        // generous array-sizing bound (not a format limit).
        constexpr int kMaxMipLevels = 16;
        TextureUpscaleIwi::MipLevel mips[kMaxMipLevels];
        uint8_t* mipsOwned[kMaxMipLevels] = {};
        int mipCount = 0;
        uint32_t curW = actualW, curH = actualH;
        const uint8_t* curRgba = finalRgba;
        uint8_t* curOwnedRgba = nullptr;

        for (; mipCount < kMaxMipLevels; ++mipCount) {
            uint32_t compSize = 0;
            uint8_t* comp = TextureUpscaleDxt::EncodeFromRGBA(curRgba, curW, curH,
                TextureUpscaleDxt::BlockFormat::BC3, &compSize);
            if (!comp) break;
            mips[mipCount].data = comp;
            mips[mipCount].size = compSize;
            mipsOwned[mipCount] = comp;

            bool reachedKnownSourceCount = sourceMipCount > 0 && mipCount + 1 >= sourceMipCount;
            bool reachedFullPyramid = curW <= 1 && curH <= 1;
            if (mipCount + 1 >= kMaxMipLevels || reachedKnownSourceCount || reachedFullPyramid) {
                ++mipCount;
                break;
            }

            const uint32_t nextW = curW > 1 ? curW / 2 : 1;
            const uint32_t nextH = curH > 1 ? curH / 2 : 1;
            uint8_t* half = static_cast<uint8_t*>(malloc(static_cast<size_t>(nextW) * nextH * 4));
            if (!half) { ++mipCount; break; }
            for (uint32_t y = 0; y < nextH; ++y) {
                for (uint32_t x = 0; x < nextW; ++x) {
                    uint32_t sx0 = x * 2, sx1 = (sx0 + 1 < curW) ? sx0 + 1 : sx0;
                    uint32_t sy0 = y * 2, sy1 = (sy0 + 1 < curH) ? sy0 + 1 : sy0;
                    const uint8_t* p0 = curRgba + (static_cast<size_t>(sy0) * curW + sx0) * 4;
                    const uint8_t* p1 = curRgba + (static_cast<size_t>(sy0) * curW + sx1) * 4;
                    const uint8_t* p2 = curRgba + (static_cast<size_t>(sy1) * curW + sx0) * 4;
                    const uint8_t* p3 = curRgba + (static_cast<size_t>(sy1) * curW + sx1) * 4;
                    uint8_t* d = half + (static_cast<size_t>(y) * nextW + x) * 4;
                    d[0] = static_cast<uint8_t>((p0[0] + p1[0] + p2[0] + p3[0]) / 4);
                    d[1] = static_cast<uint8_t>((p0[1] + p1[1] + p2[1] + p3[1]) / 4);
                    d[2] = static_cast<uint8_t>((p0[2] + p1[2] + p2[2] + p3[2]) / 4);
                    d[3] = static_cast<uint8_t>((p0[3] + p1[3] + p2[3] + p3[3]) / 4);
                }
            }
            if (curOwnedRgba) free(curOwnedRgba);
            curRgba = half;
            curOwnedRgba = half;
            curW = nextW; curH = nextH;
        }
        if (curOwnedRgba) free(curOwnedRgba);
        free(netOut);
        if (freeFinalRgba) free(finalRgba);

        if (mipCount == 0) {
            char buf[300];
            sprintf_s(buf, "[builder] '%.200s': EncodeFromRGBA failed", assetName);
            LogFromController(buf);
            return false;
        }

        uint32_t iwiSize = 0;
        uint8_t* iwi = TextureUpscaleIwi::EncodeIwi8(static_cast<uint16_t>(actualW), static_cast<uint16_t>(actualH), 1,
            TextureUpscaleIwi::Format::DXT5, mips, mipCount, &iwiSize);
        for (int i = 0; i < mipCount; ++i) free(mipsOwned[i]);
        if (!iwi) {
            char buf[300];
            sprintf_s(buf, "[builder] '%.200s': EncodeIwi8 failed", assetName);
            LogFromController(buf);
            return false;
        }

        bool stored = TextureUpscaleCache::StoreUpscaledIwi(assetName, scaleMultiplier, iwi, iwiSize);
        free(iwi);

        char buf[300];
        if (stored) {
            sprintf_s(buf, "[builder] '%.200s': %ux%u -> %ux%u cached (%u bytes, %d mip levels)",
                assetName, width, height, actualW, actualH, iwiSize, mipCount);
            LogFromController(buf);
            InterlockedIncrement(&g_totalCached);
        } else {
            sprintf_s(buf, "[builder] '%.200s': StoreUpscaledIwi failed", assetName);
            LogFromController(buf);
        }
        return stored;
    }

    void GetExeDir(char* out, size_t outSize)
    {
        GetModuleFileNameA(nullptr, out, static_cast<DWORD>(outSize));
        char* lastSlash = strrchr(out, '\\');
        if (lastSlash) *(lastSlash + 1) = '\0';
    }

    // Self-extracts this exe's own embedded Real-ESRGAN model weights to a
    // private, per-user AppData location (2026-10-01, direct instruction:
    // "whatever can be hidden from user or not neccesary to user like the
    // models should be in the appdata") -- NOT next to this exe (which lives
    // in the game's own install directory, visible to the player), same
    // "implementation detail, not player-facing" treatment
    // dxvk_streamline_extract_x64.cpp already gives the DXVK/Streamline
    // binaries. Idempotent (skip-if-already-extracted) since this is a
    // real, static, ~33MB vendored asset of this tool itself, not prone to
    // going stale the way a third-party SDK could.
    //
    // Falls back to realesrgan_models\ next to this exe (the OLD location,
    // still produced by this project's own dev-build DeployBuilderAssets
    // target) if the embedded resource isn't present in this build at all
    // (MW3NCP_EMBED_REALESRGAN_MODEL gate, a fresh clone without the real
    // model files vendored locally yet) -- matches every other embedded-
    // resource fallback already established in this project.
    bool EnsureModelsExtracted(char* outModelDir, size_t outModelDirSize)
    {
        char localAppData[MAX_PATH];
        DWORD len = GetEnvironmentVariableA("LOCALAPPDATA", localAppData, MAX_PATH);
        if (len > 0 && len < MAX_PATH) {
            char modelDir[MAX_PATH];
            sprintf_s(modelDir, "%s\\MW32011NCP\\texture_cache_builder\\realesrgan_models\\", localAppData);

            char binPath[MAX_PATH], paramPath[MAX_PATH];
            sprintf_s(binPath, "%srealesrgan-x4plus.bin", modelDir);
            sprintf_s(paramPath, "%srealesrgan-x4plus.param", modelDir);

            if (GetFileAttributesA(binPath) != INVALID_FILE_ATTRIBUTES &&
                GetFileAttributesA(paramPath) != INVALID_FILE_ATTRIBUTES) {
                strncpy_s(outModelDir, outModelDirSize, modelDir, _TRUNCATE);
                return true; // already extracted a prior run
            }

            HMODULE selfModule = nullptr;
            GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
                reinterpret_cast<LPCSTR>(&EnsureModelsExtracted), &selfModule);
            HRSRC binRes = FindResourceA(selfModule, MAKEINTRESOURCEA(IDR_REALESRGAN_BIN), RT_RCDATA);
            HRSRC paramRes = FindResourceA(selfModule, MAKEINTRESOURCEA(IDR_REALESRGAN_PARAM), RT_RCDATA);
            if (binRes && paramRes) {
                char parentDir[MAX_PATH];
                sprintf_s(parentDir, "%s\\MW32011NCP", localAppData);
                CreateDirectoryA(parentDir, nullptr);
                sprintf_s(parentDir, "%s\\MW32011NCP\\texture_cache_builder", localAppData);
                CreateDirectoryA(parentDir, nullptr);
                CreateDirectoryA(modelDir, nullptr);

                auto extractOne = [&](HRSRC res, const char* destPath) -> bool {
                    HGLOBAL resData = LoadResource(selfModule, res);
                    if (!resData) return false;
                    void* pData = LockResource(resData);
                    DWORD size = SizeofResource(selfModule, res);
                    if (!pData || size == 0) return false;
                    FILE* f = nullptr;
                    if (fopen_s(&f, destPath, "wb") != 0 || !f) return false;
                    size_t written = fwrite(pData, 1, size, f);
                    fclose(f);
                    return written == size;
                };

                if (extractOne(binRes, binPath) && extractOne(paramRes, paramPath)) {
                    LogFromController("[builder] extracted embedded Real-ESRGAN model to AppData.");
                    strncpy_s(outModelDir, outModelDirSize, modelDir, _TRUNCATE);
                    return true;
                }
                LogFromController("[builder] failed to extract the embedded Real-ESRGAN model.");
            }
        }

        // Fallback: the old, next-to-exe location (dev builds / a build
        // without the model embedded) -- TextureUpscaleNcnn::EnsureModelLoaded's
        // own default behavior when passed nullptr.
        outModelDir[0] = '\0';
        return false;
    }

    // Processes every real, raw .iwi capture sitting in the staging folder
    // (<gameDir>\texture_capture_staging\, written by the live mod's own
    // TextureCaptureStaging::WriteRawCapture -- see that header's own
    // comment) through the full pipeline above, deleting each staged file
    // once handled (cached, or confirmed already up to date). Flat by
    // construction (WriteRawCapture never creates subdirectories), but
    // walked recursively anyway as a cheap safety net, same defensive shape
    // as the prior Unlinker-output walk this replaces.
    int WalkAndProcessStagingDir(const char* dirPath, int scaleMultiplier)
    {
        int processedCount = 0;
        char searchPattern[MAX_PATH];
        sprintf_s(searchPattern, "%s\\*", dirPath);
        WIN32_FIND_DATAA findData;
        HANDLE h = FindFirstFileA(searchPattern, &findData);
        if (h == INVALID_HANDLE_VALUE) return 0;
        do {
            if (g_cancelRequested) break;
            if (strcmp(findData.cFileName, ".") == 0 || strcmp(findData.cFileName, "..") == 0) continue;
            char fullPath[MAX_PATH];
            sprintf_s(fullPath, "%s\\%s", dirPath, findData.cFileName);
            if (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                processedCount += WalkAndProcessStagingDir(fullPath, scaleMultiplier);
                continue;
            }
            const char* ext = strrchr(findData.cFileName, '.');
            if (!ext || _stricmp(ext, ".iwi") != 0) continue;

            char assetName[256];
            strncpy_s(assetName, findData.cFileName, _TRUNCATE);
            char* dot = strrchr(assetName, '.');
            if (dot) *dot = '\0';

            if (TextureUpscaleCache::CacheEntryExists(assetName, scaleMultiplier)) {
                InterlockedIncrement(&g_totalSkippedExisting);
                DeleteFileA(fullPath);
                InterlockedIncrement(&g_totalProcessed);
                PostProgress();
                continue;
            }

            FILE* f = nullptr;
            if (fopen_s(&f, fullPath, "rb") == 0 && f) {
                fseek(f, 0, SEEK_END);
                long size = ftell(f);
                fseek(f, 0, SEEK_SET);
                if (size > 0) {
                    uint8_t* buf = static_cast<uint8_t*>(malloc(static_cast<size_t>(size)));
                    if (buf) {
                        size_t readBytes = fread(buf, 1, static_cast<size_t>(size), f);
                        if (readBytes == static_cast<size_t>(size)) {
                            int format = 0, sourceMipCount = 0;
                            uint16_t width = 0, height = 0;
                            const uint8_t* baseMipData = nullptr;
                            uint32_t baseMipSize = 0;
                            if (ParseIwiHeader(buf, static_cast<uint32_t>(size), &format, &width, &height,
                                                &sourceMipCount, &baseMipData, &baseMipSize)) {
                                bool ok = ProcessOneTexture(assetName, format, width, height, sourceMipCount,
                                                             baseMipData, baseMipSize, scaleMultiplier);
                                if (ok) ++processedCount; else InterlockedIncrement(&g_totalFailed);
                            } else {
                                char buf2[300];
                                sprintf_s(buf2, "[builder] '%.200s': staged file failed to parse as a valid IWI -- skipping", assetName);
                                LogFromController(buf2);
                                InterlockedIncrement(&g_totalFailed);
                            }
                        }
                        free(buf);
                    }
                }
                fclose(f);
            }
            DeleteFileA(fullPath);
            InterlockedIncrement(&g_totalProcessed);
            PostProgress();
        } while (FindNextFileA(h, &findData));
        FindClose(h);
        return processedCount;
    }

    // Real coverage-progress baseline against the bundled ~17,000-name
    // basemap (resource.h's own IDR_TEXTURE_BASEMAP) -- PROGRESS REPORTING
    // ONLY, never used to force anything to load or extract (see this
    // file's own top header comment). Returns the real known-name count;
    // *outAlreadyCached is how many of those already have a real cache
    // entry for `scaleMultiplier` (a cheap GetFileAttributesA check per
    // name via TextureUpscaleCache::CacheEntryExists, not a real read).
    int CountBasemapCoverage(int scaleMultiplier, int* outAlreadyCached)
    {
        *outAlreadyCached = 0;
        HMODULE selfModule = nullptr;
        GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
            reinterpret_cast<LPCSTR>(&CountBasemapCoverage), &selfModule);
        HRSRC res = FindResourceA(selfModule, MAKEINTRESOURCEA(IDR_TEXTURE_BASEMAP), RT_RCDATA);
        if (!res) return 0; // not embedded in this build -- progress just won't show a known-total
        HGLOBAL resData = LoadResource(selfModule, res);
        if (!resData) return 0;
        const char* data = static_cast<const char*>(LockResource(resData));
        DWORD size = SizeofResource(selfModule, res);
        if (!data || size == 0) return 0;

        int knownCount = 0, alreadyCached = 0;
        size_t lineStart = 0;
        char name[256];
        for (DWORD i = 0; i <= size; ++i) {
            if (i == size || data[i] == '\n' || data[i] == '\r') {
                size_t lineLen = i - lineStart;
                if (lineLen > 0 && lineLen < sizeof(name)) {
                    memcpy(name, data + lineStart, lineLen);
                    name[lineLen] = '\0';
                    ++knownCount;
                    if (TextureUpscaleCache::CacheEntryExists(name, scaleMultiplier)) ++alreadyCached;
                }
                lineStart = i + 1;
            }
        }
        *outAlreadyCached = alreadyCached;
        return knownCount;
    }

    int g_scaleMultiplier = 4;

    DWORD WINAPI BuildThreadProc(LPVOID)
    {
        LogFromController("[builder] TextureCacheBuilder starting.");

        if (!TextureUpscaleCache::EnsureCacheDir()) {
            LogFromController("[builder] FATAL: could not create/access texture_upscale_cache\\ -- "
                "is this exe running from inside the real game install directory?");
            PostMessageA(g_mainWnd, WM_APP_DONE, 0, 0);
            return 0;
        }

        char extractedModelDir[MAX_PATH] = {};
        bool haveExtracted = EnsureModelsExtracted(extractedModelDir, sizeof(extractedModelDir));

        LogFromController("[builder] loading Real-ESRGAN model...");
        if (!TextureUpscaleNcnn::EnsureModelLoaded(haveExtracted ? extractedModelDir : nullptr)) {
            LogFromController("[builder] FATAL: Real-ESRGAN model failed to load. This copy of "
                "TextureCacheBuilder.exe doesn't have the model embedded -- rebuild the mod with "
                "the model files vendored, or place realesrgan_models\\ next to this exe manually.");
            PostMessageA(g_mainWnd, WM_APP_DONE, 0, 0);
            return 0;
        }
        LogFromController("[builder] model loaded.");

        char exeDir[MAX_PATH];
        GetExeDir(exeDir, sizeof(exeDir));

        char stagingDir[MAX_PATH];
        sprintf_s(stagingDir, "%stexture_capture_staging", exeDir);
        if (GetFileAttributesA(stagingDir) == INVALID_FILE_ATTRIBUTES) {
            LogFromController("[builder] Nothing staged yet (texture_capture_staging\\ doesn't exist). "
                "Play a session with the mod first -- organic capture populates this folder as you play; "
                "run this tool again once it has.");
            PostMessageA(g_mainWnd, WM_APP_DONE, 0, 0);
            return 0;
        }

        // Real coverage baseline against the bundled known-name list --
        // reporting only, see CountBasemapCoverage's own comment.
        int alreadyCached = 0;
        int knownTotal = CountBasemapCoverage(g_scaleMultiplier, &alreadyCached);
        if (knownTotal > 0) {
            char cbuf[256];
            sprintf_s(cbuf, "[builder] known-texture coverage so far: %d/%d (%.1f%%) at %dx scale.",
                alreadyCached, knownTotal, 100.0 * alreadyCached / knownTotal, g_scaleMultiplier);
            LogFromController(cbuf);
        }

        LogFromController("[builder] scanning texture_capture_staging\\ ...");
        int stagedCount = 0;
        {
            char searchPattern[MAX_PATH];
            sprintf_s(searchPattern, "%s\\*.iwi", stagingDir);
            WIN32_FIND_DATAA fd;
            HANDLE h = FindFirstFileA(searchPattern, &fd);
            if (h != INVALID_HANDLE_VALUE) {
                do { ++stagedCount; } while (FindNextFileA(h, &fd));
                FindClose(h);
            }
        }
        if (stagedCount == 0) {
            LogFromController("[builder] no newly staged captures to process right now. "
                "Play more, or re-run later -- nothing to do.");
            PostMessageA(g_mainWnd, WM_APP_DONE, 0, 0);
            return 0;
        }

        char buf[256];
        sprintf_s(buf, "[builder] %d staged capture(s) found, scale=%dx. Starting.", stagedCount, g_scaleMultiplier);
        LogFromController(buf);
        g_totalQueued = stagedCount;
        PostProgress();

        int newlyCachedThisRun = WalkAndProcessStagingDir(stagingDir, g_scaleMultiplier);

        sprintf_s(buf, "[builder] DONE. %d newly cached (%ld total cached, %ld already up to date, %ld failed).%s",
            newlyCachedThisRun, g_totalCached, g_totalSkippedExisting, g_totalFailed,
            g_cancelRequested ? " (cancelled)" : "");
        LogFromController(buf);

        if (knownTotal > 0) {
            alreadyCached = 0;
            CountBasemapCoverage(g_scaleMultiplier, &alreadyCached);
            sprintf_s(buf, "[builder] known-texture coverage now: %d/%d (%.1f%%) at %dx scale.",
                alreadyCached, knownTotal, 100.0 * alreadyCached / knownTotal, g_scaleMultiplier);
            LogFromController(buf);
        }

        PostMessageA(g_mainWnd, WM_APP_DONE, 0, 0);
        return 0;
    }

    // ---- GUI ----
    HWND g_logEdit, g_progressBar, g_statusLabel, g_startButton, g_scaleCombo;

    void AppendLog(const char* msg)
    {
        int len = GetWindowTextLengthA(g_logEdit);
        SendMessageA(g_logEdit, EM_SETSEL, len, len);
        SendMessageA(g_logEdit, EM_REPLACESEL, FALSE, reinterpret_cast<LPARAM>(msg));
        SendMessageA(g_logEdit, EM_REPLACESEL, FALSE, reinterpret_cast<LPARAM>("\r\n"));
        SendMessageA(g_logEdit, WM_VSCROLL, SB_BOTTOM, 0);
    }

    void UpdateProgressUi()
    {
        LONG queued = g_totalQueued, processed = g_totalProcessed;
        if (queued > 0) {
            SendMessageA(g_progressBar, PBM_SETRANGE32, 0, queued);
            SendMessageA(g_progressBar, PBM_SETPOS, processed, 0);
        }
        char buf[300];
        sprintf_s(buf, "Staged captures processed: %ld / %ld  |  Cached: %ld  |  Already up to date: %ld  |  Failed: %ld",
            processed, queued, g_totalCached, g_totalSkippedExisting, g_totalFailed);
        SetWindowTextA(g_statusLabel, buf);
    }

    LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        switch (msg) {
        case WM_CREATE: {
            CreateWindowExA(0, "STATIC", "Scale:", WS_CHILD | WS_VISIBLE,
                10, 12, 50, 20, hwnd, nullptr, nullptr, nullptr);
            g_scaleCombo = CreateWindowExA(0, "COMBOBOX", "", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST,
                65, 10, 80, 100, hwnd, reinterpret_cast<HMENU>(100), nullptr, nullptr);
            SendMessageA(g_scaleCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>("2x"));
            SendMessageA(g_scaleCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>("3x"));
            SendMessageA(g_scaleCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>("4x"));
            SendMessageA(g_scaleCombo, CB_SETCURSEL, 2, 0); // default 4x

            g_startButton = CreateWindowExA(0, "BUTTON", "Start",
                WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 160, 8, 100, 26,
                hwnd, reinterpret_cast<HMENU>(101), nullptr, nullptr);

            g_statusLabel = CreateWindowExA(0, "STATIC", "Ready.", WS_CHILD | WS_VISIBLE,
                10, 44, 760, 20, hwnd, nullptr, nullptr, nullptr);

            g_progressBar = CreateWindowExA(0, PROGRESS_CLASSA, "", WS_CHILD | WS_VISIBLE,
                10, 68, 760, 20, hwnd, nullptr, nullptr, nullptr);

            g_logEdit = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", "",
                WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL,
                10, 96, 760, 400, hwnd, nullptr, nullptr, nullptr);
            HFONT font = CreateFontA(14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                DEFAULT_QUALITY, FIXED_PITCH, "Consolas");
            SendMessageA(g_logEdit, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
            return 0;
        }
        case WM_COMMAND:
            if (LOWORD(wParam) == 101) { // Start/Cancel button, toggled by its own text
                char btnText[32];
                GetWindowTextA(g_startButton, btnText, sizeof(btnText));
                if (strcmp(btnText, "Start") == 0) {
                    int sel = static_cast<int>(SendMessageA(g_scaleCombo, CB_GETCURSEL, 0, 0));
                    g_scaleMultiplier = (sel == 0) ? 2 : (sel == 1) ? 3 : 4;
                    g_cancelRequested = 0;
                    g_totalQueued = g_totalProcessed = g_totalCached = g_totalSkippedExisting = g_totalFailed = 0;
                    SetWindowTextA(g_startButton, "Cancel");
                    EnableWindow(g_scaleCombo, FALSE);
                    CreateThread(nullptr, 0, BuildThreadProc, nullptr, 0, nullptr);
                } else {
                    g_cancelRequested = 1;
                    AppendLog("[builder] cancel requested -- finishing current texture...");
                }
            }
            return 0;
        case WM_APP_LOG: {
            char* text = reinterpret_cast<char*>(lParam);
            AppendLog(text);
            free(text);
            return 0;
        }
        case WM_APP_PROGRESS:
            UpdateProgressUi();
            return 0;
        case WM_APP_DONE:
            SetWindowTextA(g_startButton, "Start");
            EnableWindow(g_scaleCombo, TRUE);
            UpdateProgressUi();
            return 0;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        }
        return DefWindowProcA(hwnd, msg, wParam, lParam);
    }
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int nCmdShow)
{
    InitializeCriticalSection(&g_logLock);

    INITCOMMONCONTROLSEX icc{ sizeof(icc), ICC_PROGRESS_CLASS };
    InitCommonControlsEx(&icc);

    WNDCLASSA wc{};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = "TextureCacheBuilderWnd";
    wc.hCursor = LoadCursorA(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
    RegisterClassA(&wc);

    g_mainWnd = CreateWindowExA(0, "TextureCacheBuilderWnd",
        "MW32011NCP Texture Cache Builder (offline, out-of-process ESRGAN cache generator)",
        WS_OVERLAPPEDWINDOW & ~WS_MAXIMIZEBOX & ~WS_THICKFRAME,
        CW_USEDEFAULT, CW_USEDEFAULT, 800, 540,
        nullptr, nullptr, hInstance, nullptr);

    AppendLog("Run this from inside the real game install directory (same folder as d3d9.dll). "
        "It processes texture_capture_staging\\ (filled by the mod's own safe, organic capture "
        "during real gameplay) and writes into texture_upscale_cache\\, the same folder the live "
        "mod reads from.");
    AppendLog("The Real-ESRGAN model is bundled inside this exe -- nothing else to download or "
        "place manually. Play a session first so there's something staged to process.");

    ShowWindow(g_mainWnd, nCmdShow);
    UpdateWindow(g_mainWnd);

    MSG msg;
    while (GetMessageA(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }
    return 0;
}
