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

void InstallAnalogInputHooks()
{
    MH_Initialize();
    MH_STATUS s1 = MH_CreateHook(reinterpret_cast<LPVOID>(0x0057d430), &Hook_0057d430, &g_orig_0057d430);
    MH_STATUS s2 = MH_CreateHook(reinterpret_cast<LPVOID>(0x0057de60), &Hook_0057de60, &g_orig_0057de60);
    if (s1 == MH_OK) MH_EnableHook(reinterpret_cast<LPVOID>(0x0057d430));
    if (s2 == MH_OK) MH_EnableHook(reinterpret_cast<LPVOID>(0x0057de60));
}
