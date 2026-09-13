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
#include <cmath> // sqrtf/atan2f/fabsf -- Auto-Mantle's stick-cone check (2026-09-13),
                 // mirrors x86's own InjectControllerButtons math exactly
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
// Same file, same class of internal-linkage fix as IsPhysicalHeld_Exported/
// RouteStickAxes_Exported above -- ShouldDrawGlyphOverlay() lives in
// analog_input_hooks.cpp's own giant anonymous namespace, is pure cross-platform
// logic (no x86-only address/__asm), and is reused here (2026-09-13 text-draw hook
// port) so x64's Mantle-hint detection gates identically to x86's own
// `ShouldDrawGlyphOverlay() && !IsMenuActive()` block.
extern "C" bool ShouldDrawGlyphOverlay_Exported();
// Forward declaration -- defined later in this file (Mantle-hint structural-match
// detection, 2026-09-13 text-draw hook port). extern "C" here matches its actual
// definition's linkage-specification exactly (this project's own established MSVC
// lesson, CLAUDE.md's "Checking is far cheaper than digging": the FIRST declaration
// of a name establishes its linkage, so a forward declaration of an extern "C"
// function must itself say extern "C" or the later definition conflicts).
// Auto-Mantle's own fire condition (Hook_MovementTick, below) needs this.
extern "C" bool IsMantleHintCurrentlyShowingX64();

// Forward declarations for real glyph-icon SUBSTITUTION (2026-09-13, text-draw hook
// extension) -- all four are plain C++ (NOT extern "C") functions defined at file
// scope (outside the giant anonymous namespace) in analog_input_hooks.cpp, so a
// plain matching declaration here links correctly via ordinary C++ name mangling
// (both TUs compile with the same MSVC/ABI) -- no _Exported wrapper needed, unlike
// IsPhysicalHeld_Exported/RouteStickAxes_Exported/ShouldDrawGlyphOverlay_Exported
// above, which exist specifically to escape THAT file's anonymous namespace
// (internal linkage). These four already have external linkage as written.
//   - TryGetMantleGlyphAssetName/TryGetThrowbackGlyphAssetName: existing x86
//     functions (issue #68), reused verbatim, zero x86 changes.
//   - TryGetPickupGlyphAssetName: NEW function added to analog_input_hooks.cpp this
//     same pass specifically for this x64 port (see that file's own comment on it) --
//     purely additive, x86's own Hook_DrawGlyphText never calls it, so x86's existing
//     generic-path behavior for pickup/swap/health is completely unchanged.
// ConvertRealScreenPosToDesignSpace is NOT declared here -- confirmed via a real
// LNK2019 (checking beats digging, CLAUDE.md SS5) that x86's own copy has internal
// linkage too (it turned out to sit inside a SECOND anonymous namespace opening at
// that file's line ~7019, not the one closing at ~6873 immediately above it -- an
// easy miss by eye, caught by the linker instead of assumed). Duplicated locally
// below (Hook_DrawTextX64's own section) instead, same convention as
// TextMatchesTemplateStructurallyX64/FindColorHighlightSpanX64 -- it's four lines of
// pure math over two already-cross-platform functions (GetResolutionScale/
// GetLastKnownRenderDevice, overlay_hud.h, already used on x64 by
// controller_input.cpp/overlay_hud.cpp), not worth adding a fourth _Exported wrapper
// to analog_input_hooks.cpp for.
bool TryGetMantleGlyphAssetName(char* outAssetName, size_t outSize);
bool TryGetThrowbackGlyphAssetName(char* outAssetName, size_t outSize);
bool TryGetPickupGlyphAssetName(char* outAssetName, size_t outSize);
// TryGetMenuGlyphAssetNameForKeyName (2026-09-13, menu corner-hint port): existing
// x86 function (issue #68), reused verbatim -- resolves through the SEPARATE
// menu-specific bind vocabulary (ResolveMenuGlyphAssetNameForKeyName), not the
// gameplay table the four functions above use, since "ESC"/"F" mean different
// physical buttons in a menu-hint context (B/Y) than they would in gameplay's own
// table (Start/X) -- see that function's own header comment in analog_input_hooks.cpp.
// Also defined at file scope there (outside the anonymous namespace), same linkage
// class as the four above.
bool TryGetMenuGlyphAssetNameForKeyName(const char* keyName, char* outAssetName, size_t outSize);

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
// it ports directly with no RE needed.
//
// RESOLVED 2026-09-13 (was scoped OUT of the first x64 pass): the ADS-FOV
// look-slowdown (GetAdsLookRateScale) needed x64 equivalents of GetEffectiveFov/
// Dvar_FindVar -- both now found (FUN_140069e60/FUN_1402c3890, see the
// GetAdsLookRateScaleX64 block further down this file, right before
// Hook_MovementTick, for the full resolution trail) and wired into
// Hook_MovementTick's own Look pre-hook. gyro-aim stays out of scope for this
// pass (already PREVIEW/WIP and never live-tested on x86 itself, lowest
// priority, unrelated dependency) -- see known_issues_x64.md issue #1.
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
// RESOLVED 2026-09-13: x86 additionally gates the fire behind IsInSurvivalMode()
// (a mapname-dvar read via x86's raw Dvar_FindVar-equivalent, FUN_0062abe0 @
// 0x0062abe0). x64's own Dvar_FindVar equivalent (FUN_1402c3890) is now resolved
// (see the IsInSurvivalModeX64()/GetDvarStringX64() block further down this file,
// right before Hook_MovementTick) -- this function itself still fires
// unconditionally on any call (kept simple/self-contained, matching
// SendSyntheticActionSlot4KeyX64's own shape), but its ONE real call site
// (Hook_MovementTick's Y/weapnext hold-edge block, below) now wraps it in the
// same `if (IsInSurvivalModeX64())` gate x86 uses, closing the gap the comment
// here used to document. See known_issues_x64.md issue #1's "Survival ready-up"
// section for the full resolution record.
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

// ---- IsSprintActiveX64() (2026-09-13) -- Auto-Mantle's own fire-condition needs
// this exact logical check, per x86's own IsSprintActive() (analog_input_hooks.cpp,
// line ~1939: `return g_sprintHeld && GetRealStance() == 0 && !g_adsHeld;`). Composed
// from three x64 tracking vars that already exist for other, unrelated reasons --
// no new RE needed, confirmed by re-reading x86's own function in full first (it's a
// plain logical state check, not a native memory read):
//   - g_sprintKbuttonActiveX64 (Hook_SprintTick, above) is x64's own equivalent of
//     x86's g_sprintHeld -- true whenever this project's own real +sprint kbutton
//     claim is currently active (sprintHeld && !adsHeldNow, computed fresh every
//     Sprint-hook tick).
//   - GetRealStanceX64() (this file) is the direct x64 equivalent of x86's
//     GetRealStance(), already used elsewhere this session (Hook_SprintTick's own
//     rising-edge stand-up call, ForceStandingViaRealToggleX64's dispatch).
//   - g_adsHeldX64 (Hook_MovementTick, below) is x64's own equivalent of x86's
//     g_adsHeld, already used for Hold Breath and gyro-only-while-ADS gating.
// Same honest caveat x86's own design already carries (not a new one introduced
// here): this reads LOGICAL input intent (button state + stance + not-ADS), not
// the native sprint duration/recovery timer's own internal state -- x86's
// IsSprintActive() has never accounted for that either, so this is an exact parity
// port, not a regression relative to x86.
bool IsSprintActiveX64()
{
    return g_sprintKbuttonActiveX64 && GetRealStanceX64() == 0 && !g_adsHeldX64;
}

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

// TryGetRealFocusedItemNameX64 (2026-09-13, menu-hint parity follow-up) -- a small,
// deliberate extension of TryGetRealFocusedGroupAndIndexX64 immediately above: THE
// SAME topmost-menu/itemDef-array walk and THE SAME focus-flag check
// (kMenuItemCountOffsetX64/kMenuItemArrayOffsetX64/kItemFocusFlagsOffsetX64/
// kItemNameOffsetX64, all already live-confirmed working via the A-glyph/F2-F3 fix,
// 2026-09-12), just returning the focused item's RAW name unconditionally instead of
// requiring it to parse as "<group>_<index>". No new RE -- every offset here is
// already validated.
//
// Why this is needed as a SEPARATE function rather than reusing
// TryGetRealFocusedGroupAndIndexX64 directly: x86's own g_focusedItemName (fed by a
// register hook on the real getfocuseditemname()-equivalent, analog_input_hooks.cpp
// Hook_00616230) captures the focused item's name in EVERY case, including ones that
// deliberately do NOT fit the "<group>_<index>" shape -- "Chaos"/"Mission"/"Survival"
// (Special Ops mode-picker buttons), "none" (no focus), "friendList" (the Friends
// list screen), "SWF_COMMON_POPUP_NAME_0" (a generic popup template). x86's own
// IsInsideSpecOpsNestedModal()/IsFriendsListOpen() (analog_input_hooks.cpp) key
// directly off exactly these non-numeric-suffixed names -- TryGetRealFocusedGroupAndIndexX64
// would return false and never expose them (see its own `if (j == len || j == 0 ||
// name[j - 1] != '_') return false;` line), so it cannot serve as this function's
// data source even though it walks the identical structure.
bool TryGetRealFocusedItemNameX64(char* outName, size_t outNameSize)
{
    if (outNameSize > 0) outName[0] = '\0';
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
            strncpy_s(outName, outNameSize, name, _TRUNCATE);
            return true;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
    return false;
}

// x64 port of x86's IsInsideSpecOpsNestedModal() (analog_input_hooks.cpp, "v4,
// allowlist + sticky state" -- read that function's own large header comment for the
// full investigation history behind this exact logic; byte-for-byte the same
// algorithm here, only the data source differs (TryGetRealFocusedItemNameX64 instead
// of the x86 register-hook-fed g_focusedItemName). g_specOpsModalStickyX64 mirrors
// x86's g_specOpsModalSticky -- carries forward through generic/ambiguous focus
// states, only clears on a confirmed return to a real indexed list item.
bool g_specOpsModalStickyX64 = false;

bool IsInsideSpecOpsNestedModalX64()
{
    char name[128] = {};
    bool have = TryGetRealFocusedItemNameX64(name, sizeof(name));
    if (!have || name[0] == '\0') return g_specOpsModalStickyX64;

    static const char* const kKnownModalItemNames[] = {
        "Chaos", "Mission", "Survival",
    };
    for (const char* known : kKnownModalItemNames) {
        if (_stricmp(name, known) == 0) {
            g_specOpsModalStickyX64 = true;
            return true;
        }
    }

    if (_stricmp(name, "none") == 0) return g_specOpsModalStickyX64;

    // Same "<name>_<digits>" indexed-list-item detection as x86's own function,
    // including the "SWF_"-prefix exclusion (a generic popup template can ALSO
    // match "<name>_<digits>" -- see x86's own comment for the live-confirmed
    // false-clear this excludes).
    size_t len = strlen(name);
    size_t i = len;
    while (i > 0 && isdigit(static_cast<unsigned char>(name[i - 1]))) --i;
    bool looksLikeIndexedListItem = (i < len && i > 0 && name[i - 1] == '_') &&
                                      _strnicmp(name, "SWF_", 4) != 0;
    if (looksLikeIndexedListItem) {
        g_specOpsModalStickyX64 = false;
        return false;
    }

    return g_specOpsModalStickyX64;
}

// x64 port of x86's IsFriendsListOpen() (analog_input_hooks.cpp) -- direct,
// stateless check, same as x86: no sticky/staleness concerns needed here.
bool IsFriendsListOpenX64()
{
    char name[128] = {};
    if (!TryGetRealFocusedItemNameX64(name, sizeof(name))) return false;
    return _strnicmp(name, "friendList", 10) == 0;
}

}  // namespace

// Thin exported wrappers (2026-09-13, parity audit rows #35/#36 -- highlighted-item
// A-glyph + F2/F3 glyph-position editor) -- GetMenuStackDepthX64/
// TryGetRealFocusedGroupAndIndexX64 above live inside this file's own anonymous
// namespace (opened well before this point), same class of internal-linkage issue
// this file already fixed once for IsPhysicalHeld_Exported/RouteStickAxes_Exported
// (analog_input_hooks.cpp) -- confirmed via the same reasoning rather than assumed:
// a function in an anonymous namespace has internal linkage and cannot be called
// directly from a different translation unit. analog_input_hooks.cpp's own
// TryGetStableFocusedGroupAndIndex() (x64 branch) is the one real consumer that
// needs these from outside this file, closing the parity gap for both features in
// one place since neither ever calls the raw functions directly.
extern "C" bool TryGetRealFocusedGroupAndIndexX64_Exported(char* outGroupName, size_t outGroupNameSize, int& outIndex, int& outSiblingCount)
{
    return TryGetRealFocusedGroupAndIndexX64(outGroupName, outGroupNameSize, outIndex, outSiblingCount);
}

extern "C" int GetMenuStackDepthX64_Exported()
{
    return GetMenuStackDepthX64();
}

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

// ---- Dvar_FindVar + GetEffectiveFov x64 equivalents (2026-09-13) -- RESOLVED --------
//
// Closes the "genuinely unresolved RE target" gap GetLookAccelerationScaleX64's own
// comment above and SendSyntheticF5X64's own comment further down both flag. Found via
// this project's own established dvar-value-discovery chain (CLAUDE.md's "New Ghidra
// RE tooling" section): a broad RawStringScan.java sweep for "cg_fov" led to its real
// x64 registration call (FUN_14004a870), which also independently re-confirms (fresh
// decompile+disassembly this session, not just trusted from the earlier doc) the real
// x64 dvar VALUE offset is +0x10, not x86's +0xc -- already found once before by
// re_notes/x64_migration/actionslot_dvarhelpers_x64.md for the generic int/string
// getters, now confirmed a second, independent way for the float case specifically.
// A follow-up string sweep for the three sibling FOV dvars x86's own GetEffectiveFov
// comment names by name (cg_fovScale/cg_fovMin/cg_fovNonVehAdd) led straight to the
// real consumer, FUN_140069e60 -- full trail in re_notes/x64_migration/
// getEffectiveFov_dvarFindVar_x64.md.
namespace {

// Dvar_FindVar-equivalent: FUN_1402c3890(name) -> dvar_t*. Standard x64 calling
// convention (name in RCX) -- confirmed via disassembly of its own callers (e.g. the
// int/bool getter below): `CALL 0x1402c3890` sits right at function entry with ZERO
// register setup beforehand, RCX passed straight through unmodified from the caller.
// Genuinely simpler than x86's FUN_0062abe0, which needed a custom EDI-register
// convention and an inline __asm block (real_settings.cpp/analog_input_hooks.cpp's own
// GetDvarInt) -- no __asm needed here at all.
using FindDvarX64Fn = void*(*)(const char* name);
FindDvarX64Fn const FindDvarX64Raw = reinterpret_cast<FindDvarX64Fn>(0x1402c3890);

// Deliberately NOT reusing the generic GetDvarInt/Bool-equivalent (FUN_1402c3b10,
// re_notes/x64_migration/actionslot_dvarhelpers_x64.md) for float dvars -- confirmed
// via disassembly that it special-cases type tags 5/6 (float/string) to return the raw
// dword at +0x10 UNCONVERTED, while every OTHER type tag goes through a generic
// to-int conversion call (FUN_140396f34) -- exactly backwards from a naive "GetDvarInt
// so surely it converts floats" assumption. Reading the float directly ourselves at
// the confirmed +0x10 offset (same pattern x86's own GetDvarFloat already uses at its
// own +0xc offset) sidesteps that ambiguity entirely -- matches this project's own
// established "don't reuse one getter across dvar types" policy (real_settings.cpp's
// own GetDvarBool/Float/String header comment) and is independently re-confirmed by
// FUN_140069e60 below reading cg_fov/cg_fov1 the exact same way (raw MOVSS at +0x10).
float GetDvarFloatX64(const char* name)
{
    void* dvarPtr = FindDvarX64Raw(name);
    if (!dvarPtr) return 0.0f;
    return *reinterpret_cast<float*>(reinterpret_cast<uintptr_t>(dvarPtr) + 0x10);
}

// GetDvarString-equivalent: FUN_1402c3b50(name) -> const char*. Confirmed via direct
// decompile+disassembly to be safe to call as-is (same real function the x64
// actionslot/dvar-helpers doc already found, independently re-verified this session):
// type tag 6 (string) does an extra indirection through a live string table at +0x48,
// every other type returns the raw pointer at +0x10 directly, and a not-found dvar
// returns the same real empty-string sentinel (&DAT_1403e6bdb) x86's own GetDvarString
// falls back to -- always safe to call, never returns null.
using GetDvarStringX64Fn = const char*(*)(const char* name);
GetDvarStringX64Fn const GetDvarStringX64Raw = reinterpret_cast<GetDvarStringX64Fn>(0x1402c3b50);

const char* GetDvarStringX64(const char* name)
{
    return GetDvarStringX64Raw(name);
}

// GetEffectiveFov-equivalent: FUN_140069e60(playerIndex) -> float. Standard x64
// calling convention (playerIndex in ECX, confirmed via disassembly: `MOV EDI, ECX`
// at function entry, no other register setup beforehand) -- returns in XMM0 as a
// plain float (the standard x64 ABI float-return register), not x86's x87 float10/
// ST(0), so no cast is needed on our side the way x86's
// `static_cast<float>(GetEffectiveFov(...))` needed one.
//
// Independently confirmed via disassembly to implement the EXACT SAME blend x86's own
// GetEffectiveFov comment documents, dvar-handle-for-dvar-handle: reads cg_fov/cg_fov1
// (selected by playerIndex, at the confirmed +0x10 value offset) as the base/no-zoom
// value; an alt-scope weapon path gated by the same bit-2/mask-0x4 flag-byte check
// x86's DAT_00984b9c uses (a different address, same bit convention); a real
// time-based lerp driving toward cg_fovScale's value for the transition system x86's
// own comment names (set_lerp_fov/set_pip_fov/set_turret_fov -- confirmed via those
// three strings' own real xrefs converging on this exact call chain, both directly
// and via its one sibling helper, FUN_140068a80); cg_playerFovScale0/1 folded in via
// a per-entity table walk; and a final cg_fovNonVehAdd add + cg_fovMin floor clamp.
// Matches x86's documented formula component-for-component, not a guess from the
// function's name alone. Confirmed read-only in its own disassembly (no stores to any
// of the transition-state globals it reads) -- matches x86's own "pure query, no
// observed side effects" note.
using GetEffectiveFovX64Fn = float(*)(int playerIndex);
GetEffectiveFovX64Fn const GetEffectiveFovX64 = reinterpret_cast<GetEffectiveFovX64Fn>(0x140069e60);

// Mirrors x86's own GetAdsLookRateScale (analog_input_hooks.cpp) formula exactly, now
// that both of its real dependencies are resolved above -- see that function's own
// comment block for the full rationale/history behind each term (the 2026-07-16
// negative-scale root-cause fix that moved this to a power curve, the 2026-07-31
// close-range taper for zero-zoom weapons like pistols); not re-derived here, just
// ported verbatim against the x64 equivalents. Deliberately omits x86's own rate-
// limited `[ads-fov-diag]` byte-flag diagnostic (DAT_00984b9c's x64 equivalent was
// not part of this pass's RE scope, and this formula's math itself was already
// confirmed correct on x86 -- issue #8/#44 -- so there is no open question here that
// diagnostic would be answering).
float GetAdsLookRateScaleX64()
{
    if (!g_adsHeldX64 || g_modConfig.adsSlowdownStrength <= 0.0f) return 1.0f;

    float baseFov = GetDvarFloatX64("cg_fov");
    if (baseFov <= 0.0f) return 1.0f;

    float effectiveFov = GetEffectiveFovX64(kLocalClientIndexX64);
    if (effectiveFov <= 0.0f) return 1.0f;

    float ratio = effectiveFov / baseFov;
    float scale = g_modConfig.adsSlowdownBaseline * powf(ratio, g_modConfig.adsSlowdownStrength);

    // Issue #44's close-range taper, ported verbatim (see x86's own comment for the
    // full derivation) -- mathematically safe for any adsCloseRangeSlowdownStrength
    // in [0, 1] (clamped on config load): ratio is always in (0, 1], so closeRangeFactor
    // is always in [1-strength, 1], never negative, never inverts.
    constexpr float kCloseRangeFocusPower = 8.0f;
    float closeRangeFactor = 1.0f - g_modConfig.adsCloseRangeSlowdownStrength * powf(ratio, kCloseRangeFocusPower);
    scale *= closeRangeFactor;

    return scale;
}

// IsInSurvivalMode-equivalent: reads the real `mapname` dvar (a string-type dvar, so
// GetDvarStringX64, not GetDvarFloatX64) via the now-resolved GetDvarStringX64 above.
// Byte-for-byte the same check as x86's own IsInSurvivalMode (analog_input_hooks.cpp):
// matches FUN_00526b30's own "so_survival_" prefix check on x86 -- the GSC data/map-
// naming convention itself is unchanged by the x64 recompile (per this task's own
// scope note and this project's standing "game data/scripts are unchanged by the
// recompile" assumption elsewhere in this file), so the same prefix carries over
// unmodified rather than needing independent x64 confirmation of the string itself.
bool IsInSurvivalModeX64()
{
    const char* mapName = GetDvarStringX64("mapname");
    if (!mapName) return false;
    return _strnicmp(mapName, "so_survival_", 12) == 0;
}

} // namespace

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
                // ADS-FOV look-slowdown now wired in (2026-09-13, GetAdsLookRateScaleX64
                // above) -- mirrors x86's own `GetAdsLookRateScale() * GetLookAccelerationScale()`
                // sharedScale exactly (analog_input_hooks.cpp's InjectControllerLookAngles).
                float scale = GetAdsLookRateScaleX64() * GetLookAccelerationScaleX64();
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

    // FIX (2026-09-13, Fire/ADS "intermittent, on and off" investigation --
    // known_issues_x64.md issue #1's Fire/ADS section): this used to be
    // `if (moveX == 0.0f && moveY == 0.0f) return;` -- an early return for the
    // WHOLE REST OF THE FUNCTION, not just the movement-byte write immediately
    // below it. x86's own equivalent (InjectAllControllerInput,
    // analog_input_hooks.cpp) calls InjectControllerMovement/Ads/Fire/Sprint/
    // Reload/WeaponNext/Dpad/Scoreboard/PauseMenu/MenuBack/Rumble_Tick as
    // COMPLETELY SEPARATE, unconditionally-called functions -- none of them
    // gated on whether the movement stick is currently producing nonzero
    // output. x64 fused all of that into this one per-tick function (see this
    // file's own FOURTH/FIFTH/SIXTH ROUND history above), and the early return
    // -- almost certainly intended only to skip the "nothing to add" case for
    // the cmd[0x1c]/[0x1d] write two lines down -- ended up silently gating
    // EVERY control below it (Fire, ADS, Reload, Weapnext, Melee, Lethal,
    // Tactical, Jump, Interact, D-pad, CrouchProne, Scoreboard, the gameplay-
    // tick Pause-open poll, and Rumble_Tick) behind "is the left stick
    // currently off-center." A player standing still to aim precisely (the
    // exact moment Fire/ADS matter most) has moveX==moveY==0.0f essentially by
    // definition, so this function returned before ever reading the trigger/
    // button state at all -- reported live as "intermittent, on and off, a
    // really abnormal bug," reproducing on every weapon (nothing to do with
    // weapon class, matching the earlier 2026-09-13 pistol repro that already
    // ruled out the sniper-specific theory). Fixed by scoping the early-out to
    // just the movement-byte write (the one piece that's actually a no-op with
    // no stick input), letting every downstream control run unconditionally
    // every tick again, matching x86's own independent-function design.
    auto* cmd = reinterpret_cast<unsigned char*>(param1);
    if (moveX != 0.0f || moveY != 0.0f) {
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
    }

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
                // IsInSurvivalModeX64() gate wired 2026-09-13, matching x86's own
                // InjectControllerWeaponNext exactly (analog_input_hooks.cpp) -- see
                // SendSyntheticF5X64's own comment above for the resolution trail.
                if (IsInSurvivalModeX64()) {
                    SendSyntheticF5X64();
                }
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

            // Auto-mantle (2026-09-13 x64 port) -- STRICTLY opt-in, matches x86's
            // exact default (g_modConfig.autoMantleEnabled = false, mod_config.h --
            // unchanged by this port). Drives the SAME real +gostand usercmd bit
            // (kJumpUsercmdBit, 0x400 -- identical value/offset to x86's own
            // confirmed +0x04 usercmd_t.buttons field, this session's own Movement
            // work) Jump already uses just above -- no new native trigger needed,
            // see analog_input_hooks.cpp's own "Auto-mantle" comment block (line
            // ~1431) for the full original rationale/regression history (the
            // "jumps always when trying to sprint" fix that made gating on the
            // real native mantle-hint text-draw mandatory, not optional).
            //
            // Both of x86's real fire-condition pieces are now available on x64:
            //   - IsMantleHintCurrentlyShowingX64() (this file, defined later --
            //     see the forward declaration near the top) -- x64's own real,
            //     structural-match ledge-availability gate, shipped 2026-09-13,
            //     the dependency this feature was previously blocked on
            //     (known_issues_x64.md issue #1's Auto-Mantle section).
            //   - IsSprintActiveX64() (this file, above) -- composed this same
            //     pass from three already-existing x64 tracking vars, exact parity
            //     port of x86's own IsSprintActive(), no new RE needed.
            // moveX/moveY are reused directly from this function's own earlier
            // RouteStickAxes_Exported() call (this same tick) rather than a second
            // stick read -- x86's InjectControllerButtons is a separate function
            // from InjectControllerMovement so it has to re-read the stick itself;
            // Hook_MovementTick is both, so the values are already in scope.
            //
            // Cooldown (750ms, matches x86's own kAutoMantleCooldownMs exactly,
            // 2026-08-03 user-requested value) -- suppresses re-triggering every
            // single tick the hint stays showing, same reasoning as x86.
            if (g_modConfig.autoMantleEnabled) {
                static DWORD s_lastAutoMantleTriggerMsX64 = 0;
                constexpr DWORD kAutoMantleCooldownMsX64 = 750;

                static DWORD s_lastAutoMantleDiagLogMsX64 = 0;
                DWORD nowMsDiag = GetTickCount();
                bool sprintActiveDiag = IsSprintActiveX64();
                bool mantleHintDiag = IsMantleHintCurrentlyShowingX64();
                if (mantleHintDiag || (nowMsDiag - s_lastAutoMantleDiagLogMsX64) >= 500) {
                    s_lastAutoMantleDiagLogMsX64 = nowMsDiag;
                    char amDiagBuf[128];
                    sprintf_s(amDiagBuf, "[automantle-diag-x64] sprintActive=%d mantleHintShowing=%d",
                        sprintActiveDiag ? 1 : 0, mantleHintDiag ? 1 : 0);
                    LogFromController(amDiagBuf);
                }

                if (sprintActiveDiag && mantleHintDiag &&
                    (GetTickCount() - s_lastAutoMantleTriggerMsX64) >= kAutoMantleCooldownMsX64) {
                    float amMagnitude = sqrtf(moveX * moveX + moveY * moveY);
                    if (moveY > 0.0f && amMagnitude >= g_modConfig.autoMantleMinStickMagnitude) {
                        constexpr float kPiX64 = 3.14159265f;
                        float amHalfConeRad = (g_modConfig.autoMantleForwardConeDegrees * 0.5f) * (kPiX64 / 180.0f);
                        float amAngleFromForward = atan2f(fabsf(moveX), moveY);
                        if (amAngleFromForward <= amHalfConeRad) {
                            out |= kJumpUsercmdBit;
                            s_lastAutoMantleTriggerMsX64 = GetTickCount();
                        }
                    }
                }
            }

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
                // 2026-09-13 CRASH FIX: this literal text alone (before any %d substitution) is
                // 245 chars -- the old buf[256] only had room for 11 more bytes total (10 digits +
                // null), but 4 int substitutions can each need up to 11 chars (a negative sign +
                // 10 digits), so the true worst case is well over 256 and sprintf_s's own UCRT
                // fail-fast (0xc0000409, exception subcode 5 FAST_FAIL_INVALID_ARG) crashed on
                // EVERY launch reaching this line -- confirmed via a live crash dump
                // (iw5sp.exe.14364.dmp), same bug class as the 2026-09-05 sprintf_s sweep. Sized
                // generously above the real worst case rather than trimmed to the exact minimum,
                // per this project's own established fix convention for this bug class.
                char buf[320];
                sprintf_s(buf, "[x64-video-scale] InternalRenderScalePercent -> native=%dx%d target=%dx%d -- "
                    "overriding requested scene render resolution before FUN_1401bd1d0 runs (feeds the real "
                    "unclamped scene render-target driver, no r_mode, no vid_restart)",
                    static_cast<int>(nativeW), static_cast<int>(nativeH), targetW, targetH);
                LogFromController(buf);

                // Port of x86's high-render-scale safety warning (2026-09-13, x64
                // feature-parity audit item #7; x86 original: analog_input_hooks.cpp
                // Hook_FUN_00679010, issue #105, 2026-08-29). This was missed when
                // InternalRenderScalePercent itself was ported to x64 2026-09-12 (row
                // #43) -- the base feature carried over, this warning didn't. Same
                // >2.25x-area (~150% linear) threshold, same one-time-per-session
                // gate, same ShowOverlayMessageUntilDismissed mechanism as x86's
                // original. Honest caveat x86's own warning didn't need: iw5sp_x64/
                // iw5mp_x64.exe are 64-bit processes, so the specific "hard 4GB
                // address-space ceiling" reasoning behind x86's warning text doesn't
                // apply as-is here -- worded accordingly below rather than copied
                // verbatim, and the underlying crash/freeze risk at high scale has NOT
                // been independently re-tested against x64's own larger address
                // space. Ported as a precaution (a real risk was demonstrated on x86
                // at this same render-cost multiplier; the x64 architecture change
                // alone doesn't prove it can't recur), not because the identical
                // failure mode is confirmed to reproduce here.
                static bool s_highScaleWarningShownX64 = false;
                int64_t targetAreaX64 = static_cast<int64_t>(targetW) * targetH;
                int64_t nativeAreaX64 = static_cast<int64_t>(nativeW) * nativeH;
                if (!s_highScaleWarningShownX64 && nativeAreaX64 > 0 &&
                    targetAreaX64 * 4 > nativeAreaX64 * 9) { // > 2.25x area, i.e. > ~150% linear
                    s_highScaleWarningShownX64 = true;
                    LogFromController("[x64-video-scale][WARNING] target resolution is well above native -- "
                        "x86's own version of this warning (known_issues.md issue #105) cited a hard 4GB "
                        "address-space ceiling and real crashes/freezes reproduced at 250-300%% -- this is a "
                        "64-bit process so that specific ceiling doesn't apply as-is, and the underlying "
                        "high-scale crash/freeze risk has NOT been independently re-tested on x64 -- see the "
                        "on-screen warning");
                    ShowOverlayMessageUntilDismissed(
                        "Render resolution is set well above native. x86 builds of this mod hit real "
                        "crashes/freezes above ~250% scale due to a 32-bit memory ceiling that doesn't apply "
                        "to this 64-bit build as-is, but high-scale stability has not been independently "
                        "re-tested here.\n\n"
                        "Enter / Space / Click to continue anyway:",
                        OverlayAnimStyle::Plain);
                }
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

// ---- Custom mouse cursor overlay (x86's DrawCustomCursorIfNeeded gate), x64 port (2026-09-13) ----
//
// x86's DrawCustomCursorIfNeeded (overlay_hud.cpp) early-returns unconditionally on
// x64 -- kCursorVisibleFlagAddr/kCursorUiStateAddr (0x01c00474/0x01c0ad14) are raw
// x86-only addresses, meaningless against x64's real module base, see that
// function's own header comment (known_issues_x64.md issue #1). Real x64
// equivalents found via analyzeHeadless.bat -process iw5sp.exe -readOnly
// -noanalysis against re_notes/ghidra_project_x64/iw5sp_x64_proj (full trail:
// re_notes/known_issues_x64.md issue #1's own "Custom mouse cursor overlay" round).
//
// x86's DAT_01c00474 (cursor-visible flag, written by FUN_005385d0 from a real
// mouse-position bounds check) and DAT_01c0ad14 (per-player UI/menu-state value,
// gated as a switch with cases 0/6/10 hidden) are both read together by ONE native
// function, FUN_00478540 -- the real native cursor-draw dispatcher (confirmed via
// full decompile, re_notes/known_issues.md issue #52's own writer trail). The x64
// equivalent of THAT function is FUN_14029d170, found by tracing every reference to
// DAT_142615b20 -- already independently confirmed elsewhere in this file as x64's
// exact equivalent of x86's DAT_01c0ad14 (FUN_14029f3f0/kPauseToggleSignature's own
// SetMenuState work: `(&DAT_142615b20)[player] = mode`, writing the SAME literal
// mode values x86's own writer used -- 6=briefing, 7=victoryscreen, etc., see
// re_notes/x64_migration/decomp_menustate_openmenu_x64.txt). One of that global's
// 21 total references (DescribeRefs.java) landed inside a function performing the
// EXACT same gate/switch/draw shape as x86's FUN_00478540: skip if visFlag==0 or
// uiState==0; if uiState==3, check "sp_acceptinvite_warning[_nosave]" (the SAME two
// literal strings x86's own FUN_00478540 checks, byte-for-byte); else skip if
// uiState==6 or ==10; otherwise draw via an 8-parameter native quad-draw call using
// an asset explicitly loaded as "ui_cursor" (FUN_14029b640, the x64 UI-init
// function: `DAT_142604f58 = FUN_1401c4ba0("ui_cursor",0)`) at a position read from
// DAT_142605060/142605064 -- offset +0x10/+0x14 from the confirmed UI-context base
// DAT_142605050, the EXACT SAME +0x10 offset x86's own cursor-position pair
// (DAT_01c00468/046c) sits at relative to ITS OWN uiContext base (DAT_01c00458).
// The visible-flag address, DAT_14260506c, sits at +0x1c from that same base --
// again the identical offset to x86 (0x01c00458+0x1c=0x01c00474). Two fully
// independent lines of evidence (structural function-shape match AND identical
// struct-offset arithmetic on both platforms) agree -- high confidence despite
// neither address being live-tested yet.
//
// Signature anchors the two-instruction gate directly (re_notes/x64_migration/
// rawbytes_cursor_gate2.txt -- hand-verified byte-for-byte against the real
// disassembly, not DumpSigBytes.java's own reference-based heuristic, since both
// hits here are genuine RIP-relative loads to global data with no RSP-relative
// false-positive risk): `CMP dword ptr [rip+disp],R15D` (the visFlag test, offset
// 0, 7 bytes) then `JZ`, then `MOV EAX,[rip+disp]` (the uiState read, offset +13, 6
// bytes), then `TEST EAX,EAX / JZ / CMP EAX,3 / JZ` -- 16 literal bytes across 5
// distinct opcodes plus 2 wildcarded 4-byte rel32 jump targets (function-internal,
// never resolved -- only the two RIP-relative data loads are), extremely unlikely
// to collide elsewhere in this binary.
constexpr const char* kCursorGateSignature =
    "44 39 3D ?? ?? ?? ?? 0F 84 ?? ?? ?? ?? 8B 05 ?? ?? ?? ?? 85 C0 0F 84 ?? ?? ?? ?? 83 F8 03 74 0A";
constexpr ptrdiff_t kCursorVisFlagInsnOffset = 0;
constexpr size_t kCursorVisFlagInsnLength = 7;
constexpr ptrdiff_t kCursorUiStateInsnOffset = 13;
constexpr size_t kCursorUiStateInsnLength = 6;

int32_t* g_cursorVisibleFlagX64 = nullptr;
int32_t* g_cursorUiStateX64 = nullptr;

// Exported accessor for overlay_hud.cpp (a different translation unit) -- same
// "extern C escapes this anonymous namespace's internal linkage" pattern as
// IsMenuActiveX64_Exported/TryGetClcStateX64 above. Fails closed (returns false,
// never dereferences a null pointer) when either signature hasn't resolved --
// DrawCustomCursorIfNeeded is expected to skip drawing entirely rather than guess
// at a value that was never confirmed, matching x86's own "never crash on a bad
// read" __except posture with a resolve-time check instead of a runtime SEH catch.
extern "C" bool TryGetCursorGateX64(int* outVisFlag, int* outUiState)
{
    if (!g_cursorVisibleFlagX64 || !g_cursorUiStateX64 || !outVisFlag || !outUiState) return false;
    *outVisFlag = *g_cursorVisibleFlagX64;
    *outUiState = *g_cursorUiStateX64;
    return true;
}

// ---- Native text-draw hook (x86's Hook_DrawGlyphText), x64 port (2026-09-13) ------
//
// PRIOR STATE: this was the single remaining blocker for gameplay controller-glyph
// icons, on-screen hint prompts, and Auto-Mantle (re_notes/known_issues_x64.md issue
// #1's "Scope note" round, 2026-09-12, and the separate Auto-Mantle investigation
// round the same day) -- analog_input_hooks_x64.cpp made zero calls to any glyph/
// hint-request function and had no equivalent of x86's Hook_DrawGlyphText at all.
//
// DISCOVERY (read x86's Hook_DrawGlyphText -- analog_input_hooks.cpp, target
// FUN_00690c80 -- in full first, per this project's own compare-to-x86-original
// rule, before starting this trace): x86's own big comment above kDrawGlyphTextAddr
// documents its hook target was found by tracing FUN_00568110 (the weapon-pickup/
// swap hint-STRING builder, found via a raw string reference to
// "PLATFORM_PICKUPNEWWEAPON") forward through its real callers to the eventual
// draw call. Same technique applied here, anchored on "PLATFORM_MANTLE" instead
// (RawStringScan.java against re_notes/ghidra_project_x64/iw5sp_x64_proj):
//   1. "PLATFORM_MANTLE" @ 0x1403f0568, one reference, inside FUN_140052220 --
//      the x64 equivalent of x86's generic HUD-element dispatcher (FUN_00568110's
//      own role, just considerably more inlined on x64: this ONE function builds
//      the substitution string AND calls the draw chain per hud-element-type
//      case, rather than splitting hint-string-building and draw-queueing across
//      several separate functions the way x86 does). Case 0x50 (Mantle) and case
//      0x47 (Hold Breath) both resolve their reference key via
//      `FUN_14029f120(key)` (the real x64 SEH_GetString/GetLocalizedString
//      equivalent -- confirmed via full decompile: skips lookup for an
//      already-literal 0x15-escape-prefixed string, otherwise defers to a real
//      reference->current-language table lookup, and falls back to echoing the
//      raw key back into a static buffer if not found -- byte-for-byte the same
//      documented contract as x86's own FUN_00532230), then call
//      `FUN_14029a2b0(dcHandle, text, 0x7fffffff, fontPtr, x, y, color1, color2,
//      scale, colorVecPtr, extra)`.
//   2. FUN_14029a2b0 decompiled: an 11-parameter, plain (no custom register
//      tricks) function with 22 REAL CALLERS spanning completely unrelated HUD
//      elements (pickup/mantle/hold-breath hints, the FPS counter's own
//      "fps: %f" string, death-quote captions, distance/waypoint markers,
//      COOP_WAITINGFORPLAYER, ...) -- exactly the "universal, used-everywhere"
//      character x86's own comment attributes to FUN_00690c80. This, not the
//      smaller leaf it calls internally (FUN_140080840, a thin single-caller
//      forwarder into FUN_1401d2520 with no wider fan-in), is the correct x64
//      hook point -- matching x86's own hook SITE (a call site every drawn
//      string of this kind passes through), not chasing the deepest possible
//      leaf. Confirmed via FindCallers.java (22 callers) and DecompileAt.java.
//   3. Font_s* is threaded as an opaque pointer the entire way through
//      (FUN_140052220's own `param_14`, an "undefined8" never dereferenced by
//      that function itself) -- same "opaque handle in, opaque handle out" shape
//      x86's own fontArg has. x64's real Font_s struct layout (the x86 DiagFont
//      equivalent, needed for IsGameplayHintFont-style font-name filtering) was
//      NOT independently re-derived this pass -- see the "NOT COVERED" note
//      below.
//
// PLAIN-C++-CALLABLE, NO __ASM (per this file's own top-of-file convention):
// FUN_14029a2b0's first 4 parameters are non-float (dcHandle/text/maxChars/
// fontPtr), so no XMM-vs-GP register-class ambiguity for the register-passed
// slots; every parameter past the 4th is stack-passed on x64 regardless of type,
// so declaring x/y/scale as `float` here reads the identical bytes a `float` on
// the stack always would -- a plain typed function pointer MinHook can detour to
// directly.
//
// SCOPE OF THIS PASS (deliberately staged, matching this project's own "prove the
// plumbing before the payload" convention -- see this hook's own two stages
// below):
//   (a) Passthrough/logging only -- proves signature-scan -> MinHook-install ->
//       detour-fires-correctly on THIS specific call site, zero behavior change.
//   (b) Mantle-hint detection wired on top of (a): structurally matches the real,
//       LIVE-RESOLVED localized PLATFORM_MANTLE template (via FUN_14029f120,
//       resolved separately below, NOT the real_settings.cpp GetLocalizedString()
//       x64 stub, which deliberately just echoes the key back and would never
//       match real rendered text) against the text this draw call is about to
//       render, using the identical structural "&&1"-marker algorithm x86's own
//       RenderedTextMatchesSubstitutionTemplateWithMarker uses (duplicated here,
//       not cross-file-shared, since it's ~10 lines of pure string logic and the
//       x86 original lives in that file's own anonymous namespace). On a match,
//       advances g_mantleHintLastSeenMsX64 -- the x64 equivalent of x86's
//       g_mantleHintDrawnThisFrame/g_mantleHintLastSeenMs pair, collapsed to a
//       single timestamp write (no per-frame accumulate-then-commit step) since
//       IsMantleHintCurrentlyShowingX64() below is already a pure grace-window
//       check that only cares about the last-seen timestamp, not which specific
//       tick recorded it -- observably equivalent, simpler, no need to find/hook
//       an x64 "once per rendered frame" commit point for this alone. This is
//       gated exactly like x86's own block (`ShouldDrawGlyphOverlay() &&
//       !IsMenuActive()`, via the _Exported wrappers above) so x64's Auto-Mantle
//       dependency-readiness carries the SAME real coupling to the glyph-overlay
//       toggle x86 has -- not a divergence, a faithful port of that quirk too.
//   (c) REAL VISUAL GLYPH-ICON SUBSTITUTION (2026-09-13, second pass, same day):
//       extends (b)'s structural-match technique to two more hint families
//       CONFIRMED to flow through this exact same hook (FUN_14004fa00, found via
//       RawStringScan anchored on their own real reference-key strings, then
//       DecompileAt-confirmed to call FUN_14029a2b0 directly, same as Mantle/Hold
//       Breath in FUN_140052220):
//         - PLATFORM_PICKUPNEWWEAPON / PLATFORM_SWAPWEAPONS / PLATFORM_PICKUPHEALTH
//           ("Press^3 &&1 ^7to pick up"/"...to swap for"/pickup-health's own
//           equivalent, all real "+activate" binds per ui_assets.md's zone-dump
//           research) -- icon resolved via the NEW TryGetPickupGlyphAssetName
//           (analog_input_hooks.cpp, LogicalAction::ReloadUse, same physical key
//           Reload's own icon already uses).
//         - PLATFORM_THROWBACKGRENADE ("^3&&1 ^7throw back") -- icon resolved via
//           the EXISTING TryGetThrowbackGlyphAssetName (LogicalAction::Lethal),
//           unchanged from x86.
//       On any match, extracts the real "^N...^7" highlighted span from the ACTUAL
//       rendered text (FindColorHighlightSpanX64, a local duplicate of x86's own
//       FindColorHighlightSpan for the same internal-linkage reason
//       TextMatchesTemplateStructurallyX64 is duplicated -- see that function's own
//       comment), splits it into prefix/suffix text, converts this call's own real
//       x/y into design-space (ConvertRealScreenPosToDesignSpace, existing x86
//       function, pure resolution-scale math already cross-platform), and calls
//       RequestCustomHintOverlay -- setting suppressRealDraw so the native call-
//       through is skipped for this specific draw. Mantle now ALSO gets real
//       substitution this pass (previously detection-only) via the same path,
//       GameplayHintSlotId::Mantle; Pickup/Throwback share GameplayHintSlotId::
//       Interact, matching x86's own slot assignment exactly (see that file's own
//       Hook_DrawGlyphText, the `GameplayHintSlotId slotId = ...` line).
//   (d) REAL RELOAD/LOW-AMMO GLYPH-ICON SUBSTITUTION (2026-09-13, fourth pass,
//       same day) -- CORRECTS an earlier finding in this same pass (see the git
//       history/known_issues_x64.md issue #1 for the original claim): a prior
//       round of this investigation concluded Reload's text "flows through a
//       COMPLETELY DIFFERENT native draw function, FUN_1402afa60 -- NOT
//       FUN_14029a2b0, the function this hook observes" and is "structurally
//       unreachable from this hook." That was based on decompiling only
//       FUN_1402afa60 ITSELF, which (under this project's `-noanalysis` Ghidra
//       policy, no parameter-ID analysis pass) decompiles misleadingly as a
//       bare `void FUN_1402afa60(void) { FUN_1402b1090(); return; }` -- a real
//       decompiler artifact, not the truth. A full disassembly (`DumpDisasm.java`)
//       shows it actually re-marshals ~12 real incoming stack/register args into
//       a bigger frame and tail-forwards them to FUN_1402b1090 -- and DecompileAt
//       on FUN_1402b1090 shows THAT function is a generic word-wrap/line-layout
//       helper whose inner draw loop calls FUN_14029a2b0 directly (the exact same
//       function this hook already detours) once per wrapped line, via a
//       `param_13 == '\0'` branch (an alternate, extended-signature sibling,
//       FUN_14029a610, exists for `param_13 != 0` but was confirmed DEAD for
//       this purpose -- see below). One more hop of chasing (this project's own
//       "checking is cheaper than digging" / "never trust a correspondence
//       without independent confirmation" standards, CLAUDE.md SS5) would have
//       caught this the first time.
//       **Confirmed FUN_14029a610 is unreachable for every real caller, not just
//       assumed**: FindCallers.java on FUN_1402afa60 lists exactly 6 real
//       callers (killstreak-notify-style messages FUN_140030a70, Reload/low-ammo
//       FUN_140031bc0, vehicle boost/throttle/brake/fire FUN_1400674d0/
//       FUN_140067610, the EXE_KEYCHANGE/KEYWAIT message in FUN_14029c350, and
//       death-quote captions inline in FUN_140052220 case 0x61) -- EVERY single
//       one passes a final argument whose low byte is either a literal `0` or an
//       explicit `& 0xffffffffffffff00`/`& 0xffffff00` mask clearing exactly that
//       byte. That byte is `param_13` by the time it reaches FUN_1402b1090, so
//       all 6 real call sites take the `FUN_14029a2b0` branch unconditionally --
//       FUN_14029a610 is dead code for this whole function's real usage in this
//       build, not a case this hook needs to also cover.
//       Detection: Reload/low-ammo hints have NO "^N...^7" highlight span at all
//       (matching x86's own documented finding for this exact hint -- "the real
//       reload reminder has no ^N...^7 button-name span... it's just a bare
//       flashed/pulsed word"), so TextMatchesTemplateStructurallyX64's marker-
//       based split doesn't apply; a new plain case-insensitive whole-string
//       compare (TextMatchesResolvedExactlyX64) against the live-resolved
//       PLATFORM_RELOAD/MENU_RELOAD_WEAPON templates (g_getLocalizedStringX64,
//       same resolver Mantle/Pickup/Throwback already use) is used instead,
//       mirroring x86's own RenderedTextMatchesReferenceKey exactly (same two
//       reference keys, same case-insensitive full-string match, same
//       language-independence guarantee). On a match, builds "Press "+word
//       exactly like x86's own Reload branch (analog_input_hooks.cpp), resolves
//       the icon via the existing TryGetPickupGlyphAssetName (same ReloadUse
//       physical key x86's own `TryGetGlyphAssetNameForKeyName("F", ...)` call
//       resolves), and requests GameplayHintSlotId::Reload -- x86's own dedicated
//       slot for this hint, already defined in overlay_hud.h, unchanged.
//
// NOT COVERED THIS PASS (honestly scoped, per this task's own explicit
// permission to conclude "partial progress" rather than overclaim):
//   - x64's real Font_s struct layout was only PARTIALLY derived this pass (see
//     FUN_1401b7cd0/FUN_1401b80f0's real dereferences: pixelHeight confirmed at
//     font+0x08, glyphCount confirmed at font+0x0C, DiagGlyph* confirmed at
//     font+0x20 -- DiagGlyph's own 24-byte-stride internal layout is confirmed
//     BYTE-IDENTICAL to x86's, since it's raw loaded font-asset data with no
//     pointers, architecture-independent by construction). fontName's own offset
//     (font+0x00, by natural alignment inference matching x86's exact field order
//     widened for 8-byte pointers -- material/glowMaterial fill the confirmed
//     0x10-byte gap between glyphCount and glyphs, exactly two pointers, exactly
//     like x86) was NOT independently confirmed via decompile (no leaf function
//     found this pass that dereferences it) -- IsGameplayHintFont-style font-name
//     filtering therefore still is NOT implemented or used anywhere in this hook.
//     It was NOT NEEDED for the three cases below either: Mantle/Pickup-family/
//     Throwback are all gated by an exact structural template match against a
//     LIVE-RESOLVED reference-key template (the same protection x86's own
//     Mantle/Throwback/SentryPlace special cases rely on), not by font identity.
//   - Buy-station ("Hold ^3F^7 to use Weapon Armory") and Survival ready-up (F5)
//     remain UNPORTED -- both are x86's OWN generic-bucket cases (no reference-key
//     template of their own was ever found even for x86, per ui_assets.md's own
//     zone-dump research; ready-up's hint text is Survival-script-driven, not in
//     code_post_gfx.str at all), meaning x86 itself protects them from false
//     positives via IsGameplayHintFont + !IsMenuActive(), not a structural match.
//     Porting these safely needs the font-name-filtering gap above closed first --
//     genuinely blocked on real RE, not skipped for convenience.
//
//   UPDATE (2026-09-13, later same day, separate session): a dedicated attempt
//   was made to independently confirm Font_s.fontName's real offset via decompile
//   specifically to unlock these two cases -- real, multi-angle effort (RawStringScan
//   against the real x64 font-name literals, a full trace of the font load/asset-
//   cache chain, and an audit of every function confirmed to dereference the actual
//   Font_s* PAYLOAD pointer), and it could NOT be confirmed. The load chain
//   (FUN_14029b640 -> thunk_FUN_1401b7cb0 -> FUN_1400a5a20/FUN_1400a54c0) turned out
//   to be a generic, type-agnostic asset-cache system where the CACHE ENTRY (a
//   separate allocation from the payload callers actually receive) tracks its own
//   name via an indirected get/set pair, not a fact about the payload's own layout;
//   the one path that touches real struct bytes for a "default" font is a raw
//   memcpy of an opaque template blob, not a per-field constructor a static trace
//   can see inside. Every confirmed payload consumer (the pixelHeight/glyphCount/
//   glyphs getters) was audited and none dereferences offset +0x00. This is a real
//   negative result, not an unattempted gap -- see re_notes/x64_migration/
//   drawtext_hook_x64.md's "Stage (d)" and known_issues_x64.md's matching
//   2026-09-13 update for the full trail. Per this project's own "no unconfirmed-
//   offset OOB read" standard (CLAUDE.md SS5), NO fontName-gated substitution was
//   wired -- buy-station and Survival ready-up remain unported.
//   - Reload/low-ammo IS NOW COVERED (see stage (d) above) -- the claim
//     previously here ("calls a COMPLETELY DIFFERENT native draw function...
//     this hook can never see Reload's text") was WRONG, root-caused and
//     corrected the same day (see stage (d)'s own comment for the full trail).
//     Kept as a visible correction rather than silently deleted, per this
//     project's own documentation standard of preserving investigation history.
//   - Sentry-Place (turret placement, SENTRY_PLACE) -- RawStringScan against
//     "SENTRY_PLACE" found ZERO references anywhere in this x64 binary (unlike
//     Pickup/Throwback/Reload's keys, all found on the first try). Genuinely
//     unresolved: either this specific string is stored differently on x64, this
//     hint doesn't exist in this build, or it needs a different anchor string --
//     not pursued further this pass; TryGetSentryPlaceGlyphAssetName exists and is
//     ready to use the moment a real x64 reference/template is found.
//   - Menu corner hints (Back/Friends/Quit/Leaderboards/Game-Summary) ARE NOW
//     COVERED (see this hook's own "Menu corner hints" block, right before the
//     final real-draw call below) -- the claim previously here ("was not
//     ported -- deliberately out of scope") no longer holds for any of these
//     five. Kept as a visible correction rather than silently deleted, per
//     this project's own documentation standard of preserving investigation
//     history. **Corrected again, 2026-09-13 (menu-hint parity follow-up)**:
//     an intermediate version of this comment (Back/Friends-only pass) claimed
//     Quit/Leaderboards/Game-Summary and the Special-Ops/Friends-suppression
//     logic depended on "x86-only menu-focus/itemDef infrastructure not yet
//     ported to x64" -- that was RE-CHECKED against x86's own originals and
//     found stale (the itemDef walk this logic actually needs was already
//     ported for a DIFFERENT consumer, the same day, by the A-glyph/F2-F3
//     fix) -- see this block's own header comment, right before the "Menu
//     corner hints" block below, for the full corrected reasoning. The one
//     thing genuinely still unavailable is x64's own `Font_s.fontName` offset
//     (buy-station/Survival ready-up/Sentry-Place remain absent for that
//     reason, unrelated to menu corner hints).
using DrawTextFnX64 = void(*)(
    unsigned __int64 dcHandle, const char* text, int maxChars, void* fontArg,
    float x, float y, unsigned color1, unsigned color2, float scale,
    const void* colorVecPtr, unsigned extra);
DrawTextFnX64 g_realDrawTextX64 = nullptr;

// FUN_14029a2b0 -- see DumpSigBytes.java output (re_notes/x64_migration/
// sig_draw_text_14029a2b0.txt). DumpSigBytes.java's own reference-based heuristic
// flagged every RSP-relative MOV/MOVSS/MOVAPS/LEA in this prologue as needing
// wildcarding -- the SAME documented false positive already recorded above this
// file's kPmoveTickSignature (RSP-relative displacements are fixed stack offsets,
// never addresses that shift between builds) -- hand-corrected: the ONLY genuine
// PC-relative instruction in this span is the `CALL 0x1401b7c90` at +0x31, whose
// 4-byte rel32 displacement is wildcarded below; every other byte is kept literal.
constexpr const char* kDrawTextSignature =
    "48 89 5C 24 08 48 89 6C 24 10 48 89 74 24 18 57 48 83 EC 70 "
    "F3 0F 10 8C 24 C0 00 00 00 48 8B D9 49 8B C9 0F 29 7C 24 60 "
    "49 8B F9 41 8B F0 48 8B EA E8 ?? ?? ?? ?? 8B 84 24 B8 00 00 00 "
    "4C 8D 4C 24 54 89 44 24 30";

// FUN_14029f120 -- the real x64 SEH_GetString/GetLocalizedString equivalent (see
// this section's own header comment for the full behavioral confirmation).
// Resolved for a DIRECT CALL, same pattern as g_weaponNext/g_actionSlot/
// g_menuKeyEventX64 below -- no hook installed, this project never modifies how
// the engine resolves its own localized strings, it only reads the result.
// re_notes/x64_migration/sig_getlocalizedstring_14029f120.txt: two short (rel8)
// conditional jumps, one CALL rel32, and one RIP-relative LEA are the genuine
// PC-relative sites here (this function is small enough that DumpSigBytes.java's
// heuristic got every one of these right, unlike the RSP-relative false positives
// above) -- each wildcarded at its own displacement bytes only.
// FUN_14029f120 -- the real x64 SEH_GetString/GetLocalizedString equivalent (see
// this section's own header comment for the full behavioral confirmation).
// Resolved for a DIRECT CALL, same pattern as g_weaponNext/g_actionSlot/
// g_menuKeyEventX64 elsewhere in this file -- no hook installed, this project
// never modifies how the engine resolves its own localized strings, it only
// reads the result. re_notes/x64_migration/impl_sig_14029f120.txt: two short
// (rel8) conditional jumps, one CALL rel32, and one RIP-relative LEA are the
// genuine PC-relative sites here (this function is small enough that
// DumpSigBytes.java's heuristic got every one of these right, unlike the
// RSP-relative false positives on kDrawTextSignature above) -- each wildcarded
// at its own displacement bytes only.
using GetLocalizedStringFnX64 = const char*(*)(const char*);
GetLocalizedStringFnX64 g_getLocalizedStringX64 = nullptr;
constexpr const char* kGetLocalizedStringSignature =
    "40 53 48 83 EC 20 80 39 15 48 8B D9 75 ?? 48 FF C3 EB ?? "
    "E8 ?? ?? ?? ?? 48 85 C0 75 ?? 48 8D 0D ?? ?? ?? ?? 48 2B CB";

long long g_drawTextFireCount = 0;

// Mirrors x86's g_mantleHintDrawnThisFrame/g_mantleHintLastSeenMs pair
// (analog_input_hooks.cpp) collapsed to a single timestamp -- see this section's
// header comment for why the per-frame accumulate-then-commit step isn't needed
// here. Auto-Mantle's own +gostand-forcing feature is a SEPARATE, not-yet-ported
// piece (re_notes/known_issues_x64.md) -- this is only the detection signal it
// would consume.
DWORD g_mantleHintLastSeenMsX64 = 0;
constexpr DWORD kMantleHintGraceMsX64 = 400; // matches x86's kMantleHintGraceMs exactly
extern "C" bool IsMantleHintCurrentlyShowingX64()
{
    return (GetTickCount() - g_mantleHintLastSeenMsX64) <= kMantleHintGraceMsX64;
}

// x64-local reimplementation of x86's RenderedTextMatchesSubstitutionTemplateWithMarker
// (analog_input_hooks.cpp) -- identical algorithm (position-based, not content-based,
// so it stays correct for any substituted key length in any language), duplicated
// rather than cross-file-shared: the x86 original lives in that file's own giant
// anonymous namespace (internal linkage) and calls GetLocalizedString(), whose x64
// build is a deliberate stub (real_settings.cpp: "#if defined(_M_X64)... return
// referenceKey") since nothing on x64 had a REAL template resolver before this pass.
// Callers here pass the REAL resolved template (via g_getLocalizedStringX64) directly
// instead of a reference key, so this stays a genuine, language-independent
// structural match, not an English-only shortcut.
bool TextMatchesTemplateStructurallyX64(const char* renderedText, const char* tmpl, const char* marker)
{
    if (!renderedText || !tmpl || !marker) return false;
    const char* markerPos = strstr(tmpl, marker);
    if (!markerPos) return false;
    size_t markerLen = strlen(marker);
    size_t prefixLen = static_cast<size_t>(markerPos - tmpl);
    size_t suffixLen = strlen(markerPos + markerLen);
    size_t renderedLen = strlen(renderedText);
    if (renderedLen < prefixLen + suffixLen) return false;
    if (strncmp(renderedText, tmpl, prefixLen) != 0) return false;
    if (suffixLen > 0 && strcmp(renderedText + (renderedLen - suffixLen), markerPos + markerLen) != 0) return false;
    return true;
}

// x64-local reimplementation of x86's RenderedTextMatchesReferenceKey
// (analog_input_hooks.cpp) -- a plain, case-insensitive WHOLE-STRING compare
// against a live-resolved template, no "&&N"/"^N...^7" marker involved at all.
// Needed for Reload/low-ammo (stage (d), see this file's own header comment):
// unlike Mantle/Pickup/Throwback, that hint's real resolved text is a bare word
// with no embedded highlight span ("the real reload reminder has no ^N...^7
// button-name span at all -- it's just a bare flashed/pulsed word", x86's own
// documented finding for this exact hint), so TextMatchesTemplateStructurallyX64's
// marker-based prefix/suffix split doesn't apply -- this is the direct x64
// counterpart instead. `resolvedTmpl` must already be the LIVE-RESOLVED string
// (via g_getLocalizedStringX64), same calling convention as every other
// structural-match helper in this file, not a raw reference key.
bool TextMatchesResolvedExactlyX64(const char* renderedText, const char* resolvedTmpl)
{
    if (!renderedText || !resolvedTmpl) return false;
    return _stricmp(renderedText, resolvedTmpl) == 0;
}

// x64-local reimplementation of x86's RenderedTextMatchesReferenceKeyPrefix
// (analog_input_hooks.cpp) -- needed for Leaderboards (2026-09-13 menu-hint parity
// follow-up), whose real resolved template has TWO embedded "^N...^7" spans
// ("Leaderboards ^2Right Mouse^7/^2F1^7"), so a full-string match never applies --
// compares only up to the template's own first '^' marker (or full length if none),
// same reasoning as x86's own comment: stays correct even if the embedded bind text
// differs by language/layout. `resolvedTmpl` must already be the LIVE-RESOLVED
// string (via g_getLocalizedStringX64), matching this file's own calling convention.
bool TextMatchesResolvedPrefixX64(const char* renderedText, const char* resolvedTmpl)
{
    if (!renderedText || !resolvedTmpl) return false;
    const char* caret = strchr(resolvedTmpl, '^');
    size_t prefixLen = caret ? static_cast<size_t>(caret - resolvedTmpl) : strlen(resolvedTmpl);
    if (prefixLen == 0) return false;
    return _strnicmp(renderedText, resolvedTmpl, prefixLen) == 0;
}

// x64-local reimplementation of x86's ColorHighlightSpan/FindColorHighlightSpan
// (analog_input_hooks.cpp) -- same internal-linkage reason TextMatchesTemplateStructurallyX64
// above is duplicated rather than cross-file-shared (the x86 original lives in that
// file's own giant anonymous namespace). Byte-for-byte the same algorithm: finds the
// engine's own "^N...^7" color-highlight marker pair and returns the byte range of
// its CONTENT plus the byte range of the WHOLE marker run (both tokens included),
// so a caller can split the surrounding text into prefix/suffix once the highlighted
// content is being replaced by a real icon instead of drawn as text.
struct ColorHighlightSpanX64 { size_t contentStart; size_t contentLen; size_t markerStart; size_t markerEnd; bool found; };

ColorHighlightSpanX64 FindColorHighlightSpanX64(const char* text, size_t textLen)
{
    for (size_t i = 0; i + 1 < textLen; ++i) {
        if (text[i] == '^' && text[i + 1] >= '0' && text[i + 1] <= '9') {
            size_t contentStart = i + 2;
            for (size_t j = contentStart; j + 1 < textLen; ++j) {
                if (text[j] == '^' && text[j + 1] >= '0' && text[j + 1] <= '9') {
                    return { contentStart, j - contentStart, i, j + 2, true };
                }
            }
            break; // opening marker with no closing marker -- don't guess an end
        }
    }
    return { 0, 0, 0, 0, false };
}

// x64-local reimplementation of x86's ConvertRealScreenPosToDesignSpace
// (analog_input_hooks.cpp) -- confirmed via a real LNK2019 to have internal linkage
// there too (a second anonymous namespace, not the one immediately above its
// definition -- see this section's own header comment). Identical logic: divides a
// REAL screen-space pixel position by the actual current resolution scale so
// RequestCustomHintOverlay's consumer (DrawOneGameplayHintSlot, overlay_hud.cpp),
// which multiplies by that same scale exactly once, doesn't double-scale an
// already-real position -- see x86's own copy for the full original rationale.
void ConvertRealScreenPosToDesignSpaceX64(float realX, float realY, float& outDesignX, float& outDesignY)
{
    float scaleX = 1.0f, scaleY = 1.0f;
    GetResolutionScale(GetLastKnownRenderDevice(), scaleX, scaleY);
    outDesignX = (scaleX > 0.0001f) ? (realX / scaleX) : realX;
    outDesignY = (scaleY > 0.0001f) ? (realY / scaleY) : realY;
}

void Hook_DrawTextX64(
    unsigned __int64 dcHandle, const char* text, int maxChars, void* fontArg,
    float x, float y, unsigned color1, unsigned color2, float scale,
    const void* colorVecPtr, unsigned extra)
{
    ++g_drawTextFireCount;
    if (g_drawTextFireCount <= 5 || (g_drawTextFireCount % 5000) == 0) {
        char buf[128];
        sprintf_s(buf, "[x64-drawtext] Text-draw hook fired (count=%lld)", g_drawTextFireCount);
        LogFromController(buf);
    }

    // Real glyph-icon substitution -- see this section's own header comment (part
    // (c), 2026-09-13) for the full design/scope. Gated exactly like x86's own
    // block (ShouldDrawGlyphOverlay() && !IsMenuActive()) so this carries the same
    // real coupling to the glyph-overlay toggle, not a new behavior.
    bool suppressRealDraw = false;
    if (g_getLocalizedStringX64 && text && LooksSaneX64(reinterpret_cast<uintptr_t>(text)) &&
        ShouldDrawGlyphOverlay_Exported() && !IsMenuActiveX64_Exported()) {
        __try {
            const char* mantleTmpl = g_getLocalizedStringX64("PLATFORM_MANTLE");
            bool isMantleHint = mantleTmpl && LooksSaneX64(reinterpret_cast<uintptr_t>(mantleTmpl)) &&
                TextMatchesTemplateStructurallyX64(text, mantleTmpl, "&&1");
            if (isMantleHint) {
                // Auto-Mantle's own detection dependency (IsMantleHintCurrentlyShowingX64) --
                // unchanged from the 2026-09-13 first pass, kept independent of whether the
                // visual substitution below also succeeds (same reasoning x86's own
                // g_mantleHintDrawnThisFrame assignment documents: the ledge-availability
                // SIGNAL must not be coupled to whether this project's OWN icon lookup
                // happens to succeed this frame).
                g_mantleHintLastSeenMsX64 = GetTickCount();
                static bool s_loggedFirstMantleMatch = false;
                if (!s_loggedFirstMantleMatch) {
                    s_loggedFirstMantleMatch = true;
                    LogFromController("[x64-drawtext] Mantle-hint structural match confirmed (real "
                        "PLATFORM_MANTLE template matched against live rendered text) -- "
                        "IsMantleHintCurrentlyShowingX64() will now return true while this hint keeps "
                        "showing.");
                }
            }

            // Pickup/swap/pickup-health family (all real "+activate" binds, all confirmed
            // via DecompileAt to flow through this exact draw call -- see header comment).
            const char* pickupTmpl = g_getLocalizedStringX64("PLATFORM_PICKUPNEWWEAPON");
            const char* swapTmpl = g_getLocalizedStringX64("PLATFORM_SWAPWEAPONS");
            const char* healthTmpl = g_getLocalizedStringX64("PLATFORM_PICKUPHEALTH");
            bool isPickupHint =
                (pickupTmpl && LooksSaneX64(reinterpret_cast<uintptr_t>(pickupTmpl)) &&
                 TextMatchesTemplateStructurallyX64(text, pickupTmpl, "&&1")) ||
                (swapTmpl && LooksSaneX64(reinterpret_cast<uintptr_t>(swapTmpl)) &&
                 TextMatchesTemplateStructurallyX64(text, swapTmpl, "&&1")) ||
                (healthTmpl && LooksSaneX64(reinterpret_cast<uintptr_t>(healthTmpl)) &&
                 TextMatchesTemplateStructurallyX64(text, healthTmpl, "&&1"));

            const char* throwbackTmpl = g_getLocalizedStringX64("PLATFORM_THROWBACKGRENADE");
            bool isThrowbackHint = throwbackTmpl && LooksSaneX64(reinterpret_cast<uintptr_t>(throwbackTmpl)) &&
                TextMatchesTemplateStructurallyX64(text, throwbackTmpl, "&&1");

            if (isMantleHint || isPickupHint || isThrowbackHint) {
                size_t textLen = strlen(text);
                ColorHighlightSpanX64 span = FindColorHighlightSpanX64(text, textLen);
                if (span.found) {
                    char assetName[32] = {};
                    bool haveAssetName = isMantleHint ? TryGetMantleGlyphAssetName(assetName, sizeof(assetName))
                        : isThrowbackHint ? TryGetThrowbackGlyphAssetName(assetName, sizeof(assetName))
                        : TryGetPickupGlyphAssetName(assetName, sizeof(assetName));
                    if (haveAssetName) {
                        char prefixText[128] = {};
                        size_t prefixLen = span.markerStart < sizeof(prefixText) - 1 ? span.markerStart : sizeof(prefixText) - 1;
                        memcpy(prefixText, text, prefixLen);
                        prefixText[prefixLen] = '\0';

                        char suffixText[128] = {};
                        if (span.markerEnd < textLen) {
                            size_t suffixLen = textLen - span.markerEnd;
                            if (suffixLen >= sizeof(suffixText)) suffixLen = sizeof(suffixText) - 1;
                            memcpy(suffixText, text + span.markerEnd, suffixLen);
                            suffixText[suffixLen] = '\0';
                        }

                        // x/y here are THIS call's own already-computed real screen-pixel
                        // position (confirmed via decompile: e.g. Mantle's own
                        // `fVar16 = local_518 - (...)`, Pickup's own `fVar19`/`fVar17*fVar18+
                        // param_2[1]` -- both real final draw-call coordinates, not raw
                        // pre-layout input), same convention as x86's own param_2/param_3 at
                        // its equivalent hook site. Converted to design-space so
                        // DrawOneGameplayHintSlot's own single scaleX/scaleY multiply lands
                        // correctly at any resolution/aspect ratio, exactly like x86.
                        //
                        // HONEST CAVEAT: unlike x86 (which reached its exact pixel alignment
                        // via multiple live-tested rounds of empirical nudge constants --
                        // kHintVerticalNudge, kMantleHintXNudge/YNudge -- see that file's own
                        // history), NO equivalent nudge has been derived or applied here. This
                        // is the raw converted position with zero tuning; on-screen alignment
                        // against the real mantle-arrow sprite/pickup icon has NOT been live-
                        // verified and may need the same kind of empirical correction x86
                        // required once this is actually seen running.
                        float startX = x, startY = y;
                        ConvertRealScreenPosToDesignSpaceX64(startX, startY, startX, startY);

                        GameplayHintSlotId slotId = isMantleHint ? GameplayHintSlotId::Mantle
                                                                   : GameplayHintSlotId::Interact;
                        // Condensed role for Throwback, matching x86's own
                        // `(isThrowbackHint || isSentryPlaceHint) ? FontRole::Condensed :
                        // FontRole::Default` exactly (analog_input_hooks.cpp).
                        FontRole fontRole = isThrowbackHint ? FontRole::Condensed : FontRole::Default;
                        RequestCustomHintOverlay(startX, startY, prefixText, suffixText, assetName,
                                                   /*centerOnScreen=*/!isMantleHint, /*flashIcon=*/false, slotId,
                                                   /*topLineText=*/"", fontRole);
                        suppressRealDraw = true;

                        static bool s_loggedFirstSubstitution = false;
                        if (!s_loggedFirstSubstitution) {
                            s_loggedFirstSubstitution = true;
                            char subBuf[192];
                            sprintf_s(subBuf, "[x64-drawtext] First real glyph-icon SUBSTITUTION fired -- "
                                "kind=%s asset=%s (native hint text suppressed, our own icon+text drawn "
                                "instead)", isMantleHint ? "Mantle" : isThrowbackHint ? "Throwback" : "Pickup",
                                assetName);
                            LogFromController(subBuf);
                        }
                    }
                }
            }

            // Reload/low-ammo hint (stage (d), see this file's own header comment for
            // the full RE trail of how this call site was confirmed reachable from
            // this hook after all). Gated by the SAME `ShouldDrawGlyphOverlay_Exported()
            // && !IsMenuActiveX64_Exported()` condition as Mantle/Pickup/Throwback
            // above -- matches x86's own placement exactly (its Reload branch lives
            // inside the SAME `ShouldDrawGlyphOverlay() && !IsMenuActive()` gameplay-
            // hint block as its Mantle/Pickup handling, analog_input_hooks.cpp ~line
            // 8209 -- NOT the separate menu-hint block a few hundred lines later,
            // which is deliberately NOT gated on !IsMenuActive() for a reason specific
            // to actual menu hints that doesn't apply to a gameplay-only prompt like
            // Reload). Matches x86's own two candidate reference keys exactly
            // ("MENU_RELOAD_WEAPON"/"PLATFORM_RELOAD", zone_dump-confirmed to both
            // resolve to English "Reload" -- checking both since it's not confirmed
            // which one this specific HUD hint actually uses, same as x86). `text`
            // here IS already the bare resolved word (FUN_140289a60's own return
            // value, copied verbatim into FUN_1402b1090's line buffer with no
            // "&&N"/"^N...^7" markup at all -- confirmed via decompile), so a plain
            // exact compare against the live-resolved template is correct, not a
            // structural prefix/suffix match.
            const char* reloadTmpl1 = g_getLocalizedStringX64("PLATFORM_RELOAD");
            const char* reloadTmpl2 = g_getLocalizedStringX64("MENU_RELOAD_WEAPON");
            bool isReloadHint =
                (reloadTmpl1 && LooksSaneX64(reinterpret_cast<uintptr_t>(reloadTmpl1)) &&
                 TextMatchesResolvedExactlyX64(text, reloadTmpl1)) ||
                (reloadTmpl2 && LooksSaneX64(reinterpret_cast<uintptr_t>(reloadTmpl2)) &&
                 TextMatchesResolvedExactlyX64(text, reloadTmpl2));

            if (isReloadHint) {
                char assetName[32] = {};
                // Same physical key x86's own Reload branch resolves
                // (TryGetGlyphAssetNameForKeyName("F", ...), ReloadUse's real default
                // bind) -- reused here via the already-existing TryGetPickupGlyphAssetName
                // (ported this same pass for the Pickup/Swap/PickupHealth family, resolves
                // through the identical LogicalAction::ReloadUse).
                if (TryGetPickupGlyphAssetName(assetName, sizeof(assetName))) {
                    // x86's own template: "Press "+word (its own added text, the real
                    // string has neither) -- see analog_input_hooks.cpp's own Reload
                    // branch for the identical construction.
                    char suffixText[48] = {};
                    // 2026-09-13 safety fix: `text` is the raw, live-resolved native hint
                    // string -- genuinely unbounded from this code's own point of view (not
                    // a fixed internal identifier like assetName/kind elsewhere in this
                    // file). An untruncated %s here risks the same sprintf_s UCRT fail-fast
                    // crash class already confirmed live (iw5sp.exe.14364.dmp, 2026-09-13) --
                    // truncated per this project's own established convention for unbounded
                    // strings (see analog_input_hooks.cpp's %.Ns sites, e.g. line 4231/4323).
                    sprintf_s(suffixText, " To %.43s", text);

                    // x/y here are this call's own already-computed final draw position
                    // (post word-wrap/alignment, inside FUN_1402b1090) -- same convention
                    // as Mantle/Pickup/Throwback above. HONEST CAVEAT: x86's own Reload
                    // branch deliberately does NOT use its call's raw position (its own
                    // comment: the real p3 "rendered noticeably above the weapon... anchored
                    // directly to the same known-good target pickup/buy-station's own formula
                    // resolves to" instead, via a live-tuned constant) -- no equivalent x64
                    // live-tuning has been done here, so this uses the raw converted real
                    // position with zero empirical correction, same as this hook's other
                    // three substituted hints. On-screen alignment is UNVERIFIED and may need
                    // the same class of tuning x86 required once actually seen running.
                    float startX = x, startY = y;
                    ConvertRealScreenPosToDesignSpaceX64(startX, startY, startX, startY);

                    RequestCustomHintOverlay(startX, startY, "Press ", suffixText, assetName,
                                               /*centerOnScreen=*/true, /*flashIcon=*/true,
                                               GameplayHintSlotId::Reload);
                    suppressRealDraw = true;

                    static bool s_loggedFirstReloadMatch = false;
                    if (!s_loggedFirstReloadMatch) {
                        s_loggedFirstReloadMatch = true;
                        char subBuf[192];
                        // 2026-09-13 safety fix: same unbounded-`text` risk as suffixText
                        // above (this is a diagnostic-only log line, not gameplay-visible,
                        // but the crash it could cause is real) -- truncated per this
                        // project's established %.Ns convention for unbounded strings.
                        sprintf_s(subBuf, "[x64-drawtext] First real Reload glyph-icon SUBSTITUTION "
                            "fired (structural match against live PLATFORM_RELOAD/MENU_RELOAD_WEAPON "
                            "template, text=\"%.40s\")", text);
                        LogFromController(subBuf);
                    }
                }
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }
    }

    // ---- Menu corner hints (x86's "Back ^2ESC^7"/"Friends ^2F^7"), x64 port
    // (2026-09-13). Read x86's own Hook_DrawGlyphText menu-hint block in full first
    // (analog_input_hooks.cpp, ~line 8689 at the time of this port) per this
    // project's own compare-to-x86-original rule -- summary of what carries over
    // and what genuinely differs on x64:
    //
    // - REFERENCE KEYS ARE FIXED STRINGS, NOT "&&1" SUBSTITUTION TEMPLATES. Unlike
    //   Mantle/Pickup/Throwback above (each a template with a substituted key-name
    //   marker), x86's own comment on PLATFORM_FRIENDS_SHORTCUT/BACK_SHORTCUT is
    //   explicit: these are "real, fixed reference-key strings ... with the
    //   accelerator letter baked directly into the template -- NOT run through the
    //   &&1 substitution." So this block does a plain exact-string compare against
    //   the live-resolved template (strcmp), not TextMatchesTemplateStructurallyX64
    //   (which requires an "&&1" marker inside the template to even attempt a match
    //   -- it would silently reject both of these forever, since neither template
    //   contains one).
    // - RawStringScan.java confirmed ZERO references to the literal strings
    //   "PLATFORM_BACK_SHORTCUT"/"PLATFORM_FRIENDS_SHORTCUT" anywhere in this x64
    //   binary's own image -- NOT a sign the reference keys don't exist on x64: per
    //   x86's own line 3764 comment, these keys are itemDef "text:" properties
    //   defined in .menu UI ASSET files (popmenu_specops_survival.menu etc.),
    //   parsed from FastFiles at runtime, never baked into the executable's own
    //   code the way Mantle/Pickup's dispatcher-embedded literals are (which IS why
    //   RawStringScan could anchor on those). The localization TABLE these keys
    //   resolve against also lives in FastFile-loaded data, unaffected by the
    //   x86->x64 recompile (same game content, only the exe recompiled) -- so
    //   g_getLocalizedStringX64("PLATFORM_BACK_SHORTCUT") is expected to resolve
    //   correctly at runtime despite the negative RawStringScan result, same as
    //   x86's own zone_dump-verified (not exe-verified) confirmation for these two
    //   keys. Not independently re-verified against a live zone_dump this pass --
    //   flagged honestly below.
    // - CONFIRMED (not assumed) TO FLOW THROUGH THIS SAME HOOK: per this project's
    //   own issue #3 lesson (never trust a correspondence without independent
    //   confirmation), traced the real call chain via decompile rather than
    //   assuming x86's "same Hook_DrawGlyphText call site as gameplay hints" claim
    //   carries over unverified: FUN_1402a9950 (a generic itemDef-paint function,
    //   reads itemDef-struct-shaped fields directly off param_2 at offsets like
    //   +0xd0/+0xe0/+0xd8/+0x1f0) calls FUN_1402b1090 (a generic, color-code-aware,
    //   word-wrapping text painter -- confirmed via decompile to carry the exact
    //   Quake3-lineage "^N...^7" color-code-carry-across-wrap-boundary logic every
    //   corner hint's own highlight span depends on), which itself calls
    //   FUN_14029a2b0 directly (`FUN_14029a2b0(param_1,local_4d8,0x7fffffff,param_4,
    //   fVar14,param_6,...)`) -- the exact function this hook already detours.
    //   Confirmed via FindCallers.java against both 14029a2b0 and 1402b1090
    //   (re_notes/ghidra_project_x64/iw5sp_x64_proj, analyzeHeadless -readOnly
    //   -noanalysis). Short, single-line corner-hint text (no wrap needed) reaches
    //   FUN_14029a2b0 as a verbatim copy of the full resolved string, same as every
    //   other case this hook already handles.
    // - GATING DELIBERATELY DIFFERENT FROM THE BLOCK ABOVE. x86's own comment is
    //   explicit: menu hints are "Deliberately NOT gated on !IsMenuActive() (unlike
    //   the gameplay block above) -- these hints only ever draw WHILE a menu is
    //   active, so suppressing them in that state would suppress them entirely."
    //   Mirrored exactly here -- this is its own top-level `if`, gated only on
    //   ShouldDrawGlyphOverlay_Exported(), NOT ANDed with !IsMenuActiveX64_Exported()
    //   the way the gameplay-hint block above is. Putting this logic inside that
    //   block instead would silently never fire, since corner hints only ever draw
    //   while IsMenuActiveX64_Exported() is true.
    // - POSITION: x86 uses the RAW, unscaled param_3 (y) directly plus a fixed
    //   kMenuHintVerticalNudge (-18.0f) -- explicitly NOT the "param_3 * param_6"
    //   vertical-center formula the gameplay-hint block above uses, per x86's own
    //   comment: "That formula does not transfer to fonts/smallFont; use the raw,
    //   unscaled param_3 instead." x64's own `y` parameter here is already the
    //   equivalent raw value (Hook_DrawTextX64's own named y param, mapping to
    //   x86's param_3 the same way x86's param_2/param_6 map to this function's
    //   named x/scale params) -- used directly below, not multiplied by `scale`.
    // - ICON RESOLUTION: reuses TryGetMenuGlyphAssetNameForKeyName verbatim (x86
    //   function, confirmed external linkage, see this file's own forward
    //   declaration above) with "ESC"/"F" -- the SAME menu-specific bind vocabulary
    //   x86 uses, resolving to the real B/Y physical buttons via
    //   ResolveMenuGlyphAssetNameForKeyName's own dedicated table, NOT the
    //   LogicalAction-based shortcut TryGetMantleGlyphAssetName/TryGetPickupGlyphAssetName
    //   use above -- that shortcut exists purely to dodge a translated-substituted-
    //   text risk that doesn't apply here (nothing is substituted into these two
    //   templates at all), so there's no reason to deviate from x86's own resolution
    //   path, and doing so keeps the real menu-specific ESC->B/F->Y mapping intact.
    // - DRAW CALL: RequestMenuHintOverlay (the multi-slot pool), NOT
    //   RequestCustomHintOverlay -- x86's own comment explains why: MW3's menu UI
    //   can show multiple corner hints (Back AND Friends) simultaneously every
    //   frame, unlike a single gameplay interact prompt. Already wired into x64's
    //   own EndScene draw pass (DrawMenuHintsIfRequested, overlay_hud.cpp -- no
    //   platform guard, already audited landmine-free for x64 per
    //   known_issues_x64.md) -- this is the first call site that actually POPULATES
    //   a slot for it on x64, the draw/consume side was already live and idle.
    //
    // UPDATED 2026-09-13 (menu-hint parity follow-up): Quit/Leaderboards/Game-Summary
    // and the Friends-suppression logic below, previously listed as "NOT PORTED THIS
    // PASS" citing "x86-only menu-focus/itemDef-position infrastructure not yet
    // ported to x64" -- that claim was RE-CHECKED and found stale/wrong. The real
    // data each one needs, checked against x86's own originals in full first (per
    // this project's own compare-to-x86-original rule):
    //  - looksLikeCornerHintRow (x86) is NOT itemDef/focus data at all -- it reads
    //    only this draw call's own raw `y` parameter (x86's param_3), the exact same
    //    input this hook already has. Ported directly below as looksLikeCornerHintRowX64.
    //  - Quit/Leaderboards/Game-Summary's own literal-text matches are plain
    //    resolved-template string compares (same class as Back/Friends, already
    //    live on x64) -- no itemDef dependency either.
    //  - IsInsideSpecOpsNestedModal()/IsFriendsListOpen() (x86) key off the
    //    CURRENTLY FOCUSED ITEM'S RAW NAME, not the (group,index,siblingCount,depth)
    //    tuple TryGetRealFocusedGroupAndIndexX64 exposes -- that function
    //    deliberately returns false for names like "Chaos"/"friendList"/"none" that
    //    don't parse as "<group>_<index>" (exactly the names this logic needs). A
    //    small, confident extension of the SAME already-working x64 itemDef walk
    //    (TryGetRealFocusedItemNameX64, added this pass, same offsets already
    //    validated by the 2026-09-12 A-glyph/F2-F3 fix) closes this gap with no new
    //    RE. See that function's own header comment for the full reasoning.
    // The one thing genuinely still NOT available is x64's own Font_s.fontName
    // offset (fonts/smallFont family filtering, x86's IsMenuHintFont) -- not needed
    // here: x86 itself relies on looksLikeCornerHintRow, not font filtering, as the
    // real discriminator that stops Quit from hijacking a genuine navigable "Quit"
    // list item elsewhere on screen (see BUG-006 in analog_input_hooks.cpp) --
    // ported below for exactly that reason, not skipped.
    if (g_getLocalizedStringX64 && text && LooksSaneX64(reinterpret_cast<uintptr_t>(text)) &&
        !suppressRealDraw && ShouldDrawGlyphOverlay_Exported()) {
        __try {
            // Corner-hint-row positional tolerance (2026-09-13 port of x86's
            // looksLikeCornerHintRow, analog_input_hooks.cpp ~line 8765). Pure
            // position check on this call's own raw `y` -- no itemDef/focus
            // dependency. kStandardCornerHintYX64/kCornerHintRowTolerancePxX64 match
            // x86's own kStandardCornerHintY(995.0f)/kCornerHintRowTolerancePx(40.0f)
            // exactly (see x86's own declaration comment for why 995 is usable
            // directly as a design-space reference).
            constexpr float kStandardCornerHintYX64 = 995.0f;
            constexpr float kCornerHintRowTolerancePxX64 = 40.0f;
            constexpr float kMenuHintVerticalNudgeX64 = -18.0f;
            float unusedDesignX = 0.0f, designRowY = 0.0f;
            ConvertRealScreenPosToDesignSpaceX64(0.0f, y, unusedDesignX, designRowY);
            bool looksLikeCornerHintRowX64 = fabsf(designRowY - kStandardCornerHintYX64) < kCornerHintRowTolerancePxX64;

            // Quit (2026-09-13 port of x86's MENU_QUIT literal-text case,
            // analog_input_hooks.cpp ~line 8773). Case-SENSITIVE exact match, same as
            // x86, to keep excluding the Special Ops hub's own separate all-caps
            // "QUIT" item. Gated on looksLikeCornerHintRowX64 -- BUG-006's own real
            // precedent (x86, 2026-08-02): a bare content match alone once hijacked a
            // genuine navigable "Leaderboards"/"Quit" menu list item sharing the same
            // label; position is the fix, not font family.
            const char* quitTmpl = g_getLocalizedStringX64("MENU_QUIT");
            bool isQuitCornerHint = looksLikeCornerHintRowX64 && quitTmpl &&
                LooksSaneX64(reinterpret_cast<uintptr_t>(quitTmpl)) && strcmp(text, quitTmpl) == 0;
            if (isQuitCornerHint) {
                char bAsset[32] = {};
                if (TryGetMenuGlyphAssetNameForKeyName("ESC", bAsset, sizeof(bAsset))) {
                    float designX = 0.0f, designY = 0.0f;
                    ConvertRealScreenPosToDesignSpaceX64(x, y + kMenuHintVerticalNudgeX64, designX, designY);
                    RequestMenuHintOverlay(designX, designY, "Quit", "", bAsset);
                    suppressRealDraw = true;
                }
            }

            // Leaderboards (2026-09-13 port of x86's PLATFORM_LEADERBOARDS_SHORTCUT
            // prefix case, analog_input_hooks.cpp ~line 8800). "F1" resolves to
            // PhysicalInput::Back (the real Back/Select/View button, distinct from
            // ESC's B), same as x86.
            const char* leaderboardsTmpl = g_getLocalizedStringX64("PLATFORM_LEADERBOARDS_SHORTCUT");
            bool isLeaderboardsCornerHint = looksLikeCornerHintRowX64 && leaderboardsTmpl &&
                LooksSaneX64(reinterpret_cast<uintptr_t>(leaderboardsTmpl)) &&
                TextMatchesResolvedPrefixX64(text, leaderboardsTmpl);
            if (isLeaderboardsCornerHint) {
                char backAsset[32] = {};
                if (TryGetMenuGlyphAssetNameForKeyName("F1", backAsset, sizeof(backAsset))) {
                    float designX = 0.0f, designY = 0.0f;
                    ConvertRealScreenPosToDesignSpaceX64(x, y + kMenuHintVerticalNudgeX64, designX, designY);
                    RequestMenuHintOverlay(designX, designY, "Leaderboards ", "", backAsset);
                    suppressRealDraw = true;
                }
            }

            const char* backTmpl = g_getLocalizedStringX64("PLATFORM_BACK_SHORTCUT");
            bool isBackCornerHint = backTmpl && LooksSaneX64(reinterpret_cast<uintptr_t>(backTmpl)) &&
                strcmp(text, backTmpl) == 0;
            const char* friendsTmpl = g_getLocalizedStringX64("PLATFORM_FRIENDS_SHORTCUT");
            bool isFriendsCornerHint = friendsTmpl && LooksSaneX64(reinterpret_cast<uintptr_t>(friendsTmpl)) &&
                strcmp(text, friendsTmpl) == 0;
            // Game Summary (2026-09-13 port of x86's PLATFORM_GAMESUMMARY_SHORTCUT
            // exact-match case, analog_input_hooks.cpp ~line 8849 -- same span-gated
            // block as Back/Friends there, ported alongside them here for the same
            // reason).
            const char* gameSummaryTmpl = g_getLocalizedStringX64("PLATFORM_GAMESUMMARY_SHORTCUT");
            bool isGameSummaryCornerHint = gameSummaryTmpl && LooksSaneX64(reinterpret_cast<uintptr_t>(gameSummaryTmpl)) &&
                strcmp(text, gameSummaryTmpl) == 0;

            // !suppressRealDraw guard (defensive, matches x86's own textLen=0-when-
            // already-suppressed pattern for this same block, analog_input_hooks.cpp
            // ~line 8813) -- in practice Quit/Leaderboards' own text content never
            // equals Back/Friends/GameSummary's, so this is a fail-safe, not a
            // load-bearing condition.
            if (!suppressRealDraw && (isBackCornerHint || isFriendsCornerHint || isGameSummaryCornerHint)) {
                char assetName[32] = {};
                bool haveAssetName = isBackCornerHint
                    ? TryGetMenuGlyphAssetNameForKeyName("ESC", assetName, sizeof(assetName))
                    : isGameSummaryCornerHint
                        ? TryGetMenuGlyphAssetNameForKeyName("G", assetName, sizeof(assetName))
                        : TryGetMenuGlyphAssetNameForKeyName("F", assetName, sizeof(assetName));
                if (haveAssetName) {
                    size_t textLen = strlen(text);
                    ColorHighlightSpanX64 span = FindColorHighlightSpanX64(text, textLen);
                    if (span.found) {
                        char prefixText[128] = {};
                        size_t prefixLen = span.markerStart < sizeof(prefixText) - 1 ? span.markerStart : sizeof(prefixText) - 1;
                        memcpy(prefixText, text, prefixLen);
                        prefixText[prefixLen] = '\0';

                        char suffixText[128] = {};
                        if (span.markerEnd < textLen) {
                            size_t suffixLen = textLen - span.markerEnd;
                            if (suffixLen >= sizeof(suffixText)) suffixLen = sizeof(suffixText) - 1;
                            memcpy(suffixText, text + span.markerEnd, suffixLen);
                            suffixText[suffixLen] = '\0';
                        }

                        // Raw y + fixed nudge, NOT y*scale -- see this block's own header
                        // comment. kMenuHintVerticalNudgeX64 (declared above, alongside
                        // looksLikeCornerHintRowX64) matches x86's own
                        // kMenuHintVerticalNudge value exactly, but is UNVERIFIED live on
                        // x64 (same honest caveat as the gameplay-hint block above -- x86
                        // reached this exact constant via live-tested empirical rounds
                        // this port has not repeated).
                        float designX = 0.0f, designY = 0.0f;
                        ConvertRealScreenPosToDesignSpaceX64(x, y + kMenuHintVerticalNudgeX64, designX, designY);
                        suppressRealDraw = true;
                        // Special-Ops-nested-modal / Friends-list-open suppression
                        // (2026-09-13 port of x86's IsInsideSpecOpsNestedModal()/
                        // IsFriendsListOpen() suppression, analog_input_hooks.cpp
                        // ~line 8918). Only ever suppresses the FRIENDS request
                        // specifically, same as x86 -- Back/GameSummary always draw.
                        // suppressRealDraw above still hides the native legend text
                        // either way, matching x86's own unconditional suppressRealDraw
                        // assignment.
                        if (!(isFriendsCornerHint && (IsInsideSpecOpsNestedModalX64() || IsFriendsListOpenX64()))) {
                            RequestMenuHintOverlay(designX, designY, prefixText, suffixText, assetName,
                                                     0xFFFFFFFFu, /*isBackShortcut=*/isBackCornerHint);
                        }

                        static bool s_loggedFirstMenuHintMatch = false;
                        if (!s_loggedFirstMenuHintMatch) {
                            s_loggedFirstMenuHintMatch = true;
                            char buf[176];
                            sprintf_s(buf, "[x64-drawtext] Menu corner-hint structural match confirmed "
                                "(kind=%s) -- native hint text suppressed, our own icon+text drawn instead",
                                isBackCornerHint ? "Back" : isGameSummaryCornerHint ? "GameSummary" : "Friends");
                            LogFromController(buf);
                        }
                    }
                }
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }
    }

    if (!suppressRealDraw) {
        g_realDrawTextX64(dcHandle, text, maxChars, fontArg, x, y, color1, color2, scale, colorVecPtr, extra);
    }
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

    // Native text-draw hook (2026-09-13 port, x86's Hook_DrawGlyphText) -- see this
    // file's own "Native text-draw hook" section header comment for the full
    // discovery trail and honest scope note. Two independent resolves: the hook
    // itself (FUN_14029a2b0) and a direct-call resolve for the real localized-
    // string lookup (FUN_14029f120) Mantle-hint detection needs. Either can fail
    // independently without affecting the other or any hook installed above.
    {
        SigScan::Result r = SigScan::FindPatternInMainModule(kDrawTextSignature);
        if (!r.found) {
            LogFromController("[x64-drawtext] FATAL: text-draw signature did not resolve -- gameplay hint glyph "
                "detection (incl. Auto-Mantle's own dependency) will not work this session");
        } else {
            void* target = reinterpret_cast<void*>(r.address);
            MH_STATUS createStatus = MH_CreateHook(target, reinterpret_cast<void*>(&Hook_DrawTextX64),
                                                    reinterpret_cast<void**>(&g_realDrawTextX64));
            if (createStatus != MH_OK) {
                char buf[160];
                sprintf_s(buf, "[x64-drawtext] FATAL: MH_CreateHook failed for text-draw @ 0x%llX (status=%d)",
                           static_cast<unsigned long long>(r.address), static_cast<int>(createStatus));
                LogFromController(buf);
            } else {
                MH_STATUS enableStatus = MH_EnableHook(target);
                if (enableStatus != MH_OK) {
                    char buf[160];
                    sprintf_s(buf, "[x64-drawtext] FATAL: MH_EnableHook failed for text-draw @ 0x%llX (status=%d)",
                               static_cast<unsigned long long>(r.address), static_cast<int>(enableStatus));
                    LogFromController(buf);
                } else {
                    LogFromController("[x64-drawtext] Text-draw hook installed and enabled -- real visual "
                        "glyph-icon SUBSTITUTION now active for Mantle/Pickup-Swap-PickupHealth/Throwback "
                        "(see this hook's own header comment, part (c), 2026-09-13). Watch for "
                        "'[x64-drawtext] First real glyph-icon SUBSTITUTION fired' to confirm live. "
                        "Buy-station, Survival ready-up, Reload, and Sentry-Place remain UNPORTED (native "
                        "text still renders unmodified for those) -- see the header comment's own honest "
                        "scope note for why each one is blocked.");
                }
            }
        }
    }
    // Mantle-hint structural-match detection's own dependency: the real localized-
    // string lookup (FUN_14029f120), resolved for a direct call, no hook. Failing
    // here leaves the text-draw hook above installed and passthrough-safe -- only
    // Mantle-hint detection (and Auto-Mantle's own dependency on it) is affected.
    {
        SigScan::Result r = SigScan::FindPatternInMainModule(kGetLocalizedStringSignature);
        if (!r.found) {
            LogFromController("[x64-drawtext] FATAL: localized-string-lookup signature did not resolve -- "
                "Mantle-hint detection will not work this session even though the text-draw hook above "
                "installed fine (Auto-Mantle's own dependency stays unresolved)");
        } else {
            g_getLocalizedStringX64 = reinterpret_cast<GetLocalizedStringFnX64>(r.address);
            // 2026-09-13 CONFIRMED CRASH SITE, actually fixed here (a prior same-day commit's
            // message claimed this line was fixed as part of a broader sweep, but the sweep
            // never actually touched this specific line -- caught only because the game still
            // failed to launch afterward, at the identical fault offset/crash-dump signature,
            // proving the fix had never landed). This literal text alone is 239 chars -- the
            // old buf[160] only had room for 20 more bytes total (16-hex %llX + null), so
            // sprintf_s's own UCRT fail-fast (0xc0000409, FAST_FAIL_INVALID_ARG subcode 5)
            // crashed on EVERY launch reaching this line, confirmed via two separate live
            // crash dumps (iw5sp.exe.14364.dmp and iw5sp.exe.4540.dmp, both symbolized in
            // WinDbg against the built PDB, both showing the identical stack:
            // sprintf_s<160> <- InstallAnalogInputHooksX64+0xa2e <- DllMain).
            char buf[320];
            sprintf_s(buf, "[x64-drawtext] Localized-string lookup resolved @ 0x%llX -- Mantle-hint "
                "structural-match detection active (direct call, no hook installed). Auto-Mantle's own "
                "+gostand-forcing feature still needs a separate follow-on to consume this signal.",
                static_cast<unsigned long long>(r.address));
            LogFromController(buf);
        }
    }

    // Custom mouse cursor overlay (2026-09-13 port) -- resolves the two real x64
    // globals overlay_hud.cpp's DrawCustomCursorIfNeeded needs to replace its
    // current x64 early-return stub. See this file's own "Custom mouse cursor
    // overlay" section header comment (above TryGetCursorGateX64) for the full
    // discovery trail. Independent of every other resolve in this function -- a
    // failure here only keeps the existing safe early-return stub in place, no
    // other feature is affected either way.
    {
        SigScan::Result r = SigScan::FindPatternInMainModule(kCursorGateSignature);
        if (!r.found) {
            LogFromController("[x64-cursor] FATAL: cursor-gate signature did not resolve -- the custom mouse "
                "cursor overlay will not draw this session (safe early-return stub stays in place)");
        } else {
            g_cursorVisibleFlagX64 = reinterpret_cast<int32_t*>(
                SigScan::ResolveRipRelative(r.address + kCursorVisFlagInsnOffset, kCursorVisFlagInsnLength));
            g_cursorUiStateX64 = reinterpret_cast<int32_t*>(
                SigScan::ResolveRipRelative(r.address + kCursorUiStateInsnOffset, kCursorUiStateInsnLength));
            if (g_cursorVisibleFlagX64 && g_cursorUiStateX64) {
                char buf[224];
                sprintf_s(buf, "[x64-cursor] Cursor gate resolved: visFlag=0x%p uiState=0x%p -- custom cursor "
                    "overlay active (read-only, no hook installed).",
                    (void*)g_cursorVisibleFlagX64, (void*)g_cursorUiStateX64);
                LogFromController(buf);
            } else {
                g_cursorVisibleFlagX64 = nullptr;
                g_cursorUiStateX64 = nullptr;
                LogFromController("[x64-cursor] FATAL: cursor-gate RIP-relative resolution failed -- the custom "
                    "mouse cursor overlay will not draw this session (safe early-return stub stays in place)");
            }
        }
    }
}
