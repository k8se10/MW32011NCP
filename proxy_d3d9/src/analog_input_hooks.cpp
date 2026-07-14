// analog_input_hooks.cpp — real analog movement/look/buttons/ADS injection.
//
// ARCHITECTURE (restructured 2026-07-14 -- see InjectAllControllerInput's comment for
// the full reasoning): everything hooks a single point, FUN_0057de60, the per-frame
// pipeline's always-running finalize step. Movement is additive (reads whatever
// usercmd_t.forwardmove/rightmove keyboard input already wrote, if any, and adds the
// controller's contribution on top). Look writes directly to the pitch/yaw angle-delta
// accumulator globals, deliberately NOT routed through the mouse-delta pipeline, so it
// doesn't inherit sensitivity/m_yaw/m_pitch/cl_mouseAccel/m_filter and has its own
// independent feel -- confirmed both directionally and as architecturally "true" native
// input (not mouse emulation) with the user 2026-07-14. ADS calls the real engine
// KeyDown/KeyUp kbutton handlers directly (see InjectControllerAds).
//
// All sign conventions below are confirmed against actual real-controller-hardware
// playtest, not just Ghidra's static guesses -- see re_notes/iw5sp.md.

#include <windows.h>
#include <cstdint>
#include <cstdio>
#include "../third_party/minhook/include/MinHook.h"
#include "controller_input.h"

// Forwarder defined in dllmain.cpp -- lets this translation unit log to the same
// proxy_d3d9.log file without duplicating the log-file setup.
extern void LogFromController(const char* msg);

namespace {

inline int8_t ClampToSByte(int v)
{
    if (v > 127) return 127;
    if (v < -128) return -128;
    return static_cast<int8_t>(v);
}

} // namespace

// ---- Movement: left stick -> usercmd_t.forwardmove(+0x1c) / .rightmove(+0x1d) ----
extern "C" void __cdecl InjectControllerMovement(unsigned char* cmd)
{
    if (!cmd) return;
    float lx, ly;
    if (!Controller_GetLeftStick(lx, ly)) return;
    if (lx == 0.0f && ly == 0.0f) return;

    int8_t curForward = static_cast<int8_t>(cmd[0x1c]);
    int8_t curRight = static_cast<int8_t>(cmd[0x1d]);

    // Full stick deflection == full digital-key-equivalent speed (matches how the
    // keyboard path also just produces +-127 for a held key -- the engine's own
    // movement/physics code treats forwardmove/rightmove as a continuous fraction of
    // max speed, so this still gives real analog speed control, not just on/off).
    // Confirmed correct as-is (no inversion) via real-hardware playtest, 2026-07-14 --
    // only look (right stick) was reported inverted, not movement.
    int addForward = static_cast<int>(ly * 127.0f);
    int addRight = static_cast<int>(lx * 127.0f);

    cmd[0x1c] = static_cast<unsigned char>(ClampToSByte(curForward + addForward));
    cmd[0x1d] = static_cast<unsigned char>(ClampToSByte(curRight + addRight));
}

// ---- Buttons: FINAL native mapping (task #10), confirmed against real hardware -----
//
// A keybd_event/mouse_event-based synthetic-key approach was tried and REJECTED early
// on -- this writes DIRECTLY to usercmd_t.buttons (offset +4), fully native, no OS-level
// input emulation at all.
//
// This replaces the earlier raw-bit diagnostic pass (arbitrary XInput-button-to-bit
// test assignment) now that every bit below has a real, user-confirmed identity from
// live playtesting -- see re_notes/iw5sp.md for the full investigation. Mapped to
// standard Xbox-convention CoD controls (RT=fire, LT=ADS, bumpers=grenades):
//   RT (analog trigger, not a digital XInput button) -> Fire
//   A -> Jump (moved off Start)
//   Right stick click -> Melee (confirmed "100% knife" live, 2026-07-14 -- moved here
//        off B per user preference, matching the original Steam-config reference
//        mapping melee to the right thumbstick click. An earlier struct-offset-
//        correlation guess briefly mislabeled this bit as Sprint; that theory was
//        retracted -- it's genuinely Melee)
//   X -> Use/Reload (+usereload -- context-sensitive by the game's own design, same
//        bit covers both, confirmed working)
//   LB -> Tactical (smoke) -- moved here off D-pad Left
//   RB -> Lethal (frag) -- moved here off D-pad Down
//   B -> Crouch/Prone stance button, real Xbox 360 CoD semantics (user-specified,
//        2026-07-14): a 3-state ladder (Standing / Crouched / Prone) tracked in our
//        own g_stance, not a raw hold of either bit. Both 0x200 (crouch) and 0x100
//        (+actionslot2, prone) are hold-state bits at the engine level, so we assert
//        whichever one matches the current g_stance every frame, independent of
//        whether B is currently physically held:
//          Standing + tap  -> Crouched
//          Standing + hold -> Prone
//          Crouched + tap  -> Standing
//          Crouched + hold -> Prone
//          Prone    + tap  -> Crouched
//          Prone    + hold -> Standing   (reverse of Standing+hold, per user spec)
//        "Hold" fires the instant the press crosses the threshold (no need to also
//        release); "tap" only fires on release, and only if the threshold was never
//        reached during that press.
//   LT (analog trigger) -> ADS -- NOT handled here, see InjectControllerAds (needs the
//        real KeyDown/KeyUp kbutton calls, not a simple bit-OR)
//   Left stick click (L3) -> Sprint -- NOT handled here, see InjectControllerSprint.
//        EXPERIMENTAL/unconfirmed (2026-07-14): writes a speculative locomotion flag,
//        not a verified kbutton -- needs live playtest confirmation.
//
// NOT YET IMPLEMENTED (left unmapped, not guessed at):
//   Back -> freed up when Crouch moved to B; no action assigned yet
//   Y -> should be weapnext (one-shot command, not a held kbutton -- the console-
//        command-execution function for one-shot commands like weapnext/togglemenu/
//        toggleprone hasn't been located yet, separate investigation from this bit
//        mapping)
//   Start -> should be pause/togglemenu (same one-shot-command blocker as Y)
//   D-pad (all four directions) -> left unassigned. The underlying bits
//        (+actionslot 1-4 per the kbutton table) are still uncertain/largely untested
//        individually -- not part of this pass, revisit later.
namespace {
constexpr unsigned short kXI_RIGHT_THUMB = 0x0080;
constexpr unsigned short kXI_A = 0x1000;
constexpr unsigned short kXI_B = 0x2000;
constexpr unsigned short kXI_X = 0x4000;
constexpr unsigned short kXI_LEFT_SHOULDER = 0x0100;
constexpr unsigned short kXI_RIGHT_SHOULDER = 0x0200;
constexpr unsigned char kTriggerThresholdFire = 30; // XInput's documented trigger threshold

// How long a B press must be held before it counts as "hold" instead of "tap". Not
// user-tunable yet; task #6's options screen is the right place for that, not a
// hardcoded constant here.
constexpr DWORD kProneHoldThresholdMs = 400;

enum class Stance { Standing, Crouched, Prone };
Stance g_stance = Stance::Standing;
DWORD g_crouchButtonPressStartMs = 0;
bool g_crouchButtonWasHeld = false;
bool g_holdActionConsumed = false; // true once this press has already fired its hold action

// TEMP DIAGNOSTIC (2026-07-14): tracing the "engine auto-stands the player up on
// physical B release" bug. Not a permanent feature -- remove once the root cause is
// found (see re_notes/iw5sp.md). Logs every stance-machine transition unconditionally,
// plus a short window of frame-by-frame usercmd.buttons snapshots after each release so
// we can see whether OUR write is what changes, or something downstream of this hook
// overrides it independent of what we assert here.
int g_postReleaseWatchFrames = 0;

const char* StanceName(Stance s)
{
    switch (s) {
        case Stance::Standing: return "Standing";
        case Stance::Crouched: return "Crouched";
        case Stance::Prone:    return "Prone";
    }
    return "?";
}
}

extern "C" void __cdecl InjectControllerButtons(unsigned char* cmd)
{
    if (!cmd) return;
    unsigned short xiButtons;
    unsigned char leftTrigger, rightTrigger;
    if (!Controller_GetRawButtonsAndTriggers(xiButtons, leftTrigger, rightTrigger)) return;

    uint32_t out = 0;
    if (rightTrigger >= kTriggerThresholdFire) out |= 0x1;      // Fire (+attack)
    if (xiButtons & kXI_RIGHT_THUMB) out |= 0x4;                // Melee
    if (xiButtons & kXI_X) out |= 0x8;                          // Use/Reload (+usereload)
    if (xiButtons & kXI_LEFT_SHOULDER) out |= 0x8000;           // Tactical (smoke)
    if (xiButtons & kXI_RIGHT_SHOULDER) out |= 0x4000;          // Lethal (frag)
    if (xiButtons & kXI_A) out |= 0x400;                        // Jump (+gostand)

    bool bHeld = (xiButtons & kXI_B) != 0;
    if (bHeld && !g_crouchButtonWasHeld) {
        // Rising edge: new press starting.
        g_crouchButtonPressStartMs = GetTickCount();
        g_holdActionConsumed = false;
        char buf[128];
        sprintf_s(buf, "[stance] B DOWN  stance=%s", StanceName(g_stance));
        LogFromController(buf);
    }
    if (bHeld && !g_holdActionConsumed) {
        DWORD heldMs = GetTickCount() - g_crouchButtonPressStartMs;
        if (heldMs >= kProneHoldThresholdMs) {
            // Hold action fires once, the instant the threshold is crossed: Prone
            // reverses back to Standing, anything else goes to Prone.
            Stance prev = g_stance;
            g_stance = (g_stance == Stance::Prone) ? Stance::Standing : Stance::Prone;
            g_holdActionConsumed = true;
            char buf[128];
            sprintf_s(buf, "[stance] HOLD fired  %s -> %s", StanceName(prev), StanceName(g_stance));
            LogFromController(buf);
        }
    }
    if (!bHeld && g_crouchButtonWasHeld) {
        if (!g_holdActionConsumed) {
            // Falling edge and the hold threshold was never reached -- this was a tap.
            Stance prev = g_stance;
            switch (g_stance) {
                case Stance::Standing: g_stance = Stance::Crouched; break;
                case Stance::Crouched: g_stance = Stance::Standing; break;
                case Stance::Prone:    g_stance = Stance::Crouched; break;
            }
            char buf[128];
            sprintf_s(buf, "[stance] TAP fired  %s -> %s", StanceName(prev), StanceName(g_stance));
            LogFromController(buf);
        }
        char buf[128];
        sprintf_s(buf, "[stance] B UP  stance now=%s -- watching next 90 frames", StanceName(g_stance));
        LogFromController(buf);
        g_postReleaseWatchFrames = 90; // ~1.5s at 60fps -- long enough to see any delayed override
    }
    g_crouchButtonWasHeld = bHeld;

    switch (g_stance) {
        case Stance::Crouched: out |= 0x200u; break;
        case Stance::Prone:    out |= 0x100u; break;
        default: break;
    }

    uint32_t* buttonsField = reinterpret_cast<uint32_t*>(cmd + 4);
    uint32_t beforeOr = *buttonsField;
    if (out != 0) *buttonsField |= out;

    if (g_postReleaseWatchFrames > 0) {
        char buf[160];
        sprintf_s(buf, "[stance] watch  stance=%s before=0x%08X after=0x%08X",
                  StanceName(g_stance), beforeOr, *buttonsField);
        LogFromController(buf);
        --g_postReleaseWatchFrames;
    }
}

// ---- ADS: left trigger -> true hold-to-aim via the real +toggleads_throw kbuttons ----
//
// Found 2026-07-14 via a combination of live memory diffing (24 real ADS toggles,
// narrowed ~11M candidate bytes down to a handful) and static confirmation: the two
// surviving struct offsets (per-player kbutton context base 0x00A98AD8, stride 0x230)
// are individually-registered kbuttons at +0xB4 and +0x1E0, both driven together by
// the same special-bind case in FUN_00438710 (the same dispatcher that owns +mlook's
// flag) -- consistent with +toggleads_throw's real semantics ("toggle ADS OR
// cook-throw", context-dependent on whether a grenade is primed, hence needing two
// kbutton_t's fed from one physical bind).
//
// Rather than hand-writing kbutton_t bytes directly (fragile -- the full struct layout
// isn't pinned down), this calls the REAL engine KeyDown/KeyUp handlers the game itself
// uses, with the bind-index constants read directly off the jump table in
// FUN_00438710 (case index 13 = "+toggleads_throw" down-edge, 14 = "-toggleads_throw"
// up-edge -- the classic plus/minus command-pair convention, immediately adjacent in
// the dispatch table). This keeps hold-time/msec bookkeeping correct automatically
// since it's the same code path a real keypress would take, instead of us having to
// replicate that bookkeeping by hand.
//
// Calling convention confirmed via static analysis of the dispatcher's own call sites
// (FUN_0057d1c0: EAX=kbutton_t*, ECX=bindIndex; FUN_0057d200: EAX=kbutton_t*,
// ECX=currentTimeMs read from the same global the dispatcher reads, EDX=bindIndex) --
// not yet confirmed live with a debugger single-step, so this is a first attempt to be
// validated by real playtest per CLAUDE.md's "verify live" rule, same as the movement/
// look hooks were.
namespace {
constexpr uintptr_t kAdsKbutton1 = 0x00A98B8C;
constexpr uintptr_t kAdsKbutton2 = 0x00A98CB8;
constexpr uintptr_t kFrameTimeMsAddr = 0x0176B544;

// FIX (2026-07-14): originally used 13 for the down-case and 14 for the up-case,
// mirroring FUN_00438710's two DIFFERENT jump-table dispatch indices for the
// "+toggleads_throw"/"-toggleads_throw" command pair. That was wrong -- confirmed live
// (ADS would engage but could never be released, i.e. "toggles on, can't disable").
// Per the decompiled kbutton_t logic, KeyUp only clears a down[] slot if its keyId
// argument MATCHES what KeyDown originally stored there -- 13 and 14 never match, so
// KeyUp was a silent no-op every time. The dispatch index and the down[]-slot key
// identifier don't have to be the same value at all (they just happened to reuse the
// same EBX register at the real call sites) -- since we're calling these functions
// directly rather than going through the real dispatcher, we're free to pick any
// identifier as long as our own down/up calls agree with each other.
constexpr int kAdsBindIndex = 13;

// FUN_0057d1c0's real signature (confirmed via decompile, 2026-07-14): EAX=kbutton_t*
// (implicit self), ECX=bindIndex, and a THIRD arg -- current time in ms -- passed on
// the stack (PUSH before the call, caller cleans up after -- confirmed by the
// "PUSH EDI ... CALL ... ADD ESP,0x8" pattern around FUN_00438710's two calls to this
// function). The first implementation missed this stack argument entirely, leaving
// downtime (in_EAX[2] inside the callee) set to whatever garbage was on the stack --
// root cause of the "activates once then stays stuck" bug.
void CallKbuttonDown(uintptr_t kbutton, int bindIndex)
{
    uint32_t timeMs = *reinterpret_cast<volatile uint32_t*>(kFrameTimeMsAddr);
    constexpr uintptr_t kFn = 0x0057d1c0;
    __asm {
        push ebx
        mov eax, kbutton
        mov ecx, bindIndex
        mov ebx, kFn
        push timeMs
        call ebx
        add esp, 4
        pop ebx
    }
}

void CallKbuttonUp(uintptr_t kbutton, int bindIndex)
{
    uint32_t timeMs = *reinterpret_cast<volatile uint32_t*>(kFrameTimeMsAddr);
    constexpr uintptr_t kFn = 0x0057d200;
    __asm {
        push ebx
        mov eax, kbutton
        mov ecx, timeMs
        mov edx, bindIndex
        mov ebx, kFn
        call ebx
        pop ebx
    }
}

// XInput's own documented trigger threshold -- avoids a barely-touched trigger
// registering as a full press.
constexpr unsigned char kTriggerThreshold = 30;
bool g_adsHeld = false;
} // namespace

extern "C" void __cdecl InjectControllerAds()
{
    unsigned short buttons;
    unsigned char leftTrigger, rightTrigger;
    if (!Controller_GetRawButtonsAndTriggers(buttons, leftTrigger, rightTrigger)) return;

    bool nowHeld = leftTrigger >= kTriggerThreshold;
    if (nowHeld == g_adsHeld) return; // only fire on the edge, matching a real keypress

    g_adsHeld = nowHeld;
    if (nowHeld) {
        CallKbuttonDown(kAdsKbutton1, kAdsBindIndex);
        CallKbuttonDown(kAdsKbutton2, kAdsBindIndex);
    } else {
        CallKbuttonUp(kAdsKbutton1, kAdsBindIndex);
        CallKbuttonUp(kAdsKbutton2, kAdsBindIndex);
    }
}

// ---- Sprint: left stick click (L3) -> real pm_flags bit via a Pmove-entry hook ---
//
// FIRST ATTEMPT (struct+0xb0, 2026-07-14) confirmed WRONG by live playtest ("SPRINT NOT
// WORKING") and then confirmed WHY via decompile: FUN_0057d430 does read that flag, but
// only to gate an EXTRA forward/right movement summation that reuses the real keyboard
// +forward/+back hold-time helpers (FUN_0057d250/FUN_007380e0) -- since our movement hook
// writes forwardmove/rightmove as raw bytes instead of driving real kbuttons, those
// helpers always return 0 for us, so the flag gated a summation of zero. Right mechanism
// existed, wrong layer -- same trap Prone and ADS both hit before being solved properly.
//
// REAL MECHANISM (found 2026-07-14 via string xref -> dvar -> read-site tracing, not
// memdiff): the GSC-exposed dvar `player_sprintSpeedScale` (registered in FUN_00494310,
// pointer stored at DAT_01d397e4) is applied in FUN_00643870, gated by
// `*(uint*)(iVar2+0xc) & 0x4000` where iVar2 is a live playerState-style struct pointer.
// That same bit is read at the very top of the whole Pmove state machine,
// FUN_00644ed0(int* param_1) -- confirmed via disasm that param_1 is a plain stack arg
// (raw entry: [ESP+4], matching Ghidra's decompile of the prologue's
// `MOV EBX,[ESP+0x98]` after `SUB ESP,0x90`+`PUSH EBX`), and `*param_1` dereferences to
// the actual struct whose +0xc dword is pm_flags. This is read fresh from a register at
// every real call, so we hook FUN_00644ed0's entry and force/clear bit 0x4000 on the
// LIVE pointer each time -- no stored/hardcoded data address at all, matching the
// project's live-pointer-capture pattern already used for `cmd` in FUN_0057de60.
namespace {
constexpr unsigned short kXI_LEFT_THUMB = 0x0040;
constexpr uint32_t kPmFlagSprint = 0x4000u;
bool g_sprintHeld = false;
void* g_orig_00644ed0 = nullptr;
} // namespace

extern "C" void __cdecl InjectControllerSprint()
{
    unsigned short buttons;
    unsigned char leftTrigger, rightTrigger;
    if (!Controller_GetRawButtonsAndTriggers(buttons, leftTrigger, rightTrigger)) return;

    g_sprintHeld = (buttons & kXI_LEFT_THUMB) != 0;
}

// Called from Hook_00644ed0 with the live `param_1` (the pml/movement-locals pointer)
// pulled straight off the stack -- see the comment above for how that address was
// confirmed. Forces or clears the real sprint pm_flags bit every Pmove tick to match our
// polled controller state, same as a real held/released key would.
extern "C" void __cdecl InjectControllerSprintPmFlags(uint32_t pmlPtr)
{
    if (!pmlPtr) return;
    uint32_t ps = *reinterpret_cast<uint32_t*>(pmlPtr);
    if (!ps) return;
    uint32_t* flags = reinterpret_cast<uint32_t*>(ps + 0xc);
    if (g_sprintHeld) {
        *flags |= kPmFlagSprint;
    } else {
        *flags &= ~kPmFlagSprint;
    }
}

// Pure pre-hook, same shape as Hook_0057de60: grab the live stack argument, call our
// injector, then tail-jump into the untouched original. pushad shifts the stack by
// 0x20 (8 pushed registers), so the original [ESP+4] argument is now at [ESP+0x24].
__declspec(naked) void Hook_00644ed0()
{
    __asm {
        pushad
        mov eax, dword ptr [esp + 0x24]
        push eax
        call InjectControllerSprintPmFlags
        add esp, 4
        popad
        jmp dword ptr [g_orig_00644ed0]
    }
}

// ---- Look: right stick -> the pitch/yaw angle-delta accumulator directly -------
//
// Superseded 2026-07-14: this used to hook FUN_0057d680 (the raw mouse-delta
// source) so controller look would inherit sensitivity/m_yaw/m_pitch/cl_mouseAccel/
// m_filter "for free." User correctly flagged that as look effectively still being
// mouse emulation under the hood, not true native input. Switched to hooking
// FUN_0057de60 instead (the finalize step that packs the accumulated angle deltas
// into the final usercmd_t.angles) and writing directly to the accumulator globals,
// completely bypassing every mouse-specific cvar -- controller look now has its own
// independent sensitivity constant, no acceleration, no filtering.
//
// _DAT_00b36408 (pitch) / _DAT_00b3640c (yaw) are a float[3] PITCH/YAW/ROLL array
// (see re_notes/iw5sp.md), in DEGREES (confirmed via FUN_0057de60's own ANGLE2SHORT-
// style packing math). Not per-player-strided in the code that touches them (bare
// symbol, no offset arithmetic) -- fine since SP only ever has player 0 anyway.
//
// Sign convention derived (not guessed) from the OLD confirmed-correct mouse-pipeline
// behavior: FUN_0057d7e0 does `yaw -= mouseX * m_yaw` and `pitch += mouseY * m_pitch`
// (m_yaw/m_pitch cvars are positive by default). The old hook's confirmed-correct
// injected values were mouseX=+rx, mouseY=-ry -- substituting through both formulas
// gives yaw change proportional to -rx and pitch change proportional to -ry, so the
// direct-write equivalent subtracts both.
float* const kPitchAccum = reinterpret_cast<float*>(0x00B36408);
float* const kYawAccum = reinterpret_cast<float*>(0x00B3640C);

extern "C" void __cdecl InjectControllerLookAngles()
{
    float rx, ry;
    if (!Controller_GetRightStick(rx, ry)) return;
    if (rx == 0.0f && ry == 0.0f) return;

    float dt = Controller_DeltaTimeSeconds();
    if (dt <= 0.0f) return;

    // Degrees per second at full stick deflection -- independent of every mouse cvar.
    // Rough starting point; task #6's options screen is where a real user-facing
    // sensitivity setting belongs, not a hardcoded constant here.
    constexpr float kLookDegreesPerSecond = 250.0f;

    *kYawAccum -= rx * kLookDegreesPerSecond * dt;
    *kPitchAccum -= ry * kLookDegreesPerSecond * dt;
}

// ---- Combined per-frame entry point -- all controller injection lives here now ----
//
// Restructured 2026-07-14 to hook FUN_0057de60 (the always-running finalize step)
// instead of FUN_0057d430 directly, so controller injection keeps firing every frame
// regardless of the per-player gate bit (0x10 at DAT_00b36210) that FUN_0057e480 uses
// to skip FUN_0057d430/FUN_0057dc90/FUN_0057d300 entirely. `cmd` is `unaff_ESI` at
// FUN_0057de60's entry (confirmed by the existing usercmd_t field-offset notes in
// re_notes/iw5sp.md, e.g. "unaff_ESI[0] = DAT_01e06e88" for serverTime) -- captured by
// the naked hook stub below and passed through here.
//
// ATTEMPT 2, leaving the gate alone entirely (2026-07-14, same day): tried never
// touching the bit at all, on the theory that our injection no longer needed it cleared
// since it no longer depends on FUN_0057d430 actually running. Confirmed live this was
// wrong -- with the bit left to whatever the game naturally leaves it at, the game's own
// visible UI cursor stayed shown during real gameplay (no real mouse ever moves to
// trigger its normal hide logic) AND several other systems broke, ADS included -- the
// bit apparently gates more than just the three functions we bypass.
//
// ATTEMPT 3, context-aware via a menu-state field (2026-07-14): raw disassembly of
// FUN_0047e700 (the same function that routes mouse input to either look or the UI
// cursor based on this gate) references a global pointer (0x021cd678) to what looked
// like a "current menu" struct with a state field at +0xc. Diagnostic logging showed
// this theory doesn't hold up -- the field barely changed across an entire session
// (logged once, held at the same value throughout), not the active mechanism here.
//
// ATTEMPT 4, forcing OS window focus (2026-07-14): confirmed WRONG by the user -- the
// remaining issue isn't real Windows focus, it's this SAME in-engine gate/cursor state
// (the original diagnosis all along). Reverted.
//
// SETTLED (2026-07-14, explicit user call): back to the simplest version -- unconditional
// clear every frame, same as the very first fix. Movement/look/buttons/ADS all work
// immediately from level start with no click needed; K+M menu interaction (buy
// stations, etc.) is a known, documented limitation (see README.md) until task #6
// (native controller UI/menu navigation) is built -- that's the right place to solve
// this properly, since it requires actually understanding the in-engine menu open/close
// signal well enough to toggle this state correctly, not guessing at it from the
// outside. (Pause menu is specifically NOT a good test for that future work, either --
// it appears to reset this state itself when opened, unlike other menus.)
extern "C" void __cdecl InjectAllControllerInput(unsigned char* cmd)
{
    *reinterpret_cast<volatile uint32_t*>(0x00B36210) &= ~0x10u;

    InjectControllerLookAngles();
    if (cmd) {
        InjectControllerMovement(cmd);
        InjectControllerButtons(cmd);
    }
    InjectControllerAds();
    InjectControllerSprint();
}

namespace {
void* g_orig_0057de60 = nullptr;
}

// Pure pre-hook: inject our angle delta, then tail-jump into the untouched original
// (which does its own packing/return -- no need to intercept its return at all).
__declspec(naked) void Hook_0057de60()
{
    __asm {
        pushad
        push esi          // usercmd_t* (confirmed as ESI at this function's entry)
        call InjectAllControllerInput
        add esp, 4
        popad
        jmp dword ptr [g_orig_0057de60]
    }
}

void InstallAnalogInputHooks()
{
    MH_Initialize();
    MH_STATUS s2 = MH_CreateHook(reinterpret_cast<LPVOID>(0x0057de60), &Hook_0057de60, &g_orig_0057de60);
    if (s2 == MH_OK) MH_EnableHook(reinterpret_cast<LPVOID>(0x0057de60));

    MH_STATUS s3 = MH_CreateHook(reinterpret_cast<LPVOID>(0x00644ed0), &Hook_00644ed0, &g_orig_00644ed0);
    if (s3 == MH_OK) MH_EnableHook(reinterpret_cast<LPVOID>(0x00644ed0));
}
