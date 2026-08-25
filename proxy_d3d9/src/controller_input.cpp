#include "controller_input.h"

#include <windows.h>
#include <xinput.h>   // struct definitions only -- resolved dynamically below, never linked
#include <cmath>
#include <cstdio>
#include <cstdlib> // std::abs(int) -- XInputStateHasActivity's stick-magnitude check
#include "overlay_hud.h" // ShowOverlayMessage -- connect/disconnect notifications, see
                          // PumpPendingControllerNotification's own header comment below
#include "dualsense_input.h" // 2026-08-11 -- raw-HID DualSense fallback, see its own
                              // header comment for why XInput/Steam Input alone
                              // aren't enough (issue #74/#76)
#include "frame_benchmark.h" // 2026-08-17 -- stutter investigation, times
                              // Controller_SetVibration's own XInput/HID call

extern void LogFromController(const char* msg); // defined in dllmain.cpp

// BUG-001 follow-up (2026-08-02): centralized here rather than at each of this
// project's ~17 call sites for these two getters -- see IsControllerActiveInputMethod's
// own comment further below for the full rationale. Declared up here (not just before
// first use, further down) because the background poll thread proc (inside the
// anonymous namespace immediately below) also needs to call MarkControllerActivity.
extern void MarkControllerActivity(); // defined in analog_input_hooks.cpp
extern "C" DWORD GetLastControllerActivityTickMs(); // defined in analog_input_hooks.cpp
extern "C" DWORD GetLastMouseMoveTickMs(); // defined in d3d9_hook.cpp

// Same reasoning as MarkControllerActivity above -- defined further down in this file,
// with real external linkage, but the poll thread proc (inside the anonymous namespace
// immediately below) needs to call it every wake cycle, so it must be visible before
// that point. Declaring it here (before the anonymous namespace opens) instead of
// inside it is deliberate: a forward declaration textually inside an anonymous
// namespace gets that namespace's own implicit internal linkage, which would conflict
// with this function's real (external) definition later in the file.
void ApplyPendingVibrationOnPollThread();

namespace {

typedef DWORD(WINAPI* XInputGetState_t)(DWORD, XINPUT_STATE*);
typedef DWORD(WINAPI* XInputSetState_t)(DWORD, XINPUT_VIBRATION*);

XInputGetState_t g_XInputGetState = nullptr;
XInputSetState_t g_XInputSetState = nullptr;
bool g_triedLoad = false;

// XInput's own documented deadzone constants (thumbstick, not trigger).
constexpr float kLeftDeadzone = static_cast<float>(XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE) / 32767.0f;
constexpr float kRightDeadzone = static_cast<float>(XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE) / 32767.0f;

// Response curve exponent -- >1 gives finer control near center (console-shooter feel),
// 1.0 would be perfectly linear. Not user-tunable yet (task #6 options screen will
// expose this); a reasonable default for now.
constexpr float kCurveExponent = 1.6f;

void EnsureLoaded()
{
    if (g_triedLoad) return;
    g_triedLoad = true;
    // Issue #24 follow-up (2026-08-03): xinput9_1_0.dll -- the "legacy" DLL this
    // project originally loaded for its widest-compatibility guarantee (ships on
    // every Windows Vista+ install with zero extra dependencies) -- is a documented,
    // deliberately cut-down compatibility shim: on real Windows installs its own
    // XInputSetState either isn't exported at all or is a silent no-op, since it
    // predates/bypasses the "full" XInput redistributable vibration support
    // entirely. This is a well-known XInput gotcha, not specific to this project --
    // live-confirmed here as the actual root cause of "vibration hook fires clean,
    // zero crash, but no physical rumble ever happens" once the rumble feature
    // itself (issue #24) was finally reimplemented and needed a REAL SetState.
    // GetState's ABI/behavior is identical and fine across every XInput DLL version,
    // so this only matters for vibration -- fixed by trying the full-featured DLLs
    // FIRST (xinput1_4, Windows 8+; xinput1_3, the older DirectX-redist version)
    // and falling back to xinput9_1_0 last, so a system with either of the real
    // DLLs available gets working vibration, and a system with neither still gets
    // the exact same GetState-only behavior this project already had and relied on.
    const char* kCandidateDlls[] = { "xinput1_4.dll", "xinput1_3.dll", "xinput9_1_0.dll" };
    HMODULE h = nullptr;
    const char* loadedDllName = nullptr;
    for (const char* dllName : kCandidateDlls) {
        h = LoadLibraryA(dllName);
        if (h) { loadedDllName = dllName; break; }
    }
    if (!h) {
        LogFromController("[xinput] LoadLibrary FAILED for xinput1_4/xinput1_3/xinput9_1_0 -- no controller input or vibration this session");
        return;
    }
    g_XInputGetState = reinterpret_cast<XInputGetState_t>(GetProcAddress(h, "XInputGetState"));
    g_XInputSetState = reinterpret_cast<XInputSetState_t>(GetProcAddress(h, "XInputSetState"));
    char buf[160];
    sprintf_s(buf, "[xinput] loaded %s -- GetState=%s SetState=%s",
        loadedDllName, g_XInputGetState ? "OK" : "MISSING", g_XInputSetState ? "OK" : "MISSING");
    LogFromController(buf);
}

// Scaled radial deadzone: rescales the post-deadzone range back to [0,1] smoothly,
// instead of just clamping (which would leave a "dead click" feel right at the
// deadzone edge). Then applies the response curve, preserving sign per axis.
void ShapeStick(SHORT rawX, SHORT rawY, float deadzone, float& outX, float& outY)
{
    float x = rawX / 32768.0f;
    float y = rawY / 32768.0f;
    float mag = std::sqrt(x * x + y * y);

    if (mag < deadzone) {
        outX = 0.0f;
        outY = 0.0f;
        return;
    }

    float normalizedMag = (mag - deadzone) / (1.0f - deadzone);
    if (normalizedMag > 1.0f) normalizedMag = 1.0f;
    float curved = std::pow(normalizedMag, kCurveExponent);

    // Reapply the curved magnitude along the original direction.
    outX = (x / mag) * curved;
    outY = (y / mag) * curved;
}

LARGE_INTEGER g_qpcFrequency{};
bool g_qpcInit = false;

// ---- Background XInput polling thread (2026-08-08) --------------------------------
//
// Live-reported CRITICAL regression, same day as the multi-slot scan below was added:
// "big mouse regression, when moving the mouse it drops to 4fps big lag." Root cause,
// confirmed by the user's own profiling (one thread pegged near 100% while total CPU
// sat at 14% -- a classic single-thread-bound symptom, not a GPU/overall-CPU one):
// this project's WndProc hook calls InjectMenuInputTick() on EVERY window message, not
// once per frame -- WM_MOUSEMOVE alone can fire dozens of times per rendered frame
// while dragging the mouse, and InjectMenuInputTick() polls the real gamepad state
// (multiple separate functions) every single time it runs. XInputGetState for a
// DISCONNECTED slot is a well-documented real-world latency gotcha (Windows can't
// aggressively cache "not connected" without breaking hot-plug detection, so it can
// walk into the HID/USB stack on every call) -- this project's original code already
// paid that cost once per poll; scanning all 4 slots on every one of those polls
// (added the same day for the "controller assigned to a non-zero XInput slot, e.g.
// x360ce occupying slot 0" fix below) multiplied it by up to 4x, and the mouse-move
// message flood is exactly what maximizes how many times that multiplied cost fires
// per second.
//
// Fixed by moving ALL real XInputGetState/XInputSetState calls onto a single
// dedicated background thread that polls on its own fixed schedule, completely
// decoupled from the game's message pump or frame rate -- no matter how many
// WM_MOUSEMOVE messages fire, or how slow any individual XInput call is, it can
// never again block the main thread's message processing or frame delivery. The
// public Controller_* functions below now just read the latest snapshot this
// thread already computed, guarded by a small critical section (the snapshot is a
// handful of small POD fields updated ~120 times/second -- a full lock per read/
// write is simple, correct, and negligible overhead compared to the XInput calls
// themselves).
struct CachedControllerState {
    float leftX = 0.0f, leftY = 0.0f;
    float rightX = 0.0f, rightY = 0.0f;
    unsigned short buttons = 0;
    unsigned char leftTrigger = 0, rightTrigger = 0;
    bool connected = false;
    int activeSlot = 0;

    // 2026-08-11 (issue #76): set when this frame's state came from the raw-HID
    // DualSense backend instead of XInput -- Controller_SetVibration guards on this
    // (DualSense rumble needs its own HID output report, not implemented this pass,
    // see dualsense_input.cpp's bottom comment) and gyro is only ever populated
    // alongside this being true (XInput has no gyro at all).
    bool sourceIsDualSense = false;
    float gyroX = 0.0f, gyroY = 0.0f, gyroZ = 0.0f; // raw sensor units, see
                                                      // dualsense_input.h's header
                                                      // comment on why these aren't
                                                      // claimed to be calibrated deg/s
};

CRITICAL_SECTION g_stateLock;
bool g_stateLockInit = false;
CachedControllerState g_cachedState;
HANDLE g_pollThreadHandle = nullptr;

// Event-driven polling (2026-08-25, live-reported: "gotta be that polling bs...
// we should cut the polling and just use the first controller that input is
// detected from and stop polling"). The free-running fixed-interval poll loop
// this replaced (kPollIntervalMs, 250Hz) called XInputGetState/DualSense_Poll on
// its own independent clock regardless of whether anything actually needed fresh
// input that tick -- real, avoidable overhead, and (per live report) apparently
// costs noticeably more per call for XInputGetState than for DualSense's raw-HID
// blocking read, explaining the worse XInput-side lag specifically. Direct
// follow-up correction: source LOCKING (never re-scanning other slots/devices
// once one is found, see LockedInputSource below) already existed since
// 2026-08-18 and was not the cause here -- this is a SEPARATE fix, for the
// steady-state poll CADENCE itself, not for multi-device scanning (already
// solved). Auto-reset: only the single most recent wake matters, never a queue
// of stale ones.
HANDLE g_pollWakeEvent = nullptr;

// Pending vibration write (2026-08-25, live-reported: lag spikes persisted after the
// GetState-side event-driven fix, "check on vibratw" -- checked, and found a real
// second candidate). Controller_SetVibration used to call XInputSetState/
// DualSense_SetVibration DIRECTLY on the CALLING thread -- which, for every real call
// site in this codebase, is the game's own gameplay-tick thread (Rumble_Tick, called
// from InjectAllControllerInput). This directly contradicts this file's own original
// 2026-08-08 design goal ("moving ALL real XInputGetState/XInputSetState calls onto a
// single dedicated background thread... no matter how slow any individual XInput call
// is, it can never again block the main thread") -- SetState was carved out of that
// move on the unverified assumption that it's "cheap/idempotent," never actually
// confirmed via the FrameBenchmark_AddRumbleMs instrumentation already sitting right
// next to the call (added 2026-08-17 for exactly this suspicion, never conclusively
// resolved either way). Fixed the same way GetState already was: Controller_SetVibration
// now just stashes the requested motor values here and wakes the poll thread -- the
// actual synchronous XInputSetState/DualSense_SetVibration write happens on the
// background thread instead, never on the game's own thread again.
struct PendingVibration {
    float leftMotor = 0.0f, rightMotor = 0.0f;
    bool dirty = false;
};
PendingVibration g_pendingVibration;

// Cross-thread handoff for the connect/disconnect toast (2026-08-08, user-requested):
// deliberately NOT calling ShowOverlayMessage directly from the poll thread --
// overlay_hud.cpp's own toast state (g_overlayText/g_overlayActive/etc.) is plain,
// unsynchronized globals written and read every frame from the MAIN thread inside
// Hook_EndScene, so touching them from a second thread would be a real (if narrow)
// data race. InterlockedExchange on a single sentinel int is enough to hand the
// "a transition happened" fact safely across threads; PumpPendingControllerNotification
// (called from the main thread, see IsControllerActiveInputMethod below) is the only
// thing that ever actually calls ShowOverlayMessage.
volatile LONG g_pendingConnectionToast = -1; // -1 = none pending, 0 = show "disconnected", 1 = show "connected"

bool g_connectionStateKnown = false;
bool g_controllerCurrentlyConnected = false;

void NotifyControllerConnectionChange(bool nowConnected)
{
    if (g_connectionStateKnown && nowConnected == g_controllerCurrentlyConnected) return;
    bool wasKnown = g_connectionStateKnown;
    g_connectionStateKnown = true;
    g_controllerCurrentlyConnected = nowConnected;
    if (!wasKnown && !nowConnected) return; // first-ever check, nothing was ever connected -- no toast
    InterlockedExchange(&g_pendingConnectionToast, nowConnected ? 1 : 0);
    LogFromController(nowConnected ? "[xinput] controller connected" : "[xinput] controller disconnected");
}

// A little above XInput's own documented thumbstick deadzone constants (already
// used for real input shaping above) -- deliberately coarser, since this is only
// asking "is a HUMAN actually touching this pad right now," not shaping a real
// movement value, so idle analog drift/noise shouldn't ever count as activity.
constexpr SHORT kSlotActivityStickThreshold = 6000;
constexpr BYTE kSlotActivityTriggerThreshold = 10;

bool XInputStateHasActivity(const XINPUT_STATE& state)
{
    const XINPUT_GAMEPAD& g = state.Gamepad;
    if (g.wButtons != 0) return true;
    if (g.bLeftTrigger > kSlotActivityTriggerThreshold || g.bRightTrigger > kSlotActivityTriggerThreshold) return true;
    if (std::abs(static_cast<int>(g.sThumbLX)) > kSlotActivityStickThreshold || std::abs(static_cast<int>(g.sThumbLY)) > kSlotActivityStickThreshold) return true;
    if (std::abs(static_cast<int>(g.sThumbRX)) > kSlotActivityStickThreshold || std::abs(static_cast<int>(g.sThumbRY)) > kSlotActivityStickThreshold) return true;
    return false;
}

void LogActiveSlotChange(int fromSlot, int toSlot)
{
    char buf[96];
    sprintf_s(buf, "[xinput] active controller slot changed %d -> %d", fromSlot, toSlot);
    LogFromController(buf);
}

// Runs entirely on the background poll thread -- scans all 4 real XInput user
// indices for a connected controller instead of assuming slot 0. Live-reported
// 2026-08-08 (Nexus, v0.3.1): several players see no controller-glyph icons at all
// -- even on English, with default settings -- and it's NOT reproducible on the
// developer's own machine, pointing at a real per-environment cause rather than a
// universal regression. Every real XInput read in this project was hardcoded to
// user index 0 -- a controller that Windows/Steam assigns to a different slot (a
// second pad plugged in, a tool like x360ce occupying slot 0 with its own virtual
// device while the real physical pad lands elsewhere, Steam Input's own
// passthrough renumbering, etc.) would make every one of those calls report
// ERROR_DEVICE_NOT_CONNECTED forever, identical to "no controller at all."
//
// Also handles MULTIPLE legitimate controllers connected at once correctly, not
// just "find any one pad": if the current slot is connected but sitting idle
// while a DIFFERENT connected slot is actively showing real button/stick/trigger
// input, that's a strong signal a human is holding THAT one, so this follows the
// activity rather than latching onto whichever slot merely happened to be found
// first. Only the (relatively expensive, up to 4 XInputGetState calls) full
// rescan is throttled -- see kSlotRescanIntervalMs below -- since it no longer
// needs to be frame/message-rate responsive now that it's off the main thread
// entirely; this throttle exists purely to be a considerate, low-frequency
// caller of the XInput driver, not to protect frame rate (that's now structurally
// impossible for this code to affect).
constexpr DWORD kSlotRescanIntervalMs = 500;
int g_activeXInputSlot = 0;
DWORD g_lastSlotRescanTickMs = 0;

int ResolveActiveXInputSlotOnPollThread()
{
    if (!g_XInputGetState) return g_activeXInputSlot;

    DWORD now = GetTickCount();
    bool dueForRescan = (now - g_lastSlotRescanTickMs) >= kSlotRescanIntervalMs;
    if (!dueForRescan) {
        XINPUT_STATE state{};
        bool connected = g_XInputGetState(static_cast<DWORD>(g_activeXInputSlot), &state) == ERROR_SUCCESS;
        NotifyControllerConnectionChange(connected);
        return g_activeXInputSlot;
    }
    g_lastSlotRescanTickMs = now;

    XINPUT_STATE currentState{};
    bool currentConnected = g_XInputGetState(static_cast<DWORD>(g_activeXInputSlot), &currentState) == ERROR_SUCCESS;
    if (currentConnected && XInputStateHasActivity(currentState)) {
        NotifyControllerConnectionChange(true);
        return g_activeXInputSlot; // actively in use -- no reason to look anywhere else
    }

    int firstConnectedOther = -1;
    for (int slot = 0; slot < 4; ++slot) {
        if (slot == g_activeXInputSlot) continue;
        XINPUT_STATE state{};
        if (g_XInputGetState(static_cast<DWORD>(slot), &state) != ERROR_SUCCESS) continue;
        if (firstConnectedOther < 0) firstConnectedOther = slot;
        if (XInputStateHasActivity(state)) {
            LogActiveSlotChange(g_activeXInputSlot, slot);
            g_activeXInputSlot = slot;
            NotifyControllerConnectionChange(true);
            return slot;
        }
    }

    if (currentConnected) {
        NotifyControllerConnectionChange(true);
        return g_activeXInputSlot;
    }
    if (firstConnectedOther >= 0) {
        LogActiveSlotChange(g_activeXInputSlot, firstConnectedOther);
        g_activeXInputSlot = firstConnectedOther;
        NotifyControllerConnectionChange(true);
        return g_activeXInputSlot;
    }

    NotifyControllerConnectionChange(false);
    return g_activeXInputSlot;
}

// Safety-net-only timeout for the event wait below (2026-08-25) -- NOT a polling
// interval. Real polls are driven entirely by Controller_RequestPoll() wake-ups from
// the two actual per-tick consumers (the gameplay-tick hook, the WndProc ~60Hz
// WM_TIMER). This just guarantees the thread still checks in periodically even in the
// narrow window before either of those has fired even once (e.g. the first few
// milliseconds after DLL load), so device detection/reconnect handling can't stall
// waiting on a wake-up that hasn't happened yet.
constexpr DWORD kPollWakeTimeoutMs = 50;

// ---- Locked input source (2026-08-18) ----------------------------------------
//
// Live-reported same-day stutter investigation: "again it could be the 4
// controller polling plus now dualsense aswell, i think we should only poll
// when needed... make it so restart is needed for controller device change,
// same slot is fine but if you change controller slot restart needed." Real,
// legitimate overhead regardless of whether it turns out to be THE stutter
// cause: every tick used to (a) check DualSense_IsOpen() for sticky priority,
// (b) potentially resolve/rescan across up to 4 real XInput slots (throttled to
// once/500ms, but still a real repeating cost), AND (c) potentially attempt
// DualSense_EnsureOpen()'s own SetupDiGetClassDevsW/CreateFileW device
// enumeration every single tick whenever XInput wasn't currently handling
// input -- all of that on top of whichever ONE source is actually in the
// player's hands. Fixed by determining the active source ONCE (first ticks
// after boot, same detection logic as before) and then LOCKING onto it for the
// rest of the process lifetime -- steady-state cost drops to exactly one real
// read call per tick (XInputGetState OR DualSense_Poll, never both, never a
// scan). Matches the user's own explicit tradeoff: the SAME slot/device
// reconnecting (unplug/replug) keeps working with no restart needed (the
// locked source keeps being retried), but switching to a DIFFERENT XInput slot
// or switching between XInput and DualSense entirely requires a relaunch to be
// picked up -- this project's existing multi-slot/activity-based re-election
// system (ResolveActiveXInputSlotOnPollThread) is still used for the ONE-TIME
// initial detection below, just never called again once a source locks in.
enum class LockedInputSource { Undetermined, XInput, DualSense };
LockedInputSource g_lockedSource = LockedInputSource::Undetermined;
int g_lockedXInputSlot = 0;

void ApplyXInputStateToCache(int slot, const XINPUT_STATE& state)
{
    float lx = 0.0f, ly = 0.0f, rx = 0.0f, ry = 0.0f;
    ShapeStick(state.Gamepad.sThumbLX, state.Gamepad.sThumbLY, kLeftDeadzone, lx, ly);
    ShapeStick(state.Gamepad.sThumbRX, state.Gamepad.sThumbRY, kRightDeadzone, rx, ry);

    EnterCriticalSection(&g_stateLock);
    g_cachedState.leftX = lx;
    g_cachedState.leftY = ly;
    g_cachedState.rightX = rx;
    g_cachedState.rightY = ry;
    g_cachedState.buttons = state.Gamepad.wButtons;
    g_cachedState.leftTrigger = state.Gamepad.bLeftTrigger;
    g_cachedState.rightTrigger = state.Gamepad.bRightTrigger;
    g_cachedState.connected = true;
    g_cachedState.activeSlot = slot;
    g_cachedState.sourceIsDualSense = false;
    g_cachedState.gyroX = g_cachedState.gyroY = g_cachedState.gyroZ = 0.0f;
    LeaveCriticalSection(&g_stateLock);

    // Marked here, at the moment real input is actually observed, rather than
    // making every Controller_Get* consumer below re-derive the same "was
    // there real input" check on the cached snapshot -- one place, same "every
    // reader of real input must mark activity" principle the pre-existing
    // centralized getters comment (further below) already established for a
    // different pair of functions.
    if (lx != 0.0f || ly != 0.0f || rx != 0.0f || ry != 0.0f ||
        state.Gamepad.wButtons != 0 ||
        state.Gamepad.bLeftTrigger != 0 || state.Gamepad.bRightTrigger != 0) {
        MarkControllerActivity();
    }
}

bool ApplyDualSenseStateToCache(const DualSenseRawState& dsState)
{
    float lx = 0.0f, ly = 0.0f, rx = 0.0f, ry = 0.0f;
    // DualSense sticks are already-centered signed 8-bit range (see
    // DualSenseRawState's own comment); scaled up to a SHORT range so the SAME
    // ShapeStick() deadzone/curve math XInput uses applies unchanged.
    ShapeStick(static_cast<SHORT>(dsState.leftStickX * 256), static_cast<SHORT>(dsState.leftStickY * 256), kLeftDeadzone, lx, ly);
    ShapeStick(static_cast<SHORT>(dsState.rightStickX * 256), static_cast<SHORT>(dsState.rightStickY * 256), kRightDeadzone, rx, ry);
    unsigned short buttons = DualSense_ToXInputButtons(dsState);

    EnterCriticalSection(&g_stateLock);
    g_cachedState.leftX = lx;
    g_cachedState.leftY = ly;
    g_cachedState.rightX = rx;
    g_cachedState.rightY = ry;
    g_cachedState.buttons = buttons;
    g_cachedState.leftTrigger = dsState.leftTrigger;
    g_cachedState.rightTrigger = dsState.rightTrigger;
    g_cachedState.connected = true;
    g_cachedState.activeSlot = -1; // not an XInput slot -- see this struct's
                                     // sourceIsDualSense comment
    g_cachedState.sourceIsDualSense = true;
    g_cachedState.gyroX = static_cast<float>(dsState.gyroX);
    g_cachedState.gyroY = static_cast<float>(dsState.gyroY);
    g_cachedState.gyroZ = static_cast<float>(dsState.gyroZ);
    LeaveCriticalSection(&g_stateLock);

    bool hadActivity = lx != 0.0f || ly != 0.0f || rx != 0.0f || ry != 0.0f || buttons != 0 ||
                        dsState.leftTrigger != 0 || dsState.rightTrigger != 0;
    if (hadActivity) MarkControllerActivity();
    return hadActivity;
}

DWORD WINAPI XInputPollThreadProc(LPVOID)
{
    for (;;) {
        EnsureLoaded();

        if (g_lockedSource == LockedInputSource::Undetermined) {
            // ---- One-time detection phase -- same priority/logic as this
            // project always used (DualSense sticky-if-open, else XInput's own
            // multi-slot/activity-based resolution, else nothing yet) -- just
            // now only runs until SOMETHING is found once, then locks and is
            // never reached again for the rest of the process. Loader-lock
            // safety (GetLastKnownRenderDevice() gate before ANY DualSense
            // enumeration attempt) and the DualSense-vs-XInput poll-priority
            // fight fix (sticky DualSense) both still apply here exactly as
            // before -- this phase's own logic is otherwise unchanged, only
            // its LIFETIME shrank from "every tick forever" to "until locked".
            bool preferDualSense = DualSense_IsOpen();
            bool handledByXInput = false;

            if (!preferDualSense && g_XInputGetState) {
                int slot = ResolveActiveXInputSlotOnPollThread();
                XINPUT_STATE state{};
                if (g_XInputGetState(static_cast<DWORD>(slot), &state) == ERROR_SUCCESS) {
                    handledByXInput = true;
                    ApplyXInputStateToCache(slot, state);
                    g_lockedSource = LockedInputSource::XInput;
                    g_lockedXInputSlot = slot;
                    char buf[96];
                    sprintf_s(buf, "[xinput] locked onto XInput slot %d for this session -- a slot change now needs a restart", slot);
                    LogFromController(buf);
                    // Glyph-style auto-detect (2026-08-25) -- resolved ONCE here, at the
                    // same point the input SOURCE itself locks for the session, not on a
                    // recurring timer. This project's own input-locking design already
                    // requires a restart to switch device families (see this function's
                    // own comments above/below) -- polling for a live controller swap
                    // would be inconsistent with that, and could report PlayStation
                    // glyphs while still actually reading XInput (e.g. a Steam Input
                    // virtual pad), the exact translator-layer confusion the native
                    // DualSense backend exists to avoid. See mod_config.h's own
                    // GlyphStyleAuto comment.
                    if (g_modConfig.glyphStyleAuto) {
                        g_modConfig.glyphStyle = Controller_DetectGlyphStyle(g_modConfig.glyphStyle);
                    }
                }
            }

            if (!handledByXInput && GetLastKnownRenderDevice() != nullptr) {
                DualSenseRawState dsState;
                if (DualSense_EnsureOpen() && DualSense_Poll(dsState)) {
                    ApplyDualSenseStateToCache(dsState);
                    NotifyControllerConnectionChange(true);
                    g_lockedSource = LockedInputSource::DualSense;
                    LogFromController("[dualsense] locked onto DualSense for this session -- switching back to XInput now needs a restart");
                    // Same one-time auto-detect as the XInput branch above -- a DualSense
                    // just locked in means Controller_DetectGlyphStyle will resolve to
                    // PlayStation immediately (DualSense_IsOpen() is now true), same
                    // result either way, but going through the shared function keeps this
                    // single code path as the only place glyphStyle gets auto-written.
                    if (g_modConfig.glyphStyleAuto) {
                        g_modConfig.glyphStyle = Controller_DetectGlyphStyle(g_modConfig.glyphStyle);
                    }
                } else {
                    EnterCriticalSection(&g_stateLock);
                    g_cachedState.connected = false;
                    g_cachedState.sourceIsDualSense = false;
                    g_cachedState.gyroX = g_cachedState.gyroY = g_cachedState.gyroZ = 0.0f;
                    LeaveCriticalSection(&g_stateLock);
                    NotifyControllerConnectionChange(false);
                }
            }
        } else if (g_lockedSource == LockedInputSource::XInput) {
            // Steady state: exactly one real call per tick, the locked slot
            // only -- no other slot is ever read again this session.
            XINPUT_STATE state{};
            bool connected = g_XInputGetState &&
                g_XInputGetState(static_cast<DWORD>(g_lockedXInputSlot), &state) == ERROR_SUCCESS;
            if (connected) {
                ApplyXInputStateToCache(g_lockedXInputSlot, state);
            } else {
                // Same slot unplugged -- report disconnected but keep polling
                // THIS slot every tick (no fallback, no rescan) so a replug
                // into the SAME slot resumes working with no restart, matching
                // "same slot is fine" from the user's own request.
                EnterCriticalSection(&g_stateLock);
                g_cachedState.connected = false;
                LeaveCriticalSection(&g_stateLock);
            }
            NotifyControllerConnectionChange(connected);
        } else { // LockedInputSource::DualSense
            // Steady state: keep retrying DualSense_EnsureOpen()/Poll() every
            // tick (needed so an unplug/replug of the SAME device recovers
            // with no restart), but NEVER fall back to trying XInput -- a
            // switch to a different device family needs a restart, matching
            // "if you change controller slot restart needed."
            DualSenseRawState dsState;
            bool connected = GetLastKnownRenderDevice() != nullptr &&
                              DualSense_EnsureOpen() && DualSense_Poll(dsState);
            if (connected) {
                ApplyDualSenseStateToCache(dsState);
            } else {
                EnterCriticalSection(&g_stateLock);
                g_cachedState.connected = false;
                g_cachedState.sourceIsDualSense = false;
                g_cachedState.gyroX = g_cachedState.gyroY = g_cachedState.gyroZ = 0.0f;
                LeaveCriticalSection(&g_stateLock);
            }
            NotifyControllerConnectionChange(connected);
        }

        // 2026-08-25: applies any pending vibration write queued by Controller_SetVibration
        // since this thread last woke -- see g_pendingVibration's own comment. Checked every
        // wake regardless of source-lock state, so a rumble request queued before detection
        // even finishes still gets applied the moment a source locks in.
        ApplyPendingVibrationOnPollThread();

        // Event-driven wait (2026-08-25) -- replaces the old fixed Sleep(kPollIntervalMs).
        // Real per-tick consumers (InjectAllControllerInput, the gameplay-tick hook, and
        // the WndProc SetTimer's ~60Hz WM_TIMER, which keeps running during pause) call
        // Controller_RequestPoll() right before they read cached state, waking this thread
        // to take exactly one fresh sample -- no independent clock, no reads when nothing
        // is actually asking for one. A short timeout (not INFINITE) is kept as a safety
        // net so an unplug/replug or a still-Undetermined detection phase keeps making
        // forward progress even in the unlikely case neither real consumer happens to be
        // firing yet (e.g. very early during DLL load, before any hook has run once).
        WaitForSingleObject(g_pollWakeEvent, kPollWakeTimeoutMs);
    }
    return 0; // unreachable -- this thread lives for the whole process, matching this
              // project's existing "install once, never uninstall" hook lifetime pattern
}

void EnsurePollThreadStarted()
{
    static bool started = false;
    if (started) return;
    started = true;
    InitializeCriticalSection(&g_stateLock);
    g_stateLockInit = true;
    // Auto-reset (bManualReset=false): each SetEvent wakes the thread for exactly one
    // poll, then the event clears itself -- a caller that requests a poll while the
    // thread is already awake and mid-poll doesn't queue up a second one, it just
    // means the poll already in flight will pick up equally fresh data.
    g_pollWakeEvent = CreateEventA(nullptr, FALSE, FALSE, nullptr);
    g_pollThreadHandle = CreateThread(nullptr, 0, XInputPollThreadProc, nullptr, 0, nullptr);
    if (!g_pollThreadHandle) {
        LogFromController("[xinput] CreateThread FAILED for the background poll thread -- no controller input this session");
    }
}

} // namespace

// Wakes the background poll thread to take exactly one fresh sample right now
// (2026-08-25, see g_pollWakeEvent's own comment for the full rationale). Called from
// the two real per-tick consumers -- InjectAllControllerInput (analog_input_hooks.cpp,
// the gameplay-tick hook) and InjectMenuInputTick (the WndProc/SetTimer path) -- right
// before they read cached state via the Controller_Get* functions below, so a poll
// only ever happens when something is actually about to consume its result. Safe to
// call before the poll thread/event exist yet (lazily starts them, same pattern every
// Controller_Get* function already uses) and safe to call redundantly from both
// consumers in the same real tick (SetEvent on an already-signaled auto-reset event is
// a no-op, not a queued second wake).
//
// Rate-limited (2026-08-25, self-caught regression risk, live-reported: "what if...
// threading is whats causing the issue"): InjectMenuInputTick fires on EVERY WndProc
// message, not just its own ~60Hz WM_TIMER -- this project's own 2026-08-08 history
// documents WM_MOUSEMOVE alone firing "dozens of times per rendered frame" while
// dragging the mouse, the EXACT flood pattern that caused the original 4fps regression
// the whole background-poll-thread architecture exists to prevent. SetEvent is far
// cheaper than the XInputGetState call that caused THAT regression, but it is still a
// real kernel transition, and calling it unconditionally from that same flood-prone
// path would reintroduce a smaller version of the identical bug class. Throttled to
// once per kMinPollRequestIntervalMs regardless of how many times a caller invokes
// this in one frame -- keeps the "poll on real demand" architecture while capping the
// worst case, same margin as InjectMenuInputTick's own ~16ms WM_TIMER cadence.
namespace { DWORD g_lastPollRequestTickMs = 0; constexpr DWORD kMinPollRequestIntervalMs = 15; }
void Controller_RequestPoll()
{
    DWORD now = GetTickCount();
    if (now - g_lastPollRequestTickMs < kMinPollRequestIntervalMs) return;
    g_lastPollRequestTickMs = now;

    EnsurePollThreadStarted();
    if (g_pollWakeEvent) SetEvent(g_pollWakeEvent);
}

// BUG-001 follow-up (2026-08-02): the original rationale for centralizing
// MarkControllerActivity/GetLastControllerActivityTickMs/GetLastMouseMoveTickMs here
// rather than at each of this project's ~17 call sites -- live-reported regression
// from the first attempt: the cursor stayed visible even during controller-driven
// MENU navigation ("until gameplay") because MarkControllerActivity() had only been
// added to the gameplay-tick functions (InjectControllerMovement/Buttons), which
// halt during menus/pause -- menu-nav functions run via the always-on WndProc/timer
// tick and read these SAME getters, so marking activity here instead covers every
// caller, present and future (declared near the top of this file now, not here --
// the background poll thread above needs the same declarations before its own
// first use).

// Runs on the MAIN thread only (called from IsControllerActiveInputMethod below,
// itself called every frame/tick from the main thread already) -- the one and only
// place ShowOverlayMessage is ever called for a connect/disconnect toast, see
// g_pendingConnectionToast's own header comment for why this hop exists.
void PumpPendingControllerNotification()
{
    LONG pending = InterlockedExchange(&g_pendingConnectionToast, -1);
    if (pending == 1) ShowOverlayMessage("Controller Connected", 3000);
    else if (pending == 0) ShowOverlayMessage("Controller Disconnected", 3000);
}

// See controller_input.h's own comment on IsControllerActiveInputMethod for the
// rationale (shared by the cursor overlay and the glyph-hint overlays). Same
// recency-window-then-comparison logic already live-proven for the cursor
// (overlay_hud.cpp's DrawCustomCursorIfNeeded, BUG-001/#55): controller counts as
// the active method outright if used within the last kRecentControllerActivityMs,
// otherwise whichever of controller/(deadzone-filtered) mouse movement is more
// recent wins.
bool IsControllerActiveInputMethod()
{
    PumpPendingControllerNotification();
    constexpr DWORD kRecentControllerActivityMs = 300;
    DWORD lastController = GetLastControllerActivityTickMs();
    if (GetTickCount() - lastController < kRecentControllerActivityMs) return true;
    return lastController > GetLastMouseMoveTickMs();
}

bool Controller_GetLeftStick(float& x, float& y)
{
    EnsurePollThreadStarted();
    EnterCriticalSection(&g_stateLock);
    x = g_cachedState.leftX;
    y = g_cachedState.leftY;
    bool connected = g_cachedState.connected;
    LeaveCriticalSection(&g_stateLock);
    return connected;
}

bool Controller_GetRightStick(float& x, float& y)
{
    EnsurePollThreadStarted();
    EnterCriticalSection(&g_stateLock);
    x = g_cachedState.rightX;
    y = g_cachedState.rightY;
    bool connected = g_cachedState.connected;
    LeaveCriticalSection(&g_stateLock);
    return connected;
}

bool Controller_GetRawButtonsAndTriggers(unsigned short& buttons, unsigned char& leftTrigger, unsigned char& rightTrigger)
{
    EnsurePollThreadStarted();
    EnterCriticalSection(&g_stateLock);
    buttons = g_cachedState.buttons;
    leftTrigger = g_cachedState.leftTrigger;
    rightTrigger = g_cachedState.rightTrigger;
    bool connected = g_cachedState.connected;
    LeaveCriticalSection(&g_stateLock);
    return connected;
}

bool Controller_IsConnected()
{
    EnsurePollThreadStarted();
    EnterCriticalSection(&g_stateLock);
    bool connected = g_cachedState.connected;
    LeaveCriticalSection(&g_stateLock);
    return connected;
}

// 2026-08-11 (issue #76): raw, uncalibrated gyro sensor units -- see
// dualsense_input.h's own header comment for why this doesn't claim a deg/s
// conversion. Returns false (zeroed outputs) whenever the active controller isn't
// the raw-HID DualSense backend, e.g. any XInput pad (including Steam Input's own
// virtual ones) -- none of those expose gyro data to this project at all.
bool Controller_GetGyroRate(float& x, float& y, float& z)
{
    EnsurePollThreadStarted();
    EnterCriticalSection(&g_stateLock);
    bool hasGyro = g_cachedState.connected && g_cachedState.sourceIsDualSense;
    x = hasGyro ? g_cachedState.gyroX : 0.0f;
    y = hasGyro ? g_cachedState.gyroY : 0.0f;
    z = hasGyro ? g_cachedState.gyroZ : 0.0f;
    LeaveCriticalSection(&g_stateLock);
    return hasGyro;
}

void Controller_SetVibration(float leftMotor, float rightMotor)
{
    if (leftMotor < 0.0f) leftMotor = 0.0f;
    if (leftMotor > 1.0f) leftMotor = 1.0f;
    if (rightMotor < 0.0f) rightMotor = 0.0f;
    if (rightMotor > 1.0f) rightMotor = 1.0f;

    // 2026-08-25: no longer calls XInputSetState/DualSense_SetVibration itself --
    // see g_pendingVibration's own comment for why. Just stashes the request and
    // wakes the poll thread, which performs the actual (potentially slow) write off
    // the game's own gameplay-tick thread. Non-blocking, safe to call every frame.
    EnterCriticalSection(&g_stateLock);
    g_pendingVibration.leftMotor = leftMotor;
    g_pendingVibration.rightMotor = rightMotor;
    g_pendingVibration.dirty = true;
    LeaveCriticalSection(&g_stateLock);

    Controller_RequestPoll();
}

// Performs the actual, potentially-slow vibration write -- called ONLY from the
// background poll thread (see g_pendingVibration's own comment), never from the
// game's own gameplay-tick thread. Reads+clears the pending request under the same
// lock Controller_SetVibration writes it under, then does the real XInputSetState/
// DualSense_SetVibration call outside the lock (so a slow write never blocks a
// concurrent Controller_SetVibration call from queuing its own request).
void ApplyPendingVibrationOnPollThread()
{
    EnterCriticalSection(&g_stateLock);
    bool dirty = g_pendingVibration.dirty;
    float leftMotor = g_pendingVibration.leftMotor;
    float rightMotor = g_pendingVibration.rightMotor;
    int slot = g_cachedState.activeSlot;
    bool isDualSense = g_cachedState.sourceIsDualSense;
    g_pendingVibration.dirty = false;
    LeaveCriticalSection(&g_stateLock);
    if (!dirty) return;

    EnsureLoaded();

    // 2026-08-17 stutter investigation (frame_benchmark.h) -- times this call
    // regardless of which branch below actually fires, so a real slow-write cost
    // here shows up in frametime_benchmark.csv if it's ever the cause, instead of
    // staying a guess either way. Now measures the poll thread's own cost, not the
    // game's gameplay-tick thread's -- a real number here no longer means a real
    // stutter risk the way it would have before this function moved off that thread.
    LARGE_INTEGER benchFreq{}, benchStart{}, benchEnd{};
    QueryPerformanceFrequency(&benchFreq);
    QueryPerformanceCounter(&benchStart);

    if (isDualSense) {
        DualSense_SetVibration(static_cast<uint8_t>(leftMotor * 255.0f), static_cast<uint8_t>(rightMotor * 255.0f));
    } else if (g_XInputSetState) {
        XINPUT_VIBRATION vib{};
        vib.wLeftMotorSpeed = static_cast<WORD>(leftMotor * 65535.0f);
        vib.wRightMotorSpeed = static_cast<WORD>(rightMotor * 65535.0f);
        g_XInputSetState(static_cast<DWORD>(slot), &vib);
    }

    QueryPerformanceCounter(&benchEnd);
    FrameBenchmark_AddRumbleMs((static_cast<double>(benchEnd.QuadPart - benchStart.QuadPart) * 1000.0) / static_cast<double>(benchFreq.QuadPart));
}

float Controller_DeltaTimeSeconds()
{
    if (!g_qpcInit) {
        QueryPerformanceFrequency(&g_qpcFrequency);
        g_qpcInit = true;
    }
    static LARGE_INTEGER lastTime{};
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    if (lastTime.QuadPart == 0) {
        lastTime = now;
        return 0.0f;
    }
    float dt = static_cast<float>(now.QuadPart - lastTime.QuadPart) / static_cast<float>(g_qpcFrequency.QuadPart);
    lastTime = now;
    // Guard against absurd values (e.g. first call after a long stall/breakpoint).
    if (dt < 0.0f || dt > 0.25f) dt = 0.0f;
    return dt;
}
