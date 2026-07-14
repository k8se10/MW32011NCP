// diag_hooks.cpp — PASSIVE diagnostic hooks (task #5 groundwork).
//
// Ghidra's static decompile of FUN_0057d430 (keyboard analog movement summer) and
// FUN_0057d680 (raw mouse-delta source) reports their real arguments as "unaff_ESI" /
// "unaff_EDI" / "in_EAX" — i.e. values the CALLER leaves in specific registers rather
// than a clean stack/fastcall signature. Static analysis alone isn't trustworthy for
// that; this file installs non-invasive MinHook hooks that log full register state on
// entry, then jump through the untouched original, so live play (just pressing W/A/S/D
// and moving the mouse — no physical controller needed) confirms which register really
// holds the usercmd_t* before any real hook logic gets written on top of this.
//
// This is throwaway diagnostic code, expected to be replaced once the calling
// convention is confirmed (see re_notes/iw5sp.md).

#include <windows.h>
#include <cstdio>
#include "../third_party/minhook/include/MinHook.h"

namespace {

FILE* g_diagLog = nullptr;
volatile LONG g_hitCount_0057d430 = 0;
volatile LONG g_hitCount_0057d680 = 0;
// hit-count cap is inlined as a literal (40) directly in the asm compares below --
// MSVC inline asm can't reliably resolve a C++ const global as an immediate

void DiagLogInit()
{
    char path[MAX_PATH];
    GetModuleFileNameA(nullptr, path, MAX_PATH);
    char* lastSlash = strrchr(path, '\\');
    if (lastSlash) *(lastSlash + 1) = '\0';
    strcat_s(path, "proxy_d3d9_diag.log");
    fopen_s(&g_diagLog, path, "a");
    if (g_diagLog) {
        fprintf(g_diagLog, "---- diag hooks attach ----\n");
        fflush(g_diagLog);
    }
}

struct SavedRegs {
    DWORD edi, esi, ebp, esp_orig, ebx, edx, ecx, eax;
};

void PrintMaybeStructBytes(FILE* f, DWORD addr, const char* label)
{
    // Only dereference if it looks like a plausible pointer into this process's
    // typical address range, to avoid crashing the game on a bad guess.
    if (addr < 0x00010000 || addr > 0x7FFEFFFF) {
        fprintf(f, "    %s = 0x%08X (not dereferenced, out of plausible range)\n", label, addr);
        return;
    }
    __try {
        const unsigned char* p = reinterpret_cast<const unsigned char*>(addr);
        fprintf(f, "    %s = 0x%08X -> bytes[0..0x40): ", label, addr);
        for (int i = 0; i < 0x40; i++) {
            fprintf(f, "%02X ", p[i]);
        }
        fprintf(f, "\n");
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        fprintf(f, "    %s = 0x%08X (dereference faulted)\n", label, addr);
    }
}

extern "C" void __cdecl LogHookRegs(const char* tag, SavedRegs* regs)
{
    if (!g_diagLog) return;
    fprintf(g_diagLog, "[%s] eax=%08X ecx=%08X edx=%08X ebx=%08X ebp=%08X esi=%08X edi=%08X\n",
        tag, regs->eax, regs->ecx, regs->edx, regs->ebx, regs->ebp, regs->esi, regs->edi);
    // Dump raw bytes at ESI and EDI specifically, since Ghidra's decompile named both
    // as candidate struct-pointer registers across the two functions -- seeing which
    // one contains sane usercmd_t-shaped data (buttons at +4 nonzero when a button is
    // held, etc.) is the whole point of this diagnostic pass.
    PrintMaybeStructBytes(g_diagLog, regs->esi, "esi");
    PrintMaybeStructBytes(g_diagLog, regs->edi, "edi");
    fflush(g_diagLog);
}

// Tag strings live as normal C globals -- MSVC's inline assembler doesn't support
// MASM-style `db` data-definition directives inside __asm blocks.
const char g_tag_0057d430[] = "0057d430";
const char g_tag_0057d680[] = "0057d680";

// ---- Hook for FUN_0057d430 (keyboard analog movement summer) --------------------
void* g_orig_0057d430 = nullptr;

__declspec(naked) void Hook_0057d430()
{
    __asm {
        pushad
        cmp g_hitCount_0057d430, 40
        jge skip_log
        inc g_hitCount_0057d430
        push esp
        push offset g_tag_0057d430
        call LogHookRegs
        add esp, 8
    skip_log:
        popad
        jmp dword ptr [g_orig_0057d430]
    }
}

// ---- Hook for FUN_0057d680 (raw mouse-delta source) ------------------------------
void* g_orig_0057d680 = nullptr;

__declspec(naked) void Hook_0057d680()
{
    __asm {
        pushad
        cmp g_hitCount_0057d680, 40
        jge skip_log
        inc g_hitCount_0057d680
        push esp
        push offset g_tag_0057d680
        call LogHookRegs
        add esp, 8
    skip_log:
        popad
        jmp dword ptr [g_orig_0057d680]
    }
}

} // namespace

void InstallDiagHooks()
{
    DiagLogInit();

    if (MH_Initialize() != MH_OK) {
        if (g_diagLog) { fprintf(g_diagLog, "MH_Initialize failed\n"); fflush(g_diagLog); }
        return;
    }

    MH_STATUS s1 = MH_CreateHook(reinterpret_cast<LPVOID>(0x0057d430), &Hook_0057d430, &g_orig_0057d430);
    MH_STATUS s2 = MH_CreateHook(reinterpret_cast<LPVOID>(0x0057d680), &Hook_0057d680, &g_orig_0057d680);

    if (g_diagLog) {
        fprintf(g_diagLog, "MH_CreateHook 0057d430 -> %d, 0057d680 -> %d\n", s1, s2);
        fflush(g_diagLog);
    }

    if (s1 == MH_OK) MH_EnableHook(reinterpret_cast<LPVOID>(0x0057d430));
    if (s2 == MH_OK) MH_EnableHook(reinterpret_cast<LPVOID>(0x0057d680));
}
