#include "texture_capture_staging.h"

#include <windows.h>
#include <cstdio>
#include <cstring>

extern void LogFromController(const char* msg);

namespace TextureCaptureStaging
{
namespace
{
    bool g_dirReady = false;
    char g_dir[MAX_PATH] = {};

    // Same safe-character set as TextureUpscaleCache's own SanitizeForFilename
    // (texture_upscale_cache.cpp) -- kept as an independent copy rather than
    // a shared helper since this module has no other dependency on that file
    // and the two caches (staging vs. upscaled) are deliberately separate
    // directories with separate lifetimes.
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

    bool EnsureDir()
    {
        if (g_dirReady) return true;
        char path[MAX_PATH];
        GetModuleFileNameA(nullptr, path, MAX_PATH);
        char* lastSlash = strrchr(path, '\\');
        if (lastSlash) *(lastSlash + 1) = '\0';
        strcat_s(path, "texture_capture_staging");
        CreateDirectoryA(path, nullptr); // fine if it already exists (ERROR_ALREADY_EXISTS)
        strcpy_s(g_dir, path);
        g_dirReady = true;
        LogFromController("[texture-capture-staging] staging dir ready");
        return true;
    }
}

bool WriteRawCapture(const char* name, const uint8_t* data, uint32_t size)
{
    if (!name || !data || size == 0) return false;
    if (!EnsureDir()) return false;

    char safeName[256];
    SanitizeForFilename(name, safeName, sizeof(safeName));
    if (safeName[0] == '\0') return false;

    char path[MAX_PATH];
    sprintf_s(path, "%s\\%s.iwi", g_dir, safeName);

    // Already staged this session (or a prior one) -- the same real texture
    // can be captured many times across a play session (shared materials,
    // revisited areas); nothing to redo.
    if (GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES) return true;

    FILE* f = nullptr;
    if (fopen_s(&f, path, "wb") != 0 || !f) {
        char msg[320];
        sprintf_s(msg, "[texture-capture-staging] failed to open for write: %s", path);
        LogFromController(msg);
        return false;
    }
    size_t written = fwrite(data, 1, size, f);
    fclose(f);

    if (written != size) {
        char msg[320];
        sprintf_s(msg, "[texture-capture-staging] short write (%zu/%u bytes): %s",
                   written, size, path);
        LogFromController(msg);
        DeleteFileA(path); // don't leave a truncated/corrupt file behind
        return false;
    }
    return true;
}
}
