// analog_input_hooks_x64.cpp -- x64 hook installation, built on real signature scanning
// (signature_scan.h/.cpp), per the locked 2026-09-03 policy (CLAUDE.md SS5/SS10.3).
//
// This is the designated home for every x64 hook going forward, parallel to the
// existing x86 analog_input_hooks.cpp (which stays Win32-only -- its ~14
// __declspec(naked)/inline __asm blocks don't compile on x64 at all, a hard MSVC
// limitation confirmed earlier this migration, not a stopgap). Kept as a SEPARATE
// translation unit rather than #ifdef'd into the same 11000+-line x86 file, matching
// this project's own standing "iw5sp.exe and iw5mp.exe are separate binaries, don't
// assume shared addresses" precedent extended one level further: x86 and x64 hook
// CODE stays structurally separate too, so neither platform's build risks being
// destabilized by changes aimed at the other.
//
// x64 architectural simplification, confirmed repeatedly during this migration's RE
// pass (re_notes/known_issues_x64.md issue #1): almost every hook target uses the
// real Microsoft x64 fastcall ABI (RCX/RDX/R8/R9) with no custom register tricks --
// unlike x86, where several of this project's real hooks needed hand-written
// trampolines specifically to preserve non-standard calling conventions
// (unaff_ESI/unaff_EDI-style register-passed args). This means most x64 hooks can be
// plain C++ functions MinHook detours to directly, no __asm at all.
//
// FIRST DELIVERABLE (2026-09-03): a single, deliberately zero-behavior-change
// diagnostic hook -- proved signature-scan -> MinHook-install -> detour-fires-
// correctly works end to end on this specific x64 binary (CONFIRMED LIVE
// 2026-09-04, see re_notes/known_issues_x64.md issue #1 -- 5 real fires during
// actual gameplay, clean call-through each time). Matches this project's own
// established convention from the visual-suite work (README.md's Phase A:
// "test with a trivial passthrough shader first, before any real effect ships,
// to isolate plumbing bugs from shader bugs").
//
// SECOND DELIVERABLE (2026-09-04, same day the foundation was confirmed): the
// first real gameplay hooks, Sprint and Movement (movement added specifically
// because Sprint alone produces no observable effect without movement to
// multiply -- can't test one without the other). CONFIRMED WORKING LIVE.
//
// THIRD DELIVERABLE (2026-09-04, same day): Look (right stick), folded into
// the Movement hook as a pre-call accumulator write since MinHook only allows
// one detour per target. CONFIRMED WORKING LIVE.
//
// FOURTH DELIVERABLE (2026-09-04, same day, direct instruction "do all in one
// pass"): Buttons/ADS/Reload, Pause toggle, and Weapnext -- all via direct
// calls into confirmed, self-contained real engine functions (not MinHook
// detours), polled from the same per-tick point Look uses.
//
// FIFTH ROUND (2026-09-04, same day, live-test fixes): the first live test
// against this deliverable found two real bugs, both fixed:
// (1) Pause opened but couldn't close -- PollPauseToggleX64 now also polled
//     from InjectMenuInputTick (analog_input_hooks.cpp), the always-on tick
//     that keeps running during pause, not just from Hook_MovementTick (which
//     halts entirely while paused, same architecture x86 already hit this
//     exact bug class on).
// (2) Fire/ADS/Reload silently did nothing -- the original approach called
//     FUN_14007eaf0(player, bindIndex, isDown) directly, but that function's
//     second parameter is really a raw keycode slot, not a bind-name-table
//     index (misread on first pass); fixed by calling FUN_14007c3a0 (the
//     real case-number dispatcher, cases ARE bind-name-table indices) the
//     same way Pause/Weapnext already do.
// Sprint/Movement/Look/Pause-open/Weapnext were all already confirmed
// working live before this round.
//
// SIXTH ROUND (2026-09-04, same day, a second live test after the fifth
// round's fix): Fire fired ONCE then stopped; ADS came out as a toggle
// (stays zoomed after releasing) instead of hold. Both traced to the same
// root cause -- FUN_14007c3a0's own down/up cases tail-call
// FUN_14007e460/FUN_14007e490, and their second argument is a SOURCE
// IDENTIFIER (real dual-key-binding tracking), not an isDown boolean --
// passing 0/1 there broke release-slot matching. Fixed by calling
// FUN_14007e460/FUN_14007e490 directly with a consistent synthetic source
// id (see kSyntheticSourceId's own comment for the full corrected trace,
// including why this also fixes ADS's toggle problem as a direct
// consequence, not a separate patch).

#include <windows.h>
#include <cstdio>
#include <cstring>
#include <cctype>
#include <cstdlib>
#include "../third_party/minhook/include/MinHook.h"
#include "signature_scan.h"
#include "controller_input.h"
#include "mod_config.h"
#include "overlay_hud.h" // CustomOptionsMenu_TickInput/_ResetOnMenuClose -- see
                          // PollCustomOptionsMenuX64's own comment below
#include "rumble.h" // Rumble_Install()/Rumble_Tick() -- 2026-09-12 x64 port, see
                     // rumble.cpp's own "x64 PORT" block for the full RE trail

extern void LogFromController(const char* msg);  // dllmain.cpp, shared log file (see analog_input_hooks.cpp's
                                    // own identical convention)
// analog_input_hooks.cpp's own generic "is this logical action's physical button
// currently held" helper (no raw addresses, no __asm -- compiles for both
// platforms already, see that file's own top-of-file comment). Reused here rather
// than duplicating the same PhysicalInput switch a second time. Its own
// IsPhysicalHeld() lives in an anonymous namespace (internal linkage) -- confirmed
// via a real LNK2019 the first time this file called it directly -- so this calls
// the thin exported wrapper (IsPhysicalHeld_Exported, same file, added for exactly
// this) instead, same pattern as IsMenuActive_Exported()/LogFromController() already
// use elsewhere in this codebase.
extern "C" bool IsPhysicalHeld_Exported(PhysicalInput p, unsigned short buttons, unsigned char leftTrigger, unsigned char rightTrigger);
// Same file, same class of internal-linkage fix as IsPhysicalHeld_Exported above
// (RouteStickAxes() lives in the same anonymous namespace) -- reused here for the
// Movement hook below rather than duplicating the per-layout axis-swap switch a
// second time.
extern "C" void RouteStickAxes_Exported(float leftX, float leftY, float rightX, float rightY, StickLayout layout,
                                         float& moveX, float& moveY, float& lookX, float& lookY);
// d3d9_hook.cpp's own real game HWND getter -- plain extern "C", not arch-guarded
// there, so callable directly (no _Exported wrapper needed, unlike the two above
// which exist specifically to escape an anonymous namespace). Needed for D-pad
// Left's synthetic-key exception below (SendSyntheticActionSlot4KeyX64).
extern "C" HWND GetGameWindow();

namespace {

// FUN_1400168a0 -- the confirmed x64 Pmove per-substep tick function (the real hook
// point Sprint's own resolved chain runs through, re_notes/x64_migration/
// sprint_weapnext_x64.md). Signature derived from actual disassembly via
// DumpSigBytes.java (re_notes/x64_migration/impl_sig_1400168a0.txt), hand-corrected:
// that script's own reference-based heuristic flagged the LEA RBP,[RSP-0x80] and
// MOVAPS [RSP+0x120],XMM10 instructions as needing wildcards, which is a real false
// positive -- both are RSP-relative (a fixed small stack displacement, never an
// address that shifts between builds), not RIP-relative/absolute. Only the
// `CMP qword ptr [rip+disp32], 0` at +0x17 (a real global-flag check) and anything
// past it actually need wildcarding. Recorded here so a future signature doesn't
// re-trip the same false positive:
//   40 55 53 56 57 41 54 41 56 41 57          push rbp/rbx/rsi/rdi/r12/r14/r15 (7 regs)
//   48 8D 6C 24 80                            lea rbp,[rsp-0x80]        (RSP-relative, keep literal)
//   48 81 EC 80 01 00 00                      sub rsp,0x180
//   48 83 3D ?? ?? ?? ?? 00                   cmp qword ptr [rip+????], 0  (RIP-relative, wildcard the 4-byte disp)
//   4C 8B F1                                  mov r14,rcx
//   48 8B 31                                  mov rsi,qword ptr [rcx]
constexpr const char* kPmoveTickSignature =
    "40 55 53 56 57 41 54 41 56 41 57 48 8D 6C 24 80 48 81 EC 80 01 00 00 "
    "48 83 3D ?? ?? ?? ?? 00 4C 8B F1 48 8B 31";

using PmoveTickFn = void(__fastcall*)(void* param1);
PmoveTickFn g_realPmoveTick = nullptr;

// Rate-limited on purpose -- this function fires on every Pmove sub-step (potentially
// several times per rendered frame, see FUN_140016620's own 66ms-cap subdivision
// loop), and this project has already hit a real, live, ~22GB log-growth regression
// (issue #67) from an unconditional per-call log site once before. Logs the first 5
// fires (proves the hook is alive quickly after launch) then one heartbeat every
// ~5000 calls (still enough to confirm it's still firing during a long session,
// nowhere near flood territory).
long long g_fireCount = 0;

// Real "is a level currently live" signal for the auto-unstick sequence below --
// updated on every real Pmove tick, read from the always-on menu tick to detect
// a fresh level becoming active (Pmove tick resuming after being stopped, e.g.
// at a menu/loading screen) without needing dedicated level-load RE.
DWORD g_lastPmoveTickMs = 0;

void __fastcall Hook_PmoveTick(void* param1)
{
    ++g_fireCount;
    g_lastPmoveTickMs = GetTickCount();
    if (g_fireCount <= 5 || (g_fireCount % 5000) == 0) {
        char buf[128];
        sprintf_s(buf, "[x64-diag] Pmove tick hook fired (count=%lld)", g_fireCount);
        LogFromController(buf);
    }
    // 2026-09-04: this is now the real per-tick controller-poll request point --
    // FUN_1400168a0 (this hook's target) is confirmed to call FUN_140014a80
    // (Hook_SprintTick's own target, below) exactly once per invocation, so
    // requesting a fresh poll here, once, covers every hook riding on this same
    // Pmove tick. Matches controller_input.h's own documented convention (poll
    // once per real tick from whichever per-tick consumer is driving; every
    // Controller_Get* call below only ever reads the already-cached sample).
    Controller_RequestPoll();
    g_realPmoveTick(param1);
}

// FUN_140014a80 -- the confirmed x64 Pmove-entry Sprint pm_flags writer (called
// from within FUN_1400168a0 above, on every movement-type branch -- see
// re_notes/x64_migration/sprint_weapnext_x64.md and known_issues_x64.md issue #1
// for the full RE trail). Signature derived via DumpSigBytes.java
// (re_notes/x64_migration/impl_sig_140014a80.txt): the function's real prologue
// plus its first two real struct-offset checks, a distinctive, self-contained
// 43-byte span with only ONE genuine wildcard needed (a short JZ's 1-byte
// displacement -- kept wildcarded on the same "future-proof against a layout
// shift" reasoning as every other signature in this file, even though it's
// fixed for this specific binary). Everything else in this range is either
// RSP-relative stack saves (never an address, confirmed safe to keep literal
// per this file's own established false-positive lesson) or RBX/RCX-relative
// struct-offset reads (also not an address that shifts independently of the
// struct layout itself):
//   48 89 5C 24 08                  mov [rsp+8],rbx      (RSP-relative, keep literal)
//   48 89 6C 24 10                  mov [rsp+0x10],rbp    (RSP-relative, keep literal)
//   48 89 74 24 18                  mov [rsp+0x18],rsi    (RSP-relative, keep literal)
//   57                              push rdi
//   48 83 EC 70                     sub rsp,0x70
//   48 8B 19                        mov rbx,[rcx]
//   33 ED                           xor ebp,ebp
//   48 8B F2                        mov rsi,rdx
//   48 8B F9                        mov rdi,rcx
//   39 AB CC 01 00 00               cmp [rbx+0x1cc],ebp
//   74 ??                           jz  <wildcarded 1-byte displacement>
//   F6 41 0C 02                     test byte ptr [rcx+0xc],2
constexpr const char* kSprintTickSignature =
    "48 89 5C 24 08 48 89 6C 24 10 48 89 74 24 18 57 48 83 EC 70 "
    "48 8B 19 33 ED 48 8B F2 48 8B F9 39 AB CC 01 00 00 74 ?? F6 41 0C 02";

using SprintTickFn = void(__fastcall*)(void* param1, void* param2);
SprintTickFn g_realSprintTick = nullptr;

// Forward declarations -- these real symbols are defined further down this
// same anonymous namespace (alongside Fire/ADS/Reload's own struct-resolve
// cluster and CrouchProne/Jump's stance-dispatch helpers), but Hook_SprintTick
// below needs to reference them here, earlier in the file (this hook rides an
// earlier per-tick engine call than the one those symbols' own real resolve
// code sits on). `extern` on a variable declared inside an unnamed namespace
// is a plain forward declaration -- internal linkage is still enforced by the
// enclosing unnamed namespace regardless, not by the extern keyword; the
// matching real definitions appear unchanged below.
using KbuttonActivateFn = void(__fastcall*)(int* kbutton, int sourceId, int timestamp);
using KbuttonDeactivateFn = void(__fastcall*)(int* kbutton, int sourceId, int timestamp);
extern KbuttonActivateFn g_kbuttonActivate;
extern KbuttonDeactivateFn g_kbuttonDeactivate;
extern int* g_sprintStruct;
extern int* g_holdBreathStruct;
extern volatile uint32_t* g_timestampPtr;
int GetRealStanceX64();
void ForceStandingViaRealToggleX64();

// ---- Sprint (L3): migrated to the real +sprint kbutton (2026-09-12) --------------
//
// SUPERSEDES the 2026-09-04 pm_flags-forcing design below (kept as history, not
// reintroduced): that was x86's own ORIGINAL, deliberately-abandoned Sprint
// mechanism (see CLAUDE.md's "Sprint's real kbutton" section) -- it bypasses the
// engine's own native sprint duration/recovery timer AND the Extreme Conditioning
// perk's automatic duration override entirely, both of which x86 gets "for free"
// once the real kbutton drives Sprint instead. Found as a genuine, previously-
// undocumented x64 regression by the 2026-09-12 full feature-parity audit
// (re_notes/x64_feature_parity_audit.md, finding #2 / table row #22).
//
// Real kbutton struct resolved via the SAME anchor+offset technique already
// proven for Fire/Reload/ADS (kFireStructInsnOffset etc., below) -- confirmed via
// TWO independent angles, matching this project's own issue #3 standard (never
// trust a case/lead without independent confirmation):
//   1. Decompiled FUN_14007c3a0 (re_notes/x64_migration/decomp_14007c3a0_full.txt)
//      case 0x3d/0x3e (61/62 decimal -- x86's own exact "+sprint"/"-sprint" case
//      numbers, per CLAUDE.md) calls FUN_14007e460/FUN_14007e490 on
//      `&DAT_1406448f4 + lVar4*0x230` -- the same per-bind-struct pattern already
//      confirmed for Fire/Reload/ADS, and the same case-number-carries-over-from-
//      x86 pattern already independently confirmed for every other bind in this
//      dispatcher this session (Fire=1/2, Reload=0xb/0xc, ADS=0x3b/0x3c,
//      togglecrouch=0x48, etc.).
//   2. Independent cross-check, the SAME technique x86's own original discovery
//      used: case 9 ("+breath_sprint" down, the real default SHIFT bind) in the
//      same decompile fires FUN_14007e460 on `&DAT_14064482c` (Hold Breath's
//      alias -- Hold Breath itself is NOT implemented on x64, see
//      known_issues_x64.md) AND on `&DAT_1406448f4` back-to-back -- i.e. the real
//      default Sprint/Hold-Breath key already drives this exact same struct
//      today, mirroring x86's own "case 9 disassembles to two back-to-back
//      kbutton calls, one of which is the Sprint kbutton" cross-confirmation
//      exactly.
// A prior session's RE scratch pass (re_notes/x64_migration/
// rawbytes_sprint_struct.txt) had already dumped the raw bytes at the two real
// `LEA reg,[rip+disp32]` instructions for case 0x3d/0x3e (0x14007cead/
// 0x14007ced7) but was cut off before writing any code -- independently decoded
// by hand this session: both resolve to 0x1406448f4, matching the decompile's
// DAT name exactly (confirms that scratch lead was correct, not just assumed).
//
// Gating: excludes ADS (matches x86's `!g_adsHeld` exclusion -- the same
// physical bind is Hold Breath while aiming a sniper on x86, and hip-fire sprint
// speed has no meaning while ADS'd anyway). Computed locally from the controller
// state already read in this function rather than reaching for the separate
// g_adsHeldX64 global (Hook_MovementTick's own tracking variable) -- same
// physical-input source, avoids a forward-declaration dependency on a variable
// defined much later in this same anonymous namespace, and this hook rides a
// different, earlier per-tick engine call (FUN_140014a80/Pmove-entry) than
// Hook_MovementTick's own FUN_14007d9f0. x64 has no Hold Breath kbutton yet
// (feature-parity audit item #23, confirmed ABSENT, a separately tracked gap,
// not this fix's scope) so there's no second consumer of the bind to stay
// mutually exclusive with -- this narrows to a plain ADS exclusion, matching
// what x86's own comment describes as the two paths' real end effect anyway.
//
// Rising-edge stand-from-crouch/prone, ported alongside the kbutton fix per
// direct instruction (2026-09-12 coordinator follow-up: x86's own
// InjectControllerSprint really does this, confirmed by re-reading
// analog_input_hooks.cpp directly -- not optional polish, standing directive
// for this whole parity pass is "all 0.3.5 stuff needs to be present at the
// same level or better"). Reuses ForceStandingViaRealToggleX64() as-is (already
// built and wired for Jump's own auto-stand, re_notes/known_issues_x64.md issue
// #1) rather than reimplementing -- same real native toggle-case dispatch
// (0x48/0x49), just called from a second trigger site. Fires once on Sprint's
// own rising edge while crouched/prone and NOT ADS'd (same "hip-fire Sprint
// only, not Hold Breath" reasoning as x86's own excluded-while-ADS'd comment
// for this exact call).
bool g_sprintKbuttonActiveX64 = false; // tracks whether OUR activate call is
                                         // currently "claimed" on the real
                                         // kbutton, mirrors x86's own
                                         // g_sprintKbuttonActive -- edge-
                                         // triggers the real activate/deactivate
                                         // calls exactly once per transition.
constexpr int kSprintSyntheticSourceId = 0x1000; // same value/rationale as the
                                                   // later kSyntheticSourceId
                                                   // (Fire/ADS/Reload) -- kept as
                                                   // its own local constant
                                                   // rather than forward-
                                                   // referencing that one, since
                                                   // a constexpr (unlike a
                                                   // variable) can't be forward-
                                                   // declared separately from its
                                                   // definition.

// ---- Hold Breath (L3 while ADS'd, sniper-class): ported 2026-09-12, parity
// audit item #23 -- previously ABSENT on x64 (grepped confirmed zero
// references before this change). Same physical bind as Sprint on real
// console/keyboard, exactly like x86 (analog_input_hooks.cpp's own
// "Hold Breath (L3 while ADS'd)" section) -- gated on ADS instead of
// "not ADS," no explicit sniper-class check in OUR code either, mirroring
// x86's own design exactly: x86's InjectControllerSprint computes
// `holdBreathActive = g_sprintHeld && g_adsHeld` with zero weapon-class
// logic of its own, relying entirely on the real native kbutton to only
// produce the sway-reduction/accuracy effect on sniper-class weapons --
// this is a genuine native kbutton, not something this project's own code
// needs to gate by weapon type.
//
// Struct resolved via the SAME anchor+offset technique as Fire/Reload/ADS/
// Sprint (kHoldBreathStructInsnOffset below) -- confirmed via TWO
// independent angles, matching this project's own issue #3 standard:
//   1. Decompiled FUN_14007c3a0 case 9 ("+breath_sprint" down, the real
//      default SHIFT bind -- re_notes/x64_migration/decomp_14007c3a0_full.txt)
//      fires FUN_14007e460 on `&DAT_14064482c + lVar4*0x230` FIRST, then on
//      `&DAT_1406448f4 + lVar4*0x230` (= g_sprintStruct) SECOND -- i.e. the
//      real default Sprint/Hold-Breath key already drives both structs
//      back-to-back today, exactly mirroring x86's own original discovery
//      ("case 9 disassembles to two back-to-back kbutton calls, one of
//      which is the Sprint kbutton").
//   2. Independently re-derived the raw bytes at both real
//      `LEA reg,[rip+disp32]` instructions this decompile reference
//      resolves to (0x14007cc69 and 0x14007cc93, re_notes/x64_migration/
//      rawbytes_holdbreath_struct.txt, dumped via DumpRawBytes.java against
//      the live binary, not read off the decompile alone) -- both are
//      genuine 7-byte `48 8D 05 <disp32>` LEA instructions whose RIP-
//      relative target computes to 0x14064482C bit-for-bit, matching the
//      decompile's `DAT_14064482c` name exactly.
// A third, structural cross-check: 0x14064482c is exactly 0x14 bytes past
// g_fireStruct's own target (DAT_140644818) -- i.e. the very NEXT kbutton_t
// in the contiguous per-player array (kbutton_t is 0x14/20 bytes here:
// down0/down1/timestamp/downtime as int32 + a 1-byte active flag, per
// FUN_14007e460/e490's own decompile), not an internal field of Fire's own
// struct the way x86's 0xA98C04 alias is (x86's kbutton_t stride is
// smaller, so its own Hold Breath address lands INSIDE Fire's struct at
// its down[1] field -- a real x86-specific aliasing quirk, per
// known_issues.md issue #6). x64's Hold Breath struct is a genuinely
// separate, dedicated kbutton_t -- there is no reason to expect x86's own
// "active flag never self-clears on this specific alias" bug (issue #24)
// to recur here, and x64's own Sprint migration (immediately above) already
// proved this exact call pattern (g_kbuttonActivate/g_kbuttonDeactivate,
// no debounce, no active-flag force-clear) works cleanly with no such
// workaround needed -- so none is added here either. Flagged for live
// confirmation regardless, not assumed risk-free from static analysis alone.
//
// Also referenced (decompile lines 201-206) by a SEPARATE case, 0x31/0x32 --
// a standalone (likely unbound-by-default) "+breath_hold"-class bind that
// drives this exact same struct alone, with no paired Sprint call -- further
// corroborating this is a real, dedicated Hold Breath kbutton, not an
// incidental byproduct of case 9's own dual-call shape.
constexpr int kHoldBreathSyntheticSourceId = 0x1000; // any fixed non-zero
                                                       // value works, same
                                                       // reasoning as
                                                       // kSprintSyntheticSourceId
                                                       // -- distinct struct
                                                       // pointer means no
                                                       // cross-bind collision
                                                       // risk regardless of
                                                       // source-id reuse.
bool g_holdBreathKbuttonActiveX64 = false; // mirrors g_sprintKbuttonActiveX64
                                             // -- edge-triggers the real
                                             // activate/deactivate calls
                                             // exactly once per transition.

void __fastcall Hook_SprintTick(void* param1, void* param2)
{
    // Let native logic run to completion first, untouched -- same ordering
    // this hook always used, just no longer followed by a raw pm_flags write:
    // driving the real kbutton below lets the native engine own that bit (and
    // its own duration/recovery timer) again, the same handoff x86 made.
    g_realSprintTick(param1, param2);

    unsigned short buttons = 0;
    unsigned char leftTrigger = 0, rightTrigger = 0;
    bool haveController = Controller_GetRawButtonsAndTriggers(buttons, leftTrigger, rightTrigger);
    bool sprintHeld = haveController && IsPhysicalHeld_Exported(g_buttonMap.sprint, buttons, leftTrigger, rightTrigger);
    bool adsHeldNow = haveController && IsPhysicalHeld_Exported(g_buttonMap.ads, buttons, leftTrigger, rightTrigger);
    bool active = sprintHeld && !adsHeldNow;

    // Rising edge while crouched/prone: stand up first, matching x86's own
    // InjectControllerSprint exactly (real console sprint stands the player
    // back up before running). Checked BEFORE updating g_sprintKbuttonActiveX64
    // below so this only fires on a genuine transition into "active," not every
    // tick while held.
    if (active && !g_sprintKbuttonActiveX64 && GetRealStanceX64() != 0) {
        ForceStandingViaRealToggleX64();
    }

    int timestamp = g_timestampPtr ? static_cast<int>(*g_timestampPtr) : 0;

    // NOTE: this used to be a single early `return` once Sprint's own active
    // state matched its last-sent state -- moved to a per-block `if` instead,
    // since that early return would otherwise skip Hold Breath's own edge
    // check entirely on every tick where Sprint's state happens to be
    // steady (i.e. most ticks while ADS-holding-still-and-not-sprinting) --
    // both binds now update independently, matching x86's own
    // InjectControllerSprint shape (Sprint and Hold Breath handled as two
    // separate state machines in the same per-tick function, not chained).
    if (active != g_sprintKbuttonActiveX64) {
        g_sprintKbuttonActiveX64 = active;
        if (g_kbuttonActivate && g_kbuttonDeactivate && g_sprintStruct) {
            if (active) {
                g_kbuttonActivate(g_sprintStruct, kSprintSyntheticSourceId, timestamp);
            } else {
                g_kbuttonDeactivate(g_sprintStruct, kSprintSyntheticSourceId, timestamp);
            }
        }
    }

    // Hold Breath (L3 while ADS'd): same physical bind as Sprint, gated on
    // ADS instead of "not ADS" -- see the big comment above
    // kHoldBreathSyntheticSourceId for the full discovery trail.
    bool holdBreathActive = sprintHeld && adsHeldNow;
    if (holdBreathActive != g_holdBreathKbuttonActiveX64) {
        g_holdBreathKbuttonActiveX64 = holdBreathActive;
        if (g_kbuttonActivate && g_kbuttonDeactivate && g_holdBreathStruct) {
            if (holdBreathActive) {
                g_kbuttonActivate(g_holdBreathStruct, kHoldBreathSyntheticSourceId, timestamp);
            } else {
                g_kbuttonDeactivate(g_holdBreathStruct, kHoldBreathSyntheticSourceId, timestamp);
            }
        }
    }
}

// FUN_14007d9f0 -- the confirmed x64 movement/angle-finalize function, a genuine
// structural fusion (by the x64 compiler) of x86's separate FUN_0057d430 (keyboard
// movement writer) and FUN_0057de60 (angle-finalize) into ONE function. Confirmed
// via full decompile (re_notes/x64_migration/impl_movement_14007d9f0.txt): param_1
// (RCX) is directly the usercmd_t*, not a wrapper context struct like the Pmove
// functions above use, with forwardmove at param_1+0x1c and rightmove at
// param_1+0x1d as signed bytes -- IDENTICAL offsets to x86's own documented
// usercmd_t layout, strong evidence the underlying struct never changed across
// the recompile. Signature via DumpSigBytes.java
// (re_notes/x64_migration/impl_sig_14007d9f0.txt): the function's real prologue
// plus its first real global-flag check, with one genuine RIP-relative wildcard
// needed (everything else is either an RSP-relative displacement or, per this
// project's own established false-positive lesson re-confirmed this session,
// not actually an address at all):
//   48 8B C4                        mov rax,rsp
//   53                              push rbx
//   48 81 EC D0 00 00 00            sub rsp,0xd0
//   83 3D ?? ?? ?? ?? 00            cmp dword ptr [rip+????],0  (RIP-relative global flag, wildcard the 4-byte disp)
//   48 8B D9                        mov rbx,rcx
constexpr const char* kMovementTickSignature =
    "48 8B C4 53 48 81 EC D0 00 00 00 83 3D ?? ?? ?? ?? 00 48 8B D9";

// Angle-accumulator DATA signature, also inside FUN_14007d9f0 -- NOT a hook
// target, resolved via SigScan::ResolveRipRelative instead of MH_CreateHook.
// This exact 5-instruction/33-byte sequence (re_notes/x64_migration/
// full_sigbytes_14007d9f0.txt, offsets +0x394-+0x3B4) is the block that reads
// BOTH angle accumulators into stack scratch immediately before the real
// packing call (FUN_140003fc0) -- distinctive enough (two different RIP-
// relative float reads to two different globals, into two different stack
// slots, immediately followed by a CALL) to be unique in the whole binary:
//   F3 0F 10 05 ?? ?? ?? ??   movss xmm0,[rip+????]     -> DAT_1406e2738 (pitch)
//   F3 0F 11 44 24 38         movss [rsp+0x38],xmm0
//   F3 0F 10 05 ?? ?? ?? ??   movss xmm0,[rip+????]     -> DAT_1406e273c (yaw)
//   F3 0F 11 44 24 44         movss [rsp+0x44],xmm0
//   E8 ?? ?? ?? ??            call FUN_140003fc0
// The match's own address is the FIRST movss (pitch); the second movss (yaw)
// starts exactly 14 bytes in (8 + 6), both resolved via ResolveRipRelative
// against the same single match -- see the install block below.
constexpr const char* kAngleAccumSignature =
    "F3 0F 10 05 ?? ?? ?? ?? F3 0F 11 44 24 38 F3 0F 10 05 ?? ?? ?? ?? F3 0F 11 44 24 44 E8 ?? ?? ?? ??";

using MovementTickFn = void(__fastcall*)(void* param1, unsigned int param2);
MovementTickFn g_realMovementTick = nullptr;

// ---- "Needs a click to get input" gate, x64 (2026-09-04, live-reported) -----------
//
// Direct, corrected user report: NOT Windows/OS-level focus (the two experiments in
// d3d9_hook.cpp's own SendSyntheticActivationClick/SendRealFocusNudgeX64 both fire
// every session, confirmed via their own log lines, with the symptom still present)
// -- a genuine INTERNAL engine mechanism, the same SYMPTOM CLASS as an already-fixed
// x86 issue, relocated by the recompile rather than removed ("deffo internal they
// juist moved it").
//
// Found via full decompile of FUN_14007d9f0 itself (the function this project's own
// Movement/Look hook already sits on): its very first real branch, right after the
// mouse-delta-accumulator call, is `if ((DAT_1406e4774 & 0x800) != 0) return;` --
// when this ONE bit is set, the ENTIRE function (movement, look-angle packing,
// everything) is a complete no-op, for BOTH controller injection and real native
// keyboard/mouse input alike (this bit lives inside the native function itself, our
// hook just calls through to it). A SECOND, structurally distinct function,
// FUN_14007d5f0 (re_notes/x64_migration/decomp_14007d5f0.txt -- a real usercmd
// movement writer in its own right, touching the same forwardmove/rightmove/+0x1e/
// +0x1f usercmd fields), gates its ENTIRE body behind the exact same bit
// (`&DAT_1406e4774 + player*0xce5c`, the same per-player field FUN_14007d9f0 reads
// at offset 0 for SP's player 0). Two independent functions gating all their real
// work behind the identical single bit is strong, convergent evidence this is
// genuinely a broad "movement/input processing suppressed" gate, not a narrow
// crouch-specific lock like x86's own stance-guard bytes were.
//
// Static analysis could NOT find a writer to this flag (same limitation x86's own
// original investigation hit for ITS guard bytes, per known_issues.md issue #42 --
// `FindDataWriters.java` found only TEST/read references, no direct writes, likely
// because the real writer uses register-relative addressing Ghidra's reference
// tracker doesn't resolve back to this literal address). Rather than continue a
// static hunt with no confirmed writer to find, this applies the SAME empirical
// philosophy that already fixed both x86's original issue AND this session's own
// ADS toggle-flag bug: resolve the flag's real address and FORCE it to the desired
// state directly, every tick, rather than trying to trigger whatever real event
// naturally clears it. Real, honest uncertainty: it's not confirmed what ELSE this
// bit's legitimate SET state might represent (a deliberate "not yet controllable"
// window early in a level load being the most likely one) -- but forcing it clear
// only from inside Hook_MovementTick, which itself only ever runs during an active
// Pmove simulation tick in the first place (never during a menu/pause/loading
// screen, per this session's own established Pmove-tick-halts-during-pause
// finding), should keep this narrowly scoped to exactly the "stuck after launch"
// window this is meant to fix.
//
// Resolved via the SAME already-scanned kMovementTickSignature match (no separate
// scan needed) plus a fixed, directly-verified byte offset to the real
// `TEST dword ptr [rip+disp32], 0x800` instruction (re_notes/x64_migration/
// full_sigbytes_14007d9f0.txt, offset +0x54, 10 bytes: F7 05 + disp32 + the 0x800
// immediate) -- the first instruction shape this project has hit where the disp32
// ISN'T the instruction's last 4 bytes, hence SigScan::ResolveRipRelativeAt's own
// new, more general two-address form (signature_scan.h) rather than the existing
// ResolveRipRelative overload.
constexpr ptrdiff_t kInputGateFlagTestInsnOffset = 0x54;
uint32_t* g_inputGateFlag = nullptr;
constexpr uint32_t kInputGateBit = 0x800u;

// Trivial int8 clamp, duplicated locally rather than exported -- ClampToSByte()
// in analog_input_hooks.cpp is ALSO anonymous-namespace-scoped (its own separate
// small namespace, same class of internal linkage as IsPhysicalHeld/
// RouteStickAxes), but at 3 lines it's not worth cross-file plumbing for. Matches
// x86's own ClampToSByte exactly.
inline int8_t ClampToSByteX64(int v)
{
    if (v > 127) return 127;
    if (v < -128) return -128;
    return static_cast<int8_t>(v);
}

// ---- Look: right stick -> the pitch/yaw angle-delta accumulators directly, x64 ----
//
// x86's own current design (analog_input_hooks.cpp's InjectControllerLookAngles,
// superseded 2026-07-14 from an earlier "hook the raw mouse-delta source" approach
// per direct user correction -- see that comment for the full history) writes
// STRAIGHT to the engine's real pitch/yaw angle-accumulator globals, bypassing the
// mouse-cvar pipeline (sensitivity/m_yaw/m_pitch/cl_mouseAccel/filtering) entirely --
// controller look gets its own independent rate, not mouse emulation under the hood.
//
// On x64, those accumulators (re_notes/x64_migration/decomp_14007d3b0.txt +
// re_notes/x64_migration/full_sigbytes_14007d9f0.txt) are DAT_1406e2738 (pitch,
// added-to) / DAT_1406e273c (yaw, subtracted-from) -- confirmed via full decompile
// of FUN_14007d9f0 to be read, packed into the real usercmd_t.angles short (+0x38)/
// byte (+0x3a) via a call to FUN_140003fc0, and have their leftover fractional
// remainder written back, ALL UNCONDITIONALLY on every call to this same function --
// this happens regardless of whether there was any raw mouse delta this tick (that
// guard only gates whether NATIVE mouse/keyboard delta gets ADDED on top of
// whatever's already in the accumulators, an accumulate not an overwrite, so
// simultaneous mouse+controller input correctly stacks rather than one clobbering
// the other). This means our own write just needs to land in the accumulators
// BEFORE this function's native body runs -- a PRE-hook, unlike Movement's
// post-hook design below, since the native call itself both consumes AND packs
// them in one pass. Their real addresses aren't hardcoded (that would violate the
// locked signature-scanning policy the exact same way a hook target would) --
// resolved via SigScan::ResolveRipRelative against a real RIP-relative reference
// inside FUN_14007d9f0's own body, see kAngleAccumSignature below.
float* g_pitchAccum = nullptr;  // DAT_1406e2738 equivalent
float* g_yawAccum = nullptr;    // DAT_1406e273c equivalent

// x64 equivalents of x86's g_motionBlurYawDeltaDeg/g_motionBlurPitchDeltaDeg
// (analog_input_hooks.cpp) -- real per-frame look-rotation magnitude in degrees,
// mirrored here rather than shared cross-TU since the x86 globals live inside
// that file's own anonymous namespace (internal linkage, same class of "extern C
// wrapper needed" situation this file already handles for IsPhysicalHeld_Exported/
// RouteStickAxes_Exported). Set alongside the real yaw/pitch accumulator writes
// below (same values, same sign), read by overlay_hud.cpp's
// MotionBlurShaderSetupCallback via GetMotionBlurDeltasX64() once motion blur's
// own x64 gates are wired. Covers controller-stick look only, same real,
// documented limitation as x86 (gyro-aim isn't wired for x64 yet at all -- see
// known_issues_x64.md issue #1 -- so there's no gyro contribution to add here).
float g_motionBlurYawDeltaDegX64 = 0.0f;
float g_motionBlurPitchDeltaDegX64 = 0.0f;

// Mirrors x86's own GetLookAccelerationScale (analog_input_hooks.cpp) exactly --
// pure math against g_modConfig + GetTickCount(), no hardcoded x86 addresses, so
// it ports directly with no RE needed. Deliberately scoped OUT of this first x64
// pass: the ADS-FOV look-slowdown (GetAdsLookRateScale, needs an x64 equivalent of
// the hardcoded GetEffectiveFov/Dvar_FindVar addresses -- genuinely unresolved RE
// targets, not yet found) and gyro-aim (already PREVIEW/WIP and never live-tested
// on x86 itself, lowest priority). Both are real, honest gaps, not overlooked --
// see known_issues_x64.md issue #1.
DWORD g_lookAccelStartMsX64 = 0;

float GetLookAccelerationScaleX64()
{
    if (g_modConfig.lookAccelerationRampMs == 0) return 1.0f;

    DWORD nowMs = GetTickCount();
    if (g_lookAccelStartMsX64 == 0) {
        g_lookAccelStartMsX64 = nowMs; // rising edge: stick just left neutral this frame
    }
    DWORD elapsed = nowMs - g_lookAccelStartMsX64;
    if (elapsed >= g_modConfig.lookAccelerationRampMs) return 1.0f;

    return static_cast<float>(elapsed) / static_cast<float>(g_modConfig.lookAccelerationRampMs);
}

// ---- Buttons/ADS/Reload, Pause toggle, and Weapnext -- x64, direct calls -----------
//
// Unlike Sprint/Movement/Look above, these don't hook an existing per-tick engine
// function at all -- they CALL real, confirmed, self-contained engine functions
// directly, the same technique x86 already uses for weapnext (calls its own
// terminal weapon-cycling function directly, not through the whole dispatch chain)
// and matching this cluster's own real architecture (re_notes/x64_migration/
// README.md section 1e/1g, re_notes/x64_migration/sprint_weapnext_x64.md): every
// one of these is genuinely simpler on x64 than the x86 project's own hand-
// assembled CallKbuttonDown/CallKbuttonUp thunks needed (standard RCX/RDX/R8
// fastcall throughout, no custom register convention). Polled from within
// Hook_MovementTick below rather than a separate hook -- matches x86's own real
// architecture (analog_input_hooks.cpp calls InjectControllerButtons/Ads/Reload/
// Fire/PauseMenu/WeaponNext ALL from the SAME per-frame usercmd-build
// orchestration point FUN_14007d9f0 is this project's own x64 equivalent of).

// CORRECTED 2026-09-04, round 2 (real bug found via a SECOND live test, not
// guessed): the FIRST correction here (calling FUN_14007c3a0(player,
// caseNumber, isDown) directly, cases 1/2=Fire, 0xb/0xc=Reload,
// 0x3b/0x3c=ADS) genuinely fixed the "silently does nothing" symptom --
// Fire fired on the first press. But it then STOPPED after that one shot,
// and ADS came out as a toggle (stays zoomed after releasing the trigger)
// instead of a hold. Both trace to the SAME root cause, found by decompiling
// FUN_14007e460/FUN_14007e490 in full
// (re_notes/x64_migration/decomp_14007e460_e490.txt) -- the REAL down/up
// handlers FUN_14007c3a0 tail-calls into for every regular held bind:
//
//   void FUN_14007e460(int* kbutton, int sourceId, int timestamp) {
//       if (sourceId != kbutton[0] && sourceId != kbutton[1]) {
//           if (kbutton[0] == 0) kbutton[0] = sourceId;
//           else { if (kbutton[1] != 0) return; kbutton[1] = sourceId; }
//           ... activates on a genuinely NEW sourceId ...
//       }
//   }
//   void FUN_14007e490(int* kbutton, int sourceId, int timestamp) {
//       if (kbutton[0] == sourceId) { kbutton[0] = 0; ... }
//       else { if (kbutton[1] != sourceId) return; kbutton[1] = 0; ...
//              if (kbutton[0] != 0) return; }
//       ... deactivates only once BOTH slots are clear ...
//   }
//
// This is a real dual-binding kbutton_t (classic id-Tech/Quake lineage --
// tracks up to TWO simultaneous key sources bound to the same action, e.g.
// mouse1 AND spacebar both bound to Fire, without one release cancelling
// the other's hold). The second argument is NOT an isDown boolean -- it's a
// SOURCE IDENTIFIER, and the up-call's identifier must exactly match
// whichever slot the matching down-call actually wrote into, or the
// release is silently misrouted. Confirmed directly from
// FUN_14007eaf0's own real call sites into FUN_14007c3a0: it forwards the
// RAW KEYCODE itself as this third argument on every real native call, not
// a 0/1 flag -- so on a real keyboard press, `sourceId` is genuinely "which
// key" (VK/ASCII-ish), consistent for that key's own down+up pair.
//
// The first correction above passed `isDown ? 1 : 0` (0 or 1) as this
// argument -- on release, "0" happened to accidentally match `kbutton[1]`
// (the never-used SECOND slot, which defaults to 0), not `kbutton[0]`
// (which our own press actually wrote "1" into) -- so the release call hit
// its early-out `if (kbutton[0] != 0) return;` before ever clearing
// `kbutton[0]`, permanently wedging the kbutton "active" in a state the
// real weapon-fire logic apparently can't recover from cleanly. The next
// press then got silently ignored too (`sourceId == kbutton[0]` already,
// so FUN_14007e460's own new-source check never re-fires) -- matching
// "fire worked once, then stopped" exactly.
//
// FIXED, two changes together:
// (1) Call FUN_14007e460/FUN_14007e490 DIRECTLY (their own real, standalone
//     function signatures, resolved independently -- not through
//     FUN_14007c3a0 at all anymore) with a FIXED, consistent non-zero
//     synthetic source-id per bind (kSyntheticSourceId below), identical on
//     both the down and up call for the same bind -- guarantees the
//     release always finds and clears the exact slot the press wrote.
// (2) This also fixes ADS's toggle-vs-hold problem as a direct consequence,
//     not a separate patch: FUN_14007c3a0's case 0x3b/0x3c wrapped this
//     exact same FUN_14007e460/e490 call with an EXTRA, unconditional
//     toggle of a completely separate flag (DAT_1406e26e0, confirmed via
//     real disassembly at 0x14007ce3f-0x14007ce50: `flag = (flag==0)`,
//     unconditional on every press, never touched on release) -- that flag
//     toggle is what made ADS look like "press to toggle on, stays on."
//     Calling FUN_14007e460/e490 directly bypasses FUN_14007c3a0's case
//     dispatch entirely, so that toggle mutation never happens -- ADS now
//     drives purely off the SAME real kbutton-held mechanism Fire/Reload
//     use, matching x86's own proven hold-to-ADS design (x86's
//     InjectControllerAds already does exactly this: CallKbuttonDown on
//     press, CallKbuttonUp on release, nothing else).
//
// The three per-bind struct base addresses (kFireStructOffset/etc.) and the
// real timestamp global FUN_14007e460/e490's third argument reads from
// (DAT_141efb764) are all DATA, not code -- resolved the same way the Look
// accumulators were, via SigScan::ResolveRipRelative -- but anchored off
// FUN_14007c3a0's OWN already-reliably-resolved address (kAnchorSignature)
// plus a FIXED byte offset to each real instruction, rather than four more
// standalone multi-instruction signatures. This is the same sanctioned
// "resolve an entry point, then apply a byte offset" pattern
// signature_scan.h's own ResolveAs<FnT> already documents for the
// function-pointer case, extended here to a data reference at a fixed,
// directly-verified (via DumpRawBytes.java, not guessed) offset within the
// same already-scanned function -- these offsets can't drift independently
// of FUN_14007c3a0's own start address within a single build.
constexpr const char* kAnchorSignature =
    "48 89 5C 24 08 48 89 74 24 10 57 48 83 EC 20 48 63 D9 41 8B F8 8B CB 8B F2 "
    "E8 ?? ?? ?? ?? 85 C0 0F 84 ?? ?? ?? ?? 48 83 3D ?? ?? ?? ?? 00 "
    "0F 84 ?? ?? ?? ?? 85 F6 74 ??";

// Byte offsets from FUN_14007c3a0's own entry point to each real
// `LEA reg,[rip+disp32]` (or, for the timestamp, `MOV r32,[rip+disp32]`)
// instruction -- each independently verified via DumpRawBytes.java against
// the live binary (re_notes/x64_migration/rawbytes_c3a0_targets.txt), not
// estimated from the decompile's own pseudo-C alone. All four are 7-byte
// instructions (REX + opcode + ModRM(rip) + 4-byte disp).
constexpr ptrdiff_t kFireStructInsnOffset = 0x70;    // -> DAT_140644818 (Fire kbutton)
constexpr ptrdiff_t kReloadStructInsnOffset = 0x1EF; // -> DAT_1406448a4 (Reload kbutton)
constexpr ptrdiff_t kAdsStructInsnOffset = 0xAB4;    // -> DAT_1406448e0 (ADS kbutton)
constexpr ptrdiff_t kTimestampInsnOffset = 0x41;     // -> DAT_141efb764 (shared timestamp)
// CORRECTED 2026-09-04, round 3 (real bug found via a THIRD live test): the
// sixth round above bypassed FUN_14007c3a0's case 0x3b/0x3c entirely to
// dodge its unwanted toggle mutation on DAT_1406e26e0 -- reasoning that the
// kbutton's own held state (matching x86's proven design) was the real
// driver of ADS. Live-tested: WRONG for this specific bind. With the
// kbutton-only approach, ADS did nothing at all (worse than the toggle
// symptom) -- real, direct evidence that DAT_1406e26e0 IS the actual "is
// aiming down sights" flag this engine's aim/FOV/camera code reads, not a
// secondary/cosmetic side effect safe to ignore. Fixed by resolving this
// flag too and FORCING it to our own desired absolute value on the edge
// (1 while held, 0 while not) -- explicit set, never relying on the
// native code's own toggle-on-press-only semantics, which is what broke
// hold behavior in the first place. Still calls FUN_14007e460/e490 on the
// ADS kbutton struct too (whatever secondary state that drives, e.g.
// slowdown/animation, likely still wanted) -- this is additive on top of
// the direct flag write, not a replacement for it. Same offset already
// verified via DumpRawBytes.java (re_notes/x64_migration/
// rawbytes_c3a0_targets.txt) for the case 0x3b `LEA R8,[DAT_1406e26e0]`
// instruction at 0x14007ce3f -- 0x14007c3a0 = 0xA9F.
constexpr ptrdiff_t kAdsToggleFlagInsnOffset = 0xA9F; // -> DAT_1406e26e0 (real "is ADS active" flag)
// Sprint kbutton struct (2026-09-12) -- case 0x3d's own `LEA reg,[rip+disp32]`
// instruction, same class of offset as the four above. Independently decoded
// by hand from re_notes/x64_migration/rawbytes_sprint_struct.txt's raw bytes
// (48 8D 05 40 7A 5C 00 @ 0x14007cead -> resolves to 0x1406448f4) and
// cross-checked against decomp_14007c3a0_full.txt's own `DAT_1406448f4` name
// for case 0x3d/0x3e -- see Hook_SprintTick's own big comment block for the
// full two-angle confirmation trail.
constexpr ptrdiff_t kSprintStructInsnOffset = 0xB0D; // -> DAT_1406448f4 (Sprint kbutton)
// Hold Breath kbutton struct (2026-09-12) -- resolved from case 9's
// ("+breath_sprint" down) FIRST call target, one of the two back-to-back
// kbutton calls that bind fires (the second is kSprintStructInsnOffset's own
// target). Raw bytes independently dumped via DumpRawBytes.java
// (re_notes/x64_migration/rawbytes_holdbreath_struct.txt): the real
// instruction at this offset is `48 8D 05 BC 7B 5C 00` (a genuine 7-byte
// `LEA reg,[rip+disp32]`), whose RIP-relative target computes to
// 0x14064482C -- matches decomp_14007c3a0_full.txt's own `DAT_14064482c`
// name exactly, and matches x86's own original discovery (case 9's real
// disassembly fires two kbutton calls, one Hold Breath's, one Sprint's).
// See Hook_SprintTick's own big Hold Breath comment for the full trail
// (including the structural note that unlike x86's own Hold Breath address,
// which lands INSIDE Fire's kbutton struct as its down[1] field -- a real
// x86-specific aliasing quirk -- x64's struct here is a genuinely separate,
// dedicated kbutton_t, the very next one after Fire's own in the array).
constexpr ptrdiff_t kHoldBreathStructInsnOffset = 0x8C9; // -> DAT_14064482c (Hold Breath kbutton)
constexpr size_t kRipInsnLength = 7;

int* g_fireStruct = nullptr;
int* g_reloadStruct = nullptr;
int* g_adsStruct = nullptr;
int* g_sprintStruct = nullptr;
int* g_holdBreathStruct = nullptr;
volatile uint32_t* g_timestampPtr = nullptr;
volatile uint8_t* g_adsToggleFlag = nullptr;

// ---- CrouchProne (B), x64 (2026-09-05, next task after the remaining-controls
// pass -- deliberately deferred there pending exactly this) ------------------------
//
// The real ambiguity flagged in the earlier deferral (`FUN_14007c3a0`'s own case
// 0x17/0x18 "+stance" down/up bodies -- re_notes/x64_migration/decomp_14007c3a0_full.txt
// -- their own "restore previous posture" semantics on release, checking whether the
// SAVED old posture equals exactly 1) is a real, still-unresolved detail -- but it
// doesn't need resolving to implement this safely. Neither case body reads its own
// `param_3` argument at all (confirmed via the same decompile: `uVar3` used inside
// is `DAT_141efb764`, the shared TIMESTAMP global, not `param_3` -- unlike Fire/ADS/
// Reload's own dispatch, which passes `param_3` straight through to
// `FUN_14007e460`/`e490` as the dual-source identifier that mattered so much this
// session). This means the SAFEST possible implementation is to NOT try to replicate
// `FUN_14007c3a0`'s own internal state machine (hold-duration escalation to prone,
// tap-vs-hold, whatever it turns out to be) at all -- just forward B's real press and
// release edges to the REAL native case dispatch, exactly as a real key bound to
// "+stance" would, and trust the already-correct native logic to handle tap/hold
// internally on its own terms, the same way it already does for a real keyboard
// player. `FUN_14007c3a0` itself is reused as a genuine CALLABLE function here (not
// just an anchor for other offsets, its only use so far) -- its own internal gate
// (`FUN_140078f00()!=0 && DAT_1405145a8!=0`) is a real dvar-handle-existence /
// client-ready check, not a per-frame toggling flag, so it's expected to be stable
// (true) throughout ordinary live gameplay -- this project's own earlier Fire bug
// was NEVER about this gate (confirmed via the source-id root-cause trace), so
// there's no reason to expect it to intermittently block CrouchProne either.
using StanceDispatchFn = void(__fastcall*)(int playerIndex, int caseNumber, int param3);
StanceDispatchFn g_stanceDispatch = nullptr;
constexpr int kCrouchProneCaseDown = 0x17; // "+stance" down
constexpr int kCrouchProneCaseUp = 0x18;   // "-stance" up

// ---- Sniper Fire/ADS fix attempt, x64 (2026-09-05, resumed after the docs/ETA
// pause -- known_issues_x64.md issue #1's "two new bugs found live" thread) ----
//
// Root-cause investigation, not a guess: decompiled FUN_14007c3a0 (this file's own
// g_stanceDispatch) in full (decomp_14007c3a0_full.txt) and matched its case bodies
// against this file's ALREADY-resolved kbutton struct pointers by ADDRESS, not by
// position -- exactly the lesson from x86's own issue #3 ("never trust a bind-index
// as a case number without independent confirmation"). Case 1/2 call
// FUN_14007e460/e490 on `&DAT_140644818`, the SAME address kFireStructInsnOffset
// resolves to g_fireStruct -- confirming case 1 = "+attack" down, case 2 = "-attack"
// up. Case 0xd/0xe call the SAME pair on `&DAT_1406448e0`, matching g_adsStruct
// exactly (kAdsStructInsnOffset's own target) -- confirming case 0xd = "+ads" down,
// 0xe = "-ads" up (case 0xd/0xe ALSO clear the ads-toggle-flag byte first, matching
// this file's own g_adsToggleFlag handling one section up).
//
// The critical part: `FUN_14007c3a0` calls `FUN_14007fc00(playerIndex, caseNumber)`
// as the VERY FIRST thing it does for every non-zero case (decomp_14007c3a0_full.txt
// line 17-19), BEFORE the switch. Decompiling FUN_14007fc00 and its three callees
// (FUN_14007fb30/FUN_14026afa0/FUN_140265a20) shows it's a real client->server
// RELIABLE COMMAND send: gated on real connection-state/demo-override checks, it
// formats the case number into a short string ("n %i" -- confirmed via
// ReadStringAt.java, a genuinely standalone literal, MSVC tail-merged against
// "cubemapShot" et al in the same string pool) and writes it into a 128-entry ring
// buffer (`(seq+1) & 0x7f`, with the exact "EXE_ERR_CLIENT_CMD_OVERFLOW" message
// this engine family uses for its classic Quake3-descended reliable-command
// channel) -- i.e. every REAL bind press/release, including a real keyboard
// "+attack"/"+ads", tells the (local, loopback) server "bind N fired," separately
// from raw usercmd button bits.
//
// This is the SAME mechanism x86's own re_notes/iw5sp.md already flagged and never
// confirmed, months before x64 existed: "**Actionable hypothesis, not confirmed**:
// this project's Fire (RT) is raw usercmd_t button bits, not a synthesized +attack
// bind/command execution. If notifyonplayercommand only fires on real bind/command
// dispatch (not raw usercmd bits), that directly explains [a GSC-side gate never
// firing]... Worth a native-side check... before assuming this is the whole story"
// (Predator Missile section, 2026-07-17). GSC's `notifyonplayercommand("event",
// "+bind")`/`notifyoncommand` primitives are the confirmed native<->script bridge
// for exactly this class of event (re_notes/iw5sp.md, "General pattern confirmed").
// x64's own Fire/ADS implementation (like x86's) calls the real kbutton
// activate/deactivate handlers DIRECTLY, bypassing FUN_14007c3a0/g_stanceDispatch
// entirely -- so this reliable-command notify never fires for controller Fire/ADS,
// unlike a real keyboard press. Leading hypothesis for the sniper-only symptom: a
// sniper-class weapon's bolt-action/scope state machine is plausibly the one weapon
// class whose GSC/native logic needs this notify (a "+attack"/"+ads" bind literally
// happened) rather than relying on raw usercmd state alone, the way most other
// weapon classes evidently can.
//
// Fix: PURELY ADDITIVE. Resolve FUN_14007fc00's real address as a fixed function-to-
// function byte offset from the already-resolved g_stanceDispatch anchor (the same
// anchor-plus-fixed-offset pattern this file already uses for every struct/field
// this function resolves -- here applied to a CODE offset instead of a data offset,
// which holds for the identical reason: both addresses are fixed positions within
// the same static PE image, differing only by the one ASLR base that cancels out in
// the subtraction). Offset computed directly from Ghidra's own two addresses:
// 0x14007fc00 - 0x14007c3a0 = 0x3860. Calling it alongside (not instead of) the
// existing direct kbutton calls cannot regress anything already working -- worst
// case if this hypothesis is wrong, the extra notify is inert; the existing Fire/ADS
// kbutton logic is untouched. NOT YET LIVE-TESTED -- this is a build-verified fix
// attempt pending live confirmation the sniper symptom is actually gone, exactly
// this session's established convention for every other x64 feature so far.
using NotifyBindFn = void(__fastcall*)(int playerIndex, int caseNumber);
NotifyBindFn g_notifyBindDispatch = nullptr;
constexpr ptrdiff_t kNotifyBindFuncOffset = 0x3860; // FUN_14007fc00 - FUN_14007c3a0
constexpr int kFireBindCaseDown = 1;   // "+attack" down -- confirmed via g_fireStruct address match
constexpr int kFireBindCaseUp = 2;     // "-attack" up
constexpr int kAdsBindCaseDown = 0xd;  // "+ads" down -- confirmed via g_adsStruct address match
constexpr int kAdsBindCaseUp = 0xe;    // "-ads" up

// ---- Jump auto-stand (2026-09-05, same day as CrouchProne) -- direct
// instruction: "you need to implement the press a to stand up thing we did for
// x86 too" ---------------------------------------------------------------------
//
// x86 precedent (analog_input_hooks.cpp): ForceStandingViaRealToggle() reads the
// real current stance directly from a fixed +0x1C offset in the player struct
// (kRealStanceFieldAddr), then calls the real ToggleStance(playerIndex, mode)
// with mode SET TO THE CURRENT VALUE -- since ToggleStance's own logic is a
// genuine toggle (`current == mode ? 0 : mode`), passing mode=current always
// resolves to 0 (standing), whether current was 1 (crouch) or 2 (prone).
//
// x64 has no standalone ToggleStance(mode) function to call the same way --
// crouch/prone are driven through FUN_14007c3a0's own FIXED case numbers
// instead (0x48 = togglecrouch, toggles 0<->1; 0x49 = toggleprone, toggles
// 0<->2 -- both confirmed via decomp_14007c3a0_full.txt, and 0x48 is
// independently corroborated as the SAME case number x86's own togglecrouch
// dispatch uses, re_notes/iw5sp.md's "Found togglecrouch's REAL dispatch" note).
// Replicating x86's exact "call toggle with mode=current" trick just means
// picking the MATCHING case for whatever the current stance actually is: if
// current==1, case 0x48's own `(current != 1)` check is false, forcing 0; if
// current==2, case 0x49's own `(current != 2)` check is false, forcing 0. Same
// result as x86's dynamic-mode call, just expressed through x64's fixed-case
// dispatch instead.
//
// Second call site added 2026-09-12: Hook_SprintTick (above, earlier in this
// file) now also calls this on Sprint's own rising edge while crouched/prone
// (excluding ADS), matching x86's InjectControllerSprint exactly -- same
// function, no new logic needed, just a second, independent trigger.
//
// The real stance field itself: the decompile's `DAT_1406e26fc` is genuinely
// `DAT_1406e26e0 + 0x1c` -- confirmed by re-reading disasm_14007c3a0_full.txt's
// own case 0x49/0x4a/0x4b blocks, every one of them `[RAX + R8*0x1 + 0x1c]` off
// the SAME `LEA R8,[0x1406e26e0]` base kAdsToggleFlagInsnOffset already resolves
// for the ADS toggle flag. No new signature scan needed -- just a fixed +0x1c
// byte offset on top of the ALREADY-resolved g_adsToggleFlag pointer. This also
// mirrors x86's own design one level deeper: x86's kRealStanceFieldAddr is
// itself a fixed +0x1C offset from its own per-player struct base -- the exact
// same offset, 0x1C, carried over identically to x64's analogous struct. Reads
// here are direct/read-only, same as x86's own GetRealStance(); writes always
// go through the real case dispatch (which re-checks the same stance-lock guard
// bytes FUN_14007e430 itself checks), never a raw memory write.
constexpr ptrdiff_t kStanceFieldByteOffset = 0x1c; // g_adsToggleFlag base + this = real stance int
constexpr int kToggleCrouchCase = 0x48; // "togglecrouch" -- toggles stance 0<->1
constexpr int kToggleProneCase = 0x49;  // "toggleprone" -- toggles stance 0<->2

int GetRealStanceX64()
{
    if (!g_adsToggleFlag) return 0;
    auto* stanceField = reinterpret_cast<volatile int*>(
        reinterpret_cast<uintptr_t>(g_adsToggleFlag) + kStanceFieldByteOffset);
    return *stanceField;
}

void ForceStandingViaRealToggleX64()
{
    if (!g_stanceDispatch) return;
    int current = GetRealStanceX64();
    if (current == 1) g_stanceDispatch(0, kToggleCrouchCase, 0);      // crouched -> stand
    else if (current == 2) g_stanceDispatch(0, kToggleProneCase, 0);  // prone -> stand
}

// FUN_14007e460 -- the real kbutton "activate source" handler (down).
// Signature via DumpSigBytes.java (re_notes/x64_migration/
// impl_sig_14007e460.txt) -- every branch target in this function is a
// SHORT (rel8) jump to another point within this same function; wildcarded
// per this file's own established future-proofing convention even though
// they're deterministic for this specific build.
constexpr const char* kKbuttonActivateSignature =
    "44 8B 09 41 3B D1 ?? ?? 8B 41 04 3B D0 ?? ?? 45 85 C9 ?? ?? 89 11 ?? ?? "
    "85 C0 ?? ?? 89 51 04 80 79 10 00 ?? ??";

// FUN_14007e490 -- the real kbutton "deactivate source" handler (up). Same
// short-rel8-jump wildcarding convention as above.
constexpr const char* kKbuttonDeactivateSignature =
    "44 8B 09 44 3B CA ?? ?? 33 C0 89 01 8B 41 04 ?? ?? 39 51 04 ?? ?? "
    "33 C0 89 41 04 45 85 C9 ?? ?? 85 C0 ?? ??";

using KbuttonActivateFn = void(__fastcall*)(int* kbutton, int sourceId, int timestamp);
using KbuttonDeactivateFn = void(__fastcall*)(int* kbutton, int sourceId, int timestamp);
KbuttonActivateFn g_kbuttonActivate = nullptr;
KbuttonDeactivateFn g_kbuttonDeactivate = nullptr;

// Any fixed, non-zero value works -- this engine has no real "controller"
// keycode space to collide with, so a single shared synthetic id is safe
// reused across Fire/Reload/ADS (each has its own independent kbutton
// struct, no cross-bind collision risk). Chosen well outside any real
// VK/ASCII keycode range (0-255) purely so it's visually obvious in a
// memory dump that it's synthetic, not a real key.
constexpr int kSyntheticSourceId = 0x1000;

// FUN_1400823b0(playerIndex) -- the confirmed x64 live-gameplay pause TOGGLE
// (re_notes/x64_migration/README.md section 1g, "Same-day follow-up #5").
// Self-contained: reads the current SetMenuState mode via FUN_14029b470 and
// calls SetMenuState(player, 2) (open "pausedmenu") if not already paused, or
// SetMenuState(player, 0) (resume, Cvar_Set cl_paused 0) if it is -- a clean,
// complete toggle. Call ONLY on the press edge (never on release) -- calling it
// on release too would immediately toggle it right back. Signature via
// DumpSigBytes.java (re_notes/x64_migration/impl_sig_1400823b0.txt) --
// distinctive combination of two specific real dvar-handle reads and two real
// external calls (FUN_1401a1e50/FUN_1401a1e40); every flagged byte in this span
// is a genuine RIP-relative/rel32 reference, no RSP-relative false positives.
constexpr const char* kPauseToggleSignature =
    "40 53 48 83 EC 20 ?? ?? ?? ?? ?? ?? ?? 48 63 D9 83 78 10 00 "
    "?? ?? ?? ?? ?? ?? ?? 84 C0 ?? ?? ?? ?? ?? ?? ?? 84 C0 "
    "?? ?? ?? ?? ?? ?? ?? ?? ?? 48 85 C0 ?? ??";

using PauseToggleFn = void(__fastcall*)(int playerIndex);
PauseToggleFn g_pauseToggle = nullptr;

// Diagnostic-only, added 2026-09-04 after a THIRD "needs a click for input"
// live-test failure (the DAT_1406e4774 bit-0x800 force-clear above did NOT
// resolve it) -- direct user correction: "it was the exact same issue we had
// way back before we ever released ncp as 0.1", pointing at x86's own
// documented issue #1 (known_issues.md, "Buy-station + pause menu completely
// breaks movement"), whose real root cause was a per-player gate struct at
// x86 address 0x00B36210 (a "menu active" bit, 0x10) paired with a "game
// state" field at +8 (0x00B36218). `DAT_1406e2550` is x64's own confirmed
// structural equivalent of 0x00B36210 -- same relative +8 pairing with
// `DAT_1406e2558` (`re_notes/x64_migration/decomp_buttons_pause_weapnext.txt`:
// FUN_14007eaf0 reads `(&DAT_1406e2558)[player*100]` as its own "state"
// value right next to `DAT_1406e2550`'s own bit-0x10 "menu active" read), and
// this project's ALREADY-confirmed-working Pause hook (FUN_1400823b0) reads
// this SAME `DAT_1406e2550` bit 0x10 directly. Resolved here purely for
// DIAGNOSTIC logging (NOT forced/written -- unlike x86's issue #1, whose real
// bug was OUR OWN code unconditionally forcing this exact class of bit,
// desyncing real engine state; deliberately not repeating that mistake
// blind) -- the next live test needs REAL DATA on what this value (and
// DAT_1406e4774) actually read during the "stuck" window vs. after a real
// click resolves it, rather than a fourth blind guess. Resolved via the
// ALREADY-scanned `kPauseToggleSignature` match (no separate scan) plus a
// fixed, directly-verified offset (`+0x70`, `re_notes/x64_migration/
// disasm_1400823b0_full.txt` + `rawbytes_1406e2550.txt`) to the real
// `LEA RAX,[rip+disp32]` instruction (7 bytes).
constexpr ptrdiff_t kMenuActiveGateInsnOffset = 0x70;
// DAT_1406e2550 equivalent -- resolved for the "needs a click" investigation's
// own diagnostic heartbeat (still used there), and ALSO now read for its real,
// originally-intended purpose: this project's confirmed x64 IsMenuActive()
// equivalent (bit 0x10), gating Jump below the same way x86's own
// InjectControllerButtons already does. Read-only either way -- never forced.
uint32_t* g_menuActiveGateFlag = nullptr;

// FUN_1400706d0(playerIndex, direction) -- the confirmed x64 weapnext dispatcher
// (re_notes/x64_migration/sprint_weapnext_x64.md), reached via FUN_14007c3a0
// case 0x42 during a real keypress but called directly here, same as x86's own
// weapnext implementation calls its terminal function directly rather than
// routing through the whole dispatch chain. Gates itself internally on real
// weapon-busy/reload-state exclusion checks before calling
// FUN_140074570(player, direction, 0, 0), the actual weapon-slot-cycling
// function -- safe to call unconditionally on the press edge, the same way a
// real bound key's press would be. direction=1 (matches FUN_140074570's own
// documented "param_2 != 0 steps +1" forward-cycle convention) -- call ONLY on
// the press edge, never on release, matching a real one-shot command bind.
constexpr const char* kWeaponNextSignature =
    "48 89 5C 24 08 57 48 83 EC 20 48 83 3D ?? ?? ?? ?? 00 8B FA 8B D9 "
    "0F 84 ?? ?? ?? ?? F7 05 ?? ?? ?? ?? 08 0C 00 00 0F 85 ?? ?? ?? ?? "
    "F7 05 ?? ?? ?? ?? 80 08 00 00 0F 85 ?? ?? ?? ?? "
    "F6 05 ?? ?? ?? ?? 02 0F 85 ?? ?? ?? ?? 8B 15 ?? ?? ?? ?? 8B CA";

using WeaponNextFn = void(__fastcall*)(int playerIndex, int direction);
WeaponNextFn g_weaponNext = nullptr;

// ---- Remaining controls, x64 (2026-09-04, direct instruction "add all remaining
// controls that are missing in this pass") -----------------------------------------
//
// Re-read x86's own InjectControllerButtons/InjectControllerDpad in full before
// writing any of this -- real, important finding: x86 does NOT drive Melee/
// Tactical/Lethal/Jump/Interact through kbutton down/up calls at all (unlike Fire/
// ADS/Reload) -- it ORs raw bits directly into `usercmd_t.buttons` (a uint at
// `+0x04`, confirmed in `re_notes/iw5sp.md`'s own struct-layout table) every tick,
// then does one additive `*(uint32_t*)(cmd+4) |= out;` at the end. Since x64's
// `usercmd_t` is ALREADY confirmed identical at +0x1c/+0x1d (forwardmove/
// rightmove, this session's own Movement work), the SAME `+0x04` buttons field is
// overwhelmingly likely to carry over too (same underlying struct, same recompile)
// -- so this mirrors x86's own proven raw-bit mechanism directly rather than
// routing through FUN_14007c3a0's kbutton dispatch (a DIFFERENT, unproven-for-
// this-purpose mechanism this session only verified for Fire/ADS/Reload, which
// x86 itself drives through real kbuttons too, unlike these five). Real bit
// values copied directly from x86's own confirmed constants: Melee=0x4,
// Lethal(frag)=0x4000, Tactical(smoke)=0x8000, Jump(+gostand)=0x400,
// Interact=0x8 (dual-purpose with Reload's own physical button, same as x86).
//
// D-pad actionslot is the one exception -- x86 itself ALSO calls a real function
// there (ActionSlotDown/Up), not raw bits, and x64's own confirmed equivalent
// (FUN_14006dee0, decompiled in full this round --
// re_notes/x64_migration/decomp_actionslot_stance.txt) matches that shape: a
// genuine "use this actionslot item now" one-shot action call (internally
// dispatches to weapon-switch/killstreak-use logic based on the slot's own
// equipped-item type), not a hold-based kbutton -- called once on the press edge
// only, same pattern as weapnext.
//
// CrouchProne (B) is DELIBERATELY NOT included this round. x64's own real
// stance-lock gate (FUN_14007e430, confirmed via real disassembly this round --
// re_notes/x64_migration/disasm_14007e430.txt -- a genuine IsStanceLocked(player)
// equivalent, structurally matching x86's own FUN_0057d190) is real and
// understood, but the actual toggle logic in FUN_14007c3a0's own case 0x17/0x18
// (+stance down/up) has real, unresolved ambiguity in its own "restore previous
// posture" semantics on release (checks whether the SAVED old posture equals 1
// specifically, not a simple restore-to-saved-value) that this pass's decompile
// alone doesn't cleanly resolve. Given this project's own documented history of
// genuinely nasty stuck-crouch/stuck-prone regressions (CLAUDE.md's "Crouch
// 'needs an initial click at launch'" section, and the live x86 incident that
// motivated ToggleStance's own real-toggle redesign in the first place), shipping
// this blind risks a real softlock -- honestly deferred rather than guessed, per
// this project's own "checking is far cheaper than digging" standard extended to
// "guessing wrong here is a genuinely worse regression than not shipping it yet."
constexpr int kMeleeUsercmdBit = 0x4;
constexpr int kLethalUsercmdBit = 0x4000;
constexpr int kTacticalUsercmdBit = 0x8000;
constexpr int kJumpUsercmdBit = 0x400;
constexpr int kInteractUsercmdBit = 0x8;

// FUN_14006dee0(playerIndex, slotIndex) -- the confirmed x64 D-pad actionslot
// handler, decompiled in full this round
// (re_notes/x64_migration/decomp_actionslot_stance.txt): reads the equipped
// item's own type at the given slot and dispatches to the correct real action
// (weapon-switch-class call, a second distinct action call, or a raw flag-bit
// set) accordingly -- a genuine "use this slot now" one-shot, matching x86's
// own ActionSlotDown/Up shape closely enough to call directly the same way
// weapnext already is. Signature via DumpSigBytes.java
// (re_notes/x64_migration/impl_sig_14006dee0.txt).
constexpr const char* kActionSlotSignature =
    "48 89 5C 24 10 56 48 83 EC 20 48 63 DA 8B F1 "
    "E8 ?? ?? ?? ?? 85 C0 0F 84 ?? ?? ?? ?? 4C 8D 1D ?? ?? ?? ?? 8B D3 "
    "49 8B CB E8 ?? ?? ?? ?? 84 C0 0F 84 ?? ?? ?? ?? 41 8B 8C 9B E0 5F 01 00";

using ActionSlotFn = void(__fastcall*)(int playerIndex, int slotIndex);
ActionSlotFn g_actionSlot = nullptr;

// ---- D-pad Left / actionslot4 squadmate call-in: same narrowly-scoped exception
// x86 already needed (analog_input_hooks.cpp's own SendSyntheticActionSlot4Key,
// user-approved 2026-07-16), ported here 2026-09-05 as the leading fix for the
// live "D-pad sometimes diff keys used" report (known_issues_x64.md issue #1).
// Re-read x86's own comment in full before porting -- the real finding there:
// AI-squadmate call-ins (Survival, same buy-station/same D-pad-Left slot as
// turret call-ins, which DO work via the plain native call) failed 100% of the
// time via the direct native action-slot call, most likely because a Survival-
// only GSC script watches for a genuine WM_KEYDOWN/KEYUP ('4', the real bound
// key for +actionslot4) that a native call alone never produces. x64's own
// FUN_14006dee0 is confirmed structurally equivalent to x86's ActionSlotDown
// (same "use this slot now" one-shot dispatch, see kActionSlotSignature's own
// comment above) so the same gap is expected to exist here too, unconfirmed
// until live-tested. GetGameWindow() (d3d9_hook.cpp) already exists for x64
// unmodified -- no separate WndProc-subclass prerequisite was needed.
void SendSyntheticActionSlot4KeyX64(bool down)
{
    HWND hwnd = GetGameWindow();
    if (!hwnd) return;
    if (down) {
        PostMessageA(hwnd, WM_KEYDOWN, '4', 0x00000001);
    } else {
        PostMessageA(hwnd, WM_KEYUP, '4', 0xC0000001);
    }
}

// ---- Survival ready-up (hold Y ~740ms): same narrowly-scoped exception x86
// already needed (analog_input_hooks.cpp's own SendSyntheticF5/
// InjectControllerWeaponNext, user-approved 2026-07-15), ported here
// 2026-09-12 per re_notes/x64_feature_parity_audit.md (confirmed zero x64
// wiring for this control). Re-read x86's own comment block in full before
// touching this -- the real finding there: F5/"skip" (Survival's real
// between-wave ready-up trigger) has no locatable native dispatch after an
// extensive multi-technique search (real +gostand kbutton: wrong system;
// togglecrouch/FUN_0057d2c0 mode variants: inert or a genuine unrelated
// prone-toggle that got a player stuck prone live; GSC
// notifyonplayercommand/VM_Notify: real but needs live GSC-VM-stack
// manipulation from an async hook, too risky). That search was exhaustive
// for x86 and is NOT being re-run here -- the game data/GSC scripts are
// unchanged by the x64 recompile, so the same "no native call" conclusion is
// assumed to carry over per this task's own explicit scope.
//
// Mechanism, identical to x86's SendSyntheticF5: IW5 has no DirectInput
// import at all (CLAUDE.md's own key finding), so keyboard input is real
// WM_KEYDOWN/WM_KEYUP window messages -- synthesizing a real F5 via
// PostMessageA at the game's own HWND is indistinguishable from an actual
// keypress, and safe to fire even outside the one context it matters (a
// misplaced synthetic F5 is simply ignored by the game, same as a real,
// misplaced press would be). GetGameWindow() (d3d9_hook.cpp) is already
// confirmed architecture-neutral and in active x64 use -- same call this
// file's own SendSyntheticActionSlot4KeyX64 above already uses, direct
// template for this function.
//
// ONE HONEST, DELIBERATE DIFFERENCE FROM x86: x86 additionally gates the
// fire behind IsInSurvivalMode() (a mapname-dvar read via x86's raw
// Dvar_FindVar-equivalent, FUN_0062abe0 @ 0x0062abe0). x64's own equivalent
// of that raw dvar-lookup function is a genuinely unresolved RE target --
// real_settings.cpp's FindDvar()/GetDvarString() are x86-only (the __asm
// body is `#ifdef _M_IX86`-guarded, a safe no-op returning nullptr on x64,
// not a crash, but also not a real lookup) and analog_input_hooks_x64.cpp's
// own GetLookAccelerationScaleX64 comment already documents this exact gap
// (GetEffectiveFov/Dvar_FindVar addresses "genuinely unresolved RE targets,
// not yet found" -- known_issues_x64.md issue #1). Rather than block this
// port on a separate RE task outside its scope, this fires unconditionally
// on the hold-threshold edge, relying on the SAME "safe by construction"
// reasoning x86's own comment already documents as sufficient even without
// the mode gate (a misplaced F5 outside Survival's ready-up wait is inert).
// Revisit if x64's Dvar_FindVar equivalent is ever resolved for other work.
void SendSyntheticF5X64()
{
    HWND hwnd = GetGameWindow();
    if (!hwnd) return;
    // lParam bit 24 (extended-key flag) doesn't apply to F5; repeat count 1,
    // scan code left 0 -- the game reads the virtual-key (wParam), matching
    // x86's own SendSyntheticF5 exactly (same VK, same lParam values).
    PostMessageA(hwnd, WM_KEYDOWN, VK_F5, 0x00000001);
    PostMessageA(hwnd, WM_KEYUP, VK_F5, 0xC0000001);
    LogFromController("[x64-ready-up-diag] SendSyntheticF5X64 fired");
}

// ---- Back -> real +scores (scoreboard/objectives) via key synthesis, x64 port
// (2026-09-13) -- same narrowly-scoped exception x86 already needed
// (analog_input_hooks.cpp's own InjectControllerScoreboard, user-approved
// 2026-07-17, "THIRD and final narrow exception to the no-OS-level-input-
// emulation rule"). Re-read x86's own comment block in full before touching
// this -- the real finding there: `+scores` is a plain keyboard bind
// (`bind TAB "+scores"`), not a per-frame usercmd button/kbutton at all, read
// directly by whatever UI draws the scoreboard/objectives overlay, so a genuine
// WM_KEYDOWN/KEYUP is the only known way to trigger it, same category as
// SendSyntheticF5X64/SendSyntheticActionSlot4KeyX64 above.
//
// x86's OWN function (InjectControllerScoreboard, analog_input_hooks.cpp) has
// no architecture guard at all and would compile fine here unchanged -- per
// this session's own parity audit (re_notes/x64_feature_parity_audit.md row
// 30), it was simply never CALLED from x64's own input pipeline. This is a
// pure wiring port, not new logic: same PostMessageA(VK_TAB) mechanism,
// re-expressed as a bool-down/up function matching this file's own
// SendSyntheticActionSlot4KeyX64 shape so it can be called from
// Hook_MovementTick's existing edge-tracking block below.
//
// CONFIRMED GENUINE NO-OP IN CAMPAIGN/SURVIVAL (Xbox 360 console testimony,
// 2026-08-04, known_issues.md issue #28): there is no scoreboard UI in SP at
// all, on any platform. This port exists for completeness/consistency (the
// function costs nothing to wire in) and for real future value once
// Multiplayer support ships with its own actual scoreboard -- NOT to make a
// visible feature appear in SP. Live-testing this should confirm Back
// continues to do nothing visible in Campaign/Survival, matching real console
// behavior exactly -- that is success, not a regression.
//
// Hold-through-passthrough, not tap/toggle -- mirrors x86 exactly: Back down
// -> TAB down, Back up -> TAB up.
void SendSyntheticScoreboardKeyX64(bool down)
{
    HWND hwnd = GetGameWindow();
    if (!hwnd) return;
    if (down) {
        PostMessageA(hwnd, WM_KEYDOWN, VK_TAB, 0x00000001);
    } else {
        PostMessageA(hwnd, WM_KEYUP, VK_TAB, 0xC0000001);
    }
}

// Edge-tracking state for the Y hold-vs-tap split -- mirrors x86's own
// g_yPressStartMs/g_yReadyUpFired (analog_input_hooks.cpp) exactly. The
// existing g_weaponSwitchHeldX64 bool (declared further below alongside this
// file's other g_*HeldX64 edge trackers) already tracks the raw held state;
// these two add the hold-duration/debounce layer on top of it.
DWORD g_yPressStartMsX64 = 0;
bool g_yReadyUpFiredX64 = false; // debounces per physical Y hold -- only
                                  // fires once, even if held past threshold

// XInput D-pad bit values -- standard, shared constants, matching
// analog_input_hooks.cpp's own identical definitions (kept local here rather
// than cross-file since they're plain protocol constants, not real state).
constexpr unsigned short kXI_DPAD_UP_X64 = 0x0001;
constexpr unsigned short kXI_DPAD_DOWN_X64 = 0x0002;
constexpr unsigned short kXI_DPAD_LEFT_X64 = 0x0004;
constexpr unsigned short kXI_DPAD_RIGHT_X64 = 0x0008;

// Edge-tracking state, one bool per logical action -- mirrors x86's own
// g_adsHeld/g_reloadHeld/g_startHeld pattern (analog_input_hooks.cpp).
bool g_fireHeldX64 = false;
bool g_adsHeldX64 = false;
bool g_reloadHeldX64 = false;
bool g_pauseHeldX64 = false;
bool g_weaponSwitchHeldX64 = false;
bool g_jumpHeldX64 = false;                 // rising-edge diag not needed, just for parity w/ other bools
bool g_interactButtonWasHeldX64 = false;
DWORD g_interactPressStartMsX64 = 0;        // matches x86's own hold-to-interact timing (g_modConfig.interactHoldThresholdMs)
bool g_dpadHeldX64[4] = { false, false, false, false }; // Up, Down, Left, Right -- matches kXI_DPAD_*_X64 order
bool g_crouchProneHeldX64 = false;
bool g_scoreboardHeldX64 = false; // Back -> +scores key-synth, see SendSyntheticScoreboardKeyX64
// B is dual-purpose on x64 too (crouch/prone toggle vs. menu-back/ESC-forward),
// same as x86 -- this shared flag is x64's own equivalent of x86's
// g_currentBPressTouchedMenu (analog_input_hooks.cpp), maintained by
// InjectControllerMenuBackX64 (the always-on-tick B/ESC-forward function, added
// 2026-09-12) and read here by the CrouchProne dispatch below, so a B press that
// touched an open menu at any point during its hold never ALSO toggles real
// native stance underneath the menu (a genuine stuck-crouch/prone regression
// risk otherwise -- see CLAUDE.md's "Crouch 'needs an initial click at launch'"
// history and known_issues.md issue #13 for why x86 needed this exact fix).
bool g_currentBPressTouchedMenuX64 = false;

// Real fix for "obvs cant unpause when paused" (2026-09-04, live-confirmed
// bug): Hook_MovementTick below rides FUN_14007d9f0, part of the per-frame
// GAMEPLAY SIMULATION pipeline -- which halts entirely while genuinely
// paused, by design (same architecture x86 already documented for its own
// InjectAllControllerInput/InjectControllerPauseMenu split). That's fine
// for movement/look/buttons (meaningless while paused anyway) but means
// Start's second press could never be observed to CLOSE pause, only ever
// open it. Declared extern "C" -- even though it's lexically inside this
// anonymous namespace, that gives it genuine external linkage (this
// project's own established MSVC linkage lesson, CLAUDE.md's "Checking is
// far cheaper than digging") -- so analog_input_hooks.cpp's own
// InjectMenuInputTick (the WndProc-subclass + SetTimer-driven tick that
// keeps running even during pause, already confirmed firing unconditionally
// on x64) can poll it too. The exact same fix shape as x86's own real fix
// for this identical bug class, just reached one architecture generation
// later.
extern "C" void PollPauseToggleX64()
{
    if (!g_pauseToggle) return;
    unsigned short xiButtons = 0;
    unsigned char leftTrigger = 0, rightTrigger = 0;
    if (!Controller_GetRawButtonsAndTriggers(xiButtons, leftTrigger, rightTrigger)) return;

    bool pauseHeld = IsPhysicalHeld_Exported(g_buttonMap.pause, xiButtons, leftTrigger, rightTrigger);
    if (pauseHeld && !g_pauseHeldX64) {
        g_pauseToggle(0);
    }
    g_pauseHeldX64 = pauseHeld;
}

// ---- Menu-focus / itemDef-array tracking, x64 port (2026-09-12) -------------------
//
// Real x64 equivalent of x86's `TryGetRealFocusedGroupAndIndex`/`GetMenuStackDepth`/
// `GetTopmostActiveMenu` (analog_input_hooks.cpp) -- closes the parity gap
// known_issues_x64.md documented under "Real x64 port of menu-focus/itemDef-position
// tracking": x86's raw itemDef-array walk hardcodes a 4-byte pointer stride and
// 32-bit-width struct offsets that read misaligned garbage on x64 (safely caught by
// its own LooksSane() check, never crashes, just always returns false -- the reason
// the Custom Options screen's real open-trigger and every glyph/hint-icon request
// have been unreachable on x64 until now). Read x86's own full implementation first
// (this project's standing compare-to-x86-original rule) before reading the below --
// the STRUCTURE of the logic is intentionally unchanged, only the offsets/stride are
// re-derived for real 64-bit pointer width.
//
// Every offset below was independently re-derived via a dedicated Ghidra decompile
// pass (re_notes/x64_migration/decomp_gettopmostmenu_x64.txt,
// decomp_menuctx_helpers_x64.txt, decomp_itemnav_x64.txt, decomp_itemhelpers2_x64.txt,
// decomp_itemfocus_x64.txt) and cross-checked against MULTIPLE independent real
// consumers before being trusted -- per this project's own issue #3 lesson ("never
// trust an offset without independent confirmation"), not taken from a single call
// site:
//   - The real x64 UI-context global (x86's `kMenuStackCtx` equivalent) is
//     `DAT_142605050` -- a fixed, single-instance data address confirmed via its own
//     literal `LEA RCX,[0x142605050]` in `FUN_14029baa0` (the real x64
//     `Menu_KeyEvent` caller/resume-path, `re_notes/x64_migration/
//     impl_sig_14029baa0.txt`) and independently re-used, unmodified, across every
//     other menu-stack helper decompiled this pass (`FUN_1402acef0`, `FUN_1402ac9c0`,
//     `FUN_1402ad530`, `FUN_1402ad560`, `FUN_1402aa7e0`, `FUN_1402aaa80`).
//   - `ctx + 0x14C0` = the real open-menu-stack depth (an int) -- confirmed via TWO
//     independent functions reading the identical offset (`FUN_1402aaa80`,
//     `FUN_1402ad530`). x86's own equivalent is `kMenuStackCtx + 0xA7C`.
//   - `ctx + 0x1440` = the real open-menu stack array base, 8 bytes/entry (a pointer
//     array, matching x64 pointer width) -- confirmed via the same two functions.
//   - `FUN_1402aaa80(ctx)` = the real x64 `GetTopmostActiveMenu()` equivalent (x86's
//     `FUN_00547980`) -- confirmed via its OWN call site in `FUN_14029baa0`
//     (`plVar3 = FUN_1402aaa80(&DAT_142605050);` immediately followed by passing
//     `plVar3` into `FUN_1402aac50`, the confirmed x64 `Menu_KeyEvent` -- the exact
//     same "resolve the topmost menu, then route input into it" shape x86's own
//     `ForwardKeyToMenu` uses) AND structurally, by its own disassembly
//     (`re_notes/x64_migration/impl_sig_1402aaa80.txt`): walks the stack top-down
//     from `ctx+0x14C0`/`ctx+0x1440`, returning the first entry whose per-player
//     flags at `entry+0x58+player*4` have bits 0x4 and 0x2 both set -- the exact
//     "genuinely visible/focused" test x86's own `FUN_00547980` is documented to run.
//   - The returned "menu" pointer's item count/array live at `menu+0xB8`
//     (`0x17*8` -- x86's `menu+0xa8`) and `menu+0xC0` (`0x18*8` -- x86's `menu+0xac`),
//     confirmed via THREE independent consumers agreeing on both offsets
//     (`FUN_1402aac50`, `FUN_1402ac5d0`, `FUN_1402ac6f0` -- the real x64
//     `Menu_KeyEvent`/list-up-nav/list-down-nav functions).
//   - Each itemDef pointer's own per-player focus flags live at `item+0x50+player*4`
//     (x86's `item+0x48`, no player index there since x86 never splits this field per
//     player) -- confirmed via FIVE independent consumers testing the identical
//     `(flags & 4) != 0 && ((flags >> 1) & 1) != 0` pair (`FUN_1402aac50`,
//     `FUN_1402ad560`, `FUN_1402b21b0`, `FUN_1402b1de0`, `FUN_1402a7f00`) -- the exact
//     same bit pair x86's own function tests.
//   - Each itemDef's own name pointer lives at `item+0x0` (unchanged from x86 --
//     the first field of a struct can never shift regardless of pointer width) --
//     confirmed via TWO independent consumers directly string-comparing
//     `*itemPtr` against known literal item-name strings (`FUN_1402ac6f0`:
//     `FUN_1402ca760(*puVar2,"playlist_listing")` and friends; `FUN_1402a4580`: the
//     identical pattern against `*param_3`), not merely assumed from struct-layout
//     convention.
// Player index is hardcoded to 0 throughout (`kLocalClientIndexX64`), matching this
// file's own existing convention (`g_menuActiveGateFlag`'s `player*400` indexing
// elsewhere in this file always uses local player 0 too) and x86's own
// single-player-only design for this exact function.
namespace {

// FUN_14029baa0's own real prologue -- signature via DumpSigBytes.java
// (re_notes/x64_migration/impl_sig_14029baa0.txt). The two `MOV qword ptr
// [RSP+xx],reg` stack-spill stores at the very start are RSP-relative, NOT
// PC-relative -- DumpSigBytes.java's own reference-based heuristic over-flags them
// (the same false positive this file's kPmoveTickSignature comment already
// documents for LEA RBP,[RSP-0x80]), kept literal here rather than wildcarded.
// Every real CALL/JZ rel32 IS wildcarded (opcode byte(s) kept literal, only the
// displacement bytes masked) since those genuinely shift between builds.
constexpr const char* kUiContextAnchorSignature =
    "48 89 6C 24 10 48 89 74 24 18 57 48 83 EC 20 8B E9 41 8B F8 48 8D 0D "
    "?? ?? ?? ?? 8B F2 E8 ?? ?? ?? ?? 85 C0 0F 84 ?? ?? ?? ?? E8 ?? ?? ?? ?? "
    "84 C0 0F 84 ?? ?? ?? ?? E8 ?? ?? ?? ??";
// Byte offset (from the signature match's own start) of the `LEA RCX,[rip+disp32]`
// instruction that loads `&DAT_142605050` -- 7 bytes long (3-byte opcode/ModRM +
// 4-byte disp32), disp32 is the instruction's own last 4 bytes so
// SigScan::ResolveRipRelative applies directly (no ResolveRipRelativeAt needed).
constexpr ptrdiff_t kCtxLoadInsnOffset = 0x14;
void* g_uiMenuContextX64 = nullptr;

// FUN_1402aaa80(ctx) -- the confirmed x64 GetTopmostActiveMenu() equivalent (see this
// section's own header comment for the full confirmation trail). Signature via
// DumpSigBytes.java (re_notes/x64_migration/impl_sig_1402aaa80.txt) -- every flagged
// byte is a genuine short (rel8) conditional-jump displacement, wildcarded whole
// (2 bytes) per this file's own convention for that instruction shape.
constexpr const char* kGetTopmostActiveMenuSignature =
    "8B 91 C0 14 00 00 83 EA 01 ?? ?? 4C 63 09 48 63 C2 48 8D 0C C1 48 81 C1 "
    "40 14 00 00 0F 1F 40 00 4C 8B 01 43 8B 44 88 58 A8 04 ?? ?? D1 E8 24 01 "
    "?? ?? 43 F6 44 88 58 04 ?? ?? 48 83 E9 08 83 EA 01 ?? ?? 33 C0 C3 49 8B C0 C3";

using GetTopmostActiveMenuFnX64 = long long(__fastcall*)(void* ctx);
GetTopmostActiveMenuFnX64 g_getTopmostActiveMenuX64 = nullptr;

// FUN_1402aac50(ctx, menu, keyCode, isDown) -- the confirmed x64 combined equivalent
// of x86's ForwardKeyToMenu (0x004d9850) + the function its own non-ESC branch calls
// (FUN_004dfd30) -- found 2026-09-12 while porting native D-pad+A/B menu navigation.
// Full decompile: re_notes/x64_migration/keyhandler_1402aac50_full.txt. Confirmed via
// its own internal switch(keyCode), which matches x86's FUN_004dfd30 switch case-for-
// case: {9,0x9b,0x9d,0xbd,0xcd} -> FUN_1402ac5d0 (next-item, matches x86 Group A ->
// FUN_006253d0), {0x9a,0x9c,0xb7,0xce} -> FUN_1402ac6f0 (prev-item, matches x86 Group
// B -> FUN_00625290), {0xd,0xbf,0xca} -> select/activate (matches x86's Enter case,
// 0xd), and 0x1b -> ESC/back handling. Confirmed real call site
// (re_notes/x64_migration/keyhandler_callers_1402aac50.txt, FUN_14029baa0, this
// project's own confirmed x64 key-event-resume path):
//     plVar3 = (longlong *)FUN_1402aaa80(&DAT_142605050);   // = GetTopmostActiveMenuX64()
//     FUN_1402aac50(&DAT_142605050, plVar3, keyCode, isDown);
// i.e. `ctx` and `menu` are exactly this file's own already-resolved
// g_uiMenuContextX64/GetTopmostActiveMenuX64() -- no new context-resolution needed,
// just this one additional signature. Signature via DumpSigBytes.java
// (re_notes/x64_migration/impl_sig_1402aac50.txt) -- the first instruction (MOV
// qword ptr [RSP+0x18],RBX, a stack spill) is flagged PC-RELATIVE by that script's
// own over-eager reference heuristic, kept literal here per this file's own
// established false-positive lesson (same as kUiContextAnchorSignature's prologue);
// every genuine RIP-relative LEA/MOV/CMP disp32 and CALL/JMP/Jcc rel32/rel8 IS
// wildcarded.
constexpr const char* kMenuKeyEventSignature =
    "48 89 5C 24 18 55 57 41 55 41 56 41 57 48 81 EC 50 02 00 00 "
    "45 33 FF 45 8B E9 45 8B F7 41 8B E8 44 39 35 ?? ?? ?? ?? "
    "48 8B FA 48 8B D9 ?? ?? 45 85 C9 ?? ?? "
    "48 8B 15 ?? ?? ?? ?? E8 ?? ?? ?? ?? E9 ?? ?? ?? ?? "
    "44 39 35 ?? ?? ?? ?? ?? ?? 45 85 ED ?? ?? "
    "48 8B 15 ?? ?? ?? ?? E8 ?? ?? ?? ?? "
    "85 C0 ?? ?? 44 89 3D ?? ?? ?? ?? 4C 89 3D ?? ?? ?? ??";

using MenuKeyEventFnX64 = void(__fastcall*)(void* ctx, void* menu, uint32_t keyCode, int isDown);
MenuKeyEventFnX64 g_menuKeyEventX64 = nullptr;

constexpr ptrdiff_t kMenuStackDepthOffsetX64 = 0x14C0;  // ctx+0x14C0 -- x86's kMenuStackDepthOffset (0xA7C)
constexpr ptrdiff_t kMenuItemCountOffsetX64 = 0xB8;      // menu+0xB8 (0x17*8) -- x86's menu+0xa8
constexpr ptrdiff_t kMenuItemArrayOffsetX64 = 0xC0;      // menu+0xC0 (0x18*8) -- x86's menu+0xac
constexpr ptrdiff_t kItemFocusFlagsOffsetX64 = 0x50;     // item+0x50+player*4 -- x86's item+0x48 (no player index there)
constexpr ptrdiff_t kItemNameOffsetX64 = 0x0;            // item+0x0, unchanged from x86
constexpr int kLocalClientIndexX64 = 0;                   // matches this file's existing single-player convention

// Real x64 sanity check on a 64-bit pointer read out of live process memory (x86's
// own LooksSane() only covered the 32-bit address space) -- rejects null/low
// sentinel values and anything above the real x64 user-mode VA ceiling
// (0x00007FFFFFFFFFFF) before ever dereferencing it.
inline bool LooksSaneX64(uintptr_t p)
{
    return p >= 0x10000 && p < 0x00007FFFFFFFFFFFull;
}

int GetMenuStackDepthX64()
{
    if (!g_uiMenuContextX64) return -1;
    __try {
        return *reinterpret_cast<volatile int*>(reinterpret_cast<uintptr_t>(g_uiMenuContextX64) + kMenuStackDepthOffsetX64);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return -1;
    }
}

void* GetTopmostActiveMenuX64()
{
    if (!g_getTopmostActiveMenuX64 || !g_uiMenuContextX64) return nullptr;
    __try {
        long long menu = g_getTopmostActiveMenuX64(g_uiMenuContextX64);
        return reinterpret_cast<void*>(static_cast<uintptr_t>(menu));
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

// Direct x64 port of x86's TryGetRealFocusedGroupAndIndex(4-arg overload) --
// structurally identical logic (walk the topmost menu's real itemDef array, find the
// one item whose focus-flag bits are both set, parse its name as "<group>_<index>",
// then count real siblings sharing that base name), only the offsets/stride differ.
// See this section's own header comment for the full per-offset confirmation trail.
bool TryGetRealFocusedGroupAndIndexX64(char* outGroupName, size_t outGroupNameSize, int& outIndex, int& outSiblingCount)
{
    void* menuVoid = GetTopmostActiveMenuX64();
    if (!menuVoid) return false;
    uintptr_t menu = reinterpret_cast<uintptr_t>(menuVoid);
    __try {
        int count = *reinterpret_cast<int*>(menu + kMenuItemCountOffsetX64);
        uintptr_t arr = *reinterpret_cast<uintptr_t*>(menu + kMenuItemArrayOffsetX64);
        if (count <= 0 || count > 500 || !LooksSaneX64(arr)) return false;
        for (int i = 0; i < count; ++i) {
            uintptr_t itemPtr = *reinterpret_cast<uintptr_t*>(arr + static_cast<uintptr_t>(i) * 8);
            if (!LooksSaneX64(itemPtr)) continue;
            uint32_t flags0 = *reinterpret_cast<uint32_t*>(
                itemPtr + kItemFocusFlagsOffsetX64 + static_cast<uintptr_t>(kLocalClientIndexX64) * 4);
            if ((flags0 & 0x4) == 0 || ((flags0 >> 1) & 1) == 0) continue;
            uintptr_t nameAddr = *reinterpret_cast<uintptr_t*>(itemPtr + kItemNameOffsetX64);
            if (!LooksSaneX64(nameAddr)) return false;
            const char* name = reinterpret_cast<const char*>(nameAddr);
            size_t len = strnlen(name, 128);
            size_t j = len;
            while (j > 0 && isdigit(static_cast<unsigned char>(name[j - 1]))) --j;
            if (j == len || j == 0 || name[j - 1] != '_') return false;
            size_t baseLen = j - 1;
            if (baseLen == 0 || baseLen >= outGroupNameSize) return false;
            memcpy(outGroupName, name, baseLen);
            outGroupName[baseLen] = '\0';
            outIndex = atoi(name + j);

            // Second pass: count real siblings sharing this exact base name, same
            // technique x86's own overload uses (bounded by the same validated
            // count/arr, so no extra validation needed).
            int siblingCount = 0;
            for (int k = 0; k < count; ++k) {
                uintptr_t siblingPtr = *reinterpret_cast<uintptr_t*>(arr + static_cast<uintptr_t>(k) * 8);
                if (!LooksSaneX64(siblingPtr)) continue;
                uintptr_t siblingNameAddr = *reinterpret_cast<uintptr_t*>(siblingPtr + kItemNameOffsetX64);
                if (!LooksSaneX64(siblingNameAddr)) continue;
                const char* siblingName = reinterpret_cast<const char*>(siblingNameAddr);
                if (_strnicmp(siblingName, outGroupName, baseLen) != 0) continue;
                if (siblingName[baseLen] != '_') continue;
                size_t sj = baseLen + 1;
                size_t slen = strnlen(siblingName, 128);
                if (sj >= slen) continue;
                bool allDigits = true;
                for (size_t d = sj; d < slen; ++d) {
                    if (!isdigit(static_cast<unsigned char>(siblingName[d]))) { allDigits = false; break; }
                }
                if (allDigits) ++siblingCount;
            }
            outSiblingCount = siblingCount;
            return true;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
    return false;
}

}  // namespace

// Real x64 "ForwardKeyToMenu" wrapper -- see kMenuKeyEventSignature's own big
// comment (above) for the full confirmation trail. Resolves the topmost active
// menu itself on every call (exactly matching the real call site's own shape,
// FUN_14029baa0: `plVar3 = FUN_1402aaa80(ctx); FUN_1402aac50(ctx, plVar3, key,
// isDown);`) rather than caching a menu pointer across ticks, since which menu
// is topmost can change between calls (a real menu opening/closing).
void ForwardKeyToMenuX64(int keyCode, int isDown)
{
    if (!g_menuKeyEventX64 || !g_uiMenuContextX64) return;
    void* menu = GetTopmostActiveMenuX64();
    if (!menu) return;
    g_menuKeyEventX64(g_uiMenuContextX64, menu, static_cast<uint32_t>(keyCode), isDown);
}

// ---- Custom Options screen, x64 (2026-09-05, release-parity pass) -----------------
//
// x86 precedent: `CustomOptionsMenu_TickInput` (overlay_hud.cpp) and the screen it
// drives (`DrawCustomOptionsMenuIfOpen`) are genuinely cross-platform already -- read
// in full before writing anything here, per this project's standing compare-to-x86-
// original rule. Neither function reads a single hardcoded address; the whole
// draw/navigate-once-open half of this feature already works correctly on x64 with
// zero porting needed, confirmed by inspection.
//
// The gap this section originally documented: x86's own real TRIGGER for opening the
// screen (`InjectControllerMenuNav`, analog_input_hooks.cpp) never ran on x64 at all
// -- x64's own per-frame input pipeline (this file) never called
// `CustomOptionsMenu_TickInput`, so `g_optMenuOpen` could never become true and the
// whole screen was unreachable in-game, even though drawing it worked fine once open.
//
// **REAL TRIGGER NOW WIRED (2026-09-12)**, alongside the temporary chord below, not
// replacing it outright until this specific path is live-tested -- see
// `openOptionsRequestedEdge` in `PollCustomOptionsMenuX64` further down, which uses
// the newly-ported `TryGetRealFocusedGroupAndIndexX64` the exact same way x86's own
// `InjectControllerMenuNav` does: detect real NATIVE menu-item focus landing on the
// pause/campaign/specops menu's own real "Options" button (`PAUSE_LIST`/1,
// `CAMPAIGN_BUTTON_LIST`/3, `SPECOPS_BUTTON_LIST`/5 -- the same group names/indices
// x86 uses, expected to carry over unchanged since the x86->x64 migration was a code
// recompile against the same game data, not a content update -- see this project's
// own "10-string persistence check" finding in the Version Timeline). Both paths
// funnel into the same single `openRequestedEdge` bool `CustomOptionsMenu_TickInput`
// already accepts, so there's no double-open risk from having both wired at once.
//
// TEMPORARY substitute, KEPT as a safety net until the real trigger above is
// confirmed live: opening is ALSO still reachable via a manual chord (LB+RB held
// together) while a real native menu is already active (`g_menuActiveGateFlag`,
// already-confirmed x64 IsMenuActive() equivalent, bit 0x10). Still gated on
// `[Options] UseCustomOptionsScreen` (default OFF), matching x86's own opt-in
// convention exactly. Remove this chord once the real trigger is live-confirmed to
// work reliably on its own -- it is no longer the ONLY way in, just a fallback.
bool g_optNavUpHeldX64 = false, g_optNavDownHeldX64 = false, g_optNavLeftHeldX64 = false,
     g_optNavRightHeldX64 = false, g_optNavSelectHeldX64 = false, g_optNavBackHeldX64 = false,
     g_optNavTabPrevHeldX64 = false, g_optNavTabNextHeldX64 = false;
bool g_optOpenComboHeldX64 = false;
bool g_optMenuWasActiveX64 = false;

extern "C" void PollCustomOptionsMenuX64()
{
    if (!g_modConfig.useCustomOptionsScreen) return;

    bool menuActiveNow = g_menuActiveGateFlag && ((*g_menuActiveGateFlag & 0x10u) != 0);
    if (!menuActiveNow) {
        // Mirrors x86's own InjectControllerMenuNav early-return-and-reset when the
        // real menu system isn't active -- next open should always see a fresh
        // rising edge, and the screen itself must forget any drilldown/open state.
        g_optNavUpHeldX64 = g_optNavDownHeldX64 = g_optNavLeftHeldX64 = g_optNavRightHeldX64 = false;
        g_optNavSelectHeldX64 = g_optNavBackHeldX64 = false;
        g_optNavTabPrevHeldX64 = g_optNavTabNextHeldX64 = false;
        g_optOpenComboHeldX64 = false;
        if (g_optMenuWasActiveX64) CustomOptionsMenu_ResetOnMenuClose();
        g_optMenuWasActiveX64 = false;
        return;
    }
    g_optMenuWasActiveX64 = true;

    unsigned short xiButtons = 0;
    unsigned char leftTrigger = 0, rightTrigger = 0;
    if (!Controller_GetRawButtonsAndTriggers(xiButtons, leftTrigger, rightTrigger)) return;

    bool upHeld = (xiButtons & kXI_DPAD_UP_X64) != 0;
    bool upEdge = upHeld && !g_optNavUpHeldX64;
    bool downHeld = (xiButtons & kXI_DPAD_DOWN_X64) != 0;
    bool downEdge = downHeld && !g_optNavDownHeldX64;
    bool leftHeld = (xiButtons & kXI_DPAD_LEFT_X64) != 0;
    bool leftEdge = leftHeld && !g_optNavLeftHeldX64;
    bool rightHeld = (xiButtons & kXI_DPAD_RIGHT_X64) != 0;
    bool rightEdge = rightHeld && !g_optNavRightHeldX64;
    bool selectHeld = IsPhysicalHeld_Exported(PhysicalInput::A, xiButtons, leftTrigger, rightTrigger);
    bool selectEdge = selectHeld && !g_optNavSelectHeldX64;
    bool backHeld = IsPhysicalHeld_Exported(PhysicalInput::B, xiButtons, leftTrigger, rightTrigger);
    bool backEdge = backHeld && !g_optNavBackHeldX64;
    bool tabPrevHeld = IsPhysicalHeld_Exported(PhysicalInput::LB, xiButtons, leftTrigger, rightTrigger);
    bool tabPrevEdge = tabPrevHeld && !g_optNavTabPrevHeldX64;
    bool tabNextHeld = IsPhysicalHeld_Exported(PhysicalInput::RB, xiButtons, leftTrigger, rightTrigger);
    bool tabNextEdge = tabNextHeld && !g_optNavTabNextHeldX64;

    // Real trigger (2026-09-12) -- see this function's own header comment for the
    // full confirmation trail. Mirrors x86's InjectControllerMenuNav exactly: real
    // native menu-item focus landing on the pause/campaign/specops menu's own real
    // "Options" button, via the newly-ported itemDef-array walk.
    char focusedGroupX64[128] = {};
    int focusedIndexX64 = -1, siblingCountX64 = -1;
    bool haveFocusX64 = TryGetRealFocusedGroupAndIndexX64(focusedGroupX64, sizeof(focusedGroupX64),
                                                              focusedIndexX64, siblingCountX64);
    // Deduped live-confirmation diagnostic for the newly-ported itemDef walk itself
    // (independent of whether it happens to land on an Options button this tick) --
    // this is the cheapest possible way to confirm TryGetRealFocusedGroupAndIndexX64
    // is resolving real, changing values live against actual gameplay, without
    // needing the separate (not-yet-ported) native text-draw hook a full on-screen
    // glyph overlay would require. Only runs while a menu is genuinely active (this
    // function's own early-return above already gates that).
    {
        static char s_lastFocusDiagKeyX64[200] = "";
        char focusDiagKeyX64[200];
        sprintf_s(focusDiagKeyX64, "%d|%s|%d|%d", haveFocusX64 ? 1 : 0, focusedGroupX64, focusedIndexX64, siblingCountX64);
        if (strcmp(focusDiagKeyX64, s_lastFocusDiagKeyX64) != 0) {
            strncpy_s(s_lastFocusDiagKeyX64, focusDiagKeyX64, _TRUNCATE);
            char fbuf[256];
            sprintf_s(fbuf, "[x64-menufocus-diag] haveFocus=%d group=\"%s\" index=%d siblingCount=%d",
                haveFocusX64 ? 1 : 0, focusedGroupX64, focusedIndexX64, siblingCountX64);
            LogFromController(fbuf);
        }
    }
    bool onPauseMenuOptionsButtonX64 = haveFocusX64 && _stricmp(focusedGroupX64, "PAUSE_LIST") == 0 && focusedIndexX64 == 1;
    bool onCampaignMenuOptionsButtonX64 = haveFocusX64 && _stricmp(focusedGroupX64, "CAMPAIGN_BUTTON_LIST") == 0 && focusedIndexX64 == 3;
    bool onSpecOpsMenuOptionsButtonX64 = haveFocusX64 && _stricmp(focusedGroupX64, "SPECOPS_BUTTON_LIST") == 0 && focusedIndexX64 == 5;
    bool onAnyRealOptionsButtonX64 = onPauseMenuOptionsButtonX64 || onCampaignMenuOptionsButtonX64 || onSpecOpsMenuOptionsButtonX64;
    bool openOptionsRequestedEdge = onAnyRealOptionsButtonX64 && selectEdge && g_modConfig.useCustomOptionsScreen;

    // Temporary open chord -- KEPT as a fallback, see this function's own header
    // comment. Only checked while our own menu isn't already open
    // (CustomOptionsMenu_TickInput's own openRequestedEdge parameter is ignored once
    // g_optMenuOpen is already true, but avoiding the edge computation entirely here
    // is clearer than relying on that).
    bool openCombo = tabPrevHeld && tabNextHeld;
    bool openComboEdge = openCombo && !g_optOpenComboHeldX64;
    g_optOpenComboHeldX64 = openCombo;
    // Suppress the tab-prev/tab-next edges on the SAME tick the open chord fires --
    // otherwise opening on LB+RB would also immediately register as a tab-switch
    // the instant the screen appears (both were pressed to open it).
    if (openComboEdge) { tabPrevEdge = false; tabNextEdge = false; }

    bool openRequestedEdge = openComboEdge || openOptionsRequestedEdge;
    if (openOptionsRequestedEdge) {
        LogFromController("[x64-optmenu-realtrigger] Real focus-based Options trigger fired "
            "(group=\"PAUSE_LIST/CAMPAIGN_BUTTON_LIST/SPECOPS_BUTTON_LIST\") -- opening Custom Options screen.");
    }

    CustomOptionsMenu_TickInput(openRequestedEdge, upEdge, downEdge, leftEdge, rightEdge,
                                  selectEdge, backEdge, tabPrevEdge, tabNextEdge);

    g_optNavUpHeldX64 = upHeld;
    g_optNavDownHeldX64 = downHeld;
    g_optNavLeftHeldX64 = leftHeld;
    g_optNavRightHeldX64 = rightHeld;
    g_optNavSelectHeldX64 = selectHeld;
    g_optNavBackHeldX64 = backHeld;
    g_optNavTabPrevHeldX64 = tabPrevHeld;
    g_optNavTabNextHeldX64 = tabNextHeld;
}

// ---- Native D-pad+A/B menu navigation, x64 (2026-09-12) ---------------------------
//
// Direct port of x86's InjectControllerMenuNav()/InjectControllerMenuBack()
// (analog_input_hooks.cpp ~2725-3047) -- read those in full before touching this
// section, per this project's standing compare-to-x86-original rule. Structurally
// unchanged (same keycodes, same edge-fires-both-down-and-up-transitions design, same
// dual-purpose-B handling), with two real differences from a literal line-for-line
// port, both intentional:
//
//   1. LB/RB tab-prev/tab-next are NOT reimplemented here -- x64 already has that
//      exact logic in PollCustomOptionsMenuX64 (feeds tabPrevEdge/tabNextEdge into
//      CustomOptionsMenu_TickInput). InjectControllerMenuNavX64 only owns
//      Up/Down/Left/Right/A(select)/Y/X/Back-button.
//
//   2. x86 has ONE function that calls CustomOptionsMenu_TickInput itself and
//      branches on its return value ("claimed this tick" vs. not) to decide whether
//      to forward D-pad/A to the real native menu. x64 already has that call living
//      in a SEPARATELY-scheduled function (PollCustomOptionsMenuX64) -- to preserve
//      the same "custom overlay owns input exclusively while open" guarantee without
//      duplicating that call, InjectControllerMenuNavX64 instead reads
//      CustomOptionsMenu_IsOpen() (a plain state read, no side effects) and skips
//      every ForwardKeyToMenuX64/synthetic-key call for the tick when it's true --
//      while still updating every held-state edge tracker unconditionally, mirroring
//      x86's own "claimed this tick" branch (which updates trackers before
//      returning, so edge detection never goes stale across an open/close).
//      REQUIRES PollCustomOptionsMenuX64() to run BEFORE this function in the same
//      tick so CustomOptionsMenu_IsOpen() reflects this tick's own open/close
//      decision -- see the wiring in InjectMenuInputTick (analog_input_hooks.cpp).
//
// Y/X/Back-button synthetic sends (Friends/Game Summary/Leaderboards) reimplemented
// locally here (SendSyntheticFX64/GX64/F1X64) rather than cross-file-exposing x86's
// versions -- x86's SendSyntheticF/G/F1 sit in an anonymous namespace in
// analog_input_hooks.cpp (internal/file linkage only, confirmed by reading that
// namespace block), so calling them from this TU would need an extern "C" export;
// simplest, lowest-risk option is the same local-reimplementation pattern this file
// already uses for SendSyntheticActionSlot4KeyX64 (GetGameWindow()/PostMessageA,
// already proven working on x64).
namespace {
constexpr int kKeyEscapeX64 = 0x1b;
constexpr int kKeyPrevItemX64 = 0x9a;  // real Up alt-keycode -- generic previous only
constexpr int kKeyNextItemX64 = 0x9b;  // real Down alt-keycode -- generic next only
constexpr int kKeyLeftNavX64 = 0x9c;   // real Left keycode -- drill-out on options screens, generic previous elsewhere
constexpr int kKeyRightNavX64 = 0x9d;  // real Right keycode -- drill-in on options screens, generic next elsewhere
constexpr int kKeyEnterX64 = 13;       // K_ENTER

bool g_menuNavUpHeldX64 = false;
bool g_menuNavDownHeldX64 = false;
bool g_menuNavLeftHeldX64 = false;
bool g_menuNavRightHeldX64 = false;
bool g_menuNavSelectHeldX64 = false;
bool g_menuNavYHeldX64 = false;
bool g_menuNavXHeldX64 = false;
bool g_menuNavBackButtonHeldX64 = false;
bool g_menuBackHeldX64 = false; // InjectControllerMenuBackX64's own physical-B edge tracker

void SendSyntheticFX64()
{
    HWND hwnd = GetGameWindow();
    if (!hwnd) return;
    PostMessageA(hwnd, WM_KEYDOWN, 'F', 0x00000001);
    PostMessageA(hwnd, WM_KEYUP, 'F', 0xC0000001);
}

void SendSyntheticGX64()
{
    HWND hwnd = GetGameWindow();
    if (!hwnd) return;
    PostMessageA(hwnd, WM_KEYDOWN, 'G', 0x00000001);
    PostMessageA(hwnd, WM_KEYUP, 'G', 0xC0000001);
}

void SendSyntheticF1X64()
{
    HWND hwnd = GetGameWindow();
    if (!hwnd) return;
    PostMessageA(hwnd, WM_KEYDOWN, VK_F1, 0x00000001);
    PostMessageA(hwnd, WM_KEYUP, VK_F1, 0xC0000001);
}
}  // namespace

extern "C" void InjectControllerMenuNavX64()
{
    bool menuActiveNow = g_menuActiveGateFlag && ((*g_menuActiveGateFlag & 0x10u) != 0);
    if (!menuActiveNow) {
        // Not stale-tracking across a menu close -- next press should always be seen
        // as a fresh rising edge once a menu is open again. Matches x86's own
        // InjectControllerMenuNav early-return-and-reset exactly.
        g_menuNavUpHeldX64 = false;
        g_menuNavDownHeldX64 = false;
        g_menuNavLeftHeldX64 = false;
        g_menuNavRightHeldX64 = false;
        g_menuNavSelectHeldX64 = false;
        g_menuNavYHeldX64 = false;
        g_menuNavXHeldX64 = false;
        g_menuNavBackButtonHeldX64 = false;
        return;
    }

    unsigned short buttons = 0;
    unsigned char leftTrigger = 0, rightTrigger = 0;
    if (!Controller_GetRawButtonsAndTriggers(buttons, leftTrigger, rightTrigger)) return;

    bool upHeld = (buttons & kXI_DPAD_UP_X64) != 0;
    bool downHeld = (buttons & kXI_DPAD_DOWN_X64) != 0;
    bool leftHeld = (buttons & kXI_DPAD_LEFT_X64) != 0;
    bool rightHeld = (buttons & kXI_DPAD_RIGHT_X64) != 0;
    bool selectHeld = IsPhysicalHeld_Exported(PhysicalInput::A, buttons, leftTrigger, rightTrigger);

    // Custom Options overlay owns D-pad/A exclusively while open -- see this
    // section's own header comment (point 2) for why this is a state READ here
    // rather than a claim-and-branch call, and why PollCustomOptionsMenuX64 must
    // run first in the same tick for this to be accurate.
    bool customMenuOpen = CustomOptionsMenu_IsOpen();

    if (!customMenuOpen) {
        if (upHeld != g_menuNavUpHeldX64) ForwardKeyToMenuX64(kKeyPrevItemX64, upHeld ? 1 : 0);
        if (downHeld != g_menuNavDownHeldX64) ForwardKeyToMenuX64(kKeyNextItemX64, downHeld ? 1 : 0);
        if (leftHeld != g_menuNavLeftHeldX64) ForwardKeyToMenuX64(kKeyLeftNavX64, leftHeld ? 1 : 0);
        if (rightHeld != g_menuNavRightHeldX64) ForwardKeyToMenuX64(kKeyRightNavX64, rightHeld ? 1 : 0);
        if (selectHeld != g_menuNavSelectHeldX64) ForwardKeyToMenuX64(kKeyEnterX64, selectHeld ? 1 : 0);
    }
    g_menuNavUpHeldX64 = upHeld;
    g_menuNavDownHeldX64 = downHeld;
    g_menuNavLeftHeldX64 = leftHeld;
    g_menuNavRightHeldX64 = rightHeld;
    g_menuNavSelectHeldX64 = selectHeld;

    if (!customMenuOpen) {
        // Friends (Y), Game Summary (X), Leaderboards (Back) -- rising edge only,
        // matching x86's own InjectControllerMenuNav exactly.
        bool yHeld = IsPhysicalHeld_Exported(PhysicalInput::Y, buttons, leftTrigger, rightTrigger);
        if (yHeld && !g_menuNavYHeldX64) SendSyntheticFX64();
        g_menuNavYHeldX64 = yHeld;

        bool xHeld = IsPhysicalHeld_Exported(PhysicalInput::X, buttons, leftTrigger, rightTrigger);
        if (xHeld && !g_menuNavXHeldX64) SendSyntheticGX64();
        g_menuNavXHeldX64 = xHeld;

        bool backButtonHeld = IsPhysicalHeld_Exported(PhysicalInput::Back, buttons, leftTrigger, rightTrigger);
        if (backButtonHeld && !g_menuNavBackButtonHeldX64) SendSyntheticF1X64();
        g_menuNavBackButtonHeldX64 = backButtonHeld;
    }
}

// Direct port of x86's InjectControllerMenuBack() -- B forwards real ESC to
// whatever native menu is active (main menu, pause menu, buy station, options,
// etc.), the exact same mechanism Start's own pause-menu-open work already
// established. Deliberately hardcoded to physical B (not routed through
// g_buttonMap/layout remapping), matching x86 exactly -- this is a system-level
// menu action, not a gameplay bind. Maintains g_currentBPressTouchedMenuX64 (see
// its own declaration comment above, near g_crouchProneHeldX64) so CrouchProne's
// own dispatch in Hook_MovementTick never fires for a B press that overlapped an
// open menu.
extern "C" void InjectControllerMenuBackX64()
{
    unsigned short buttons = 0;
    unsigned char leftTrigger = 0, rightTrigger = 0;
    if (!Controller_GetRawButtonsAndTriggers(buttons, leftTrigger, rightTrigger)) return;

    bool held = IsPhysicalHeld_Exported(PhysicalInput::B, buttons, leftTrigger, rightTrigger);
    bool menuActiveNow = g_menuActiveGateFlag && ((*g_menuActiveGateFlag & 0x10u) != 0);

    if (held && !g_menuBackHeldX64) {
        // Rising edge of B itself: a fresh physical press is starting.
        g_currentBPressTouchedMenuX64 = false;
    }
    if (held && menuActiveNow) {
        g_currentBPressTouchedMenuX64 = true;
    }
    // Custom Options overlay (mirrors x86's own guard): while it's open, B closes
    // IT, not the real native menu underneath -- PollCustomOptionsMenuX64's own
    // backEdge handles that close. Without this guard the same B press would also
    // forward a real ESC here, backing out of both the overlay AND the real menu
    // in one press.
    if (menuActiveNow && held != g_menuBackHeldX64 && !CustomOptionsMenu_IsOpen()) {
        ForwardKeyToMenuX64(kKeyEscapeX64, held ? 1 : 0);
    }
    g_menuBackHeldX64 = held;
}

// ---- Auto pause/unpause "unstick" cycle, x64 (2026-09-04) -------------------------
//
// Real, DIRECT user confirmation of the actual fix, not a theory: "still no
// difference it still requires the classic pause unpause workaround." This
// project's two prior fix attempts this session (SendSyntheticActivationClick,
// SendRealFocusNudgeX64/SendPeriodicActivationNudgeX64 -- all in d3d9_hook.cpp)
// were both built around the theory that some WndProc-level activation/focus
// EVENT is what's needed. That theory is now directly disproven by the user's
// own report -- what ACTUALLY fixes it, every time, is manually opening the
// pause menu and closing it again. Genuinely pausing and unpausing evidently
// triggers some real internal re-sync this project's own earlier x86 work
// already suspected but never pinned down for ITS OWN version of this exact
// bug class (known_issues.md issue #1's own theory: "some engine-side state
// expects a genuine transition/event... to occur" -- the "genuine transition"
// turns out to be a real menu open+close, not a WndProc message).
//
// Rather than keep guessing at WHICH internal flag needs forcing (three
// attempts already, all wrong), this automates the user's OWN confirmed manual
// fix directly, using this project's ALREADY-confirmed-working Pause toggle
// (g_pauseToggle/FUN_1400823b0) -- open pause, wait one real beat, close it
// again. Triggered once per level: `g_lastPmoveTickMs` (updated by
// Hook_PmoveTick on every real tick) tells us whether the Pmove/gameplay-
// simulation pipeline is CURRENTLY live -- when it transitions from "not
// ticking recently" (a menu/loading screen) to "ticking steadily for the last
// half-second" (a level is genuinely active), that's this project's own
// reusable proxy for "a level just (re)loaded," without needing dedicated
// level-load-flag RE work. Runs from the SAME always-on menu tick
// PollPauseToggleX64 above already uses (must NOT run from Hook_MovementTick --
// this function's own OPEN step pauses the game, which stops Hook_MovementTick
// from running at all until the CLOSE step happens, so this has to live on the
// tick that keeps running regardless of pause state).
namespace {
enum class AutoUnstickState { Idle, WaitingToSettle, JustOpenedPause };
AutoUnstickState g_autoUnstickState = AutoUnstickState::Idle;
DWORD g_autoUnstickStateChangedMs = 0;
DWORD g_levelActiveSinceMs = 0; // when Pmove was FIRST confirmed ticking this streak, not "last tick"
bool g_autoUnstickDoneForThisLevel = true; // starts true -- nothing to unstick before a level exists

// CORRECTED 2026-09-04, real live-test feedback ("weird, sprint fires when
// gated and when standing still so its reading it but not allowing the other
// input, also the workaround fires too early to work"): the original 500ms/
// 250ms delays were both far too short. Two real, separate timing bugs:
// (1) firing the auto-cycle the instant Pmove starts ticking steadily is too
//     early -- a real manual "pause then unpause" only ever happens well
//     after the player is actually settled into a level, not the literal
//     instant it starts simulating (loading-screen-to-gameplay transitions,
//     spawn cinematics, etc. may still be resolving); (2) a 250ms open-close
//     gap may be too brief for the pause state to genuinely "stick" long
//     enough for whatever real internal re-sync this depends on to run --
//     nothing about a real player's own pace suggests they re-press that
//     fast either. Both widened substantially, matching x86's own original
//     "3-second window" scale for this exact bug class (known_issues.md
//     issue #1) rather than this session's own first-guess short values.
constexpr DWORD kLevelSettleDelayMs = 1250;     // wait this long after Pmove first goes live before opening pause
                                                 // (2026-09-04 tuning: 4000 -> 2000 -> 1750 -> 1250ms, all direct
                                                 // live-test feedback -- "wait needs to be halved", "still a touch
                                                 // slow maybe 1.75s", "could still be earlier try 1.25s")
constexpr DWORD kLevelIdleResetMs = 2000;        // Pmove silent this long -- treat as "back at a menu"
// Close as fast as possible -- direct user request: "make it close basically
// instantly, it should basically look flawless user end". NOT reduced to 0
// (same tick as the open call) -- keeps the open and close as two genuinely
// separate ticks with real, if minimal, elapsed engine time between them,
// rather than risking the native side treating them as one indistinguishable
// event. 50ms is roughly 3 WM_TIMER ticks at this project's own ~16ms/60Hz
// cadence -- well under normal human flash-perception threshold, about as
// close to "instant" as this project's own tick-based (non-blocking, see
// CLAUDE.md SS5's hook-safety rule) design can get.
constexpr DWORD kAutoUnstickCloseDelayMs = 50;
}  // namespace

extern "C" void AutoUnstickPauseCycleX64()
{
    if (!g_pauseToggle) return;
    DWORD nowMs = GetTickCount();
    DWORD sinceLastPmoveTick = nowMs - g_lastPmoveTickMs;
    bool pmoveLiveNow = sinceLastPmoveTick <= 500;

    if (sinceLastPmoveTick > kLevelIdleResetMs) {
        // Back at a menu/loading screen (or not yet in a level at all) -- arm
        // for the NEXT level's own first activation.
        g_autoUnstickDoneForThisLevel = false;
        g_autoUnstickState = AutoUnstickState::Idle;
        return;
    }

    switch (g_autoUnstickState) {
        case AutoUnstickState::Idle:
            if (!g_autoUnstickDoneForThisLevel && pmoveLiveNow) {
                // Level just became active -- start the settle timer, don't
                // open pause yet.
                g_levelActiveSinceMs = nowMs;
                g_autoUnstickState = AutoUnstickState::WaitingToSettle;
            }
            break;

        case AutoUnstickState::WaitingToSettle:
            if (nowMs - g_levelActiveSinceMs >= kLevelSettleDelayMs) {
                g_pauseToggle(0); // not currently paused -- this call opens it
                g_autoUnstickState = AutoUnstickState::JustOpenedPause;
                g_autoUnstickStateChangedMs = nowMs;
                LogFromController("[x64-auto-unstick] opened pause -- starting automated open/close "
                    "cycle for this level (after a real settle delay), closing again shortly.");
            }
            break;

        case AutoUnstickState::JustOpenedPause:
            if (nowMs - g_autoUnstickStateChangedMs >= kAutoUnstickCloseDelayMs) {
                g_pauseToggle(0); // currently paused (we just opened it) -- this call closes it
                g_autoUnstickState = AutoUnstickState::Idle;
                g_autoUnstickDoneForThisLevel = true;
                LogFromController("[x64-auto-unstick] closed pause -- automated open/close cycle "
                    "complete for this level (real fix confirmed by direct user report: \"still "
                    "requires the classic pause unpause workaround\").");
            }
            break;
    }
}

void __fastcall Hook_MovementTick(void* param1, unsigned int param2)
{
    // Rate-limited (~1s) diagnostic heartbeat -- real data for the "needs a
    // click for input" investigation (see kMenuActiveGateInsnOffset's own
    // comment), so the NEXT test run shows what these candidate gate values
    // actually read during the stuck window vs. after a real click resolves
    // it, instead of guessing a fourth blind fix. Purely informational --
    // does not affect behavior.
    {
        static DWORD s_lastGateDiagMs = 0;
        DWORD nowMs = GetTickCount();
        if (nowMs - s_lastGateDiagMs >= 1000) {
            s_lastGateDiagMs = nowMs;
            char buf[192];
            sprintf_s(buf, "[x64-diag-gate] heartbeat: inputGateFlag(DAT_1406e4774)=0x%08X "
                "menuActiveGateFlag(DAT_1406e2550)=0x%08X",
                g_inputGateFlag ? *g_inputGateFlag : 0xFFFFFFFFu,
                g_menuActiveGateFlag ? *g_menuActiveGateFlag : 0xFFFFFFFFu);
            LogFromController(buf);
        }
    }

    // LOOK first, PRE-hook (before call-through) -- see the design comment above
    // g_pitchAccum/g_yawAccum: the native call itself both consumes and packs
    // these accumulators in one pass, so our contribution has to already be
    // sitting in them before g_realMovementTick runs.
    if (param1 && g_pitchAccum && g_yawAccum) {
        float leftX, leftY, rightX, rightY;
        if (Controller_GetLeftStick(leftX, leftY) && Controller_GetRightStick(rightX, rightY)) {
            float moveX, moveY, lookX, lookY;
            RouteStickAxes_Exported(leftX, leftY, rightX, rightY, g_modConfig.stickLayout, moveX, moveY, lookX, lookY);
            float dt = Controller_DeltaTimeSeconds();
            if (dt > 0.0f && (lookX != 0.0f || lookY != 0.0f)) {
                float scale = GetLookAccelerationScaleX64();
                float yawRate = g_modConfig.lookDegreesPerSecondHorizontal * scale;
                float pitchRate = g_modConfig.lookDegreesPerSecondVertical * scale;
                float pitchInput = g_modConfig.invertLook ? -lookY : lookY; // OG console "Invert Look"
                float yawDelta = lookX * yawRate * dt;
                float pitchDelta = pitchInput * pitchRate * dt;
                // Sign convention mirrored directly from x86's own confirmed-correct
                // InjectControllerLookAngles (analog_input_hooks.cpp) -- both accumulators
                // are SUBTRACTED from, not added.
                *g_yawAccum -= yawDelta;
                *g_pitchAccum -= pitchDelta;
                // Motion blur's own real per-frame delta feed -- same values, same
                // sign convention x86's InjectControllerLookAngles uses for its own
                // g_motionBlurYawDeltaDeg/g_motionBlurPitchDeltaDeg (analog_input_hooks.cpp).
                g_motionBlurYawDeltaDegX64 = yawDelta;
                g_motionBlurPitchDeltaDegX64 = pitchDelta;
            } else {
                g_lookAccelStartMsX64 = 0; // stick back at neutral -- next push starts the ramp fresh
                g_motionBlurYawDeltaDegX64 = 0.0f;
                g_motionBlurPitchDeltaDegX64 = 0.0f;
            }

            // Gyro-aim, ported 2026-09-12 -- mirrors x86's own InjectControllerLookAngles
            // (analog_input_hooks.cpp) exactly, same PREVIEW/WIP status carried over, not
            // promoted. dualsense_input.cpp/controller_input.cpp's gyro-read path (HID
            // report parsing, Controller_GetGyroRate) has no _M_IX86/_M_X64 guard anywhere
            // in either file -- confirmed by direct grep before writing this, not assumed
            // -- so this is the same "real mechanism exists, was just never called from the
            // x64 tick" shape as the vibration gap this session already closed (rumble.h),
            // not a new RE target. Applied regardless of the stick-look block above being
            // active (a gyro nudge should register even at right-stick neutral -- that's
            // the entire point of gyro-assisted aim), gated by g_modConfig.gyroOnlyWhileAds
            // against g_adsHeldX64 (Hook_MovementTick's own ADS-held tracking var, already
            // ported for Hold Breath -- see its own comment above), same as x86 uses
            // g_adsHeld. Axis-to-yaw/pitch mapping (Z=yaw, X=pitch) and the Invert Yaw/
            // Pitch/Look sign handling are copied verbatim from x86's still-unverified-on-
            // real-hardware mapping -- if it's ever corrected there, port the correction
            // here too, don't re-derive independently. Bluetooth-vs-USB: no extra handling
            // needed here -- dualsense_input.cpp's own DualSense_Poll already produces
            // gyroX/Y/Z uniformly for both transports (BT's CRC32 validation is internal to
            // that file); issue #76 (USB never independently confirmed by a second tester)
            // and issue #77 (BT stick input, not gyro, was the int16-overflow bug) both
            // stay exactly as true/untrue on x64 as they already are on x86 -- this port
            // doesn't change either finding's status.
            if (g_modConfig.gyroEnabled && (!g_modConfig.gyroOnlyWhileAds || g_adsHeldX64)) {
                float gyroX = 0.0f, gyroY = 0.0f, gyroZ = 0.0f;
                if (Controller_GetGyroRate(gyroX, gyroY, gyroZ)) {
                    float gyroYawDelta = gyroZ * g_modConfig.gyroSensitivity * dt;
                    float gyroPitchDelta = gyroX * g_modConfig.gyroSensitivity * dt;
                    if (g_modConfig.gyroInvertYaw) gyroYawDelta = -gyroYawDelta;
                    if (g_modConfig.gyroInvertPitch) gyroPitchDelta = -gyroPitchDelta;
                    if (g_modConfig.invertLook) gyroPitchDelta = -gyroPitchDelta; // OG console "Invert Look" applies uniformly
                    *g_yawAccum -= gyroYawDelta;
                    *g_pitchAccum -= gyroPitchDelta;
                    g_motionBlurYawDeltaDegX64 += gyroYawDelta;
                    g_motionBlurPitchDeltaDegX64 += gyroPitchDelta;
                }
            }
        }
    }

    // Force-clear the "needs a click" input gate BEFORE calling through -- see
    // kInputGateFlagTestInsnOffset's own comment above for the full trace. Must
    // happen before the call, since FUN_14007d9f0 itself checks this bit as its
    // very first real branch and returns immediately (skipping everything,
    // including our own already-written look accumulators' native pack step)
    // if it's set.
    if (g_inputGateFlag) *g_inputGateFlag &= ~kInputGateBit;

    // Call through -- native logic runs to completion (picks up our look write
    // above as part of its own unconditional accumulator-pack step; for
    // movement, this is the same "native logic runs to completion, then this
    // hook adds its own contribution on top" design as Sprint above, matching
    // x86's own InjectControllerMovement, a POST-hook additive layer on top of
    // the keyboard writer, not a replacement of it).
    g_realMovementTick(param1, param2);

    if (!param1) return;

    float leftX, leftY, rightX, rightY;
    if (!Controller_GetLeftStick(leftX, leftY)) return;
    if (!Controller_GetRightStick(rightX, rightY)) return;

    float moveX, moveY, lookX, lookY;
    RouteStickAxes_Exported(leftX, leftY, rightX, rightY, g_modConfig.stickLayout, moveX, moveY, lookX, lookY);
    if (moveX == 0.0f && moveY == 0.0f) return;

    auto* cmd = reinterpret_cast<unsigned char*>(param1);
    int8_t curForward = static_cast<int8_t>(cmd[0x1c]);
    int8_t curRight   = static_cast<int8_t>(cmd[0x1d]);

    // Confirmed correct as-is (no inversion) via x86's own real-hardware playtest
    // (analog_input_hooks.cpp's InjectControllerMovement, 2026-07-14) -- only
    // look (right stick) was ever reported inverted, not movement. Mirrored here
    // unchanged since the underlying usercmd_t layout is confirmed identical.
    int addForward = static_cast<int>(moveY * 127.0f);
    int addRight   = static_cast<int>(moveX * 127.0f);

    cmd[0x1c] = static_cast<unsigned char>(ClampToSByteX64(curForward + addForward));
    cmd[0x1d] = static_cast<unsigned char>(ClampToSByteX64(curRight + addRight));

    // Buttons/ADS/Reload/Weapnext -- polled from here for the same reason x86
    // calls InjectControllerButtons/Ads/Reload/Fire/WeaponNext all from ONE
    // per-frame orchestration point (analog_input_hooks.cpp's own
    // Hook_0057de60): this function (FUN_14007d9f0) IS that x64 orchestration
    // point, called once per real usercmd-build tick, same as x86's. Held-style
    // actions (Fire/ADS/Reload) fire on the edge only, calling the real
    // kbutton activate/deactivate handlers directly with a consistent
    // synthetic source-id (see kSyntheticSourceId's own comment for why --
    // real dual-source kbutton tracking, not a simple isDown boolean) --
    // mirroring x86's own InjectControllerAds/Reload/Fire down/up edge
    // design. Weapnext (one-shot) fires on the PRESS edge only. Pause is
    // NOT polled here (see PollPauseToggleX64 below) -- the Pmove tick this
    // hook rides on halts entirely while the game is paused (by design, same
    // architecture x86 already documented), so a pause-only-here poll could
    // toggle pause ON but could never observe the press that would toggle it
    // back OFF. Real bug, live-confirmed ("obvs cant unpause when paused").
    unsigned short xiButtons = 0;
    unsigned char leftTrigger = 0, rightTrigger = 0;
    if (Controller_GetRawButtonsAndTriggers(xiButtons, leftTrigger, rightTrigger)) {
        if (g_kbuttonActivate && g_kbuttonDeactivate) {
            int timestamp = g_timestampPtr ? static_cast<int>(*g_timestampPtr) : 0;

            if (g_fireStruct) {
                bool fireHeld = IsPhysicalHeld_Exported(g_buttonMap.fire, xiButtons, leftTrigger, rightTrigger);
                if (fireHeld != g_fireHeldX64) {
                    g_fireHeldX64 = fireHeld;
                    // Sniper Fire/ADS fix attempt -- see kNotifyBindFuncOffset's own
                    // comment. Purely additive alongside the existing kbutton call.
                    if (g_notifyBindDispatch) g_notifyBindDispatch(0, fireHeld ? kFireBindCaseDown : kFireBindCaseUp);
                    if (fireHeld) g_kbuttonActivate(g_fireStruct, kSyntheticSourceId, timestamp);
                    else g_kbuttonDeactivate(g_fireStruct, kSyntheticSourceId, timestamp);
                }
            }

            if (g_adsStruct) {
                bool adsHeld = IsPhysicalHeld_Exported(g_buttonMap.ads, xiButtons, leftTrigger, rightTrigger);
                if (adsHeld != g_adsHeldX64) {
                    g_adsHeldX64 = adsHeld;
                    // Sniper Fire/ADS fix attempt -- see kNotifyBindFuncOffset's own
                    // comment. Purely additive alongside the existing kbutton call.
                    if (g_notifyBindDispatch) g_notifyBindDispatch(0, adsHeld ? kAdsBindCaseDown : kAdsBindCaseUp);
                    if (adsHeld) g_kbuttonActivate(g_adsStruct, kSyntheticSourceId, timestamp);
                    else g_kbuttonDeactivate(g_adsStruct, kSyntheticSourceId, timestamp);
                    // Force the real "is aiming down sights" flag directly to our
                    // own desired absolute state -- see kAdsToggleFlagInsnOffset's
                    // own comment: live-tested, the kbutton call above alone does
                    // NOT drive actual ADS engagement, this flag does. Explicit
                    // set/clear, never a toggle -- correct regardless of whatever
                    // value native logic left it at.
                    if (g_adsToggleFlag) *g_adsToggleFlag = adsHeld ? 1 : 0;
                }
            }

            if (g_reloadStruct) {
                bool reloadHeld = IsPhysicalHeld_Exported(g_buttonMap.reloadUse, xiButtons, leftTrigger, rightTrigger);
                if (reloadHeld != g_reloadHeldX64) {
                    g_reloadHeldX64 = reloadHeld;
                    if (reloadHeld) g_kbuttonActivate(g_reloadStruct, kSyntheticSourceId, timestamp);
                    else g_kbuttonDeactivate(g_reloadStruct, kSyntheticSourceId, timestamp);
                }
            }
        }

        // Weapnext / Survival ready-up -- same physical button (Y), same
        // hold-vs-tap split as x86's own InjectControllerWeaponNext: a quick
        // tap (or a hold that never reaches the threshold) fires the normal
        // weapon-switch on release; a hold past
        // g_modConfig.readyUpHoldThresholdMs fires the synthetic F5 exactly
        // once per press (debounced by g_yReadyUpFiredX64), and SUPPRESSES
        // the release-edge weapon-switch for that same press (matching x86's
        // own "!g_yReadyUpFired" release-edge guard) -- Survival's between-
        // wave break is live gameplay with usable weapons, so firing
        // weapnext unconditionally on Y's press edge would also switch
        // weapons on every ready-up hold, an unwanted side effect x86 already
        // solved by deferring weapnext to the release edge.
        if (g_weaponNext) {
            bool weaponSwitchHeld = IsPhysicalHeld_Exported(g_buttonMap.weaponSwitch, xiButtons, leftTrigger, rightTrigger);
            if (weaponSwitchHeld && !g_weaponSwitchHeldX64) {
                g_yPressStartMsX64 = GetTickCount();
                g_yReadyUpFiredX64 = false;
            }
            if (weaponSwitchHeld && !g_yReadyUpFiredX64 &&
                (GetTickCount() - g_yPressStartMsX64) >= g_modConfig.readyUpHoldThresholdMs) {
                g_yReadyUpFiredX64 = true;
                SendSyntheticF5X64();
            }
            if (!weaponSwitchHeld && g_weaponSwitchHeldX64 && !g_yReadyUpFiredX64) {
                // Falling edge, ready-up threshold never reached this press --
                // switch weapons, same as x86's own release-edge fallback.
                g_weaponNext(0, 1);
            }
            g_weaponSwitchHeldX64 = weaponSwitchHeld;
        }

        // Melee/Lethal/Tactical/Jump/Interact -- raw usercmd_t.buttons bits,
        // additively OR'd every tick while held, exactly mirroring x86's own
        // InjectControllerButtons (see the big comment above kMeleeUsercmdBit
        // for the full trace on why this mirrors x86's raw-bit design rather
        // than the kbutton-dispatch mechanism used for Fire/ADS/Reload above).
        {
            uint32_t out = 0;
            if (IsPhysicalHeld_Exported(g_buttonMap.melee, xiButtons, leftTrigger, rightTrigger)) out |= kMeleeUsercmdBit;
            if (IsPhysicalHeld_Exported(g_buttonMap.lethal, xiButtons, leftTrigger, rightTrigger)) out |= kLethalUsercmdBit;
            if (IsPhysicalHeld_Exported(g_buttonMap.tactical, xiButtons, leftTrigger, rightTrigger)) out |= kTacticalUsercmdBit;

            // Jump -- suppressed while a menu is active, matching x86's own
            // InjectControllerButtons exactly (A doubles as menu-select there).
            // g_menuActiveGateFlag was resolved earlier this session purely for
            // diagnostic logging (the "needs a click" investigation) -- reused
            // here for its own real, originally-intended purpose (this project's
            // own confirmed x64 IsMenuActive() equivalent, bit 0x10).
            bool menuActiveNow = g_menuActiveGateFlag && ((*g_menuActiveGateFlag & 0x10u) != 0);
            bool jumpHeld = IsPhysicalHeld_Exported(g_buttonMap.jump, xiButtons, leftTrigger, rightTrigger) && !menuActiveNow;
            if (jumpHeld) out |= kJumpUsercmdBit;
            // Auto-stand from crouch/prone on Jump's rising edge -- ports x86's own
            // InjectControllerButtons precedent (ForceStandingViaRealToggle) now that
            // CrouchProne's own stance-dispatch mechanism (g_stanceDispatch) exists to
            // build it on top of -- see ForceStandingViaRealToggleX64's own comment.
            if (jumpHeld && !g_jumpHeldX64) {
                ForceStandingViaRealToggleX64();
            }
            g_jumpHeldX64 = jumpHeld;

            // Interact (X) -- hold-to-interact, dual-purpose with Reload on the
            // SAME physical button, matching x86's own design exactly (both the
            // kbutton-based Reload call above AND this raw bit fire off the same
            // physical press).
            bool interactHeld = IsPhysicalHeld_Exported(g_buttonMap.reloadUse, xiButtons, leftTrigger, rightTrigger);
            if (interactHeld && !g_interactButtonWasHeldX64) {
                g_interactPressStartMsX64 = GetTickCount();
            }
            if (interactHeld && (GetTickCount() - g_interactPressStartMsX64) >= g_modConfig.interactHoldThresholdMs) {
                out |= kInteractUsercmdBit;
            }
            g_interactButtonWasHeldX64 = interactHeld;

            if (out != 0) {
                // Real usercmd_t.buttons field, confirmed at +0x04 on x86
                // (re_notes/iw5sp.md) -- see the big comment above for why this
                // offset is trusted to carry over unverified-by-a-fresh-scan
                // (the SAME struct's +0x1c/+0x1d fields are already independently
                // confirmed identical this session).
                auto* buttonsField = reinterpret_cast<uint32_t*>(cmd + 4);
                *buttonsField |= out;
            }
        }

        // D-pad actionslot -- one-shot action call on the press edge only, same
        // pattern as Weapnext above (see kActionSlotSignature's own comment for
        // why this doesn't need an "up" call the way Fire/ADS/Reload do).
        // EXCEPTION: slot 3 (D-pad Left) goes through SendSyntheticActionSlot4KeyX64
        // instead of g_actionSlot -- see that function's own comment. Held on both
        // edges (down on press, up on release) to mirror a real keypress exactly,
        // same as x86's own paired call; deliberately does NOT also call g_actionSlot
        // for this slot (would double-dispatch -- the synthesized key's own real
        // dispatch already reaches FUN_14006dee0 itself). The other three directions
        // are unchanged, still driven by the direct native call.
        //
        // GATING FIX (2026-09-12, menu-nav port): suppressed while a menu is active,
        // matching x86's own InjectControllerDpad exactly ("While a menu is open,
        // D-pad drives item navigation instead") -- both the press AND release
        // branches, same symmetric gate x86 uses. Without this, native D-pad menu
        // navigation (InjectControllerMenuNavX64, always-on tick) and this raw
        // actionslot dispatch (gameplay tick, which keeps running while a non-pause
        // menu -- e.g. a Survival buy station -- is open) would double-fire on the
        // same physical D-pad press.
        {
            struct { unsigned short bit; int slot; } kDpad[4] = {
                { kXI_DPAD_UP_X64, 0 }, { kXI_DPAD_RIGHT_X64, 1 }, { kXI_DPAD_DOWN_X64, 2 }, { kXI_DPAD_LEFT_X64, 3 }
            };
            bool menuActiveNowForDpad = g_menuActiveGateFlag && ((*g_menuActiveGateFlag & 0x10u) != 0);
            for (int i = 0; i < 4; ++i) {
                bool held = (xiButtons & kDpad[i].bit) != 0;
                bool isSlot4 = (kDpad[i].slot == 3);
                if (held != g_dpadHeldX64[i]) {
                    if (!menuActiveNowForDpad) {
                        if (isSlot4) {
                            SendSyntheticActionSlot4KeyX64(held);
                        } else if (held && g_actionSlot) {
                            g_actionSlot(0, kDpad[i].slot);
                        }
                    }
                    g_dpadHeldX64[i] = held;
                }
            }
        }

        // CrouchProne (B) -- forwards real press/release edges directly to the
        // native "+stance" case dispatch (see kCrouchProneCaseDown's own comment
        // for why this is the safe design, not a state-machine replication).
        //
        // GATING FIX (2026-09-12, menu-nav port): B is dual-purpose (crouch/prone vs.
        // menu-back/ESC-forward, InjectControllerMenuBackX64) -- suppressed for any
        // press that has touched an open menu at any point during its hold
        // (g_currentBPressTouchedMenuX64, maintained by InjectControllerMenuBackX64),
        // mirroring x86's own InjectControllerButtons guard exactly. Without this, B
        // backing out of a menu would ALSO toggle real native stance underneath it --
        // a real stuck-crouch/prone regression risk (see g_currentBPressTouchedMenuX64's
        // own declaration comment for the full rationale).
        if (g_stanceDispatch) {
            bool crouchProneHeld = IsPhysicalHeld_Exported(g_buttonMap.crouchProne, xiButtons, leftTrigger, rightTrigger);
            if (crouchProneHeld != g_crouchProneHeldX64) {
                g_crouchProneHeldX64 = crouchProneHeld;
                if (!g_currentBPressTouchedMenuX64) {
                    if (crouchProneHeld) g_stanceDispatch(0, kCrouchProneCaseDown, 1);
                    else g_stanceDispatch(0, kCrouchProneCaseUp, 0);
                }
            }
        }

        // Scoreboard (Back) -- hold-through-passthrough key synthesis, x64 port
        // 2026-09-13 (see SendSyntheticScoreboardKeyX64's own comment for the full
        // rationale and the "confirmed genuine no-op in SP" framing). No menu-active
        // gate needed -- x86's own InjectControllerScoreboard has none either, and
        // Back has no other current meaning on x64 to conflict with.
        {
            bool scoreboardHeld = IsPhysicalHeld_Exported(g_buttonMap.scoreboard, xiButtons, leftTrigger, rightTrigger);
            if (scoreboardHeld != g_scoreboardHeldX64) {
                g_scoreboardHeldX64 = scoreboardHeld;
                SendSyntheticScoreboardKeyX64(scoreboardHeld);
            }
        }
    }

    // Pause also polled from here, redundantly -- matches x86's own established
    // "call the same edge-debounced poll from both the gameplay tick AND the
    // always-on menu tick, it's safe/idempotent either way" pattern
    // (InjectControllerPauseMenu's own comment, analog_input_hooks.cpp). This
    // call is what handles OPENING pause during live gameplay; PollPauseToggleX64
    // called from InjectMenuInputTick below is what handles CLOSING it, since
    // this whole function stops being called at all once actually paused.
    PollPauseToggleX64();

    // Vibration/rumble (2026-09-12 x64 port, known_issues_x64.md's "Corrected gap
    // list, 2026-09-12" entry) -- gameplay-tick only, matching x86's own placement
    // (InjectAllControllerInput's own call to Rumble_Tick(), analog_input_hooks.cpp):
    // rumble is a gameplay-feedback feature, not a UI one, and this is the same
    // per-frame usercmd-build tick x86's Fire/damage-poll logic already rides on.
    // Rumble_Tick() itself is architecture-neutral (rumble.cpp) and internally
    // dispatches to the x64-specific fire-hook/damage-poll implementations resolved
    // by Rumble_Install() below.
    Rumble_Tick();
}

// ---- Visual-enhancement suite x64 gating (2026-09-12) -- InternalRenderScalePercent
// (x86 issue #88) and the clcState/in-level-flag safety gates FSR RCAS (x86 issue
// #94/#103/#104) and motion blur (x86 issue #96/#97) both need before either can be
// wired into the x64 per-frame call at all. x86 originals live in overlay_hud.cpp
// (RunFullScreenPostProcessIfEnabled/RunPreOverlayMotionBlurPassIfEnabled) and
// analog_input_hooks.cpp (Hook_FUN_00679010) -- re-read in full before writing this,
// per this project's own standing compare-to-x86-original rule. Closes the two
// blockers known_issues_x64.md's "Visual-enhancement suite x64 port" entry flagged
// as needing live tracing -- both resolved statically instead, via decompile-based
// structural pattern matching (not the string/reference scan that had already come
// up empty for clcState, three separate techniques, per that entry's own record).

// FUN_1401bd1d0 -- the confirmed x64 equivalent of x86's FUN_00679010 (the
// this-in-RCX quality-tier-clamp function InternalRenderScalePercent hooks). Found
// via a different route than x86's own investigation: traced the real "r_mode"/
// "Direct X resolution mode" dvar-registration string (FUN_1401bc8a0, confirmed via
// FindStringRefs.java) to its one real caller (FUN_1401bcf30, the x64 equivalent of
// x86's FUN_0067a320 -- same Direct3DCreate9(0x20) call, same early-return
// "already initialized" guard, same r_mode-registration-then-retry-loop shape), then
// down through that retry loop's own window-creation pair (FUN_1401bda40 computes
// requested/native W/H into a shared struct; FUN_1401bc780 creates the real OS
// window and -- on success -- calls FUN_1401bd1d0(param_1) with that SAME struct,
// exactly mirroring x86's own confirmed "FUN_00679db0 passes its own struct through
// unchanged via MOV ECX,ESI immediately before CALL FUN_00679010" relationship).
// Raw disassembly (DumpDisasm.java) of FUN_1401bd1d0 confirms the exact same
// quality-tier-clamp shape x86's FUN_00679010 has: `+0x20`/`+0x24` = requested W/H
// (copied UNCLAMPED into DAT_141888670/674, the real scene-resolution driver -- x64
// equivalent of x86's DAT_021d2e00/04), `+0x28`/`+0x2c` = native/upper-bound W/H
// (also permanently stashed once into DAT_14188868c/690, x64 equivalent of x86's
// DAT_021d2e1c/20), `+0x18` = aspect-ratio float, tier-clamped output written to
// DAT_141888678/67c (x64 equiv of DAT_021d2e08/0c) with an "is scaled" flag at
// DAT_141888688 (equiv of DAT_021d2e18). Standard x64 fastcall (RCX = the struct
// pointer) -- confirmed via raw disassembly (`MOV RBX,[RCX]` / `MOV RDI,RCX` at
// entry), no custom register convention, matching this file's own documented
// "x64 hooks are simpler than x86" pattern. Signature covers the function's first
// 63 bytes (DumpSigBytes.java + PatternScan.java, confirmed exactly 1 match in the
// whole binary) -- literal bytes kept for the two `LEA reg,[RSP+0x20]` instructions
// (RSP-relative, not an address -- the same false-positive class this file's own
// kPmoveTickSignature comment already documents), wildcarded for the three real
// internal CALLs and the one RIP-relative data write.
constexpr const char* kRenderResComputeSignature =
    "48 89 5C 24 08 57 48 83 EC 60 48 8B 19 48 8B F9 B9 15 00 00 00 "
    "E8 ?? ?? ?? ?? 48 8B D7 89 05 ?? ?? ?? ?? 48 8D 4C 24 20 "
    "E8 ?? ?? ?? ?? 4C 8D 44 24 20 BA 46 00 00 00 48 8B CB E8 ?? ?? ?? ??";

using RenderResComputeFn = void(__fastcall*)(void* self);
RenderResComputeFn g_origRenderResCompute = nullptr;

// Same override mechanism as x86's Hook_FUN_00679010: write the desired requested
// W/H into the incoming struct BEFORE the real trampoline runs, so its own
// unconditional, unclamped copy (DAT_141888670/674) picks up our value instead of
// the engine's own dvar-driven default -- no r_mode write, no vid_restart, no
// device/window recreation, identical risk profile to the x86 original this ports.
void __fastcall Hook_RenderResCompute(void* self)
{
    int pct = g_modConfig.internalRenderScalePercent;
    if (pct > 0 && self != nullptr) {
        auto* base = reinterpret_cast<uint8_t*>(self);
        int32_t nativeW = *reinterpret_cast<int32_t*>(base + 0x28);
        int32_t nativeH = *reinterpret_cast<int32_t*>(base + 0x2c);
        if (nativeW > 0 && nativeH > 0) {
            int targetW = static_cast<int>(static_cast<int64_t>(nativeW) * pct / 100);
            int targetH = static_cast<int>(static_cast<int64_t>(nativeH) * pct / 100);
            // Same 640x480 floor x86's own mode-enumeration logic enforces (issue
            // #88) -- a target below that is never a value this engine would
            // produce on its own.
            if (targetW >= 640 && targetH >= 480) {
                *reinterpret_cast<int32_t*>(base + 0x20) = targetW;
                *reinterpret_cast<int32_t*>(base + 0x24) = targetH;
                char buf[256];
                sprintf_s(buf, "[x64-video-scale] InternalRenderScalePercent -> native=%dx%d target=%dx%d -- "
                    "overriding requested scene render resolution before FUN_1401bd1d0 runs (feeds the real "
                    "unclamped scene render-target driver, no r_mode, no vid_restart)",
                    static_cast<int>(nativeW), static_cast<int>(nativeH), targetW, targetH);
                LogFromController(buf);
            }
        }
    }
    g_origRenderResCompute(self);
}

// In-level time-delta flag -- x64 equivalent of x86's kInLevelFlagAddr
// (0x00A98ACC). Found via the same per-frame orchestrator chain this project's own
// movement/look work already confirmed: x86's writer, FUN_0057e5b0, computes
// `DAT_00a98acc = now - lastFrameTime` (clamped to 200ms) then calls FUN_0057e480
// (the confirmed CL_CreateCmd-equivalent) -- x64's own confirmed equivalent of
// FUN_0057e480 is FUN_14007e1e0 (x64_migration/README.md's own per-frame usercmd
// table); its one real caller, FUN_14007d500, does the exact same computation with
// the exact same 200 (0xC8) clamp constant immediately before calling FUN_14007e1e0
// -- confirmed via decompile, not assumed from the caller relationship alone.
// Signature anchors the whole computation sequence (MOV ECX,[rip] / MOV EDX,0xC8 /
// SUB EAX,[rip] / CMP / MOV[rip] / LEA RSP-relative / CMOVA / XOR / MOV[rip] / CALL
// / MOV[rip], PatternScan.java-confirmed as the ONLY match in the whole binary) so
// the resolve is anchored on real, unique surrounding structure, not just the
// two-byte write opcode alone. The target write instruction (`MOV dword ptr
// [rip+disp32],EAX`, the final, already-clamped value) sits at a fixed +0x25 byte
// offset into this signature's own match, confirmed via raw disassembly.
constexpr const char* kInLevelFlagSignature =
    "8B 0D ?? ?? ?? ?? BA C8 00 00 00 8B C1 2B 05 ?? ?? ?? ?? 3B C2 89 0D ?? ?? ?? ?? "
    "48 8D 4C 24 20 0F 47 C2 33 D2 89 05 ?? ?? ?? ?? E8 ?? ?? ?? ?? 8B 0D ?? ?? ?? ??";
constexpr ptrdiff_t kInLevelFlagWriteInsnOffset = 0x25;

int32_t* g_inLevelFlag = nullptr;

// clcState -- x64 equivalent of x86's kClcStateAddrForTest/ForMenuCheck/
// ForBlurMenuCheck (0x00B36218). NOT independently signature-scanned -- derived
// directly from g_menuActiveGateFlag (already confirmed, resolved, and LIVE-
// working elsewhere this session) plus a fixed +8 byte struct offset, mirroring
// x86's own real relationship exactly (0x00B36218 = 0x00B36210 + 8, and
// g_menuActiveGateFlag is this project's own confirmed x64 counterpart of
// 0x00B36210). Cross-validated via a SECOND, fully independent code site:
// FUN_14007eaf0's own kbutton dispatch (decomp_buttons_pause_weapnext.txt) reads
// this exact per-player field via a totally different addressing mode
// (`[RDX + R10*0x1 + 0x6e2558]`, base-register + SIB, not RIP-relative) -- its own
// module-relative displacement, 0x6E2558, is EXACTLY 0x6E2550 + 8 (0x6E2550 being
// kPauseToggleSignature's own already-confirmed module-relative offset for
// DAT_1406e2550/g_menuActiveGateFlag), independently confirming the +8
// relationship from a second, unrelated function/addressing mode. Semantic
// cross-check, same decompile: this field's value gates real per-case logic at
// `== 6` -- matching this project's own already-confirmed x86 finding that
// clcState==6 means "ordinary SP/Survival gameplay" (re_notes/known_issues.md,
// FUN_00450740's own DAT_00b36218==6 gate) -- and separately at `== 1 || == 2`,
// both small-int-enum shapes matching clcState's real role, not some unrelated
// per-player counter. No separate signature/pointer needed: computed on demand
// from the already-resolved g_menuActiveGateFlag below.
constexpr ptrdiff_t kClcStateOffsetFromMenuGate = 8;

// Exported accessors for overlay_hud.cpp (a different translation unit) -- same
// "extern C escapes this anonymous namespace's internal linkage" pattern already
// established by IsMenuActive_Exported()/PollPauseToggleX64()/IsPhysicalHeld_Exported()
// elsewhere in this codebase. Each returns false/0 (never dereferences a null
// pointer) when its own signature hasn't resolved -- matching this file's own
// "read-only, defensive default" convention for every other not-yet-fully-proven
// x64 flag, and per CLAUDE.md SS5 ("fail loudly and refuse to hook... rather than
// jumping to garbage") the CALLER (overlay_hud.cpp) is expected to treat a false
// return as "this gate cannot be confirmed safe, do not run the pass" -- same
// fail-closed posture x86's own gating already has.
extern "C" bool IsMenuActiveX64_Exported()
{
    return g_menuActiveGateFlag && ((*g_menuActiveGateFlag & 0x10u) != 0);
}

extern "C" bool TryGetClcStateX64(int* outValue)
{
    if (!g_menuActiveGateFlag || !outValue) return false;
    *outValue = *reinterpret_cast<int32_t*>(
        reinterpret_cast<uint8_t*>(g_menuActiveGateFlag) + kClcStateOffsetFromMenuGate);
    return true;
}

extern "C" bool TryGetInLevelFlagX64(int* outValue)
{
    if (!g_inLevelFlag || !outValue) return false;
    *outValue = *g_inLevelFlag;
    return true;
}

extern "C" void GetMotionBlurDeltasX64(float* outYawDeg, float* outPitchDeg)
{
    if (outYawDeg) *outYawDeg = g_motionBlurYawDeltaDegX64;
    if (outPitchDeg) *outPitchDeg = g_motionBlurPitchDeltaDegX64;
}

}  // namespace

// Called from dllmain.cpp under #ifdef _M_X64, mirroring InstallAnalogInputHooks()'s
// own call site for the x86 build. Deliberately named distinctly (not an overload)
// so the call site itself makes the platform split visible, not just the #ifdef.
void InstallAnalogInputHooksX64()
{
    MH_Initialize(); // idempotent -- same pattern d3d9_hook.cpp already uses; this
                      // runs at DLL_PROCESS_ATTACH time (dllmain.cpp), before
                      // d3d9_hook.cpp's own later MH_Initialize() calls, so it must
                      // not assume MinHook is already up.

    // Each hook below is independent -- per CLAUDE.md SS5, a signature/install
    // failure is fatal to THAT hook only (fail loudly, refuse to install it,
    // never fall through to a garbage address), but must not block any other
    // hook's own independent attempt. 2026-09-04: previously this whole function
    // returned early on the first failure, which was fine when there was only
    // one hook -- now that a second, unrelated hook (Sprint) exists, that early
    // return would silently skip Sprint too if only the diagnostic hook failed.

    {
        SigScan::Result r = SigScan::FindPatternInMainModule(kPmoveTickSignature);
        if (!r.found) {
            LogFromController("[x64-diag] FATAL: Pmove-tick signature did not resolve -- diagnostic hook not installed");
        } else {
            void* target = reinterpret_cast<void*>(r.address);
            MH_STATUS createStatus = MH_CreateHook(target, reinterpret_cast<void*>(&Hook_PmoveTick),
                                                    reinterpret_cast<void**>(&g_realPmoveTick));
            if (createStatus != MH_OK) {
                char buf[160];
                sprintf_s(buf, "[x64-diag] FATAL: MH_CreateHook failed for Pmove tick @ 0x%llX (status=%d)",
                           static_cast<unsigned long long>(r.address), static_cast<int>(createStatus));
                LogFromController(buf);
            } else {
                MH_STATUS enableStatus = MH_EnableHook(target);
                if (enableStatus != MH_OK) {
                    char buf[160];
                    sprintf_s(buf, "[x64-diag] FATAL: MH_EnableHook failed for Pmove tick @ 0x%llX (status=%d)",
                               static_cast<unsigned long long>(r.address), static_cast<int>(enableStatus));
                    LogFromController(buf);
                } else {
                    LogFromController("[x64-diag] Pmove tick diagnostic hook installed and enabled -- log-and-call-through only, "
                        "zero behavior change. Watch the log for '[x64-diag] Pmove tick hook fired' during play "
                        "to confirm the whole signature-scan -> MinHook pipeline works on this build.");
                }
            }
        }
    }

    {
        SigScan::Result r = SigScan::FindPatternInMainModule(kSprintTickSignature);
        if (!r.found) {
            LogFromController("[x64-sprint] FATAL: Sprint-tick signature did not resolve -- Sprint hook not installed, "
                "controller Sprint will not work this session");
        } else {
            void* target = reinterpret_cast<void*>(r.address);
            MH_STATUS createStatus = MH_CreateHook(target, reinterpret_cast<void*>(&Hook_SprintTick),
                                                    reinterpret_cast<void**>(&g_realSprintTick));
            if (createStatus != MH_OK) {
                char buf[160];
                sprintf_s(buf, "[x64-sprint] FATAL: MH_CreateHook failed for Sprint tick @ 0x%llX (status=%d)",
                           static_cast<unsigned long long>(r.address), static_cast<int>(createStatus));
                LogFromController(buf);
            } else {
                MH_STATUS enableStatus = MH_EnableHook(target);
                if (enableStatus != MH_OK) {
                    char buf[160];
                    sprintf_s(buf, "[x64-sprint] FATAL: MH_EnableHook failed for Sprint tick @ 0x%llX (status=%d)",
                               static_cast<unsigned long long>(r.address), static_cast<int>(enableStatus));
                    LogFromController(buf);
                } else {
                    LogFromController("[x64-sprint] Sprint hook installed and enabled -- drives the real +sprint "
                        "kbutton (FUN_14007e460/e490 on the Sprint struct, resolved below) while the controller's "
                        "mapped Sprint input is held, matching x86's final shipped design (native duration/recovery "
                        "timer + Extreme Conditioning apply automatically; vanilla keyboard sprint untouched).");
                }
            }
        }
    }

    {
        SigScan::Result r = SigScan::FindPatternInMainModule(kMovementTickSignature);
        if (!r.found) {
            LogFromController("[x64-movement] FATAL: Movement-tick signature did not resolve -- Movement hook not "
                "installed, controller left-stick movement will not work this session");
        } else {
            void* target = reinterpret_cast<void*>(r.address);
            MH_STATUS createStatus = MH_CreateHook(target, reinterpret_cast<void*>(&Hook_MovementTick),
                                                    reinterpret_cast<void**>(&g_realMovementTick));
            if (createStatus != MH_OK) {
                char buf[160];
                sprintf_s(buf, "[x64-movement] FATAL: MH_CreateHook failed for Movement tick @ 0x%llX (status=%d)",
                           static_cast<unsigned long long>(r.address), static_cast<int>(createStatus));
                LogFromController(buf);
            } else {
                MH_STATUS enableStatus = MH_EnableHook(target);
                if (enableStatus != MH_OK) {
                    char buf[160];
                    sprintf_s(buf, "[x64-movement] FATAL: MH_EnableHook failed for Movement tick @ 0x%llX (status=%d)",
                               static_cast<unsigned long long>(r.address), static_cast<int>(enableStatus));
                    LogFromController(buf);
                } else {
                    LogFromController("[x64-movement] Movement hook installed and enabled -- adds left-stick "
                        "forward/right movement on top of whatever the native keyboard writer already produced "
                        "this tick, additive and unclamped-input-gated (no stick deflection = no-op).");
                }
            }

            // "Needs a click" input-gate flag -- resolved from the SAME match
            // address as the Movement hook above (no separate scan), see
            // kInputGateFlagTestInsnOffset's own comment for the full trace.
            // Independent of whether the hook install above succeeded.
            uintptr_t testInsnAddr = r.address + kInputGateFlagTestInsnOffset;
            g_inputGateFlag = reinterpret_cast<uint32_t*>(
                SigScan::ResolveRipRelativeAt(testInsnAddr + 2, testInsnAddr + 10));
            if (g_inputGateFlag) {
                char buf[160];
                sprintf_s(buf, "[x64-inputgate] Resolved @ 0x%p -- forcing bit 0x%X clear every "
                    "Movement tick (experimental fix for \"needs a click to get input\").",
                    (void*)g_inputGateFlag, kInputGateBit);
                LogFromController(buf);
            } else {
                LogFromController("[x64-inputgate] FATAL: input-gate flag failed to resolve -- the "
                    "\"needs a click to get input\" symptom will not be addressed this session");
            }
        }
    }

    // Look shares the Movement hook above (both live in FUN_14007d9f0 -- MinHook
    // only supports one detour per target address, so this can't be a separate
    // MH_CreateHook) -- this block just resolves the two angle-accumulator DATA
    // addresses Hook_MovementTick's own look logic needs (g_pitchAccum/
    // g_yawAccum), independent of whether the Movement hook install above
    // succeeded, per this file's own decoupled-block convention. If this fails,
    // Sprint/Movement still work -- Look alone silently no-ops (g_pitchAccum/
    // g_yawAccum stay null, Hook_MovementTick's own null-check skips the whole
    // look block).
    {
        SigScan::Result r = SigScan::FindPatternInMainModule(kAngleAccumSignature);
        if (!r.found) {
            LogFromController("[x64-look] FATAL: angle-accumulator signature did not resolve -- Look hook not "
                "installed, controller right-stick look will not work this session");
        } else {
            uintptr_t pitchInsnAddr = r.address;       // first movss, 8 bytes
            uintptr_t yawInsnAddr = r.address + 14;    // second movss, 8 bytes, starts after the first movss (8) + its stack store (6)
            g_pitchAccum = reinterpret_cast<float*>(SigScan::ResolveRipRelative(pitchInsnAddr, 8));
            g_yawAccum = reinterpret_cast<float*>(SigScan::ResolveRipRelative(yawInsnAddr, 8));
            if (!g_pitchAccum || !g_yawAccum) {
                LogFromController("[x64-look] FATAL: angle-accumulator RIP-relative resolution failed -- Look hook "
                    "not installed, controller right-stick look will not work this session");
            } else {
                char buf[192];
                sprintf_s(buf, "[x64-look] Angle accumulators resolved: pitch=0x%p yaw=0x%p -- look injection "
                    "active, folded into the Movement hook (same tick, pre-call write).",
                    (void*)g_pitchAccum, (void*)g_yawAccum);
                LogFromController(buf);
            }
        }
    }

    // Buttons/ADS/Reload -- resolves FUN_14007e460/FUN_14007e490 (the real
    // kbutton activate/deactivate handlers) directly for a DIRECT CALL, not
    // a hook -- no MinHook involvement. FUN_14007c3a0 is ALSO resolved here --
    // used both as a stable anchor for locating the three per-bind struct
    // addresses and the shared timestamp global via fixed byte offsets (see
    // kFireStructInsnOffset's own comment), AND as a genuine callable
    // dispatcher for CrouchProne (see kCrouchProneCaseDown's own comment) and
    // Jump's own auto-stand (see ForceStandingViaRealToggleX64's own comment) --
    // the same g_stanceDispatch pointer serves both.
    {
        SigScan::Result activateResult = SigScan::FindPatternInMainModule(kKbuttonActivateSignature);
        if (!activateResult.found) {
            LogFromController("[x64-buttons] FATAL: kbutton-activate signature did not resolve -- Fire/ADS/Reload "
                "will not work this session");
        } else {
            g_kbuttonActivate = reinterpret_cast<KbuttonActivateFn>(activateResult.address);
        }

        SigScan::Result deactivateResult = SigScan::FindPatternInMainModule(kKbuttonDeactivateSignature);
        if (!deactivateResult.found) {
            LogFromController("[x64-buttons] FATAL: kbutton-deactivate signature did not resolve -- Fire/ADS/Reload "
                "will not work this session");
        } else {
            g_kbuttonDeactivate = reinterpret_cast<KbuttonDeactivateFn>(deactivateResult.address);
        }

        SigScan::Result anchorResult = SigScan::FindPatternInMainModule(kAnchorSignature);
        if (!anchorResult.found) {
            LogFromController("[x64-buttons] FATAL: struct-anchor signature did not resolve -- Fire/ADS/Reload "
                "will not work this session");
        } else {
            uintptr_t anchor = anchorResult.address;
            g_fireStruct = reinterpret_cast<int*>(SigScan::ResolveRipRelative(anchor + kFireStructInsnOffset, kRipInsnLength));
            g_reloadStruct = reinterpret_cast<int*>(SigScan::ResolveRipRelative(anchor + kReloadStructInsnOffset, kRipInsnLength));
            g_adsStruct = reinterpret_cast<int*>(SigScan::ResolveRipRelative(anchor + kAdsStructInsnOffset, kRipInsnLength));
            g_sprintStruct = reinterpret_cast<int*>(SigScan::ResolveRipRelative(anchor + kSprintStructInsnOffset, kRipInsnLength));
            g_holdBreathStruct = reinterpret_cast<int*>(SigScan::ResolveRipRelative(anchor + kHoldBreathStructInsnOffset, kRipInsnLength));
            g_timestampPtr = reinterpret_cast<volatile uint32_t*>(SigScan::ResolveRipRelative(anchor + kTimestampInsnOffset, kRipInsnLength));
            g_adsToggleFlag = reinterpret_cast<volatile uint8_t*>(SigScan::ResolveRipRelative(anchor + kAdsToggleFlagInsnOffset, kRipInsnLength));
            g_stanceDispatch = reinterpret_cast<StanceDispatchFn>(anchor);
            g_notifyBindDispatch = reinterpret_cast<NotifyBindFn>(anchor + kNotifyBindFuncOffset);
            if (g_stanceDispatch) {
                char buf[128];
                sprintf_s(buf, "[x64-crouchprone] Stance dispatch resolved @ 0x%llX -- CrouchProne active "
                    "(direct call, no hook installed).", static_cast<unsigned long long>(anchor));
                LogFromController(buf);
            }
            if (g_notifyBindDispatch) {
                // Was char buf[160] -- too small for this message's own worst case (169 chars +
                // null = 170 bytes: a 16-hex-digit %p plus surrounding text), a real, previously
                // unexecuted bug caught live 2026-09-05 (see known_issues_x64.md issue #1): this
                // UCRT's sprintf_s fails fast (0xC0000409 / FAST_FAIL_INVALID_ARG) rather than
                // silently truncating when the formatted output doesn't fit, unlike the truncation
                // behavior this codebase's other sprintf_s call sites happened to never exceed.
                char buf[256];
                sprintf_s(buf, "[x64-sniper-fix] Reliable-command notify dispatch resolved @ 0x%p -- "
                    "sniper Fire/ADS fix attempt active (fixed +0x%llX offset from stance-dispatch anchor).",
                    (void*)g_notifyBindDispatch, static_cast<unsigned long long>(kNotifyBindFuncOffset));
                LogFromController(buf);
            }
        }

        if (g_kbuttonActivate && g_kbuttonDeactivate && g_fireStruct && g_reloadStruct && g_adsStruct
            && g_timestampPtr && g_adsToggleFlag) {
            char buf[320];
            sprintf_s(buf, "[x64-buttons] Fire/ADS/Reload active: fireStruct=0x%p reloadStruct=0x%p "
                "adsStruct=0x%p timestampPtr=0x%p adsToggleFlag=0x%p (direct calls, no hook installed).",
                (void*)g_fireStruct, (void*)g_reloadStruct, (void*)g_adsStruct, (void*)g_timestampPtr,
                (void*)g_adsToggleFlag);
            LogFromController(buf);
        } else {
            LogFromController("[x64-buttons] FATAL: one or more Fire/ADS/Reload targets failed to resolve -- "
                "these will not work this session");
        }

        // Sprint's own kbutton struct -- resolved in the same block (shares the
        // anchor + g_kbuttonActivate/g_kbuttonDeactivate above) but logged/
        // checked separately since it's consumed by Hook_SprintTick, a
        // different hook than Fire/ADS/Reload's direct-call site.
        if (g_kbuttonActivate && g_kbuttonDeactivate && g_sprintStruct && g_timestampPtr) {
            char buf[192];
            sprintf_s(buf, "[x64-sprint] Sprint kbutton struct resolved @ 0x%p -- real +sprint kbutton active "
                "(direct call from Hook_SprintTick, no raw pm_flags-forcing).", (void*)g_sprintStruct);
            LogFromController(buf);
        } else {
            LogFromController("[x64-sprint] FATAL: Sprint kbutton struct failed to resolve -- controller Sprint "
                "will not work this session");
        }

        // Hold Breath's own kbutton struct -- resolved in the same block
        // (shares the anchor + g_kbuttonActivate/g_kbuttonDeactivate above),
        // consumed by Hook_SprintTick alongside Sprint's own struct. See
        // kHoldBreathStructInsnOffset's own comment for the full resolve trail.
        if (g_kbuttonActivate && g_kbuttonDeactivate && g_holdBreathStruct && g_timestampPtr) {
            char buf[224];
            sprintf_s(buf, "[x64-holdbreath] Hold Breath kbutton struct resolved @ 0x%p -- real kbutton active "
                "(direct call from Hook_SprintTick, gated on ADS -- parity audit item #23).", (void*)g_holdBreathStruct);
            LogFromController(buf);
        } else {
            LogFromController("[x64-holdbreath] FATAL: Hold Breath kbutton struct failed to resolve -- controller "
                "Hold Breath will not work this session");
        }
    }

    // Pause toggle -- same direct-call pattern as Buttons above.
    {
        SigScan::Result r = SigScan::FindPatternInMainModule(kPauseToggleSignature);
        if (!r.found) {
            LogFromController("[x64-pause] FATAL: pause-toggle signature did not resolve -- controller Pause "
                "will not work this session");
        } else {
            g_pauseToggle = reinterpret_cast<PauseToggleFn>(r.address);
            char buf[160];
            sprintf_s(buf, "[x64-pause] Pause toggle resolved @ 0x%llX -- active (direct call, no hook installed).",
                static_cast<unsigned long long>(r.address));
            LogFromController(buf);

            // Diagnostic-only menu-active gate resolve, see
            // kMenuActiveGateInsnOffset's own comment above.
            g_menuActiveGateFlag = reinterpret_cast<uint32_t*>(
                SigScan::ResolveRipRelative(r.address + kMenuActiveGateInsnOffset, 7));
            if (g_menuActiveGateFlag) {
                char buf2[160];
                sprintf_s(buf2, "[x64-diag-gate] menu-active gate resolved @ 0x%p (diagnostic only, "
                    "not forced/written) -- watch for [x64-diag-gate] heartbeat lines.",
                    (void*)g_menuActiveGateFlag);
                LogFromController(buf2);
            }
        }
    }

    // InternalRenderScalePercent -- MinHook detour on FUN_1401bd1d0 (x64 equivalent
    // of x86's FUN_00679010), same override-before-trampoline mechanism as the
    // x86 original. See kRenderResComputeSignature's own comment for the full
    // discovery trail.
    {
        SigScan::Result r = SigScan::FindPatternInMainModule(kRenderResComputeSignature);
        if (!r.found) {
            LogFromController("[x64-video-scale] FATAL: render-resolution-compute signature did not resolve -- "
                "InternalRenderScalePercent will have no effect this session");
        } else {
            void* target = reinterpret_cast<void*>(r.address);
            MH_STATUS createStatus = MH_CreateHook(target, reinterpret_cast<void*>(&Hook_RenderResCompute),
                                                    reinterpret_cast<void**>(&g_origRenderResCompute));
            if (createStatus != MH_OK) {
                char buf[160];
                sprintf_s(buf, "[x64-video-scale] FATAL: MH_CreateHook failed for render-res-compute @ 0x%llX (status=%d)",
                           static_cast<unsigned long long>(r.address), static_cast<int>(createStatus));
                LogFromController(buf);
            } else {
                MH_STATUS enableStatus = MH_EnableHook(target);
                if (enableStatus != MH_OK) {
                    char buf[160];
                    sprintf_s(buf, "[x64-video-scale] FATAL: MH_EnableHook failed for render-res-compute @ 0x%llX (status=%d)",
                               static_cast<unsigned long long>(r.address), static_cast<int>(enableStatus));
                    LogFromController(buf);
                } else {
                    LogFromController("[x64-video-scale] InternalRenderScalePercent hook installed and enabled -- "
                        "FUN_1401bd1d0 (x64 equivalent of x86's FUN_00679010).");
                }
            }
        }
    }

    // In-level time-delta flag -- resolves g_inLevelFlag (one of FSR RCAS/motion
    // blur's two real safety gates on x64; the third, menu-active, is already
    // covered by g_menuActiveGateFlag above). See kInLevelFlagSignature's own
    // comment for the full discovery trail. clcState needs no separate resolve --
    // it's derived on demand from g_menuActiveGateFlag (TryGetClcStateX64 above).
    {
        SigScan::Result r = SigScan::FindPatternInMainModule(kInLevelFlagSignature);
        if (!r.found) {
            LogFromController("[x64-visualfx-gate] FATAL: in-level time-delta flag signature did not resolve -- "
                "FSR RCAS and motion blur will stay disabled this session (safety gate unresolved)");
        } else {
            uintptr_t writeInsnAddr = r.address + kInLevelFlagWriteInsnOffset;
            g_inLevelFlag = reinterpret_cast<int32_t*>(SigScan::ResolveRipRelative(writeInsnAddr, 6));
            if (g_inLevelFlag) {
                char buf[192];
                sprintf_s(buf, "[x64-visualfx-gate] In-level flag resolved @ 0x%p -- FSR RCAS/motion blur gating "
                    "now available (menu-active + in-level>0 + clcState!=0, matching x86's own three-gate design).",
                    (void*)g_inLevelFlag);
                LogFromController(buf);
            } else {
                LogFromController("[x64-visualfx-gate] FATAL: in-level time-delta flag RIP-relative resolution "
                    "failed -- FSR RCAS and motion blur will stay disabled this session (safety gate unresolved)");
            }
        }
    }

    // Weapnext -- same direct-call pattern as Buttons/Pause above.
    {
        SigScan::Result r = SigScan::FindPatternInMainModule(kWeaponNextSignature);
        if (!r.found) {
            LogFromController("[x64-weapnext] FATAL: weapnext signature did not resolve -- controller weapon "
                "switch will not work this session");
        } else {
            g_weaponNext = reinterpret_cast<WeaponNextFn>(r.address);
            char buf[160];
            sprintf_s(buf, "[x64-weapnext] Weapnext resolved @ 0x%llX -- active (direct call, no hook installed).",
                static_cast<unsigned long long>(r.address));
            LogFromController(buf);
        }
    }

    // D-pad actionslot -- same direct-call pattern as Weapnext/Pause above.
    // Melee/Lethal/Tactical/Jump/Interact need NO separate resolve at all --
    // they're raw usercmd_t.buttons bits, written directly via the SAME `cmd`
    // pointer Movement/Look already use, no new signature required.
    {
        SigScan::Result r = SigScan::FindPatternInMainModule(kActionSlotSignature);
        if (!r.found) {
            LogFromController("[x64-actionslot] FATAL: D-pad actionslot signature did not resolve -- controller "
                "D-pad will not work this session");
        } else {
            g_actionSlot = reinterpret_cast<ActionSlotFn>(r.address);
            char buf[160];
            sprintf_s(buf, "[x64-actionslot] D-pad actionslot resolved @ 0x%llX -- active (direct call, no hook "
                "installed).", static_cast<unsigned long long>(r.address));
            LogFromController(buf);
        }
    }

    // Menu-focus/itemDef tracking (2026-09-12 port) -- resolves the real x64 UI
    // context global (DAT_142605050) via a RIP-relative LEA inside FUN_14029baa0's
    // own confirmed prologue, and GetTopmostActiveMenu's real entry point
    // (FUN_1402aaa80) for a direct call. Both independent of each other and of
    // every other resolve in this function -- a failure here only disables the
    // real Options-screen trigger and the itemDef-walk diagnostic, the temporary
    // LB+RB chord (already resolved above via g_menuActiveGateFlag) stays available
    // either way. See this file's own "Menu-focus / itemDef-array tracking" section
    // header comment for the full confirmation trail behind every offset used here.
    {
        SigScan::Result r = SigScan::FindPatternInMainModule(kUiContextAnchorSignature);
        if (!r.found) {
            LogFromController("[x64-menufocus] FATAL: UI-context anchor signature did not resolve -- the real "
                "Options-screen open trigger and menu-focus diagnostic will not work this session (LB+RB chord "
                "still available)");
        } else {
            g_uiMenuContextX64 = reinterpret_cast<void*>(
                SigScan::ResolveRipRelative(r.address + kCtxLoadInsnOffset, 7));
            if (g_uiMenuContextX64) {
                char buf[160];
                sprintf_s(buf, "[x64-menufocus] UI context resolved @ 0x%p.", g_uiMenuContextX64);
                LogFromController(buf);
            } else {
                LogFromController("[x64-menufocus] FATAL: UI-context RIP-relative resolution failed -- the real "
                    "Options-screen open trigger and menu-focus diagnostic will not work this session (LB+RB "
                    "chord still available)");
            }
        }
    }
    {
        SigScan::Result r = SigScan::FindPatternInMainModule(kGetTopmostActiveMenuSignature);
        if (!r.found) {
            LogFromController("[x64-menufocus] FATAL: GetTopmostActiveMenu signature did not resolve -- the real "
                "Options-screen open trigger and menu-focus diagnostic will not work this session (LB+RB chord "
                "still available)");
        } else {
            g_getTopmostActiveMenuX64 = reinterpret_cast<GetTopmostActiveMenuFnX64>(r.address);
            char buf[160];
            sprintf_s(buf, "[x64-menufocus] GetTopmostActiveMenu resolved @ 0x%llX -- active (direct call, no "
                "hook installed).", static_cast<unsigned long long>(r.address));
            LogFromController(buf);
        }
    }
    if (g_uiMenuContextX64 && g_getTopmostActiveMenuX64) {
        LogFromController("[x64-menufocus] Real menu-focus/itemDef tracking active -- the real focus-based "
            "Options-screen trigger and [x64-menufocus-diag]/[x64-optmenu-realtrigger] log lines are live.");
    }

    // Native D-pad+A/B menu navigation (2026-09-12) -- FUN_1402aac50, the real x64
    // ForwardKeyToMenu equivalent (see kMenuKeyEventSignature's own header comment
    // for the full confirmation trail). Independent of the menu-focus resolve above
    // (a failure here only disables native menu-item navigation/B-back -- the
    // Options-screen focus-based trigger and LB+RB chord above are unaffected either
    // way, and vice versa).
    {
        SigScan::Result r = SigScan::FindPatternInMainModule(kMenuKeyEventSignature);
        if (!r.found) {
            LogFromController("[x64-menunav] FATAL: ForwardKeyToMenu (FUN_1402aac50) signature did not resolve -- "
                "native D-pad+A/B menu navigation and B's ESC-forward will not work this session (keyboard/mouse "
                "still required for all menu interaction)");
        } else {
            g_menuKeyEventX64 = reinterpret_cast<MenuKeyEventFnX64>(r.address);
            char buf[160];
            sprintf_s(buf, "[x64-menunav] ForwardKeyToMenu resolved @ 0x%llX -- active (direct call, no hook "
                "installed).", static_cast<unsigned long long>(r.address));
            LogFromController(buf);
        }
    }
    if (g_uiMenuContextX64 && g_getTopmostActiveMenuX64 && g_menuKeyEventX64) {
        LogFromController("[x64-menunav] Native D-pad+A/B menu navigation and B's ESC-forward are active -- main "
            "menu, pause menu, options drill-down, and buy-station/armory lists are now controller-navigable.");
    } else {
        LogFromController("[x64-menunav] Native menu navigation NOT fully active this session (see FATAL lines "
            "above for which real target failed to resolve) -- keyboard/mouse will still be needed for some or "
            "all menu interaction.");
    }

    // Vibration/rumble (2026-09-12 x64 port) -- see rumble.cpp's own "x64 PORT" block
    // for the full RE trail (fire-effects hook + entity-array resolve for the damage
    // health poll). Called last, same as every other independent hook/resolve group
    // in this function -- its own success/failure is logged and gated internally by
    // Rumble_Install(), doesn't block or depend on anything else installed above.
    Rumble_Install();
}
