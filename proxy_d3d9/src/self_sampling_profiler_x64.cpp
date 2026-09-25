// self_sampling_profiler_x64.cpp -- added 2026-09-26, a real, repeated in-process
// sampling profiler, built after two real limitations of this project's other live-
// inspection tools were hit in the same session: (1) an attached x64dbg session
// crashes reproducibly against this game (see known_issues_x64.md issue #4's newest
// round and the project's own persistent feedback_no_autonomous_live_debugger memory
// -- confirmed across two different x64dbg builds, so not build-specific); (2) even
// TriggerSelfMemoryDumpX64's self-triggered MiniDumpWriteDump call (no external
// handle, no debugger, running from inside the already-injected DLL) fails with
// ERROR_INVALID_PARAMETER for its fuller flag combinations under conditions not yet
// pinned down, and even when it DOES succeed (the MiniDumpNormal fallback tier), a
// single-instant snapshot can't show a SUSTAINED performance difference -- every
// background/worker thread is caught mid-wait at essentially any single instant,
// regardless of overall frame cost, so two dumps a few seconds apart showed
// byte-identical idle thread stacks and told us nothing about the actual
// pause-vs-gameplay regression this tool exists to help chase.
//
// Direct user framing for why this exists: "why not do a live scanning approach in
// our dll with stealth measures to counter anti debug etc." The "stealth" half of
// that framing doesn't actually apply here, and it's worth being precise about why:
// this profiler does not attach as an external debugger at all -- it never calls
// DebugActiveProcess, never establishes a debug object/debug port, and never sets
// PEB->BeingDebugged. It uses SuspendThread/GetThreadContext/ResumeThread against
// this SAME process's own sibling threads, which is a completely ordinary, widely
// used pattern (every real sampling profiler -- Superluminal, Very Sleepy, VTune --
// and every language runtime's GC implementation does exactly this) that has nothing
// to do with the DEBUG_PROCESS/DEBUG_OBJECT machinery anti-debug techniques like
// ThreadHideFromDebugger or IsDebuggerPresent actually target. So this isn't "the
// same live-inspection technique, but stealthier" -- it's a genuinely different
// mechanism that was never in that fight to begin with, which is why it should be
// safe where an attached external debugger reproducibly wasn't.
//
// WHAT IT DOES: on trigger (F10), spawns a dedicated worker thread that repeatedly,
// over a fixed window, walks every OTHER thread in this process (via a
// CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD) enumeration refreshed each round, since
// this project's own background threads and the game's own worker pool can come and
// go), briefly suspends each one just long enough to read its RIP (CONTEXT_CONTROL is
// sufficient and keeps the suspend window as short as possible), and resumes it
// immediately. Each sample is bucketed by (thread id, module+RVA) resolved against a
// module list snapshotted once at profile start (TH32CS_SNAPMODULE) -- no DbgHelp
// symbol resolution is used at all, sidestepping whatever's failing inside
// MiniDumpWriteDump's own internal walk entirely. At the end of the window, writes a
// plain-text report: per thread, which module+RVA the sampler caught it at most often
// across all samples -- a real, if coarse, sampling-profiler "where is time actually
// going" answer, built from nothing but ordinary same-process thread introspection.
//
// SAFETY: this briefly halts other threads one at a time, hundreds of times over a
// few seconds -- a real, if small, perf cost while it runs (typical SuspendThread/
// GetThreadContext/ResumeThread round-trip is low-microseconds; this is the standard
// cost every sampling profiler accepts). Never suspends its own thread. Skips a
// thread cleanly (logs and continues) if OpenThread/SuspendThread/GetThreadContext
// fails for it rather than aborting the whole run -- a single uncooperative thread
// (e.g. one already suspended by something else) must not blank out every other
// thread's data for the same window.

#include <windows.h>
#include <tlhelp32.h>
#include <cstdint>
#include <cstdio>
#include <vector>
#include <map>
#include <algorithm>

extern void LogFromController(const char* msg); // dllmain.cpp

namespace {

constexpr DWORD kProfileDurationMs = 3000;
constexpr DWORD kSampleIntervalMs = 20;
constexpr int kTopEntriesPerThread = 5;

struct ModuleRange {
    uintptr_t base;
    uintptr_t end;
    char name[64];
};

struct SampleKey {
    DWORD threadId;
    uintptr_t moduleBase; // 0 if unresolved to any known module
    uintptr_t rva;        // offset from moduleBase, or the raw address if unresolved

    bool operator<(const SampleKey& other) const {
        if (threadId != other.threadId) return threadId < other.threadId;
        if (moduleBase != other.moduleBase) return moduleBase < other.moduleBase;
        return rva < other.rva;
    }
};

std::vector<ModuleRange> SnapshotModules(DWORD pid)
{
    std::vector<ModuleRange> modules;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
    if (snap == INVALID_HANDLE_VALUE) return modules;

    MODULEENTRY32 me = {};
    me.dwSize = sizeof(me);
    if (Module32First(snap, &me)) {
        do {
            ModuleRange r;
            r.base = reinterpret_cast<uintptr_t>(me.modBaseAddr);
            r.end = r.base + me.modBaseSize;
            strncpy_s(r.name, me.szModule, _TRUNCATE);
            modules.push_back(r);
        } while (Module32Next(snap, &me));
    }
    CloseHandle(snap);
    return modules;
}

const ModuleRange* ResolveModule(const std::vector<ModuleRange>& modules, uintptr_t addr)
{
    for (const auto& m : modules) {
        if (addr >= m.base && addr < m.end) return &m;
    }
    return nullptr;
}

// One sampling round: enumerates threads fresh (this project's own background
// threads and the game's real worker pool can start/stop between rounds), briefly
// suspends each one, reads RIP, resumes. Never touches selfTid (the profiler's own
// worker thread) or the calling process's main WndProc thread while it's inside this
// very function (not applicable here since this runs on its own dedicated thread).
void SampleAllThreadsOnce(DWORD pid, DWORD selfTid, std::map<SampleKey, int>& histogram,
                           const std::vector<ModuleRange>& modules)
{
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snap == INVALID_HANDLE_VALUE) return;

    THREADENTRY32 te = {};
    te.dwSize = sizeof(te);
    if (!Thread32First(snap, &te)) {
        CloseHandle(snap);
        return;
    }

    do {
        if (te.th32OwnerProcessID != pid) continue;
        if (te.th32ThreadID == selfTid) continue;

        HANDLE hThread = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION,
                                     FALSE, te.th32ThreadID);
        if (!hThread) continue; // thread may have already exited between enum and open -- skip, not fatal

        if (SuspendThread(hThread) != static_cast<DWORD>(-1)) {
            CONTEXT ctx = {};
            ctx.ContextFlags = CONTEXT_CONTROL;
            if (GetThreadContext(hThread, &ctx)) {
                uintptr_t rip = static_cast<uintptr_t>(ctx.Rip);
                const ModuleRange* mod = ResolveModule(modules, rip);
                SampleKey key;
                key.threadId = te.th32ThreadID;
                key.moduleBase = mod ? mod->base : 0;
                key.rva = mod ? (rip - mod->base) : rip;
                histogram[key]++;
            }
            ResumeThread(hThread);
        }
        CloseHandle(hThread);
    } while (Thread32Next(snap, &te));

    CloseHandle(snap);
}

DWORD WINAPI SelfSamplingProfilerThreadProc(LPVOID)
{
    LogFromController("[self-profile] Starting live in-process sampling profile (no external debugger, no MiniDumpWriteDump)");

    DWORD pid = GetCurrentProcessId();
    DWORD selfTid = GetCurrentThreadId();
    std::vector<ModuleRange> modules = SnapshotModules(pid);

    std::map<SampleKey, int> histogram;
    std::map<DWORD, int> samplesPerThread;
    DWORD startTick = GetTickCount();
    int totalRounds = 0;

    while (GetTickCount() - startTick < kProfileDurationMs) {
        SampleAllThreadsOnce(pid, selfTid, histogram, modules);
        totalRounds++;
        Sleep(kSampleIntervalMs);
    }

    for (const auto& kv : histogram) samplesPerThread[kv.first.threadId] += kv.second;

    char path[MAX_PATH];
    SYSTEMTIME st;
    GetLocalTime(&st);
    sprintf_s(path, "selfprofile_%04d%02d%02d_%02d%02d%02d.txt",
              st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);

    FILE* f = nullptr;
    if (fopen_s(&f, path, "w") != 0 || !f) {
        LogFromController("[self-profile] Could not open report file for writing");
        return 0;
    }

    fprintf(f, "Self-sampling profile: %d rounds over ~%lu ms (interval=%lu ms)\n",
            totalRounds, kProfileDurationMs, kSampleIntervalMs);
    fprintf(f, "Threads observed: %zu\n\n", samplesPerThread.size());

    for (const auto& tkv : samplesPerThread) {
        DWORD tid = tkv.first;
        int totalForThread = tkv.second;
        fprintf(f, "Thread %lu (%d samples):\n", tid, totalForThread);

        // Collect this thread's entries, sorted by count descending -- top N only.
        std::vector<std::pair<SampleKey, int>> entries;
        for (const auto& kv : histogram) {
            if (kv.first.threadId == tid) entries.push_back(kv);
        }
        std::sort(entries.begin(), entries.end(),
                  [](const auto& a, const auto& b) { return a.second > b.second; });

        int shown = 0;
        for (const auto& e : entries) {
            if (shown++ >= kTopEntriesPerThread) break;
            const SampleKey& key = e.first;
            double pct = totalForThread > 0 ? (100.0 * e.second / totalForThread) : 0.0;
            if (key.moduleBase != 0) {
                const ModuleRange* mod = ResolveModule(modules, key.moduleBase);
                fprintf(f, "    %5.1f%% (%d) - %s+0x%llx\n", pct, e.second,
                        mod ? mod->name : "?", static_cast<unsigned long long>(key.rva));
            } else {
                fprintf(f, "    %5.1f%% (%d) - <unresolved> 0x%llx\n", pct, e.second,
                        static_cast<unsigned long long>(key.rva));
            }
        }
        fprintf(f, "\n");
    }

    fclose(f);

    char logBuf[320];
    sprintf_s(logBuf, "[self-profile] Wrote %s (%d rounds, %zu threads)", path, totalRounds, samplesPerThread.size());
    LogFromController(logBuf);
    return 0;
}

} // namespace

void TriggerSelfSamplingProfileX64()
{
    // Runs on its own dedicated thread so the profile window doesn't block the
    // calling (WndProc) thread for the full ~3 seconds -- same reasoning this
    // project's other background work (controller poll, vibration, log-flush)
    // already uses dedicated threads instead of blocking a shared one.
    HANDLE h = CreateThread(nullptr, 0, SelfSamplingProfilerThreadProc, nullptr, 0, nullptr);
    if (h) CloseHandle(h);
    else LogFromController("[self-profile] Failed to start profiler thread");
}
