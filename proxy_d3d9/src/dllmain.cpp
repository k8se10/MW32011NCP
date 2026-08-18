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
#include "overlay_hud.h"

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
DWORD g_lastLogFlushTickMs = 0;

// Live-reported 2026-08-08 (performance pass targeting worst-case circa-2008
// hardware): the original Log() called fflush() on EVERY single call, unconditionally
// -- a real, synchronous disk write forced on every one of this project's ~180 log
// call sites, several of which (issue #67 -- proxy_d3d9.log had grown to 22GB) fire
// unconditionally on every frame or every menu item. On slow storage (spinning HDDs,
// the realistic worst case for a 2011 game's minimum-spec audience) a forced flush
// can cost single-digit milliseconds EACH -- multiplied across dozens of log lines a
// frame, this alone could visibly tank frame time independent of anything else this
// project does. Fixed by flushing at most once every kLogFlushIntervalMs instead of
// every call -- stdio's own internal buffering absorbs everything in between.
//
// "We still need conclusive logs" (explicit direction) -- a periodic-only flush risks
// losing the last <1s of buffered lines if the process is killed HARD (not a clean
// exit) before the next scheduled flush, which would be a real regression for a log
// whose whole purpose is diagnosing crashes. Solved with a real, additive
// AddVectoredExceptionHandler (confirmed safe: this engine already registers its own,
// see re_notes/known_issues.md's iw5sp research citing FUN_006c0ec0 -- vectored
// handlers are designed to coexist, unlike SetUnhandledExceptionFilter's single
// global slot, which this project deliberately does NOT use to avoid any risk of
// displacing the game's own crash reporter) that flushes the log and always returns
// EXCEPTION_CONTINUE_SEARCH -- never actually handles/suppresses anything, purely
// observes and flushes before the exception continues exactly as it would have
// without this handler installed at all. This means a genuine crash still has a
// fully flushed log up to the last line written, while ordinary per-frame diagnostic
// spam no longer pays a disk-flush cost on every single call.
constexpr DWORD kLogFlushIntervalMs = 1000;

LONG WINAPI FlushLogOnCrash(EXCEPTION_POINTERS* /*exceptionInfo*/)
{
    if (g_log) fflush(g_log);
    return EXCEPTION_CONTINUE_SEARCH; // never handle -- observe-and-flush only
}

void LogInit()
{
    char path[MAX_PATH];
    GetModuleFileNameA(nullptr, path, MAX_PATH); // full path of the .exe that loaded us
    char* lastSlash = strrchr(path, '\\');
    if (lastSlash) *(lastSlash + 1) = '\0';
    strcat_s(path, "proxy_d3d9.log");
    // Live-confirmed 2026-08-16 (real user's proxy_d3d9.log): "a" (append) mode meant
    // this file was NEVER trimmed -- it had grown to 2.6GB / 22.5M lines across every
    // launch since install, not just one session. Directly implicated in a real,
    // reported "random hitching in extended play sessions" bug: appending to an
    // already-multi-GB, likely-fragmented file is a much worse fit for "random,
    // gets-worse-over-time" stalls than anything session-local, and explains why the
    // report came in against an OLDER version too (this has been true since logging
    // was added, not introduced by a recent change). Switched to "w" (truncate) so
    // every launch starts from a clean, small file -- this project's log has always
    // been a live/current-session diagnostic tool (grep'd for recent tags during this
    // exact investigation), never a historical record anyone reads back across
    // sessions, so there's no loss from not keeping old sessions around.
    //
    // _fsopen with _SH_DENYWR (not fopen_s, which opens exclusively on Windows) so the
    // log can still be read live while the game is running -- needed for diagnosing
    // bugs where the game gets stuck in a bad state and can't be closed normally to
    // release the file.
    g_log = _fsopen(path, "w", _SH_DENYWR);
    if (g_log) {
        fprintf(g_log, "---- proxy_d3d9 attach ----\n");
        fflush(g_log); // always flush this one line -- marks a real session boundary
                         // even if the process crashes before the periodic flush below
                         // ever fires once.
        g_lastLogFlushTickMs = GetTickCount();
        AddVectoredExceptionHandler(1, FlushLogOnCrash); // call FIRST (1), see this
            // block's own comment above for why this is safe alongside the game's own
    }
}

void Log(const char* msg)
{
    if (g_log) {
        fprintf(g_log, "%s\n", msg);
        DWORD now = GetTickCount();
        if (now - g_lastLogFlushTickMs >= kLogFlushIntervalMs) {
            fflush(g_log);
            g_lastLogFlushTickMs = now;
        }
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
// KNOWN, ACCEPTED LIMITATION (2026-07-17 pre-release review): no null-check on
// g_real_##name before the jmp -- if the real system d3d9.dll were ever missing one
// of these obscure exports (ResolveRealExports logs a warning but treats it as
// non-fatal, since MW3 2011 is not expected to call these directly) AND the game
// somehow called it anyway, this jumps through a null pointer and crashes. Not fixed
// because there isn't a clean way to: these are deliberately UNKNOWN-ARITY stdcall/
// cdecl exports (the entire point of the tail-jump approach is not needing to know
// each one's real signature) -- a "graceful" fallback can't safely `ret` without
// knowing how many bytes of the caller's stack to clean up, so a naive null-check
// would either need to know each function's real signature anyway (defeating the
// purpose) or risk corrupting the caller's stack on return, which is worse than the
// crash it would be guarding against. Real-world risk is low: these are all standard
// exports present on essentially any genuine Windows d3d9.dll. Direct3DCreate9 itself
// (the one export MW3 unconditionally needs) IS correctly guarded -- ResolveRealExports
// returns false and DllMain aborts entirely if that specific one is missing.
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
        LoadOverlayFonts(hModule); // 2026-07-31 follow-up -- self-contained Barlow
            // Condensed, embedded in this DLL rather than depending on a system
            // install; logs its own success/failure, never fatal to DLL init either way
        if (!LoadRealD3D9()) return FALSE;
        if (!ResolveRealExports()) return FALSE;
        Log("proxy_d3d9 init OK — analog movement/look hooks installing.");
        InstallAnalogInputHooks(); // task #5 -- see analog_input_hooks.cpp
        break;
    case DLL_PROCESS_DETACH:
        UnloadOverlayFonts(); // release the private font resource before this DLL's
            // own memory (where the embedded font data lives) goes away
        Log("proxy_d3d9 detach");
        if (g_log) fclose(g_log);
        break;
    }
    return TRUE;
}
