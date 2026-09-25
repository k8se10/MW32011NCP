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
#include <dbghelp.h>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include "../third_party/minhook/include/MinHook.h"
#include "overlay_hud.h"
#include "mod_config.h"
#include "game_exe_detect.h"

#pragma comment(lib, "dbghelp.lib")

extern void LogFromController(const char* msg);
extern bool IsDxvkActive(); // dllmain.cpp -- see g_dxvkActive's own comment there
#if defined(_M_X64) || defined(_WIN64)
extern bool ResolveAndCacheDxvkVulkanHandlesX64(IUnknown* d3d9Device); // streamline_integration_x64.cpp,
    // 2026-09-26 -- real DXVK Vulkan instance/device/queue resolution, independent of Streamline.
extern bool TryInitStreamlineX64(IUnknown* d3d9Device); // streamline_integration_x64.cpp,
    // 2026-09-24 -- deliberately called from here (after CreateDevice returns), NOT from
    // DllMain -- see that file's own header comment and dllmain.cpp's TryLoadVendoredDxvk()
    // for the real loader-lock-hang history behind that choice. Takes the just-created
    // device so it can QueryInterface DXVK's Vulkan-interop interface for slSetVulkanInfo.
#endif
extern "C" void __cdecl InjectMenuInputTick(); // defined in analog_input_hooks.cpp
extern "C" bool IsGlyphPositionEditModeActive(); // defined in analog_input_hooks.cpp

// Real result of the [null-rt-shadow-cap-diag] hardware-capability probe
// (Hook_CreateDevice, below) -- true file-scope statics, NOT declared inside
// the anonymous namespace below, so the exported accessor after the
// namespace's own close can see and return them via ordinary unqualified
// lookup falling through to this enclosing scope (the same established
// pattern this project uses elsewhere to avoid the anonymous-namespace
// internal-linkage trap -- see analog_input_hooks_x64.cpp's own
// RecordRenderViewFireX64 comment for the full explanation of why an
// `extern` declared INSIDE the namespace would silently NOT bind here).
static bool g_nullRtShadowCapableX64 = false;
static bool g_nullRtShadowCapabilityKnownX64 = false;

#if defined(_M_X64) || defined(_WIN64)
extern void TriggerSelfSamplingProfileX64(); // self_sampling_profiler_x64.cpp
extern void TriggerGpuCaptureX64(); // renderdoc_capture_x64.cpp
#endif

namespace {

// TriggerSelfMemoryDumpX64 -- added 2026-09-16 after live memory investigation
// via BOTH a live debugger (x64dbg) and a plain external ReadProcessMemory-only
// tool independently crashed the game, specifically once real gameplay started
// (see known_issues_x64.md's newest round for the full incident trail). Direct
// user insight: an EXTERNAL process holding any kind of handle to this game
// appears to be what triggers the reaction, whatever it is -- so do the capture
// from CODE ALREADY RUNNING INSIDE THE GAME PROCESS instead. This calls
// MiniDumpWriteDump against GetCurrentProcess() -- the exact same DbgHelp API
// Windows itself already uses to write the crash dumps this project has
// analyzed before (%LOCALAPPDATA%\CrashDumps, see the 2026-09-05/2026-09-13/14
// sprintf_s crash investigations) -- there is no external handle, no
// DebugActiveProcess, no separate process involved at all; the game is asking
// itself to write its own memory to disk, the same category of operation as an
// ordinary unhandled-exception crash dump, just triggered voluntarily on a
// keypress instead of by a real crash. MiniDumpWithFullMemory captures every
// thread's full context/stack plus all committed memory, enough to statically
// walk the render/UI thread's real call stack at the exact moment of capture --
// the same information a live breakpoint would have given us, without ever
// needing to attach to or resume the live process from outside it.
void TriggerSelfMemoryDumpX64()
{
    char path[MAX_PATH];
    SYSTEMTIME st;
    GetLocalTime(&st);
    sprintf_s(path, "selfdump_%04d%02d%02d_%02d%02d%02d.dmp",
              st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);

    HANDLE hFile = CreateFileA(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) {
        LogFromController("[self-dump] CreateFileA failed, could not write dump");
        return;
    }

    // Switched from MiniDumpWithFullMemory, 2026-09-26 -- direct user report/
    // fix: the first real F9 capture came out at ~10GB despite Task Manager
    // showing iw5sp.exe's own working set at ~2.4GB. Real, non-suspicious
    // cause, not a bug in this function: MiniDumpWithFullMemory captures
    // EVERY committed page in the process's address space, including this
    // project's own IwdReadAccelEnabled feature's real memory-mapped .iwd
    // archive file views (WaitCoalescingEnabled/analog_input_hooks_x64.cpp's
    // sibling feature) -- committed VA, not resident working set, so it
    // never shows up in Task Manager's Memory column but still gets fully
    // captured. Direct user instruction: "we just need the bounded game
    // memory... weve identified it lives in subsections of the memory based
    // on x86 approach" -- MiniDumpWithPrivateReadWriteMemory captures the
    // process's own private (not file-backed/shared) read-write memory --
    // real heaps, stacks, static data -- while excluding exactly the giant
    // mapped-file views that bloated the first capture. MiniDumpWithDataSegs
    // adds the module .data/.bss sections (global variables like every
    // DAT_141xxxxxx this project's own RE work already tracks by address);
    // MiniDumpWithThreadInfo/MiniDumpWithFullMemoryInfo keep real per-thread
    // context/stack-walk data, still needed for the same "walk the render
    // thread's real call stack at the exact capture moment" purpose this
    // tool was originally built for.
    // Fixed 2026-09-26, round 2 -- direct live report: the bounded combo from
    // round 1 failed with ERROR_INVALID_PARAMETER (0x80070057) specifically
    // during in-game-pause captures, and a same-day retest showed the
    // round-1 fallback (MiniDumpWithDataSegs | MiniDumpWithPrivateReadWriteMemory)
    // ALSO fails with the identical error in the same scenario -- ruling out
    // MiniDumpWithHandleData/MiniDumpWithFullMemoryInfo as the specific
    // culprits, since a combo without either of them still fails the same
    // way. This points at something more fundamental than one bad flag pair
    // -- possibly a thread/driver-state issue specific to the paused render
    // loop that DbgHelp's internal thread-suspend/GetThreadContext walk
    // can't handle regardless of which optional streams are requested. Real
    // second bug found and fixed here too: a failed MiniDumpWriteDump call
    // can partially write to the file handle before erroring, so retrying
    // on the SAME handle risked feeding a corrupted/appended file into the
    // next attempt -- each tier below now reopens (CREATE_ALWAYS, truncating)
    // a fresh handle rather than reusing one across attempts. Three tiers
    // now attempted in order: the original bounded combo, the round-1
    // fallback, and finally bare `MiniDumpNormal` (0, no extra flags at all)
    // -- the most minimal, universally-supported combination DbgHelp
    // supports, still enough to recover real thread stacks/module list/
    // exception info even if every optional stream this tool wanted is
    // refused. If even THIS tier fails, the real cause isn't a flag choice
    // at all and needs a different investigation angle entirely.
    struct DumpTier { MINIDUMP_TYPE type; const char* label; };
    const DumpTier kTiers[] = {
        { static_cast<MINIDUMP_TYPE>(MiniDumpWithDataSegs | MiniDumpWithPrivateReadWriteMemory |
                                      MiniDumpWithHandleData | MiniDumpWithThreadInfo | MiniDumpWithFullMemoryInfo),
          "full" },
        { static_cast<MINIDUMP_TYPE>(MiniDumpWithDataSegs | MiniDumpWithPrivateReadWriteMemory),
          "reduced" },
        { MiniDumpNormal, "minimal" },
    };

    BOOL ok = FALSE;
    DWORD tierErrs[3] = { 0, 0, 0 };
    int succeededTier = -1;
    for (int i = 0; i < 3 && !ok; ++i) {
        if (i > 0) {
            // Fresh handle per tier -- a prior failed attempt may have
            // partially written to the file before erroring out.
            CloseHandle(hFile);
            hFile = CreateFileA(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (hFile == INVALID_HANDLE_VALUE) {
                tierErrs[i] = GetLastError();
                continue;
            }
        }
        ok = MiniDumpWriteDump(
            GetCurrentProcess(), GetCurrentProcessId(), hFile,
            kTiers[i].type, nullptr, nullptr, nullptr);
        tierErrs[i] = ok ? 0 : GetLastError();
        if (ok) succeededTier = i;
    }

    CloseHandle(hFile);

    char logBuf[320];
    if (ok && succeededTier == 0) {
        sprintf_s(logBuf, "[self-dump] Wrote %s (self-triggered, no external handle)", path);
    } else if (ok) {
        sprintf_s(logBuf, "[self-dump] Wrote %s via '%s' tier (earlier tier(s) failed, first=%lu)",
                  path, kTiers[succeededTier].label, tierErrs[0]);
    } else {
        sprintf_s(logBuf, "[self-dump] MiniDumpWriteDump FAILED at all 3 tiers (full=%lu, reduced=%lu, minimal=%lu)",
                  tierErrs[0], tierErrs[1], tierErrs[2]);
    }
    LogFromController(logBuf);
}

#if defined(_M_X64) || defined(_WIN64)
// Forward-declared so HookWndProc (defined earlier in this file than the
// function itself) can call it periodically -- see that function's own
// definition, further down, for the full "needs re-firing per level, not
// just once per process" rationale. Must live INSIDE this anonymous
// namespace (not before it) -- a forward declaration at global scope for a
// function actually defined inside the namespace mangles to a DIFFERENT
// symbol, which is exactly the LNK2019 this project's own established
// linkage lesson (CLAUDE.md's "checking is far cheaper than digging")
// already warns about, caught here immediately via the real build error
// rather than left unnoticed.
void SendPeriodicActivationNudgeX64(HWND hwnd);
#endif

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

// Glyph position editor mouse isolation, part 3 (2026-08-24, live-reported: gameplay
// hint calibration still fought the real game's own mouse-look while dragging --
// unlike the menu case (part 2 above), an actual GAMEPLAY session's look input isn't
// driven by WM_MOUSEMOVE/GetCursorPos at all: re_notes/iw5sp.md documents the real
// look pipeline (FUN_0057d680) reading from its own internal double-buffered
// accumulator via a non-cdecl, register-based calling convention -- too risky to hook
// directly (per that same doc's own "hooking this needs care about calling
// convention" warning, and this project's own "no guessing" standard for anything
// touching core engine internals). Whatever actually FEEDS that accumulator each
// frame is still an OS-level mouse capture, though (this engine has no DirectInput
// import at all -- confirmed, see CLAUDE.md's own original findings -- so it's either
// SetCapture-based tracking or a ClipCursor-confined delta read, both plain user32
// exports). Rather than guess which and hook the wrong one, this hooks BOTH, same
// low-risk technique as Hook_GetCursorPos immediately above (plain WINAPI signature,
// no calling-convention risk at all) -- while the editor is active, the real game's
// own SetCapture/ClipCursor requests are swallowed entirely (never forwarded), so
// whichever one the engine actually relies on for its mouse-look accumulator loses
// its OS-level capture/confinement and the accumulator stops updating from real
// mouse movement, while this project's OWN drag logic (GetLastMouseMoveClientPos,
// entirely separate, unaffected by either) keeps working throughout.
typedef HWND(WINAPI* SetCapture_t)(HWND);
SetCapture_t g_origSetCapture = nullptr;
typedef BOOL(WINAPI* ClipCursor_t)(const RECT*);
ClipCursor_t g_origClipCursor = nullptr;
bool g_mouseCaptureHooksInstalled = false;

HWND WINAPI Hook_SetCapture(HWND hWnd)
{
    if (IsGlyphPositionEditModeActive()) {
        return nullptr; // never grant the real game mouse capture while editing
    }
    if (!g_origSetCapture) return nullptr;
    return g_origSetCapture(hWnd);
}

BOOL WINAPI Hook_ClipCursor(const RECT* lpRect)
{
    if (IsGlyphPositionEditModeActive()) {
        // Force the cursor free regardless of what the real game asked to clip to --
        // if it's already confined from before editing started, this releases it;
        // if it wasn't, this is a harmless no-op. Doesn't call g_origClipCursor at
        // all, so the real game's own requested rect is dropped entirely, not just
        // overridden this once (it would otherwise just re-clip next frame).
        if (g_origClipCursor) g_origClipCursor(nullptr);
        return TRUE;
    }
    if (!g_origClipCursor) return FALSE;
    return g_origClipCursor(lpRect);
}

void InstallMouseCaptureSuppressionHooks()
{
    if (g_mouseCaptureHooksInstalled) return;
    g_mouseCaptureHooksInstalled = true;
    MH_Initialize(); // idempotent
    HMODULE user32 = GetModuleHandleA("user32.dll");
    char buf[144];

    void* realSetCapture = user32 ? reinterpret_cast<void*>(GetProcAddress(user32, "SetCapture")) : nullptr;
    if (!realSetCapture) {
        LogFromController("[glyph-editor] SetCapture hook: GetProcAddress(user32.dll, \"SetCapture\") failed");
    } else {
        MH_STATUS s = MH_CreateHook(realSetCapture, reinterpret_cast<void*>(&Hook_SetCapture),
            reinterpret_cast<void**>(&g_origSetCapture));
        sprintf_s(buf, "[glyph-editor] MH_CreateHook(SetCapture @ %p) = %d", realSetCapture, static_cast<int>(s));
        LogFromController(buf);
        if (s == MH_OK) {
            MH_STATUS e = MH_EnableHook(realSetCapture);
            sprintf_s(buf, "[glyph-editor] MH_EnableHook(SetCapture) = %d", static_cast<int>(e));
            LogFromController(buf);
        }
    }

    void* realClipCursor = user32 ? reinterpret_cast<void*>(GetProcAddress(user32, "ClipCursor")) : nullptr;
    if (!realClipCursor) {
        LogFromController("[glyph-editor] ClipCursor hook: GetProcAddress(user32.dll, \"ClipCursor\") failed");
    } else {
        MH_STATUS s = MH_CreateHook(realClipCursor, reinterpret_cast<void*>(&Hook_ClipCursor),
            reinterpret_cast<void**>(&g_origClipCursor));
        sprintf_s(buf, "[glyph-editor] MH_CreateHook(ClipCursor @ %p) = %d", realClipCursor, static_cast<int>(s));
        LogFromController(buf);
        if (s == MH_OK) {
            MH_STATUS e = MH_EnableHook(realClipCursor);
            sprintf_s(buf, "[glyph-editor] MH_EnableHook(ClipCursor) = %d", static_cast<int>(e));
            LogFromController(buf);
        }
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

extern "C" void NotifyWindowFocusLostX64(); // analog_input_hooks_x64.cpp

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

    // F9 -- self-triggered live memory dump (2026-09-16). See
    // TriggerSelfMemoryDumpX64's own header comment above for the full
    // reasoning. Deliberately NOT gated on g_keybindCaptureActive or any menu
    // state -- this needs to fire at the exact real-gameplay moment a prompt
    // like Survival ready-up/buy-station is on screen, which is whenever the
    // user presses it, not just while a menu is focused.
#if defined(_M_X64) || defined(_WIN64)
    // Window lost focus (alt-tab etc.): re-arms the once-per-level input sweep (analog_input_hooks_x64.cpp) -- and
    // only this or a level load does, not an ordinary unpause.
    if ((msg == WM_ACTIVATEAPP && wParam == FALSE) || msg == WM_KILLFOCUS) NotifyWindowFocusLostX64();
#endif

    if (msg == WM_KEYDOWN && wParam == VK_F9 && (lParam & 0x40000000) == 0) {
        // High bit of lParam's repeat-count/previous-key-state (bit 30) is the
        // "was already down" flag -- only fire once per physical press, not
        // once per Windows key-repeat tick while held.
        TriggerSelfMemoryDumpX64();
    }

#if defined(_M_X64) || defined(_WIN64)
    // Bug found live 2026-09-26: a bare F10 press (no Alt/Ctrl held) is delivered
    // by Windows as WM_SYSKEYDOWN, not WM_KEYDOWN -- a well-known legacy quirk,
    // since F10 alone also activates the menu-bar/system-menu context (this same
    // file's own keybind-capture editor, line ~525, already handles both for the
    // identical reason). Checking WM_KEYDOWN only meant this trigger silently
    // never fired for a real, unmodified F10 press.
    if ((msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN) && wParam == VK_F10 && (lParam & 0x40000000) == 0) {
        // See self_sampling_profiler_x64.cpp's own header comment for why this
        // exists (a real in-process sampling profiler, not an external debugger
        // attach) and how it differs from F9's single-instant memory dump.
        TriggerSelfSamplingProfileX64();
    }

    if (msg == WM_KEYDOWN && wParam == VK_F11 && (lParam & 0x40000000) == 0) {
        // See renderdoc_capture_x64.cpp's own header comment -- a real GPU-side
        // frame capture via RenderDoc's official in-application API, the class of
        // data neither F9's memory dump nor F10's CPU thread sampler can produce.
        TriggerGpuCaptureX64();
    }
#endif

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

#if defined(_M_X64) || defined(_WIN64)
    // Real structural gap found 2026-09-04, after diagnostic heartbeat data +
    // direct user clarification ("check it i played a bit after but yeah
    // always on entry of level as we had in x86"): both
    // SendSyntheticActivationClick and SendRealFocusNudgeX64 only ever fired
    // ONCE, from InstallWndProcHook -- which itself only runs once per game
    // SESSION (CreateDevice's own hwnd doesn't change across an ordinary
    // level load, so the subclass/one-shot-fire logic never re-triggers for
    // one). If the real x64 gate needs this activation-style event on EVERY
    // level entry -- exactly x86's own original issue #1 shape (a per-level
    // transition, not a one-time launch quirk; x86's real fix for THAT issue
    // was a windowed re-assertion tied to level load, not a single one-shot
    // event either) -- neither experiment could ever have worked, regardless
    // of which one's underlying theory was closer to correct: they simply
    // never got a chance to run again for the second, third, Nth level.
    // Fixed by re-firing periodically for the whole session via this
    // already-existing ~60Hz WM_TIMER tick -- see
    // SendPeriodicActivationNudgeX64's own comment for why this calls a
    // NEW, click-free function rather than the original
    // SendSyntheticActivationClick (which stays one-shot-only, unchanged).
    // Rate-limited to once every 2 seconds (matching x86's own original
    // "3-second window" scale for this exact bug class).
    //
    // 2026-09-15, live-caught regression -- gated to iw5sp.exe ONLY. This
    // function was designed and confirmed needed for exactly one thing:
    // SP's own "needs an initial click at launch" gate (CLAUDE.md's own
    // SS10.8 policy already warns a mechanism confirmed for one binary is
    // never assumed to carry over to the other unverified -- this call site
    // was the one place that policy hadn't actually been applied yet,
    // because it predates this project's later SP/MP detection work
    // entirely). It was running completely unconditionally for BOTH
    // binaries -- every 2 seconds, for the whole session, it injects a real
    // WM_ACTIVATE/WM_SETFOCUS pair straight into the engine's own WndProc
    // plus real OS-level SetForegroundWindow/SetActiveWindow/SetFocus calls.
    // Direct live report under iw5mp.exe (TDM, then confirmed again in
    // Domination): "keyboard input regression on mp sprint is intermittent
    // it stops triggering randomly... it goes to sub 1s" -- exactly the
    // symptom shape a focus-reassertion firing while a key is actively held
    // would produce, if the engine's own input layer treats a focus
    // transition as a signal to clear held-key state (extremely common,
    // legitimate engine behavior, meant to avoid stuck keys after a real
    // alt-tab) -- MP was never confirmed to need or safely tolerate this
    // nudge at all, since the "needs a click" bug this function exists to
    // fix was only ever reported against SP. No live report of this
    // symptom under SP itself, so SP keeps the existing, already-proven
    // behavior unchanged; only MP (and any unrecognized executable, as a
    // fail-safe) now skips this call entirely.
    // 2026-09-21: DISABLED for SP too. The real "needs an initial click" root cause was fixed natively
    // (ForceReleaseStuckKbuttonsX64, 2026-09-16), so this 2s WM_ACTIVATE/SetForegroundWindow workaround is
    // obsolete -- and it was live-reported as making the game pause itself intermittently.
    if (false && GetDetectedGameExecutable() == GameExecutable::SP) {
        static DWORD s_lastPeriodicNudgeMs = 0;
        DWORD nowMs = GetTickCount();
        if (g_gameHwnd && (nowMs - s_lastPeriodicNudgeMs >= 2000)) {
            s_lastPeriodicNudgeMs = nowMs;
            SendPeriodicActivationNudgeX64(g_gameHwnd);
        }
    }
#endif

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

#if defined(_M_X64) || defined(_WIN64)
// ---- x64 real-focus nudge experiment (2026-09-04) -----------------------------------
//
// Live-reported: the exact same class of symptom SendSyntheticActivationClick above
// was built to fix on x86 ("needs an initial click at launch" -- known_issues.md
// issues #1/#27/#42) is BACK on x64, even though that same synthetic-message
// function is confirmed still firing here too (`[focus-gate-fix]` appears in
// proxy_d3d9.log every x64 session). This means the x86 fix's own MECHANISM
// (synthesize WM_ACTIVATE/WM_SETFOCUS/click messages THROUGH the engine's own
// WndProc via CallWindowProcA, deliberately never touching real OS focus state --
// "no SetForegroundWindow, no stealing focus," per that function's own original
// comment) isn't sufficient for whatever x64's OWN equivalent internal gate
// actually checks. Per this project's own standing principle (CLAUDE.md SS10.8 --
// x86/x64 are separately-built binaries, don't assume a mechanism carries over
// unverified), this is NOT assumed to be the same crouch-specific guard-byte pair
// x86's fix targeted (crouch input isn't even wired on x64 yet as of this
// session) -- it's a genuinely new x64 investigation, still open.
//
// The one real, testable difference between "a synthesized message reaches the
// WndProc" and "a real user click": a genuine click also changes actual OS-level
// window state (GetForegroundWindow/GetActiveWindow/GetFocus) that
// CallWindowProcA's direct-dispatch approach never touches, by design, on x86.
// If x64's own gate reads THAT real OS state (rather than reacting purely to the
// message content, which is what x86's own gate apparently did), the synthetic-
// message-only approach would never satisfy it. This function tests that theory
// empirically -- EXPERIMENTAL, not a confirmed fix, same honesty standard as the
// original x86 experiment above. x64-only: x86 is already confirmed working
// without this (live-tested, "never clicked the window once"), so this must not
// risk regressing it -- SetForegroundWindow specifically CAN steal focus from an
// unrelated window if misused, which is exactly why x86's own fix deliberately
// avoided it; gated here to only ever run on the x64 build.
void SendRealFocusNudgeX64(HWND hwnd)
{
    SetForegroundWindow(hwnd);
    SetActiveWindow(hwnd);
    SetFocus(hwnd);
    LogFromController("[focus-gate-fix-x64] real SetForegroundWindow/SetActiveWindow/SetFocus "
                       "issued (x64-only experiment, distinct from the synthetic-message-only "
                       "approach already proven sufficient on x86) -- confirm live whether "
                       "input now works without a manual click.");
}

// ---- Periodic (per-level, not just per-process) activation nudge, x64 --------------
//
// 2026-09-04, real gap found via diagnostic data + direct user clarification: both
// SendSyntheticActivationClick and SendRealFocusNudgeX64 above only ever fire ONCE,
// from InstallWndProcHook -- which itself only runs once per game session (the hwnd
// doesn't change across an ordinary level load). If the real x64 gate needs this
// activation event on EVERY level entry (confirmed repro shape: "always on entry of
// level as we had in x86", matching x86's own issue #1 -- a per-LEVEL transition
// bug, not a one-time launch quirk), neither one-shot experiment could ever have
// worked regardless of which theory was closer to correct.
//
// Deliberately does NOT include SendSyntheticActivationClick's own
// WM_LBUTTONDOWN/WM_LBUTTONUP click simulation -- that function's own "(1,1) as the
// click coordinate" safety reasoning only holds for a ONE-TIME fire before any real
// UI or gameplay exists yet (right at device creation). Repeating a real click every
// 2 seconds for an entire play session is a genuinely different risk: if the game's
// own WndProc treats WM_LBUTTONDOWN as a real input event (e.g. keyboard/mouse Fire,
// or a world-interact prompt), firing it periodically DURING ACTUAL GAMEPLAY could
// misfire a real gameplay action -- a regression risk the one-shot version never
// had. This function only re-asserts WM_ACTIVATE/WM_SETFOCUS (through the engine's
// own WndProc, same as the one-shot version) plus the real OS-level
// SetForegroundWindow/SetActiveWindow/SetFocus calls (also already proven side-
// effect-free for a single fire) -- no click, safe to repeat indefinitely.
void SendPeriodicActivationNudgeX64(HWND hwnd)
{
    if (g_origWndProc) {
        CallWindowProcA(g_origWndProc, hwnd, WM_ACTIVATE, MAKEWPARAM(WA_ACTIVE, 0),
                         reinterpret_cast<LPARAM>(hwnd));
        CallWindowProcA(g_origWndProc, hwnd, WM_SETFOCUS, 0, 0);
    }
    SetForegroundWindow(hwnd);
    SetActiveWindow(hwnd);
    SetFocus(hwnd);
}
#endif

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

// ---- First-launch welcome modal (2026-09-22) -------------------------------------------------------------------
// Replaces the old high-render-scale warning modal (its x86 wording was obsolete): shown ONCE per mod version, then
// the version is recorded in a tiny state file beside the game exe so it never reappears until the next release.
// kWelcomeFeatureList is LIVE CONTENT -- update it whenever the feature set changes (CLAUDE.md / AGENTS.md rule).
constexpr const char* kWelcomeFeatureList =
    "\x02" "\xE2\x9C\x94 Native controller support and menu navigation\n"
    "\x02" "\xE2\x9C\x94 Controller button prompts and vibration\n"
    "\x02" "\xE2\x9C\x94 Four netcode security fixes\n"
    "\x02" "\xE2\x9C\x94 Frame pacing and faster loading\n"
    "\x02" "\xE2\x9C\x94 Render scale (now including MP), FSR, motion blur\n"
    "\x02" "\xE2\x9C\x94 Plugin API for sub-mods";
    // 2026-09-23: "anisotropic" REMOVED from this list -- known_issues_x64.md issue #6
    // found ForceAnisotropicFiltering/ForceHighQualityShadows/ForceHighQualityLighting
    // are silent no-ops on x64 (the underlying native dvar-write function has no x64
    // equivalent yet) -- this list had been claiming a broken feature as working since
    // the port. "motion blur" stays a real claim -- confirmed working (2026-09-12/13)
    // and now also fixed for keyboard/mouse this same release.

bool ShowWelcomeModalIfNewVersion()
{
    char path[MAX_PATH] = {};
    GetModuleFileNameA(nullptr, path, MAX_PATH);
    char* slash = strrchr(path, '\\');
    if (slash) *(slash + 1) = '\0';
    strcat_s(path, "mw3ncp_state.ini");
    char seen[64] = {};
    GetPrivateProfileStringA("State", "WelcomeShownVersion", "", seen, sizeof(seen), path);
    if (strcmp(seen, kModVersionString) == 0) return false;

    char msg[1024];
    sprintf_s(msg,
              "\x03" "Thanks for downloading MW32011NCP (Native Community Patches) v%s.\n\n"
              "\x03" "This version includes:\n%s\n\n"
              "\x01" "\xE2\x9A\xA0 EARLY RELEASE: expect hidden bugs and unfinished or unported features. Survival is the only"
              " recommended mode for now (controller support in Campaign and Multiplayer is incomplete or unsupported)." 
              "\n\n"
              "\x02" "\xE2\x9C\x94 The netcode security fixes protect every mode, Multiplayer included.\n\n"
              "Settings live in mw3ncp_config.ini.\n\nEnter / Space / Click to continue:",
              kModVersionString, kWelcomeFeatureList);
    ShowOverlayMessageUntilDismissed(msg, OverlayAnimStyle::Plain);
    WritePrivateProfileStringA("State", "WelcomeShownVersion", kModVersionString, path);
    return true;
}

// ---- Possibly-outdated modal (2026-09-22) ------------------------------------------------------------------------
// Early releases change quickly. Every version below 0.4.0 (the beta milestone) nags, at most once per day, when the DLL
// was built more than 4 weeks ago. The build date is the compile-time __DATE__, so it is the date of THIS build.
int MonthFromName(const char* m)
{
    static const char* const kNames[12] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
    for (int i = 0; i < 12; ++i) if (strncmp(m, kNames[i], 3) == 0) return i + 1;
    return 1;
}

bool ShowOutdatedModalIfStale()
{
    int major = 0, minor = 0, patch = 0;
    sscanf_s(kModVersionString, "%d.%d.%d", &major, &minor, &patch);
    if (major > 0 || minor >= 4) return false; // 0.4.0-x64 (beta) and later never nag

    // Build date -> days since the FILETIME epoch.
    SYSTEMTIME built = {};
    built.wMonth = static_cast<WORD>(MonthFromName(__DATE__));
    built.wDay = static_cast<WORD>(atoi(__DATE__ + 4));
    built.wYear = static_cast<WORD>(atoi(__DATE__ + 7));
    FILETIME builtFt = {};
    if (!SystemTimeToFileTime(&built, &builtFt)) return false;
    SYSTEMTIME nowSt = {};
    GetSystemTime(&nowSt);
    FILETIME nowFt = {};
    SystemTimeToFileTime(&nowSt, &nowFt);
    ULARGE_INTEGER b, n;
    b.LowPart = builtFt.dwLowDateTime; b.HighPart = builtFt.dwHighDateTime;
    n.LowPart = nowFt.dwLowDateTime; n.HighPart = nowFt.dwHighDateTime;
    if (n.QuadPart <= b.QuadPart) return false;
    long long ageDays = static_cast<long long>((n.QuadPart - b.QuadPart) / (10000000ULL * 86400ULL));
    // Test hook: `[State] TestOutdated=1` in mw3ncp_state.ini pretends the build is 30 days old and ignores the once-a-day limit.
    char testPath[MAX_PATH] = {};
    GetModuleFileNameA(nullptr, testPath, MAX_PATH);
    char* testSlash = strrchr(testPath, 92); // 92 = backslash
    if (testSlash) *(testSlash + 1) = 0;
    strcat_s(testPath, "mw3ncp_state.ini");
    const bool testOutdated = GetPrivateProfileIntA("State", "TestOutdated", 0, testPath) != 0;
    if (testOutdated) ageDays = 30;
    if (ageDays < 28) return false;

    // At most once per calendar day.
    char path[MAX_PATH] = {};
    GetModuleFileNameA(nullptr, path, MAX_PATH);
    char* slash = strrchr(path, '\\');
    if (slash) *(slash + 1) = '\0';
    strcat_s(path, "mw3ncp_state.ini");
    const int today = nowSt.wYear * 10000 + nowSt.wMonth * 100 + nowSt.wDay;
    if (!testOutdated && static_cast<int>(GetPrivateProfileIntA("State", "OutdatedShownDay", 0, path)) == today) return false;

    char msg[900];
    sprintf_s(msg,
              "\x03" "This version of MW32011NCP may be out of date.\n\n"
              "\x01" "\xE2\x9A\xA0 This build (v%s) is %lld days old. Early releases change quickly and fix real bugs -- please check GitHub or"
              " Nexus for a newer version before reporting problems.\n\n"
              "Enter / Space / Click to continue:",
              kModVersionString, ageDays);
    ShowOverlayMessageUntilDismissed(msg, OverlayAnimStyle::Plain);
    char todayStr[16];
    sprintf_s(todayStr, "%d", today);
    WritePrivateProfileStringA("State", "OutdatedShownDay", todayStr, path);
    return true;
}

void ShowStartupMessage()
{
    // 1) once per version: welcome + feature list; 2) possibly-outdated (below 0.4.0, build older than 4 weeks, once a
    // day); 3) every normal launch: the short toast below, which carries the version and the early-release reminder.
    if (ShowWelcomeModalIfNewVersion()) return;
    if (ShowOutdatedModalIfStale()) return;
    srand(GetTickCount());
    if ((rand() % kVariantMessageOneInN) != 0) {
        char startedMsg[128];
        sprintf_s(startedMsg, "MW32011NCP v%s Started (early release)", kModVersionString);
        ShowOverlayMessage(startedMsg, 15000, OverlayAnimStyle::Plain);
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
#if defined(_M_X64) || defined(_WIN64)
    // 2026-09-04, live-reported: the synthetic-message-only approach above,
    // already confirmed proven sufficient on x86, is NOT sufficient on x64 --
    // see SendRealFocusNudgeX64's own comment for the full experimental
    // rationale. x64-only, deliberately not run on x86 (already working
    // without it, no reason to add risk there).
    SendRealFocusNudgeX64(hwnd);
#endif
}

// InitGraphicsApiMode (2026-09-23) -- the real branch point [Video] GraphicsApi's own
// comment (mod_config.h) refers to. Called once, right before the real CreateDevice
// call-through, since a future DXVK-backed Vulkan mode would need to intercept device
// creation itself rather than letting the real D3D9 device get created first (see
// re_notes/x64_migration/vulkan_dlss_pipeline_research.md's own hook-ordering open
// question). Currently a real, honest no-op for BOTH selections: LegacyD3D9 needs no
// action (the existing pipeline below runs unchanged either way), and Vulkan logs a
// clear "not implemented yet, falling back" message rather than silently doing
// nothing or crashing -- a player who sets GraphicsApi=Vulkan today gets an honest
// explanation in proxy_d3d9.log instead of an unexplained non-effect. Real
// implementation (DXVK vendoring, the vulkan-1.dll proxy-load hook, slSetVulkanInfo)
// lands here once that work starts; nothing below this comment should need to change
// shape when it does, only gain real branches.
// Real answer to "is Vulkan mode actually allowed to run right now" -- SP-only,
// direct instruction (2026-09-23): "make sure its conditional only SP for now as
// again we know mp holds more risk and this is qol not an essential feature for mp,
// so until we have real precedent from sp we will bring it to mp." This is a
// separate, additional gate on top of the config selection itself -- a player CAN
// set GraphicsApi=Vulkan in the ini (it's a global, not a per-binary setting; MP
// support is a real future decision, not something this gate forecloses), but it
// only actually takes effect under iw5sp.exe. Matches this project's own
// established pattern for exactly this shape of decision: MP tracks SP by a real,
// deliberate lag (the 2026-09-05 "2-4 releases behind SP until beta" cadence
// standard) rather than shipping every new feature to both binaries simultaneously,
// and every other structurally-significant, not-yet-proven-safe feature this
// project ships (AutoMantleEnabled, UseCustomOptionsScreen, the MP VAC-risk
// acknowledgment itself) defaults to the safer scope first. Real practical reason
// beyond risk alone: this is a real Vulkan/DXVK MODULE-REPLACEMENT technique (see
// vulkan_dlss_pipeline_research.md S5's own ENB depth-of-modification finding) --
// untested on the one binary (iw5mp.exe) where VAC is confirmed active at all;
// Survival co-op and Solo Campaign both sit at near-zero/low real VAC risk per that
// same research, making SP the correct, deliberate place to get real precedent
// before ever considering MP.
bool IsGraphicsApiVulkanModeAllowed()
{
    return g_modConfig.graphicsApi == GraphicsApi::Vulkan
        && GetDetectedGameExecutable() == GameExecutable::SP;
}

void InitGraphicsApiMode()
{
    if (g_modConfig.graphicsApi != GraphicsApi::Vulkan) {
        LogFromController("[graphics-api] GraphicsApi=LegacyD3D9 (default) -- existing native D3D9 pipeline, unchanged.");
        return;
    }
    if (GetDetectedGameExecutable() != GameExecutable::SP) {
        LogFromController("[graphics-api] GraphicsApi=Vulkan selected, but Vulkan mode is "
            "SP-only for now (real precedent needed on iw5sp.exe before this is ever "
            "considered for iw5mp.exe) -- forcing LegacyD3D9 behavior under this binary.");
        return;
    }
    // 2026-09-23: this used to unconditionally claim "not implemented yet, falling
    // back" -- wrong as of DXVK actually being vendored; caught via the first live
    // GraphicsApi=Vulkan test (this exact line was logged on a session where DXVK
    // HAD already loaded successfully, per the same log). Now reports the real
    // status via IsDxvkActive() (dllmain.cpp), set on TryLoadVendoredDxvk()'s own
    // success path -- the one place that actually knows which module g_realD3D9
    // ended up pointing at.
    if (IsDxvkActive()) {
        LogFromController("[graphics-api] GraphicsApi=Vulkan active under iw5sp.exe -- "
            "device creation is routing through the vendored DXVK build.");
    } else {
        LogFromController("[graphics-api] GraphicsApi=Vulkan selected under iw5sp.exe, but "
            "no vendored DXVK build was loaded (see the earlier [graphics-api] line from "
            "startup for the real reason) -- falling back to LegacyD3D9 behavior.");
    }
}

HRESULT WINAPI Hook_CreateDevice(void* This, UINT Adapter, DWORD DeviceType,
    HWND hFocusWindow, DWORD BehaviorFlags, void* pPresentationParameters,
    void** ppReturnedDeviceInterface)
{
    InitGraphicsApiMode();

    HRESULT hr = g_origCreateDevice(This, Adapter, DeviceType, hFocusWindow, BehaviorFlags,
        pPresentationParameters, ppReturnedDeviceInterface);

#if defined(_M_X64) || defined(_WIN64)
    // MW32011NCP, 2026-09-24: real, safe point to init Streamline -- the real
    // Vulkan device (via DXVK, if GraphicsApi=Vulkan) now exists, and we're
    // long past DllMain's own loader-lock window (a real, live-reproduced
    // hang confirmed calling this from DllMain deadlocks -- see
    // TryLoadVendoredDxvk()'s own comment, dllmain.cpp). Gated to fire once
    // -- CreateDevice can in principle be called more than once (Reset/
    // device-loss recovery paths elsewhere in this project already handle
    // that for other state), Streamline's own slInit() should not be
    // re-invoked on every call. Only attempted once CreateDevice actually
    // produced a device -- slSetVulkanInfo needs DXVK's VkDevice behind it,
    // and a failed first CreateDevice must not burn the one-shot attempt.
    static bool s_streamlineInitAttempted = false;
    if (!s_streamlineInitAttempted && SUCCEEDED(hr) && ppReturnedDeviceInterface &&
        *ppReturnedDeviceInterface) {
        s_streamlineInitAttempted = true;
        TryInitStreamlineX64(static_cast<IUnknown*>(*ppReturnedDeviceInterface));

        // 2026-09-26: real DXVK Vulkan instance/device/queue resolution, deliberately
        // independent of StreamlineEnabled -- gpu_timing_probe_x64.cpp's own GPU-inclusive
        // frame-timing diagnostic needs these too and has nothing to do with Streamline/DLSS.
        // Idempotent (ResolveAndCacheDxvkVulkanHandlesX64 returns the cached result if
        // TryInitStreamlineX64 above already resolved them), so calling it unconditionally
        // here whenever this is a real DXVK device is safe and never redundant work.
        if (g_modConfig.graphicsApi == GraphicsApi::Vulkan) {
            ResolveAndCacheDxvkVulkanHandlesX64(static_cast<IUnknown*>(*ppReturnedDeviceInterface));
        }
    }
#endif

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

    // Renderer-backend diagnostic (2026-08-26, D3D11/12/Vulkan investigation --
    // see the approved plan's own "Renderer-backend questions" section): confirms
    // whether this session is really running through D3D9On12 (a real Microsoft
    // OS component that maps D3D9 onto D3D12 -- NOT a third-party DLL swap, the
    // real d3d9.dll this proxy already forwards to is still what's in use either
    // way) or a native D3D9 driver. Two independent, real checks, logged as raw
    // evidence rather than a guessed conclusion:
    // 1. GetModuleHandleA("d3d9on12.dll") -- Microsoft's own documentation
    //    confirms this DLL is loaded into the process specifically when a
    //    D3D9On12 device is created, so a non-null handle is a direct positive.
    // 2. IDirect3D9::GetAdapterIdentifier (vtable index 5, standard fixed COM
    //    layout, same class of call already relied on elsewhere in this file for
    //    GetAdapterModeCount/EnumAdapterModes at indices 6/7) -- D3D9On12 is
    //    documented to report itself distinctly in Driver/Description versus a
    //    real GPU vendor driver filename (e.g. nvldumdx.dll/aticfx64.dll).
    // `This` here is the real IDirect3D9* this hook's own CreateDevice method
    // was called through -- no separate pointer needed.
    {
        struct D3DADAPTER_IDENTIFIER9_LOCAL {
            char Driver[512];
            char Description[512];
            char DeviceName[32];
            LARGE_INTEGER DriverVersion;
            DWORD VendorId;
            DWORD DeviceId;
            DWORD SubSysId;
            DWORD Revision;
            GUID DeviceIdentifier;
            DWORD WHQLLevel;
        };
        void** d3d9VtableForDiag = *reinterpret_cast<void***>(This);
        using GetAdapterIdentifierFn = HRESULT(WINAPI*)(void*, UINT, DWORD, void*);
        auto getAdapterIdentifier = reinterpret_cast<GetAdapterIdentifierFn>(d3d9VtableForDiag[5]);
        D3DADAPTER_IDENTIFIER9_LOCAL ident{};
        HRESULT idHr = getAdapterIdentifier(This, Adapter, 0, &ident);
        bool d3d9on12Loaded = (GetModuleHandleA("d3d9on12.dll") != nullptr);
        char diagBuf[700];
        if (SUCCEEDED(idHr)) {
            sprintf_s(diagBuf, "[d3d9on12-diag] d3d9on12.dll loaded=%s adapter driver=\"%s\" description=\"%s\"",
                d3d9on12Loaded ? "yes" : "no", ident.Driver, ident.Description);
        } else {
            sprintf_s(diagBuf, "[d3d9on12-diag] d3d9on12.dll loaded=%s (GetAdapterIdentifier failed hr=0x%08lX)",
                d3d9on12Loaded ? "yes" : "no", static_cast<unsigned long>(idHr));
        }
        LogFromController(diagBuf);
    }

    // NULL-render-target shadow-map capability diagnostic (2026-09-26,
    // issue #4's real-transitions investigation -- re_notes/known_issues_x64.md).
    // Read-only, no behavior change: replicates the exact hardware-capability
    // probe x86's own FUN_00679260 performs (found via FindDataWriters.java
    // against the real writer of DAT_021d35f4) to settle whether x86's own
    // detection criteria would classify THIS machine's real GPU as "capable"
    // of the fast NULL-render-target shadow-map path x86 uses to SKIP its
    // per-light render-view-activator call entirely (see FUN_00698f10's own
    // `if (DAT_021d35f4 == '\0')` gate, and its x64 counterpart FUN_140196ad0,
    // which has no such gate at all -- the leading root-cause candidate for
    // the live-captured 6/7-alternation transition storm). Real IDirect3D9
    // vtable slots (same across x86/x64, only the byte-offset stride differs):
    // slot 10 = CheckDeviceFormat, slot 12 = CheckDepthStencilMatch. Tries
    // the same 4 (DepthStencilFormat, RenderTargetFormat) pairs x86's own
    // FUN_00679260 does, in the same order, stopping at the first pair where
    // BOTH the NULL-render-target check (CheckDepthStencilMatch) AND the
    // depth-stencil-surface-usable check (CheckDeviceFormat) succeed --
    // exactly x86's own real logic, just read-only here.
    if (SUCCEEDED(hr)) {
        constexpr int kCheckDeviceFormatVtableIndex = 10;
        constexpr int kCheckDepthStencilMatchVtableIndex = 12;
        constexpr UINT kD3DFMT_X8R8G8B8 = 22;
        constexpr DWORD kD3DUSAGE_DEPTHSTENCIL = 2;
        constexpr UINT kD3DRTYPE_SURFACE = 3;
        constexpr UINT kD3DFMT_NULL = 0x4C4C554E; // MAKEFOURCC('N','U','L','L')
        struct FormatPair { UINT depthStencilFmt; UINT renderTargetFmt; };
        constexpr FormatPair kCandidates[4] = {
            {75, kD3DFMT_NULL}, // D3DFMT_D24S8 + the real "NULL" render-target trick
            {75, 23},           // D3DFMT_D24S8 + D3DFMT_D24X8
            {75, 22},           // D3DFMT_D24S8 + D3DFMT_X8R8G8B8
            {75, 21},           // D3DFMT_D24S8 + D3DFMT_R5G6B5
        };
        void** d3d9VtableForCap = *reinterpret_cast<void***>(This);
        using CheckDepthStencilMatchFn = HRESULT(WINAPI*)(void*, UINT, DWORD, UINT, UINT, UINT);
        using CheckDeviceFormatFn = HRESULT(WINAPI*)(void*, UINT, DWORD, UINT, DWORD, UINT, UINT);
        auto checkDepthStencilMatch = reinterpret_cast<CheckDepthStencilMatchFn>(
            d3d9VtableForCap[kCheckDepthStencilMatchVtableIndex]);
        auto checkDeviceFormat = reinterpret_cast<CheckDeviceFormatFn>(
            d3d9VtableForCap[kCheckDeviceFormatVtableIndex]);
        bool capable = false;
        int matchedIndex = -1;
        for (int i = 0; i < 4; ++i) {
            HRESULT r1 = checkDepthStencilMatch(This, Adapter, kD3DDEVTYPE_HAL, kD3DFMT_X8R8G8B8,
                kCandidates[i].renderTargetFmt, kCandidates[i].depthStencilFmt);
            if (SUCCEEDED(r1)) {
                HRESULT r2 = checkDeviceFormat(This, Adapter, kD3DDEVTYPE_HAL, kD3DFMT_X8R8G8B8,
                    kD3DUSAGE_DEPTHSTENCIL, kD3DRTYPE_SURFACE, kCandidates[i].depthStencilFmt);
                if (SUCCEEDED(r2)) {
                    capable = true;
                    matchedIndex = i;
                    break;
                }
            }
        }
        g_nullRtShadowCapableX64 = capable;
        g_nullRtShadowCapabilityKnownX64 = true;
        char capBuf[320]; // worst case measured at 230 chars -- generous margin per this
            // project's own hard-learned per-commit sprintf_s buffer-safety discipline
        sprintf_s(capBuf, "[null-rt-shadow-cap-diag] x86-style capability check: capable=%s matchedPair=%d "
            "(x86 skips its per-light activator call entirely when capable -- x64's FUN_140196ad0 has no "
            "equivalent gate at all, see known_issues_x64.md issue #4)",
            capable ? "YES" : "no", matchedIndex);
        LogFromController(capBuf);
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

// Real result of the [null-rt-shadow-cap-diag] probe (Hook_CreateDevice,
// above) -- exposed so analog_input_hooks_x64.cpp's per-light shadow-
// activation skip (SkipRedundantShadowActivationX64, see its own comment
// in mod_config.h) can gate itself on real hardware capability rather than
// assuming it. Returns false (never skip) until the probe has actually run
// at least once -- a device must exist before this can be known, and
// defaulting to "not capable" is the conservative, safe direction (worst
// case: the toggle simply does nothing yet, never an incorrect skip).
extern "C" bool IsNullRenderTargetShadowCapableX64()
{
    return g_nullRtShadowCapabilityKnownX64 && g_nullRtShadowCapableX64;
}

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
    InstallMouseCaptureSuppressionHooks();
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
