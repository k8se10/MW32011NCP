#include "texture_upscale_cache.h"

#include <windows.h>
#include <cstdio>
#include <cstring>
#include <cstdlib>

#include "texture_upscale_dxt_codec.h" // TextureUpscaleDxt::CompressedSize --
    // real correctness validation below.
#include "texture_upscale_iwi_writer.h" // TextureUpscaleIwi::Format -- same
    // format enum below.

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
            // Widened 2026-09-30 as part of the same sweep that found and fixed a
            // real, live sprintf_s overflow crash in this file's own corrupt-file
            // log line below -- this one's real worst case (~410 bytes) also
            // exceeded a 300-byte buffer, just hadn't been hit live yet.
            char buf[600];
            sprintf_s(buf, "[texture-upscale-cache] WARNING: '%.256s' read %zu of %ld expected bytes -- "
                "treating as a miss, falling back to the original image", path, readBytes, size);
            LogFromController(buf);
            free(buffer);
            return nullptr;
        }

        if (outSize) *outSize = static_cast<uint32_t>(size);
        return buffer;
    }

    // Real correctness validation (2026-09-30) -- a truncated write (this
    // project has now hit three real crashes/hangs mid-session while the
    // worker was actively writing cache files) can leave a file on disk
    // that reports a plausible size (Windows' own file-size metadata can
    // update before the actual data is flushed) but whose content is
    // garbage -- StoreUpscaledIwi's own temp-file-then-rename discipline
    // protects a concurrent READER from ever seeing a PARTIAL write, but
    // does nothing for a write that was already fully "renamed into place"
    // before a crash cut off the underlying flush. Live-reported: a
    // skybox that looked genuinely corrupt (not a decode/encode bug --
    // manually deleting and letting it recapture through the exact same
    // pipeline produced a correct result) turned out to be exactly this.
    // This feature's own cache ALWAYS writes DXT5 (see ProcessJob's own
    // header comment: "always re-encoded as DXT5 regardless of source
    // format"), always as a single mip -- so a real, cheap, definitive
    // check is possible: the header's own declared format/width/height
    // must produce an EXACT expected total file size; any mismatch means
    // the file is truncated, extended, or otherwise not what this feature
    // itself would ever have written, and is treated as fully corrupt.
    bool ValidateCachedIwi(const uint8_t* data, uint32_t size)
    {
        constexpr uint32_t kIwiHeaderSize = 0x20;
        if (!data || size <= kIwiHeaderSize) return false;
        if (data[0] != 'I' || data[1] != 'W' || data[2] != 'i' || data[3] != 8) return false;
        int8_t format = static_cast<int8_t>(data[0x08]);
        if (format != static_cast<int8_t>(TextureUpscaleIwi::Format::DXT5)) return false; // this
            // feature's own writer never emits anything else to its own cache
        uint16_t width = *reinterpret_cast<const uint16_t*>(data + 0x0A);
        uint16_t height = *reinterpret_cast<const uint16_t*>(data + 0x0C);
        if (width == 0 || height == 0) return false;
        uint32_t expectedTotal = kIwiHeaderSize +
            TextureUpscaleDxt::CompressedSize(width, height, TextureUpscaleDxt::BlockFormat::BC3);
        return expectedTotal == size;
    }
}

uint8_t* TryLoadCachedUpscaledIwi(const char* imageName, int scaleMultiplier, uint32_t* outSize)
{
    if (outSize) *outSize = 0;
    if (!EnsureCacheDir()) return nullptr;

    char path[MAX_PATH];
    if (!BuildCacheFilePath(imageName, scaleMultiplier, path, sizeof(path))) return nullptr;

    uint32_t size = 0;
    uint8_t* data = ReadWholeFile(path, &size);
    if (!data) return nullptr;

    if (!ValidateCachedIwi(data, size)) {
        // Real, live-confirmed crash (2026-09-30, FAIL_FAST_INVALID_ARG via
        // sprintf_s): the original buf[300] was genuinely too small for this
        // line's own real worst case (literal text ~170 bytes + up to 256
        // bytes for the truncated path + up to 10 digits for %u) -- the same
        // recurring sprintf_s-overflow bug class this project has hit many
        // times before, this time self-inflicted in this exact fix. Widened
        // with real margin, not just barely over the computed worst case.
        char buf[600];
        sprintf_s(buf, "[texture-upscale-cache] CORRUPT cache file detected and deleted: '%.256s' "
            "(%u bytes) -- will be treated as a miss and recaptured/regenerated the next time this "
            "image is loaded.", path, size);
        LogFromController(buf);
        free(data);
        DeleteFileA(path); // real, deliberate: never let a confirmed-corrupt file linger to be
            // served again or independently rediscovered -- forces a genuine rebuild.
        return nullptr;
    }

    if (outSize) *outSize = size;
    return data;
}

// Lightweight existence check (2026-09-29) -- GetFileAttributesA, never a
// real file open/read, for the session-continuity basemap's own startup
// coverage count (texture_upscale_worker.cpp's LoadPendingManifest), which
// needs to check up to ~17,000 names quickly without paying a real open+read
// cost per name the way TryLoadCachedUpscaledIwi above does.
bool CacheEntryExists(const char* imageName, int scaleMultiplier)
{
    if (!EnsureCacheDir()) return false;
    char path[MAX_PATH];
    if (!BuildCacheFilePath(imageName, scaleMultiplier, path, sizeof(path))) return false;
    DWORD attrs = GetFileAttributesA(path);
    return attrs != INVALID_FILE_ATTRIBUTES && !(attrs & FILE_ATTRIBUTE_DIRECTORY);
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

    // Every buf[] below widened to 600 in the same 2026-09-30 sweep that found
    // and fixed a real, live sprintf_s overflow crash in this file (all four
    // of these real worst-case computations exceed a 300-byte buffer once the
    // %.256s path substitution is near its own real max, just hadn't been hit
    // live yet).
    FILE* f = nullptr;
    if (fopen_s(&f, tempPath, "wb") != 0 || !f) {
        char buf[600];
        sprintf_s(buf, "[texture-upscale-cache] FAILED to open '%.256s' for write", tempPath);
        LogFromController(buf);
        return false;
    }
    size_t written = fwrite(iwiData, 1, iwiSize, f);
    fclose(f);
    if (written != iwiSize) {
        char buf[600];
        sprintf_s(buf, "[texture-upscale-cache] FAILED to write '%.256s' -- wrote %zu of %u bytes", tempPath, written, iwiSize);
        LogFromController(buf);
        DeleteFileA(tempPath);
        return false;
    }

    if (!MoveFileExA(tempPath, path, MOVEFILE_REPLACE_EXISTING)) {
        char buf[600];
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
