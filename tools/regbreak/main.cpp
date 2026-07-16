// regbreak — dev-only diagnostic tool (not part of the shipped mod). Attaches to a
// running iw5sp.exe as a real Windows debugger, sets a single software breakpoint
// (INT3) at a given address, waits for the FIRST hit, dumps the full x86 register
// context plus targeted memory probes at candidate offsets off each general-purpose
// register, restores the original byte, and detaches cleanly (DebugActiveProcessStop,
// with kill-on-exit disabled) so the game keeps running completely undisturbed --
// used in place of manually driving x32dbg's GUI when the user is mid-session and the
// inspection needs to happen without interrupting them.
//
// Usage: regbreak.exe <hexAddress> [maxWaitSeconds]
//
// Built for task #16 (aim assist): confirming what the "unaff_ESI"-style implicit
// register context actually points to at FUN_0057d7e0's entry, by breaking there live
// and reading ESI + probing candidate struct offsets already suspected from static
// decompiles (view-origin, aim-assist target state, usercmd-adjacent fields).

#include <windows.h>
#include <tlhelp32.h>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <map>

namespace {

DWORD FindProcessId(const wchar_t* exeName)
{
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return 0;
    PROCESSENTRY32W pe{};
    pe.dwSize = sizeof(pe);
    DWORD pid = 0;
    if (Process32FirstW(snap, &pe)) {
        do {
            if (_wcsicmp(pe.szExeFile, exeName) == 0) { pid = pe.th32ProcessID; break; }
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    return pid;
}

void DumpProbe(HANDLE hProcess, const char* regName, DWORD regValue)
{
    if (regValue == 0) {
        printf("  %s = 0x00000000 (null, skipping probes)\n", regName);
        return;
    }
    printf("  %s = 0x%08lX\n", regName, regValue);

    // Candidate offsets already suspected from static decompiles: usercmd-adjacent
    // fields (+0x1c/+0x1d forwardmove/rightmove-style, +0x20/+0x21, +0x38/+0x3a),
    // aim-assist view-origin (+0xd0/+0xd4/+0xd8, 3 floats), tag-candidate array
    // header (+0x134 start, +0xe34 count), locked-target id (+0xe48), and the
    // real output target-angle fields (+0xe50/+0xe58).
    struct Probe { uint32_t offset; int len; const char* label; };
    static const Probe probes[] = {
        {0x00, 4, "+0x00 (first dword)"},
        {0x08, 4, "+0x08"},
        {0x1c, 1, "+0x1c (usercmd-adjacent?)"},
        {0x1d, 1, "+0x1d (usercmd-adjacent?)"},
        {0x20, 1, "+0x20"},
        {0x21, 1, "+0x21"},
        {0x38, 2, "+0x38"},
        {0x3a, 1, "+0x3a"},
        {0x150, 4, "+0x150 (tag id?)"},
        {0xd0, 4, "+0xd0 (view origin X?)"},
        {0xd4, 4, "+0xd4 (view origin Y?)"},
        {0xd8, 4, "+0xd8 (view origin Z?)"},
        {0x134, 4, "+0x134 (candidate array start?)"},
        {0xe34, 4, "+0xe34 (candidate count?)"},
        {0xe48, 4, "+0xe48 (locked target id?)"},
        {0xe50, 4, "+0xe50 (output target angle X?)"},
        {0xe58, 4, "+0xe58 (output target angle Y?)"},
    };

    for (const auto& p : probes) {
        uint8_t buf[4] = {0, 0, 0, 0};
        SIZE_T bytesRead = 0;
        uintptr_t addr = static_cast<uintptr_t>(regValue) + p.offset;
        BOOL ok = ReadProcessMemory(hProcess, reinterpret_cast<LPCVOID>(addr), buf, p.len, &bytesRead);
        if (!ok || bytesRead != static_cast<SIZE_T>(p.len)) {
            printf("    %s: (unreadable)\n", p.label);
            continue;
        }
        if (p.len == 1) {
            printf("    %-28s = 0x%02X\n", p.label, buf[0]);
        } else if (p.len == 2) {
            uint16_t v = buf[0] | (buf[1] << 8);
            printf("    %-28s = 0x%04X\n", p.label, v);
        } else {
            uint32_t v = buf[0] | (buf[1] << 8) | (buf[2] << 16) | (static_cast<uint32_t>(buf[3]) << 24);
            float f;
            memcpy(&f, &v, 4);
            printf("    %-28s = 0x%08X  (as float: %f)\n", p.label, v, f);
        }
    }
}

} // namespace

int main(int argc, char** argv)
{
    setvbuf(stdout, nullptr, _IONBF, 0);

    if (argc < 2) {
        printf("Usage: regbreak.exe <hexAddress> [maxWaitSeconds]\n");
        return 1;
    }
    uintptr_t targetAddr = static_cast<uintptr_t>(strtoull(argv[1], nullptr, 16));
    DWORD maxWaitMs = (argc >= 3) ? static_cast<DWORD>(strtoul(argv[2], nullptr, 10)) * 1000 : 30000;

    DWORD pid = FindProcessId(L"iw5sp.exe");
    if (pid == 0) {
        printf("iw5sp.exe not found -- launch the game first.\n");
        return 1;
    }
    printf("Found iw5sp.exe, PID %lu. Target breakpoint address: 0x%08zX\n", pid, targetAddr);

    if (!DebugActiveProcess(pid)) {
        printf("DebugActiveProcess failed (%lu) -- run this tool as Administrator, and make sure\n"
               "no other debugger (x32dbg, etc.) is already attached to this process.\n", GetLastError());
        return 1;
    }
    // Critical: without this, detaching would terminate the game.
    DebugSetProcessKillOnExit(FALSE);

    HANDLE hProcess = nullptr;
    std::map<DWORD, HANDLE> threadHandles;
    uint8_t originalByte = 0;
    bool breakpointArmed = false;
    bool done = false;
    DWORD startTick = GetTickCount();

    printf("Attached. Waiting for the initial attach breakpoint, then arming ours...\n");

    while (!done && (GetTickCount() - startTick) < maxWaitMs) {
        DEBUG_EVENT ev{};
        if (!WaitForDebugEvent(&ev, 500)) {
            continue; // timeout, loop again to re-check maxWaitMs
        }

        DWORD continueStatus = DBG_CONTINUE;

        switch (ev.dwDebugEventCode) {
            case CREATE_PROCESS_DEBUG_EVENT:
                hProcess = ev.u.CreateProcessInfo.hProcess;
                if (ev.u.CreateProcessInfo.hThread) {
                    threadHandles[ev.dwThreadId] = ev.u.CreateProcessInfo.hThread;
                }
                if (ev.u.CreateProcessInfo.hFile) CloseHandle(ev.u.CreateProcessInfo.hFile);
                break;

            case CREATE_THREAD_DEBUG_EVENT:
                threadHandles[ev.dwThreadId] = ev.u.CreateThread.hThread;
                break;

            case EXIT_THREAD_DEBUG_EVENT:
                threadHandles.erase(ev.dwThreadId);
                break;

            case EXCEPTION_DEBUG_EVENT: {
                const auto& rec = ev.u.Exception.ExceptionRecord;
                if (rec.ExceptionCode == EXCEPTION_BREAKPOINT) {
                    uintptr_t hitAddr = reinterpret_cast<uintptr_t>(rec.ExceptionAddress);
                    if (!breakpointArmed) {
                        // This is the automatic initial attach breakpoint (ntdll), not ours yet.
                        // Now place our own: read+save the original byte, write 0xCC.
                        SIZE_T br = 0;
                        if (ReadProcessMemory(hProcess, reinterpret_cast<LPCVOID>(targetAddr), &originalByte, 1, &br) && br == 1) {
                            uint8_t int3 = 0xCC;
                            SIZE_T bw = 0;
                            if (WriteProcessMemory(hProcess, reinterpret_cast<LPVOID>(targetAddr), &int3, 1, &bw) && bw == 1) {
                                FlushInstructionCache(hProcess, reinterpret_cast<LPCVOID>(targetAddr), 1);
                                breakpointArmed = true;
                                printf("Breakpoint armed at 0x%08zX (original byte 0x%02X saved). Waiting for it to hit...\n",
                                       targetAddr, originalByte);
                            } else {
                                printf("Failed to write breakpoint byte (%lu)\n", GetLastError());
                                done = true;
                            }
                        } else {
                            printf("Failed to read original byte at target (%lu)\n", GetLastError());
                            done = true;
                        }
                    } else if (hitAddr == targetAddr) {
                        printf("\n=== Breakpoint hit at 0x%08zX (thread %lu) ===\n", targetAddr, ev.dwThreadId);

                        HANDLE hThread = nullptr;
                        auto it = threadHandles.find(ev.dwThreadId);
                        if (it != threadHandles.end()) hThread = it->second;
                        if (!hThread) hThread = OpenThread(THREAD_ALL_ACCESS, FALSE, ev.dwThreadId);

                        CONTEXT ctx{};
                        ctx.ContextFlags = CONTEXT_FULL;
                        if (hThread && GetThreadContext(hThread, &ctx)) {
                            printf("Registers:\n");
                            printf("  EIP=0x%08lX  ESP=0x%08lX  EBP=0x%08lX\n", ctx.Eip, ctx.Esp, ctx.Ebp);
                            printf("  EAX=0x%08lX  EBX=0x%08lX  ECX=0x%08lX  EDX=0x%08lX\n",
                                   ctx.Eax, ctx.Ebx, ctx.Ecx, ctx.Edx);
                            printf("  ESI=0x%08lX  EDI=0x%08lX\n", ctx.Esi, ctx.Edi);
                            printf("\n--- ESI probes ---\n");
                            DumpProbe(hProcess, "ESI", ctx.Esi);
                            printf("\n--- EDI probes ---\n");
                            DumpProbe(hProcess, "EDI", ctx.Edi);
                            printf("\n--- EAX probes (in case arg is register-passed differently) ---\n");
                            DumpProbe(hProcess, "EAX", ctx.Eax);

                            // Restore original byte and rewind EIP by 1 so the real
                            // instruction executes normally once we continue.
                            SIZE_T bw = 0;
                            WriteProcessMemory(hProcess, reinterpret_cast<LPVOID>(targetAddr), &originalByte, 1, &bw);
                            FlushInstructionCache(hProcess, reinterpret_cast<LPCVOID>(targetAddr), 1);
                            ctx.Eip -= 1;
                            SetThreadContext(hThread, &ctx);
                        } else {
                            printf("GetThreadContext failed (%lu)\n", GetLastError());
                        }
                        if (it == threadHandles.end() && hThread) CloseHandle(hThread); // opened via OpenThread above
                        done = true;
                    } else {
                        // Some other breakpoint (not ours) -- pass through.
                        continueStatus = DBG_EXCEPTION_NOT_HANDLED;
                    }
                } else {
                    // Not a breakpoint exception -- let the process/OS handle it normally.
                    continueStatus = DBG_EXCEPTION_NOT_HANDLED;
                }
                break;
            }

            default:
                break;
        }

        ContinueDebugEvent(ev.dwProcessId, ev.dwThreadId, continueStatus);
    }

    if (!done) {
        printf("Timed out after %lu ms without the breakpoint hitting.\n", maxWaitMs);
        if (breakpointArmed && hProcess) {
            SIZE_T bw = 0;
            WriteProcessMemory(hProcess, reinterpret_cast<LPVOID>(targetAddr), &originalByte, 1, &bw);
            FlushInstructionCache(hProcess, reinterpret_cast<LPCVOID>(targetAddr), 1);
            printf("Restored original byte before detaching.\n");
        }
    }

    for (auto& kv : threadHandles) {
        // Handles from debug events are owned by the debugger and should be closed
        // once we're done with them (except ones we opened ourselves above, already closed).
        CloseHandle(kv.second);
    }

    DebugActiveProcessStop(pid);
    printf("\nDetached cleanly -- game continues running undisturbed.\n");
    return 0;
}
