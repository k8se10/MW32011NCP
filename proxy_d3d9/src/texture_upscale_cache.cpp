#include "texture_upscale_cache.h"

#include <windows.h>
#include <cstdio>
#include <cstring>
#include <cstdlib>

extern void LogFromController(const char* msg); // defined in dllmain.cpp

namespace TextureUpscaleCache
{
namespace
{
    bool g_cacheDirReady = false;
    char g_cacheDir[MAX_PATH];

    // Sanitizes a real image name into a safe, flat filename component --
    // real names observed live (texture_upscale_cache_research.md's own
    // FindOrLoadAsset/FUN_1401bae80 diagnostic rounds) include characters
    // like '~' and '-' (procedurally-generated composite names) and this
    // project has no static guarantee real names never contain a real path
    // separator ('/' or '\\') -- replace anything outside a conservative
    // safe set with '_' rather than assume. Truncates to fit the destination
    // buffer; a truncated-but-still-unique-enough name is an acceptable
    // degradation for a cache key, never a crash risk.
    void SanitizeForFilename(const char* name, char* out, size_t outSize)
    {
        size_t i = 0;
        for (; name[i] != '\0' && i + 1 < outSize; ++i) {
            char c = name[i];
            bool safe = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                        (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '~';
            out[i] = safe ? c : '_';
        }
        out[i] = '\0';
    }
}

bool EnsureCacheDir()
{
    if (g_cacheDirReady) return true;
    char path[MAX_PATH];
    GetModuleFileNameA(nullptr, path, MAX_PATH); // full path of the .exe that loaded us,
        // same convention as asset_capture.cpp's own EnsureOutputDir
    char* lastSlash = strrchr(path, '\\');
    if (lastSlash) *(lastSlash + 1) = '\0';
    strcat_s(path, "texture_upscale_cache");
    CreateDirectoryA(path, nullptr); // ignore failure -- ERROR_ALREADY_EXISTS is the
        // common/expected case across sessions; any other failure surfaces naturally
        // as every subsequent read/write also failing, logged once at that point.
    strcpy_s(g_cacheDir, path);
    g_cacheDirReady = true;
    return true;
}

namespace
{
    // Builds the real, full on-disk path for a given (name, scale) cache
    // entry. Returns false if EnsureCacheDir() hasn't succeeded or the
    // sanitized name would produce an empty/degenerate filename.
    bool BuildCacheFilePath(const char* imageName, int scaleMultiplier, char* outPath, size_t outPathSize)
    {
        if (!g_cacheDirReady || !imageName || imageName[0] == '\0') return false;
        char safeName[256];
        SanitizeForFilename(imageName, safeName, sizeof(safeName));
        if (safeName[0] == '\0') return false;
        // "<dir>\<name>_<scale>x.iwi" -- the scale factor is part of the
        // filename itself (not a separate metadata sidecar) so changing
        // TextureRenderRes naturally invalidates old entries by no longer
        // matching any existing filename -- no explicit cache-invalidation
        // pass needed, matching this feature's own locked design.
        int written = sprintf_s(outPath, outPathSize, "%s\\%s_%dx.iwi", g_cacheDir, safeName, scaleMultiplier);
        return written > 0;
    }
}

namespace
{
    // Shared "read a whole file into a malloc'd buffer" helper -- used by
    // both TryLoadCachedUpscaledIwi and TryLoadCustomAsset below, which
    // differ only in how they build the real path, not in how they read it.
    uint8_t* ReadWholeFile(const char* path, uint32_t* outSize)
    {
        if (outSize) *outSize = 0;
        FILE* f = nullptr;
        if (fopen_s(&f, path, "rb") != 0 || !f) return nullptr; // real, expected
            // miss case -- no file at this path yet, not an error worth logging

        fseek(f, 0, SEEK_END);
        long size = ftell(f);
        if (size <= 0) {
            fclose(f);
            return nullptr;
        }
        fseek(f, 0, SEEK_SET);

        uint8_t* buffer = static_cast<uint8_t*>(malloc(static_cast<size_t>(size)));
        if (!buffer) {
            fclose(f);
            return nullptr;
        }
        size_t readBytes = fread(buffer, 1, static_cast<size_t>(size), f);
        fclose(f);
        if (readBytes != static_cast<size_t>(size)) {
            // Real, worth-logging case -- the file exists but is truncated/
            // corrupt. Treat identically to a miss for the caller (fall
            // through to the real, original image), but log it once so a
            // corrupt file doesn't silently and permanently shadow a real
            // texture.
            char buf[300];
            sprintf_s(buf, "[texture-upscale-cache] WARNING: '%.256s' read %zu of %ld expected bytes -- "
                "treating as a miss, falling back to the original image", path, readBytes, size);
            LogFromController(buf);
            free(buffer);
            return nullptr;
        }

        if (outSize) *outSize = static_cast<uint32_t>(size);
        return buffer;
    }
}

uint8_t* TryLoadCachedUpscaledIwi(const char* imageName, int scaleMultiplier, uint32_t* outSize)
{
    if (outSize) *outSize = 0;
    if (!EnsureCacheDir()) return nullptr;

    char path[MAX_PATH];
    if (!BuildCacheFilePath(imageName, scaleMultiplier, path, sizeof(path))) return nullptr;

    return ReadWholeFile(path, outSize);
}

bool StoreUpscaledIwi(const char* imageName, int scaleMultiplier, const uint8_t* iwiData, uint32_t iwiSize)
{
    if (!EnsureCacheDir()) return false;
    if (!iwiData || iwiSize == 0) return false;

    char path[MAX_PATH];
    if (!BuildCacheFilePath(imageName, scaleMultiplier, path, sizeof(path))) return false;

    // Write to a temp file then rename into place -- avoids a reader (the
    // synchronous TryLoadCachedUpscaledIwi above, called from the real
    // engine's own image-load hot path) ever observing a partially-written
    // cache file, matching the same "never let a reader see a half-written
    // file" discipline asset_capture.cpp's own background-write-worker
    // already established for this codebase, without needing a second,
    // separate write queue here.
    char tempPath[MAX_PATH];
    sprintf_s(tempPath, "%s.tmp", path);

    FILE* f = nullptr;
    if (fopen_s(&f, tempPath, "wb") != 0 || !f) {
        char buf[300];
        sprintf_s(buf, "[texture-upscale-cache] FAILED to open '%.256s' for write", tempPath);
        LogFromController(buf);
        return false;
    }
    size_t written = fwrite(iwiData, 1, iwiSize, f);
    fclose(f);
    if (written != iwiSize) {
        char buf[300];
        sprintf_s(buf, "[texture-upscale-cache] FAILED to write '%.256s' -- wrote %zu of %u bytes", tempPath, written, iwiSize);
        LogFromController(buf);
        DeleteFileA(tempPath);
        return false;
    }

    if (!MoveFileExA(tempPath, path, MOVEFILE_REPLACE_EXISTING)) {
        char buf[300];
        sprintf_s(buf, "[texture-upscale-cache] FAILED to finalize '%.256s' (MoveFileExA error=%lu)", path, GetLastError());
        LogFromController(buf);
        DeleteFileA(tempPath);
        return false;
    }

    return true;
}

uint8_t* TryLoadCustomAsset(const char* imageName, uint32_t* outSize)
{
    if (outSize) *outSize = 0;
    if (!imageName || imageName[0] == '\0') return nullptr;

    char safeName[256];
    SanitizeForFilename(imageName, safeName, sizeof(safeName));
    if (safeName[0] == '\0') return nullptr;

    // Real, bundled, shipped-with-the-mod asset directory -- distinct from
    // texture_upscale_cache\ (that one holds runtime-generated upscale
    // entries; this one holds fixed assets the mod itself ships, e.g. the
    // custom main menu background). Deployed alongside d3d9.dll by
    // proxy_d3d9.vcxproj's own build (mirrors the DXVK binary's existing
    // DeployDxvk target) -- never created/written by this project's own
    // runtime code, only ever read.
    char exeDir[MAX_PATH];
    GetModuleFileNameA(nullptr, exeDir, MAX_PATH);
    char* lastSlash = strrchr(exeDir, '\\');
    if (lastSlash) *(lastSlash + 1) = '\0';

    char path[MAX_PATH];
    sprintf_s(path, "%scustom_assets\\%s.iwi", exeDir, safeName);

    return ReadWholeFile(path, outSize);
}

}
