// MW32011NCP, 2026-09-24: runtime extraction of the embedded DXVK fork build
// and NVIDIA Streamline SDK binaries (see resource.h/proxy_d3d9.rc) to a
// private, per-user AppData location -- never the game's own install folder.
// Direct instruction: "we shouldnt need to have extra dlls in the game
// folder. it should all be inside our dll," followed by "this should be on
// every launch to somehere in appdata" -- so extraction is UNCONDITIONAL on
// every DLL init (no skip-if-already-extracted check), always writing fresh
// copies, and the destination is %LOCALAPPDATA%\MW32011NCP\runtime_x64\, not
// the OS %TEMP% directory this file's first draft used.
//
// These are real, mostly-large binaries (nvngx_dlss.dll alone is ~58MB) --
// writing them out is a real, visible disk write every launch, not a cached
// no-op. That's the explicit tradeoff for "always fresh, never stale" over
// "fast, but could silently serve a stale extracted copy after a rebuild."
//
// Extraction is a byte-identical copy of the embedded resource -- these
// binaries are never modified, only relocated from "embedded in our DLL" to
// "a real file on disk Windows' LoadLibrary can open," satisfying the
// NVIDIA RTX SDKs License's "unmodified... object-code form" redistribution
// term (see CLAUDE.md/AGENTS.md's own "NVIDIA Streamline / DLSS" section).

#include <windows.h>
#include <cstdio>
#include "../resource.h"

extern void LogFromController(const char* msg); // dllmain.cpp's own external-linkage logger.

namespace {

bool ExtractResourceToFile(int resId, const char* destPath)
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

    HANDLE hFile = CreateFileA(destPath, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) return false;

    DWORD written = 0;
    BOOL ok = WriteFile(hFile, pData, size, &written, nullptr);
    CloseHandle(hFile);
    return ok && written == size;
}

// %LOCALAPPDATA%\MW32011NCP\runtime_x64\ -- real, private, per-user location,
// distinct from the game's own install folder (which stays a single d3d9.dll
// from a player's perspective) and distinct from OS %TEMP% (survives across
// reboots without relying on temp-cleanup timing; still not part of the mod's
// own distributed footprint since it's regenerated fresh every launch).
bool GetRuntimeDirX64(char* outDir, size_t outDirSize)
{
    char localAppData[MAX_PATH];
    DWORD len = GetEnvironmentVariableA("LOCALAPPDATA", localAppData, MAX_PATH);
    if (len == 0 || len >= MAX_PATH) return false;

    sprintf_s(outDir, outDirSize, "%s\\MW32011NCP\\runtime_x64\\", localAppData);

    char parentDir[MAX_PATH];
    sprintf_s(parentDir, "%s\\MW32011NCP\\", localAppData);
    CreateDirectoryA(parentDir, nullptr);
    CreateDirectoryA(outDir, nullptr);
    return true;
}

} // namespace

// Extracts the embedded DXVK fork build to
// %LOCALAPPDATA%\MW32011NCP\runtime_x64\dxvk\d3d9.dll and returns that real
// path in outPath, ready to LoadLibraryA. Called from dllmain.cpp's
// TryLoadVendoredDxvk(), replacing the old "load a loose file the player had
// to manually place next to this DLL" design.
bool ExtractEmbeddedDxvkX64(char* outPath, size_t outPathSize)
{
    char dir[MAX_PATH];
    if (!GetRuntimeDirX64(dir, sizeof(dir))) {
        LogFromController("[embed] Could not resolve %LOCALAPPDATA% -- cannot extract the "
            "embedded DXVK build.");
        return false;
    }

    char subDir[MAX_PATH];
    sprintf_s(subDir, "%sdxvk\\", dir);
    CreateDirectoryA(subDir, nullptr);

    sprintf_s(outPath, outPathSize, "%sd3d9.dll", subDir);
    if (!ExtractResourceToFile(IDR_DXVK_D3D9, outPath)) {
        char buf[300];
        sprintf_s(buf, "[embed] Failed to extract the embedded DXVK build to '%s' (err=%lu).",
            outPath, GetLastError());
        LogFromController(buf);
        return false;
    }
    return true;
}

// Extracts the four embedded Streamline/NVIDIA binaries to
// %LOCALAPPDATA%\MW32011NCP\runtime_x64\streamline\ (all in the SAME
// directory, since sl.interposer.dll resolves its own sibling plugins via
// its own directory's real file system -- this project cannot change that,
// it's baked into NVIDIA's signed binary) and returns that directory in
// outDir. Called from streamline_integration_x64.cpp's
// TryLoadStreamlineInterposer().
bool ExtractEmbeddedStreamlineX64(char* outDir, size_t outDirSize)
{
    char dir[MAX_PATH];
    if (!GetRuntimeDirX64(dir, sizeof(dir))) {
        LogFromController("[embed] Could not resolve %LOCALAPPDATA% -- cannot extract the "
            "embedded Streamline SDK binaries.");
        return false;
    }

    char subDir[MAX_PATH];
    sprintf_s(subDir, "%sstreamline\\", dir);
    CreateDirectoryA(subDir, nullptr);

    struct { int resId; const char* fileName; } files[] = {
        { IDR_SL_INTERPOSER, "sl.interposer.dll" },
        { IDR_SL_COMMON,     "sl.common.dll" },
        { IDR_SL_DLSS,       "sl.dlss.dll" },
        { IDR_NGX_DLSS,      "nvngx_dlss.dll" },
    };

    for (const auto& f : files) {
        char path[MAX_PATH];
        sprintf_s(path, "%s%s", subDir, f.fileName);
        if (!ExtractResourceToFile(f.resId, path)) {
            char buf[300];
            sprintf_s(buf, "[embed] Failed to extract embedded Streamline file '%s' (err=%lu).",
                f.fileName, GetLastError());
            LogFromController(buf);
            return false;
        }
    }

    sprintf_s(outDir, outDirSize, "%s", subDir);
    return true;
}
