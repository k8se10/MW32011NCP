// options_render_suppress.cpp -- see options_render_suppress.h for the full
// rationale and the raw disassembly trail (re_notes/options_menu_full_map.md sec 13).
#include <windows.h>
#include <cstdio>
#include "options_render_suppress.h"
#include "../third_party/minhook/include/MinHook.h"

extern void LogFromController(const char* msg); // defined in dllmain.cpp

namespace {
void* g_suppressedMenuPtr = nullptr;

using MenuRenderFn = void(__cdecl*)(void*);
void* g_orig_0050b740 = nullptr;
void* g_orig_004a4150 = nullptr;

// Both confirmed via raw disassembly: plain __cdecl, single void* menuDef pointer
// argument, standard prologue -- see options_render_suppress.h's own comment for why
// gating on pointer identity alone (no return-address check) is precise here.
void __cdecl Hook_0050b740(void* menuPtr)
{
    if (menuPtr && menuPtr == g_suppressedMenuPtr) return;
    reinterpret_cast<MenuRenderFn>(g_orig_0050b740)(menuPtr);
}

void __cdecl Hook_004a4150(void* menuPtr)
{
    if (menuPtr && menuPtr == g_suppressedMenuPtr) return;
    reinterpret_cast<MenuRenderFn>(g_orig_004a4150)(menuPtr);
}
} // namespace

void SetSuppressedMenuPointer(void* menuPtr)
{
    g_suppressedMenuPtr = menuPtr;
}

void InstallOptionsRenderSuppressionHooks()
{
    char buf[160];

    MH_STATUS s1 = MH_CreateHook(reinterpret_cast<LPVOID>(0x0050b740), &Hook_0050b740, &g_orig_0050b740);
    sprintf_s(buf, "[hooks] MH_CreateHook(0050b740 options-render-suppress layer0) = %d", static_cast<int>(s1));
    LogFromController(buf);
    if (s1 == MH_OK) {
        MH_STATUS e1 = MH_EnableHook(reinterpret_cast<LPVOID>(0x0050b740));
        sprintf_s(buf, "[hooks] MH_EnableHook(0050b740 options-render-suppress layer0) = %d", static_cast<int>(e1));
        LogFromController(buf);
    }

    MH_STATUS s2 = MH_CreateHook(reinterpret_cast<LPVOID>(0x004a4150), &Hook_004a4150, &g_orig_004a4150);
    sprintf_s(buf, "[hooks] MH_CreateHook(004a4150 options-render-suppress layer1) = %d", static_cast<int>(s2));
    LogFromController(buf);
    if (s2 == MH_OK) {
        MH_STATUS e2 = MH_EnableHook(reinterpret_cast<LPVOID>(0x004a4150));
        sprintf_s(buf, "[hooks] MH_EnableHook(004a4150 options-render-suppress layer1) = %d", static_cast<int>(e2));
        LogFromController(buf);
    }
}
