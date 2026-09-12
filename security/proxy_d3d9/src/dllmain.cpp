// proxy_d3d9 -- MW32011NSP standalone injection point.
//
// Same technique as the sibling MW32011NCP project (its own dllmain.cpp is the
// direct model this file is adapted from): this DLL ships as "d3d9.dll" next to
// iw5sp.exe/iw5mp.exe. The game's normal DLL search order loads it before the
// real system d3d9.dll, giving us code execution at process start with zero
// external injector.
//
// Unlike NCP, this project's fixes don't touch rendering or input at all -- they
// only need the game process's own module base (available immediately) to
// signature-scan for their target functions, so InstallNetcodeFixes() runs
// directly from DLL_PROCESS_ATTACH, with no need to wait for CreateDevice the way
// NCP's own rendering/input hooks do.
//
// All non-Direct3DCreate9 exports are pure tail-jump forwards to the real system
// d3d9.dll -- x64 has no inline-asm/naked-function support in MSVC, so these are
// implemented in forward_stubs_x64.asm (MASM), not inline here. Direct3DCreate9
// itself is implemented directly, purely as a transparent passthrough (this
// project has no need to observe/hook the returned IDirect3D9 interface at all --
// unlike NCP, which hooks CreateDevice for rendering; this project's own fixes
// are all installed before Direct3DCreate9 is even called for the first time).

#include <windows.h>
#include <cstdio>

#include "netcode_fixes.h"
#include "../third_party/minhook/include/MinHook.h"

extern "C" {
void* g_real_D3DPERF_BeginEvent = nullptr;
void* g_real_D3DPERF_EndEvent = nullptr;
void* g_real_D3DPERF_GetStatus = nullptr;
void* g_real_D3DPERF_QueryRepeatFrame = nullptr;
void* g_real_D3DPERF_SetMarker = nullptr;
void* g_real_D3DPERF_SetOptions = nullptr;
void* g_real_D3DPERF_SetRegion = nullptr;
void* g_real_DebugSetLevel = nullptr;
void* g_real_DebugSetMute = nullptr;
void* g_real_Direct3D9EnableMaximizedWindowedModeShim = nullptr;
void* g_real_Direct3DCreate9Ex = nullptr;
void* g_real_Direct3DCreate9On12 = nullptr;
void* g_real_Direct3DCreate9On12Ex = nullptr;
void* g_real_Direct3DShaderValidatorCreate9 = nullptr;
void* g_real_PSGPError = nullptr;
void* g_real_PSGPSampleTexture = nullptr;
}  // extern "C"

namespace {

HMODULE g_realD3D9 = nullptr;
FILE* g_log = nullptr;

bool LoadRealD3D9()
{
    char sysDir[MAX_PATH];
    GetSystemDirectoryA(sysDir, MAX_PATH);
    strcat_s(sysDir, "\\d3d9.dll");
    g_realD3D9 = LoadLibraryA(sysDir);
    return g_realD3D9 != nullptr;
}

typedef void* (WINAPI* Direct3DCreate9_t)(UINT);
Direct3DCreate9_t g_real_Direct3DCreate9 = nullptr;

bool ResolveRealExports()
{
    struct Entry { const char* name; void** slot; };
    Entry entries[] = {
        { "D3DPERF_BeginEvent", &g_real_D3DPERF_BeginEvent },
        { "D3DPERF_EndEvent", &g_real_D3DPERF_EndEvent },
        { "D3DPERF_GetStatus", &g_real_D3DPERF_GetStatus },
        { "D3DPERF_QueryRepeatFrame", &g_real_D3DPERF_QueryRepeatFrame },
        { "D3DPERF_SetMarker", &g_real_D3DPERF_SetMarker },
        { "D3DPERF_SetOptions", &g_real_D3DPERF_SetOptions },
        { "D3DPERF_SetRegion", &g_real_D3DPERF_SetRegion },
        { "DebugSetLevel", &g_real_DebugSetLevel },
        { "DebugSetMute", &g_real_DebugSetMute },
        { "Direct3D9EnableMaximizedWindowedModeShim", &g_real_Direct3D9EnableMaximizedWindowedModeShim },
        { "Direct3DCreate9Ex", &g_real_Direct3DCreate9Ex },
        { "Direct3DCreate9On12", &g_real_Direct3DCreate9On12 },
        { "Direct3DCreate9On12Ex", &g_real_Direct3DCreate9On12Ex },
        { "Direct3DShaderValidatorCreate9", &g_real_Direct3DShaderValidatorCreate9 },
        { "PSGPError", &g_real_PSGPError },
        { "PSGPSampleTexture", &g_real_PSGPSampleTexture },
    };
    for (auto& e : entries) {
        *e.slot = reinterpret_cast<void*>(GetProcAddress(g_realD3D9, e.name));
    }
    g_real_Direct3DCreate9 = reinterpret_cast<Direct3DCreate9_t>(GetProcAddress(g_realD3D9, "Direct3DCreate9"));
    return g_real_Direct3DCreate9 != nullptr;
}

// ---- FixHostServices implementation -----------------------------------------

void* GetGameModuleBase()
{
    return GetModuleHandleA(nullptr);
}

int InstallHookImpl(void* target, void* detour, void** outOriginal)
{
    if (!target) return 0;
    MH_STATUS createStatus = MH_CreateHook(target, detour, outOriginal);
    if (createStatus != MH_OK) return 0;
    MH_STATUS enableStatus = MH_EnableHook(target);
    return enableStatus == MH_OK ? 1 : 0;
}

} // namespace

// Log() is used by both signature_scan.cpp and the fix modules -- plain,
// internal-linkage-free so those separately-compiled translation units can
// declare their own `extern void Log(const char*);` and link against this.
void Log(const char* msg)
{
    if (g_log) {
        fprintf(g_log, "%s\n", msg);
        fflush(g_log); // this project's own log volume is tiny (a handful of
            // lines total, once at startup) compared to NCP's ~180 call sites --
            // an unconditional flush here is not the same class of stutter risk
            // NCP's own issue #87 found, so no need for NCP's periodic-flush-
            // thread machinery.
    }
}

extern "C" __declspec(dllexport) void* WINAPI Direct3DCreate9(UINT SDKVersion)
{
    Log("[nsp] Direct3DCreate9 called (pure passthrough, no rendering hooks)");
    if (!g_real_Direct3DCreate9) return nullptr;
    return g_real_Direct3DCreate9(SDKVersion);
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID /*lpReserved*/)
{
    switch (reason) {
    case DLL_PROCESS_ATTACH: {
        DisableThreadLibraryCalls(hModule);
        errno_t err = fopen_s(&g_log, "proxy_d3d9_nsp.log", "w");
        if (err != 0) g_log = nullptr;
        Log("[nsp] MW32011NSP proxy_d3d9 init -- netcode security fix DLL");

        if (!LoadRealD3D9()) {
            Log("[nsp] FATAL: failed to load real system d3d9.dll -- cannot proxy");
            return FALSE;
        }
        if (!ResolveRealExports()) {
            Log("[nsp] FATAL: real d3d9.dll missing Direct3DCreate9 -- cannot proxy");
            return FALSE;
        }

        MH_Initialize(); // idempotent if already initialized elsewhere in-process

        FixHostServices host;
        host.InstallHook = &InstallHookImpl;
        host.GetGameModuleBase = &GetGameModuleBase;
        host.Log = &Log;
        InstallNetcodeFixes(host);
        break;
    }
    case DLL_PROCESS_DETACH:
        Log("[nsp] proxy_d3d9 detach");
        if (g_log) fclose(g_log);
        break;
    }
    return TRUE;
}
