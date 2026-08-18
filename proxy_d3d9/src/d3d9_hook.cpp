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
extern "C" bool IsGlyphPositionEditModeActive(); // defined in analog_input_hooks.cpp

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
// BUG-001 follow-up (2026-08-02, stream co-op report): "recent input method" signal
// for the custom cursor -- see GetLastMouseMoveTickMs()'s own comment for why this
// is WM_MOUSEMOVE specifically, not WM_KEYDOWN/other messages too.
DWORD g_lastMouseMoveTickMs = 0;
// Live-reported same day: the cursor came back too fast after using the controller --
// the fix at the time only checked "has the controller been quiet for a bit," never
// actually required genuine mouse movement to have happened. Root cause of THAT is a
// real pixel deadzone missing here: any WM_MOUSEMOVE at all (even a 1px native
// engine-driven snap/clamp, unrelated to the player's hand) was counted as "real
// mouse activity." Mirrors the same deadzone concept this project's own controller
// sticks already use -- only counts as real activity once cumulative movement since
// the last confirmed real move exceeds a small pixel radius; anchors re-baseline
// every time the deadzone is cleared, so slow deliberate movement still accumulates
// and eventually counts, same semantics as a stick deadzone measured from center.
int g_lastMouseActivityBaselineX = -1;
int g_lastMouseActivityBaselineY = -1;
constexpr int kMouseMoveDeadzonePx = 4;

// Real left-click held state (2026-08-04, issue #66 follow-up: "our im game cursor
// to be able to click entries too" -- the custom Options screen's rows/tabs need
// real mouse-click support, not just controller D-pad/A). Same real WM_LBUTTONDOWN/
// WM_LBUTTONUP messages the game's own WndProc already receives -- this project's
// own WndProc subclass just also watches them, exactly like WM_MOUSEMOVE above,
// rather than adding a second, separate input-capture mechanism.
bool g_leftMouseButtonHeld = false;

// Glyph position editor mouse isolation, part 2 (2026-08-16, live-reported "the in
// game highlight still happens even in edit mode" after WndProc message-swallowing
// alone wasn't enough). Confirmed via re_notes/iw5sp.md this engine has NO DirectInput
// import at all -- so the real menu's own mouse hover/hit-testing isn't reading a
// message-queue value at all, it's polling the plain Win32 GetCursorPos() directly
// every frame, which happens completely independently of whatever WM_MOUSEMOVE
// messages this project's WndProc subclass does or doesn't forward. Process-wide
// MinHook detour on user32.dll's real GetCursorPos (confirmed no other code in this
// project calls it anymore -- the custom-cursor overlay switched to the WM_MOUSEMOVE-
// based GetLastMouseMoveClientPos above specifically to avoid a DPI-mismatch bug
// GetCursorPos had, see that migration's own comment) freezes what the REAL GAME sees
// at whatever the cursor's last real position was the moment editing turned on, while
// this project's OWN drag logic (GetLastMouseMoveClientPos, entirely separate) keeps
// reading live values throughout -- so the real menu's hover/selection can't drift
// with the mouse anymore while editing, without needing to know any internal engine
// memory layout.
typedef BOOL(WINAPI* GetCursorPos_t)(LPPOINT);
GetCursorPos_t g_origGetCursorPos = nullptr;
POINT g_lastRealCursorPos = {};
bool g_haveLastRealCursorPos = false;
bool g_getCursorPosHookInstalled = false;

BOOL WINAPI Hook_GetCursorPos(LPPOINT lpPoint)
{
    if (IsGlyphPositionEditModeActive() && g_haveLastRealCursorPos) {
        if (lpPoint) *lpPoint = g_lastRealCursorPos;
        return TRUE;
    }
    if (!g_origGetCursorPos) return FALSE;
    BOOL ok = g_origGetCursorPos(lpPoint);
    if (ok && lpPoint && !IsGlyphPositionEditModeActive()) {
        g_lastRealCursorPos = *lpPoint;
        g_haveLastRealCursorPos = true;
    }
    return ok;
}

void InstallGetCursorPosHook()
{
    if (g_getCursorPosHookInstalled) return;
    g_getCursorPosHookInstalled = true;
    MH_Initialize(); // idempotent -- harmless if analog_input_hooks.cpp already called this
    HMODULE user32 = GetModuleHandleA("user32.dll");
    void* realGetCursorPos = user32 ? reinterpret_cast<void*>(GetProcAddress(user32, "GetCursorPos")) : nullptr;
    if (!realGetCursorPos) {
        LogFromController("[glyph-editor] GetCursorPos hook: GetProcAddress(user32.dll, \"GetCursorPos\") failed");
        return;
    }
    MH_STATUS s = MH_CreateHook(realGetCursorPos, reinterpret_cast<void*>(&Hook_GetCursorPos),
        reinterpret_cast<void**>(&g_origGetCursorPos));
    char buf[128];
    sprintf_s(buf, "[glyph-editor] MH_CreateHook(GetCursorPos @ %p) = %d", realGetCursorPos, static_cast<int>(s));
    LogFromController(buf);
    if (s == MH_OK) {
        MH_STATUS e = MH_EnableHook(realGetCursorPos);
        sprintf_s(buf, "[glyph-editor] MH_EnableHook(GetCursorPos) = %d", static_cast<int>(e));
        LogFromController(buf);
    }
}

// Full-scope Options expansion (2026-08-06, issue #66, explicit direction: "Build
// full rebind capture now"). Rebinding a real keyboard/mouse bind from a
// controller-driven menu needs the ACTUAL Win32 key/mouse-button message -- the
// controller-edge booleans CustomOptionsMenu_TickInput already receives (D-pad/A/B)
// have no way to represent "the player just pressed W." This hooks into the exact
// same real WndProc subclass WM_MOUSEMOVE/WM_LBUTTONDOWN already use above, rather
// than adding a second, separate input-capture mechanism.
//
// StartKeybindCapture() arms it; the very next real key/mouse-button press this
// WndProc sees gets translated to this engine's own real key-name string (the exact
// format kKeyActionTable/KeyNameToKeynum already use -- "A".."Z", "0".."9", "SPACE",
// "CTRL", "MOUSE1", etc.) and stashed for PollCapturedKeynum to consume once. The
// message that completed the capture is deliberately NOT forwarded to the real
// WndProc (CallWindowProcA is skipped for it) so e.g. capturing "W" doesn't also
// move the player or capturing "ESCAPE" doesn't also toggle a real menu underneath.
bool g_keybindCaptureActive = false;
char g_capturedKeyName[32] = {};
bool g_haveCapturedKeyName = false;

// VK_* -> this engine's own real key-name string. Deliberately NOT exhaustive --
// covers the keys a player would realistically rebind to (letters, digits, common
// modifiers/navigation, function keys, the 3 real mouse buttons) rather than every
// possible VK code. An unmapped key press is silently ignored (capture stays armed)
// rather than guessing a name that might not exist in the real key-action table --
// expand this table if a real gap is reported, same as this project's other
// incrementally-grown key tables.
const char* VkCodeToKeyName(WPARAM vk)
{
    if (vk >= 'A' && vk <= 'Z') {
        static char single[2];
        single[0] = static_cast<char>(vk);
        single[1] = '\0';
        return single;
    }
    if (vk >= '0' && vk <= '9') {
        static char single[2];
        single[0] = static_cast<char>(vk);
        single[1] = '\0';
        return single;
    }
    if (vk >= VK_F1 && vk <= VK_F12) {
        static char fkey[4];
        sprintf_s(fkey, "F%d", static_cast<int>(vk - VK_F1 + 1));
        return fkey;
    }
    switch (vk) {
        case VK_SPACE:   return "SPACE";
        case VK_TAB:     return "TAB";
        case VK_RETURN:  return "ENTER";
        case VK_ESCAPE:  return "ESCAPE";
        case VK_BACK:    return "BACKSPACE";
        case VK_DELETE:  return "DEL";
        case VK_INSERT:  return "INS";
        case VK_HOME:    return "HOME";
        case VK_END:     return "END";
        case VK_PRIOR:   return "PGUP";
        case VK_NEXT:    return "PGDN";
        case VK_CAPITAL: return "CAPSLOCK";
        case VK_CONTROL: return "CTRL";
        case VK_SHIFT:   return "SHIFT";
        case VK_MENU:    return "ALT";
        case VK_UP:      return "UPARROW";
        case VK_DOWN:    return "DOWNARROW";
        case VK_LEFT:    return "LEFTARROW";
        case VK_RIGHT:   return "RIGHTARROW";
        case VK_OEM_3:   return "~"; // tilde/console key
        default:         return nullptr;
    }
}

extern "C" void StartKeybindCapture()
{
    g_keybindCaptureActive = true;
    g_haveCapturedKeyName = false;
    g_capturedKeyName[0] = '\0';
}

extern "C" void CancelKeybindCapture()
{
    g_keybindCaptureActive = false;
}

// Returns true exactly once per completed capture (consumes the result) -- called
// every tick from CustomOptionsMenu_TickInput while capture mode is active.
extern "C" bool PollCapturedKeyName(char* outBuf, int outBufSize)
{
    if (!g_haveCapturedKeyName) return false;
    strncpy_s(outBuf, outBufSize, g_capturedKeyName, _TRUNCATE);
    g_haveCapturedKeyName = false;
    return true;
}

LRESULT CALLBACK HookWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (g_keybindCaptureActive) {
        const char* keyName = nullptr;
        if (msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN) {
            keyName = VkCodeToKeyName(wParam);
        } else if (msg == WM_LBUTTONDOWN) {
            keyName = "MOUSE1";
        } else if (msg == WM_RBUTTONDOWN) {
            keyName = "MOUSE2";
        } else if (msg == WM_MBUTTONDOWN) {
            keyName = "MOUSE3";
        }
        if (keyName) {
            strncpy_s(g_capturedKeyName, keyName, _TRUNCATE);
            g_haveCapturedKeyName = true;
            g_keybindCaptureActive = false;
            return 0; // swallow this one message -- see this block's own header comment
        }
    }

    if (msg == WM_MOUSEMOVE) {
        int newX = static_cast<short>(LOWORD(lParam));
        int newY = static_cast<short>(HIWORD(lParam));
        g_lastMouseMoveClientX = newX;
        g_lastMouseMoveClientY = newY;
        g_haveMouseMovePos = true;

        if (g_lastMouseActivityBaselineX < 0) {
            g_lastMouseActivityBaselineX = newX;
            g_lastMouseActivityBaselineY = newY;
        }
        int dx = newX - g_lastMouseActivityBaselineX;
        int dy = newY - g_lastMouseActivityBaselineY;
        if (dx * dx + dy * dy >= kMouseMoveDeadzonePx * kMouseMoveDeadzonePx) {
            g_lastMouseMoveTickMs = GetTickCount();
            g_lastMouseActivityBaselineX = newX;
            g_lastMouseActivityBaselineY = newY;
        }
    } else if (msg == WM_LBUTTONDOWN) {
        g_leftMouseButtonHeld = true;
    } else if (msg == WM_LBUTTONUP) {
        g_leftMouseButtonHeld = false;
    }
    InjectMenuInputTick();

    // Glyph position editor (2026-08-16, issue #51 follow-up): live-reported "it
    // skips through the menu" -- a click meant to grab/drag a calibration handle was
    // ALSO reaching the real menu's own native mouse-click support (this engine
    // responds to real mouse messages directly, not just controller D-pad/A -- see
    // this project's own earlier real-mouse-click work), so calibrating one item
    // could accidentally activate/select/hover whatever real menu element happened
    // to be under the cursor. Follow-up, same day: user direction was "the mouse
    // shouldn't interact with anything but our UI layer" while the editor is active --
    // broadened from left-click-only to the ENTIRE standard mouse message range
    // (WM_MOUSEFIRST..WM_MOUSELAST: move, all three buttons down/up/double-click,
    // and both wheel messages), so hover-highlight and every other real mouse-driven
    // menu behavior is fully isolated too, not just clicks. g_lastMouseMoveClientX/Y
    // and g_leftMouseButtonHeld above are already updated by this point, so this
    // project's OWN UI layer (the drag handles, the custom cursor, hit-testing) is
    // completely unaffected -- only the real game's own WndProc stops seeing mouse
    // input while the editor is actively toggled on (F2).
    if (msg >= WM_MOUSEFIRST && msg <= WM_MOUSELAST && IsGlyphPositionEditModeActive()) {
        return 0;
    }

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
    if (!hwnd) return;
    // Live-reported 2026-08-01 (custom cursor overlay stopped updating after a
    // display-mode change): this function's own original comment ("only need to
    // subclass once -- the game has one window") was confirmed WRONG for at least
    // this transition -- a real log comparison showed CreateDevice's own hwnd
    // differs between the initial launch and a mid-session display-mode-change
    // recreation (two genuinely different HWNDs, not just a new device on the same
    // window). The stale one-shot guard left the OLD window subclassed and the NEW
    // one never receiving HookWndProc at all, so WM_MOUSEMOVE tracking
    // (GetLastMouseMoveClientPos) silently froze at its last value from before the
    // transition -- the custom cursor kept "working" in the sense of drawing, just
    // never updating position again. Fixed by re-subclassing whenever the hwnd
    // actually changes, restoring the previous window's original WndProc first
    // (cleanup, even though that window is very likely already destroyed by this
    // point) rather than assuming the game has exactly one window for its whole
    // lifetime.
    if (g_wndProcHooked && hwnd == g_gameHwnd) return; // already subclassed, same window
    if (g_wndProcHooked && g_origWndProc) {
        SetWindowLongPtrA(g_gameHwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(g_origWndProc));
    }
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

    // Research diagnostic (2026-08-16, long-term "why does MW3 look worse than
    // BO1/BO2" investigation -- re_notes/known_issues.md, texture/resolution-cap
    // angle): does the engine ever request a real D3D backbuffer smaller than the
    // real window it's shown in (and get stretched to fill it)? BackBufferWidth/
    // BackBufferHeight are the first two DWORDs of D3DPRESENT_PARAMETERS -- this
    // project already confirmed a live instance of exactly this kind of mismatch
    // in an unrelated context (overlay_hud.cpp's viewport-vs-window-size glyph-
    // editor debugging), but never logged it at the actual moment the engine
    // REQUESTS the backbuffer. Deliberately just two DWORD reads + one GetClientRect
    // -- no dvar reads here (Dvar_FindVar-based real_settings.h getters are
    // documented NOT yet live-tested end-to-end, and this exact function is where
    // issue #76's loader-lock hang was already found once from an unrelated overly-
    // early engine call -- not repeating that risk class for a research-only log line).
    if (pPresentationParameters) {
        DWORD backBufferWidth = *reinterpret_cast<DWORD*>(pPresentationParameters);
        DWORD backBufferHeight = *(reinterpret_cast<DWORD*>(pPresentationParameters) + 1);
        RECT clientRect{};
        GetClientRect(hFocusWindow, &clientRect);
        char resLogBuf[160];
        sprintf_s(resLogBuf, "[d3d9-hook] [res-diag] backbuffer=%lux%lu windowClient=%ldx%ld",
            backBufferWidth, backBufferHeight, clientRect.right - clientRect.left, clientRect.bottom - clientRect.top);
        LogFromController(resLogBuf);
    }

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

// Real left mouse button held state, for the custom Options screen's own click
// hit-testing (overlay_hud.cpp) -- see g_leftMouseButtonHeld's own comment.
extern "C" bool IsLeftMouseButtonHeld()
{
    return g_leftMouseButtonHeld;
}

// BUG-001 (stream co-op report, 2026-08-02): "mouse cursor appears during co-op
// gameplay despite this not occurring during solo gameplay" -- root cause is that
// the custom cursor's visibility gate (DrawCustomCursorIfNeeded) mirrors the NATIVE
// cursor's own visibility flags exactly (by design, see that function's comment),
// and co-op's own nameplate-display state apparently makes the native game consider
// the cursor "visible" even though the player is actively using a controller with no
// real mouse/keyboard input at all -- something solo play never triggers. Report's
// own suggested fix: track which input method was used more recently and only show
// the cursor for real keyboard/mouse activity. This is that signal for the
// keyboard/mouse side (see analog_input_hooks.cpp's GetLastControllerActivityTickMs
// for the controller side) -- deliberately WM_MOUSEMOVE only, not WM_KEYDOWN/other
// messages: several real gameplay features in this project (ready-up's F5, the
// squadmate call-in, Hold Breath) synthesize real keyboard messages via PostMessageA
// in reaction to a CONTROLLER press, and those must never be misread as "the user
// just touched the keyboard" -- mouse movement is never something this project
// synthesizes anywhere, so it's a clean, unambiguous real-input-only signal.
extern "C" DWORD GetLastMouseMoveTickMs()
{
    return g_lastMouseMoveTickMs;
}

// Called from dllmain.cpp's Direct3DCreate9 implementation with the real IDirect3D9*
// (kept as void* across this boundary -- dllmain.cpp deliberately keeps IDirect3D9
// opaque to avoid a d3d9.h include collision with its naked export forwarding stubs).
extern "C" void HookD3D9CreateDevice(void* realD3D9)
{
    if (!realD3D9) return;
    InstallGetCursorPosHook();
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
