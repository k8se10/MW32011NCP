// Extracts the embedded TextureCacheBuilder.exe (the standalone, offline,
// out-of-process texture-upscale cache generator -- see
// tools/texture_cache_builder/main.cpp's own header comment for the full
// "why a separate process" rationale) to the GAME's own install directory,
// on every real mod init (2026-10-01). Direct instruction, following "this
// is a user facing feature and must have a gui and easy use": "it should be
// extracted by the mod on first run" -- a player should never have to find,
// download, or manually copy this tool anywhere; it just appears next to
// the game the moment the mod itself runs.
//
// Deliberately NOT the same %LOCALAPPDATA% destination
// dxvk_streamline_extract_x64.cpp uses for DXVK/Streamline -- those are
// internal runtime dependencies the player never interacts with directly.
// This tool is the opposite: a real, player-facing, double-clickable
// application that needs to sit somewhere the player will actually find it
// (the game's own install folder, right next to d3d9.dll) and needs to read/
// write texture_capture_staging\ and texture_upscale_cache\, both already
// real paths relative to that same directory.
//
// Idempotent, not unconditional-every-launch like the DXVK/Streamline
// extraction -- this is this project's OWN build output, not a third-party
// SDK that could silently drift out of sync underneath it, so a cheap real
// file-size comparison against the embedded resource's own size is enough
// to detect "stale, needs re-extracting" (e.g. after a mod update) without
// paying a real ~14MB disk write on every single launch.

#include <windows.h>
#include <cstdio>
#include <cstring>
#include "../resource.h"

extern void LogFromController(const char* msg); // dllmain.cpp's own external-linkage logger.

namespace
{
    bool ExtractResourceToFile(int resId, const char* destPath, DWORD* outSize)
    {
        HMODULE selfModule = nullptr;
        GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
            reinterpret_cast<LPCSTR>(&ExtractResourceToFile), &selfModule);

        HRSRC res = FindResourceA(selfModule, MAKEINTRESOURCEA(resId), RT_RCDATA);
        if (!res) return false;
        HGLOBAL resData = LoadResource(selfModule, res);
        if (!resData) return false;
        void* pData = LockResource(resData);
        DWORD size = SizeofResource(selfModule, res);
        if (!pData || size == 0) return false;
        if (outSize) *outSize = size;

        HANDLE hFile = CreateFileA(destPath, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
            FILE_ATTRIBUTE_NORMAL, nullptr);
        if (hFile == INVALID_HANDLE_VALUE) return false;

        DWORD written = 0;
        BOOL ok = WriteFile(hFile, pData, size, &written, nullptr);
        CloseHandle(hFile);
        return ok && written == size;
    }
}

// Called once, unconditionally, early in DllMain's real init path (both
// SP and MP share the same real game install directory, so this isn't
// gated behind GetDetectedGameExecutable() the way the SP-only gameplay
// hooks are -- just a file write, no engine interaction at all).
void ExtractTextureCacheBuilderIfNeededX64()
{
    HMODULE selfModule = nullptr;
    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
        reinterpret_cast<LPCSTR>(&ExtractTextureCacheBuilderIfNeededX64), &selfModule);
    HRSRC res = FindResourceA(selfModule, MAKEINTRESOURCEA(IDR_TEXTURE_CACHE_BUILDER_EXE), RT_RCDATA);
    if (!res) return; // not embedded in this build (dev build without the
        // tool built locally yet) -- same graceful, silent skip as the
        // DXVK/NVIDIA embed's own missing-resource fallback.
    DWORD embeddedSize = SizeofResource(selfModule, res);
    if (embeddedSize == 0) return;

    char exeDir[MAX_PATH];
    GetModuleFileNameA(nullptr, exeDir, MAX_PATH); // the GAME's own exe path
        // (main module), same convention texture_capture_staging.cpp's own
        // EnsureDir already uses.
    char* lastSlash = strrchr(exeDir, '\\');
    if (lastSlash) *(lastSlash + 1) = '\0';

    char destPath[MAX_PATH];
    sprintf_s(destPath, "%sTextureCacheBuilder.exe", exeDir);

    WIN32_FILE_ATTRIBUTE_DATA existing{};
    if (GetFileAttributesExA(destPath, GetFileExInfoStandard, &existing)) {
        LARGE_INTEGER sz;
        sz.HighPart = static_cast<LONG>(existing.nFileSizeHigh);
        sz.LowPart = existing.nFileSizeLow;
        if (static_cast<DWORD>(sz.QuadPart) == embeddedSize) return; // already current
    }

    DWORD writtenSize = 0;
    if (ExtractResourceToFile(IDR_TEXTURE_CACHE_BUILDER_EXE, destPath, &writtenSize)) {
        LogFromController("[embed] Extracted/updated TextureCacheBuilder.exe in the game install "
            "directory -- run it any time to cache upscaled textures from a real play session.");
    } else {
        char buf[300];
        sprintf_s(buf, "[embed] Failed to extract TextureCacheBuilder.exe to '%s' (err=%lu).",
            destPath, GetLastError());
        LogFromController(buf);
    }
}
