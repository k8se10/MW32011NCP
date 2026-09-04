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

void __fastcall Hook_PmoveTick(void* param1)
{
    ++g_fireCount;
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
constexpr size_t kRipInsnLength = 7;

int* g_fireStruct = nullptr;
int* g_reloadStruct = nullptr;
int* g_adsStruct = nullptr;
volatile uint32_t* g_timestampPtr = nullptr;

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

// Edge-tracking state, one bool per logical action -- mirrors x86's own
// g_adsHeld/g_reloadHeld/g_startHeld pattern (analog_input_hooks.cpp).
bool g_fireHeldX64 = false;
bool g_adsHeldX64 = false;
bool g_reloadHeldX64 = false;
bool g_pauseHeldX64 = false;
bool g_weaponSwitchHeldX64 = false;

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

void __fastcall Hook_MovementTick(void* param1, unsigned int param2)
{
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
        }

        if (g_kbuttonActivate && g_kbuttonDeactivate && g_fireStruct && g_reloadStruct && g_adsStruct && g_timestampPtr) {
            char buf[256];
            sprintf_s(buf, "[x64-buttons] Fire/ADS/Reload active: fireStruct=0x%p reloadStruct=0x%p "
                "adsStruct=0x%p timestampPtr=0x%p (direct calls, no hook installed).",
                (void*)g_fireStruct, (void*)g_reloadStruct, (void*)g_adsStruct, (void*)g_timestampPtr);
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
}
