// d3d9_hook.cpp — real IDirect3DDevice9::Present hook.
//
// WHY THIS EXISTS (found 2026-07-15): the mod's whole per-frame injection
// (analog_input_hooks.cpp's InjectAllControllerInput) lives inside FUN_0057de60, part
// of the game's per-frame GAMEPLAY SIMULATION pipeline. Confirmed live via a heartbeat
// diagnostic that this hook stops firing entirely while the game is genuinely paused --
// pausing halts simulation by design, and our whole mod was riding on top of it. That's
// fine for movement/look/buttons (meaningless while paused anyway), but it meant Start's
// second press (to unpause) could never be detected: the very code path needed to notice
// it doesn't run while paused. Present, by contrast, keeps firing every rendered frame
// regardless of pause state (the pause menu itself still needs to be drawn) -- so
// menu-related input (see InjectMenuInputTick in analog_input_hooks.cpp) is driven from
// here instead.
//
// Deliberately NOT including <d3d9.h> here, same reasoning as dllmain.cpp: we only need
// the CreateDevice/Present vtable SLOTS and their calling convention (both are COM
// methods -- WINAPI/__stdcall with an explicit "this" as the first parameter when called
// via a raw vtable function pointer, not through C++ virtual dispatch), not the full
// interface definitions. Avoids pulling in d3d9.lib entirely.

#include <windows.h>
#include <cstdint>
#include <cstdio>
#include "../third_party/minhook/include/MinHook.h"

extern void LogFromController(const char* msg);
extern "C" void __cdecl InjectMenuInputTick(); // defined in analog_input_hooks.cpp

namespace {

constexpr int kCreateDeviceVtableIndex = 16; // IDirect3D9::CreateDevice
constexpr int kPresentVtableIndex = 17;      // IDirect3DDevice9::Present
constexpr DWORD kD3DDEVTYPE_HAL = 1;         // the real hardware device -- see below

typedef HRESULT(WINAPI* CreateDevice_t)(void* This, UINT Adapter, DWORD DeviceType,
    HWND hFocusWindow, DWORD BehaviorFlags, void* pPresentationParameters,
    void** ppReturnedDeviceInterface);
typedef HRESULT(WINAPI* Present_t)(void* This, const RECT* pSourceRect,
    const RECT* pDestRect, HWND hDestWindowOverride, const void* pDirtyRegion);

CreateDevice_t g_origCreateDevice = nullptr;
Present_t g_origPresent = nullptr;
bool g_presentHooked = false; // Present's real implementation is shared across every
                              // HAL device instance -- only need to hook it once.

HRESULT WINAPI Hook_Present(void* This, const RECT* pSourceRect, const RECT* pDestRect,
    HWND hDestWindowOverride, const void* pDirtyRegion)
{
    InjectMenuInputTick();
    return g_origPresent(This, pSourceRect, pDestRect, hDestWindowOverride, pDirtyRegion);
}

// FIX (2026-07-15): the first version hooked Present on whichever device's CreateDevice
// call succeeded first, with no DeviceType check -- installed cleanly (MH_OK) but the
// detour was never invoked again live. Root cause (confirmed via research into the D3D9
// hooking community): many D3D9 titles create a throwaway D3DDEVTYPE_REF/NULLREF
// "probe" device first (e.g. to query GetDeviceCaps/display modes) before creating the
// real D3DDEVTYPE_HAL device actually used for rendering -- REF/NULLREF devices use
// Microsoft's software reference rasterizer internals, a COMPLETELY DIFFERENT vtable/
// Present implementation from the real HAL driver's. The log showed exactly this
// pattern (Direct3DCreate9 called twice, CreateDevice hook firing for both) -- we almost
// certainly locked onto the probe device's Present, which is never called again, while
// the real HAL device's Present (a different function entirely) stayed unhooked. Fix:
// only treat a CreateDevice call as "the one" when DeviceType == D3DDEVTYPE_HAL.
HRESULT WINAPI Hook_CreateDevice(void* This, UINT Adapter, DWORD DeviceType,
    HWND hFocusWindow, DWORD BehaviorFlags, void* pPresentationParameters,
    void** ppReturnedDeviceInterface)
{
    HRESULT hr = g_origCreateDevice(This, Adapter, DeviceType, hFocusWindow, BehaviorFlags,
        pPresentationParameters, ppReturnedDeviceInterface);

    char logBuf[128];
    sprintf_s(logBuf, "[d3d9-hook] CreateDevice called: DeviceType=%lu hr=0x%08lX", DeviceType, hr);
    LogFromController(logBuf);

    if (SUCCEEDED(hr) && !g_presentHooked && DeviceType == kD3DDEVTYPE_HAL
        && ppReturnedDeviceInterface && *ppReturnedDeviceInterface) {
        g_presentHooked = true;
        void** deviceVtable = *reinterpret_cast<void***>(*ppReturnedDeviceInterface);
        void* realPresent = deviceVtable[kPresentVtableIndex];

        MH_STATUS s = MH_CreateHook(realPresent, reinterpret_cast<void*>(&Hook_Present),
            reinterpret_cast<void**>(&g_origPresent));
        char buf[128];
        sprintf_s(buf, "[d3d9-hook] MH_CreateHook(Present @ %p, HAL device) = %d", realPresent, static_cast<int>(s));
        LogFromController(buf);
        if (s == MH_OK) {
            MH_STATUS e = MH_EnableHook(realPresent);
            sprintf_s(buf, "[d3d9-hook] MH_EnableHook(Present) = %d", static_cast<int>(e));
            LogFromController(buf);
        }
    }
    return hr;
}

} // namespace

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
