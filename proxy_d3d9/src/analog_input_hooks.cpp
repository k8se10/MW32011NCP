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
#include <cstring>
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

// Shared hold-threshold, also reused by Survival's ready-up hold (further down, near
// Y's ready-up section) and by Interact's hold-to-interact gate below. Moved up here
// (rather than staying local to the ready-up section it was first added for) purely so
// it's declared before Interact's earlier use of it in this file -- same constant,
// same value, not a second threshold.
constexpr DWORD kReadyUpHoldThresholdMs = 740;

// SP only ever has player 0. Moved up here (from its original spot near the weapnext
// section) so it's declared before the ADS look-slowdown code's earlier use of it in
// this file -- same constant, not a second one.
constexpr int kLocalClientIndex = 0;

enum class Stance { Standing, Crouched, Prone };
Stance g_stance = Stance::Standing;
DWORD g_crouchButtonPressStartMs = 0;
bool g_crouchButtonWasHeld = false;
bool g_holdActionConsumed = false; // true once this press has already fired its hold action

// ---- Stuck-prone diagnostic instrumentation (2026-07-15/16, task #10) --------------
//
// Log-only (no behavior change) instrumentation added to chase a live-reported,
// game-breaking bug: using the Predator missile killstreak while prone (confirmed via
// the user's own repro to get stuck DURING/AFTER the missile-cam sequence, not at the
// D-pad select itself) leaves the player permanently stuck prone, not recoverable even
// via real keyboard input. An earlier attempt "fixed" this by auto-standing before a
// killstreak-type D-pad select -- REJECTED: real console MW3 doesn't force standing to
// use a killstreak prone, so that changed behavior instead of fixing a bug. Reverted.
//
// This logs g_stance (ours) alongside the real native per-player stance byte
// (&DAT_00b363b0 + playerIndex*0xbe5c, the same memory FUN_00438710's cases 0x3b/0x4c/
// 0x4d toggle/clear) on every stance transition, on every D-pad press, and on a ~500ms
// heartbeat regardless of input -- so a real repro produces a timeline showing exactly
// when/how the two diverge, without needing to already know the missile-cam's own
// entry/exit flag. Once a repro log is captured, this can be trimmed back down.
constexpr uintptr_t kRealStanceByteAddr = 0x00b363b0; // player 0 (SP-only, stride*0 offset)
DWORD g_lastStanceDiagLogMs = 0;

void LogStanceDiag(const char* tag)
{
    uint8_t realByte = *reinterpret_cast<volatile uint8_t*>(kRealStanceByteAddr);
    char buf[160];
    sprintf_s(buf, "[stance-diag] %s g_stance=%d realStanceByte=0x%02x t=%lu",
              tag, static_cast<int>(g_stance), realByte, GetTickCount());
    LogFromController(buf);
}

// ---- Interact: hold-to-interact, not instant-on-tap (2026-07-16) -------------------
//
// User feedback after v0.1.0-prealpha: Interact should require a hold, not fire the
// instant X is pressed. Reusing the same hold threshold already tuned for the Survival
// ready-up hold (kReadyUpHoldThresholdMs, 740ms, defined further down near Y's ready-up
// section) per explicit direction ("same timing as the F5 replacement would work
// fine"). Scoped ONLY to the raw usercmd Interact bit (0x8) below -- Reload
// (InjectControllerReload, a separate real kbutton on the same physical X button) is
// untouched and still fires instantly on press/release, since reload isn't the thing
// that was asked to require a hold.
DWORD g_interactPressStartMs = 0;
bool g_interactButtonWasHeld = false;
}

extern "C" void __cdecl InjectControllerButtons(unsigned char* cmd)
{
    if (!cmd) return;
    unsigned short xiButtons;
    unsigned char leftTrigger, rightTrigger;
    if (!Controller_GetRawButtonsAndTriggers(xiButtons, leftTrigger, rightTrigger)) return;

    DWORD nowMs = GetTickCount();
    if (nowMs - g_lastStanceDiagLogMs >= 500) {
        LogStanceDiag("heartbeat");
        g_lastStanceDiagLogMs = nowMs;
    }

    uint32_t out = 0;
    bool fireHeld = rightTrigger >= kTriggerThresholdFire;
    if (fireHeld) out |= 0x1;                                   // Fire (+attack)
    if (xiButtons & kXI_RIGHT_THUMB) out |= 0x4;                // Melee
    if (xiButtons & kXI_LEFT_SHOULDER) out |= 0x8000;           // Tactical (smoke)
    if (xiButtons & kXI_RIGHT_SHOULDER) out |= 0x4000;          // Lethal (frag)
    if (xiButtons & kXI_A) out |= 0x400;                        // Jump (+gostand)

    // Interact (0x8): hold-to-interact, not instant-on-tap -- see the comment on
    // g_interactPressStartMs above. Reload (a separate real kbutton, same physical X
    // button) is unaffected and still fires instantly; see InjectControllerReload.
    bool xHeld = (xiButtons & kXI_X) != 0;
    if (xHeld && !g_interactButtonWasHeld) {
        g_interactPressStartMs = GetTickCount();
    }
    if (xHeld && (GetTickCount() - g_interactPressStartMs) >= kReadyUpHoldThresholdMs) {
        out |= 0x8;
    }
    g_interactButtonWasHeld = xHeld;

    {
        static bool s_fireHeldForDiag = false;
        if (fireHeld != s_fireHeldForDiag) {
            LogStanceDiag(fireHeld ? "fire-press" : "fire-release");
            s_fireHeldForDiag = fireHeld;
        }
    }

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
            LogStanceDiag("hold-fire");
        }
    }
    if (!bHeld && g_crouchButtonWasHeld && !g_holdActionConsumed) {
        // Falling edge and the hold threshold was never reached -- this was a tap.
        switch (g_stance) {
            case Stance::Standing: g_stance = Stance::Crouched; break;
            case Stance::Crouched: g_stance = Stance::Standing; break;
            case Stance::Prone:    g_stance = Stance::Crouched; break;
        }
        LogStanceDiag("tap-fire");
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

// ---- Back: +scores (scoreboard) -- STILL UNRESOLVED, REVERTED (2026-07-15) -------
//
// Attempted a static shortcut: found "+scores" in the same 8-byte-stride bind-name table
// already used to confirm +reload=idx26/+actionslot4=idx10/+stance=idx11 (base
// 0x00929fa4) -- "+scores" cleanly resolves to idx 31 with zero remainder, same clean fit
// as every other confirmed entry. ASSUMED (wrongly) that this table's index number is the
// SAME numbering FUN_00438710's switch dispatches on, and used case 0x1f (31 decimal,
// address 0xA98B14) directly without independent confirmation. CONFIRMED WRONG LIVE:
// holding Back made the player walk backward, meaning 0xA98B14 is almost certainly the
// real +back (movement) kbutton, not +scores. Root cause: ADS/Reload's case numbers were
// each confirmed by searching FUN_00438710's disassembly for an address ALREADY verified
// independently (via memdiff or an xref chain) -- never by trusting the bind-name table's
// index as if it were the same enumeration as the switch's case numbers. That assumption
// was never actually validated and doesn't hold; the two tables are apparently ordered
// differently. Three live memdiff attempts on TAB itself also failed (two collapsed to
// zero candidates, one produced a noisy 67-reference heap cluster Ghidra confirmed has
// zero real code references -- see re_notes/known_issues.md). Needs a properly
// independent method next: e.g. live-reading FUN_00541020's raw-keycode dispatch table
// (DAT_00a98e4c) for VK_TAB to get the REAL case number directly from the same lookup
// the game itself uses, the same idea already tried (inconclusively) for Reload.

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

// ---- Sprint stamina/cooldown (2026-07-15) --------------------------------------------
//
// Our own layer, not the game's real one: forcing kPmFlagSprint every tick (below)
// bypasses whatever native duration/recovery timer normally limits sprint -- confirmed
// this gives infinite sprint, unlike real vanilla keyboard play. `player_sprintUnlimited`
// (a real dvar, default 0) only gets set to 1 in a couple of specific Campaign missions
// (`dubai_code.gsc`/`intro_code.gsc` per Plutonium's public GSC dump), not universally,
// meaning Survival and most Campaign missions genuinely have a limited-by-default
// stamina system we were bypassing entirely. Traced FUN_00643870 (the real
// `player_sprintSpeedScale` consumer) fully -- confirmed it's pure speed-calculation,
// no duration/timer logic at all, so the real native clock lives elsewhere in the Pmove
// chain and wasn't located (see re_notes/known_issues.md). Implemented as our own timer
// layer instead: 4 seconds of continuous sprint to fully deplete, 2 seconds not
// sprinting to fully recover (real MW3 values, confirmed).
//
// OVERRIDE (flagged by user, 2026-07-15): `player_sprintUnlimited` (real dvar, default
// 0) is live-set to 1 by specific mission scripts (`dubai_code.gsc`/`intro_code.gsc`
// confirmed so far via Plutonium's GSC dump, likely others) -- when set, bypass our
// timer entirely and allow genuinely infinite sprint, matching what real keyboard play
// gets in those missions. Checked live every tick via `GetDvarInt`, a raw dvar-value
// getter (NOT `FUN_00498ec0`/`GetDvarString` -- that one blindly returns
// `*(char**)(dvarPtr+0xc)` as a string pointer, which would crash on a boolean/int dvar
// like this one, since +0xc holds a raw 0/1 there, not a valid pointer). Still open:
// the Extreme Conditioning perk doubles sprint duration to 8 seconds -- likely a
// SEPARATE mechanism (probably `perk_sprintMultiplier`, a real dvar found earlier that
// scales `player_sprinttime`, not the same on/off flag as sprintUnlimited), not yet
// investigated -- see task tracking.
constexpr float kSprintMaxStaminaSeconds = 4.0f;
constexpr float kSprintRegenSeconds = 2.0f;
float g_sprintStamina = kSprintMaxStaminaSeconds;
// FIX (live-confirmed bug, same day): originally cleared g_sprintWinded the instant
// g_sprintStamina ticked back above zero -- but regen starts adding immediately every
// frame, so stamina crossed back above zero (a tiny fraction) within a SINGLE frame of
// hitting empty, clearing the lockout almost instantly and letting sprint resume right
// away ("our calls keep firing" -- confirmed live). Fixed with a real, fixed-duration
// cooldown timer, fully decoupled from the continuous stamina float, so hitting empty
// unconditionally blocks sprint for the whole 2 seconds -- we're the ones deciding and
// enforcing this, not something the continuous float model could silently undermine.
float g_sprintCooldownRemaining = 0.0f; // only meaningful while g_sprintWinded is true
bool g_sprintWinded = false;
DWORD g_sprintLastTickMs = 0; // NOT Controller_DeltaTimeSeconds() -- that helper uses a
                              // single process-wide shared static timer (despite its own
                              // doc comment claiming "for this call site"), already
                              // consumed every frame by InjectControllerLookAngles().
                              // Adding a second caller in the same per-frame tick would
                              // starve this one to a near-zero delta every time (whichever
                              // call happens first each frame resets the shared clock the
                              // second call then reads as almost no elapsed time) -- an
                              // independent GetTickCount()-based timer avoids that entirely.

// Raw dvar-value getter -- calls the same Dvar_FindVar-equivalent FUN_00498ec0 itself
// calls internally (FUN_0062abe0, confirmed via FUN_00498ec0's disassembly: name arg
// passed in EDI, not on the stack -- a custom register convention, same class as this
// file's other non-cdecl engine calls), then reads the raw int at dvarPtr+0xc directly.
// Deliberately NOT reusing GetDvarString/FUN_00498ec0 here -- that function blindly
// returns `*(char**)(dvarPtr+0xc)` as a string pointer, which is only valid for actual
// string-type dvars; calling it on a boolean/int dvar like player_sprintUnlimited would
// read the raw 0/1 stored there as if it were a memory address and crash dereferencing
// it as a string.
int GetDvarInt(const char* name)
{
    constexpr uintptr_t kFindDvarFn = 0x0062abe0;
    void* dvarPtr = nullptr;
    __asm {
        push edi
        mov edi, name
        mov eax, kFindDvarFn
        call eax
        mov dvarPtr, eax
        pop edi
    }
    if (!dvarPtr) return 0;
    return *reinterpret_cast<int*>(reinterpret_cast<uintptr_t>(dvarPtr) + 0xc);
}

bool IsSprintActive()
{
    if (GetDvarInt("player_sprintUnlimited") != 0) return g_sprintHeld && g_stance == Stance::Standing;
    return g_sprintHeld && g_stance == Stance::Standing && !g_sprintWinded;
}
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

    // Keep the tick baseline fresh even while bypassed below -- otherwise, if
    // player_sprintUnlimited ever toggles back off later in the same session, the next
    // real tick would compute dt across the WHOLE bypassed interval (potentially minutes)
    // instead of one frame, corrupting the stamina/cooldown math with a huge bogus jump.
    DWORD nowMs = GetTickCount();
    float dt = (g_sprintLastTickMs != 0) ? (nowMs - g_sprintLastTickMs) / 1000.0f : 0.0f;
    g_sprintLastTickMs = nowMs;

    if (GetDvarInt("player_sprintUnlimited") != 0) {
        // Unlimited sprint is live for this mission -- don't drain/regen our own timer
        // at all, so it doesn't sit at some stale mid-depleted value if this dvar is
        // ever toggled back off later in the same session.
        return;
    }

    if (dt > 0.0f) {
        if (g_sprintWinded) {
            // Fixed cooldown, deliberately NOT tied to the continuous stamina float --
            // guarantees sprint stays fully blocked for the whole 2 seconds regardless
            // of anything else, since that float alone already proved unreliable here.
            g_sprintCooldownRemaining -= dt;
            if (g_sprintCooldownRemaining <= 0.0f) {
                g_sprintWinded = false;
                g_sprintStamina = kSprintMaxStaminaSeconds; // full refill once cooldown clears
            }
        } else if (g_sprintHeld && g_stance == Stance::Standing) {
            g_sprintStamina -= dt;
            if (g_sprintStamina <= 0.0f) {
                g_sprintStamina = 0.0f;
                g_sprintWinded = true;
                g_sprintCooldownRemaining = kSprintRegenSeconds;
            }
        } else {
            g_sprintStamina += dt * (kSprintMaxStaminaSeconds / kSprintRegenSeconds);
            if (g_sprintStamina >= kSprintMaxStaminaSeconds) {
                g_sprintStamina = kSprintMaxStaminaSeconds;
            }
        }
    }
}

// Called from Hook_00644ed0 with the live `param_1` (the pml/movement-locals pointer)
// pulled straight off the stack -- see the comment above for how that address was
// confirmed. Forces or clears the real sprint pm_flags bit every Pmove tick to match our
// polled controller state AND our own stamina layer, same as a real held/released key
// with real stamina would. Gated on being upright -- never assert sprint while crouched/
// prone (see InjectControllerSprint).
extern "C" void __cdecl InjectControllerSprintPmFlags(uint32_t pmlPtr)
{
    if (!pmlPtr) return;
    uint32_t ps = *reinterpret_cast<uint32_t*>(pmlPtr);
    if (!ps) return;
    uint32_t* flags = reinterpret_cast<uint32_t*>(ps + 0xc);
    if (IsSprintActive()) {
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
    if (!IsSprintActive()) return;
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

// ---- ADS look-slowdown via live effective FOV (2026-07-16, task #12) --------------
//
// Bug reported after v0.1.0-prealpha: look feels far too sensitive while ADS,
// especially on magnified scopes. Root cause confirmed via RE, NOT a native engine
// gap: our own look injection above uses a single flat kLookDegreesPerSecond
// regardless of ADS/zoom state -- it was never given any zoom awareness at all.
//
// Traced the real mouse pipeline (FUN_0057d7e0, sensitivity/m_pitch/m_yaw/
// cl_mouseAccel) fully and confirmed it has NO ADS/zoom scaling either -- matches
// the user's own research that OG MW3 never exposed (or apparently implemented, for
// mouse) a distinct ADS sensitivity multiplier. So this isn't a matter of "inheriting"
// something we skipped; the scaling genuinely has to be ours.
//
// Explicit design constraint from the user: do NOT touch real rendered FOV (cg_fov et
// al.) to achieve this -- only READ the game's own live effective FOV as a zoom
// SIGNAL, purely to scale our own independent look-rate. This keeps look input on the
// same footing as the rest of this file's philosophy (see the comment above this
// function on why look was deliberately moved OFF the mouse-cvar pipeline in the
// first place: routing through real engine values as a dependency was previously
// flagged as "mouse emulation under the hood," not a control we get to depend on
// wholesale -- reading one piece of state as an input signal to our own curve is a
// narrower, deliberate exception, not a reversion of that call).
//
// FUN_004b0580(playerIndex) confirmed via decompile+disasm to be the real, live
// "compute this frame's effective FOV" function -- blends base FOV (cg_fov/cg_fov1)
// toward the current weapon's real ADS zoom target (via FUN_004d4a70/FUN_004f6b70),
// applies cg_fovScale's transition system (the same one set_lerp_fov/set_pip_fov/
// set_turret_fov drive) and cg_fovNonVehAdd/cg_fovMin. Plain stack-int-arg, ST(0)
// float10 return (confirmed via raw disassembly: PUSH/CALL, no custom register
// convention) -- callable directly as an ordinary function pointer, no inline asm
// needed, unlike this file's other non-cdecl engine calls. Read-only: this frame's
// effective FOV is a pure query, no observed side effects in its disassembly.
//
// cg_fov itself (confirmed via reference scan: only ever written once, at its own
// registration) never changes during ADS -- it stays the user's hipfire base value,
// making it the correct "no zoom" baseline to compare the live effective FOV against.
namespace {
using GetEffectiveFovFn = double(__cdecl*)(int playerIndex);
GetEffectiveFovFn const GetEffectiveFov = reinterpret_cast<GetEffectiveFovFn>(0x004b0580);

// Raw dvar-value getter, float variant -- same Dvar_FindVar-equivalent as GetDvarInt
// above, reading dvarPtr+0xc as a float instead of an int. Needed for cg_fov (a real
// float-type dvar, per its own registration: FUN_004f9cc0("cg_fov", ...)).
float GetDvarFloat(const char* name)
{
    constexpr uintptr_t kFindDvarFn = 0x0062abe0;
    void* dvarPtr = nullptr;
    __asm {
        push edi
        mov edi, name
        mov eax, kFindDvarFn
        call eax
        mov dvarPtr, eax
        pop edi
    }
    if (!dvarPtr) return 0.0f;
    return *reinterpret_cast<float*>(reinterpret_cast<uintptr_t>(dvarPtr) + 0xc);
}

// How strongly the ADS look-slowdown applies: 0.0 = off (flat rate regardless of
// zoom), 1.0 = fully proportional to the live FOV ratio (closest to real console
// feel). Hardcoded at full strength for now -- task #14's config file is where this
// becomes a real user-facing slider, not a constant here.
constexpr float kAdsSlowdownStrength = 1.0f;

// Computes the ADS look-rate scale factor for this frame: 1.0 when not aiming (or
// strength is 0), otherwise the live effective-FOV/hipfire-FOV ratio (< 1.0 when
// zoomed in), blended toward 1.0 by (1 - kAdsSlowdownStrength). Weapon-agnostic by
// construction -- works for every scope/attachment, including future DLC weapons,
// since it reads the game's own already-computed zoom state rather than classifying
// weapon/attachment IDs ourselves.
float GetAdsLookRateScale()
{
    if (!g_adsHeld || kAdsSlowdownStrength <= 0.0f) return 1.0f;

    float baseFov = GetDvarFloat("cg_fov");
    if (baseFov <= 0.0f) return 1.0f;

    float effectiveFov = static_cast<float>(GetEffectiveFov(kLocalClientIndex));
    if (effectiveFov <= 0.0f) return 1.0f;

    float ratio = effectiveFov / baseFov;
    return 1.0f - kAdsSlowdownStrength * (1.0f - ratio);
}
} // namespace

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

    float rate = kLookDegreesPerSecond * GetAdsLookRateScale();
    *kYawAccum -= rx * rate * dt;
    *kPitchAccum -= ry * rate * dt;
}

// ---- Investigation record: Cbuf_AddText / Cmd_ExecuteString exist, but aren't the
// mechanism for weapnext/togglemenu (2026-07-15) -----------------------------------
//
// Found FUN_00457c90 (a real, confirmed Cbuf_AddText -- lock-protected per-client
// text-buffer append, found via the classic "search for a hardcoded screenshot command
// string" CoD-RE anchor technique) and FUN_00605f60/FUN_004d6960 (the real
// Cbuf_Execute -> Cmd_ExecuteString pair: tokenizes each buffered line and walks a
// linked list at DAT_017507d8, nodes shaped {next, namePtr, callbackPtr}, doing a
// case-insensitive name match before calling the matched callback directly). Both
// mechanisms check out structurally and were confirmed live (append/drain telemetry
// all correct), but calling them with "weapnext\n"/"togglemenu\n"/"screenshot\n" had no
// visible effect. A one-time live dump of the full registered-command list (132
// entries) proved why: none of those three strings are registered there at all -- the
// list skews almost entirely toward UI/profile/social/debug commands, essentially no
// core gameplay verbs. This is genuinely the wrong mechanism for these buttons; see
// the Start/pause-menu section below for what the real one turned out to be (ESCAPE is
// hardcoded directly in the key-event handler, bypassing this dispatcher entirely).
// weapnext is still unimplemented -- almost certainly needs the same bind-index/
// FUN_00438710 technique already proven for ADS/Reload, not a text command. See
// re_notes/known_issues.md issue #2 for the full trace.

namespace {
constexpr unsigned short kXI_START = 0x0010;
bool g_startHeld = false;
} // namespace

// ---- Y -> weapnext, SOLVED (2026-07-15) -----------------------------------------
//
// FOUND: dumped every command genuinely registered in the real Cmd_ExecuteString linked
// list (DAT_017507d8, 132 entries) live -- "weapnext", "togglemenu", and "screenshot" are
// ALL absent, confirming this engine resolves core gameplay actions through a different
// mechanism entirely (see the Start/pause-menu writeup below for the ESCAPE-hardcoded
// precedent that pointed this way).
//
// A first attempt tried reusing ADS/Reload's technique (compute weapnext's index in the
// bind-name string table, feed it as a FUN_00438710 case number directly) -- this was
// WRONG (confirmed live: it turned out to be +back's movement kbutton, see the Back
// section above) because the bind-name table and FUN_00438710's switch aren't the same
// numbering at all. The reliable fix: live-read FUN_00541020's own raw-keycode dispatch
// table (DAT_00a98e4c) for weapnext's REAL bound keys ('1'=0x31, '2'=0x32, per
// players2/config.cfg) -- the exact lookup the game itself performs on a real keypress.
// Confirmed formula from FUN_00541020's disassembly (EBP = playerIndex*0xd28, collapsing
// to 0 for SP's player 0): `value = *(int32_t*)(0xA98E4C + keyCode*12)`. Both '1' and '2'
// read back the identical value **66** (0x42) live -- makes sense, both keys bind to the
// same command, so they resolve to the same internal dispatch ID.
//
// FUN_00438710's case 0x42 (=66) calls `FUN_004a5f70(playerIndex, 1)`, paired with case
// 0x46 calling `FUN_004a5f70(playerIndex, 0)` -- a clean next/prev-direction pair, unlike
// ADS/Reload's down/up kbutton pairs (this is a genuine one-shot call, no held state).
// Decompiling confirmed it: FUN_004a5f70 calls FUN_0057a670(playerIndex, direction, 0, 0),
// which does modulo-15 weapon-inventory-slot cycling stepped by `direction` and ends with
// FUN_0042d6b0(playerIndex, weaponIndex, ...) -- a real weapon-SET call. This is
// unambiguously weapnext/weapprev, not a guess.
namespace {
using WeaponNextFn = void(__cdecl*)(int playerIndex, int direction);
WeaponNextFn const WeaponNext = reinterpret_cast<WeaponNextFn>(0x004a5f70);
constexpr unsigned short kXI_Y = 0x8000;
bool g_yHeld = false;
} // namespace

// ---- Survival ready-up (hold Y ~1s): TEMPORARY keypress-synthesis workaround --------
//
// F5/"skip" (the real key that triggers Survival's between-wave ready-up) has no
// locatable native dispatch after an extensive search -- see re_notes/known_issues.md
// for the full trail (real +gostand kbutton call: wrong system; real togglecrouch/
// FUN_0057d2c0 call: inert no-op; a mode-2 variant of that same call: confirmed to be a
// genuine, unrelated toggle-prone command that left the player stuck prone live; GSC
// notifyonplayercommand/VM_Notify: real but requires live GSC-VM-stack manipulation).
//
// EXPLICIT, NARROWLY-SCOPED EXCEPTION (user-approved 2026-07-15): synthesize a real F5
// keydown/keyup via PostMessage at the game's own window, ONLY for this one case,
// ONLY while in Survival (IsInSurvivalMode() gate), as a temporary workaround until the
// real native call is found -- at which point this gets replaced. This is the sole
// deliberate departure from the project's "no OS-level input emulation" rule; every
// other button in this file drives the engine's real internal state directly. Safe by
// construction even without a "is the ready-up wait specifically active" check (which
// we don't have): IW5 has no DirectInput import at all (confirmed in CLAUDE.md's own
// findings), so keyboard input is real WM_KEYDOWN/WM_KEYUP messages -- a synthetic F5
// outside the one context it matters is simply ignored by the game itself, the same as
// a real, misplaced F5 press would be.
namespace {
using GetDvarStringFn = const char*(__cdecl*)(const char*);
GetDvarStringFn const GetDvarString = reinterpret_cast<GetDvarStringFn>(0x00498ec0);
extern "C" HWND GetGameWindow(); // defined in d3d9_hook.cpp
// kReadyUpHoldThresholdMs is now declared near kProneHoldThresholdMs, top of file --
// shared with Interact's hold-to-interact gate.
// The between-wave break is live gameplay, not a frozen/weapons-disabled wait (unlike
// the OTHER, wrong "+gostand" system's freezecontrols wait tried earlier) -- weapons
// stay usable so you can move/shoot/shop freely. That means firing weapnext on Y's
// PRESS edge unconditionally would ALSO switch weapons on every ready-up hold, an
// unwanted side effect. Fixed by deferring weapnext to Y's RELEASE, firing it as long as
// the ready-up threshold was never reached -- so a slightly slow/held tap that isn't
// actually a ready-up attempt still does something, rather than being silently eaten.
DWORD g_yPressStartMs = 0;
bool g_yReadyUpFired = false; // debounces per physical Y hold -- only fires once, even
                              // if held well past the threshold

bool IsInSurvivalMode()
{
    const char* mapName = GetDvarString("mapname");
    if (!mapName) return false;
    return _strnicmp(mapName, "so_survival_", 12) == 0; // matches FUN_00526b30's own check
}

void SendSyntheticF5()
{
    HWND hwnd = GetGameWindow();
    if (!hwnd) return;
    // lParam bit 24 (extended-key flag) doesn't apply to F5; repeat count 1, scan code
    // left 0 -- the game reads the virtual-key (wParam), not the scan code, same as
    // every other key this project has traced through FUN_00541020's dispatch.
    PostMessageA(hwnd, WM_KEYDOWN, VK_F5, 0x00000001);
    PostMessageA(hwnd, WM_KEYUP, VK_F5, 0xC0000001);
}
} // namespace

extern "C" void __cdecl InjectControllerWeaponNext()
{
    unsigned short buttons;
    unsigned char leftTrigger, rightTrigger;
    if (!Controller_GetRawButtonsAndTriggers(buttons, leftTrigger, rightTrigger)) return;

    bool held = (buttons & kXI_Y) != 0;
    if (held && !g_yHeld) {
        g_yPressStartMs = GetTickCount();
        g_yReadyUpFired = false;
    }
    if (held && !g_yReadyUpFired && (GetTickCount() - g_yPressStartMs) >= kReadyUpHoldThresholdMs) {
        g_yReadyUpFired = true;
        if (IsInSurvivalMode()) {
            SendSyntheticF5();
        }
    }
    if (!held && g_yHeld && !g_yReadyUpFired) {
        // Falling edge, and the ready-up threshold was never reached this press --
        // switch weapons. Covers both a quick tap AND a slower-but-not-quite-1s hold,
        // so an attempted ready-up that didn't quite reach the threshold still does
        // something instead of being silently eaten.
        WeaponNext(kLocalClientIndex, 1);
    }
    g_yHeld = held;
}
// See re_notes/known_issues.md issue #2 for the full trace.

// ---- Start -> real pause-menu toggle, via FUN_00541020's hardcoded ESC path -----
//
// FOUND 2026-07-15: "togglemenu" isn't a registered command at all (confirmed via the
// live Cmd list dump above) -- ESCAPE is hardcoded directly in the real key-event
// handler, FUN_00541020, completely bypassing the generic command dispatcher. Traced
// via its disassembly (not the decompile, which mis-detected the parameter count --
// FUN_0054b9f0 calls it with 4 args, Ghidra only inferred 3):
//
//   gate = *(uint32_t*)(0x00B36210 + playerIndex*0x188)   // same gate bit our own
//                                                          // buy-station fix touches
//   state = *(int32_t*)(0x00B36218 + playerIndex*0x188)   // per-player game-state
//   if (gate & 0x10) {              // a menu is currently active
//       FUN_004d9850(playerIndex, 0x1b, isDown);  // forward ESC to it -- this IS the
//                                                   // real "close current menu" action
//   } else if (state == 1 || state == 2) {          // normal gameplay, no menu open
//       FUN_004d6620(playerIndex);                   // opens the pause menu
//   }
//   // any other state (loading, cutscene, etc.) -- real engine does nothing; so do we
//
// For SP, playerIndex is always 0, so both reads collapse to flat addresses (no stride
// math needed). Both callback signatures confirmed via the real call sites' disasm
// (0x0054126e-73 for FUN_004d6620, 0x00541281-89 for FUN_004d9850): plain __cdecl,
// integer args pushed right-to-left, caller cleans the stack -- same easy pattern as
// Cbuf_AddText, no register-passed weirdness.
namespace {
using OpenPauseMenuFn = void(__cdecl*)(int playerIndex);
OpenPauseMenuFn const OpenPauseMenu = reinterpret_cast<OpenPauseMenuFn>(0x004d6620);
constexpr uintptr_t kPlayerStateAddr = 0x00B36218;

// ---- Real unpause path, found 2026-07-15 via decompiling FUN_004396d0 fully ---------
//
// FUN_004396d0(playerIndex, mode) is the same function we already call for "open" (mode
// 2 -- sets cl_paused, opens the "pausedmenu" UI). Its full switch has a mode 0 case too:
//   case 0: FUN_0053ada0(playerIndex, 0xffffffef); thunk_FUN_0057e710(playerIndex);
//           FUN_005396b0("cl_paused", 0);   // <-- clears cl_paused: this IS resume/unpause
//           FUN_004a1280(0); FUN_004ae120(&DAT_01c00458); return 1;
// This is a genuine, real "resume gameplay" call, not a guess -- confirmed by direct
// contrast with case 2 (which sets cl_paused non-zero and opens pausedmenu). We track our
// own g_paused bool (set on the same physical Start press that opened/closed it) rather
// than re-reading engine state, since we're the only thing calling this function from
// our own hook right now.
using SetMenuStateFn = void(__cdecl*)(int playerIndex, int mode);
SetMenuStateFn const SetMenuState = reinterpret_cast<SetMenuStateFn>(0x004396d0);
constexpr int kMenuStateUnpause = 0;
constexpr int kMenuStatePausedMenu = 2;
bool g_paused = false;
} // namespace

extern "C" void __cdecl InjectControllerPauseMenu()
{
    unsigned short buttons;
    unsigned char leftTrigger, rightTrigger;
    if (!Controller_GetRawButtonsAndTriggers(buttons, leftTrigger, rightTrigger)) return;

    bool held = (buttons & kXI_START) != 0;
    if (held && !g_startHeld) {
        char buf[128];
        if (!g_paused) {
            int32_t state = *reinterpret_cast<volatile int32_t*>(kPlayerStateAddr);
            sprintf_s(buf, "[pause-diag] Start pressed (opening): state=%d", state);
            LogFromController(buf);
            // Both live-confirmed real gameplay states (2026-07-15): normal SP/Survival
            // gameplay reports state 6; state 1/2 kept as a fallback for whatever other
            // context might report it (menu-transition edge cases, not yet hit live).
            if (state == 1 || state == 2) {
                OpenPauseMenu(kLocalClientIndex);
            } else {
                SetMenuState(kLocalClientIndex, kMenuStatePausedMenu);
            }
            g_paused = true;
        } else {
            LogFromController("[pause-diag] Start pressed (closing): calling SetMenuState(0, unpause)");
            SetMenuState(kLocalClientIndex, kMenuStateUnpause);
            g_paused = false;
        }
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

// ---- D-pad -> +actionslot 1-4, found via the live raw-keycode dispatch table
// (2026-07-15) --------------------------------------------------------------------
//
// Applied the SAME reliable technique that solved weapnext (never trust a bind-name-
// table index as a FUN_00438710 case number -- see the Back regression above): live-
// read FUN_00541020's real raw-keycode table (DAT_00a98e4c) for the actual keys bound
// to +actionslot 1-4 per players2/config.cfg (N=slot1, 3=slot3, 4=slot4, 5=slot2).
// Formula confirmed: value = *(int32_t*)(0xA98E4C + keyCode*12) for SP (playerIndex 0).
// Letter keys use LOWERCASE ASCII in this table (matching the earlier Reload memdiff
// finding, 'r' not 'R') -- uppercase 'N' read back 0 (unhandled), lowercase 'n' read
// back the expected 15, fitting the exact same arithmetic pattern as the other three
// (17/19/21 for slots 2/3/4, each 2 apart) once corrected.
//
// FUN_00438710's decompile shows a clean, uniform down/up case pattern for all four:
//   case 0xf/0x10  (slot1, 'n'): FUN_00410ad0(playerIndex,0) / FUN_0044ec40(playerIndex)
//   case 0x11/0x12 (slot2, '5'): FUN_00410ad0(playerIndex,1) / FUN_0044ec40(playerIndex)
//   case 0x13/0x14 (slot3, '3'): FUN_00410ad0(playerIndex,2) / FUN_0044ec40(playerIndex)
//   case 0x15/0x16 (slot4, '4'): FUN_00410ad0(playerIndex,3) / FUN_0044ec40(playerIndex)
// Both are plain, simple __cdecl (no special register convention needed, unlike ADS/
// Reload's KeyDown/KeyUp). Decompiling FUN_00410ad0 shows the real slot behavior is
// DATA-DRIVEN: it reads DAT_00985064[slotIndex] (a runtime "what's assigned to this
// slot" type) and either switches weapon (calling the same FUN_0057a670 weapon-cycle
// function weapnext uses, or a direct FUN_0042d6b0 weapon-set), calls FUN_0057a930
// (a distinct action -- likely equipment/killstreak use), or ORs a flag
// (DAT_009a19ec |= 0x40000, likely an NVG-style persistent toggle) -- matching the
// user's own expectation that D-pad maps to killstreaks/attachments, which vary by
// loadout/context rather than being one fixed action per direction.
// FUN_0044ec40(playerIndex) is nearly a no-op (just calls the same FUN_00416040 guard
// check FUN_00410ad0 itself starts with) -- called anyway on release for correctness,
// matching the real dispatcher's own down/up pairing.
namespace {
using ActionSlotDownFn = void(__cdecl*)(int playerIndex, int slotIndex);
using ActionSlotUpFn = void(__cdecl*)(int playerIndex);
ActionSlotDownFn const ActionSlotDown = reinterpret_cast<ActionSlotDownFn>(0x00410ad0);
ActionSlotUpFn const ActionSlotUp = reinterpret_cast<ActionSlotUpFn>(0x0044ec40);
constexpr unsigned short kXI_DPAD_UP = 0x0001;
constexpr unsigned short kXI_DPAD_DOWN = 0x0002;
constexpr unsigned short kXI_DPAD_LEFT = 0x0004;
constexpr unsigned short kXI_DPAD_RIGHT = 0x0008;
// Mapping per the user's own reference Steam Controller config (re_notes/iw5sp.md):
// D-Pad Up = actionslot1(0), Right = actionslot2(1), Down = actionslot3(2), Left = actionslot4(3)
bool g_dpadHeld[4] = { false, false, false, false };

// Per-slot action TYPE table FUN_00410ad0 itself reads (confirmed via decompile,
// 2026-07-15 later session): int[4] at 0x00985064, one entry per actionslot -- 1 =
// direct weapon-set, 2 = calls FUN_0057a930 (killstreak/equipment "wield" select, itself
// a weapon-inventory scan+set, not a stance call), 3 = ORs an NVG-style persistent flag.
// Not read by our own code (see the stuck-prone note below for why an earlier attempt
// that did read this was reverted).
} // namespace

// GAME-BREAKING BUG, STILL OPEN (live-reported by user after the v0.1.0-prealpha
// release): using the Predator missile killstreak while prone in the first mission left
// the player permanently stuck prone -- not recoverable even via real keyboard input.
// Static RE ruled out the obvious suspect: FUN_0057d2c0 (the function that caused the
// earlier, similarly-unrecoverable stuck-prone regression during the F5/ready-up hunt)
// has exactly one caller in the whole binary (FUN_00438710's cases 0x48/0x49, confirmed
// via FindCallers.java) and neither is invoked anywhere in this file -- so this is a
// different bug, not a recurrence of that one, despite the identical symptom.
//
// Working theory, NOT yet confirmed live: InjectControllerButtons (above) unconditionally
// re-asserts g_stance's usercmd bit (0x100/0x200) every single frame regardless of what
// else the game is doing -- the same general failure pattern as the earlier buy-
// station+pause bug (known_issues.md issue #1: forcing a bit continuously, ignoring
// context, breaks a native subsystem's own state transition). Predator missile is used
// like a "weapon" (select via D-pad, then fire) that puts the local player into a
// scripted missile-cam sequence; if the player is prone when that sequence starts, our
// hook may keep forcing the prone bit through it and through the exit transition.
//
// A first attempt fixed this by auto-standing before a killstreak-type D-pad select
// (mirroring Sprint's own "auto-stand from crouch/prone first" precedent above) --
// REJECTED by the user: real console MW3 does NOT force you to stand to use a
// killstreak while prone, so that "fix" would have broken behavior parity with the
// original game to paper over a bug, which fails this project's console-parity bar.
// Reverted. The real fix needs to identify what's actually different about the missile-
// cam's entry/exit sequence when the player is prone, via live diagnostic logging
// (g_stance transitions + the real native stance byte, &DAT_00b363b0 + player*0xbe5c,
// across a real repro), not a change to normal stance behavior -- log-only
// instrumentation for exactly this is now in place (LogStanceDiag, defined above near
// InjectControllerButtons; also logs a D-pad-press event below). See task #10.
extern "C" void __cdecl InjectControllerDpad()
{
    unsigned short buttons;
    unsigned char leftTrigger, rightTrigger;
    if (!Controller_GetRawButtonsAndTriggers(buttons, leftTrigger, rightTrigger)) return;

    struct { unsigned short bit; int slot; } kDpad[4] = {
        { kXI_DPAD_UP, 0 }, { kXI_DPAD_RIGHT, 1 }, { kXI_DPAD_DOWN, 2 }, { kXI_DPAD_LEFT, 3 }
    };
    for (int i = 0; i < 4; ++i) {
        bool held = (buttons & kDpad[i].bit) != 0;
        if (held != g_dpadHeld[i]) {
            if (held) {
                char tag[32];
                sprintf_s(tag, "dpad-press slot=%d", kDpad[i].slot);
                LogStanceDiag(tag);
                ActionSlotDown(kLocalClientIndex, kDpad[i].slot);
            } else {
                LogStanceDiag("dpad-release");
                ActionSlotUp(kLocalClientIndex);
            }
            g_dpadHeld[i] = held;
        }
    }
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
    InjectControllerDpad();

    // Also called from InjectMenuInputTick (the WndProc hook, see below) -- kept here
    // too purely for redundancy/robustness. Calling it from both places is safe/
    // idempotent: g_startHeld debounces per real button edge regardless of which hook
    // happens to observe it first in a given frame.
    InjectControllerPauseMenu();
}

// ---- Menu input tick -- driven by a WndProc subclass hook, NOT this file's gameplay tick
//
// FOUND 2026-07-15: a heartbeat diagnostic confirmed InjectAllControllerInput (this
// function, called from FUN_0057de60) completely stops firing while genuinely paused --
// it lives inside the per-frame GAMEPLAY SIMULATION pipeline, and pausing halts
// simulation by design. That's irrelevant for movement/look/buttons (meaningless while
// paused anyway), but it meant Start's second press could never be detected: pausing the
// game also paused the only code path checking for the unpause press.
//
// FIRST FIX ATTEMPT, CONFIRMED DEAD: drove this from a real IDirect3DDevice9::Present
// hook instead (installed cleanly, MH_OK, confirmed targeting the real HAL device) -- but
// a fire-counter diagnostic proved its detour never fired even ONCE during an entire
// normal, unpaused play session, ruling out a pause-specific timing issue and pointing at
// an external hook on the same vtable slot (Steam Overlay is the prime suspect) stomping
// ours. Abandoned rather than fought.
//
// REAL FIX: d3d9_hook.cpp now subclasses the game's own window procedure (WndProc) once
// the real device's window handle is known, plus a SetTimer-driven ~60Hz WM_TIMER so this
// keeps ticking even during totally idle periods with no other window messages. Windows
// keeps pumping window messages even while the game's own simulation is paused (proven by
// vanilla keyboard ESC still being able to unpause today) -- and unlike a D3D9 vtable,
// nothing else has a reason to silently take over our own subclassed window procedure.
extern "C" void __cdecl InjectMenuInputTick()
{
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
