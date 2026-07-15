// d3d9_hook.cpp — hooks IDirect3D9::CreateDevice purely to capture the game's real
// window handle, then subclasses that window's WndProc for menu-related input that
// needs to keep working while the game is genuinely paused.
//
// WHY THIS EXISTS (found 2026-07-15): the mod's whole per-frame injection
// (analog_input_hooks.cpp's InjectAllControllerInput) lives inside FUN_0057de60, part
// of the game's per-frame GAMEPLAY SIMULATION pipeline. Confirmed live via a heartbeat
// diagnostic that this hook stops firing entirely while the game is genuinely paused --
// pausing halts simulation by design. That's fine for movement/look/buttons (meaningless
// while paused anyway), but it meant Start's second press (to unpause) could never be
// detected: the very code path needed to notice it doesn't run while paused.
//
// FIRST ATTEMPT (same day): hooked IDirect3DDevice9::Present instead, on the theory that
// it keeps firing every rendered frame regardless of pause state. Installed cleanly
// (MH_CreateHook/MH_EnableHook both returned MH_OK, confirmed targeting the real
// D3DDEVTYPE_HAL device's real Present address, not a REF/NULLREF probe device -- ruled
// that theory out explicitly). CONFIRMED DEAD via a fire-counter diagnostic
// (g_presentFireCount, incremented inside the detour): it stayed at exactly zero through
// an entire normal, unpaused play session with dozens of confirmed gameplay-tick frames
// elapsing in between -- i.e. the detour never fired even once, not just "during pause."
// That rules out a pause-specific timing issue entirely; something is intercepting the
// same vtable slot our hook targets and preventing our patched bytes from ever running
// (Steam Overlay is the prime suspect -- it's well documented to hook Present itself and
// is active by default for any Steam-launched title; a driver-level overlay is the other
// usual suspect). Abandoned rather than chased further -- not worth fighting an unrelated
// third party's hook for this.
//
// REAL FIX: subclass the game's own window procedure instead of touching D3D9 at all.
// This is a plain Win32 API (SetWindowLongPtr on GWLP_WNDPROC), not a COM vtable, so
// nothing D3D9-related can silently steal it. Windows keeps pumping window messages even
// while the game's own simulation is paused -- proven by the fact vanilla keyboard ESC
// can still unpause the game today, which only works because SOME message-pump-adjacent
// code path keeps running throughout the paused state. A SetTimer-driven WM_TIMER message
// (posted at a fixed ~60Hz cadence regardless of mouse movement or other activity)
// guarantees our hook still ticks even during totally idle periods with no other window
// messages arriving. Runs on the game's own thread (whichever thread owns/pumps the
// window), same as every other hook in this project -- not a separate free-running
// thread, which would call real engine functions from an unsynchronized thread and risk
// exactly the kind of corruption CLAUDE.md's hook-safety rules warn against.
//
// Deliberately NOT including <d3d9.h> here, same reasoning as dllmain.cpp: we only need
// the CreateDevice vtable SLOT and its calling convention (a COM method -- WINAPI/
// __stdcall with an explicit "this" as the first parameter when called via a raw vtable
// function pointer, not through C++ virtual dispatch), not the full interface
// definition. Avoids pulling in d3d9.lib entirely.

#include <windows.h>
#include <cstdint>
#include <cstdio>
#include "../third_party/minhook/include/MinHook.h"

extern void LogFromController(const char* msg);
extern "C" void __cdecl InjectMenuInputTick(); // defined in analog_input_hooks.cpp

namespace {

constexpr int kCreateDeviceVtableIndex = 16; // IDirect3D9::CreateDevice
constexpr DWORD kD3DDEVTYPE_HAL = 1;         // the real hardware device, not a REF/NULLREF probe
constexpr UINT_PTR kPollTimerId = 0xC0D3;    // arbitrary, just needs to be ours

typedef HRESULT(WINAPI* CreateDevice_t)(void* This, UINT Adapter, DWORD DeviceType,
    HWND hFocusWindow, DWORD BehaviorFlags, void* pPresentationParameters,
    void** ppReturnedDeviceInterface);

CreateDevice_t g_origCreateDevice = nullptr;
WNDPROC g_origWndProc = nullptr;
bool g_wndProcHooked = false; // only need to subclass once -- the game has one window
HWND g_gameHwnd = nullptr;

LRESULT CALLBACK HookWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    InjectMenuInputTick();
    return CallWindowProcA(g_origWndProc, hwnd, msg, wParam, lParam);
}

void InstallWndProcHook(HWND hwnd)
{
    if (g_wndProcHooked || !hwnd) return;
    g_wndProcHooked = true;
    g_gameHwnd = hwnd;
    g_origWndProc = reinterpret_cast<WNDPROC>(
        SetWindowLongPtrA(hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&HookWndProc)));
    char buf[128];
    sprintf_s(buf, "[wndproc-hook] subclassed hwnd=%p, orig proc=%p", hwnd, g_origWndProc);
    LogFromController(buf);
    // Guarantees WM_TIMER messages at a fixed ~60Hz cadence even if nothing else
    // generates window messages (e.g. the mouse sits still over an idle paused menu) --
    // without this, HookWndProc would only tick as often as real messages happen to
    // arrive, which isn't reliably frequent enough to catch a quick Start press/release.
    SetTimer(hwnd, kPollTimerId, 16, nullptr);
}

HRESULT WINAPI Hook_CreateDevice(void* This, UINT Adapter, DWORD DeviceType,
    HWND hFocusWindow, DWORD BehaviorFlags, void* pPresentationParameters,
    void** ppReturnedDeviceInterface)
{
    HRESULT hr = g_origCreateDevice(This, Adapter, DeviceType, hFocusWindow, BehaviorFlags,
        pPresentationParameters, ppReturnedDeviceInterface);

    char logBuf[128];
    sprintf_s(logBuf, "[d3d9-hook] CreateDevice called: DeviceType=%lu hwnd=%p hr=0x%08lX",
        DeviceType, hFocusWindow, hr);
    LogFromController(logBuf);

    if (SUCCEEDED(hr) && DeviceType == kD3DDEVTYPE_HAL) {
        InstallWndProcHook(hFocusWindow);
    }
    return hr;
}

} // namespace

// Exposed so analog_input_hooks.cpp can PostMessage a synthetic keypress directly at the
// game's real window -- used ONLY for the Survival ready-up F5 workaround (see
// InjectControllerReadyUp), an explicit, narrowly-scoped exception to this project's
// "no OS-level input emulation" rule, approved by the user specifically for that one
// case pending a real native fix.
extern "C" HWND GetGameWindow()
{
    return g_gameHwnd;
}

// Called from dllmain.cpp's Direct3DCreate9 implementation with the real IDirect3D9*
// (kept as void* across this boundary -- dllmain.cpp deliberately keeps IDirect3D9
// opaque to avoid a d3d9.h include collision with its naked export forwarding stubs).
extern "C" void HookD3D9CreateDevice(void* realD3D9)
{
    if (!realD3D9) return;
    void** d3d9Vtable = *reinterpret_cast<void***>(realD3D9);
    void* realCreateDevice = d3d9Vtable[kCreateDeviceVtableIndex];

    MH_STATUS s = MH_CreateHook(realCreateDevice, reinterpret_cast<void*>(&Hook_CreateDevice),
        reinterpret_cast<void**>(&g_origCreateDevice));
    char buf[128];
    sprintf_s(buf, "[d3d9-hook] MH_CreateHook(CreateDevice @ %p) = %d", realCreateDevice, static_cast<int>(s));
    LogFromController(buf);
    if (s == MH_OK) {
        MH_STATUS e = MH_EnableHook(realCreateDevice);
        sprintf_s(buf, "[d3d9-hook] MH_EnableHook(CreateDevice) = %d", static_cast<int>(e));
        LogFromController(buf);
    }
}
