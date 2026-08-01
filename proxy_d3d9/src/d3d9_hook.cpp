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
#include <cstdlib>
#include "../third_party/minhook/include/MinHook.h"
#include "overlay_hud.h"

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

// Live-reported 2026-08-01 (custom cursor overlay): GetCursorPos+ScreenToClient
// produced a position that grew increasingly wrong further from the top-left of the
// screen -- classic symptom of a DPI-awareness-context mismatch between this DLL
// and the host process (GetCursorPos silently returns virtualized/scaled
// coordinates for a DPI-unaware caller, real physical pixels for a DPI-aware one).
// Rather than fight that, capture the exact same WM_MOUSEMOVE client-coordinate
// values the game's own WndProc already receives and uses for its own hit-testing
// -- guaranteed to agree with whatever the game itself considers "the mouse is
// here," since it's literally the same message data, no separate coordinate
// system to reconcile.
int g_lastMouseMoveClientX = -1;
int g_lastMouseMoveClientY = -1;
bool g_haveMouseMovePos = false;

LRESULT CALLBACK HookWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (msg == WM_MOUSEMOVE) {
        g_lastMouseMoveClientX = static_cast<short>(LOWORD(lParam));
        g_lastMouseMoveClientY = static_cast<short>(HIWORD(lParam));
        g_haveMouseMovePos = true;
    }
    InjectMenuInputTick();
    return CallWindowProcA(g_origWndProc, hwnd, msg, wParam, lParam);
}

// ---- Issue #1/#42 "needs an initial click" experiment (2026-07-31) -----------------
//
// User theory, backed by a real precedent already in this codebase (issue #1, day
// one of the mod): some real engine gate needs a genuine window-activation/click
// transition to sync up before certain systems behave correctly -- without it, the
// very first attempt after launch can silently fail even though everything works
// fine afterward. Fresh Ghidra work this session (re_notes/known_issues.md issue
// #42) confirmed crouch's own ToggleStance guard bytes (0xA98CA0/0xA98BC4) are a
// genuine "stance change locked" pair (FUN_0057d190 is a plain IsStanceLocked()
// query; FUN_0057d430, the per-frame keyboard-movement function this project's own
// movement hook already sits on top of, forces real stance to 0 and forces usercmd
// crouch/prone button bits while locked) -- but an exhaustive whole-binary scan for
// both exact addresses found only 4 reader functions and ZERO writers, so what
// actually sets/clears them (and whether that's the same mechanism a real click
// would trigger) could not be pinned down via static analysis alone.
//
// This tests the user's own fix idea empirically: synthesize a real activation +
// click sequence DIRECTLY into the game's own real WndProc via CallWindowProcA --
// bypasses the OS message queue entirely (no SetForegroundWindow, no stealing focus
// from another window -- "through the engine, not Windows", per the user's own
// framing) while still triggering whatever the engine itself does in reaction to a
// genuine WM_ACTIVATE/WM_SETFOCUS/click sequence. Fires once, as early as possible
// (right after the D3D9 device's real window handle is known, before any real
// rendering/menu could exist yet -- about the safest possible moment to synthesize
// input, and (1,1) as the click coordinate to make it essentially impossible to land
// on a real UI element even if one somehow already existed). EXPERIMENTAL, not a
// confirmed fix -- see re_notes/known_issues.md issue #42 for the full reasoning and
// what to check in proxy_d3d9.log's [stance-diag] lines (now logging both guard
// bytes on every heartbeat) if this doesn't fully resolve the symptom.
void SendSyntheticActivationClick(HWND hwnd)
{
    if (!g_origWndProc) return;
    CallWindowProcA(g_origWndProc, hwnd, WM_ACTIVATE, MAKEWPARAM(WA_ACTIVE, 0),
                     reinterpret_cast<LPARAM>(hwnd));
    CallWindowProcA(g_origWndProc, hwnd, WM_SETFOCUS, 0, 0);
    CallWindowProcA(g_origWndProc, hwnd, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(1, 1));
    CallWindowProcA(g_origWndProc, hwnd, WM_LBUTTONUP, 0, MAKELPARAM(1, 1));
    LogFromController("[focus-gate-fix] synthesized WM_ACTIVATE/WM_SETFOCUS/"
                       "WM_LBUTTONDOWN+UP into the real WndProc (issue #42 experiment)");
}

// ---- "MW32011NCP Started" QoL notification (2026-07-31, user request) -------------
//
// Fires once, right after the real HAL device exists (the earliest point overlay_hud
// can actually draw anything). A 1-in-3 (~33%) roll shows one of several alternate
// variants -- exact odds are just a tunable constant here, not derived from
// anything (raised from an original 1-in-20 the same day, per user request, so the
// variants are actually seen during normal play rather than needing
// [Overlay] TestCycleAllVariants). Three of the four variants are a "vibes" homage
// to WaW's real, documented hidden dev clan-tag codes (re_notes/known_issues.md
// issue #37: GOLD, RAIN, CYLN) now that overlay_hud can actually animate/color the
// quad -- not a literal recreation (those were clan tags, this is a toast message),
// just a nod.
constexpr int kVariantMessageOneInN = 3;
constexpr int kVariantCount = 4;

void ShowStartupMessage()
{
    srand(GetTickCount());
    if ((rand() % kVariantMessageOneInN) != 0) {
        ShowOverlayMessage("MW32011NCP Started", 15000, OverlayAnimStyle::Plain);
        return;
    }

    switch (rand() % kVariantCount) {
        case 0:
            ShowOverlayMessage("MW32011NCP Started - Thanks For Supporting The Project :P",
                                15000, OverlayAnimStyle::Plain);
            break;
        case 1:
            ShowOverlayMessage("MW32011NCP Started", 15000, OverlayAnimStyle::Gold); // WaW "GOLD" homage
            break;
        case 2:
            ShowOverlayMessage("MW32011NCP Started", 15000, OverlayAnimStyle::Rainbow); // WaW "RAIN" homage
            break;
        default:
            ShowOverlayMessage("MW32011NCP Started", 15000, OverlayAnimStyle::Sweep); // WaW "CYLN" homage
            break;
    }
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
    SendSyntheticActivationClick(hwnd);
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
        if (ppReturnedDeviceInterface && *ppReturnedDeviceInterface) {
            // Live-reported 2026-07-31 CRITICAL bug: "changing display mode crashes the
            // whole game." Confirmed via proxy_d3d9.log that this engine does NOT call
            // IDirect3DDevice9::Reset on a display-mode change -- it destroys the whole
            // device and calls CreateDevice again from scratch (this exact log line
            // fires a second time, well after the first device's install). See
            // overlay_hud.h's own OnDeviceRecreated comment for the full trail. Detected
            // here via g_deviceEverCreated: every call after the first means the
            // previous device (and every texture this project cached against it) is
            // already gone.
            static bool g_deviceEverCreated = false;
            if (g_deviceEverCreated) {
                OnDeviceRecreated();
            }
            g_deviceEverCreated = true;
            InstallEndSceneHook(*ppReturnedDeviceInterface);
            ShowStartupMessage();
        }
    }
    return hr;
}

} // namespace

// Exposed so analog_input_hooks.cpp can PostMessage a synthetic keypress directly at the
// game's real window -- used for two explicit, narrowly-scoped exceptions to this
// project's "no OS-level input emulation" rule, each approved by the user for that one
// specific case pending a real native fix: the Survival ready-up F5 workaround (see
// InjectControllerReadyUp) and D-pad Left's squadmate call-in '4' workaround (see
// InjectControllerDpad).
extern "C" HWND GetGameWindow()
{
    return g_gameHwnd;
}

// Exposed so overlay_hud.cpp's custom-cursor draw can position itself using the
// exact same WM_MOUSEMOVE client-coordinate values the game's own WndProc already
// received and used for its own hit-testing -- see the big comment above
// g_lastMouseMoveClientX for why this replaced a GetCursorPos-based approach.
// Returns false (leaving outX/outY untouched) until the very first WM_MOUSEMOVE
// this session, which should always have happened well before any menu is visible.
extern "C" bool GetLastMouseMoveClientPos(int& outX, int& outY)
{
    if (!g_haveMouseMovePos) return false;
    outX = g_lastMouseMoveClientX;
    outY = g_lastMouseMoveClientY;
    return true;
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
