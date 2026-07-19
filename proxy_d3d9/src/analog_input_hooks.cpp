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
#include <intrin.h>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include "../third_party/minhook/include/MinHook.h"
#include "controller_input.h"
#include "mod_config.h"
#include "rumble.h"

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

// All raw XInput button-bit/trigger constants live here in one place (previously
// scattered redeclarations across each Inject* section) now that task #15's button-
// layout remapping needs a single IsPhysicalHeld() usable from every hook function,
// regardless of where in the file it's defined. Declared this early (before
// InjectControllerMovement, the file's first hook function) so every later function
// can use it without a forward-declaration.
namespace {
constexpr unsigned short kXI_DPAD_UP = 0x0001;
constexpr unsigned short kXI_DPAD_DOWN = 0x0002;
constexpr unsigned short kXI_DPAD_LEFT = 0x0004;
constexpr unsigned short kXI_DPAD_RIGHT = 0x0008;
constexpr unsigned short kXI_START = 0x0010;
constexpr unsigned short kXI_BACK = 0x0020;
constexpr unsigned short kXI_LEFT_THUMB = 0x0040;
constexpr unsigned short kXI_RIGHT_THUMB = 0x0080;
constexpr unsigned short kXI_LEFT_SHOULDER = 0x0100;
constexpr unsigned short kXI_RIGHT_SHOULDER = 0x0200;
constexpr unsigned short kXI_A = 0x1000;
constexpr unsigned short kXI_B = 0x2000;
constexpr unsigned short kXI_X = 0x4000;
constexpr unsigned short kXI_Y = 0x8000;
constexpr unsigned char kTriggerThresholdFire = 30; // XInput's documented trigger threshold

// SP only ever has player 0. Declared this early so every function in the file
// (including the ToggleStance/GetRealStance helpers right below) can use it without
// a forward-declaration.
constexpr int kLocalClientIndex = 0;

// The real per-player "a menu is currently active" gate bit (see the big writeup
// above InjectControllerMenuBack for how this was found/confirmed). Declared this
// early, same rationale as everything else on this page: InjectControllerButtons'
// Jump bit (task #22) needs it too, and that function is defined well before the
// menu-back code further down the file.
constexpr uintptr_t kMenuActiveGateAddr = 0x00B36210;
constexpr uint32_t kMenuActiveGateBit = 0x10u;

bool IsMenuActive()
{
    uint32_t gate = *reinterpret_cast<volatile uint32_t*>(kMenuActiveGateAddr);
    return (gate & kMenuActiveGateBit) != 0;
}

// task #15: resolves a logical action's PhysicalInput (from g_buttonMap, itself
// resolved from the active ButtonLayout + FlipTriggers) down to an actual XInput
// button-bit/trigger check. Every Inject* function below should read its physical
// input through this + g_buttonMap.<action> rather than a hardcoded kXI_* constant,
// so the whole mod stays consistent under any button layout.
bool IsPhysicalHeld(PhysicalInput p, unsigned short buttons, unsigned char leftTrigger, unsigned char rightTrigger)
{
    switch (p) {
        case PhysicalInput::RT: return rightTrigger >= kTriggerThresholdFire;
        case PhysicalInput::LT: return leftTrigger >= kTriggerThresholdFire;
        case PhysicalInput::RB: return (buttons & kXI_RIGHT_SHOULDER) != 0;
        case PhysicalInput::LB: return (buttons & kXI_LEFT_SHOULDER) != 0;
        case PhysicalInput::X: return (buttons & kXI_X) != 0;
        case PhysicalInput::Y: return (buttons & kXI_Y) != 0;
        case PhysicalInput::A: return (buttons & kXI_A) != 0;
        case PhysicalInput::B: return (buttons & kXI_B) != 0;
        case PhysicalInput::LS: return (buttons & kXI_LEFT_THUMB) != 0;
        case PhysicalInput::RS: return (buttons & kXI_RIGHT_THUMB) != 0;
        case PhysicalInput::Start: return (buttons & kXI_START) != 0;
        case PhysicalInput::Back: return (buttons & kXI_BACK) != 0;
    }
    return false;
}

// ---- Stick layout routing (task #15) ----------------------------------------------
//
// Default: left stick = move (fwd/back, strafe), right stick = look (pitch, turn).
// Southpaw: whole sticks swapped.
// Legacy: only the HORIZONTAL axes swap between the two sticks -- left stick keeps
// forward/back, right stick keeps look up/down, but left-stick-X becomes turn and
// right-stick-X becomes strafe (i.e. left stick handles rotation, right handles
// strafing -- the historical CoD4-era "Legacy" scheme, per user-supplied reconstruction).
// LegacySouthpaw: the two sticks swapped again on top of Legacy.
void RouteStickAxes(float leftX, float leftY, float rightX, float rightY, StickLayout layout,
                     float& moveX, float& moveY, float& lookX, float& lookY)
{
    switch (layout) {
        case StickLayout::Southpaw:
            moveX = rightX; moveY = rightY;
            lookX = leftX;  lookY = leftY;
            break;
        case StickLayout::Legacy:
            moveX = rightX; moveY = leftY;
            lookX = leftX;  lookY = rightY;
            break;
        case StickLayout::LegacySouthpaw:
            moveX = leftX;  moveY = rightY;
            lookX = rightX; lookY = leftY;
            break;
        default: // Default
            moveX = leftX;  moveY = leftY;
            lookX = rightX; lookY = rightY;
            break;
    }
}

// ---- Real togglecrouch/toggleprone -- FUN_0057d2c0 (2026-07-16) -------------------
//
// Found via the SAME technique already proven for weapnext/D-pad: live-read the real
// raw-keycode dispatch table (formula: value = *(int32_t*)(0xA98E4C + keyCode*12))
// for the actual keys bound to togglecrouch/toggleprone (players2/config.cfg: C ->
// togglecrouch, CTRL -> toggleprone). C (0x43) reads case 0x48; the game's internal
// keycode for CTRL (0x9F, NOT Windows' VK_CONTROL=0x11 -- this table uses the
// engine's own Quake-derived key enum, confirmed the hard way during the earlier F5
// hunt) reads case 0x49. Both dispatch to this SAME function, `FUN_0057d2c0(playerIndex,
// mode)` -- confirmed via raw disassembly to be a genuine __fastcall (ECX=playerIndex,
// EDX=mode, no custom register convention needed):
//
//   EAX = playerIndex * 0x230
//   if (byte[EAX + 0xA98CA0] != 0) return;      // guard 1 (unknown gate, e.g. vehicle/menu)
//   if (byte[EAX + 0xA98BC4] != 0) return;      // guard 2 (same class of gate)
//   ECX = &DAT_00B363B0 + playerIndex*0xBE5C
//   current = *(int*)(ECX + 0x1C)
//   *(int*)(ECX + 0x1C) = (current != mode) ? mode : 0;   // genuine toggle
//
// This is a REAL toggle between 0 (standing) and `mode` (1 = crouch, 2 = prone) --
// and it already implements this mod's entire desired stance ladder natively:
// Standing+togglecrouch->Crouched, Crouched+togglecrouch->Standing,
// Crouched+toggleprone->Prone (2!=1), Prone+toggleprone->Standing (2==2),
// Prone+togglecrouch->Crouched (2!=1). No separate state machine needed on our side
// at all -- READ this same field live for "what's the current real stance" instead
// of tracking our own parallel copy, and call this function on tap/hold transitions
// instead of computing+asserting the ladder ourselves.
//
// This replaces the previous design (own g_stance enum + per-frame raw usercmd-bit
// forcing), which was live-suspected of FIGHTING this exact real toggle state: the
// user found real keyboard Ctrl could recover a stuck-prone Campaign session that
// neither our B button nor Sprint could -- strong evidence our own bit-forcing was
// overriding/conflicting with this authoritative field rather than reading it.
using ToggleStanceFn = void(__fastcall*)(int playerIndex, unsigned int mode);
ToggleStanceFn const ToggleStance = reinterpret_cast<ToggleStanceFn>(0x0057d2c0);
constexpr uintptr_t kRealStanceFieldAddr = 0xB363CC; // player 0 (SP-only, stride*0 offset)

int GetRealStance()
{
    return *reinterpret_cast<volatile int*>(kRealStanceFieldAddr);
}

// Brings the real stance back to Standing (0) from whatever it currently is, by
// calling ToggleStance with the mode that EQUALS the current value (per the toggle
// logic above, current==mode always resolves to 0) -- a no-op if already standing.
void ForceStandingViaRealToggle()
{
    int current = GetRealStance();
    if (current == 1 || current == 2) {
        ToggleStance(kLocalClientIndex, static_cast<unsigned int>(current));
    }
}
} // namespace

// ---- Movement: move-stick -> usercmd_t.forwardmove(+0x1c) / .rightmove(+0x1d) ----
//
// "Move-stick" rather than a hardcoded "left stick" since task #15's Stick Layout
// (g_modConfig.stickLayout) can route movement off either physical stick -- both are
// read every frame and RouteStickAxes (defined above) picks which feeds move vs. look
// per the active layout. Under the default layout this is exactly the original left-
// stick-only behavior.
extern "C" void __cdecl InjectControllerMovement(unsigned char* cmd)
{
    if (!cmd) return;
    float leftX, leftY, rightX, rightY;
    if (!Controller_GetLeftStick(leftX, leftY)) return;
    if (!Controller_GetRightStick(rightX, rightY)) return;

    float moveX, moveY, lookX, lookY;
    RouteStickAxes(leftX, leftY, rightX, rightY, g_modConfig.stickLayout, moveX, moveY, lookX, lookY);
    if (moveX == 0.0f && moveY == 0.0f) return;

    int8_t curForward = static_cast<int8_t>(cmd[0x1c]);
    int8_t curRight = static_cast<int8_t>(cmd[0x1d]);

    // Full stick deflection == full digital-key-equivalent speed (matches how the
    // keyboard path also just produces +-127 for a held key -- the engine's own
    // movement/physics code treats forwardmove/rightmove as a continuous fraction of
    // max speed, so this still gives real analog speed control, not just on/off).
    // Confirmed correct as-is (no inversion) via real-hardware playtest, 2026-07-14 --
    // only look (right stick) was reported inverted, not movement.
    int addForward = static_cast<int>(moveY * 127.0f);
    int addRight = static_cast<int>(moveX * 127.0f);

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
//        2026-07-14): a 3-state ladder (Standing / Crouched / Prone). Originally
//        tracked via our own g_stance enum + per-frame raw usercmd-bit forcing;
//        replaced 2026-07-16 (see the ToggleStance/GetRealStance comment further up)
//        with calls to the REAL togglecrouch/toggleprone toggle (FUN_0057d2c0) on
//        tap/hold, and a live read of the real stance field for the per-frame bit
//        assertion Pmove still needs -- the ladder below is implemented natively by
//        that real toggle's own semantics, not computed by us:
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
//   Left stick click (L3) -> Sprint / Hold Breath (context-sensitive, same as real
//        console/keyboard) -- NOT handled here, see InjectControllerSprint.
//        Sprint CONFIRMED WORKING live (2026-07-14, real kbutton migration
//        2026-07-19) -- drives the real +sprint kbutton_t (0xA98CCC) via
//        CallKbuttonDown/CallKbuttonUp, same technique as ADS/Reload/Fire (superseded
//        the original raw pm_flags-bit-forcing approach). Hold Breath (task #24,
//        2026-07-19) drives a second real kbutton_t (0xA98C04) the same way, gated on
//        ADS instead of stance -- not yet live-tested. See re_notes/iw5sp.md and
//        re_notes/known_issues.md issue #6 for the full investigation of both.
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
//
// kXI_* constants, IsPhysicalHeld(), and RouteStickAxes() (task #15's button/stick-
// layout remapping) now live near the top of the file, before InjectControllerMovement
// -- moved there so every Inject* function, including the earliest one, can use them.
namespace {
// Hold-vs-tap thresholds (B/Interact/ready-up), sensitivity, ADS slowdown strength,
// and sprint stamina/regen all now come from g_modConfig (task #14, mw3ncp_config.ini)
// instead of being hardcoded here -- see mod_config.h for the full list and defaults.

DWORD g_crouchButtonPressStartMs = 0;
bool g_crouchButtonWasHeld = false;
bool g_holdActionConsumed = false; // true once this press has already fired its hold action

// Tracks, for B's CURRENT physical press (since its own last rising edge), whether a
// menu was ever active at any point during it. Maintained entirely by
// InjectControllerMenuBack (2026-07-16), since that function -- unlike
// InjectControllerButtons -- keeps running via the always-on WndProc/timer tick even
// while genuinely paused, so it's the only reliable continuous observer of B's state
// across a pause. B is the SAME physical button used for both "close menu" and
// crouch/prone; InjectControllerButtons consults this flag (not a one-shot resync tied
// to a global menu-active transition, which would misfire if some OTHER menu happened
// to open/close while B was already down for an unrelated gameplay press) to decide
// whether THIS press is allowed to fire crouch (tap) or prone (hold) at all. Reset on
// B's own rising edge, so a later, genuinely menu-free press is unaffected.
bool g_currentBPressTouchedMenu = false;

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
// Logs the REAL native stance field (GetRealStance(), &DAT_00B363B0+0x1C -- see the
// ToggleStance/GetRealStance comment above) on every stance transition, every D-pad
// press, and a ~500ms heartbeat. Originally this watched &DAT_00b363b0+0x0 (the
// struct's base address) rather than +0x1c, the actual field FUN_0057d2c0 reads/
// writes -- fixed now that the real field's exact offset is confirmed via
// disassembly, rather than guessed at.
DWORD g_lastStanceDiagLogMs = 0;

void LogStanceDiag(const char* tag)
{
    char buf[160];
    sprintf_s(buf, "[stance-diag] %s realStance=%d t=%lu",
              tag, GetRealStance(), GetTickCount());
    LogFromController(buf);
}

// ---- Missile-guidance / third-analog-channel diagnostic (2026-07-18, task #30) -----
//
// SUPERSEDED for Predator Missile specifically (2026-07-19): see
// Hook_MissileGuidanceDispatch further below for the real mechanism, found via a full
// GSC re-read plus a whole-binary scan for the ACTUAL bit `controlslinkto` sets
// (clientStruct+0xc bit 0x80000 -- a different address than the +0x1094 flag this
// comment block is about, despite the same bit VALUE). The `+0x1094`/`cmd+0x3e`/`0x3f`
// theory below was never confirmed to be what Predator Missile guidance uses, and the
// real reader chain found today looks nothing like it. Kept running (harmless,
// change-triggered) since bit 0x800 on this same dword is still an open, SEPARATE lead
// for the "Turbulence" bug -- just no longer believed relevant to missile guidance.
//
// FUN_0057e480's per-frame orchestrator has a control-mode branch, gated on this same
// per-player struct family (base &DAT_00B363B0 + playerIndex*0xBE5C, SAME base
// GetRealStance() reads at +0x1C -- confirmed via fresh disassembly this is literally
// the same struct, just a different field) at offset +0x1094, bit 0x80000: when set,
// the engine redirects real mouse-delta into a THIRD analog-input channel
// (cmd+0x3e/0x3f) instead of normal look -- ORIGINALLY believed to be the root cause of
// the Predator Missile post-fire guidance sequence breaking controller movement, now
// refuted for that specific bug (see above). A whole-binary scalar-operand scan for the
// literal offset 0x1094 found exactly 2 references, BOTH reads -- the real SETTER isn't
// a fixed instruction anywhere (almost certainly a generic/data-driven "set entity
// flag" mechanism, offset passed as a runtime argument), so it can't be found by static
// scanning alone.
//
// Also worth watching: bit 0x800 on this SAME dword separately gates whether ANY
// keyboard/analog movement processing runs at all in FUN_0057d430 (the movement
// summer) -- a real candidate for the still-unresolved "Turbulence" moves-when-
// should-be-frozen bug (issue #27 bug #4), found as a side effect of this
// investigation, not yet chased.
//
// Change-triggered (not a fixed-interval heartbeat) -- only logs when the raw value
// actually changes, so a full playtest session doesn't spam the log file.
constexpr uintptr_t kMissileGuidanceFlagAddr = 0xB374E4; // player 0: 0xB363B0 + 0x1094
unsigned int g_lastMissileGuidanceFlagValue = 0xFFFFFFFF; // sentinel: force first log

void LogMissileGuidanceFlagDiag()
{
    unsigned int current = *reinterpret_cast<volatile unsigned int*>(kMissileGuidanceFlagAddr);
    if (current == g_lastMissileGuidanceFlagValue) return;

    char buf[160];
    sprintf_s(buf, "[missile-guidance-diag] +0x1094=0x%08X (bit0x80000=%d bit0x800=%d) t=%lu",
              current, (current & 0x80000) != 0 ? 1 : 0, (current & 0x800) != 0 ? 1 : 0,
              GetTickCount());
    LogFromController(buf);
    g_lastMissileGuidanceFlagValue = current;
}

// ---- controlslinkto diagnostic hook (2026-07-18, task #30 follow-up) ---------------
//
// The +0x1094 bit theory above is confirmed WRONG for missile guidance -- a GSC deep
// read found the real mechanism is a different builtin entirely, `controlslinkto`,
// called on the missile projectile entity when guidance starts. Its native
// implementation, FUN_005d7f20, was fully decompiled AND independently re-confirmed
// from its true entry point via raw disassembly (not just partial/prior analysis,
// given today's earlier lesson about incompletely-confirmed signatures):
//   MOV EAX,[ESP+4]                    -- single stack arg: an entity HANDLE, not a
//                                          raw pointer
//   (upper 16 bits == 0 fast path) index = handle & 0xFFFF
//   entity = 0x01197AD8 + index*0x270  -- the SAME entity-handle-resolution array
//                                          this project's aim-assist research flagged
//                                          as a parked, uncertain lead (issue #15) --
//                                          now confirmed real and live-used here
//   clientStruct = *(int*)(entity + 0x10c)
//   *(uint*)(clientStruct + 0xc) |= 0x80000   -- SETS THE REAL LINK FLAG
//   *(uint*)(clientStruct + 0x4c) = linkTargetId
// Confirmed plain __cdecl, ONE stack arg, bare RET (caller cleanup) -- a simple,
// safe signature to hook, NOT the same risk class as the rumble dispatchers that
// crashed the game earlier today (those were generic multi-purpose dispatchers
// called with genuinely different real argument counts elsewhere; this is a
// single-purpose builtin implementation with one confirmed call shape).
//
// Log-and-forward only -- calls the real original function completely unchanged,
// then independently re-resolves the SAME entity-handle -> client-struct chain
// (read-only) to log the resulting +0xc value. No behavior change at all; this
// exists purely to observe, during a real Predator Missile playtest, whether this
// fires at the expected moment and what the resulting per-player state looks like --
// the same live-diagnostic approach that already found this whole mechanism.
namespace {
using ControlsLinkToFn = void(__cdecl*)(unsigned int entityHandle);
constexpr uintptr_t kControlsLinkToAddr = 0x005d7f20;
ControlsLinkToFn g_origControlsLinkTo = nullptr;

void __cdecl Hook_ControlsLinkTo(unsigned int entityHandle)
{
    g_origControlsLinkTo(entityHandle);

    char buf[200];
    if ((entityHandle >> 16) == 0) {
        unsigned int index = entityHandle & 0xFFFF;
        uintptr_t entityPtr = 0x01197AD8 + static_cast<uintptr_t>(index) * 0x270;
        uintptr_t clientStructPtr = *reinterpret_cast<volatile uintptr_t*>(entityPtr + 0x10c);
        if (clientStructPtr) {
            unsigned int flagValue = *reinterpret_cast<volatile unsigned int*>(clientStructPtr + 0xc);
            sprintf_s(buf,
                "[controlslinkto-diag] entityHandle=0x%08X entity=0x%08X clientStruct=0x%08X "
                "+0xc=0x%08X (bit0x80000=%d) t=%lu",
                entityHandle, static_cast<unsigned int>(entityPtr),
                static_cast<unsigned int>(clientStructPtr), flagValue,
                (flagValue & 0x80000) != 0 ? 1 : 0, GetTickCount());
        } else {
            sprintf_s(buf, "[controlslinkto-diag] entityHandle=0x%08X entity=0x%08X clientStruct=NULL t=%lu",
                entityHandle, static_cast<unsigned int>(entityPtr), GetTickCount());
        }
    } else {
        sprintf_s(buf, "[controlslinkto-diag] entityHandle=0x%08X (non-fast-path, unresolved) t=%lu",
            entityHandle, GetTickCount());
    }
    LogFromController(buf);
}
} // namespace

// ---- Missile-guidance per-frame angle dispatcher diagnostic (2026-07-19, task #30
// follow-up, GSC-plus-static-analysis pass) -----------------------------------------
//
// Full GSC re-read of 1555.gsc's guidance-phase while-loop (lines 916-937) confirms
// there is NO per-frame input read at the script level at all -- it's a plain
// `while (isdefined(level._id_3C11)) { wait 0.05; <abort checks only> }` poll. Whatever
// steers the missile is 100% native, engaged once by `controlslinkto` and read every
// frame by the engine itself -- this settles the "is it GSC-level or native" question
// definitively in favor of native.
//
// Found the real per-frame READER chain via a whole-binary scan for the literal scalar
// 0x80000 (FindConstantRefs.java) cross-referenced against FUN_005d7f20's own known
// callers/siblings -- FOUR functions test `[reg+0xc] & 0x80000` (the same clientStruct
// bit `controlslinkto` sets); one of them, FUN_004554d0, is the real per-frame
// per-client "process this tick" dispatcher (confirmed via FindCallers.java: its own
// caller is FUN_00644ed0 -- the exact Pmove-tick function this mod's PREVIOUS Sprint
// mechanism used to hook, called `FUN_004554d0(pml, *pml /* clientStruct */,
// frameDeltaMs, pml+1, someByte)`). Raw disassembly of FUN_004554d0 (not just the
// decompile, which obscures the register-passed tail call) confirms: when clientStruct
// +0xc bit 0x80000 is set, it does NOT run its normal look/movement dispatch at all --
// instead it tail-jumps into FUN_006423d0 with ECX=param_4 (pml+4, i.e. pml+0xc/+0x10/
// +0x14 once inside that function) and EAX=clientStruct. FUN_006423d0 reads 3
// sequential floats from pml+0xc/+0x10/+0x14 and angle-wraps (anglemod-style) each one
// into clientStruct+0x10c/+0x110/+0x114 -- a DIFFERENT, more specific target than the
// old `cmd+0x3e`/`0x3f` theory (issue #30's original guess), which this pass REFUTES
// as the relevant mechanism for THIS bug specifically: `cmd+0x3e`/`0x3f` was tied to a
// per-player-struct `+0x1094` bit that's a different address entirely from the
// clientStruct `+0xc` bit `controlslinkto` actually sets (see the diagnostic above).
//
// **Still open, and why this is a diagnostic, not yet a fix**: pml+0xc/+0x10/+0x14
// (the READ side) is a Pmove-locals field, not the real usercmd_t this mod's own look
// hook (`kPitchAccum`/`kYawAccum`, packed into `cmd.angles` by `Hook_0057de60`)
// directly writes to. Whether pml+0xc/+0x10/+0x14 is a live per-frame copy of the real
// cmd angles (in which case our existing look input should already reach the missile
// for free, and the bug is that it's frozen/stale for some OTHER reason while linked)
// or something else entirely wasn't nailed down via static analysis alone in the time
// available -- the copy site wasn't located. Logging both sides side by side during a
// real missile flight is what actually answers this: if pml+0xc/+0x10/+0x14 tracks
// kPitchAccum/kYawAccum in real time, the fix is elsewhere (something upstream isn't
// running while linked); if it's frozen, the fix is writing this mod's own look input
// into pml+0xc/+0x10/+0x14 directly instead of (or in addition to) kPitchAccum/
// kYawAccum while clientStruct+0xc bit 0x80000 is set.
//
// Log-and-forward only, same convention as Hook_ControlsLinkTo above -- calls the real
// original function completely unchanged. Gated on clientStruct+0xc bit 0x80000 so a
// normal (non-guidance) play session logs nothing at all; change-triggered within that
// gate so an actual guidance sequence doesn't spam the log every 16ms either.
namespace {
using MissileGuidanceDispatchFn = void(__cdecl*)(
    void* pmlPtr, void* clientStructPtr, float frameDeltaMs, void* pmlPlusOne, char flagByte);
constexpr uintptr_t kMissileGuidanceDispatchAddr = 0x004554d0;
MissileGuidanceDispatchFn g_origMissileGuidanceDispatch = nullptr;

float g_lastLoggedPmlPitch = 0.0f;
bool g_missileGuidanceDiagHasLogged = false;

void __cdecl Hook_MissileGuidanceDispatch(
    void* pmlPtr, void* clientStructPtr, float frameDeltaMs, void* pmlPlusOne, char flagByte)
{
    if (!clientStructPtr) {
        g_origMissileGuidanceDispatch(pmlPtr, clientStructPtr, frameDeltaMs, pmlPlusOne, flagByte);
        return;
    }

    unsigned int linkFlag = *reinterpret_cast<volatile unsigned int*>(
        reinterpret_cast<uintptr_t>(clientStructPtr) + 0xc);
    bool linked = (linkFlag & 0x80000) != 0;

    if (linked && pmlPtr) {
        float pmlPitch = *reinterpret_cast<volatile float*>(reinterpret_cast<uintptr_t>(pmlPtr) + 0xc);
        float pmlYaw = *reinterpret_cast<volatile float*>(reinterpret_cast<uintptr_t>(pmlPtr) + 0x10);
        float pmlRoll = *reinterpret_cast<volatile float*>(reinterpret_cast<uintptr_t>(pmlPtr) + 0x14);
        float clientAngle0 = *reinterpret_cast<volatile float*>(reinterpret_cast<uintptr_t>(clientStructPtr) + 0x10c);
        float clientAngle1 = *reinterpret_cast<volatile float*>(reinterpret_cast<uintptr_t>(clientStructPtr) + 0x110);
        float clientAngle2 = *reinterpret_cast<volatile float*>(reinterpret_cast<uintptr_t>(clientStructPtr) + 0x114);

        if (!g_missileGuidanceDiagHasLogged || pmlPitch != g_lastLoggedPmlPitch) {
            // Read this mod's own look-accumulator globals directly by their known
            // fixed address (0x00B36408/0x00B3640C, same as kPitchAccum/kYawAccum
            // declared later in this file) rather than depending on declaration
            // order -- this diagnostic sits earlier in the file than that pair.
            float ourPitchAccum = *reinterpret_cast<volatile float*>(0x00B36408);
            float ourYawAccum = *reinterpret_cast<volatile float*>(0x00B3640C);
            char buf[280];
            sprintf_s(buf,
                "[missile-guidance-dispatch-diag] LINKED pml+0xc/0x10/0x14=%.4f/%.4f/%.4f "
                "clientStruct+0x10c/0x110/0x114=%.4f/%.4f/%.4f ourPitchAccum=%.4f ourYawAccum=%.4f t=%lu",
                pmlPitch, pmlYaw, pmlRoll, clientAngle0, clientAngle1, clientAngle2,
                ourPitchAccum, ourYawAccum, GetTickCount());
            LogFromController(buf);
            g_lastLoggedPmlPitch = pmlPitch;
            g_missileGuidanceDiagHasLogged = true;
        }
    } else if (g_missileGuidanceDiagHasLogged && !linked) {
        // One-shot "we left guidance mode" marker so the log clearly brackets the
        // whole sequence instead of just trailing off silently.
        LogFromController("[missile-guidance-dispatch-diag] UNLINKED (guidance ended)");
        g_missileGuidanceDiagHasLogged = false;
    }

    g_origMissileGuidanceDispatch(pmlPtr, clientStructPtr, frameDeltaMs, pmlPlusOne, flagByte);
}
} // namespace

// ---- Interact: hold-to-interact, not instant-on-tap (2026-07-16) -------------------
//
// User feedback after v0.1.0-prealpha: Interact should require a hold, not fire the
// instant X is pressed. Threshold is g_modConfig.interactHoldThresholdMs ([Interact]
// HoldThresholdMs in mw3ncp_config.ini, defaults to 740ms to match the Survival
// ready-up hold, per the original explicit direction "same timing as the F5
// replacement would work fine") -- independently tunable from ready-up's own
// threshold now that both live in config, not sharing one hardcoded constant. Scoped
// ONLY to the raw usercmd Interact bit (0x8) below -- Reload (InjectControllerReload,
// a separate real kbutton on the same physical X button) is untouched and still fires
// instantly on press/release, since reload isn't the thing that was asked to require
// a hold.
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
    LogMissileGuidanceFlagDiag(); // task #30 -- change-triggered, cheap to call every frame

    uint32_t out = 0;
    // Fire (+attack) moved off raw usercmd bit 0x1 and onto the real +attack kbutton --
    // see InjectControllerFire() (task #7, 2026-07-18). fireHeld is still computed here
    // (unaffected) purely for the existing stance diagnostic logging below.
    bool fireHeld = IsPhysicalHeld(g_buttonMap.fire, xiButtons, leftTrigger, rightTrigger);
    if (IsPhysicalHeld(g_buttonMap.melee, xiButtons, leftTrigger, rightTrigger)) out |= 0x4;       // Melee
    if (IsPhysicalHeld(g_buttonMap.tactical, xiButtons, leftTrigger, rightTrigger)) out |= 0x8000; // Tactical (smoke)
    if (IsPhysicalHeld(g_buttonMap.lethal, xiButtons, leftTrigger, rightTrigger)) out |= 0x4000;   // Lethal (frag)
    // Suppressed while a menu is open -- A doubles as menu-select there (InjectControllerMenuNav,
    // task #22), same dual-purpose pattern as B (ESC-forward vs crouch/prone).
    if (IsPhysicalHeld(g_buttonMap.jump, xiButtons, leftTrigger, rightTrigger) && !IsMenuActive()) out |= 0x400;      // Jump (+gostand)

    // Interact (0x8): hold-to-interact, not instant-on-tap -- see the comment on
    // g_interactPressStartMs above. Reload (a separate real kbutton, same physical
    // button as reloadUse) is unaffected and still fires instantly; see
    // InjectControllerReload.
    bool xHeld = IsPhysicalHeld(g_buttonMap.reloadUse, xiButtons, leftTrigger, rightTrigger);
    if (xHeld && !g_interactButtonWasHeld) {
        g_interactPressStartMs = GetTickCount();
    }
    if (xHeld && (GetTickCount() - g_interactPressStartMs) >= g_modConfig.interactHoldThresholdMs) {
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

    // Crouch/Prone (B): drives the REAL togglecrouch/toggleprone toggle
    // (ToggleStance/FUN_0057d2c0, see the comment above GetRealStance) on tap/hold
    // transitions, instead of computing our own stance and forcing a usercmd bit
    // every frame. That older design is what the user's own live testing pointed at
    // as fighting the real engine's own stance state (a stuck-prone Campaign session
    // that neither our B nor Sprint could recover, but real keyboard Ctrl could) --
    // this drives the SAME real toggle a keyboard press does, so there's no separate
    // state left to desync from it. The real toggle's own semantics already implement
    // this mod's whole desired ladder (see the ToggleStance comment for the full
    // per-transition proof), so no ladder logic is needed here beyond hold-vs-tap
    // detection.
    bool bHeld = IsPhysicalHeld(g_buttonMap.crouchProne, xiButtons, leftTrigger, rightTrigger);
    if (bHeld && !g_crouchButtonWasHeld) {
        // Rising edge: new press starting.
        g_crouchButtonPressStartMs = GetTickCount();
        g_holdActionConsumed = false;
    }
    // g_currentBPressTouchedMenu (maintained by InjectControllerMenuBack, which keeps
    // running across a pause unlike this function) gates BOTH the hold and tap fire
    // below -- if this press ever overlapped an active menu at any point, it's the
    // menu's press, not gameplay's, regardless of when InjectControllerButtons happens
    // to next run relative to the menu closing. Edge-tracking bookkeeping below still
    // runs unconditionally so state never desyncs once the flag clears on the next
    // genuinely menu-free press.
    if (bHeld && !g_holdActionConsumed && !g_currentBPressTouchedMenu) {
        DWORD heldMs = GetTickCount() - g_crouchButtonPressStartMs;
        if (heldMs >= g_modConfig.proneHoldThresholdMs) {
            // Hold action fires once, the instant the threshold is crossed.
            ToggleStance(kLocalClientIndex, 2); // toggleprone
            g_holdActionConsumed = true;
            LogStanceDiag("hold-fire");
        }
    }
    if (!bHeld && g_crouchButtonWasHeld && !g_holdActionConsumed && !g_currentBPressTouchedMenu) {
        // Falling edge and the hold threshold was never reached -- this was a tap.
        ToggleStance(kLocalClientIndex, 1); // togglecrouch
        LogStanceDiag("tap-fire");
    }
    g_crouchButtonWasHeld = bHeld;

    // Per-frame usercmd bit assertion still needed for actual Pmove movement/
    // collision behavior -- but now reads the REAL stance field live every frame
    // (GetRealStance()) instead of our own tracked copy, so it can never desync from
    // whatever the authoritative value currently is, even if something else in the
    // engine (e.g. a killstreak's own internal state changes) touches it directly.
    switch (GetRealStance()) {
        case 1: out |= 0x200u; break; // crouch
        case 2: out |= 0x100u; break; // prone
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

bool g_adsHeld = false;
} // namespace

extern "C" void __cdecl InjectControllerAds()
{
    unsigned short buttons;
    unsigned char leftTrigger, rightTrigger;
    if (!Controller_GetRawButtonsAndTriggers(buttons, leftTrigger, rightTrigger)) return;

    bool nowHeld = IsPhysicalHeld(g_buttonMap.ads, buttons, leftTrigger, rightTrigger);
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

    bool nowHeld = IsPhysicalHeld(g_buttonMap.reloadUse, buttons, leftTrigger, rightTrigger);
    if (nowHeld == g_reloadHeld) return; // only fire on the edge, matching a real keypress

    g_reloadHeld = nowHeld;
    if (nowHeld) {
        CallKbuttonDown(kReloadKbutton, kReloadBindIndex);
    } else {
        CallKbuttonUp(kReloadKbutton, kReloadBindIndex);
    }
}

// ---- Fire: RT -> real +attack kbutton (2026-07-18, task #7) -----------------------
//
// Was raw usercmd bit 0x1, forced directly every frame -- confirmed to produce real
// gunfire (Pmove/weapon-fire code reads cmd->buttons directly, same as movement), but
// bypasses the real +attack kbutton_t entirely. iw5sp.md's full killstreak GSC trace
// (2026-07-17) found remote_missile's (Predator Missile) launch is gated behind
// notifyonplayercommand("launch_remote_missile", "+attack") -- a native<->GSC bridge
// that fires on real bind/command dispatch, not on raw usercmd bits being set. Standing
// hypothesis: this is why Predator Missile camera/view works (generic UAV control, not
// notify-gated) but Fire/launch is unreliable.
//
// +attack's real kbutton_t address was already resolved in the SAME bit-correlation
// table (iw5sp.md, "Kbutton table position <-> usercmd.buttons bit correlation") that
// found the other bind offsets: struct base 0x00A98AD8 (per-player, playerIndex*0x230,
// SP player 0 => bare base) + struct offset 0x128 (table idx 0, first entry of the
// 10-entry/stride-0x14 kbutton array FUN_0057dc90 itself reads) = 0x00A98C00. Same
// struct FAMILY as ADS's kbuttons (0x00A98B8C/0x00A98CB8) and Reload's (0x00A98C68),
// same CallKbuttonDown/CallKbuttonUp calling convention already proven live for both.
//
// Full replace, not additive: removed the raw-bit force from InjectControllerButtons
// below and route Fire through this real kbutton instead, same as the crouch/prone
// migration (issue #9) -- FUN_0057dc90 already reads this exact kbutton every frame
// and re-derives the same usercmd bit 0x1 from it, so ordinary gunfire should be
// unaffected, just now via the authentic path instead of a manual force. NOT YET LIVE-
// CONFIRMED for either regular gunfire (regression risk, since this is the single most
// exercised input in the game) or the Predator Missile fix itself -- verify both before
// considering task #7 done.
namespace {
constexpr uintptr_t kAttackKbutton = 0x00A98C00;
constexpr int kAttackBindIndex = 17; // distinct from ADS's 13 / Reload's 15 -- arbitrary
                                      // but must be self-consistent between our own
                                      // down/up calls, same rationale as those two
bool g_attackHeld = false;

// ---- notifyonplayercommand delivery kick, task #7 (2026-07-18) --------------------
//
// The real +attack kbutton call above (CallKbuttonDown/Up) was confirmed live to be
// NECESSARY but NOT SUFFICIENT to launch Predator Missile -- a dedicated Ghidra deep
// dive traced the full native chain GSC's notifyonplayercommand("launch_remote_missile",
// "+attack") actually goes through: the missile's own notify REGISTRATION (a real
// native function, entity-scoped, resolved via the GSC-VM's builtin-method dispatch,
// method ID 0x82A5) is separate from DELIVERY, which only happens when the literal
// command string "n" appears in the local player's real per-client command queue --
// this specific queued command is what makes the engine walk all registered
// notifyonplayercommand/notifyoncommand listeners and fire matching ones. The normal
// path that would push "n" for a real keypress (FUN_00528db0, a generic command-
// forwarder) appears to filter out anything starting with '+'/'-' -- meaning +attack's
// own down-edge may never reach this queue at all, via ANY input method, real keyboard
// included. Confirmed via raw disassembly (not just decompiled pseudocode, since a
// wrong calling convention here risks crashing the game, not just failing silently):
// FUN_00428a70(int clientIdx, const char* str) is a genuinely plain __cdecl function,
// both args on the stack, no register tricks, no interned-string requirement (a bounded
// strncpy-style copy into a 64-byte ring-buffer slot), no lock, and a real but
// non-fatal 128-slot ring-buffer overflow path (logs a warning, still enqueues,
// wraps). Safe to call directly.
//
// This is a genuine engine-internal call, not OS-level input emulation -- same
// category as CallKbuttonDown/Up above, not the PostMessage-based key-synthesis
// exceptions used elsewhere in this file (ready-up/D-pad-squadmate/Back). Pushed only
// on Fire's down-edge, matching how a real one-shot command dispatch behaves (not
// every frame while held), additive alongside the existing kbutton call, not a
// replacement -- if this turns out not to be what's needed, it's a clean, isolated
// revert. NOT YET LIVE-TESTED whether this actually launches the missile -- the call
// itself is confirmed safe to make, that says nothing about whether "n" is really the
// missing piece.
using PushClientCommandFn = void(__cdecl*)(int clientIdx, const char* str);
constexpr uintptr_t kPushClientCommandAddr = 0x00428a70;

void PushClientCommand(int clientIdx, const char* str)
{
    reinterpret_cast<PushClientCommandFn>(kPushClientCommandAddr)(clientIdx, str);
}
} // namespace

extern "C" void __cdecl InjectControllerFire()
{
    unsigned short buttons;
    unsigned char leftTrigger, rightTrigger;
    if (!Controller_GetRawButtonsAndTriggers(buttons, leftTrigger, rightTrigger)) return;

    bool nowHeld = IsPhysicalHeld(g_buttonMap.fire, buttons, leftTrigger, rightTrigger);
    if (nowHeld == g_attackHeld) return; // only fire on the edge, matching a real keypress

    g_attackHeld = nowHeld;
    if (nowHeld) {
        CallKbuttonDown(kAttackKbutton, kAttackBindIndex);
        if (g_modConfig.fireNotifyQueueKick) {
            // FIXED 2026-07-18: "n" ALONE is not sufficient -- a dedicated fork traced
            // delivery (FUN_0053b1f0) all the way through and found it reads Cmd_Argv(1)
            // (the token AFTER "n" in the same tokenized command) and parses it with
            // FUN_00738683 (confirmed to be a plain atol(), not a string hash) as a
            // DECIMAL INDEX into a distinct 81-entry bind-name table at 0x00929fa0
            // (confirmed via direct memory dump, NOT the same as the already-known
            // 32-entry kbutton table -- easy to conflate, verified separately). Index 0
            // is a deliberate placeholder/empty-string slot (avoids ambiguity with "not
            // found"); index 1 = "+attack", confirmed by dumping the table directly.
            // Registration (FUN_00454a30, called from FUN_005BC9A0) stores this same
            // table's index via FUN_005330a0(bindNameStr) -- so "n 1" is what actually
            // matches launch_remote_missile's real +attack registration; "n" alone left
            // Cmd_Argv(1) empty, falling back to a real empty-string constant that could
            // never match. NOT YET LIVE-TESTED.
            PushClientCommand(kLocalClientIndex, "n 1");
        }
    } else {
        CallKbuttonUp(kAttackKbutton, kAttackBindIndex);
    }
}

// ---- Back: +scores (scoreboard) -- this FIRST attempt REVERTED (2026-07-15) -------
// SUPERSEDED 2026-07-17 by the real, working implementation further down this file
// (search "THIRD and final narrow exception" / InjectControllerScoreboard) -- kept here
// as historical dead-end record per this project's "document every last detail,
// including dead ends" standard, not because this approach is still in use.
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

// ---- Sprint: left stick click (L3) -> real +sprint kbutton (2026-07-19) ---------
//
// SUPERSEDES the raw pm_flags-forcing mechanism below this comment block's history.
// FIRST ATTEMPT (struct+0xb0, 2026-07-14) confirmed WRONG by live playtest ("SPRINT NOT
// WORKING") and then confirmed WHY via decompile: FUN_0057d430 does read that flag, but
// only to gate an EXTRA forward/right movement summation that reuses the real keyboard
// +forward/+back hold-time helpers (FUN_0057d250/FUN_007380e0) -- since our movement hook
// writes forwardmove/rightmove as raw bytes instead of driving real kbuttons, those
// helpers always return 0 for us, so the flag gated a summation of zero. Right mechanism
// existed, wrong layer -- same trap Prone and ADS both hit before being solved properly.
//
// SECOND MECHANISM (pm_flags bit 0x4000 force via a Pmove-entry hook, implemented
// 2026-07-15, REPLACED 2026-07-19): found via string xref -> dvar -> read-site tracing.
// The GSC-exposed dvar `player_sprintSpeedScale` (registered in FUN_00494310, pointer
// stored at DAT_01d397e4) is applied in FUN_00643870, gated by
// `*(uint*)(iVar2+0xc) & 0x4000` where iVar2 is a live playerState-style struct pointer.
// That same bit is read at the very top of the whole Pmove state machine,
// FUN_00644ed0(int* param_1). Worked, but forced a raw engine bit directly rather than
// driving a real kbutton -- exactly the class of thing this project's later kbutton
// migrations (Fire, task #7; crouch/prone) moved away from wherever a real kbutton could
// be found instead. Three prior dedicated searches for Sprint's real kbutton (whole-heap
// live-diff correlation x2, live write-testing, a targeted static-range scan -- see
// re_notes/iw5sp.md, "Sprint's real kbutton -- PARKED") all came back negative and this
// was believed to be a genuine dead end.
//
// REAL KBUTTON FOUND (2026-07-19), via a completely different, entirely static technique
// -- no live process/memdiff needed this time. FUN_00438710 (the ~77-case special-bind
// dispatcher already confirmed for ADS/weapnext/togglecrouch) has its real 77-entry jump
// table at 0x00438f48 (bounds-checked `CMP EAX,0x4c; JA default` after `EAX = param_2-1`,
// so param_2 ranges 1-0x4d = 77, matching the "~77-case" estimate exactly). Reconstructed
// all 77 entries via DumpRawRange.java + a raw dword walk (not the decompiler's switch
// recovery, which only partially resolved under -noanalysis). Separately dumped the real,
// STATIC 81-entry canonical bind-name table at 0x00929fa0 (the one FUN_005330a0 linearly
// scans, "index 1 = +attack") via DumpRawDwords.java -- entirely compile-time data, no
// live process required. **The table's own index is confirmed IDENTICAL to
// FUN_00438710's case number**, cross-validated four independent ways: index/case 1 =
// "+attack", 66 (0x42) = "weapnext", 72 (0x48) = "togglecrouch", and 59/60 (0x3b/0x3c) =
// "+toggleads_throw"/"-toggleads_throw" (ADS, matching the already-confirmed 0xa98cb8
// kbutton exactly). This directly resolves the "Back regression" lesson from
// known_issues.md issue #3 (never trust a bind-table index as a case number without
// independent confirmation) -- the earlier mistake used the WRONG table (the 32-entry
// {name,-name} pair table at 0092a014, which is NOT case-ordered); THIS 81-entry table
// genuinely is, four times over.
//
// Index/case 61/62 (0x3d/0x3e) = "+sprint"/"-sprint" -- a real, separate bind command
// distinct from the default-bound "+breath_sprint" (index/case 9/10). Raw disassembly of
// case 0x3d confirms it drives a dedicated kbutton_t at (per-player base)+0xA98CCC, the
// exact same "special case, dedicated global" pattern as ADS's 0xA98CB8 (immediately
// adjacent in memory, one kbutton_t struct apart). **Independently cross-confirmed**: case
// 9 ("+breath_sprint" DOWN, the actual SHIFT-bound default) disassembles to TWO back-to-
// back kbutton calls -- one on 0xA98C04 (a second, previously-unidentified kbutton, very
// likely the real Hold Breath kbutton for task #24) and a SECOND on 0xA98CCC, the exact
// same address "+sprint" drives. I.e. the real default Sprint/Hold-Breath key press
// already drives this same 0xA98CCC kbutton today, on real hardware -- this is not a
// guess from table adjacency, it's the literal disassembled behavior of the bind actually
// shipped and bound by default. See re_notes/iw5sp.md for the full raw disassembly trail.
namespace {
bool g_sprintHeld = false;

// Raw dvar-value getter -- calls the same Dvar_FindVar-equivalent FUN_00498ec0 itself
// calls internally (FUN_0062abe0, confirmed via FUN_00498ec0's disassembly: name arg
// passed in EDI, not on the stack -- a custom register convention, same class as this
// file's other non-cdecl engine calls), then reads the raw int at dvarPtr+0xc directly.
// Deliberately NOT reusing GetDvarString/FUN_00498ec0 here -- that function blindly
// returns `*(char**)(dvarPtr+0xc)` as a string pointer, which is only valid for actual
// string-type dvars; calling it on a boolean/int dvar would read the raw 0/1 stored
// there as if it were a memory address and crash dereferencing it as a string. General
// utility, used elsewhere in this file too (e.g. cl_paused), not Sprint-specific.
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

extern "C" HWND GetGameWindow(); // defined in d3d9_hook.cpp -- forward-declared here
                                  // (this project's own ready-up section, further down
                                  // this file, has the same declaration for its own
                                  // synthetic-key use; linkage specs must be at global
                                  // scope, so it's repeated here rather than nested)

// ---- Sprint (L3): real +sprint kbutton (2026-07-19) ------------------------------
//
// Found via a purely static technique -- see the big comment block above this
// namespace for the full disassembly trail (FUN_00438710's real 77-entry jump table
// cross-referenced against the real static 81-entry canonical bind-name table
// FUN_005330a0 scans, confirming the table index IS the dispatcher's case number,
// four independent ways). Case 61-62 ("+sprint"/"-sprint") drives a dedicated
// kbutton_t at (per-player base)+0xA98CCC -- independently cross-confirmed because the
// real default SHIFT bind ("+breath_sprint", case 9-10) disassembles to two
// back-to-back kbutton calls, one on a new, previously-unidentified 0xA98C04 (very
// likely Hold Breath's own kbutton, a live lead for task #24) and a second on this
// exact same 0xA98CCC.
//
// REMOVED THE SAME DAY, LIVE-CONFIRMED: this migration superseded an entire custom
// stamina/cooldown timer layer this mod maintained since 2026-07-15, built
// specifically to work around the PREVIOUS raw pm_flags-forcing approach bypassing
// the engine's own native sprint duration/recovery timer entirely. Driving the real
// kbutton instead means that native timer now applies automatically -- **live-tested
// and confirmed working**, including Extreme Conditioning's real duration override
// applying for free, with zero separate detection code needed (closing out that half
// of task #9/#24). The old timer (`g_sprintStamina`/`g_sprintWinded`/
// `g_sprintCooldownRemaining`), its `[Sprint]` config section, its
// `player_sprintUnlimited`-dvar bypass, and the diagnostic code that investigated
// whether a real native timer existed (`GetRealSprintValue`/`LogSprintDiag`, see
// FUN_004b9350) are all gone -- not just disabled, since there's nothing left for any
// of them to do. See `re_notes/known_issues.md` issue #6 and `PATCHNOTES.md` for the
// full history, including the three prior dead-end searches this superseded.
// Excludes g_adsHeld (2026-07-19, Hold Breath exception below): the real SHIFT
// keypress that drives Sprint's kbutton also unconditionally fires Hold Breath's
// kbutton on the exact same physical press (per the dispatcher trail above) -- now
// that Hold Breath is driven via a SYNTHETIC real Shift keypress while ADS'd (see
// below), that synthetic press would ALSO re-trigger this project's own direct
// Sprint-kbutton call if this stayed ungated, double-claiming the same kbutton_t's
// down[] slots from two different sources. Excluding ADS here makes the two paths
// fully mutually exclusive: this raw-kbutton path owns Sprint whenever NOT aiming,
// the synthetic-Shift path (which naturally also drives this same kbutton, exactly
// like a real keyboard press) owns it whenever aiming -- matching real console
// behavior anyway, since hip-fire sprint speed has no meaning while ADS'd.
bool IsSprintActive()
{
    return g_sprintHeld && GetRealStance() == 0 && !g_adsHeld;
}

constexpr uintptr_t kSprintKbutton = 0x00A98CCC;
constexpr int kSprintBindIndex = 16; // distinct from ADS's 13/Reload's 15 -- arbitrary,
                                      // just needs to be self-consistent between our own
                                      // down/up calls (see ADS's kAdsBindIndex comment)
bool g_sprintKbuttonActive = false; // tracks whether OUR CallKbuttonDown is currently
                                     // "claimed" on the real kbutton, so we call KeyUp
                                     // exactly once per KeyDown regardless of which of
                                     // several different conditions (controller
                                     // disconnect, physical release, stance change)
                                     // caused sprint to stop being active this tick.

// Drives the real kbutton off IsSprintActive()'s logical state (held + upright stance),
// not just the raw physical hold -- keeps KeyDown/KeyUp edge-triggered exactly once per
// real transition, same convention as ADS/Reload/Fire.
void UpdateSprintKbutton(bool active)
{
    if (active == g_sprintKbuttonActive) return;
    g_sprintKbuttonActive = active;
    if (active) {
        CallKbuttonDown(kSprintKbutton, kSprintBindIndex);
    } else {
        CallKbuttonUp(kSprintKbutton, kSprintBindIndex);
    }
}

// ---- Hold Breath (L3 while ADS'd): FOURTH key-synthesis exception (2026-07-19) ---
//
// Same physical bind as Sprint on real console/keyboard (`+breath_sprint`) -- the
// disassembly trail above (case 9, "+breath_sprint" DOWN) showed the real bind fires
// TWO kbutton calls back-to-back, unconditionally, on every press: 0xA98C04 (this
// project's own name for Hold Breath's kbutton) and 0xA98CCC (Sprint's, above).
//
// TWO PRIOR ATTEMPTS at driving 0xA98C04 directly both failed live:
// 1. Plain CallKbuttonDown/CallKbuttonUp (2026-07-19) -- confirmed live regression,
//    "engages once, never releases."
// 2. Root-caused via full decompile of KeyDown/KeyUp: kbutton_t has a second flag
//    byte at +0x11 that KeyDown sets but KeyUp never clears -- fixed by manually
//    zeroing +0x11 ourselves after KeyUp. **Still confirmed stuck live** -- the
//    +0x11 theory, while well-evidenced from the decompile alone, was NOT the
//    (or not the only) real cause. Whatever the real native consumer of "is Hold
//    Breath active" actually is, it wasn' resolved by fixing kbutton_t's own fields,
//    meaning it likely doesn't read this kbutton_t directly at all, or reads it via
//    a path this project hasn't found.
//
// FOURTH EXCEPTION to the "no OS-level input emulation" rule (matching Survival
// ready-up's F5, D-pad Left's squadmate-call-in '4', and Back's scoreboard TAB):
// synthesize a REAL Shift keypress via PostMessage, ONLY while ADS'd, instead of
// touching the kbutton_t at all. This routes Hold Breath through the exact same
// real native input pipeline a real keyboard player's Shift press already takes --
// sidestepping whatever this project's own direct-kbutton approach was missing,
// same reasoning as every prior exception (IW5 has no DirectInput import at all, so
// keyboard input is genuine WM_KEYDOWN/WM_KEYUP -- a synthetic Shift is
// indistinguishable from a real keypress). The real bind is `bind SHIFT
// "+breath_sprint"` (`players2/config.cfg`, already confirmed in the Sprint kbutton
// research). Deliberately NOT gated on stance (holding breath while crouched/prone
// and scoped is a normal case) -- only on ADS, since a real Shift press while NOT
// ADS'd is Sprint's own job (still handled by the direct-kbutton path above).
//
// IMPORTANT side effect, deliberately accounted for: a real Shift press ALSO fires
// Sprint's own kbutton (0xA98CCC) natively, exactly as it does for a real keyboard
// player. `IsSprintActive()` above now excludes `g_adsHeld` specifically so this
// project's own direct Sprint-kbutton call never double-claims the same kbutton_t
// while this synthetic press is also active -- the two paths are fully mutually
// exclusive (raw kbutton owns Sprint when NOT aiming, synthetic Shift owns both
// Sprint-natively-ignored-by-the-engine and Hold Breath when aiming).
bool g_holdBreathSyntheticHeld = false;

void SendSyntheticHoldBreathKey(bool down)
{
    HWND hwnd = GetGameWindow();
    if (!hwnd) return;
    if (down) {
        PostMessageA(hwnd, WM_KEYDOWN, VK_SHIFT, 0x00000001);
    } else {
        PostMessageA(hwnd, WM_KEYUP, VK_SHIFT, 0xC0000001);
    }
}
} // namespace

extern "C" void __cdecl InjectControllerSprint()
{
    unsigned short buttons;
    unsigned char leftTrigger, rightTrigger;
    if (!Controller_GetRawButtonsAndTriggers(buttons, leftTrigger, rightTrigger)) {
        // Controller gone -- release the real kbutton if we were holding it, same as a
        // real keyboard key being physically lifted. Otherwise a disconnect mid-sprint
        // would leave the engine's own kbutton_t stuck "down" forever.
        UpdateSprintKbutton(false);
        return;
    }

    bool held = IsPhysicalHeld(g_buttonMap.sprint, buttons, leftTrigger, rightTrigger);
    if (held && !g_sprintHeld && GetRealStance() != 0 && !g_adsHeld) {
        // Rising edge while crouched/prone: real console sprint stands the player back
        // up to full upright first, same as pressing forward while ducked/prone does.
        // Drives the same real toggle B does now (ForceStandingViaRealToggle), not our
        // own tracked stance -- without this, sprint would just run while still
        // crouched/prone (bug found 2026-07-15).
        //
        // Excluded while ADS'd (task #24, 2026-07-18): the same physical bind is also
        // Hold Breath while aiming a sniper, and per the original design intent
        // (CLAUDE.md), crouched/prone + Hold Breath must NOT force standing the way
        // ordinary Sprint does -- only the hip-fire Sprint case should trigger the
        // real stance toggle. Hold Breath itself isn't implemented yet (needs its own
        // sway-reduction layer, see known_issues.md task #24) -- this only stops the
        // incorrect forced-stand regression, it doesn't add sway reduction.
        ForceStandingViaRealToggle();
    }
    g_sprintHeld = held;

    // Drive the real +sprint kbutton off held + upright stance -- the engine's own
    // native sprint duration/recovery timer (and Extreme Conditioning's real override)
    // now apply automatically once the real kbutton is engaged, LIVE-CONFIRMED
    // 2026-07-19 (see the big comment above IsSprintActive for the full history of
    // what this replaced).
    UpdateSprintKbutton(IsSprintActive());

    // Hold Breath (task #24, 4th key-synthesis exception): same physical bind, gated
    // on ADS instead of stance -- see the big comment above SendSyntheticHoldBreathKey
    // for why this ignores stance entirely (crouched/prone + scoped is a normal case,
    // unlike Sprint's own standing-only gate) and why this synthesizes a real Shift
    // press instead of driving the kbutton directly (two prior direct-kbutton
    // attempts both failed live).
    bool holdBreathActive = g_sprintHeld && g_adsHeld;
    if (holdBreathActive != g_holdBreathSyntheticHeld) {
        g_holdBreathSyntheticHeld = holdBreathActive;
        char hbBuf[128];
        sprintf_s(hbBuf, "[hold-breath-diag-v2] synthetic Shift -> %s (g_sprintHeld=%d g_adsHeld=%d)",
            holdBreathActive ? "DOWN" : "UP", g_sprintHeld ? 1 : 0, g_adsHeld ? 1 : 0);
        LogFromController(hbBuf);
        SendSyntheticHoldBreathKey(holdBreathActive);
    }

    // Heartbeat, ONLY while active -- distinguishes "our own tracking got stuck true"
    // (this log keeps firing with g_sprintHeld/g_adsHeld both still 1 long after the
    // player believes they released L3) from "our tracking correctly went false but
    // the native/visual effect just didn't clear" (this log stops firing at the right
    // time, meaning the bug is on the native side, not in this project's own state).
    static DWORD s_lastHoldBreathHeartbeatMs = 0;
    if (holdBreathActive) {
        DWORD nowMs = GetTickCount();
        if (nowMs - s_lastHoldBreathHeartbeatMs >= 500) {
            s_lastHoldBreathHeartbeatMs = nowMs;
            char hbBuf2[128];
            sprintf_s(hbBuf2, "[hold-breath-diag-v2] heartbeat: still active (g_sprintHeld=%d g_adsHeld=%d) t=%lu",
                g_sprintHeld ? 1 : 0, g_adsHeld ? 1 : 0, nowMs);
            LogFromController(hbBuf2);
        }
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
// kAdsSlowdownStrength now comes from g_modConfig.adsSlowdownStrength ([Look]
// AdsSlowdownStrength in mw3ncp_config.ini, task #14) rather than being hardcoded.

// Computes the ADS look-rate scale factor for this frame: 1.0 when not aiming (or
// strength is 0), otherwise ratio^strength, where ratio is the live effective-FOV/
// hipfire-FOV ratio (< 1.0 when zoomed in).
//
// ROOT-CAUSE FOUND AND FIXED (2026-07-16): the ORIGINAL linear blend formula here
// (`1 - strength*(1-ratio)`) went NEGATIVE -- inverting look direction -- for any
// strength > 1.0 once ratio dropped below (1 - 1/strength). Live-confirmed with
// diagnostic logging: this was NOT a native engine bug, NOT the ACOG-specific
// alt-FOV-path theory, and NOT FPU corruption (the "risky" alt-path flag,
// DAT_00984b9c bit 2, never set during the whole repro that exposed this --
// FUN_004b0580 stayed on its normal, safe lerp path throughout). It was purely this
// formula's own shape: the user had `AdsSlowdownStrength=2.0` configured (testing
// how far the value could go), and at ratio=0.31 (a real, legitimate ACOG zoom
// level), `1 - 2*(1-0.31) = -0.38` -- a real negative scale factor from otherwise
// completely normal inputs.
//
// Fixed by switching to a power curve (`pow(ratio, strength)`) instead of a linear
// blend: strength=0 -> 1.0 (no slowdown, matches old behavior), strength=1 ->
// exactly `ratio` (matches the old formula's own "fully proportional" case), and
// strength>1 gives progressively MORE aggressive slowdown than proportional --
// but mathematically, `ratio^strength` can NEVER go negative or invert for any
// strength >= 0, no matter how high, since ratio itself is always positive (both
// effectiveFov and baseFov are guarded > 0 above). This preserves the ability to
// configure a stronger-than-1.0 slowdown (rejected a plain clamp-to-1.0 fix for
// exactly this reason) while making the "overflow" that caused inversion
// structurally impossible instead of just guarding against one specific value.
float GetAdsLookRateScale()
{
    if (!g_adsHeld || g_modConfig.adsSlowdownStrength <= 0.0f) return 1.0f;

    float baseFov = GetDvarFloat("cg_fov");
    if (baseFov <= 0.0f) return 1.0f;

    float effectiveFov = static_cast<float>(GetEffectiveFov(kLocalClientIndex));
    if (effectiveFov <= 0.0f) return 1.0f;

    float ratio = effectiveFov / baseFov;
    // Live feedback (2026-07-16): pure ratio^strength gave almost no slowdown on
    // low-zoom optics (ratio close to 1.0 -- iron sights/red dots barely change FOV),
    // since anything close to 1 raised to any power stays close to 1, regardless of
    // strength. adsSlowdownBaseline multiplies the whole curve down, applying some
    // real slowdown even at minimal zoom while preserving the proportional-to-zoom
    // shape on top for higher-magnification optics. Still mathematically safe at any
    // combination of values (both factors are guarded >= 0, so the product can never
    // go negative/invert).
    float scale = g_modConfig.adsSlowdownBaseline * powf(ratio, g_modConfig.adsSlowdownStrength);

    // Diagnostic (task #12/known_issues.md issue #8): rate-limited log of the raw
    // inputs to this computation. DAT_00984b9c is the flag FUN_004b0580 itself
    // checks (bit 2, mask 0x4) to decide between the safe cg_fov-lerp path and the
    // alt-toggle path (FUN_004f6b70) -- kept here since it's still useful evidence
    // that the safe path is the one actually in use during normal ADS.
    static DWORD s_lastAdsDiagLogMs = 0;
    DWORD nowMs = GetTickCount();
    if (nowMs - s_lastAdsDiagLogMs >= 250) {
        s_lastAdsDiagLogMs = nowMs;
        uint8_t altPathFlags = *reinterpret_cast<volatile uint8_t*>(0x00984b9c);
        char buf[200];
        sprintf_s(buf,
            "[ads-fov-diag] baseFov=%.3f effectiveFov=%.3f ratio=%.4f scale=%.4f altFlags=0x%02x",
            baseFov, effectiveFov, ratio, scale, altPathFlags);
        LogFromController(buf);
    }

    return scale;
}
} // namespace

// ---- Aim assist: our own implementation, not the native chain (task #16) ----------
//
// The native aim-assist chain traced earlier this session (FUN_0057d7e0 ->
// FUN_004a07a0 -> FUN_0055bac0 -> FUN_0055b9d0 -> FUN_0055b7d0) turned out to be
// shared math bots use to aim AT the player, not a dormant player-facing feature --
// MW3 PC genuinely has no mouse aim-assist, confirmed by the user. Reusing it for the
// player's own aim would mean invoking bot-aiming logic in the wrong direction
// entirely. Built from scratch instead, using real entity data found via static
// analysis (re_notes/iw5sp.md) plus our own curve math, applied directly onto
// kPitchAccum/kYawAccum below -- no native call chain needed for the actual
// correction, only the entity data it's based on.
//
// Entity array: base 0x9ac010, stride 0x194 bytes, confirmed via disassembly of the
// (bot-aiming) native chain and independently cross-validated via a second, float-
// typed alias into the same array (DAT_009ac020) found in FUN_0055c650. Index 0 is
// the local player (position and a type byte of 1 confirmed live, repeatably).
// +0x10/+0x14/+0x18 = position (X/Y/Z floats). +0xcc = a per-entity type/state byte;
// 0xd/0xf are the two values the native code sends through a tag/bone-lookup path
// (used for animated/skeletal actors), while 0x49 is by far the most common value
// elsewhere (static/dynamic props, not characters). Used here as a coarse "is this a
// living AI actor" filter -- Survival has no neutral AI to exclude besides a co-op
// partner, who would show up as type 1 like the local player, not 0xd/0xf, so this
// filter should naturally exclude both self and any co-op partner without needing a
// separate team field (which wasn't found this session -- see re_notes/iw5sp.md for
// the full trail, including two live entity-type-byte polling sessions that came back
// inconclusive before this static approach was settled on instead).
namespace {
constexpr uintptr_t kEntityArrayBase = 0x009ac010;
constexpr size_t kEntityStride = 0x194;
// BUG FIX (2026-07-17, live-reported "doesn't do anything"): originally 64, on the
// assumption dynamically-spawned entities (AI included) would sit in early slots.
// Confirmed wrong via a live positional scan -- real nearby entities (including the
// only plausible AI-actor cluster found, four 0x0d-type entities at indices 334-338)
// sit almost entirely above index 330. Static level geometry apparently claims the
// early slots at load time; anything spawned later (AI, dropped items, etc.) gets
// allocated into higher-numbered slots instead. 400 covers everything observed live
// with margin.
constexpr int kEntityCount = 400;
constexpr uintptr_t kEntityPosOffset = 0x10;
constexpr uintptr_t kInLevelFlagAddrForAimAssist = 0x00A98ACC; // same flag used elsewhere in this file

// A second, separate 0x270-stride entity array (base 0x01197AD8) was investigated this
// session as a possible source of real type/health data for target classification --
// parked, not adopted. Full trail (what was confirmed statically, what failed live,
// and why) is in re_notes/iw5sp.md rather than duplicated here; the short version is
// the array itself looks real, but the assumed cross-link from this centity-equivalent
// array to it (via this array's own +0x150 field as a "clientnum") produced garbage
// values live and was disproven -- do not resurrect that specific link without new
// evidence.

struct Vec3 { float x, y, z; };

Vec3 GetEntityPosition(int index)
{
    uintptr_t addr = kEntityArrayBase + static_cast<size_t>(index) * kEntityStride + kEntityPosOffset;
    Vec3 v;
    v.x = *reinterpret_cast<volatile float*>(addr);
    v.y = *reinterpret_cast<volatile float*>(addr + 4);
    v.z = *reinterpret_cast<volatile float*>(addr + 8);
    return v;
}

// ---- Movement-based target validity (2026-07-17) -- supersedes the +0xcc type byte
// as the primary filter -----------------------------------------------------------
//
// Three separate live investigations (memdiff polling across multiple sessions,
// static disassembly of FUN_0055c650/FUN_0055ead0, and tracing the GSC `isai()`
// builtin -- a dead end, since GSC builtin names turned out to be hash-based, not
// locatable as strings anywhere in this retail binary) never converged on one type
// value that reliably tags every real enemy. Live-confirmed the same session: the
// type-byte filter DID find and lock onto a real enemy at least once (proving the
// rest of the pipeline -- position reads, angle math, correction -- works), but only
// ever caught ONE after a long delay, and the user confirmed entity slot indices are
// dynamic/unstable between spawns, not fixed per enemy.
//
// Movement sidesteps the classification question entirely: a static prop's position
// never changes; a living AI's does the moment it walks anywhere. Sampled
// periodically rather than every frame -- at 60+fps a single frame's movement is too
// small relative to float jitter to be a reliable signal, but accumulating over a
// longer window makes real movement unambiguous. Once confirmed moving, an entity
// stays "known alive" for a grace period so a briefly-stationary enemy (aiming,
// posted up behind cover) doesn't immediately drop out of consideration.
constexpr DWORD kMovementSampleIntervalMs = 250;
constexpr float kMovementThreshold = 4.0f;  // world units of change over one sample interval
// SHORTENED (2026-07-17, live-reported "targeting anything" not the nearest real
// enemy): 8000ms let one-off movers -- a thrown grenade, a ragdoll settling, physics
// debris -- stay "known alive" long after they'd actually stopped moving, alongside
// genuine walking AI. A real enemy re-triggers movement continuously; a thrown object
// only moves briefly. Shortened so only things ACTIVELY moving right now (or a
// half-second ago) qualify, filtering out one-off movement without needing to
// distinguish AI from props by type at all.
constexpr DWORD kMovementGraceMs = 600;

Vec3 g_lastEntitySamplePos[kEntityCount];
bool g_hasEntitySample[kEntityCount];
DWORD g_lastMovedMs[kEntityCount]; // 0 = never confirmed moving
DWORD g_lastMovementSampleMs = 0;

void SampleEntityMovement()
{
    DWORD now = GetTickCount();
    if (now - g_lastMovementSampleMs < kMovementSampleIntervalMs) return;
    g_lastMovementSampleMs = now;

    for (int i = 1; i < kEntityCount; ++i) {
        Vec3 pos = GetEntityPosition(i);
        if (g_hasEntitySample[i]) {
            float dx = pos.x - g_lastEntitySamplePos[i].x;
            float dy = pos.y - g_lastEntitySamplePos[i].y;
            float dz = pos.z - g_lastEntitySamplePos[i].z;
            float moved = sqrtf(dx * dx + dy * dy + dz * dz);
            if (moved >= kMovementThreshold) {
                g_lastMovedMs[i] = now;
            }
        }
        g_lastEntitySamplePos[i] = pos;
        g_hasEntitySample[i] = true;
    }
}

bool IsEntityKnownAlive(int index)
{
    if (g_lastMovedMs[index] == 0) return false;
    return (GetTickCount() - g_lastMovedMs[index]) < kMovementGraceMs;
}

// Approximate eye-height offset by stance -- the real per-stance constants weren't
// independently found this session (see re_notes/iw5sp.md). Only affects pitch
// precision, and only by a small, distance-shrinking angular error, not yaw --
// acceptable for a first pass; tune live if targets consistently aim slightly high/low.
float EyeHeightForStance(int stance)
{
    switch (stance) {
        case 1: return 40.0f;  // crouch
        case 2: return 18.0f;  // prone
        default: return 64.0f; // standing
    }
}

float NormalizeDegrees(float deg)
{
    while (deg > 180.0f) deg -= 360.0f;
    while (deg <= -180.0f) deg += 360.0f;
    return deg;
}

// Real curve shape recovered from this game's own aim_assist/view_input_0.graph
// (extracted via OpenAssetTools' Unlinker from code_post_gfx.ff -- re_notes/iw5sp.md),
// the gentlest of the four real view_input graphs found there. A classic ease-in
// response: gentle near 0, ramping up toward 1. Reused here as our own falloff shape
// (input = normalized angular distance from crosshair, inverted so 0 = dead-on-target
// = full strength) -- not because it's proven to be the "correct" application for
// this exact purpose (the real graphs are stick-response curves, not confirmed to be
// friction/magnetism-strength curves specifically), but because it's a genuine
// console-authentic curve shape to build on rather than a guessed formula.
constexpr float kCurveX[] = {
    0.0000f, 0.1218f, 0.1972f, 0.2612f, 0.3244f, 0.3872f, 0.4543f, 0.5149f,
    0.5723f, 0.6263f, 0.6771f, 0.7288f, 0.7781f, 0.8387f, 1.0000f
};
constexpr float kCurveY[] = {
    0.0000f, 0.0171f, 0.0295f, 0.0451f, 0.0644f, 0.0897f, 0.1281f, 0.1729f,
    0.2203f, 0.2771f, 0.3483f, 0.4296f, 0.5157f, 0.6301f, 1.0000f
};
constexpr int kCurvePoints = sizeof(kCurveX) / sizeof(kCurveX[0]);

float EvaluateCurve(float x)
{
    if (x <= 0.0f) return 0.0f;
    if (x >= 1.0f) return 1.0f;
    for (int i = 1; i < kCurvePoints; ++i) {
        if (x <= kCurveX[i]) {
            float t = (x - kCurveX[i - 1]) / (kCurveX[i] - kCurveX[i - 1]);
            return kCurveY[i - 1] + t * (kCurveY[i] - kCurveY[i - 1]);
        }
    }
    return 1.0f;
}

struct AimAssistTarget
{
    bool valid = false;
    float yawErrorDeg = 0.0f;
    float pitchErrorDeg = 0.0f;
    float angleErrorDeg = 0.0f; // combined, used for scoring and as the curve input
};

// Finds the best (smallest angular error) valid AI target within range/cone of the
// current crosshair. Player world position comes from entity index 0 (confirmed live,
// repeatably, to be the local player) plus an approximate stance-based eye height;
// current view direction comes straight from kPitchAccum/kYawAccum, since those are
// the real, always-current view angles in degrees (see the comment above their
// declaration) -- no separate native view-origin/angle query needed.
AimAssistTarget FindBestAimAssistTarget(float playerYawDeg, float playerPitchDeg)
{
    AimAssistTarget best;
    if (!g_modConfig.aimAssistEnabled) return best;
    if (*reinterpret_cast<volatile int32_t*>(kInLevelFlagAddrForAimAssist) <= 0) return best;

    SampleEntityMovement();

    Vec3 playerPos = GetEntityPosition(0);
    playerPos.z += EyeHeightForStance(GetRealStance());

    float bestAngleError = g_modConfig.aimAssistConeDegrees;

    for (int i = 1; i < kEntityCount; ++i) {
        if (!IsEntityKnownAlive(i)) continue;

        Vec3 targetPos = GetEntityPosition(i);
        float dx = targetPos.x - playerPos.x;
        float dy = targetPos.y - playerPos.y;
        float dz = targetPos.z - playerPos.z;

        float horizDist = sqrtf(dx * dx + dy * dy);
        float dist = sqrtf(dx * dx + dy * dy + dz * dz);
        if (dist <= 1.0f || dist > g_modConfig.aimAssistRange) continue;

        // Excludes aerial killstreaks/UAVs (live-confirmed 2026-07-17: a first test's
        // "aimed at the sky" turned out to be a real, correct lock onto an active
        // aerial killstreak, not a bug) -- real ground combat, even on multi-level
        // Survival maps, shouldn't need more than this much vertical separation
        // (comfortably covers the ~100-130 unit elevated-platform cluster confirmed
        // live during this session's entity investigation).
        constexpr float kMaxVerticalOffset = 300.0f;
        if (fabsf(dz) > kMaxVerticalOffset) continue;

        // REVERTED (2026-07-17): a first live test looked "aimed at the sky" and this
        // sign got flipped to compensate, but the user confirmed a UAV/aerial
        // killstreak was active during that exact test -- a real correct lock onto a
        // genuinely airborne entity, not a math bug. Reverted to the original
        // (unverified either way, but no longer suspected wrong) sign; the actual fix
        // is excluding aerial entities below, not this formula.
        //
        // Standard engine convention (yaw around Z, pitch relative to horizontal) --
        // NOT independently re-verified for this exact computation. If targets pull
        // the wrong direction on a future live test with no aerial killstreak active,
        // the fix is flipping this atan2's sign/argument order, not the overall
        // approach -- same as every other sign convention in this file that got
        // confirmed via real-hardware playtest rather than static analysis alone.
        float targetYawDeg = atan2f(dy, dx) * (180.0f / 3.14159265f);
        float targetPitchDeg = -atan2f(dz, horizDist) * (180.0f / 3.14159265f);

        // SIGN FIX (2026-07-17, live-reported "facing 180 the wrong way"): yaw error
        // was backwards relative to how kYawAccum actually turns -- magnetism was
        // pushing away from the target's direction, which converges to facing exactly
        // opposite as a stable (wrong) equilibrium. Negated. Pitch wasn't reported
        // wrong this time, left as-is.
        float yawErr = -NormalizeDegrees(targetYawDeg - playerYawDeg);
        float pitchErr = NormalizeDegrees(targetPitchDeg - playerPitchDeg);
        float angleErr = sqrtf(yawErr * yawErr + pitchErr * pitchErr);

        if (angleErr < bestAngleError) {
            bestAngleError = angleErr;
            best.valid = true;
            best.yawErrorDeg = yawErr;
            best.pitchErrorDeg = pitchErr;
            best.angleErrorDeg = angleErr;
            // Diagnostic (2026-07-17, task #16 live tuning): exact numbers instead of
            // guessing from verbal "roughly N degrees off" reports across different,
            // uncontrolled encounters (the actual enemy position differs every time,
            // so "180 wrong" then "90 wrong" aren't directly comparable data points).
            static DWORD s_lastAimAssistDiagLogMs = 0;
            DWORD nowMsDiag = GetTickCount();
            if (nowMsDiag - s_lastAimAssistDiagLogMs >= 300) {
                s_lastAimAssistDiagLogMs = nowMsDiag;
                char buf[300];
                sprintf_s(buf,
                    "[aimassist-diag] idx=%d playerPos=(%.1f,%.1f,%.1f) targetPos=(%.1f,%.1f,%.1f) "
                    "dx=%.1f dy=%.1f dz=%.1f playerYaw=%.1f playerPitch=%.1f "
                    "targetYaw=%.1f targetPitch=%.1f yawErr=%.1f pitchErr=%.1f",
                    i, playerPos.x, playerPos.y, playerPos.z, targetPos.x, targetPos.y, targetPos.z,
                    dx, dy, dz, playerYawDeg, playerPitchDeg, targetYawDeg, targetPitchDeg, yawErr, pitchErr);
                LogFromController(buf);
            }
        }
    }

    return best;
}

// [0,1] multiplier to scale the player's own look rate down when the crosshair is
// near a valid target (rotational friction) -- 1.0 when no target is near, down
// toward (1 - frictionStrength) exactly on target.
float GetAimAssistFrictionScale(const AimAssistTarget& target)
{
    if (!target.valid || g_modConfig.aimAssistFrictionStrength <= 0.0f) return 1.0f;
    float normalizedDist = target.angleErrorDeg / g_modConfig.aimAssistConeDegrees;
    float curveOut = EvaluateCurve(1.0f - normalizedDist); // invert: closer = more friction
    return 1.0f - g_modConfig.aimAssistFrictionStrength * curveOut;
}
} // namespace

// "Look-stick" rather than a hardcoded "right stick" -- see InjectControllerMovement's
// comment on RouteStickAxes/task #15's Stick Layout. Under the default layout this is
// exactly the original right-stick-only behavior.
extern "C" void __cdecl InjectControllerLookAngles()
{
    float leftX, leftY, rightX, rightY;
    if (!Controller_GetLeftStick(leftX, leftY)) return;
    if (!Controller_GetRightStick(rightX, rightY)) return;

    float moveX, moveY, lookX, lookY;
    RouteStickAxes(leftX, leftY, rightX, rightY, g_modConfig.stickLayout, moveX, moveY, lookX, lookY);

    float dt = Controller_DeltaTimeSeconds();
    if (dt <= 0.0f) return;

    // Evaluated BEFORE this frame's own stick-based update below, against wherever
    // the crosshair currently sits.
    AimAssistTarget target = FindBestAimAssistTarget(*kYawAccum, *kPitchAccum);
    float frictionScale = GetAimAssistFrictionScale(target);

    if (lookX != 0.0f || lookY != 0.0f) {
        // Degrees per second at full stick deflection -- independent of every mouse
        // cvar. g_modConfig.lookDegreesPerSecond ([Look] Sensitivity in
        // mw3ncp_config.ini, task #14) rather than a hardcoded constant.
        float rate = g_modConfig.lookDegreesPerSecond * GetAdsLookRateScale() * frictionScale;
        float pitchInput = g_modConfig.invertLook ? -lookY : lookY; // OG console "Invert Look"
        *kYawAccum -= lookX * rate * dt;
        *kPitchAccum -= pitchInput * rate * dt;
    }

    // Magnetism: a small, capped pull toward a valid target's exact angle, applied
    // regardless of whether the stick moved this frame (matches real console assist
    // continuing to hold you onto a target even while the stick is centered).
    if (target.valid && g_modConfig.aimAssistMagnetismDegreesPerSecond > 0.0f) {
        float maxStep = g_modConfig.aimAssistMagnetismDegreesPerSecond * dt;
        float yawStep = target.yawErrorDeg;
        if (yawStep > maxStep) yawStep = maxStep;
        if (yawStep < -maxStep) yawStep = -maxStep;
        float pitchStep = target.pitchErrorDeg;
        if (pitchStep > maxStep) pitchStep = maxStep;
        if (pitchStep < -maxStep) pitchStep = -maxStep;
        *kYawAccum += yawStep;
        *kPitchAccum += pitchStep;
    }
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
// real native call is found -- at which point this gets replaced. This was the sole
// deliberate departure from the project's "no OS-level input emulation" rule until a
// second, narrower one was added for D-pad Left's squadmate call-in (see that section
// further down); every OTHER button in this file still drives the engine's real
// internal state directly. Safe by construction even without a "is the ready-up wait
// specifically active" check (which we don't have): IW5 has no DirectInput import at
// all (confirmed in CLAUDE.md's own findings), so keyboard input is real
// WM_KEYDOWN/WM_KEYUP messages -- a synthetic F5 outside the one context it matters is
// simply ignored by the game itself, the same as a real, misplaced F5 press would be.
namespace {
using GetDvarStringFn = const char*(__cdecl*)(const char*);
GetDvarStringFn const GetDvarString = reinterpret_cast<GetDvarStringFn>(0x00498ec0);
extern "C" HWND GetGameWindow(); // defined in d3d9_hook.cpp
// Ready-up hold threshold is g_modConfig.readyUpHoldThresholdMs ([Survival]
// ReadyUpHoldThresholdMs in mw3ncp_config.ini, task #14), independently tunable from
// Interact's own hold threshold now that both live in config.
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

    bool held = IsPhysicalHeld(g_buttonMap.weaponSwitch, buttons, leftTrigger, rightTrigger);
    if (held && !g_yHeld) {
        g_yPressStartMs = GetTickCount();
        g_yReadyUpFired = false;
    }
    if (held && !g_yReadyUpFired && (GetTickCount() - g_yPressStartMs) >= g_modConfig.readyUpHoldThresholdMs) {
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
// contrast with case 2 (which sets cl_paused non-zero and opens pausedmenu).
//
// FIXED 2026-07-17 (pre-release review, task v0.1.3): this used to track its own
// `g_paused` bool, set only on a controller Start press -- exactly the same class of
// "manually-tracked copy can desync from the engine's own real state" bug the
// crouch/prone rewrite (see GetRealStance() above) was built to eliminate. Real
// keyboard ESC also opens/closes the pause menu natively (keyboard/mouse remains
// fully supported alongside controller per this project's own design) -- if a player
// paused/unpaused via keyboard, `g_paused` never found out, so the next controller
// Start press could act on stale belief and call the wrong case (open on an
// already-paused game, or unpause on an already-running one, silently eating that
// Start press). Now reads `cl_paused` directly via `GetDvarInt` (the same real dvar
// SetMenuState's own open/close cases toggle) instead of trusting a local copy --
// the same fix shape as the crouch/prone rewrite, applied here for the same reason.
using SetMenuStateFn = void(__cdecl*)(int playerIndex, int mode);
SetMenuStateFn const SetMenuState = reinterpret_cast<SetMenuStateFn>(0x004396d0);
constexpr int kMenuStateUnpause = 0;
constexpr int kMenuStatePausedMenu = 2;
} // namespace

// ---- B -> real ESC-forward-to-menu, for "exit menu / back one step" (2026-07-16) ----
//
// Reuses two things already found and confirmed for Start's pause-menu work above,
// not a fresh discovery: the real per-player "a menu is currently active" gate bit
// (`0x10` at `0xB36210`, the SAME address this file already force-clears for 3
// seconds after level entry, see the big writeup above `InjectAllControllerInput`),
// and `FUN_004d9850(playerIndex, keyCode, isDown)` -- the exact real call the
// decompiled `FUN_00541020` key-event handler makes to forward a keypress to
// whatever menu is active when its own gate check is true. That's the literal
// mechanism real ESC uses to back out of ANY open menu (main menu, pause menu, buy
// station, options, etc.), not something pause-specific -- so calling it ourselves
// with keycode `0x1b` (ESC) reproduces exactly what a real ESC press does, in
// whatever menu context is actually open. Confirmed `__cdecl` via the same real call
// site disassembly already cited for `OpenPauseMenu`/`SetMenuState` above.
//
// Deliberately hardcoded to physical B (not routed through `g_buttonMap`/layout
// remapping) for the same reason Start/pause is: this is a system-level menu action,
// not a gameplay bind, so it should behave identically regardless of button-layout
// preset. Only acts while a menu is genuinely active (`IsMenuActive()`) -- while
// gameplay is running normally, this function does nothing at all. Since B is ALSO
// the crouch/prone button (`InjectControllerButtons`), and that function's own edge-
// tracking state goes stale while paused (it's dead during pause, same reason this
// function had to move onto the always-running WndProc tick -- see known_issues.md
// issue #13), this function also continuously maintains
// `g_currentBPressTouchedMenu` (see its declaration comment above
// `InjectControllerButtons`) so crouch/prone can never fire for a B press that
// overlapped an open menu, no matter when `InjectControllerButtons` next happens to
// run relative to the menu closing.
namespace {
using ForwardKeyToMenuFn = void(__cdecl*)(int playerIndex, int keyCode, int isDown);
ForwardKeyToMenuFn const ForwardKeyToMenu = reinterpret_cast<ForwardKeyToMenuFn>(0x004d9850);
constexpr int kKeyEscape = 0x1b;
bool g_menuBackHeld = false;
} // namespace

extern "C" void __cdecl InjectControllerMenuBack()
{
    unsigned short buttons;
    unsigned char leftTrigger, rightTrigger;
    if (!Controller_GetRawButtonsAndTriggers(buttons, leftTrigger, rightTrigger)) return;

    bool held = IsPhysicalHeld(PhysicalInput::B, buttons, leftTrigger, rightTrigger);
    bool menuActive = IsMenuActive();

    if (held && !g_menuBackHeld) {
        // Rising edge of B itself: a fresh physical press is starting, so whatever the
        // previous press did is no longer relevant.
        g_currentBPressTouchedMenu = false;
    }
    if (held && menuActive) {
        // This press has touched an active menu at some point -- mark it as the
        // menu's press for the remainder of this hold, however long that turns out to
        // be, so InjectControllerButtons never fires crouch/prone for it.
        g_currentBPressTouchedMenu = true;
    }
    if (menuActive && held != g_menuBackHeld) {
        ForwardKeyToMenu(kLocalClientIndex, kKeyEscape, held ? 1 : 0);
    }
    g_menuBackHeld = held;
}

// ---- D-pad Up/Down + A -> real menu item navigation/select (2026-07-17, task #22) ----
//
// Decompiling FUN_00541020 (the real key-event handler, right where the already-
// confirmed ESC-forward call lives) shows ForwardKeyToMenu (FUN_004d9850) is NOT
// ESC-specific -- it's called for ANY keycode whenever the same menu-active gate bit
// (0x10 at 0xB36210) is set:
//     if ((*(uint*)(&DAT_00b36210 + iVar8) & 0x10) != 0) {
//         ...
//         FUN_004d9850(param_1, uVar5, param_3);   // forwards whatever keycode this is
//         return;
//     }
// Real menu items are genuine engine-native focusable objects too (confirmed via the
// extracted, plain-text .menu assets in zone/english/ui.ff -- e.g.
// scriptmenus/survival_armory_weapon.menu's itemDef blocks with onFocus/leaveFocus/
// action callbacks, plus a dormant ui_buttonNavGroupCurrent_popup "selection cursor"
// and ui_swfSelectionBarVis "highlight bar" var pair that a working nav input would
// drive). So real D-pad-driven item navigation doesn't need a new mechanism, just the
// SAME call already wired for B, fed the right keycodes for Up/Down/Enter instead of
// ESC.
//
// UPDATED (2026-07-17, first live test with the standard-idTech-constant guess
// 128/129 came back "nothing"): decompiled FUN_004dfd30, the real function
// ForwardKeyToMenu's non-ESC branch calls, and found its actual keycode switch has
// no case for 128/129 at all -- that guess was wrong. The switch DOES contain two
// real groups of alternate keycodes, confirmed via decompiling the two functions
// each group calls:
//   Group A (case 9, 0x9b, 0x9d, 0xbd, 0xcd) -> FUN_006253d0(param_2, 1), which
//     increments a focus-index field (param_1[0x2a] item count, wraps at the end)
//     -- genuine native "move to NEXT item".
//   Group B (case 0x9a, 0x9c, 0xb7, 0xce) -> FUN_00625290(param_2, 1), which
//     decrements the same index (wraps at the start) -- genuine native "move to
//     PREVIOUS item".
// Any keycode within a group calls the identical function, so one representative
// value per group was picked (0x9b for next/down, 0x9a for previous/up) -- these
// are NOT a guess, they're read directly out of the real switch statement. `0xd`
// (13, Enter) for select/activate WAS already in this same switch (its own case,
// confirmed handling a "local_188"/selected-item pointer) -- that part of the
// original guess was correct and is unchanged.
//
// A doubles as gameplay Jump normally (InjectControllerButtons) -- same dual-purpose
// pattern as B (ESC-forward vs crouch/prone), gated the same way: InjectControllerDpad
// and Jump's own bit are both suppressed while IsMenuActive() so D-pad/A can't mean
// two things at once.
// UPDATED AGAIN (2026-07-17, user report: main menu worked with Up/Down but the same
// screen actually needed real Left/Right, and separately, options-style two-pane
// screens -- category list on the left, that category's settings on the right (see
// known_issues.md issue #22 for the screenshot/discussion) -- need a distinct
// "drill in / drill out" gesture on Left/Right that plain next/prev can't provide,
// since the two panes are genuinely separate sibling menuDefs (pc_options_video etc
// open/close each other), not one combined item list. Initially unified Up+Left/
// Down+Right onto the same two keycodes (0x9a/0x9b) -- WRONG, confirmed by checking
// the real keyboard behavior the user described ("pressing right on keyboard works
// and left") and finding the actual mechanism in the extracted .menu scripts
// (zone/english/ui.ff, e.g. ui/pc_options_video.menu):
//     execKeyInt 157 { if (getfocuseditemname() == "OPTIONS_LIST_0" || ...) {
//         setfocus localvarstring(ui_options_focus); } }   // ->  drill IN
//     execKeyInt 156 { if (getfocuseditemname() == "color_blind" || ...) {
//         setLocalVarString ui_options_focus getfocuseditemname();
//         setfocus OPTIONS_LIST_0; } }                     // ->  drill OUT
// These are REAL keyboard Left(156)/Right(157) codes, distinct from the Up/Down alt-
// pair (0x9a/0x9b) -- and critically, each menu's execKeyInt only fires when focus
// matches its specific condition; otherwise 156/157 silently fall through to the
// exact same generic FUN_006253d0/FUN_00625290 previous/next dispatch Up/Down uses
// (156/157 are themselves alternate members of those same two groups -- see the
// group listing above). So real keyboard Left/Right get free, native, per-menu-aware
// drill-in/drill-out on options-style screens, and plain previous/next everywhere
// else -- with NO custom "am I inside a submenu" state-tracking needed on our side.
// Using the real keycodes instead of reusing Up/Down's gets us the same thing.
namespace {
constexpr int kKeyPrevItem = 0x9a;  // real Up alt-keycode -- generic previous only, see FUN_00625290
constexpr int kKeyNextItem = 0x9b;  // real Down alt-keycode -- generic next only, see FUN_006253d0
constexpr int kKeyLeftNav = 0x9c;   // real Left keycode -- drill-out on options screens, generic previous elsewhere
constexpr int kKeyRightNav = 0x9d;  // real Right keycode -- drill-in on options screens, generic next elsewhere
constexpr int kKeyEnter = 13;       // K_ENTER -- confirmed, same case in FUN_004dfd30
bool g_menuNavUpHeld = false;
bool g_menuNavDownHeld = false;
bool g_menuNavLeftHeld = false;
bool g_menuNavRightHeld = false;
bool g_menuNavSelectHeld = false;
} // namespace

extern "C" void __cdecl InjectControllerMenuNav()
{
    if (!IsMenuActive()) {
        // Not stale-tracking across a menu close -- next press should always be seen
        // as a fresh rising edge once a menu is open again.
        g_menuNavUpHeld = false;
        g_menuNavDownHeld = false;
        g_menuNavLeftHeld = false;
        g_menuNavRightHeld = false;
        g_menuNavSelectHeld = false;
        return;
    }

    unsigned short buttons;
    unsigned char leftTrigger, rightTrigger;
    if (!Controller_GetRawButtonsAndTriggers(buttons, leftTrigger, rightTrigger)) return;

    bool upHeld = (buttons & kXI_DPAD_UP) != 0;
    if (upHeld != g_menuNavUpHeld) {
        ForwardKeyToMenu(kLocalClientIndex, kKeyPrevItem, upHeld ? 1 : 0);
        g_menuNavUpHeld = upHeld;
    }
    bool downHeld = (buttons & kXI_DPAD_DOWN) != 0;
    if (downHeld != g_menuNavDownHeld) {
        ForwardKeyToMenu(kLocalClientIndex, kKeyNextItem, downHeld ? 1 : 0);
        g_menuNavDownHeld = downHeld;
    }
    bool leftHeld = (buttons & kXI_DPAD_LEFT) != 0;
    if (leftHeld != g_menuNavLeftHeld) {
        ForwardKeyToMenu(kLocalClientIndex, kKeyLeftNav, leftHeld ? 1 : 0);
        g_menuNavLeftHeld = leftHeld;
    }
    bool rightHeld = (buttons & kXI_DPAD_RIGHT) != 0;
    if (rightHeld != g_menuNavRightHeld) {
        ForwardKeyToMenu(kLocalClientIndex, kKeyRightNav, rightHeld ? 1 : 0);
        g_menuNavRightHeld = rightHeld;
    }
    bool selectHeld = IsPhysicalHeld(PhysicalInput::A, buttons, leftTrigger, rightTrigger);
    if (selectHeld != g_menuNavSelectHeld) {
        ForwardKeyToMenu(kLocalClientIndex, kKeyEnter, selectHeld ? 1 : 0);
        g_menuNavSelectHeld = selectHeld;
    }
}

extern "C" void __cdecl InjectControllerPauseMenu()
{
    unsigned short buttons;
    unsigned char leftTrigger, rightTrigger;
    if (!Controller_GetRawButtonsAndTriggers(buttons, leftTrigger, rightTrigger)) return;

    bool held = IsPhysicalHeld(g_buttonMap.pause, buttons, leftTrigger, rightTrigger);
    if (held && !g_startHeld) {
        char buf[128];
        bool currentlyPaused = GetDvarInt("cl_paused") != 0;
        if (!currentlyPaused) {
            // If some OTHER menu is already open (buy station, etc. -- the same real
            // gate bit B's own menu-back checks) close it FIRST via the same real
            // ESC-forward mechanism, so the pause menu doesn't stack on top of it and
            // unpausing later drops the player straight back into gameplay instead of
            // back inside that menu. Deliberately calls ForwardKeyToMenu directly
            // rather than routing through InjectControllerMenuBack/g_menuBackHeld --
            // this is a synthetic close triggered by Start, not a real physical B
            // press, so it must NOT touch g_currentBPressTouchedMenu or any of B's own
            // press-tracking state (see known_issues.md issue #13 for why that state
            // has to stay scoped to B's actual physical presses only).
            if (IsMenuActive()) {
                sprintf_s(buf, "[pause-diag] Start pressed while a menu is open -- auto-closing it first");
                LogFromController(buf);
                ForwardKeyToMenu(kLocalClientIndex, kKeyEscape, 1);
                ForwardKeyToMenu(kLocalClientIndex, kKeyEscape, 0);
            }
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
        } else {
            LogFromController("[pause-diag] Start pressed (closing): calling SetMenuState(0, unpause)");
            SetMenuState(kLocalClientIndex, kMenuStateUnpause);
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

// GAME-BREAKING BUG, RESOLVED (live-reported after the v0.1.0-prealpha release,
// fixed and CONFIRMED LIVE 2026-07-16): using the Predator missile killstreak while
// prone in the first mission used to leave the player permanently stuck prone -- not
// recoverable even via real keyboard input. Static RE ruled out the obvious suspect:
// FUN_0057d2c0 (the function that caused the earlier, similarly-unrecoverable
// stuck-prone regression during the F5/ready-up hunt) has exactly one caller in the
// whole binary (FUN_00438710's cases 0x48/0x49, confirmed via FindCallers.java) and
// neither was invoked anywhere in this file -- so this was a different bug, not a
// recurrence of that one, despite the identical symptom.
//
// Root cause: InjectControllerButtons used to unconditionally re-assert our OWN
// tracked g_stance's usercmd bit (0x100/0x200) every single frame regardless of what
// else the game was doing -- the same general failure pattern as the earlier buy-
// station+pause bug (known_issues.md issue #1: forcing a bit continuously, ignoring
// context, breaks a native subsystem's own state transition). Predator missile is used
// like a "weapon" (select via D-pad, then fire) that puts the local player into a
// scripted missile-cam sequence; if the player was prone when that sequence started,
// that old per-frame forcing kept fighting the prone bit through it and through the
// exit transition.
//
// A first attempt fixed this by auto-standing before a killstreak-type D-pad select
// (mirroring Sprint's own "auto-stand from crouch/prone first" precedent above) --
// REJECTED by the user: real console MW3 does NOT force you to stand to use a
// killstreak while prone, so that "fix" would have broken behavior parity with the
// original game to paper over a bug, which fails this project's console-parity bar.
// Reverted.
//
// Actual fix landed indirectly: a SEPARATE live repro (a stuck-prone Campaign session
// B/Sprint couldn't recover, but real keyboard Ctrl could) confirmed our own
// g_stance-based bit-forcing WAS fighting the real engine's own stance field rather
// than reading it -- B/Sprint were rewired (see ToggleStance/GetRealStance above) to
// call the real togglecrouch/toggleprone toggle and read the real stance field live
// every frame, instead of tracking a separate copy that could desync. **CONFIRMED
// LIVE by the user**: this fixed the Predator-missile-while-prone repro too, exactly
// as expected -- the specific mechanism (our own stale bit fighting a real state
// change mid-sequence) no longer exists. See known_issues.md issue #9 for the full
// crouch/prone rewrite writeup.
// ---- D-pad Left / actionslot4 squadmate call-in: EXPLICIT, NARROWLY-SCOPED EXCEPTION
// (user-approved 2026-07-16) -- same category of workaround as Survival ready-up's F5
// synthesis above, applied here ONLY to D-pad Left, NOT the other three directions.
//
// Task #13: turret call-ins worked via the direct `FUN_00410ad0(playerIndex,3)` /
// `FUN_0044ec40(playerIndex)` calls below, but AI-squadmate call-ins (purchased at the
// same buy station, same D-pad Left slot, different loadout choice) failed 100% of the
// time -- confirmed identical on the native-call side (same addresses, same arguments
// as what FUN_00438710's real dispatcher itself calls for the real '4' key, verified via
// direct disassembly of both, not a guess) and confirmed NOT a timing issue (deliberately
// holding D-pad Left longer live-tested, no change). Whole-binary string search found
// zero occurrences of "squad" or any ally-call-in terminology anywhere (turret, by
// contrast, has dozens of native strings: `ET_TURRET`, `G_SpawnTurret`,
// `sentry_placement_trace_*`, etc.) -- and the user confirmed this call-in is unique to
// Survival, not shared with Campaign. Together this points at the same root cause as
// ready-up: a Survival-specific GSC script watching for something our direct native
// call never produces, most likely a genuine key event (the same category of problem,
// not yet independently confirmed via a GSC decompile -- still blocked on the same `.ff`
// unpacker gap noted elsewhere in this file).
//
// Fix: for D-pad Left ONLY, synthesize a real WM_KEYDOWN/WM_KEYUP for '4' (the actual
// bound key for `+actionslot4`, confirmed via the live raw-keycode-table read above)
// instead of calling FUN_00410ad0/FUN_0044ec40 directly -- so whatever's watching for a
// real keypress (GSC or otherwise) sees exactly what a real keyboard press produces,
// same reasoning as ready-up's F5 synthesis (IW5 has no DirectInput import at all, so
// keyboard input is genuine WM_KEYDOWN/WM_KEYUP -- a synthetic '4' is indistinguishable
// from a real one, and simply falls through the normal FUN_00438710 dispatch itself,
// which is what still drives turret-type items correctly through this same path).
// Deliberately does NOT ALSO call FUN_00410ad0/FUN_0044ec40 for this slot -- doing both
// would double-dispatch the native side (the synthesized key's own real dispatch already
// calls FUN_00410ad0 itself). The other three D-pad directions are UNCHANGED, still
// driven by the direct native call, since nothing has been reported broken about them.
//
// EXPLICITLY NOT a general policy change: this is one narrowly-scoped exception for one
// specific input, same as ready-up's. Per the user's own direction (2026-07-16): "we
// will trace all these non natives later on" -- the real GSC-side mechanism for this
// (and ready-up) should eventually be found and this synthesis replaced, not treated as
// a permanent design choice.
namespace {
void SendSyntheticActionSlot4Key(bool down)
{
    HWND hwnd = GetGameWindow();
    if (!hwnd) return;
    if (down) {
        PostMessageA(hwnd, WM_KEYDOWN, '4', 0x00000001);
    } else {
        PostMessageA(hwnd, WM_KEYUP, '4', 0xC0000001);
    }
}
} // namespace

// ---- Back -> real +scores (scoreboard/objectives) via key synthesis -- THIRD and
// final narrow exception to the "no OS-level input emulation" rule (2026-07-17) ----
//
// Real trigger genuinely never found this session or prior ones: the previous
// attempt (wiring FUN_00438710's dispatcher directly with a guessed case number)
// regressed live -- it hit +back's real kbutton instead of +scores' (see
// known_issues.md issue #3), because the guess was never independently validated,
// exactly the mistake the live-raw-keycode-table technique exists to avoid. That
// technique doesn't apply cleanly here since +scores isn't a per-frame usercmd
// button/kbutton at all -- it's a plain keyboard bind (`bind TAB "+scores"`,
// confirmed real in players2/config.cfg) read directly by whatever UI draws the
// scoreboard/objectives overlay, the same category of "genuine WM_KEYDOWN/KEYUP,
// not a native call" problem ready-up (F5) and D-pad Left's squadmate call-in ('4')
// already needed the same fix for.
//
// Justified as "good enough for now, not essential to gunplay" per explicit user
// direction -- Back has no other current meaning (confirmed unused elsewhere in this
// file), so there's no dual-purpose conflict to manage, unlike B/A's menu-context
// overloading. In Campaign this shows the real scoreboard/mission-objectives
// overlay; Survival has no native scoreboard at all (confirmed this session, see
// re_notes/known_issues.md and the project memory on Back's scope split) so holding
// Back there is expected to do nothing visible -- not a bug, just Survival genuinely
// having nothing native for TAB to show.
//
// Hold-through-passthrough, not tap/toggle: `+scores` is itself a real hold-to-show
// bind, so Back down -> TAB down, Back up -> TAB up, mirrors real keyboard exactly.
namespace {
bool g_scoreboardHeld = false;
}

extern "C" void __cdecl InjectControllerScoreboard()
{
    unsigned short buttons;
    unsigned char leftTrigger, rightTrigger;
    if (!Controller_GetRawButtonsAndTriggers(buttons, leftTrigger, rightTrigger)) return;

    bool held = IsPhysicalHeld(g_buttonMap.scoreboard, buttons, leftTrigger, rightTrigger);
    if (held == g_scoreboardHeld) return;
    g_scoreboardHeld = held;

    HWND hwnd = GetGameWindow();
    if (!hwnd) return;
    if (held) {
        PostMessageA(hwnd, WM_KEYDOWN, VK_TAB, 0x00000001);
    } else {
        PostMessageA(hwnd, WM_KEYUP, VK_TAB, 0xC0000001);
    }
}

extern "C" void __cdecl InjectControllerDpad()
{
    unsigned short buttons;
    unsigned char leftTrigger, rightTrigger;
    if (!Controller_GetRawButtonsAndTriggers(buttons, leftTrigger, rightTrigger)) return;

    // While a menu is open, D-pad drives item navigation instead (InjectControllerMenuNav,
    // task #22) -- suppress the gameplay actionslot dispatch below so D-pad can't mean two
    // things at once, but still update g_dpadHeld unconditionally (not an early return)
    // so press-tracking never goes stale across a menu open/close, the same staleness bug
    // already fixed once for B/crouch (known_issues.md issue #13).
    bool menuActive = IsMenuActive();

    struct { unsigned short bit; int slot; } kDpad[4] = {
        { kXI_DPAD_UP, 0 }, { kXI_DPAD_RIGHT, 1 }, { kXI_DPAD_DOWN, 2 }, { kXI_DPAD_LEFT, 3 }
    };
    for (int i = 0; i < 4; ++i) {
        bool held = (buttons & kDpad[i].bit) != 0;
        if (held != g_dpadHeld[i]) {
            bool isSlot4 = (kDpad[i].slot == 3);
            if (held) {
                char tag[32];
                sprintf_s(tag, "dpad-press slot=%d", kDpad[i].slot);
                LogStanceDiag(tag);
                if (!menuActive) {
                    if (isSlot4) {
                        SendSyntheticActionSlot4Key(true);
                    } else {
                        ActionSlotDown(kLocalClientIndex, kDpad[i].slot);
                    }
                }
            } else {
                LogStanceDiag("dpad-release");
                if (!menuActive) {
                    if (isSlot4) {
                        SendSyntheticActionSlot4Key(false);
                    } else {
                        ActionSlotUp(kLocalClientIndex);
                    }
                }
            }
            g_dpadHeld[i] = held;
        }
    }
}

// ---- DEBUG-ONLY: live test of the real zone-loading entry point (task #23) ----
//
// FUN_004ca310 is the real function FUN_0053cbc0 (the level-load orchestrator) calls
// repeatedly to queue real zones for loading (patch_specialops, common_survival,
// etc.), always as {char* name, int flags, int unused} triples: `array, count, mode`.
// Disassembly shows it's a 2-instruction tail-dispatch veneer (CALL FUN_00463430;
// JMP EAX) -- not a real jump table Ghidra failed to recover, a genuinely computed
// redirect. Calling this exact entry point ourselves is exactly what real game code
// does, not a bypass. FUN_0053cbc0's own call sites decompiled as plain 3-int-arg
// calls with no register-passed-arg warnings, so treated as __cdecl here.
//
// Test zone: zone/dlc/roundtrip.ff (NEW file, nothing existing touched), a known-good
// round-trip of an UNMODIFIED real game menu (spec_ops_dlc_go_to_store_popup.menu)
// compiled via OpenAssetTools' Linker -- isolates whether the LOAD call itself works
// before ever trying custom-authored content. Gated behind a deliberately obscure,
// impossible-to-hit-by-accident hold (LB+RB, 2s) so this can't fire during normal
// play; fires once per session (g_zoneLoadTestFired latch).
namespace {
using LoadZonesFn = void(__cdecl*)(void* zoneArray, int count, int mode);
LoadZonesFn const LoadZones = reinterpret_cast<LoadZonesFn>(0x004ca310);

struct ZoneLoadEntry { const char* name; int flags; int unused; };

// FUN_00544a50(&DAT_01c00458, "menuname") -- the real, generic "open menu by name"
// function, found by fully decompiling FUN_004396d0 (already confirmed real for the
// pause menu specifically). Every single menu transition in that function goes
// through this exact call -- "pausedmenu", "briefing", "victoryscreen",
// "main_specops", "error_popmenu", etc. -- all as plain string names passed straight
// from native C code, not menu-script bytecode. DAT_01c00458 is a real, shared
// "menu system context" object passed to every menu operation seen this session
// (FUN_004c8c00, FUN_00544a50, FUN_004ae120), always by address.
using OpenMenuByNameFn = void(__cdecl*)(void* menuContext, const char* menuName);
OpenMenuByNameFn const OpenMenuByName = reinterpret_cast<OpenMenuByNameFn>(0x00544a50);
constexpr uintptr_t kMenuSystemContext = 0x01c00458;

// The two other real calls SetMenuState's real pausedmenu case makes alongside
// FUN_00544a50(&DAT_01c00458,"pausedmenu") -- found by fully decompiling
// FUN_004396d0 (already confirmed real for Start's pause menu). Live-confirmed
// necessary 2026-07-17: calling FUN_00544a50 with our own custom menu name alone
// DID register/open it (proven -- it rendered briefly), but only became genuinely
// VISIBLE once the player separately paused (Start) afterward -- the engine's main
// render path stays on the 3D world unless these two also run, switching it to the
// paused/menu render mode. Both confirmed __cdecl via raw disassembly (2 stack
// args each, plain RET).
//   FUN_005396b0(dvarName, value) -- generic "set a dvar by name" utility; real
//     call site uses it for "cl_paused" specifically, with plain 0/1 int values
//     (0 hardcoded directly in the unpause case elsewhere in the same function).
//   FUN_005293c0(playerIndex, flags) -- sets a per-player flags value at
//     0xB36210 + playerIndex*0x188 (the SAME real per-player struct base already
//     used elsewhere in this file for the menu-active gate bit) -- real call uses
//     flags=0x10.
using SetDvarByNameFn = void(__cdecl*)(const char* dvarName, int value);
SetDvarByNameFn const SetDvarByName = reinterpret_cast<SetDvarByNameFn>(0x005396b0);

using SetPlayerMenuFlagsFn = void(__cdecl*)(int playerIndex, int flags);
SetPlayerMenuFlagsFn const SetPlayerMenuFlags = reinterpret_cast<SetPlayerMenuFlagsFn>(0x005293c0);

// FUN_004adc60(filePath) -- real "find/load a menuList asset by file path" function,
// confirmed via raw disassembly of a real call site (FUN_004856b0, loading
// "ui/hud.txt"/"ui/patch_hud.txt"): only reads its FIRST stack arg (the path
// string) despite the caller also pushing a second value (0x7) that this specific
// function's own body never touches -- __cdecl (plain RET, no operand), single
// real argument. Returns a MenuList-shaped pointer (count at +4, array at +8,
// matching OpenAssetTools' own MenuList{int menuCount; menuDef_t** menus;} struct).
using FindOrLoadMenuListFn = void*(__cdecl*)(const char* menuListPath);
FindOrLoadMenuListFn const FindOrLoadMenuList = reinterpret_cast<FindOrLoadMenuListFn>(0x004adc60);

// FUN_0050a350(ctx, menuList, flag) -- the real registration function: iterates a
// MenuList's menus, and for each one NOT ALREADY in the registry (checked via
// FUN_00486990, the same search FUN_00544a50 itself uses), appends its pointer to
// the registry array (ctx+0x38) and increments the count (ctx+0xa38) -- the exact
// write FindConstantRefs/DescribeRefs couldn't locate statically. Found by fully
// decompiling FUN_004856b0 (real UI boot-time init code) and recognizing this
// exact shape. Confirmed __cdecl via its own plain RET. NOTE: silently SKIPS
// registering any menu whose name already exists in the registry -- does NOT
// replace/override an existing entry, so this only helps for uniquely-named new
// menus, not same-name overrides of already-registered ones like
// pc_options_controls_ingame (see known_issues.md task #23 for that separate,
// still-open problem).
using RegisterMenuListFn = void(__cdecl*)(void* menuContext, void* menuList, int flag);
RegisterMenuListFn const RegisterMenuList = reinterpret_cast<RegisterMenuListFn>(0x0050a350);

enum class ZoneLoadTestStage { WaitingForCombo, Loaded, Opened };
ZoneLoadTestStage g_zoneLoadTestStage = ZoneLoadTestStage::WaitingForCombo;
DWORD g_zoneLoadTestHoldStartMs = 0;
DWORD g_zoneLoadTestLoadedMs = 0;

// DIAGNOSTIC ONLY (2026-07-17, task #23) -- dumps the real "registered menu" array
// FUN_00486990 searches by name (array base ctx+0x38, count at ctx+0xa38 -- both
// confirmed via decompile; since DAT_01c00458 is a fixed global not a runtime
// pointer, these resolve to fixed absolute addresses 0x01c00490/0x01c00e90). Each
// slot is a pointer to a menuDef-like struct; the name string pointer lives at
// +4 within that struct (matches FUN_00486990's own `*(entryPtr+4)` dereference).
// Used to empirically observe whether/when our own loaded zone's menus actually
// appear in this registry, since static analysis couldn't find the write/insert
// site (likely another register-passed-arg function, same recurring obstacle all
// session).
constexpr uintptr_t kMenuRegistryArrayBase = 0x01c00490;
constexpr uintptr_t kMenuRegistryCountAddr = 0x01c00e90;

bool LooksLikeValidPointer(uintptr_t p)
{
    return p >= 0x00010000 && p < 0x7FFF0000;
}

void LogMenuRegistry(const char* tag)
{
    int32_t count = *reinterpret_cast<volatile int32_t*>(kMenuRegistryCountAddr);
    char buf[256];
    sprintf_s(buf, "[menureg-diag:%s] count=%d", tag, count);
    LogFromController(buf);

    int32_t safeCount = count;
    if (safeCount < 0) safeCount = 0;
    if (safeCount > 300) safeCount = 300; // sanity clamp -- diagnostic only, never trust raw count blindly
    for (int i = 0; i < safeCount; ++i) {
        uintptr_t entryPtr = *reinterpret_cast<volatile uintptr_t*>(kMenuRegistryArrayBase + static_cast<size_t>(i) * 4);
        if (!LooksLikeValidPointer(entryPtr)) {
            sprintf_s(buf, "[menureg-diag:%s] [%d] entryPtr=0x%08X (implausible, skipped)", tag, i, static_cast<unsigned>(entryPtr));
            LogFromController(buf);
            continue;
        }
        uintptr_t namePtr = *reinterpret_cast<volatile uintptr_t*>(entryPtr + 4);
        char nameBuf[64];
        nameBuf[0] = '\0';
        if (LooksLikeValidPointer(namePtr)) {
            const char* src = reinterpret_cast<const char*>(namePtr);
            size_t j = 0;
            for (; j < 63 && src[j] != '\0'; ++j) nameBuf[j] = src[j];
            nameBuf[j] = '\0';
        }
        sprintf_s(buf, "[menureg-diag:%s] [%d] entryPtr=0x%08X name=\"%s\"", tag, i, static_cast<unsigned>(entryPtr), nameBuf);
        LogFromController(buf);
    }
}

// FUN_0050a350's real per-menu body, confirmed via RAW DISASSEMBLY 2026-07-17 (not
// just decompile -- the decompile summary omitted a real step): for EVERY menu it
// processes, BEFORE ever touching the registry array, it calls
// FindOrLoadAsset(0x1a /*asset type: menu*/, name, 1) -- the SAME generic, thread-
// safe "find-or-load asset by name" function FindOrLoadMenuList itself calls with
// type 0x19 for menuLists. ONLY THEN does it check FUN_00486990 (already-registered?)
// and append if not found. A same-name OVERRIDE attempt (previous version of this
// function, same day) skipped this interning call entirely, then found a live black-
// screen flash when overwriting an already-registered slot in place. Root cause
// belief, not yet proven: FindOrLoadAsset does its own name-keyed lookup
// (FUN_00585400 internally) and, like any interning/asset-cache system, almost
// certainly hands back the EXISTING cached entry for an already-registered name
// rather than adopting new content under it -- meaning same-name override fights the
// engine's own asset pool, which a raw registry-array write bypasses entirely,
// leaving the pool and the array pointing at different objects for the same name.
// User decision 2026-07-17: do NOT pursue same-name override further. Register only
// under names that don't already exist (this function no longer has an override
// branch at all -- an existing name is left untouched, logged, nothing more); the
// "copy all default menus, ours become effective" plan's real mechanism is unique
// internal names + finding/patching whatever real call sites reference the original
// names, not same-slot replacement.
using FindOrLoadAssetFn = void*(__cdecl*)(int assetType, const char* name, int flag);
FindOrLoadAssetFn const FindOrLoadAsset = reinterpret_cast<FindOrLoadAssetFn>(0x004ff000);
constexpr int kAssetTypeMenu = 0x1a;

void RegisterMenu(void* menuDefPtr)
{
    uintptr_t entryPtr = reinterpret_cast<uintptr_t>(menuDefPtr);
    if (!LooksLikeValidPointer(entryPtr)) return;
    uintptr_t namePtr = *reinterpret_cast<uintptr_t*>(entryPtr + 4);
    if (!LooksLikeValidPointer(namePtr)) return;
    const char* name = reinterpret_cast<const char*>(namePtr);

    char buf[192];
    FindOrLoadAsset(kAssetTypeMenu, name, 1);

    // Real cap is 0x280 (640), confirmed via disassembly (CMP ...,0x280) -- earlier
    // version of this file used 0x27f, an off-by-one from guessing rather than
    // reading the real compare. Real code logs an error past this but still proceeds
    // to write; we choose to just refuse instead, since going out of bounds on
    // purpose is not something to replicate.
    int32_t count = *reinterpret_cast<volatile int32_t*>(kMenuRegistryCountAddr);
    if (count < 0) count = 0;
    if (count > 0x280) count = 0x280;

    for (int32_t i = 0; i < count; ++i) {
        uintptr_t existingPtr = *reinterpret_cast<uintptr_t*>(kMenuRegistryArrayBase + static_cast<size_t>(i) * 4);
        if (!LooksLikeValidPointer(existingPtr)) continue;
        uintptr_t existingNamePtr = *reinterpret_cast<uintptr_t*>(existingPtr + 4);
        if (!LooksLikeValidPointer(existingNamePtr)) continue;
        if (_stricmp(reinterpret_cast<const char*>(existingNamePtr), name) == 0) {
            sprintf_s(buf, "[menureg] \"%s\" already registered at slot %d (0x%08X) -- leaving it alone", name, i, static_cast<unsigned>(existingPtr));
            LogFromController(buf);
            return;
        }
    }

    if (count >= 0x280) {
        sprintf_s(buf, "[menureg] registry full (0x280), cannot append \"%s\"", name);
        LogFromController(buf);
        return;
    }
    uintptr_t* appendSlot = reinterpret_cast<uintptr_t*>(kMenuRegistryArrayBase + static_cast<size_t>(count) * 4);
    *appendSlot = entryPtr;
    *reinterpret_cast<volatile int32_t*>(kMenuRegistryCountAddr) = count + 1;
    sprintf_s(buf, "[menureg] \"%s\" appended at new slot %d (0x%08X)", name, count, static_cast<unsigned>(entryPtr));
    LogFromController(buf);
}

// Iterates a loaded MenuList (menuCount at +4, menuDef_t** menus at +8 -- matches
// OpenAssetTools' own MenuList{int menuCount; menuDef_t** menus;} struct, same shape
// FUN_0050a350 itself walks) and registers every menu it defines under its own name.
void RegisterLoadedMenuList(void* menuList)
{
    if (menuList == nullptr) return;
    uintptr_t base = reinterpret_cast<uintptr_t>(menuList);
    int32_t menuCount = *reinterpret_cast<int32_t*>(base + 4);
    void** menus = *reinterpret_cast<void***>(base + 8);
    char buf[160];
    if (menuCount <= 0 || menuCount > 2000 || menus == nullptr) {
        sprintf_s(buf, "[menureg] implausible MenuList (count=%d, menus=0x%08X), aborting",
            menuCount, static_cast<unsigned>(reinterpret_cast<uintptr_t>(menus)));
        LogFromController(buf);
        return;
    }
    sprintf_s(buf, "[menureg] MenuList has %d menu(s)", menuCount);
    LogFromController(buf);
    for (int32_t i = 0; i < menuCount; ++i) {
        if (menus[i] == nullptr) continue;
        RegisterMenu(menus[i]);
    }
}
} // namespace -- closes the one opened above ZoneLoadEntry (was previously closed
  // after the old blocking scan function; that function got replaced by
  // StartMenuDefScan/TickMenuDefScan below, each in their own separate namespace)

// DIAGNOSTIC ONLY (2026-07-17, task #23) -- FUN_004ca310 loads our zone's data
// safely but confirmed live (before/after registry dump) NOT to register it into
// FUN_00486990's searchable array. Since the real registration function wasn't
// found statically (register-passed-arg obstacle, same recurring wall all
// session), this scans committed, readable process memory for a SECOND menuDef-like
// structure whose name matches our target but whose address differs from the known
// original -- our own loaded copy, wherever the zone loader actually put it.
//
// REWRITTEN 2026-07-17 after a live hang: the first version used one __try/__except
// PER 4-BYTE ADDRESS across the whole scan -- correctness-wise fine (SEH did catch
// faults, per the log), but the sheer per-iteration SEH setup/teardown cost across
// potentially gigabytes of memory made the whole scan run far too slowly on the
// game's own thread, freezing it for an extended period (force-closed live rather
// than finishing). Two real fixes, not one: (1) resumable, budgeted across many
// ticks (kBytesPerTick of address space per call, driven from the always-running
// menu tick) instead of one blocking call, so no single frame ever does more than a
// small bounded slice of work; (2) coarse-grained SEH -- ONE __try/__except per
// slice (up to kBytesPerTick), not per address, cutting SEH overhead by ~6+ orders
// of magnitude. A fault anywhere in a slice abandons just that slice (resumes at
// the next slice/region boundary), not the whole scan -- an acceptable tradeoff for
// a debug diagnostic.
namespace {
struct MenuDefScanState {
    bool active = false;
    uintptr_t currentAddr = 0x00010000;
    uintptr_t regionEnd = 0; // 0 = need a fresh VirtualQuery for the next region
    bool currentRegionReadable = false;
    int found = 0;
    size_t regionsScanned = 0;
    size_t bytesScanned = 0;
    const char* targetName = nullptr;
    size_t targetLen = 0;
    uintptr_t excludeAddr = 0;
};
MenuDefScanState g_menuDefScan;

constexpr uintptr_t kScanCeiling = 0x7FFF0000;
constexpr size_t kMaxRegionSize = 16 * 1024 * 1024;
constexpr size_t kBytesPerTick = 2 * 1024 * 1024; // ~2MB of address space per call
} // namespace

void StartMenuDefScan(const char* targetName, uintptr_t excludeAddr)
{
    g_menuDefScan = MenuDefScanState{};
    g_menuDefScan.active = true;
    g_menuDefScan.targetName = targetName;
    g_menuDefScan.targetLen = strlen(targetName);
    g_menuDefScan.excludeAddr = excludeAddr;
    char buf[256];
    sprintf_s(buf, "[menuscan-diag] starting incremental scan for \"%s\" (excluding known original 0x%08X)",
        targetName, static_cast<unsigned>(excludeAddr));
    LogFromController(buf);
}

// Call every tick while a scan is active. Processes at most kBytesPerTick of address
// space then returns, resuming from where it left off next call. Returns false once
// the scan is finished (nothing more to do -- safe to stop calling).
bool TickMenuDefScan()
{
    if (!g_menuDefScan.active) return false;
    char buf[256];
    size_t processedThisTick = 0;

    while (processedThisTick < kBytesPerTick) {
        if (g_menuDefScan.currentAddr >= kScanCeiling) {
            sprintf_s(buf, "[menuscan-diag] scan complete: %d candidate(s), %zu regions, %zu MB scanned",
                g_menuDefScan.found, g_menuDefScan.regionsScanned, g_menuDefScan.bytesScanned / (1024 * 1024));
            LogFromController(buf);
            g_menuDefScan.active = false;
            return false;
        }

        if (g_menuDefScan.regionEnd == 0) {
            MEMORY_BASIC_INFORMATION mbi;
            if (VirtualQuery(reinterpret_cast<LPCVOID>(g_menuDefScan.currentAddr), &mbi, sizeof(mbi)) == 0) {
                LogFromController("[menuscan-diag] VirtualQuery failed, stopping scan");
                g_menuDefScan.active = false;
                return false;
            }
            uintptr_t regionBase = reinterpret_cast<uintptr_t>(mbi.BaseAddress);
            uintptr_t regionEndAddr = regionBase + mbi.RegionSize;
            if (regionEndAddr <= g_menuDefScan.currentAddr) {
                LogFromController("[menuscan-diag] non-advancing region, stopping scan");
                g_menuDefScan.active = false;
                return false;
            }
            g_menuDefScan.currentRegionReadable = (mbi.State == MEM_COMMIT)
                && ((mbi.Protect & (PAGE_READWRITE | PAGE_READONLY | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE)) != 0)
                && ((mbi.Protect & PAGE_GUARD) == 0)
                && (mbi.RegionSize <= kMaxRegionSize);
            g_menuDefScan.regionEnd = regionEndAddr;
            if (g_menuDefScan.currentRegionReadable) {
                g_menuDefScan.regionsScanned++;
                g_menuDefScan.bytesScanned += mbi.RegionSize;
            }
        }

        if (!g_menuDefScan.currentRegionReadable) {
            g_menuDefScan.currentAddr = g_menuDefScan.regionEnd;
            g_menuDefScan.regionEnd = 0;
            continue;
        }

        uintptr_t sliceEnd = g_menuDefScan.currentAddr + (kBytesPerTick - processedThisTick);
        if (sliceEnd > g_menuDefScan.regionEnd) sliceEnd = g_menuDefScan.regionEnd;

        uintptr_t p = g_menuDefScan.currentAddr;
        __try {
            for (; p + 8 <= sliceEnd; p += 4) {
                uintptr_t candidate = *reinterpret_cast<volatile uintptr_t*>(p);
                if (candidate == g_menuDefScan.excludeAddr || !LooksLikeValidPointer(candidate)) continue;
                uintptr_t namePtr = *reinterpret_cast<volatile uintptr_t*>(candidate + 4);
                if (!LooksLikeValidPointer(namePtr)) continue;
                const char* s = reinterpret_cast<const char*>(namePtr);
                bool match = true;
                for (size_t i = 0; i <= g_menuDefScan.targetLen; ++i) {
                    if (s[i] != g_menuDefScan.targetName[i]) { match = false; break; }
                }
                if (match) {
                    sprintf_s(buf, "[menuscan-diag] CANDIDATE at 0x%08X (found at scan offset 0x%08X)",
                        static_cast<unsigned>(candidate), static_cast<unsigned>(p));
                    LogFromController(buf);
                    g_menuDefScan.found++;
                }
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            // Fault somewhere in this slice -- abandon just the rest of this slice,
            // not the whole scan.
            p = sliceEnd;
        }

        processedThisTick += (p - g_menuDefScan.currentAddr);
        g_menuDefScan.currentAddr = p;
        if (g_menuDefScan.currentAddr >= g_menuDefScan.regionEnd) {
            g_menuDefScan.regionEnd = 0;
        }

        if (g_menuDefScan.found >= 20) {
            LogFromController("[menuscan-diag] 20 candidates found, stopping early");
            g_menuDefScan.active = false;
            return false;
        }
    }
    return true; // more to do -- call again next tick
}

// UPDATED AGAIN 2026-07-17: now tests the SAME-NAME OVERRIDE path specifically --
// loads a modified copy of the REAL pc_options_controls_ingame.menu (marker text
// "OPTIONS [MODDED]" in place of the real "@MENU_OPTIONS_UPPER_CASE" localized
// string) and registers it via RegisterOrOverrideMenuList instead of the real
// FUN_0050a350, which would silently skip it since that name already exists. This is
// the generic mechanism the "copy all default menus into our own zone, our copies
// become effective, real ui.ff untouched on disk" plan (user, 2026-07-17) depends on
// -- proving it works for ONE already-registered real name proves it'll work at
// whatever scale we later load. The combo still opens the menu directly for a fast
// isolated look, but the real test is backing out (B/ESC) afterward and navigating
// there NORMALLY (pause -> Options -> Controller) to confirm the override is visible
// through the game's own real navigation, not just our direct-open shortcut.
void InjectZoneLoadDebugTest()
{
    if (g_zoneLoadTestStage != ZoneLoadTestStage::WaitingForCombo) return;

    unsigned short buttons;
    unsigned char leftTrigger, rightTrigger;
    if (!Controller_GetRawButtonsAndTriggers(buttons, leftTrigger, rightTrigger)) return;

    // Switched from LB+RB+Back (2026-07-17): the pipeline test actually WORKED --
    // menu genuinely opened -- but closed itself a split second later. Real cause:
    // Back is also wired to synthesize a real TAB keypress (InjectControllerScoreboard),
    // and this session's own earlier decompile of the real key handler (FUN_00541020)
    // found logic that reinterprets certain low keycodes -- TAB included -- as ESC
    // under specific conditions, closing whatever menu is open. Dropping Back from
    // the combo entirely removes that interaction. LB+RB alone (no third button) is
    // still obscure enough not to hit by accident during normal play.
    bool comboHeld = (buttons & kXI_LEFT_SHOULDER) != 0 && (buttons & kXI_RIGHT_SHOULDER) != 0;

    if (!comboHeld) {
        g_zoneLoadTestHoldStartMs = 0;
        return;
    }
    if (g_zoneLoadTestHoldStartMs == 0) {
        g_zoneLoadTestHoldStartMs = GetTickCount();
        return;
    }
    if (GetTickCount() - g_zoneLoadTestHoldStartMs < 2000) return;

    // STRATEGY CHANGE 2026-07-17: same-name override abandoned (see RegisterMenu's
    // own comment above for the full reasoning -- fights the engine's real asset-
    // interning pool, likely cause of a live black-screen flash). Our test menu was
    // renamed internally to "controller_mod_options_controls" (a unique name, does
    // NOT collide with the real "pc_options_controls_ingame") specifically so this
    // now exercises RegisterMenu's plain APPEND path -- the goal of THIS test is
    // narrower than before: confirm the disassembly-verified interning-call fix
    // doesn't itself cause instability when registering a large, real-content-derived
    // menu (materials + 41 items), isolated from the same-name-override question
    // entirely. Finding/patching whatever real call site opens
    // "pc_options_controls_ingame" so it targets our unique name instead is separate,
    // not-yet-started follow-up work.
    LogFromController("[zoneload-test] LB+RB held 2s -- dumping menu registry BEFORE load");
    LogMenuRegistry("before");

    LogFromController("[zoneload-test] loading zone \"roundtrip\" (contains controller_mod_options_controls)");
    ZoneLoadEntry entry{ "roundtrip", 4, 0 };
    LoadZones(&entry, 1, 0);
    LogFromController("[zoneload-test] FUN_004ca310 returned without crashing");

    LogMenuRegistry("after-load");

    LogFromController("[zoneload-test] calling FUN_004adc60(\"ui/pc_options_controls_ingame.menu\")");
    void* menuList = FindOrLoadMenuList("ui/pc_options_controls_ingame.menu");
    char buf[128];
    sprintf_s(buf, "[zoneload-test] FUN_004adc60 returned 0x%08X", static_cast<unsigned>(reinterpret_cast<uintptr_t>(menuList)));
    LogFromController(buf);

    if (menuList != nullptr) {
        LogFromController("[zoneload-test] calling RegisterLoadedMenuList (append-only, with interning fix)");
        RegisterLoadedMenuList(menuList);
        LogFromController("[zoneload-test] RegisterLoadedMenuList returned without crashing");

        LogMenuRegistry("after-register");
        // Deliberately no open/render step here -- our own synthetic
        // cl_paused+flags+OpenMenuByName trigger path is independently confirmed
        // broken (garbled render) regardless of content, so it's not a useful way to
        // visually verify this. This test's job is just "does register-with-
        // interning stay crash/flash-free"; visual confirmation waits on the real
        // call-site-redirect work.
    } else {
        LogFromController("[zoneload-test] FUN_004adc60 returned null, skipping register");
    }

    g_zoneLoadTestLoadedMs = GetTickCount();
    g_zoneLoadTestStage = ZoneLoadTestStage::Loaded;
}

// ---- Boot-time zone splice: auto-load the extended button-glyph font (2026-07-19,
// task #6 UI scope / controller glyphs) ------------------------------------------
//
// Supersedes the LB+RB manual zoneload-test above for real deployment: that trigger
// proved LoadZones (FUN_004ca310) can be called safely and that a real custom zone
// loads without crashing, but it's a manual, session-only debug trigger, not
// something a real player would ever hit. This hooks FUN_004ca310 itself and
// splices one extra entry into the REAL boot-time zone queue FUN_00679680 already
// builds and processes -- so this project's extended font zone
// (assets/zones/bigfont_ext.ff, a copy of the real fonts/bigfont/gamefonts_pc pair
// plus one new glyph codepoint, see re_notes/ui_assets.md for the full
// build-pipeline trail) loads automatically through the exact same real code path
// every other real zone loads through, with no separate call of our own needed.
//
// FUN_00679680 calls FUN_004ca310 TWICE (confirmed via disassembly, both call
// sites within this one function): Call 1 (return address 0x006796EB) is
// CONDITIONAL, gated on a global; Call 2 (return address 0x006797C2) is
// UNCONDITIONAL, the function's natural fall-through -- always runs. Splicing
// into Call 2 only, gated on an EXACT return-address match (not a range), so
// every other real caller of this same function (FUN_0067a690, FUN_00481e50,
// FUN_0053cbc0, and Call 1 above) passes through completely untouched -- this
// hook only ever alters the one specific call it was pressure-tested against.
//
// Real entry format confirmed via disassembly at all 4 real callers:
// {namePtr_or_int, typeFlag, 0} triples, 12 bytes/entry. The real caller's local
// array is `int[30]` (10 entries x 3 ints) -- this splice trusts the REAL `count`
// argument (how many of those 10 slots are currently populated) and appends
// directly at index `count`, rather than scanning for a null-name "unused slot"
// sentinel: the caller's array is an uninitialized stack local, and a
// sentinel-based scan was explicitly flagged as unconfirmed/unsafe in the
// pressure-testing pass (re_notes/ui_assets.md, "Boot-zone splice: pressure-tested,
// conditional GO"). Appending at `count` and passing `count+1` through needs no
// sentinel assumption at all -- only the already-confirmed entry layout and the
// already-confirmed 10-slot physical capacity, with a hard bounds check as the
// fail-safe (never splice, just forward unmodified, if the array is already full).
//
// Idempotency: MinHook detours the function once, process-wide, for its entire
// lifetime -- `g_bootZoneSpliced` exists only to stop a SECOND matching call
// (e.g. a hypothetical retry of FUN_00679680 itself) from appending a second
// duplicate entry, not to protect against a double-hook-install (MH_CreateHook is
// only ever called once, from InstallAnalogInputHooks).
namespace {
LoadZonesFn g_origLoadZonesForBootSplice = nullptr;
constexpr uintptr_t kBootZoneSpliceReturnAddr = 0x006797C2; // FUN_00679680 Call 2 (unconditional)
constexpr int kBootZoneArrayCapacity = 10; // int[30] local == 10 entries * 3 ints, confirmed
bool g_bootZoneSpliced = false; // idempotency guard -- splice at most once per process

void __cdecl Hook_LoadZonesForBootSplice(void* zoneArray, int count, int mode)
{
    uintptr_t returnAddr = reinterpret_cast<uintptr_t>(_ReturnAddress());
    if (!g_bootZoneSpliced && returnAddr == kBootZoneSpliceReturnAddr &&
        zoneArray != nullptr && count >= 0 && count < kBootZoneArrayCapacity) {
        ZoneLoadEntry* entries = reinterpret_cast<ZoneLoadEntry*>(zoneArray);
        entries[count].name = "bigfont_ext"; // bare zone name, no path/extension --
                                              // matches the confirmed real convention
                                              // (the existing "roundtrip" zoneload-
                                              // test uses the same bare form, resolved
                                              // against zone/english/<name>.ff, where
                                              // this file is physically placed)
        entries[count].flags = 1; // matches this exact batch's real neighboring
                                   // entries' typeFlag for a plain zone name
        entries[count].unused = 0;
        g_bootZoneSpliced = true; // claim the splice regardless of what happens
                                   // below -- never retry into a buffer that may
                                   // already be consumed
        LogFromController("[boot-zone-splice] spliced assets/zones/bigfont_ext into the real boot zone queue");
        g_origLoadZonesForBootSplice(zoneArray, count + 1, mode);
        return;
    }
    // Every other real call site -- or a full/invalid array on this one -- passes
    // through completely unmodified. Fail-safe, not a silent skip: still forwards
    // to the real function either way, exactly as if this hook didn't exist.
    g_origLoadZonesForBootSplice(zoneArray, count, mode);
}
} // namespace

// ---- DEBUG-ONLY: live dump of the real Font struct for fonts/bigFont (2026-07-19,
// task #6 UI scope / glyphs, follow-up to the boot-splice crash) --------------------
//
// Read-only diagnostic, zero mutation, zero hooking of anything boot-related --
// deliberately the safest possible next step after the boot-splice crash (which
// intercepted the zone-LOADING path itself). This instead calls the same real
// FindOrLoadFont function the engine's own boot code already calls, well after boot
// has finished, from the always-safe WndProc/SetTimer tick -- since fonts/bigFont is
// already loaded and asset-interned by name at this point, this call returns the
// SAME cached Font* the real boot process created, it does not reload or duplicate
// anything. Purpose: verify this session's Ghidra-confirmed Font/Glyph struct
// layout against REAL live memory before ever attempting to mutate it.
//
// FUN_0045d040 = FindOrLoadFont, thin __cdecl(const char* path) wrapper hardcoding
// assetType 0x18 into FUN_004ff000 -- same calling-convention class as the already-
// proven FindOrLoadMenuList (0x004adc60) above, not a register-arg function like
// FUN_0061f6f0.
//
// Font_s/Glyph_s layout below is exactly what this session's dedicated Ghidra pass
// confirmed (cross-validated two independent ways: direct decompile of the font
// load body FUN_005021c0's material/glowMaterial writes at +0xC/+0x10, AND the
// render-time glyph-lookup function FUN_0047dfa0's direct-index math using +0x8
// (count) and +0x14 (glyph array), stride 0x18/24 bytes per glyph). NOT yet
// independently re-confirmed by this project's own Ghidra project -- this diagnostic
// exists specifically to catch a live mismatch before it could cause a bad write.
namespace {
using FindOrLoadFontFn = void*(__cdecl*)(const char* fontPath);
FindOrLoadFontFn const FindOrLoadFont = reinterpret_cast<FindOrLoadFontFn>(0x0045d040);

#pragma pack(push, 1)
struct DiagGlyph
{
    unsigned short letter; // +0x00
    signed char x0;        // +0x02
    signed char y0;        // +0x03
    unsigned char dx;      // +0x04 -- advance width, confirmed via the render-time
                             // measure loop's direct *(byte*)(glyph+4) read
    unsigned char pixelWidth;  // +0x05
    unsigned char pixelHeight; // +0x06
    unsigned char _pad07;      // +0x07
    float s0, t0, s1, t1;      // +0x08 .. +0x17
};
struct DiagFont
{
    const char* fontName; // +0x00
    int pixelHeight;       // +0x04
    int glyphCount;        // +0x08
    void* material;        // +0x0C
    void* glowMaterial;     // +0x10
    DiagGlyph* glyphs;      // +0x14
};
#pragma pack(pop)
static_assert(sizeof(DiagGlyph) == 0x18, "DiagGlyph must match the confirmed 24-byte real glyph stride");

enum class FontDiagStage { WaitingForCombo, Done };
FontDiagStage g_fontDiagStage = FontDiagStage::WaitingForCombo;
DWORD g_fontDiagHoldStartMs = 0;
} // namespace

// Reuses the same obscure LB+RB-held-2s convention as the zoneload-test above (that
// test is disabled/not wired into the live tick, so no collision) -- deliberately
// impossible to trigger by accident during normal play.
void InjectFontStructDebugTest()
{
    if (g_fontDiagStage != FontDiagStage::WaitingForCombo) return;

    unsigned short buttons;
    unsigned char leftTrigger, rightTrigger;
    if (!Controller_GetRawButtonsAndTriggers(buttons, leftTrigger, rightTrigger)) return;

    bool comboHeld = (buttons & kXI_LEFT_SHOULDER) != 0 && (buttons & kXI_RIGHT_SHOULDER) != 0;
    if (!comboHeld) {
        g_fontDiagHoldStartMs = 0;
        return;
    }
    if (g_fontDiagHoldStartMs == 0) {
        g_fontDiagHoldStartMs = GetTickCount();
        return;
    }
    if (GetTickCount() - g_fontDiagHoldStartMs < 2000) return;

    g_fontDiagStage = FontDiagStage::Done; // fire once per session regardless of outcome below

    LogFromController("[font-struct-diag] LB+RB held 2s -- calling FindOrLoadFont(\"fonts/bigfont\")");
    void* rawFont = FindOrLoadFont("fonts/bigfont");
    char buf[256];
    if (!LooksLikeValidPointer(reinterpret_cast<uintptr_t>(rawFont))) {
        sprintf_s(buf, "[font-struct-diag] FindOrLoadFont returned implausible pointer 0x%08X -- aborting dump",
            static_cast<unsigned>(reinterpret_cast<uintptr_t>(rawFont)));
        LogFromController(buf);
        return;
    }
    DiagFont* font = reinterpret_cast<DiagFont*>(rawFont);
    sprintf_s(buf, "[font-struct-diag] Font* = 0x%08X, name=0x%08X pixelHeight=%d glyphCount=%d material=0x%08X glowMaterial=0x%08X glyphs=0x%08X",
        static_cast<unsigned>(reinterpret_cast<uintptr_t>(font)),
        static_cast<unsigned>(reinterpret_cast<uintptr_t>(font->fontName)),
        font->pixelHeight, font->glyphCount,
        static_cast<unsigned>(reinterpret_cast<uintptr_t>(font->material)),
        static_cast<unsigned>(reinterpret_cast<uintptr_t>(font->glowMaterial)),
        static_cast<unsigned>(reinterpret_cast<uintptr_t>(font->glyphs)));
    LogFromController(buf);

    if (LooksLikeValidPointer(reinterpret_cast<uintptr_t>(font->fontName))) {
        sprintf_s(buf, "[font-struct-diag] fontName string = \"%.63s\"", font->fontName);
        LogFromController(buf);
    }

    // Sanity bounds before ever indexing the glyph array -- a plausible font has
    // somewhere between 96 (bare minimum, hard schema requirement) and a few
    // hundred glyphs (the real bigFont's atlas covers extended Latin, ~191 known).
    if (!LooksLikeValidPointer(reinterpret_cast<uintptr_t>(font->glyphs)) ||
        font->glyphCount < 96 || font->glyphCount > 1000) {
        sprintf_s(buf, "[font-struct-diag] glyphs ptr or glyphCount looks implausible (count=%d) -- not dumping entries, struct layout may be WRONG",
            font->glyphCount);
        LogFromController(buf);
        return;
    }

    // Dump a few direct-indexed entries (codepoints 'A'=0x41, 'E'=0x45 -- common
    // interact-prompt letters) plus the first 2 sorted-tail entries beyond the
    // required 96, to confirm both the direct-index region AND the sorted-extra
    // region look sane.
    auto dumpGlyph = [&](int idx, const char* label) {
        if (idx < 0 || idx >= font->glyphCount) return;
        const DiagGlyph& g = font->glyphs[idx];
        char b2[200];
        sprintf_s(b2, "[font-struct-diag] glyph[%d] (%s): letter=0x%02X dx=%u pxW=%u pxH=%u s0=%.4f t0=%.4f s1=%.4f t1=%.4f",
            idx, label, g.letter, g.dx, g.pixelWidth, g.pixelHeight, g.s0, g.t0, g.s1, g.t1);
        LogFromController(b2);
    };
    dumpGlyph('A' - 0x20, "'A', direct-indexed");
    dumpGlyph('E' - 0x20, "'E', direct-indexed");
    if (font->glyphCount > 96) dumpGlyph(96, "first sorted-extra entry");
    if (font->glyphCount > 97) dumpGlyph(97, "second sorted-extra entry");

    LogFromController("[font-struct-diag] dump complete -- compare against re_notes/known_issues.md issue #6/#31 Font struct notes before attempting any patch");
}

// ---- DEBUG-ONLY: live glyph-array patch test, MECHANISM ONLY (2026-07-19) --------
//
// Tests the "reallocate + repoint" patch mechanism itself, deliberately isolated
// from the still-unsolved texture/material problem (see ui_assets.md's 2026-07-19
// fork-research section, item 5 -- Font_s has only ONE material for the whole font,
// so a new glyph can't yet get its own real pixel content without more work).
// Instead of real new pixel content, this test BORROWS an existing glyph's UV rect
// (a copy of 'A''s s0/t0/s1/t1) for the new codepoint -- if the mechanism works,
// looking up codepoint 0x81 will render as a visible 'A' (wrong picture, right
// mechanism), proving the array-growth+repoint patch is sound before ever touching
// the harder graphics problem. Deliberately gated behind its own separate combo
// (not the read-only diagnostic's LB+RB) and fires only once per session.
//
// Safety ordering, deliberate: writes font->glyphs (the pointer) BEFORE
// font->glyphCount. If this engine turns out to have any concurrent reader (not
// expected -- no threading evidence found anywhere in the font boot-registration
// chain -- but not proven impossible either), a reader using the OLD glyphCount
// with the NEW glyphs pointer simply ignores the extra entry (safe); the reverse
// order (count first) would let a reader see the new, larger count while glyphs
// still pointed at the old, too-small array -- a real out-of-bounds read. This
// project's own proxy DLL heap (`new[]`) is used for the replacement array, never
// the engine's own zone/pool memory -- no zone data is touched by this patch.
namespace {
enum class FontPatchStage { WaitingForCombo, Done };
FontPatchStage g_fontPatchStage = FontPatchStage::WaitingForCombo;
DWORD g_fontPatchHoldStartMs = 0;
} // namespace

void InjectFontGlyphPatchTest()
{
    if (g_fontPatchStage != FontPatchStage::WaitingForCombo) return;

    unsigned short buttons;
    unsigned char leftTrigger, rightTrigger;
    if (!Controller_GetRawButtonsAndTriggers(buttons, leftTrigger, rightTrigger)) return;

    // Distinct combo from the read-only diagnostic (LB+RB) so the two tests can't
    // be confused for each other or accidentally chained -- LB+RB+A, still obscure.
    bool comboHeld = (buttons & kXI_LEFT_SHOULDER) != 0 && (buttons & kXI_RIGHT_SHOULDER) != 0 &&
        (buttons & kXI_A) != 0;
    if (!comboHeld) {
        g_fontPatchHoldStartMs = 0;
        return;
    }
    if (g_fontPatchHoldStartMs == 0) {
        g_fontPatchHoldStartMs = GetTickCount();
        return;
    }
    if (GetTickCount() - g_fontPatchHoldStartMs < 2000) return;

    g_fontPatchStage = FontPatchStage::Done; // fire once regardless of outcome below

    LogFromController("[font-patch-test] LB+RB+A held 2s -- attempting glyph-array patch on fonts/bigfont");
    void* rawFont = FindOrLoadFont("fonts/bigfont");
    char buf[200];
    if (!LooksLikeValidPointer(reinterpret_cast<uintptr_t>(rawFont))) {
        LogFromController("[font-patch-test] FindOrLoadFont returned implausible pointer -- aborting");
        return;
    }
    DiagFont* font = reinterpret_cast<DiagFont*>(rawFont);
    if (!LooksLikeValidPointer(reinterpret_cast<uintptr_t>(font->glyphs)) ||
        font->glyphCount < 96 || font->glyphCount > 1000) {
        sprintf_s(buf, "[font-patch-test] glyphs ptr or glyphCount implausible (count=%d) -- aborting, struct layout may be wrong",
            font->glyphCount);
        LogFromController(buf);
        return;
    }

    const int oldCount = font->glyphCount;
    const unsigned short kNewCodepoint = 0x81;

    // Find insertion point in the sorted [96, oldCount) tail (matches FUN_0047dfa0's
    // real binary-search ordering, confirmed via the render-lookup fork -- codepoints
    // 0x20-0x7F are direct-indexed and must never move).
    int insertAt = oldCount; // default: append at the very end
    for (int i = 96; i < oldCount; ++i) {
        if (font->glyphs[i].letter > kNewCodepoint) { insertAt = i; break; }
        if (font->glyphs[i].letter == kNewCodepoint) {
            sprintf_s(buf, "[font-patch-test] codepoint 0x%02X already exists at index %d -- aborting, nothing to insert",
                kNewCodepoint, i);
            LogFromController(buf);
            return;
        }
    }

    DiagGlyph* newArray = new DiagGlyph[oldCount + 1];
    memcpy(newArray, font->glyphs, sizeof(DiagGlyph) * insertAt);
    // Borrowed UV rect: a real, valid existing glyph's texture coordinates ('A',
    // direct-indexed at 'A'-0x20), deliberately NOT new pixel content -- see the
    // big comment above this function for why.
    const DiagGlyph& borrowSource = font->glyphs['A' - 0x20];
    newArray[insertAt].letter = kNewCodepoint;
    newArray[insertAt].x0 = borrowSource.x0;
    newArray[insertAt].y0 = borrowSource.y0;
    newArray[insertAt].dx = borrowSource.dx;
    newArray[insertAt].pixelWidth = borrowSource.pixelWidth;
    newArray[insertAt].pixelHeight = borrowSource.pixelHeight;
    newArray[insertAt].s0 = borrowSource.s0;
    newArray[insertAt].t0 = borrowSource.t0;
    newArray[insertAt].s1 = borrowSource.s1;
    newArray[insertAt].t1 = borrowSource.t1;
    memcpy(newArray + insertAt + 1, font->glyphs + insertAt, sizeof(DiagGlyph) * (oldCount - insertAt));

    sprintf_s(buf, "[font-patch-test] built replacement array (%d -> %d entries), inserted codepoint 0x%02X at index %d, repointing live Font_s now",
        oldCount, oldCount + 1, kNewCodepoint, insertAt);
    LogFromController(buf);

    // Deliberate ordering -- see the big comment above this function.
    font->glyphs = newArray;
    font->glyphCount = oldCount + 1;
    // Old array intentionally leaked, not deleted -- freeing memory the real engine
    // might still hold a stray reference/iterator into (not proven impossible) is a
    // worse failure mode than a one-time small leak for a debug-only test. Revisit
    // if/when this becomes a real shipped feature rather than a mechanism test.

    LogFromController("[font-patch-test] patch applied -- if the mechanism is sound, any UI text containing byte 0x81 should now render as a visible (borrowed) 'A' glyph instead of missing/tofu. Compare against re_notes/known_issues.md before trusting this without a visual confirm.");
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
    InjectControllerFire();
    InjectControllerSprint();
    InjectControllerReload();
    InjectControllerWeaponNext();
    InjectControllerDpad();
    InjectControllerScoreboard();

    // Also called from InjectMenuInputTick (the WndProc hook, see below) -- kept here
    // too purely for redundancy/robustness. Calling it from both places is safe/
    // idempotent: g_startHeld debounces per real button edge regardless of which hook
    // happens to observe it first in a given frame.
    InjectControllerPauseMenu();
    InjectControllerMenuBack(); // same redundancy rationale as the pause-menu call above

    Rumble_Tick(); // task #17 -- gameplay-tick only, not the menu tick (rumble is a
                    // gameplay-feedback feature, not a UI one)
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
    // BUG FIX (2026-07-16, live report "B doesn't exit pause"): InjectControllerMenuBack
    // was only ever called from InjectAllControllerInput, which the comment block above
    // already documents as completely dead while genuinely paused (it lives on the
    // gameplay-simulation tick, which pause halts by design). This tick function is the
    // ONLY one confirmed to keep running during pause (WndProc subclass + SetTimer), same
    // reason InjectControllerPauseMenu is called from here -- B's ESC-forward needs the
    // same treatment Start's open/close already got, or it can never fire while a menu is
    // actually open, which is the one state it exists to handle.
    InjectControllerMenuBack();
    // D-pad/A menu-item navigation (task #22) needs the same always-running tick as
    // B's ESC-forward, for the same reason: the gameplay-simulation tick this would
    // otherwise share with InjectControllerDpad halts entirely during a genuine pause.
    InjectControllerMenuNav();
    // DISABLED for the v0.1.3 public pre-alpha build (2026-07-17): task #23's zone/
    // menu-injection debug trigger (LB+RB hold) is real, working test code, not a
    // finished feature -- it's gated behind an internal combo, has no player-facing
    // purpose yet, and the underlying live-injection approach is now known to be
    // unsafe for real menu content (see re_notes/known_issues.md issue #23). Left
    // defined below for when this resumes (the level-load-transition alternative),
    // just not wired into the live tick for a public build.
    // InjectZoneLoadDebugTest(); // DEBUG ONLY, task #23 -- see comment above its definition
    // TickMenuDefScan(); // DEBUG ONLY, task #23 -- no-op unless a scan is active (StartMenuDefScan called)

    // DEBUG ONLY, task #6/#31/#32 follow-up (2026-07-19) -- read-only Font-struct
    // dump, see comment above InjectFontStructDebugTest's definition. Deliberately
    // wired live (unlike the two debug calls above): this one never mutates
    // anything or intercepts the boot path, so it carries none of the risk that got
    // the zone-load test disabled for public builds.
    InjectFontStructDebugTest();
    // DEBUG ONLY, task #6 follow-up (2026-07-19) -- the glyph-array patch MECHANISM
    // test (borrowed UV, see comment above InjectFontGlyphPatchTest's definition).
    // Gated behind its own distinct LB+RB+A combo so it can never fire from the
    // same input as the read-only diagnostic above. This DOES mutate a live game
    // asset (though only this project's own heap, never zone memory) -- wired live
    // deliberately so it can actually be tested, same as every other debug trigger
    // in this file, all gated behind combos that can't be hit by accident.
    InjectFontGlyphPatchTest();
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

    // Sprint (L3) no longer hooks FUN_00644ed0/FUN_00643ce0 to force the raw pm_flags
    // bit -- superseded 2026-07-19 by driving the real +sprint kbutton_t (0xA98CCC)
    // directly via CallKbuttonDown/CallKbuttonUp from InjectControllerSprint(), same
    // technique as ADS/Reload/Fire. See the big comment block above InjectControllerSprint
    // for the full disassembly trail. Both hooks and their trampolines were removed
    // entirely (not just disabled) now that nothing calls them.

    // task #30 -- controlslinkto diagnostic (log-and-forward only, see comment above
    // Hook_ControlsLinkTo for the full disassembly-confirmed calling convention)
    MH_STATUS s5 = MH_CreateHook(reinterpret_cast<LPVOID>(kControlsLinkToAddr),
        &Hook_ControlsLinkTo, reinterpret_cast<LPVOID*>(&g_origControlsLinkTo));
    sprintf_s(buf, "[hooks] MH_CreateHook(controlslinkto @ 005d7f20) = %d", static_cast<int>(s5));
    LogFromController(buf);
    if (s5 == MH_OK) {
        MH_STATUS e5 = MH_EnableHook(reinterpret_cast<LPVOID>(kControlsLinkToAddr));
        sprintf_s(buf, "[hooks] MH_EnableHook(controlslinkto) = %d", static_cast<int>(e5));
        LogFromController(buf);
    }

    // task #30 follow-up (2026-07-19) -- missile-guidance per-frame angle-dispatch
    // diagnostic (log-and-forward only, see comment above Hook_MissileGuidanceDispatch
    // for the full GSC + disassembly trail). Plain __cdecl target (all 5 args on the
    // stack, confirmed via raw disassembly), same low-risk hook class as
    // Hook_ControlsLinkTo -- not a generic multi-signature dispatcher like the rumble
    // hooks that crashed the game earlier this session.
    MH_STATUS s6 = MH_CreateHook(reinterpret_cast<LPVOID>(kMissileGuidanceDispatchAddr),
        &Hook_MissileGuidanceDispatch, reinterpret_cast<LPVOID*>(&g_origMissileGuidanceDispatch));
    sprintf_s(buf, "[hooks] MH_CreateHook(missile-guidance-dispatch @ 004554d0) = %d", static_cast<int>(s6));
    LogFromController(buf);
    if (s6 == MH_OK) {
        MH_STATUS e6 = MH_EnableHook(reinterpret_cast<LPVOID>(kMissileGuidanceDispatchAddr));
        sprintf_s(buf, "[hooks] MH_EnableHook(missile-guidance-dispatch) = %d", static_cast<int>(e6));
        LogFromController(buf);
    }

    // TEMPORARILY DISABLED (2026-07-19) -- CONFIRMED LIVE CRASH. Game failed to
    // start with this hook active; proxy_d3d9.log shows the EXACT same crash
    // signature as the 2026-07-18 rumble-hook crash below: every hook (including
    // this one) installing successfully (all MH_OK/status 0), then an immediate
    // detach with ZERO gameplay-tick activity ever logged (no [stance-diag]
    // heartbeat at all, unlike a normal session) -- meaning the crash happens
    // during early boot, before the first gameplay frame. Notably, this hook's own
    // "[boot-zone-splice] spliced..." log line NEVER appears anywhere in the log
    // either, meaning the return-address-gated splice branch itself never even
    // ran -- the crash is happening either before FUN_00679680's Call 2 executes,
    // or the mere act of hooking FUN_004ca310 (even the plain-passthrough branch
    // every OTHER real caller takes) is unsafe in a way the static disassembly
    // review didn't catch. Disabling to isolate the cause and get a working build
    // back -- Hold Breath (added the same session) is untouched and NOT suspected,
    // since it only ever executes once gameplay ticks are already running, which
    // this log shows never happened. See known_issues.md issue #6's glyph section
    // for the live diagnosis in progress. Code kept, not deleted -- same precedent
    // as the rumble hook below.
    // MH_STATUS s7 = MH_CreateHook(reinterpret_cast<LPVOID>(0x004ca310),
    //     &Hook_LoadZonesForBootSplice, reinterpret_cast<LPVOID*>(&g_origLoadZonesForBootSplice));
    // sprintf_s(buf, "[hooks] MH_CreateHook(boot-zone-splice @ 004ca310) = %d", static_cast<int>(s7));
    // LogFromController(buf);
    // if (s7 == MH_OK) {
    //     MH_STATUS e7 = MH_EnableHook(reinterpret_cast<LPVOID>(0x004ca310));
    //     sprintf_s(buf, "[hooks] MH_EnableHook(boot-zone-splice) = %d", static_cast<int>(e7));
    //     LogFromController(buf);
    // }

    // TEMPORARILY DISABLED (2026-07-18) -- game failed to start after this was added;
    // proxy_d3d9.log shows every hook installing successfully (all MH_OK) then an
    // immediate detach with zero per-frame activity ever logged, meaning the crash
    // happens before the first gameplay frame -- before any real weapon-fired/damage
    // event could have fired. FUN_004895b0/FUN_0044cdb0 are GENERAL native notify
    // dispatchers (not weapon-fire/damage-specific) -- almost certainly called for
    // other, unrelated event types during engine init with a genuinely different
    // real argument count than the one call site (weapon_fired/damage) this hook's
    // fixed-parameter signature was confirmed against. Disabling to isolate the
    // cause -- see known_issues.md issue #24 for the live diagnosis in progress.
    // Rumble_Install(); // task #17 -- its own module, see rumble.h/.cpp
}
