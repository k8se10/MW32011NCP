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
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include "../third_party/minhook/include/MinHook.h"
#include "controller_input.h"
#include "mod_config.h"

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

    uint32_t out = 0;
    bool fireHeld = IsPhysicalHeld(g_buttonMap.fire, xiButtons, leftTrigger, rightTrigger);
    if (fireHeld) out |= 0x1;                                   // Fire (+attack)
    if (IsPhysicalHeld(g_buttonMap.melee, xiButtons, leftTrigger, rightTrigger)) out |= 0x4;       // Melee
    if (IsPhysicalHeld(g_buttonMap.tactical, xiButtons, leftTrigger, rightTrigger)) out |= 0x8000; // Tactical (smoke)
    if (IsPhysicalHeld(g_buttonMap.lethal, xiButtons, leftTrigger, rightTrigger)) out |= 0x4000;   // Lethal (frag)
    if (IsPhysicalHeld(g_buttonMap.jump, xiButtons, leftTrigger, rightTrigger)) out |= 0x400;      // Jump (+gostand)

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
//
// Max stamina/regen seconds now come from g_modConfig ([Sprint] in mw3ncp_config.ini,
// task #14) rather than being hardcoded. g_sprintStamina's own initializer below is a
// plain literal, NOT a read of g_modConfig -- global initialization order between two
// different .cpp files' statics is unspecified in C++, so reading g_modConfig here
// could run before or after its own default-member-initializers depending on link
// order. InstallAnalogInputHooks() (called from DllMain strictly after LoadModConfig())
// re-syncs this to the real configured value, which is what actually matters since no
// gameplay hook fires before DllMain returns.
float g_sprintStamina = 4.0f;
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
    if (GetDvarInt("player_sprintUnlimited") != 0) return g_sprintHeld && GetRealStance() == 0;
    return g_sprintHeld && GetRealStance() == 0 && !g_sprintWinded;
}

// ---- Investigating a REAL native sprint timer (2026-07-16) ------------------------
//
// Found via the sprint-meter HUD render function (FUN_005696d0/FUN_005695a0, which
// consume the real cg_sprintMeterFullColor/EmptyColor/DisabledColor dvars found via
// string search): both compute `fVar4 = (float)FUN_004b9350(...) / (float)FUN_007380e0()`
// and use that as the real meter fill FRACTION -- exactly the shape of a genuine
// current/max sprint-time ratio. If this is really what it looks like, it may let us
// read the game's own real stamina state directly (and inherit perk_sprintMultiplier /
// Extreme Conditioning overrides for free) instead of maintaining our own separate
// timer at all.
//
// `FUN_004b9350(playerStructAddr, currentTimeMs)` confirmed via disassembly to be a
// genuine __cdecl (both args on the stack, no custom register convention) -- safe to
// call directly. `FUN_007380e0()` takes NO arguments and reads an ambient value
// already sitting in the x87 FPU register (ST0) at time of call -- i.e. it depends on
// its CALLER having set up that register first, so calling it cold ourselves would
// likely read garbage (the same class of risk investigated and ruled out for the ADS
// FOV bug). NOT calling it directly for that reason -- FUN_004b9350 already calls it
// internally, in the correct context, on the branch where it's needed, so we get its
// effect safely by only calling FUN_004b9350 ourselves.
//
// player struct address is `&DAT_00984b88` (a fixed global, passed as the raw address
// itself, not its dereferenced contents -- confirmed from the real call site's exact
// argument pattern); current time is `*(int*)0x00984b78` (dereferenced value, the same
// "current time" global already used elsewhere in this project, e.g. FUN_0057d740's
// DAT_00984b78 frame-time reads).
using GetRealSprintValueFn = int(__cdecl*)(uintptr_t playerStructAddr, int currentTimeMs);
GetRealSprintValueFn const GetRealSprintValue = reinterpret_cast<GetRealSprintValueFn>(0x004b9350);
constexpr uintptr_t kSprintPlayerStructAddr = 0x00984b88;
constexpr uintptr_t kSprintCurrentTimeAddr = 0x00984b78;

DWORD g_lastSprintDiagLogMs = 0;

void LogSprintDiag()
{
    DWORD nowMs = GetTickCount();
    if (nowMs - g_lastSprintDiagLogMs < 250) return;
    g_lastSprintDiagLogMs = nowMs;

    int currentTimeMs = *reinterpret_cast<volatile int*>(kSprintCurrentTimeAddr);
    int realValue = GetRealSprintValue(kSprintPlayerStructAddr, currentTimeMs);

    char buf[220];
    sprintf_s(buf,
        "[sprint-diag] realValue=%d currentTimeMs=%d ourStamina=%.3f ourWinded=%d "
        "sprintHeld=%d realStance=%d",
        realValue, currentTimeMs, g_sprintStamina, g_sprintWinded ? 1 : 0,
        g_sprintHeld ? 1 : 0, GetRealStance());
    LogFromController(buf);
}
} // namespace

extern "C" void __cdecl InjectControllerSprint()
{
    unsigned short buttons;
    unsigned char leftTrigger, rightTrigger;
    if (!Controller_GetRawButtonsAndTriggers(buttons, leftTrigger, rightTrigger)) return;

    LogSprintDiag(); // task #9 -- investigating a real native sprint timer, see comment above

    bool held = IsPhysicalHeld(g_buttonMap.sprint, buttons, leftTrigger, rightTrigger);
    if (held && !g_sprintHeld && GetRealStance() != 0) {
        // Rising edge while crouched/prone: real console sprint stands the player back
        // up to full upright first, same as pressing forward while ducked/prone does.
        // Drives the same real toggle B does now (ForceStandingViaRealToggle), not our
        // own tracked stance -- without this, sprint would just run while still
        // crouched/prone (bug found 2026-07-15).
        ForceStandingViaRealToggle();
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
                g_sprintStamina = g_modConfig.sprintMaxStaminaSeconds; // full refill once cooldown clears
            }
        } else if (g_sprintHeld && GetRealStance() == 0) {
            g_sprintStamina -= dt;
            if (g_sprintStamina <= 0.0f) {
                g_sprintStamina = 0.0f;
                g_sprintWinded = true;
                g_sprintCooldownRemaining = g_modConfig.sprintRegenSeconds;
            }
        } else {
            g_sprintStamina += dt * (g_modConfig.sprintMaxStaminaSeconds / g_modConfig.sprintRegenSeconds);
            if (g_sprintStamina >= g_modConfig.sprintMaxStaminaSeconds) {
                g_sprintStamina = g_modConfig.sprintMaxStaminaSeconds;
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
// BIT-OWNERSHIP TRACKING (2026-07-16, revised): the first version of this fix gated
// on Controller_IsConnected(), but that only covers a fully-unplugged controller --
// with one connected-but-idle (a very normal setup: controller sitting on the desk
// while actually playing keyboard/mouse), IsSprintActive() is still false (g_sprintHeld
// correctly reads "not held" from the idle controller), so the `else` branch would
// still clear a bit real keyboard input may have just set a moment earlier. The
// general fix doesn't need to detect "which input device is active" at all -- it only
// needs to never clear a bit it didn't set itself. Track whether WE are the one
// currently asserting the bit; only clear what we own, and otherwise leave real
// pm_flags completely alone (whatever native keyboard/kbutton logic put there stands).
bool g_weOwnSprintBit = false;

extern "C" void __cdecl InjectControllerSprintPmFlags(uint32_t pmlPtr)
{
    if (!pmlPtr) return;
    uint32_t ps = *reinterpret_cast<uint32_t*>(pmlPtr);
    if (!ps) return;
    uint32_t* flags = reinterpret_cast<uint32_t*>(ps + 0xc);
    if (IsSprintActive()) {
        *flags |= kPmFlagSprint;
        g_weOwnSprintBit = true;
    } else if (g_weOwnSprintBit) {
        // Only clear it if we were the one holding it on -- never touch a bit that
        // real keyboard/native input set on its own (this is exactly what broke
        // vanilla keyboard sprint: "toggles once, times out, never recovers").
        *flags &= ~kPmFlagSprint;
        g_weOwnSprintBit = false;
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
    // Only ever ORs the bit in (never clears), so it was already safe by construction
    // w.r.t. the keyboard regression -- still marks ownership so the clearing branch
    // in InjectControllerSprintPmFlags above knows this tick's bit is ours to release
    // later.
    if (!IsSprintActive()) return;
    if (!pmlPtr) return;
    uint32_t ps = *reinterpret_cast<uint32_t*>(pmlPtr);
    if (!ps) return;
    *reinterpret_cast<uint32_t*>(ps + 0xc) |= kPmFlagSprint;
    g_weOwnSprintBit = true;
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
constexpr uintptr_t kMenuActiveGateAddr = 0x00B36210;
constexpr uint32_t kMenuActiveGateBit = 0x10u;
constexpr int kKeyEscape = 0x1b;
bool g_menuBackHeld = false;

bool IsMenuActive()
{
    uint32_t gate = *reinterpret_cast<volatile uint32_t*>(kMenuActiveGateAddr);
    return (gate & kMenuActiveGateBit) != 0;
}
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

extern "C" void __cdecl InjectControllerPauseMenu()
{
    unsigned short buttons;
    unsigned char leftTrigger, rightTrigger;
    if (!Controller_GetRawButtonsAndTriggers(buttons, leftTrigger, rightTrigger)) return;

    bool held = IsPhysicalHeld(g_buttonMap.pause, buttons, leftTrigger, rightTrigger);
    if (held && !g_startHeld) {
        char buf[128];
        if (!g_paused) {
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
            bool isSlot4 = (kDpad[i].slot == 3);
            if (held) {
                char tag[32];
                sprintf_s(tag, "dpad-press slot=%d", kDpad[i].slot);
                LogStanceDiag(tag);
                if (isSlot4) {
                    SendSyntheticActionSlot4Key(true);
                } else {
                    ActionSlotDown(kLocalClientIndex, kDpad[i].slot);
                }
            } else {
                LogStanceDiag("dpad-release");
                if (isSlot4) {
                    SendSyntheticActionSlot4Key(false);
                } else {
                    ActionSlotUp(kLocalClientIndex);
                }
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
    InjectControllerMenuBack(); // same redundancy rationale as the pause-menu call above
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
    // g_sprintStamina's own initializer is a plain literal, not a read of g_modConfig
    // (see its declaration comment for why) -- resync here now that LoadModConfig()
    // (called earlier in DllMain) has definitely run, in case the INI overrode the
    // default max stamina.
    g_sprintStamina = g_modConfig.sprintMaxStaminaSeconds;

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
