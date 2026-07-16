// proxy_d3d9 — MW3 (iw5sp.exe / iw5mp.exe) native controller mod injection point.
//
// This DLL ships as "d3d9.dll" next to iw5sp.exe/iw5mp.exe. The game's normal DLL
// search order loads it before the real system d3d9.dll, giving us code execution
// at process start with zero external injector.
//
// All non-Direct3DCreate9 exports are pure tail-jump forwards to the real system
// d3d9.dll (resolved by explicit SysWOW64/System32 path, never by bare name, since
// a bare LoadLibraryW(L"d3d9.dll") from inside a DLL already loaded AS "d3d9.dll"
// would resolve back to ourselves via the loader's loaded-module-by-name cache).
// Direct3DCreate9 is the one export we implement directly so we can observe/hook
// the returned IDirect3D9 interface later (CreateDevice -> device vtable -> Present).

#include <windows.h>
#include <cstdio>
#include <share.h>
#include "mod_config.h"

void InstallAnalogInputHooks(); // defined in analog_input_hooks.cpp
extern "C" void HookD3D9CreateDevice(void* realD3D9); // defined in d3d9_hook.cpp

// Deliberately NOT including <d3d9.h>: its prototypes for Direct3DCreate9/D3DPERF_*/etc.
// would collide with the untyped naked-stub exports below (whose whole point is to not
// need the real signatures). IDirect3D9 stays an opaque pointer until task #3 actually
// needs its vtable (CreateDevice -> Present hook) — at that point pull in d3d9.h in a
// separate translation unit that doesn't also define these forwarding stubs.
struct IDirect3D9;

namespace {

HMODULE g_realD3D9 = nullptr;

FILE* g_log = nullptr;

void LogInit()
{
    char path[MAX_PATH];
    GetModuleFileNameA(nullptr, path, MAX_PATH); // full path of the .exe that loaded us
    char* lastSlash = strrchr(path, '\\');
    if (lastSlash) *(lastSlash + 1) = '\0';
    strcat_s(path, "proxy_d3d9.log");
    // _fsopen with _SH_DENYWR (not fopen_s, which opens exclusively on Windows) so the
    // log can still be read live while the game is running -- needed for diagnosing
    // bugs where the game gets stuck in a bad state and can't be closed normally to
    // release the file.
    g_log = _fsopen(path, "a", _SH_DENYWR);
    if (g_log) {
        fprintf(g_log, "---- proxy_d3d9 attach ----\n");
        fflush(g_log);
    }
}

void Log(const char* msg)
{
    if (g_log) {
        fprintf(g_log, "%s\n", msg);
        fflush(g_log);
    }
}

// Loads the real system d3d9.dll by an explicit, unambiguous path so we never
// collide with our own module (which is also named d3d9.dll). GetSystemDirectoryA
// called from this 32-bit process returns ...\System32, but Windows' WOW64 file
// system redirector transparently maps that to ...\SysWOW64 for a 32-bit process's
// actual file access — no manual SysWOW64 path handling needed.
bool LoadRealD3D9()
{
    char sysDir[MAX_PATH];
    GetSystemDirectoryA(sysDir, MAX_PATH);
    strcat_s(sysDir, "\\d3d9.dll");
    g_realD3D9 = LoadLibraryA(sysDir);
    if (!g_realD3D9) {
        char buf[512];
        sprintf_s(buf, "FATAL: failed to load real d3d9.dll from '%s' (err=%lu)", sysDir, GetLastError());
        Log(buf);
        return false;
    }
    char buf[512];
    sprintf_s(buf, "Loaded real d3d9.dll from '%s'", sysDir);
    Log(buf);
    return true;
}

// One resolved function pointer per forwarded (non-Direct3DCreate9) export.
// Populated in LoadRealD3D9Exports(); consumed by the naked tail-jump stubs below.
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

typedef IDirect3D9* (WINAPI* Direct3DCreate9_t)(UINT);
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

    bool ok = true;
    for (auto& e : entries) {
        *e.slot = reinterpret_cast<void*>(GetProcAddress(g_realD3D9, e.name));
        if (!*e.slot) {
            char buf[256];
            sprintf_s(buf, "WARNING: real d3d9.dll missing export '%s' (unexpected, but non-fatal for this exe)", e.name);
            Log(buf);
            // Not fatal: MW3 (2011) is not expected to call these obscure exports directly.
        }
    }

    g_real_Direct3DCreate9 = reinterpret_cast<Direct3DCreate9_t>(GetProcAddress(g_realD3D9, "Direct3DCreate9"));
    if (!g_real_Direct3DCreate9) {
        Log("FATAL: real d3d9.dll missing Direct3DCreate9 — cannot proxy, game will fail to init D3D9.");
        ok = false;
    }
    return ok;
}

} // namespace

// Diagnostic logging entry point for other translation units (e.g.
// analog_input_hooks.cpp) -- Log()/g_log above stay internal-linkage, this is just a
// thin forwarder so hook code can log without duplicating the log-file setup.
void LogFromController(const char* msg)
{
    Log(msg);
}

// ---- Direct3DCreate9: the real interception point --------------------------------
// Implemented (not forwarded) so we can hold onto / hook the returned IDirect3D9
// interface (CreateDevice -> device vtable -> Present) once real hooking begins.
// For now this is a transparent pass-through: identical behavior to vanilla d3d9.dll.
extern "C" __declspec(dllexport) IDirect3D9* WINAPI Direct3DCreate9(UINT SDKVersion)
{
    Log("Direct3DCreate9 called");
    if (!g_real_Direct3DCreate9) return nullptr;
    IDirect3D9* real = g_real_Direct3DCreate9(SDKVersion);
    if (real) {
        HookD3D9CreateDevice(real);
    }
    return real;
}

// ---- Pure tail-jump forwards for every other real d3d9.dll export ----------------
// __declspec(naked) is valid for x86 (this project always targets Win32 — see
// CLAUDE.md, both game binaries are confirmed x86). A bare `jmp` through the
// resolved pointer preserves the original caller's stack exactly as pushed, so the
// real function's own `ret N` returns directly to the ORIGINAL caller — this works
// regardless of the real export's exact argument count/calling convention, so we
// don't need to know or replicate their signatures.
#define FORWARD_STUB(name) \
    extern "C" __declspec(dllexport) __declspec(naked) void WINAPI name() \
    { \
        __asm { jmp dword ptr [g_real_##name] } \
    }

FORWARD_STUB(D3DPERF_BeginEvent)
FORWARD_STUB(D3DPERF_EndEvent)
FORWARD_STUB(D3DPERF_GetStatus)
FORWARD_STUB(D3DPERF_QueryRepeatFrame)
FORWARD_STUB(D3DPERF_SetMarker)
FORWARD_STUB(D3DPERF_SetOptions)
FORWARD_STUB(D3DPERF_SetRegion)
FORWARD_STUB(DebugSetLevel)
FORWARD_STUB(DebugSetMute)
FORWARD_STUB(Direct3D9EnableMaximizedWindowedModeShim)
FORWARD_STUB(Direct3DCreate9Ex)
FORWARD_STUB(Direct3DCreate9On12)
FORWARD_STUB(Direct3DCreate9On12Ex)
FORWARD_STUB(Direct3DShaderValidatorCreate9)
FORWARD_STUB(PSGPError)
FORWARD_STUB(PSGPSampleTexture)

#undef FORWARD_STUB

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID)
{
    switch (reason) {
    case DLL_PROCESS_ATTACH:
        DisableThreadLibraryCalls(hModule);
        LogInit();
        LoadModConfig(); // task #14 -- must run before InstallAnalogInputHooks reads g_modConfig
        if (!LoadRealD3D9()) return FALSE;
        if (!ResolveRealExports()) return FALSE;
        Log("proxy_d3d9 init OK — analog movement/look hooks installing.");
        InstallAnalogInputHooks(); // task #5 -- see analog_input_hooks.cpp
        break;
    case DLL_PROCESS_DETACH:
        Log("proxy_d3d9 detach");
        if (g_log) fclose(g_log);
        break;
    }
    return TRUE;
}
