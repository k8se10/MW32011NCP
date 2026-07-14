// analog_input_hooks.cpp — real analog movement + look injection (task #5).
//
// Both hooks are strictly ADDITIVE post-hooks: call the original function first
// (preserving 100% of existing keyboard/mouse behavior), then add our controller-
// derived contribution on top. See re_notes/iw5sp.md ("Calling convention CONFIRMED
// live", 2026-07-14) for how the register roles below were verified against actual
// running gameplay, not just Ghidra's static guesses.
//
// KNOWN OPEN ITEM: sign conventions (does pushing the left stick up increase or
// decrease forwardmove; does positive right-stick X need to add or subtract from the
// mouse-X output) are a best-effort guess below, matching the most common convention
// (XInput Y-up-positive == "forward" positive, XInput X-right-positive == mouse-X-
// right-positive). This has NOT been visually confirmed against actual in-game
// character movement yet -- that requires a real controller and a human watching the
// screen, which isn't something this automated pass can do. Flip the relevant sign(s)
// in Controller_GetLeftStick/Controller_GetRightStick call sites below if movement
// or look comes out inverted during first playtest.

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

// ---- Look: right stick -> raw mouse-delta output floats (*ESI = dx, *EDI = dy) ----
extern "C" void __cdecl InjectControllerLook(float* outX, float* outY)
{
    if (!outX || !outY) return;
    float rx, ry;
    if (!Controller_GetRightStick(rx, ry)) return;
    if (rx == 0.0f && ry == 0.0f) return;

    float dt = Controller_DeltaTimeSeconds();
    if (dt <= 0.0f) return;

    // Tunable look speed in raw-mouse-delta units per second at full deflection.
    // The rest of the pipeline (FUN_0057d740's sensitivity/accel, FUN_0057d7e0's
    // m_yaw/m_pitch) scales this exactly like real mouse movement, so this constant
    // is deliberately a rough starting point, not a finished sensitivity value --
    // task #6's options screen is where a real user-facing sensitivity setting
    // belongs, not a hardcoded constant here.
    constexpr float kLookUnitsPerSecond = 4000.0f;

    *outX += rx * kLookUnitsPerSecond * dt;
    // Inverted so pushing the stick up (XInput Y positive) looks up (matches the
    // default, non-inverted console convention) -- flip if this reads backwards
    // during playtest.
    *outY += -ry * kLookUnitsPerSecond * dt;
}

namespace {
void* g_orig_0057d680 = nullptr;
}

__declspec(naked) void Hook_0057d680()
{
    __asm {
        push esi          // save outX ptr
        push edi          // save outY ptr
        call dword ptr [g_orig_0057d680]
        pop edi           // restore outY ptr
        pop esi           // restore outX ptr
        push edi
        push esi
        call InjectControllerLook
        add esp, 8
        ret
    }
}

void InstallAnalogInputHooks()
{
    MH_Initialize();
    MH_STATUS s1 = MH_CreateHook(reinterpret_cast<LPVOID>(0x0057d430), &Hook_0057d430, &g_orig_0057d430);
    MH_STATUS s2 = MH_CreateHook(reinterpret_cast<LPVOID>(0x0057d680), &Hook_0057d680, &g_orig_0057d680);
    if (s1 == MH_OK) MH_EnableHook(reinterpret_cast<LPVOID>(0x0057d430));
    if (s2 == MH_OK) MH_EnableHook(reinterpret_cast<LPVOID>(0x0057d680));
}
