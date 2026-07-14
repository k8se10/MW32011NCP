// analog_input_hooks.cpp — real analog movement + look injection (task #5).
//
// Movement is a strictly ADDITIVE post-hook on FUN_0057d430: call the original
// keyboard-movement function first (100% of keyboard behavior preserved), then add
// the controller's contribution on top of usercmd_t.forwardmove/rightmove.
//
// Look is a pre-hook directly on the pitch/yaw angle-delta accumulator (see the
// comment above InjectControllerLookAngles) -- deliberately NOT routed through the
// mouse-delta pipeline, so it doesn't inherit sensitivity/m_yaw/m_pitch/cl_mouseAccel/
// m_filter and has its own independent feel, confirmed both directionally and as
// architecturally "true" native input (not mouse emulation) with the user 2026-07-14.
//
// All sign conventions below are confirmed against actual real-controller-hardware
// playtest, not just Ghidra's static guesses -- see re_notes/iw5sp.md.

#include <windows.h>
#include <cstdint>
#include <cstdio>
#include "../third_party/minhook/include/MinHook.h"
#include "controller_input.h"

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

// ---- Buttons: NATIVE raw-bit diagnostic pass (task #10, in progress 2026-07-14) --
//
// A keybd_event/mouse_event-based synthetic-key approach was tried and REJECTED --
// user wants true native in-engine calls, not OS-level input emulation, matching the
// same principle already applied to movement/look.
//
// Real bind data (players2/config.cfg, genuine Infinity Ward default binds) confirmed
// which actions are HELD kbuttons already present in the known 32-entry kbutton name
// table (+gostand/+activate/+frag/+smoke/+breath_sprint/+melee_zoom/+actionslot 1-4/
// +scores/+toggleads_throw/+attack/+reload) vs. ONE-SHOT console commands not in that
// table at all (togglemenu/weapnext/toggleprone) -- see re_notes/iw5sp.md for the full
// table. X (F key = "+activate") is confirmed context-sensitive on console too
// (reload when ammo allows, interact/use otherwise), so it covers both without a
// separate reload input.
//
// This diagnostic writes DIRECTLY to usercmd_t.buttons (offset +4) -- fully native,
// no OS-level input emulation at all -- one XInput input per known bit, purely so a
// single self-driven test pass (virtual controller + screenshots, not requiring the
// user's time) can identify each bit's real action via its visual tell (muzzle
// flash=fire, stance change=crouch/prone, etc.) before committing to a final mapping.
namespace {
constexpr unsigned short kXI_DPAD_UP = 0x0001;
constexpr unsigned short kXI_DPAD_DOWN = 0x0002;
constexpr unsigned short kXI_DPAD_LEFT = 0x0004;
constexpr unsigned short kXI_DPAD_RIGHT = 0x0008;
constexpr unsigned short kXI_START = 0x0010;
constexpr unsigned short kXI_BACK = 0x0020;
constexpr unsigned short kXI_LEFT_THUMB = 0x0040;
constexpr unsigned short kXI_LEFT_SHOULDER = 0x0100;
constexpr unsigned short kXI_RIGHT_SHOULDER = 0x0200;
constexpr unsigned short kXI_A = 0x1000;
constexpr unsigned short kXI_B = 0x2000;
constexpr unsigned short kXI_X = 0x4000;
constexpr unsigned short kXI_Y = 0x8000;
}

extern "C" void __cdecl InjectControllerButtonsDiagnostic(unsigned char* cmd)
{
    if (!cmd) return;
    unsigned short xiButtons;
    unsigned char lt, rt;
    if (!Controller_GetRawButtonsAndTriggers(xiButtons, lt, rt)) return;

    uint32_t out = 0;
    if (xiButtons & kXI_A) out |= 0x1;
    if (xiButtons & kXI_B) out |= 0x4;
    if (xiButtons & kXI_X) out |= 0x8;
    if (xiButtons & kXI_Y) out |= 0x10;
    if (xiButtons & kXI_LEFT_SHOULDER) out |= 0x20;
    if (xiButtons & kXI_RIGHT_SHOULDER) out |= 0x100;
    if (xiButtons & kXI_BACK) out |= 0x200;
    if (xiButtons & kXI_START) out |= 0x400;
    if (xiButtons & kXI_DPAD_UP) out |= 0x2000;
    if (xiButtons & kXI_DPAD_DOWN) out |= 0x4000;
    if (xiButtons & kXI_DPAD_LEFT) out |= 0x8000;
    if (xiButtons & kXI_DPAD_RIGHT) out |= 0x40000;
    if (xiButtons & kXI_LEFT_THUMB) out |= 0x80000;

    if (out == 0) return;
    uint32_t* buttonsField = reinterpret_cast<uint32_t*>(cmd + 4);
    *buttonsField |= out;
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

namespace {
void* g_orig_0057d430 = nullptr;
}

__declspec(naked) void Hook_0057d430()
{
    __asm {
        push eax          // save player index
        push edi          // save usercmd_t* (== esi at entry, confirmed live)
        call dword ptr [g_orig_0057d430]
        pop edi           // restore usercmd_t* (original may have clobbered edi/esi)
        pop eax           // restore player index
        push edi
        call InjectControllerMovement
        add esp, 4
        push edi
        call InjectControllerButtonsDiagnostic
        add esp, 4
        call InjectControllerAds
        ret
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
    // "Needs a click" fix, take 2 (2026-07-14): forcing the cursor-visibility flag
    // (DAT_01c00474, see git history) didn't unblock movement -- wrong theory. Raw
    // disassembly of FUN_0057e480 (not just the decompile, which dropped the constant
    // args) shows the REAL first gate: `FUN_00416150(EBX, 0x10)` right at entry, before
    // even the keyboard-turn call -- if bit 0x10 of a per-player flags dword at
    // DAT_00b36210 (stride 0x188, offset 0 for SP's player index 0) is set, EVERYTHING
    // is skipped except the always-running finalize call. This is a different state
    // block than DAT_00b37444 (which gates a LATER, less restrictive branch) or the
    // cursor flag (a UI-only symptom, not a code-level gate at all). Force this bit
    // clear every frame, unconditionally, before any early return below -- this hook
    // fires in every branch of FUN_0057e480, including the one this bit itself gates.
    *reinterpret_cast<volatile uint32_t*>(0x00B36210) &= ~0x10u;

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

namespace {
void* g_orig_0057de60 = nullptr;
}

// Pure pre-hook: inject our angle delta, then tail-jump into the untouched original
// (which does its own packing/return -- no need to intercept its return at all).
__declspec(naked) void Hook_0057de60()
{
    __asm {
        pushad
        call InjectControllerLookAngles
        popad
        jmp dword ptr [g_orig_0057de60]
    }
}

// NOTE (2026-07-14): the FUN_0057e480 bit-0x80000 gate-clear hook that used to live
// here was based on a wrong theory -- diagnostic logging showed that flag already
// reads 0 even while the "needs a click" problem is present, so it isn't the real
// mechanism. User pinpointed the actual cause directly: the in-engine menu/loading-
// screen mouse cursor doesn't auto-hide when a level starts, and a click is literally
// just dismissing that visible cursor (confirmed as fully in-engine, not a Windows
// focus issue). Real fix belongs on the ui_cursor cvar/cursor-visibility state
// instead -- see re_notes/iw5sp.md, investigation in progress.

void InstallAnalogInputHooks()
{
    MH_Initialize();
    MH_STATUS s1 = MH_CreateHook(reinterpret_cast<LPVOID>(0x0057d430), &Hook_0057d430, &g_orig_0057d430);
    MH_STATUS s2 = MH_CreateHook(reinterpret_cast<LPVOID>(0x0057de60), &Hook_0057de60, &g_orig_0057de60);
    if (s1 == MH_OK) MH_EnableHook(reinterpret_cast<LPVOID>(0x0057d430));
    if (s2 == MH_OK) MH_EnableHook(reinterpret_cast<LPVOID>(0x0057de60));
}
