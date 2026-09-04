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
#include "../third_party/minhook/include/MinHook.h"
#include "signature_scan.h"
#include "controller_input.h"
#include "mod_config.h"

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

// Bit-ownership tracking (2026-09-04) -- matches x86's own established fix for
// the EXACT same regression class (CLAUDE.md's "Sprint's real kbutton" section:
// InjectControllerSprintPmFlags/ReassertSprintPmFlags unconditionally clearing a
// bit it didn't set broke vanilla keyboard sprint). Only ever clear the pm_flags
// sprint bit here if THIS hook was the one that set it -- if native (keyboard)
// logic set it, this hook must never touch it, in either direction.
bool g_sprintBitForcedByUs = false;

void __fastcall Hook_SprintTick(void* param1, void* param2)
{
    // Let native logic run to completion FIRST, untouched -- matches x86's own
    // "force the OUTPUT bit directly, after the real logic runs" design
    // (InjectControllerSprintPmFlags), not the alternative of feeding a
    // synthetic "held" input bit into param1 before the call, which risks
    // corrupting a shared bitfield with only partially-understood semantics
    // (see the real x64 finding: bit 0x2 at param1+0xc is read by more than
    // just this one function).
    g_realSprintTick(param1, param2);

    if (!param1) return;
    void* lVar3 = *reinterpret_cast<void**>(param1);
    if (!lVar3) return;

    // pm_flags-equivalent field, confirmed via this session's own RE (the exact
    // field FUN_140014a80 itself ORs 0x4000 into on its own native sprint path).
    auto* pmFlags = reinterpret_cast<uint32_t*>(reinterpret_cast<uintptr_t>(lVar3) + 0xc);
    constexpr uint32_t kSprintBit = 0x4000u;

    unsigned short buttons = 0;
    unsigned char leftTrigger = 0, rightTrigger = 0;
    bool haveController = Controller_GetRawButtonsAndTriggers(buttons, leftTrigger, rightTrigger);
    bool sprintHeld = haveController && IsPhysicalHeld_Exported(g_buttonMap.sprint, buttons, leftTrigger, rightTrigger);

    if (sprintHeld) {
        if ((*pmFlags & kSprintBit) == 0) {
            *pmFlags |= kSprintBit;
            g_sprintBitForcedByUs = true;
        }
    } else if (g_sprintBitForcedByUs) {
        *pmFlags &= ~kSprintBit;
        g_sprintBitForcedByUs = false;
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
constexpr size_t kRipInsnLength = 7;

int* g_fireStruct = nullptr;
int* g_reloadStruct = nullptr;
int* g_adsStruct = nullptr;
volatile uint32_t* g_timestampPtr = nullptr;
volatile uint8_t* g_adsToggleFlag = nullptr;

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
            } else {
                g_lookAccelStartMsX64 = 0; // stick back at neutral -- next push starts the ramp fresh
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
                    if (fireHeld) g_kbuttonActivate(g_fireStruct, kSyntheticSourceId, timestamp);
                    else g_kbuttonDeactivate(g_fireStruct, kSyntheticSourceId, timestamp);
                }
            }

            if (g_adsStruct) {
                bool adsHeld = IsPhysicalHeld_Exported(g_buttonMap.ads, xiButtons, leftTrigger, rightTrigger);
                if (adsHeld != g_adsHeldX64) {
                    g_adsHeldX64 = adsHeld;
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

        if (g_weaponNext) {
            bool weaponSwitchHeld = IsPhysicalHeld_Exported(g_buttonMap.weaponSwitch, xiButtons, leftTrigger, rightTrigger);
            if (weaponSwitchHeld && !g_weaponSwitchHeldX64) {
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
            g_jumpHeldX64 = jumpHeld;
            // NOTE: x86's own "auto-stand from crouch/prone on Jump's rising edge"
            // enhancement (ForceStandingViaRealToggle) is deliberately NOT ported
            // yet -- it depends on the same real stance-toggle mechanism
            // CrouchProne itself needs, which this pass explicitly defers (see
            // the big comment above). Jump's own core bit-force works standalone
            // without it; this is a minor feature gap, not a functional bug.

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
        if (g_actionSlot) {
            struct { unsigned short bit; int slot; } kDpad[4] = {
                { kXI_DPAD_UP_X64, 0 }, { kXI_DPAD_RIGHT_X64, 1 }, { kXI_DPAD_DOWN_X64, 2 }, { kXI_DPAD_LEFT_X64, 3 }
            };
            for (int i = 0; i < 4; ++i) {
                bool held = (xiButtons & kDpad[i].bit) != 0;
                if (held && !g_dpadHeldX64[i]) {
                    g_actionSlot(0, kDpad[i].slot);
                }
                g_dpadHeldX64[i] = held;
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
                    LogFromController("[x64-sprint] Sprint hook installed and enabled -- forces the real pm_flags "
                        "sprint bit while the controller's mapped Sprint input is held, bit-ownership tracked so "
                        "vanilla keyboard sprint is never touched.");
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
    // a hook -- no MinHook involvement. FUN_14007c3a0 is ALSO resolved here,
    // but only as a stable anchor for locating the three per-bind struct
    // addresses and the shared timestamp global via fixed byte offsets
    // (see kFireStructInsnOffset's own comment) -- it's never itself called.
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
            g_timestampPtr = reinterpret_cast<volatile uint32_t*>(SigScan::ResolveRipRelative(anchor + kTimestampInsnOffset, kRipInsnLength));
            g_adsToggleFlag = reinterpret_cast<volatile uint8_t*>(SigScan::ResolveRipRelative(anchor + kAdsToggleFlagInsnOffset, kRipInsnLength));
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
}
