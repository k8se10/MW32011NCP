// wait_coalescing_x64.cpp -- render/backend Sleep(1) precision + archive
// worker priority boost during a burst, x64-only.
//
// ATTRIBUTION: the wait-coalescing/precise-timer algorithm and the archive
// burst-detection/thread-priority-boost logic below are ported, with real
// credit, from legoliamneeson/MW3_Standalone_D3D9_Project
// (github.com/legoliamneeson/MW3_Standalone_D3D9_Project, src/runtime.hpp,
// the HookSleep/HookWaitForSingleObject/PreciseRendererSleep1/
// PreciseRendererEventWait/MarkArchiveBurstLean functions) -- a real external
// reference implementation this project independently evaluated (the "D3D9
// optimizer repo evaluation" round, 2026-09-16). That project's own README
// states no redistribution license is asserted for this specific runtime
// source; credited here, in README.md's Credits section, and via a
// Co-Authored-By line on the commit that ports it, per direct instruction.
//
// WHY THE SOURCE PROJECT'S HARDCODED RVAs ARE SAFE TO TREAT AS REAL LEADS
// HERE (unlike a blind copy): a dedicated investigation this session
// confirmed, via direct raw-byte comparison against our OWN live iw5sp.exe
// (not assumed), that all of the source project's real code-signature RVAs
// for this technique (MW3_BACKEND_SLEEP1_SIGNATURE, MW3_RENDER_SLEEP1_SIGNATURE,
// MW3_RENDER_WAIT1_SIGNATURE, and the worker-wait call site) match our binary
// byte-for-byte at the identical address -- our iw5sp.exe's own SHA256 differs
// from the source project's stated target (almost certainly a stripped/
// re-signed Authenticode certificate difference, not a code difference; PE
// identity -- Machine/TimeDateStamp/SizeOfImage -- matches exactly). This
// project's own signature-scanning policy (CLAUDE.md SS5/SS10.3) is used
// below regardless, not because these addresses are in doubt, but because
// it's the same "verify before hooking, fail loudly rather than jump to
// garbage" standard every other x64 hook in this codebase already gets, and
// because the same call sites may legitimately shift on a genuinely
// different future game update -- signature-scanning degrades gracefully
// (this feature just doesn't activate) rather than crashing in that case.
//
// WHAT'S DIFFERENT FROM THE SOURCE PROJECT'S OWN MECHANISM: it IAT-patches
// Sleep/WaitForSingleObject process-wide via Detours. This uses a single
// MinHook detour on the real kernel32 exports instead (resolved via
// GetProcAddress, the same "well-known, always-valid OS export" precedent
// this project's own D3D9 vtable hooks already use) -- narrower, and
// consistent with this project's own established hooking conventions.

#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <intrin.h>

#include "../third_party/minhook/include/MinHook.h"
#include "signature_scan.h"
#include "mod_config.h"

extern void LogFromController(const char* msg);

namespace {

double ClampD(double v, double lo, double hi) { return v < lo ? lo : (v > hi ? hi : v); }

// Real byte patterns, ported verbatim from the source project's own
// MW3_BACKEND_SLEEP1_SIGNATURE/MW3_RENDER_SLEEP1_SIGNATURE/
// MW3_RENDER_WAIT1_SIGNATURE constants (runtime.hpp) -- independently
// byte-confirmed against our own live binary this session (see this file's
// own header comment). The two Sleep(1) patterns intentionally do NOT
// wildcard their own CALL displacement (unlike this project's usual
// convention) -- both share an identical first 5 bytes and a generically-
// shaped CALL opcode, so wildcarding the displacement would collide the two
// into one ambiguous pattern; the literal displacement bytes are what makes
// each one uniquely resolvable. A future binary update changing either
// displacement simply fails this specific scan (feature doesn't activate),
// same graceful-degradation the rest of this project's signature scanning
// already relies on.
constexpr char kBackendSleep1Signature[] = "B9 01 00 00 00 E8 9D BF 08 00";
constexpr char kRenderSleep1Signature[] = "B9 01 00 00 00 E8 7B EB 0B 00";
// RIP-relative operands wildcarded (the [event] global and the
// WaitForSingleObject IAT slot) -- both are real addresses that could shift
// on a relink even if this exact instruction sequence doesn't.
constexpr char kRenderWait1Signature[] = "48 8B 0D ?? ?? ?? ?? BA 01 00 00 00 FF 15 ?? ?? ?? ??";
constexpr char kWorkerWaitSignature[] = "48 8B 0D ?? ?? ?? ?? BA FF FF FF FF FF 15 ?? ?? ?? ??";

uintptr_t g_backendSleep1ReturnAddr = 0;
uintptr_t g_renderSleep1ReturnAddr = 0;
uintptr_t g_renderWait1ReturnAddr = 0;
uintptr_t g_workerWaitReturnAddr = 0;

using SleepFn = VOID(WINAPI*)(DWORD);
using WaitForSingleObjectFn = DWORD(WINAPI*)(HANDLE, DWORD);
SleepFn g_realSleep = nullptr;
WaitForSingleObjectFn g_realWaitForSingleObject = nullptr;

// ---- Precise-wait constants, ported verbatim (runtime.hpp) -----------------
constexpr double kPreciseInitialRequestMs = 0.60;
constexpr double kPreciseTargetActualMs = 0.90;
constexpr double kPreciseMinRequestMs = 0.20;
constexpr double kPreciseMaxRequestMs = 0.90;
constexpr double kPreciseCalibrationGain = 0.22;
constexpr uint32_t kPreciseCalibrationSamples = 64;
constexpr double kRenderCoalesce2Ms = 2.00;
constexpr double kArchiveCoalesce4Ms = 4.00;
constexpr uint32_t kCoalesceStreak2Ms = 3;
constexpr uint32_t kCoalesceStreak4Ms = 8;
constexpr uint32_t kIsolatedSyncCoalesceStreak = 8;
constexpr double kArchiveBurstWindowMs = 6.00;
constexpr uint64_t kArchiveBurstMinBytes = 256ull * 1024ull;
constexpr uint64_t kArchiveBurstMinCalls = 32;
constexpr double kArchivePriorityIdleGapMs = 8.0;

thread_local HANDLE g_tlsHighResTimer = nullptr;
thread_local bool g_tlsHighResTimerAttempted = false;
thread_local double g_tlsWaitRequestMs = kPreciseInitialRequestMs;
thread_local double g_tlsSleepRequestMs = kPreciseInitialRequestMs;
thread_local uint32_t g_tlsWaitCalibrationSamples = 0;
thread_local uint32_t g_tlsSleepCalibrationSamples = 0;
thread_local uint32_t g_tlsRendererTimeoutStreak = 0;

thread_local uint64_t g_tlsArchiveBurstBytes = 0;
thread_local uint64_t g_tlsArchiveBurstCalls = 0;
int64_t g_archiveBurstUntilQpc = 0; // read/written under normal x86-relaxed races, same as source

thread_local bool g_tlsArchivePriorityBoosted = false;
thread_local int g_tlsArchiveOriginalPriority = THREAD_PRIORITY_NORMAL;
thread_local int64_t g_tlsArchiveLastMarkQpc = 0;

int64_t QpcNowRaw()
{
    LARGE_INTEGER q{};
    QueryPerformanceCounter(&q);
    return q.QuadPart;
}

double QpcElapsedMs(int64_t begin, int64_t end)
{
    static LARGE_INTEGER frequency = [] { LARGE_INTEGER f{}; QueryPerformanceFrequency(&f); return f; }();
    if (frequency.QuadPart <= 0 || end < begin) return 0.0;
    return static_cast<double>(end - begin) * 1000.0 / static_cast<double>(frequency.QuadPart);
}

int64_t QpcTicksFromMs(double ms)
{
    static LARGE_INTEGER frequency = [] { LARGE_INTEGER f{}; QueryPerformanceFrequency(&f); return f; }();
    if (frequency.QuadPart <= 0) return 0;
    return static_cast<int64_t>(static_cast<double>(frequency.QuadPart) * ms / 1000.0);
}

HANDLE GetThreadHighResTimer()
{
    if (g_tlsHighResTimer != nullptr) return g_tlsHighResTimer;
    if (g_tlsHighResTimerAttempted) return nullptr;
    g_tlsHighResTimerAttempted = true;
#ifndef CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
#define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x00000002
#endif
    g_tlsHighResTimer = CreateWaitableTimerExW(nullptr, nullptr,
        CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_MODIFY_STATE | SYNCHRONIZE);
    return g_tlsHighResTimer;
}

void CalibrateTimerRequest(double& requestMs, double actualMs)
{
    if (actualMs <= 0.0 || actualMs > 8.0) return;
    const double error = kPreciseTargetActualMs - actualMs;
    requestMs = ClampD(requestMs + error * kPreciseCalibrationGain, kPreciseMinRequestMs, kPreciseMaxRequestMs);
}

bool ArmPreciseTimer(HANDLE timer, double requestMs)
{
    if (timer == nullptr) return false;
    (void)WaitForSingleObject(timer, 0);
    LARGE_INTEGER due{};
    const double clamped = ClampD(requestMs, kPreciseMinRequestMs, kArchiveCoalesce4Ms);
    LONGLONG hundredNs = static_cast<LONGLONG>(clamped * 10000.0);
    if (hundredNs < 1) hundredNs = 1;
    due.QuadPart = -hundredNs;
    return SetWaitableTimer(timer, &due, 0, nullptr, nullptr, FALSE) != FALSE;
}

bool ArchiveBurstActiveForCoalescing()
{
    const int64_t until = g_archiveBurstUntilQpc;
    if (until <= 0) return false;
    return QpcNowRaw() <= until;
}

void RestoreArchiveThreadPriorityIfCurrent()
{
    if (!g_tlsArchivePriorityBoosted) return;
    SetThreadPriority(GetCurrentThread(), g_tlsArchiveOriginalPriority);
    g_tlsArchivePriorityBoosted = false;
}

void ResetArchiveBurstTlsOnWorkerIdle()
{
    RestoreArchiveThreadPriorityIfCurrent();
    g_tlsArchiveBurstBytes = 0;
    g_tlsArchiveBurstCalls = 0;
    g_tlsArchiveLastMarkQpc = 0;
}

void PreciseRendererSleep1()
{
    HANDLE timer = GetThreadHighResTimer();
    if (timer == nullptr || !ArmPreciseTimer(timer, g_tlsSleepRequestMs)) {
        if (g_realSleep != nullptr) g_realSleep(1);
        return;
    }
    const bool calibrating = g_tlsSleepCalibrationSamples < kPreciseCalibrationSamples;
    const int64_t begin = calibrating ? QpcNowRaw() : 0;
    WaitForSingleObject(timer, INFINITE);
    if (calibrating) {
        const int64_t end = QpcNowRaw();
        CalibrateTimerRequest(g_tlsSleepRequestMs, QpcElapsedMs(begin, end));
        ++g_tlsSleepCalibrationSamples;
    }
}

DWORD PreciseRendererEventWait(HANDLE object, DWORD milliseconds)
{
    if (g_realWaitForSingleObject == nullptr) return WAIT_FAILED;

    const DWORD immediate = g_realWaitForSingleObject(object, 0);
    if (immediate != WAIT_TIMEOUT) {
        g_tlsRendererTimeoutStreak = 0;
        return immediate;
    }

    double requestMs = g_tlsWaitRequestMs;
    bool coalesced = false;
    if (g_modConfig.waitCoalescingEnabled && g_tlsRendererTimeoutStreak >= kCoalesceStreak2Ms) {
        const bool archiveBurst = ArchiveBurstActiveForCoalescing();
        if (archiveBurst && g_tlsRendererTimeoutStreak >= kCoalesceStreak4Ms) {
            requestMs = kArchiveCoalesce4Ms;
            coalesced = true;
        } else if (archiveBurst || g_tlsRendererTimeoutStreak >= kIsolatedSyncCoalesceStreak) {
            requestMs = kRenderCoalesce2Ms;
            coalesced = true;
        }
    }

    HANDLE timer = GetThreadHighResTimer();
    if (timer == nullptr || !ArmPreciseTimer(timer, requestMs)) {
        return g_realWaitForSingleObject(object, milliseconds);
    }

    const bool calibrating = !coalesced && g_tlsWaitCalibrationSamples < kPreciseCalibrationSamples;
    const int64_t begin = calibrating ? QpcNowRaw() : 0;

    HANDLE objects[2] = { object, timer };
    const DWORD result = WaitForMultipleObjects(2, objects, FALSE, INFINITE);

    if (result == WAIT_OBJECT_0) {
        CancelWaitableTimer(timer);
        WaitForSingleObject(timer, 0);
        g_tlsRendererTimeoutStreak = 0;
        return WAIT_OBJECT_0;
    }
    if (result == WAIT_OBJECT_0 + 1) {
        ++g_tlsRendererTimeoutStreak;
        if (calibrating) {
            const int64_t end = QpcNowRaw();
            CalibrateTimerRequest(g_tlsWaitRequestMs, QpcElapsedMs(begin, end));
            ++g_tlsWaitCalibrationSamples;
        }
        return WAIT_TIMEOUT;
    }
    if (result == WAIT_ABANDONED_0) return WAIT_ABANDONED_0;
    return result;
}

VOID WINAPI Hook_Sleep(DWORD milliseconds)
{
    if (g_modConfig.waitCoalescingEnabled && milliseconds == 1) {
        const uintptr_t returnAddr = reinterpret_cast<uintptr_t>(_ReturnAddress());
        if (returnAddr == g_backendSleep1ReturnAddr) {
            SwitchToThread();
            return;
        }
        if (returnAddr == g_renderSleep1ReturnAddr) {
            PreciseRendererSleep1();
            return;
        }
    }
    if (g_realSleep != nullptr) g_realSleep(milliseconds);
}

DWORD WINAPI Hook_WaitForSingleObject(HANDLE object, DWORD milliseconds)
{
    if (g_realWaitForSingleObject == nullptr) return WAIT_FAILED;

    const uintptr_t returnAddr = reinterpret_cast<uintptr_t>(_ReturnAddress());

    if (returnAddr == g_workerWaitReturnAddr) {
        // Verified archive/job worker idle boundary -- restore burst-only
        // priority and clear burst state before the worker actually blocks.
        ResetArchiveBurstTlsOnWorkerIdle();
    }

    if (!g_modConfig.waitCoalescingEnabled
        || milliseconds != 1
        || returnAddr != g_renderWait1ReturnAddr
        || object == nullptr
        || object == INVALID_HANDLE_VALUE) {
        return g_realWaitForSingleObject(object, milliseconds);
    }

    return PreciseRendererEventWait(object, milliseconds);
}

// Resolves one signature and returns the address immediately AFTER the
// matched bytes (i.e. the real return address a CALL from inside that
// pattern would produce) -- computed from the pattern's own real matched
// length, never a hardcoded RVA.
uintptr_t ResolveReturnAddress(const char* pattern, const char* label)
{
    SigScan::Result r = SigScan::FindPatternInMainModule(pattern);
    if (!r.found) {
        char buf[192];
        sprintf_s(buf, "[wait-coalescing] signature did not resolve for %s -- that call site's coalescing stays off, "
            "no other hook affected", label);
        LogFromController(buf);
        return 0;
    }
    // Count real (non-space) byte tokens in the pattern to get its true
    // matched length -- each token (hex pair or "??") is one byte.
    size_t byteCount = 0;
    for (const char* p = pattern; *p != '\0'; ) {
        while (*p == ' ') ++p;
        if (*p == '\0') break;
        ++byteCount;
        while (*p != ' ' && *p != '\0') ++p;
    }
    const uintptr_t returnAddr = r.address + byteCount;
    char buf[192];
    sprintf_s(buf, "[wait-coalescing] %s resolved @ 0x%llX (return addr 0x%llX)",
        label, static_cast<unsigned long long>(r.address), static_cast<unsigned long long>(returnAddr));
    LogFromController(buf);
    return returnAddr;
}

} // namespace

void NotifyArchiveIoActivityX64(unsigned int bytes)
{
    if (!g_modConfig.waitCoalescingEnabled || bytes == 0) return;

    g_tlsArchiveBurstBytes += bytes;
    ++g_tlsArchiveBurstCalls;
    if (g_tlsArchiveBurstBytes < kArchiveBurstMinBytes && g_tlsArchiveBurstCalls < kArchiveBurstMinCalls) {
        return;
    }

    const int64_t now = QpcNowRaw();
    if (g_tlsArchiveLastMarkQpc > 0 && QpcElapsedMs(g_tlsArchiveLastMarkQpc, now) > kArchivePriorityIdleGapMs) {
        RestoreArchiveThreadPriorityIfCurrent();
    }

    g_tlsArchiveLastMarkQpc = now;
    g_tlsArchiveBurstBytes = 0;
    g_tlsArchiveBurstCalls = 0;
    g_archiveBurstUntilQpc = now + QpcTicksFromMs(kArchiveBurstWindowMs);

    if (!g_tlsArchivePriorityBoosted) {
        const int current = GetThreadPriority(GetCurrentThread());
        if (current != THREAD_PRIORITY_ERROR_RETURN
            && current < THREAD_PRIORITY_ABOVE_NORMAL
            && SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL) != FALSE) {
            g_tlsArchiveOriginalPriority = current;
            g_tlsArchivePriorityBoosted = true;
        }
    }
}

void InstallWaitCoalescingHooksX64()
{
    g_backendSleep1ReturnAddr = ResolveReturnAddress(kBackendSleep1Signature, "backend Sleep(1) poll");
    g_renderSleep1ReturnAddr = ResolveReturnAddress(kRenderSleep1Signature, "render Sleep(1) poll");
    g_renderWait1ReturnAddr = ResolveReturnAddress(kRenderWait1Signature, "render WaitForSingleObject(1)");
    g_workerWaitReturnAddr = ResolveReturnAddress(kWorkerWaitSignature, "archive/job worker idle wait");

    if (g_backendSleep1ReturnAddr == 0 && g_renderSleep1ReturnAddr == 0
        && g_renderWait1ReturnAddr == 0 && g_workerWaitReturnAddr == 0) {
        LogFromController("[wait-coalescing] no signatures resolved -- skipping hook install entirely");
        return;
    }

    HMODULE kernel32 = GetModuleHandleA("kernel32.dll");
    void* realSleep = kernel32 ? reinterpret_cast<void*>(GetProcAddress(kernel32, "Sleep")) : nullptr;
    void* realWait = kernel32 ? reinterpret_cast<void*>(GetProcAddress(kernel32, "WaitForSingleObject")) : nullptr;
    if (!realSleep || !realWait) {
        LogFromController("[wait-coalescing] FATAL: GetProcAddress(kernel32, Sleep/WaitForSingleObject) failed");
        return;
    }

    MH_STATUS s1 = MH_CreateHook(realSleep, reinterpret_cast<void*>(&Hook_Sleep),
        reinterpret_cast<void**>(&g_realSleep));
    MH_STATUS s2 = (s1 == MH_OK) ? MH_EnableHook(realSleep) : s1;
    MH_STATUS s3 = MH_CreateHook(realWait, reinterpret_cast<void*>(&Hook_WaitForSingleObject),
        reinterpret_cast<void**>(&g_realWaitForSingleObject));
    MH_STATUS s4 = (s3 == MH_OK) ? MH_EnableHook(realWait) : s3;

    char buf[192];
    sprintf_s(buf, "[wait-coalescing] Sleep hook create=%d enable=%d, WaitForSingleObject hook create=%d enable=%d",
        static_cast<int>(s1), static_cast<int>(s2), static_cast<int>(s3), static_cast<int>(s4));
    LogFromController(buf);
}
