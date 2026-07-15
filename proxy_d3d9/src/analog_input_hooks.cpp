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
//   X -> Interact (bit 0x8) + real Reload kbutton (see InjectControllerReload).
//        Two earlier raw-usercmd-bit attempts both failed live (one had no effect, one
//        turned out to be an unrelated color-grading toggle) -- see re_notes/iw5sp.md.
//        Real fix: memdiff (watching actual R-key transitions, tuned to avoid noise
//        from shooting/ammo changes) found a real static kbutton_t at 0x00A98C68,
//        confirmed via CallKbuttonDown/CallKbuttonUp, same technique as ADS.
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
//   Left stick click (L3) -> Sprint -- NOT handled here, see InjectControllerSprint /
//        InjectControllerSprintPmFlags. CONFIRMED WORKING live (2026-07-14) -- forces
//        the real pm_flags bit (0x4000) that drives `player_sprintSpeedScale`, via a
//        hook on the Pmove entry point (FUN_00644ed0) plus a reassert hook one level
//        deeper (FUN_00643ce0) to survive whatever clears it in between. See
//        re_notes/iw5sp.md for the full investigation.
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
    if (xiButtons & kXI_X) out |= 0x8;                          // Interact -- Reload still unresolved, see re_notes/iw5sp.md
    if (xiButtons & kXI_LEFT_SHOULDER) out |= 0x8000;           // Tactical (smoke)
    if (xiButtons & kXI_RIGHT_SHOULDER) out |= 0x4000;          // Lethal (frag)
    if (xiButtons & kXI_A) out |= 0x400;                        // Jump (+gostand)

    bool bHeld = (xiButtons & kXI_B) != 0;
    if (bHeld && !g_crouchButtonWasHeld) {
        // Rising edge: new press starting.
        g_crouchButtonPressStartMs = GetTickCount();
        g_holdActionConsumed = false;
    }
    if (bHeld && !g_holdActionConsumed) {
        DWORD heldMs = GetTickCount() - g_crouchButtonPressStartMs;
        if (heldMs >= kProneHoldThresholdMs) {
            // Hold action fires once, the instant the threshold is crossed: Prone
            // reverses back to Standing, anything else goes to Prone.
            g_stance = (g_stance == Stance::Prone) ? Stance::Standing : Stance::Prone;
            g_holdActionConsumed = true;
        }
    }
    if (!bHeld && g_crouchButtonWasHeld && !g_holdActionConsumed) {
        // Falling edge and the hold threshold was never reached -- this was a tap.
        switch (g_stance) {
            case Stance::Standing: g_stance = Stance::Crouched; break;
            case Stance::Crouched: g_stance = Stance::Standing; break;
            case Stance::Prone:    g_stance = Stance::Crouched; break;
        }
    }
    g_crouchButtonWasHeld = bHeld;

    switch (g_stance) {
        case Stance::Crouched: out |= 0x200u; break;
        case Stance::Prone:    out |= 0x100u; break;
        default: break;
    }

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

// ---- Reload: X -> real +reload kbutton, found via memdiff + pointer scan (2026-07-15) --
//
// Two prior attempts on X (raw usercmd bits 0x40000, then ruled out) both failed live --
// see re_notes/iw5sp.md. Real mechanism found via memdiff watching real R-key
// transitions: a clean single candidate at 0x00A98C68 (held=0x72 'r' ASCII,
// released=0x00), a STATIC address in the same per-player struct region already used
// for the ADS kbuttons -- not a moving heap address like the first memdiff pass caught.
// 0x00A98C78 (+0x10 from it) also correlated (held=0x01/released=0x00), matching
// kbutton_t's confirmed `active` field offset exactly -- strong confirmation this is a
// real kbutton_t, same struct layout as ADS's, not a coincidental correlate.
namespace {
constexpr uintptr_t kReloadKbutton = 0x00A98C68;
constexpr int kReloadBindIndex = 15; // distinct from ADS's 13 -- arbitrary but must be
                                      // self-consistent between our own down/up calls
bool g_reloadHeld = false;
} // namespace

extern "C" void __cdecl InjectControllerReload()
{
    unsigned short buttons;
    unsigned char leftTrigger, rightTrigger;
    if (!Controller_GetRawButtonsAndTriggers(buttons, leftTrigger, rightTrigger)) return;

    bool nowHeld = (buttons & kXI_X) != 0;
    if (nowHeld == g_reloadHeld) return; // only fire on the edge, matching a real keypress

    g_reloadHeld = nowHeld;
    if (nowHeld) {
        CallKbuttonDown(kReloadKbutton, kReloadBindIndex);
    } else {
        CallKbuttonUp(kReloadKbutton, kReloadBindIndex);
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

    bool held = (buttons & kXI_LEFT_THUMB) != 0;
    if (held && !g_sprintHeld && g_stance != Stance::Standing) {
        // Rising edge while crouched/prone: real console sprint stands the player back
        // up to full upright first, same as pressing forward while ducked/prone does --
        // forcing the pm_flags bit alone doesn't touch our own stance state at all, so
        // without this, sprint would just run while still crouched/prone (bug found
        // 2026-07-15).
        g_stance = Stance::Standing;
    }
    g_sprintHeld = held;
}

// Called from Hook_00644ed0 with the live `param_1` (the pml/movement-locals pointer)
// pulled straight off the stack -- see the comment above for how that address was
// confirmed. Forces or clears the real sprint pm_flags bit every Pmove tick to match our
// polled controller state, same as a real held/released key would. Gated on being
// upright -- never assert sprint while crouched/prone (see InjectControllerSprint).
extern "C" void __cdecl InjectControllerSprintPmFlags(uint32_t pmlPtr)
{
    if (!pmlPtr) return;
    uint32_t ps = *reinterpret_cast<uint32_t*>(pmlPtr);
    if (!ps) return;
    uint32_t* flags = reinterpret_cast<uint32_t*>(ps + 0xc);
    if (g_sprintHeld && g_stance == Stance::Standing) {
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

// REASSERT POINT: our write at FUN_00644ed0's entry succeeds, but something between
// there and the actual pm_flags read in FUN_00643870 clears it every tick -- neither
// FUN_00644ed0 nor FUN_00643ce0's own decompiled logic explicitly clears bit 0x4000, so
// it happens in one of the many sub-calls in between (confirmed via temporary read-site
// instrumentation during investigation, since removed -- see re_notes/iw5sp.md for the
// full trace). Reasserting here, one level deeper, is what makes it survive through to
// the actual read.
namespace {
void* g_orig_00643ce0 = nullptr;
}

extern "C" void __cdecl ReassertSprintPmFlags(uint32_t pmlPtr)
{
    if (!g_sprintHeld || g_stance != Stance::Standing) return;
    if (!pmlPtr) return;
    uint32_t ps = *reinterpret_cast<uint32_t*>(pmlPtr);
    if (!ps) return;
    *reinterpret_cast<uint32_t*>(ps + 0xc) |= kPmFlagSprint;
}

// CRASH FIX (2026-07-14): first version of this hook used raw-entry [ESP+8] and
// crashed the game (confirmed via Windows Event Log Application Error -> proxy d3d9.dll
// offset 0x4e -> ReassertSprintPmFlags's `MOV ECX,[EAX+0xc]`, i.e. dereferencing a
// garbage pointer). Root cause: misread the prologue -- FUN_00643ce0's `PUSH ESI`
// instruction executes BETWEEN the two `MOV reg,[ESP+0x74]` reads, so they read
// DIFFERENT stack slots, not the same one twice. Confirmed via the real call site
// (`PUSH EDX` for a local scratch buffer, THEN `PUSH EBX` for the pml pointer, THEN
// `CALL`) that the pml pointer is the LAST-pushed arg, landing at raw-entry [ESP+4] --
// same slot/formula as every other hook in this file, not [ESP+8].
__declspec(naked) void Hook_00643ce0()
{
    __asm {
        pushad
        mov eax, dword ptr [esp + 0x24]
        push eax
        call ReassertSprintPmFlags
        add esp, 4
        popad
        jmp dword ptr [g_orig_00643ce0]
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

// ---- One-shot commands (Y=weapnext, Start=togglemenu) via the real command dispatcher --
//
// Found 2026-07-15 by using external research instead of continuing blind: a community
// write-up on classic CoD-engine RE (kiwidog.me) describes searching a binary for a
// hardcoded "screenshot" command string as an anchor to find the real
// Cbuf_AddText-equivalent, since that dev command is almost always issued through it
// somewhere. We already had exactly this in hand from hours earlier in the same
// investigation (decompiling callers of the special-bind dispatcher FUN_00438710 turned
// up FUN_004dfd30, which contains `FUN_00457c90(*param_1, "screenshot\n")`) -- it just
// hadn't been recognized as the anchor until the research named the pattern.
//
// FUN_00457c90 decompiles to a lock-protected (FUN_00428af0/FUN_00528fe0 acquire/release
// a critical section, id 0x1f), per-client text-buffer append: it takes a client index
// and a command string, special-cases a "p0 " prefix to redirect the target client index
// (irrelevant for SP, always client 0), computes the string length, and appends the
// string (including its null terminator) into a per-client buffer whose base/capacity/
// write-offset triplet lives at &DAT_017507e4/e8/ec + clientIndex*0xc, bounds-checked
// against capacity. The write-offset only advances by the string length (NOT length+1),
// so the next append overwrites the terminator -- meaning the buffer is a plain
// concatenated command stream, not individually-delimited entries, and the caller MUST
// supply its own trailing "\n" (confirmed by "screenshot\n" including one) since nothing
// here adds one automatically. This is exactly Cbuf_AddText's textbook Quake3-lineage
// behavior ("adds command text at the end of the buffer, does not add a final \n").
//
// Calling convention confirmed via the real call site in FUN_004dfd30 (PUSH the string,
// PUSH the client index, CALL, caller does ADD ESP,0x8 afterward) -- plain __cdecl, args
// in normal declared order, no register-passed weirdness at all (unlike almost every
// other hook in this file). Callable directly as an ordinary C function pointer.
namespace {
using CbufAddTextFn = void(__cdecl*)(int clientIndex, const char* text);
CbufAddTextFn const CbufAddText = reinterpret_cast<CbufAddTextFn>(0x00457c90);
constexpr int kLocalClientIndex = 0; // SP only ever has player 0

constexpr unsigned short kXI_Y = 0x8000;
constexpr unsigned short kXI_START = 0x0010;
bool g_weaponNextHeld = false;
bool g_startHeld = false;
} // namespace

// Y -> weapnext. Edge-triggered (fires once per press, not every frame held) -- this
// appends into a growing text buffer, so holding the button would spam "weapnext\n"
// into it every single frame, same class of bug as any key-repeat issue.
extern "C" void __cdecl InjectControllerWeaponNext()
{
    unsigned short buttons;
    unsigned char leftTrigger, rightTrigger;
    if (!Controller_GetRawButtonsAndTriggers(buttons, leftTrigger, rightTrigger)) return;

    bool held = (buttons & kXI_Y) != 0;
    if (held && !g_weaponNextHeld) {
        CbufAddText(kLocalClientIndex, "weapnext\n");
    }
    g_weaponNextHeld = held;
}

// Start -> togglemenu (the real ESC/pause-menu bind command). Edge-triggered, same
// reasoning as weapnext above.
extern "C" void __cdecl InjectControllerPauseMenu()
{
    unsigned short buttons;
    unsigned char leftTrigger, rightTrigger;
    if (!Controller_GetRawButtonsAndTriggers(buttons, leftTrigger, rightTrigger)) return;

    bool held = (buttons & kXI_START) != 0;
    if (held && !g_startHeld) {
        CbufAddText(kLocalClientIndex, "togglemenu\n");
    }
    g_startHeld = held;
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
// (native controller UI/menu navigation) is built.
//
// REOPENED (2026-07-15): confirmed WORSE than "known limitation" -- using a buy station
// then opening and closing the pause menu leaves the player completely unable to move,
// and diagnostic logging showed this isn't controller-specific: real mouse/keyboard
// input stops registering too. Since our own logging also confirmed the gate bit itself
// reads 0x00000000 (already cleared) throughout the whole broken window, the bug isn't
// "the bit ends up wrongly set" -- it's the opposite: forcibly holding this bit at 0
// *permanently* likely interferes with the buy station's own closing sequence, which may
// need the bit to legitimately become 1 briefly to detect "menu fully closing, finish
// cleanup" -- if that transition never gets to happen, the game's own menu-depth/state
// tracking can get stuck desynced, blocking ALL input (ours and real) until level reload.
//
// FIX: reinstated the earlier 3-second rising-edge window (originally found and
// confirmed working for this exact buy-station scenario on 2026-07-14, before an
// unrelated same-day architecture change moved the hook to FUN_0057de60 and the window
// fix was never re-adapted -- it just got replaced with the unconditional clear above
// without being re-tested against real buy-station use). Only force-clears the bit for
// 3 seconds after entering a level; leaves it alone for the rest of gameplay, same as
// the original confirmed-working behavior. Re-verify live: (1) still no click needed at
// level start, (2) ADS/cursor still behave normally during general gameplay (this is
// the part ATTEMPT 2 found broken when leaving the bit alone from the start -- unclear
// whether that was really about the bit itself or about the different hook location at
// the time), (3) buy station open/use still works, (4) buy station -> pause -> resume no
// longer breaks movement.
namespace {
constexpr uintptr_t kInLevelFlagAddr = 0x00A98ACC; // same flag tools/memdiff uses to detect level load
constexpr DWORD kGateForceWindowMs = 3000;
bool g_wasInLevel = false;
DWORD g_levelEnterTick = 0;
}

extern "C" void __cdecl InjectAllControllerInput(unsigned char* cmd)
{
    int32_t inLevelVal = *reinterpret_cast<volatile int32_t*>(kInLevelFlagAddr);
    bool nowInLevel = inLevelVal > 0;
    if (nowInLevel && !g_wasInLevel) {
        g_levelEnterTick = GetTickCount();
    }
    g_wasInLevel = nowInLevel;

    if (nowInLevel && (GetTickCount() - g_levelEnterTick) < kGateForceWindowMs) {
        *reinterpret_cast<volatile uint32_t*>(0x00B36210) &= ~0x10u;
    }

    // ATTEMPT 3 RE-TEST (2026-07-15): re-checked the 0x021cd678+0xc "menu field" lead with
    // change-triggered diagnostic logging across 9 real ESC presses in and out of the
    // pause menu -- the field never changed once across all 9 transitions. Confirmed dead
    // for real this time (the original dismissal was right; it just hadn't been tested
    // this rigorously before). Diagnostic block removed now that this is settled.

    InjectControllerLookAngles();
    if (cmd) {
        InjectControllerMovement(cmd);
        InjectControllerButtons(cmd);
    }
    InjectControllerAds();
    InjectControllerSprint();
    InjectControllerReload();
    InjectControllerWeaponNext();
    InjectControllerPauseMenu();
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
    char buf[128];
    sprintf_s(buf, "[hooks] MH_CreateHook(0057de60) = %d", static_cast<int>(s2));
    LogFromController(buf);
    if (s2 == MH_OK) {
        MH_STATUS e2 = MH_EnableHook(reinterpret_cast<LPVOID>(0x0057de60));
        sprintf_s(buf, "[hooks] MH_EnableHook(0057de60) = %d", static_cast<int>(e2));
        LogFromController(buf);
    }

    MH_STATUS s3 = MH_CreateHook(reinterpret_cast<LPVOID>(0x00644ed0), &Hook_00644ed0, &g_orig_00644ed0);
    sprintf_s(buf, "[hooks] MH_CreateHook(00644ed0) = %d", static_cast<int>(s3));
    LogFromController(buf);
    if (s3 == MH_OK) {
        MH_STATUS e3 = MH_EnableHook(reinterpret_cast<LPVOID>(0x00644ed0));
        sprintf_s(buf, "[hooks] MH_EnableHook(00644ed0) = %d", static_cast<int>(e3));
        LogFromController(buf);
    }

    MH_STATUS s4 = MH_CreateHook(reinterpret_cast<LPVOID>(0x00643ce0), &Hook_00643ce0, &g_orig_00643ce0);
    sprintf_s(buf, "[hooks] MH_CreateHook(00643ce0) = %d", static_cast<int>(s4));
    LogFromController(buf);
    if (s4 == MH_OK) {
        MH_STATUS e4 = MH_EnableHook(reinterpret_cast<LPVOID>(0x00643ce0));
        sprintf_s(buf, "[hooks] MH_EnableHook(00643ce0) = %d", static_cast<int>(e4));
        LogFromController(buf);
    }
}
