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
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include "../third_party/minhook/include/MinHook.h"
#include "controller_input.h"
#include "mod_config.h"
#include "overlay_hud.h"
#include "rumble.h"
#include "options_render_suppress.h"
#include "real_settings.h"
#include "asset_capture.h"
#include "frame_benchmark.h"

// Forwarder defined in dllmain.cpp -- lets this translation unit log to the same
// proxy_d3d9.log file without duplicating the log-file setup.
extern void LogFromController(const char* msg);

// Defined in d3d9_hook.cpp -- same mouse primitives overlay_hud.cpp's own harness-only
// diagram editor already uses (see EditGlyphPositionsForFrame below, its in-game
// counterpart).
extern "C" bool GetLastMouseMoveClientPos(int& outX, int& outY);
extern "C" bool IsLeftMouseButtonHeld();

// Defined in mod_config.cpp (2026-07-31, config hot-reload QoL feature) -- stats
// mw3ncp_config.ini's last-write-time (internally rate-limited) and re-runs
// LoadModConfig() if it changed since the last check, showing an on-screen
// confirmation via ShowOverlayMessage (overlay_hud.cpp).
extern "C" void CheckConfigHotReload();
// Defined in overlay_hud.cpp -- a no-op unless [Overlay] TestCycleAllVariants is on,
// strictly a testing aid (see that config key's own comment).
void TickOverlayTestCycle();

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

// Real native menu-stack depth (2026-08-02, dedicated Ghidra research pass -- see
// menu_stack_findings.md / re_notes/known_issues.md for the full decompiled evidence).
// A genuine menu stack hangs off one global context object at 0x01c00458: this int
// field (offset 0xA7C, int-index 0x29F from the struct base) is literally "how many
// menus are currently open" (0 = none, 1 = exactly one screen, 2+ = a popup/modal is
// stacked on top of something else). Confirmed via THREE independent real functions
// that all walk this exact [0x29f]-depth/[0x28e/0x28f]-array shape, including
// FUN_00547980 -- the engine's own real "get the current topmost active menu"
// accessor, called directly by ForwardKeyToMenu (0x004d9850, already used throughout
// this file for every synthetic keypress) right before it decides where to route
// input. This is a plain memory read, no hooking needed.
constexpr uintptr_t kMenuStackCtx = 0x01c00458;
constexpr int kMenuStackDepthOffset = 0xA7C;
int GetMenuStackDepth()
{
    return *reinterpret_cast<volatile int*>(kMenuStackCtx + kMenuStackDepthOffset);
}

// Same research (4 passes total, see re_notes/known_issues.md): FUN_00547980(&ctx) is
// the engine's own real "get current topmost active menu" accessor -- called directly
// by ForwardKeyToMenu (0x004d9850) before it routes input, confirmed via decompile to
// walk the stack top-down and return the first entry whose per-player flags mark it
// genuinely visible/focused (not just present on the stack). Returned menu object has
// a real itemDef array: menu+0xa8 = item count, menu+0xac = itemDef*[count] -- this is
// literally what getfocuseditemname()'s own real implementation (FUN_004c1220) walks
// internally. Calling the real function rather than reimplementing its per-player-flag
// walk ourselves, per this project's own established preference. Wrapped in SEH since
// this is the first call-through to this specific address -- its calling convention
// (assumed __cdecl, matching every other "clean" FUN_ pointer in this file) was
// inferred from a clean decompile with no unaff_ESI/EDI register-guess artifacts, but
// hasn't been live-exercised yet.
using GetTopmostActiveMenuFn = void*(__cdecl*)(void* ctx);
GetTopmostActiveMenuFn const GetTopmostActiveMenuNative = reinterpret_cast<GetTopmostActiveMenuFn>(0x00547980);

void* GetTopmostActiveMenu()
{
    __try {
        return GetTopmostActiveMenuNative(reinterpret_cast<void*>(kMenuStackCtx));
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

// menu+0xa8 -- confirmed via FUN_004c1220's own decompile (see the big comment above).
int GetActiveMenuItemCount()
{
    void* menu = GetTopmostActiveMenu();
    if (!menu) return -1;
    __try {
        return *reinterpret_cast<int*>(static_cast<char*>(menu) + 0xa8);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return -1;
    }
}

// A plain, local sanity check (not LooksLikeValidPointer -- that's declared later in
// this file and using it here would be a use-before-declaration, a mistake this
// project has already made and fixed once for GetActiveMenuItemCount() above).
inline bool LooksSane(uintptr_t p)
{
    return p >= 0x10000 && p < 0x80000000u;
}

// Direct itemDef-array focus read (2026-08-03, issue #51 follow-up) -- confirmed
// live that getfocuseditemname()'s own native call is NOT invoked by every
// screen's .menu script (e.g. CAMPAIGN_BUTTON_LIST never calls it at all), so
// g_focusedItemName can sit frozen on a stale value from whatever screen last
// happened to trigger it, indefinitely, breaking any check that depends on it
// being fresh for the CURRENT screen. This reads the exact same real memory this
// project's own live-memory-dump analysis (issue #51's calibration tooling)
// already proved reliable: walk the topmost menu's real itemDef array
// (menu+0xa8=count, menu+0xac=itemDef*[count], both already confirmed) and check
// each item's own per-player focus-flag byte (+0x48, bits 0x4+0x2) directly --
// a plain memory read, true every frame regardless of whether any script ever
// calls getfocuseditemname(). The focused item's own real internal name (+0x0)
// is parsed the same way UpdateNavGroupTrackingFromResolvedValue already parses
// ui_swf_selection values (strip a trailing "_<digits>" suffix).
bool TryGetRealFocusedGroupAndIndex(char* outGroupName, size_t outGroupNameSize, int& outIndex, int& outSiblingCount);

bool TryGetRealFocusedGroupAndIndex(char* outGroupName, size_t outGroupNameSize, int& outIndex)
{
    int unusedSiblingCount = -1;
    return TryGetRealFocusedGroupAndIndex(outGroupName, outGroupNameSize, outIndex, unusedSiblingCount);
}

// Overload adding outSiblingCount (2026-08-03, issue #51 follow-up): the REAL number
// of items in the SAME array sharing this exact base name (i.e. how many real
// "<group>_<N>" items actually exist in THIS instance of the screen), not just the
// focused one's own index. Needed because some shared groups (e.g.
// SWF_COMMON_POPUP_NAME) are reused for variants with different real item counts --
// live-confirmed via "Choose Content Pack" (2 real items: On Disk Content/DLC
// Content) vs. the 3-item "Leave Lobby?" variant the manual position table was
// originally calibrated against. The table's own per-index Y values are BOTTOM-
// anchored in practice (confirmed live: the 2-item variant's real items land at the
// table's OWN index1/index2 Y values, 554/603, not index0/index1's 504/554) --
// TryGetManualGlyphPosition uses this count to shift into the shared table
// correctly instead of assuming the focused item's own local index (always
// 0-based per instance) lines up with the table's absolute row.
bool TryGetRealFocusedGroupAndIndex(char* outGroupName, size_t outGroupNameSize, int& outIndex, int& outSiblingCount)
{
    void* menu = GetTopmostActiveMenu();
    if (!menu) return false;
    __try {
        int count = *reinterpret_cast<int*>(static_cast<char*>(menu) + 0xa8);
        uintptr_t arr = *reinterpret_cast<uintptr_t*>(static_cast<char*>(menu) + 0xac);
        if (count <= 0 || count > 500 || !LooksSane(arr)) return false;
        for (int i = 0; i < count; ++i) {
            uintptr_t itemPtr = *reinterpret_cast<uintptr_t*>(arr + i * 4);
            if (!LooksSane(itemPtr)) continue;
            uint32_t flags0 = *reinterpret_cast<uint32_t*>(itemPtr + 0x48);
            if ((flags0 & 0x4) == 0 || ((flags0 >> 1) & 1) == 0) continue;
            uintptr_t nameAddr = *reinterpret_cast<uintptr_t*>(itemPtr + 0x0);
            if (!LooksSane(nameAddr)) return false;
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

            // Second pass: count real siblings sharing this exact base name (bounded
            // by the same validated count/arr from above, so no extra validation needed).
            int siblingCount = 0;
            for (int k = 0; k < count; ++k) {
                uintptr_t siblingPtr = *reinterpret_cast<uintptr_t*>(arr + k * 4);
                if (!LooksSane(siblingPtr)) continue;
                uintptr_t siblingNameAddr = *reinterpret_cast<uintptr_t*>(siblingPtr + 0x0);
                if (!LooksSane(siblingNameAddr)) continue;
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

// Debounced wrapper around TryGetRealFocusedGroupAndIndex (2026-08-16, live-reported
// "it goes to the set position but after x amount of time [it] moves" -- confirmed
// happening on the REAL, SHIPPED glyph overlay during ordinary play, not the in-game
// editor). Root cause: the raw itemDef-array read above can briefly report a stale
// or not-yet-settled (group, index) for a few frames during a menu transition --
// already documented elsewhere in this codebase (haveFocus/siblingCount observed
// flickering frame-to-frame even while sitting completely still, e.g. the
// [manual-glyph-diag] log alternating haveFocus=1 and haveFocus=0 for the exact same
// screen). Every caller that positions something against "the currently focused
// item" (the shipped manual-table draw below, and the in-game glyph editor) was
// calling the raw version directly, so a transient misread could visibly snap
// whatever's drawn onto a DIFFERENT already-calibrated item's position for a moment
// before settling back -- looking exactly like an already-correct, already-exported
// position "moving" on its own even though nothing changed. Requires the SAME
// (group, depth, index) to be reported for several consecutive frames before it's
// treated as the real target; a single frame of lost focus is treated the same way
// (ignored, not cleared) since that's exactly the kind of flicker this exists to
// filter out. State is a single shared instance (not per-caller) since only one
// screen/item can be genuinely focused at a time -- callers that ask on the same
// frame this returns for see the same stable answer.
bool TryGetStableFocusedGroupAndIndex(char* outGroupName, size_t outGroupNameSize,
                                        int& outDepth, int& outIndex, int& outSiblingCount)
{
    static char s_stableGroup[64] = "";
    static int s_stableDepth = -999, s_stableIndex = -999, s_stableSiblingCount = -1;
    static char s_pendingGroup[64] = "";
    static int s_pendingDepth = -999, s_pendingIndex = -999, s_pendingSiblingCount = -1;
    static int s_pendingStableFrames = 0;
    static int s_pendingLostFrames = 0;
    constexpr int kFocusStableFrameThreshold = 4;

    char rawGroup[64] = {};
    int rawIndex = -1, rawSiblingCount = -1;
    bool haveRaw = TryGetRealFocusedGroupAndIndex(rawGroup, sizeof(rawGroup), rawIndex, rawSiblingCount);
    if (haveRaw) {
        s_pendingLostFrames = 0;
        int curDepth = GetMenuStackDepth();
        if (_stricmp(rawGroup, s_pendingGroup) == 0 && rawIndex == s_pendingIndex && curDepth == s_pendingDepth) {
            if (s_pendingStableFrames < kFocusStableFrameThreshold) ++s_pendingStableFrames;
        } else {
            strncpy_s(s_pendingGroup, rawGroup, _TRUNCATE);
            s_pendingDepth = curDepth;
            s_pendingIndex = rawIndex;
            s_pendingSiblingCount = rawSiblingCount;
            s_pendingStableFrames = 1;
        }
        if (s_pendingStableFrames >= kFocusStableFrameThreshold) {
            strncpy_s(s_stableGroup, s_pendingGroup, _TRUNCATE);
            s_stableDepth = s_pendingDepth;
            s_stableIndex = s_pendingIndex;
            s_stableSiblingCount = s_pendingSiblingCount;
        }
    } else if (++s_pendingLostFrames >= kFocusStableFrameThreshold) {
        s_pendingStableFrames = 0;
        s_stableIndex = -999;
        s_stableGroup[0] = '\0';
    }

    if (s_stableIndex < 0 || s_stableGroup[0] == '\0') return false;
    strncpy_s(outGroupName, outGroupNameSize, s_stableGroup, _TRUNCATE);
    outDepth = s_stableDepth;
    outIndex = s_stableIndex;
    outSiblingCount = s_stableSiblingCount;
    return true;
}

// BUG-051 diagnostic (2026-08-02) -- user question: is FUN_00547980 itself returning
// the wrong stack entry for a lightweight popup? That function walks the stack
// top-down but SKIPS any entry whose per-player flags (+0x4c+player*4, bits 0x4+0x2)
// aren't both set -- a real filter for "genuinely visible/focused," not just
// "present." A small fixed popup might not set those exact bits the same way a full
// menu screen does, which would make FUN_00547980 silently fall through to the
// screen underneath -- explaining a suspiciously large activeMenuItemCount (the
// hub's real total itemDef count, not garbage) at the same time depth() correctly
// shows 2+ (the popup genuinely is on the stack, just skipped by the flag filter).
// This reads index [depth-1] of the SAME inline stack array directly, no flag
// filtering at all, so its result can be compared against GetTopmostActiveMenu()'s.
constexpr int kMenuStackArrayOffset = 0xA3C;

void* GetRawTopOfStackMenu()
{
    int depth = GetMenuStackDepth();
    if (depth <= 0) return nullptr;
    __try {
        void* const* stackArray = reinterpret_cast<void* const*>(kMenuStackCtx + kMenuStackArrayOffset);
        return stackArray[depth - 1];
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

int GetRawTopOfStackItemCount()
{
    void* menu = GetRawTopOfStackMenu();
    if (!menu) return -1;
    __try {
        return *reinterpret_cast<int*>(static_cast<char*>(menu) + 0xa8);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return -1;
    }
}

// User-requested (2026-08-02): the glyph/hint overlay system should turn itself off
// whenever keyboard/mouse becomes the active input method, same as console never
// shows button-prompt glyphs alongside a mouse cursor -- uses the exact same shared
// decision (controller_input.h's IsControllerActiveInputMethod) the custom cursor
// overlay's own visibility already depends on, so the two can never disagree. Every
// real DRAW gate in this file that previously checked g_modConfig.glyphIconOverlayEnabled
// directly now calls this instead.
//
// glyphIconOverlayEnabled REMOVED 2026-08-16 (issue #74 root-cause fix, mod_config.h):
// it was the master on/off switch for the whole overlay, shipped defaulted OFF since
// issue #48 and never flipped on for release -- the real cause of every "no glyphs"
// community report. Removed entirely rather than just flipping its default, so a stale
// `false` already written to an existing user's on-disk ini can't keep blocking this.
bool ShouldDrawGlyphOverlay()
{
    // Live-reported 2026-08-08 (Nexus, v0.3.1): several players see no glyphs at all,
    // even on English -- not reproducible on the developer's own machine, so this is a
    // real diagnostic escape hatch, not a permanent behavior change. See
    // g_modConfig.forceGlyphOverlay's own header comment (mod_config.h) for the full
    // reasoning and what a live test with this on is expected to reveal either way.
    return g_modConfig.forceGlyphOverlay || IsControllerActiveInputMethod();
}

// ---- BUG-003 follow-up (2026-08-02): B-press/crouch-drop diagnostics --------------
//
// Stream-reported symptom: crouch occasionally stops responding, user-confirmed via
// direct playtest log analysis to correlate with (but per the user's own earlier
// Campaign testing, NOT exclusively caused by) a Survival round transition -- user
// has also directly confirmed this happens with NO menu/buy-station involved at all,
// so IsMenuActive() being genuinely true is not a required precondition. Two prior
// suspects were fully RULED OUT by analyzing the actual stream's proxy_d3d9.log: the
// game window/device was only ever created once that whole session (no repeated
// SendSyntheticActivationClick misfires), and every single ToggleStance attempt (102
// of them) succeeded on the first try with clean guard bytes -- so whatever's
// dropping the press happens upstream of RequestStanceToggle entirely, and is not
// (at least not in that specific session) the already-instrumented guard-byte lock.
// This logging doesn't presume a specific mechanism -- it just makes every previously
// invisible step in B's own gating path (menu-active state, and whether THIS press
// got latched as "the menu's press" per g_currentBPressTouchedMenu below) visible, so
// the next real occurrence gives a conclusive answer instead of another guess.
// Change-triggered (not a heartbeat), so a full session doesn't spam the log.
namespace {
bool g_lastMenuActiveGateState = false;
bool g_menuActiveGateStateInitialized = false;
}

void LogMenuActiveGateDiag()
{
    bool current = IsMenuActive();
    if (g_menuActiveGateStateInitialized && current == g_lastMenuActiveGateState) return;
    g_menuActiveGateStateInitialized = true;
    g_lastMenuActiveGateState = current;

    char buf[96];
    sprintf_s(buf, "[menu-active-gate-diag] IsMenuActive()=%d t=%lu", current ? 1 : 0, GetTickCount());
    LogFromController(buf);
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
        // LB+RB held together is reserved as this project's own out-of-band
        // capture/diagnostic chord (2026-08-02, live memory-dump tool) -- suppressed
        // here, centrally, so NEITHER shoulder button's normal mapped action fires
        // for any logical action layout maps them to, regardless of ButtonLayout.
        // Matches this file's own pre-existing convention of using LB+RB-held as an
        // "impossible to trigger by accident during normal play" combo (see the
        // zone-load-test/font-struct-diag comments elsewhere in this file).
        case PhysicalInput::RB: return (buttons & kXI_RIGHT_SHOULDER) != 0 && (buttons & kXI_LEFT_SHOULDER) == 0;
        case PhysicalInput::LB: return (buttons & kXI_LEFT_SHOULDER) != 0 && (buttons & kXI_RIGHT_SHOULDER) == 0;
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

// Forward declaration -- defined further down the file (its own diagnostic-log
// section); needed here since ProcessPendingStanceRetry below logs through it and
// this reopened anonymous namespace still resolves to the same one defined later.
void LogStanceDiag(const char* tag);

// ---- Bug #2/#42 fix attempt: ToggleStance's guard bytes can silently no-op a
// legitimate tap/hold (2026-07-31) --------------------------------------------------
//
// re_notes/known_issues.md issue #27 (Bug #2) and issue #42 both report crouch
// intermittently just not firing -- no crash, no error, the press is simply dropped.
// The real lead on record since issue #9: ToggleStance() (FUN_0057d2c0) has two guard
// bytes at its very top (playerIndex*0x230 + 0xA98CA0 / +0xA98BC4) that make the whole
// call a silent no-op if either is nonzero. Their real meaning (vehicle? cutscene?
// mid-animation? something else entirely) was never decoded, and still isn't -- but
// deciding what to DO about the symptom doesn't require decoding them: a blocked call
// is guaranteed to leave the real stance field completely untouched (see the
// disassembly above -- the guard `return`s before ever touching +0x1C), so retrying
// the exact same call on a later frame can never mis-fire. If still gated, the retry
// is just as harmless a no-op as the original; the instant the gate clears, the retry
// reproduces exactly the transition the original press asked for, instead of it being
// silently lost. This mirrors -- and should subsume -- both independently-reported
// "something unrelated seems to unstick it" patterns (pause/unpause, and issue #42's
// knife/melee press): if either one works by coincidentally landing after the same
// transient gate clears, a same-frame-onward retry loop reaches that same clearing
// moment on its own, every time, without needing the player to find the right
// unrelated action first.
constexpr uintptr_t kStanceGuard1Addr = 0xA98CA0; // playerIndex*0x230 offset omitted --
constexpr uintptr_t kStanceGuard2Addr = 0xA98BC4; // kLocalClientIndex is always 0 in SP
constexpr DWORD kStanceRetryTimeoutMs = 500; // give up chasing stale intent after this long

int g_pendingStanceMode = 0; // 0 = no retry pending; 1/2 = togglecrouch/toggleprone mode
int g_pendingStanceExpected = 0;
DWORD g_pendingStanceStartMs = 0;

void GetStanceGuardBytes(uint8_t& guard1, uint8_t& guard2)
{
    guard1 = *reinterpret_cast<volatile uint8_t*>(kStanceGuard1Addr);
    guard2 = *reinterpret_cast<volatile uint8_t*>(kStanceGuard2Addr);
}

// Calls the real ToggleStance and confirms, against the real stance field, that it
// actually took effect -- rather than trusting the call the way the original
// tap/hold code did. Logs guard-byte values on every attempt (the diagnostic issue
// #27's Bug #2 write-up asked for), and arms a short retry window if the call was
// silently blocked instead of dropping the press.
void RequestStanceToggle(unsigned int mode, const char* tag)
{
    int before = GetRealStance();
    ToggleStance(kLocalClientIndex, mode);
    int after = GetRealStance();
    int expected = (before != static_cast<int>(mode)) ? static_cast<int>(mode) : 0;

    uint8_t guard1, guard2;
    GetStanceGuardBytes(guard1, guard2);
    char buf[220];
    sprintf_s(buf,
              "[stance-diag] %s before=%d after=%d expected=%d guard1=%d guard2=%d t=%lu",
              tag, before, after, expected, guard1, guard2, GetTickCount());
    LogFromController(buf);

    if (after == expected) {
        g_pendingStanceMode = 0; // took effect immediately -- nothing to chase
        return;
    }
    // Silently blocked -- arm the retry instead of letting the press vanish.
    g_pendingStanceMode = static_cast<int>(mode);
    g_pendingStanceExpected = expected;
    g_pendingStanceStartMs = GetTickCount();
}

// Re-attempts a blocked stance toggle once per frame until it takes effect or
// kStanceRetryTimeoutMs elapses. Safe for the same reason RequestStanceToggle's own
// first retry is: ToggleStance is a guaranteed no-op while genuinely gated, so calling
// it again every frame can only ever succeed once, never mis-fire early.
void ProcessPendingStanceRetry()
{
    if (g_pendingStanceMode == 0) return;
    if (GetRealStance() == g_pendingStanceExpected) {
        g_pendingStanceMode = 0; // reached the target some other way -- done
        return;
    }
    if (GetTickCount() - g_pendingStanceStartMs >= kStanceRetryTimeoutMs) {
        LogStanceDiag("stance-retry-timeout");
        g_pendingStanceMode = 0;
        return;
    }
    ToggleStance(kLocalClientIndex, static_cast<unsigned int>(g_pendingStanceMode));
    if (GetRealStance() == g_pendingStanceExpected) {
        LogStanceDiag("stance-retry-success");
        g_pendingStanceMode = 0;
    }
}
} // namespace

// BUG-001 follow-up (2026-08-02): IsMenuActive() above lives in an anonymous
// namespace (internal linkage -- fine for every OTHER caller in this same
// translation unit, but invisible to overlay_hud.cpp). Live log evidence showed the
// cursor's own uiState exclusion list (0/6/10) doesn't actually cover ordinary
// active gameplay at all (real values seen during a live "cursor persists through
// gameplay" repro: 1, 9 -- held for 20+ seconds straight -- and 2, none excluded)
// -- rather than keep guessing more magic uiState values one at a time, the cursor
// overlay now also requires this project's own already-proven-reliable "a real menu
// is open" signal, the same one every corner hint/ESC-forward call in this file
// already trusts. Thin exported wrapper so the internal IsMenuActive() everything
// else in this file already calls doesn't need to move/lose its current scope.
extern "C" bool IsMenuActive_Exported()
{
    return IsMenuActive();
}

// Same "internal linkage, invisible to overlay_hud.cpp" situation as IsMenuActive_Exported
// above, this time for RouteStickAxes -- issue #66 restyle (2026-08-05), needed by the
// Stick Layout drill-down screen's controller diagram to know which PHYSICAL stick each
// move/look axis currently comes from, per StickLayout preset. Rather than hand-duplicate
// RouteStickAxes' own swap table (a real, live-tested source of truth this project
// already has no reason to risk drifting out of sync with), this feeds it distinct
// sentinel values for left/right stick axes and checks which sentinel lands in each
// output -- reads the real routing directly instead of re-deriving it.
extern "C" void GetStickLayoutAxisSources(StickLayout layout, bool& moveXFromRight, bool& moveYFromRight,
                                            bool& lookXFromRight, bool& lookYFromRight)
{
    float moveX = 0.0f, moveY = 0.0f, lookX = 0.0f, lookY = 0.0f;
    RouteStickAxes(1.0f, 2.0f, 3.0f, 4.0f, layout, moveX, moveY, lookX, lookY); // 1=leftX,2=leftY,3=rightX,4=rightY
    moveXFromRight = (moveX == 3.0f);
    moveYFromRight = (moveY == 4.0f);
    lookXFromRight = (lookX == 3.0f);
    lookYFromRight = (lookY == 4.0f);
}

// ---- Movement: move-stick -> usercmd_t.forwardmove(+0x1c) / .rightmove(+0x1d) ----
//
// "Move-stick" rather than a hardcoded "left stick" since task #15's Stick Layout
// (g_modConfig.stickLayout) can route movement off either physical stick -- both are
// read every frame and RouteStickAxes (defined above) picks which feeds move vs. look
// per the active layout. Under the default layout this is exactly the original left-
// stick-only behavior.
// BUG-001 follow-up (2026-08-02): "recent input method" signal, controller side --
// see d3d9_hook.cpp's GetLastMouseMoveTickMs() for the keyboard/mouse side and the
// full rationale. Only marked on genuine post-deadzone stick deflection or an actual
// button/trigger press, never merely "a controller is connected and polling
// succeeded" -- otherwise this would read as "recently active" every single frame
// regardless of whether the player touched anything. MarkControllerActivity() itself
// is called centrally from controller_input.cpp's four Controller_Get*() functions
// (every caller, gameplay AND menu-nav ticks alike, goes through those), not
// scattered at each of this project's ~17 call sites -- see that file's own comment
// for why (a live-reported regression from a first attempt that only instrumented
// the gameplay-tick functions, which halt during menus).
DWORD g_lastControllerActivityTickMs = 0;
void MarkControllerActivity() { g_lastControllerActivityTickMs = GetTickCount(); }
extern "C" DWORD GetLastControllerActivityTickMs() { return g_lastControllerActivityTickMs; }

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

// Guard-byte values now included on EVERY call, not just tap/hold attempts (2026-07-31,
// issue #42 follow-up) -- the heartbeat call (every ~500ms, including right from level
// load) is what can actually answer "were the guards already locked at launch, and
// exactly when did they clear relative to some other event" -- data this project has
// never had a continuous timeline for before now that these bytes' real meaning is
// understood (see the big comment block above ProcessPendingStanceRetry: FUN_0050b770/
// FUN_0057d190/FUN_0057d430 confirm they're a genuine "stance change locked" pair, with
// FUN_0057d430 -- the per-frame keyboard-movement function this project's own movement
// hook already sits on top of -- forcing real stance to 0 and forcing usercmd crouch/
// prone button bits while locked, guard1-set = force prone, guard2-only-set = force
// crouch).
void LogStanceDiag(const char* tag)
{
    uint8_t guard1, guard2;
    GetStanceGuardBytes(guard1, guard2);
    char buf[200];
    sprintf_s(buf, "[stance-diag] %s realStance=%d guard1=%d guard2=%d t=%lu",
              tag, GetRealStance(), guard1, guard2, GetTickCount());
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
bool g_jumpButtonWasHeld = false; // user-reported gap (2026-08-02): Jump from prone should stand up first

// Issue #57 follow-up (2026-08-02, full .menu-file audit): co-op money-sharing's real
// mechanism was found in every survival_armory_*.menu shell -- a menu-level
// `execKeyInt 168` handler (self-gated on `iscoop() && credits>=500`) that fires
// `scriptmenuresponse share`, with hint text `"[{+activate}]"` (an engine template
// resolving to whatever key is really bound to +activate). The real default bind,
// confirmed directly from this install's own `players2/config.cfg`, is
// `bind F "+activate"` -- a genuinely different command from Reload's own real
// `bind R "+reload"` (this project's own X button already covers both Interact and
// Reload as one combined controller action, but on keyboard they're two separate
// keys/commands). Forwarded as a real 'F' keydown/keyup through the exact same
// ForwardKeyToMenu call (0x004d9850) this file's own ESC-forward/D-pad-nav code
// already uses for every other real menu keypress -- not a new mechanism, the same
// one.
//
// User-caught bug, same day, before this ever shipped: the FIRST version of this
// gated purely on IsMenuActive() -- far too broad, since 'F' is ALSO the real menu
// bind for opening the Friends list (see this file's own "Friends ^2F^7" corner-hint
// work). That would have fired on X in the MAIN MENU, OPTIONS, or any other menu
// entirely unrelated to a buy station, incorrectly popping Friends open. Buy
// stations are NOT a real pause (confirmed by this project's own established
// understanding -- Survival gameplay keeps running with a buy station open, unlike
// the real pause menu), so `cl_paused` (already read elsewhere in this file via
// GetDvarInt for the exact same "genuinely paused, not just some menu" distinction)
// combined with IsInSurvivalMode() narrows this to "a menu is open, during a
// Survival match, that isn't the real pause menu" -- excludes the main menu
// (IsInSurvivalMode() false there) and the Survival pause menu (cl_paused != 0)
// while still covering buy stations and any other in-Survival-gameplay menu.
using ForwardKeyToMenuFn = void(__cdecl*)(int playerIndex, int keyCode, int isDown);
ForwardKeyToMenuFn const ForwardKeyToMenuForShare = reinterpret_cast<ForwardKeyToMenuFn>(0x004d9850);
constexpr int kKeyMoneyShare = 'F';
int GetDvarInt(const char* name); // defined later in this file
bool IsInSurvivalMode(); // defined later in this file
bool IsSprintActive(); // defined later in this file -- auto-mantle needs it in InjectControllerButtons
bool IsMantleHintCurrentlyShowing(); // defined later in this file -- auto-mantle's real ledge gate
bool g_moneyShareButtonWasHeld = false;
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
    LogMenuActiveGateDiag(); // BUG-003 follow-up -- change-triggered, see its own comment above

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
    bool jumpHeld = IsPhysicalHeld(g_buttonMap.jump, xiButtons, leftTrigger, rightTrigger) && !IsMenuActive();
    if (jumpHeld) out |= 0x400;      // Jump (+gostand)
    // User-reported gap (2026-08-02): jumping from crouch/prone didn't stand the
    // player up first -- the raw +gostand usercmd bit alone isn't enough, same
    // reason Sprint needed a real ToggleStance call (ForceStandingViaRealToggle)
    // rather than relying on its own raw usercmd bit (see InjectControllerSprint's
    // own "auto-stand from crouch/prone" precedent, task #6). Mirrors that exact
    // condition (any non-standing stance, on Jump's own rising edge only).
    if (jumpHeld && !g_jumpButtonWasHeld && GetRealStance() != 0) {
        ForceStandingViaRealToggle();
    }
    g_jumpButtonWasHeld = jumpHeld;

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

    // Issue #57: money-sharing forward -- see g_moneyShareButtonWasHeld's own comment
    // above. Scoped to "a menu is open, during Survival, that isn't the real pause
    // menu" -- NOT the broad IsMenuActive() alone (would also fire in the main menu/
    // options and incorrectly pop Friends open, since 'F' is Friends' own real menu
    // bind). On X's own rising edge so a held press doesn't spam repeated keydowns.
    if (xHeld && !g_moneyShareButtonWasHeld && IsMenuActive() && IsInSurvivalMode() &&
        GetDvarInt("cl_paused") == 0) {
        LogFromController("[money-share-diag] X pressed in a Survival menu (not paused) -- forwarding real 'F' (+activate)");
        ForwardKeyToMenuForShare(kLocalClientIndex, kKeyMoneyShare, 1);
        ForwardKeyToMenuForShare(kLocalClientIndex, kKeyMoneyShare, 0);
    }
    g_moneyShareButtonWasHeld = xHeld;

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
        // Rising edge: new press starting. Any still-pending retry from a stale
        // prior press (see RequestStanceToggle/ProcessPendingStanceRetry above) is
        // superseded by this fresh input rather than left to fire underneath it.
        g_crouchButtonPressStartMs = GetTickCount();
        g_holdActionConsumed = false;
        g_pendingStanceMode = 0;
        // BUG-003 -- ROOT CAUSE CONFIRMED via live proxy_d3d9.log (2026-08-02): a
        // ~5.4s stretch showed 24 separate fresh rising edges here, EVERY one still
        // carrying touchedMenu=1, while [menu-active-gate-diag] confirmed
        // IsMenuActive() was false the ENTIRE time -- g_currentBPressTouchedMenu is
        // only ever cleared by InjectControllerMenuBack's own, SEPARATE edge tracker
        // (g_menuBackHeld), polled independently via the WndProc/timer tick, and it
        // had desynced from reality (this function's own tracker kept detecting the
        // real presses correctly the whole time). The flag had no way to self-correct
        // until MenuBack's tracker eventually caught up on its own. Fix: this
        // function's own rising-edge detection is proven reliable (it's the one that
        // caught all 24 real presses) -- if no menu is genuinely active RIGHT NOW,
        // there is no legitimate reason a BRAND NEW press could be "the menu's
        // press" (that classification is only ever valid for a press that itself
        // overlapped an active menu), so force-clear the stale latch here too,
        // independent of whatever MenuBack's own tracker is doing. Does not weaken
        // the original protection: a press that starts WHILE a menu is genuinely
        // active is untouched (menuActive is false in exactly the case being fixed).
        if (!IsMenuActive()) {
            g_currentBPressTouchedMenu = false;
        }
        // BUG-003 follow-up (2026-08-02): every rising edge logged unconditionally --
        // this is the one signal that proves the physical press was even seen by this
        // function at all, regardless of what happens to it afterward. Deliberately
        // NOT gated on anything (menu-touched, guard bytes, etc.) so a future "crouch
        // did nothing" report can be checked against whether this line even exists at
        // the right timestamp -- if it doesn't, the drop is upstream of this function
        // entirely (controller read / layout mapping), not anything below this point.
        char rBuf[160];
        sprintf_s(rBuf, "[bpress-diag] rising-edge touchedMenu=%d realStance=%d t=%lu",
                  g_currentBPressTouchedMenu ? 1 : 0, GetRealStance(), GetTickCount());
        LogFromController(rBuf);
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
            RequestStanceToggle(2, "hold-fire"); // toggleprone
            g_holdActionConsumed = true;
        }
    } else if (bHeld && !g_holdActionConsumed && g_currentBPressTouchedMenu) {
        // BUG-003 follow-up: this press crossed the hold threshold but is being
        // suppressed because it's latched as "the menu's press" -- logged once per
        // press (via g_holdActionConsumed) so a future repro shows exactly when/why a
        // hold was silently eaten instead of just absence of a hold-fire line.
        DWORD heldMs = GetTickCount() - g_crouchButtonPressStartMs;
        if (heldMs >= g_modConfig.proneHoldThresholdMs) {
            LogStanceDiag("bpress-suppressed-hold-menu-touch");
            g_holdActionConsumed = true; // still consume so this doesn't spam every frame
        }
    }
    if (!bHeld && g_crouchButtonWasHeld && !g_holdActionConsumed && !g_currentBPressTouchedMenu) {
        // Falling edge and the hold threshold was never reached -- this was a tap.
        RequestStanceToggle(1, "tap-fire"); // togglecrouch
    } else if (!bHeld && g_crouchButtonWasHeld && !g_holdActionConsumed && g_currentBPressTouchedMenu) {
        // BUG-003 follow-up: a tap that would have fired togglecrouch, but got
        // silently dropped because this press was latched as "the menu's press".
        LogStanceDiag("bpress-suppressed-tap-menu-touch");
    }
    g_crouchButtonWasHeld = bHeld;

    // Issue #27 Bug #2 / issue #42: chase a stance toggle that RequestStanceToggle()
    // above found silently blocked, once per frame, until it takes effect or times
    // out. Suppressed while a menu is active for the same reason the fresh-press
    // paths above are -- B's real intent while a menu is open is "close menu", not
    // "resolve a stale gameplay stance request".
    if (!IsMenuActive()) {
        ProcessPendingStanceRetry();
    }

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

    // Auto-mantle (2026-08-03) -- STRICTLY opt-in, see mod_config.h's own comment
    // for the full rationale/citation. Drives the exact same real +gostand command
    // (0x400) Jump already uses -- confirmed the real, contextual mantle trigger
    // (re_notes/iw5sp.md's "Mantle -- found, concretely" section: FUN_00568da0's
    // real call FUN_004fafd0(param_1, "+gostand", ...), the engine's own real
    // condition flags decide mantle-vs-stand-vs-nothing).
    //
    // REGRESSION FIXED same day, live-reported: "the sprint mantle is borked... it
    // jumps always when trying to sprint." Root cause: +gostand is NOT a safe no-op
    // absent a real ledge -- it's literally the SAME usercmd bit (0x400) as Jump, so
    // forcing it continuously while sprinting+forward with nothing to mantle over
    // just makes the player jump repeatedly, exactly like holding Jump would. Fixed
    // by gating on the game's own REAL "is there actually a mantleable ledge right
    // now" signal instead of inferring it from stance+stick alone: this project
    // already detects the real native "Press A to..." mantle hint text draw
    // elsewhere in this file (isMantleHint) to render it -- IsMantleHintCurrentlyShowing()
    // exposes that exact same detection (accumulated during rendering, committed
    // once per frame; see its own comment for the one-frame-lag this introduces,
    // imperceptible at 60fps) read-only here. isMantleHint's own detection was
    // upgraded 2026-08-05 (issue #68) from a literal "SPACE" text match (broken
    // under non-English languages) to a language-independent structural template
    // match against PLATFORM_MANTLE. FIXED 2026-08-16 (issue #62 follow-up, never
    // diagnosed before now): g_mantleHintDrawnThisFrame -- the flag
    // IsMantleHintCurrentlyShowing() actually reads -- used to only get set INSIDE
    // the block that also required this project's own icon lookup
    // (TryGetMantleGlyphAssetName) to succeed, silently coupling auto-mantle's
    // entire detection to "did our own icon resolve" rather than "is the real
    // native hint actually showing." Moved to fire unconditionally on isMantleHint
    // alone, right where that's computed. Auto-mantle now only fires when the engine itself has
    // ALREADY decided a ledge is mantleable, sprinting+stick-cone is additionally
    // required, and the real "any" mantle attempt still only reduces to Jump's own
    // existing behavior at that specific moment (never idle-frame jump-spam again).
    // Cooldown (2026-08-03, user-requested): once a real auto-mantle fires, suppress
    // re-triggering for kAutoMantleCooldownMs -- the mantle hint can plausibly stay
    // showing for more than one frame around the same ledge (still sprinting into
    // it, or a cluster of low obstacles), and without a cooldown that would fire
    // every single frame the condition holds, not just once per real mantle.
    static DWORD s_lastAutoMantleTriggerMs = 0;
    constexpr DWORD kAutoMantleCooldownMs = 750;

    // Diagnostic (2026-08-03, live-reported "doesn't fire at all now" -- the
    // opposite symptom from the earlier jump-spam regression this gate was added
    // to fix). Logs the two real gate conditions independently, rate-limited, so
    // a live retest near an actual mantleable ledge while sprinting can show
    // whether IsMantleHintCurrentlyShowing() ever becomes true AT THE SAME TIME as
    // IsSprintActive() -- if the hint is never observed showing while sprintActive
    // is also true, that points to the native hint itself not rendering during a
    // real sprint state (a possible engine behavior this project hasn't
    // specifically confirmed either way), not a bug in this gate's own logic.
    if (g_modConfig.autoMantleEnabled) {
        static DWORD s_lastAutoMantleDiagLogMs = 0;
        DWORD nowMsDiag = GetTickCount();
        bool sprintActiveDiag = IsSprintActive();
        bool mantleHintDiag = IsMantleHintCurrentlyShowing();
        if (mantleHintDiag || (nowMsDiag - s_lastAutoMantleDiagLogMs) >= 500) {
            s_lastAutoMantleDiagLogMs = nowMsDiag;
            char buf[128];
            sprintf_s(buf, "[automantle-diag] sprintActive=%d mantleHintShowing=%d",
                sprintActiveDiag ? 1 : 0, mantleHintDiag ? 1 : 0);
            LogFromController(buf);
        }
    }

    if (g_modConfig.autoMantleEnabled && IsSprintActive() && IsMantleHintCurrentlyShowing() &&
        (GetTickCount() - s_lastAutoMantleTriggerMs) >= kAutoMantleCooldownMs) {
        float amLeftX, amLeftY, amRightX, amRightY;
        if (Controller_GetLeftStick(amLeftX, amLeftY) && Controller_GetRightStick(amRightX, amRightY)) {
            float amMoveX, amMoveY, amLookX, amLookY;
            RouteStickAxes(amLeftX, amLeftY, amRightX, amRightY, g_modConfig.stickLayout,
                            amMoveX, amMoveY, amLookX, amLookY);
            float amMagnitude = sqrtf(amMoveX * amMoveX + amMoveY * amMoveY);
            if (amMoveY > 0.0f && amMagnitude >= g_modConfig.autoMantleMinStickMagnitude) {
                constexpr float kPi = 3.14159265f;
                float amHalfConeRad = (g_modConfig.autoMantleForwardConeDegrees * 0.5f) * (kPi / 180.0f);
                float amAngleFromForward = atan2f(fabsf(amMoveX), amMoveY);
                if (amAngleFromForward <= amHalfConeRad) {
                    out |= 0x400u; // +gostand
                    s_lastAutoMantleTriggerMs = GetTickCount();
                }
            }
        }
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

// ---- Hold Breath (L3 while ADS'd): genuine native kbutton (2026-07-20, task #24) --
//
// Same physical bind as Sprint on real console/keyboard (`+breath_sprint`) -- the
// disassembly trail above (case 9, "+breath_sprint" DOWN) showed the real bind fires
// TWO kbutton calls back-to-back, unconditionally, on every press: 0xA98C04 (this
// project's own name for Hold Breath's kbutton, actually Fire's own down[1] slot --
// see the aliasing finding in known_issues.md #6) and 0xA98CCC (Sprint's, above).
//
// Full saga, condensed (complete trail in re_notes/known_issues.md #6/#24): FOUR
// attempts before the real fix was found. #1 plain CallKbuttonDown/Up on 0xA98C04 --
// "engages once, never releases." #2 guessed (from a decompile read, not measured
// data) that a second flag byte at +0x11 was the culprit and manually cleared it --
// still stuck; wrong byte. Pivoted to a 4th key-synthesis exception (synthesizing a
// real Shift keypress instead of touching the kbutton at all) as attempts #3
// (PostMessage) and #4 (SendInput, after PostMessage also latched) -- both ALSO
// latched identically, proving the transport layer was never the problem. A live
// memory readback (AppendKbuttonSnapshots below) finally isolated it: 0xA98C04's
// down0/down1 cycle perfectly on every press/release, but its `active` byte (+0x10,
// NOT +0x11) latches to 1 on the first release and never clears again on its own.
// **Real fix**: go back to driving the kbutton directly (same calls as attempt #1),
// paired with ClearHoldBreathActiveFlag() force-clearing +0x10 after every release --
// CONFIRMED WORKING LIVE. This is genuinely native input, no key-synthesis exception
// needed for this feature after all -- the earlier synthesis detour was a real
// dead end, not wasted effort, since it's what proved the transport layer innocent
// and narrowed the search to kbutton_t's own fields.
bool g_holdBreathSyntheticHeld = false; // name kept for git history continuity; tracks
                                          // whether we currently believe the kbutton is
                                          // held, regardless of mechanism

// Debounce, added 2026-07-20 during the key-synthesis detour above, kept for the final
// native design too (cheap, harmless, avoids sending redundant KeyDown/KeyUp pairs
// faster than this 30fps-locked engine's own tick, 33.33ms/frame).
constexpr DWORD kHoldBreathDebounceMs = 40; // slightly over one 30fps frame (33.33ms)
DWORD g_lastHoldBreathTransitionMs = 0;

// Real-memory kbutton_t readback, added 2026-07-20 after TWO different transport-layer
// fixes (debounce, PostMessage->SendInput) both failed to unstick a live-reported
// "perma on" -- neither fix touched what actually happens once the key event reaches
// the native engine, and this diagnostic-first project shouldn't keep guessing fixes
// blind a third time. User's own hypothesis: this project's own Sprint-kbutton code
// (0xA98CCC, driven directly by UpdateSprintKbutton with OUR OWN bindIndex=16,
// gated !g_adsHeld) might be interacting badly with the SAME kbutton_t also being
// touched by the native dispatch's own internal bindIndex whenever our synthetic
// Shift reaches it (per the case-9 disassembly: a real/synthetic Shift press
// unconditionally drives BOTH 0xA98C04 -- Hold Breath's alias, already proven
// corrupted by FUN_0057dc90 -- AND this exact same 0xA98CCC Sprint kbutton). Rather
// than guess a fourth blind fix, log the REAL struct fields (down[0]/down[1]/active/
// the +0x11 flag byte) for BOTH addresses on every Hold Breath transition and
// heartbeat, so the next live session gives hard data on which kbutton (if either) is
// actually stuck down when the sway effect visually latches.
struct KbuttonSnapshot { unsigned int down0, down1; unsigned char active, flag11; };

KbuttonSnapshot ReadKbutton(uintptr_t addr)
{
    KbuttonSnapshot s;
    s.down0 = *reinterpret_cast<volatile unsigned int*>(addr + 0x00);
    s.down1 = *reinterpret_cast<volatile unsigned int*>(addr + 0x04);
    s.active = *reinterpret_cast<volatile unsigned char*>(addr + 0x10);
    s.flag11 = *reinterpret_cast<volatile unsigned char*>(addr + 0x11);
    return s;
}

constexpr uintptr_t kHoldBreathAliasAddr = 0x00A98C04; // = Fire's down[1] slot (confirmed
                                                         // alias, see known_issues.md #6)

void AppendKbuttonSnapshots(char* buf, size_t bufSize)
{
    KbuttonSnapshot hb = ReadKbutton(kHoldBreathAliasAddr);
    KbuttonSnapshot sp = ReadKbutton(kSprintKbutton);
    char extra[192];
    sprintf_s(extra, " | 0xA98C04(hb) down0=%u down1=%u active=%u f11=%u | 0xA98CCC(sp) down0=%u down1=%u active=%u f11=%u",
        hb.down0, hb.down1, hb.active, hb.flag11, sp.down0, sp.down1, sp.active, sp.flag11);
    strcat_s(buf, bufSize, extra);
}

// Targeted fix, 2026-07-20, built directly from the readback data above: across a full
// live session, 0xA98C04's down0/down1 cycled cleanly (0 <-> 160 = VK_LSHIFT) on every
// single press/release, but its `active` byte (+0x10) latched to 1 on the very FIRST
// release and never returned to 0 again for the rest of the session -- while
// 0xA98CCC (Sprint's real kbutton) toggled `active` perfectly in sync with its own
// down0 the entire time. This is direct, repeated, live-measured evidence (not a
// decompile guess like the earlier +0x11 attempt) that +0x10 specifically is the field
// failing to follow KeyUp on this alias. Force-clear it ourselves right after sending
// the synthetic release, since we now know down0/down1 already self-clear correctly and
// this is the one field that doesn't.
void ClearHoldBreathActiveFlag()
{
    *reinterpret_cast<volatile unsigned char*>(kHoldBreathAliasAddr + 0x10) = 0;
}

// CRITICAL FIX (2026-07-31, issue #46): this was 17, colliding directly with
// kAttackBindIndex (Fire's own bind index, also 17 -- see kAttackKbutton above).
// Combined with kHoldBreathAliasAddr being literally Fire's own down[1] memory
// field (see the aliasing writeup above), Hold Breath's CallKbuttonDown was
// writing the SAME bind-index value into Fire's own kbutton state that a real
// Fire press also writes -- live-reported as "can't shoot while holding breath
// with a sniper." The two calls' bind-index values need to be distinct from
// EVERY other bind index touching this aliased memory region, not just the
// other three this comment used to enumerate (which never included Fire's,
// since Fire's own constant lives in a different section of this file).
constexpr int kHoldBreathBindIndex = 18; // distinct from ADS's 13/Reload's 15/Sprint's
                                          // 16/Fire's 17 -- was 17 (WRONG, collided
                                          // with Fire) until the fix above.
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

    // Hold Breath (task #24): same physical bind, gated on ADS instead of stance --
    // CONFIRMED WORKING LIVE via direct CallKbuttonDown/CallKbuttonUp on 0xA98C04
    // paired with ClearHoldBreathActiveFlag()'s +0x10 force-clear. See the big
    // comment above g_holdBreathSyntheticHeld for the full saga of what didn't work
    // first and why.
    bool holdBreathActive = g_sprintHeld && g_adsHeld;
    if (holdBreathActive != g_holdBreathSyntheticHeld) {
        DWORD nowMsDebounce = GetTickCount();
        if (nowMsDebounce - g_lastHoldBreathTransitionMs >= kHoldBreathDebounceMs) {
            g_lastHoldBreathTransitionMs = nowMsDebounce;
            g_holdBreathSyntheticHeld = holdBreathActive;
            char hbBuf[320];
            sprintf_s(hbBuf, "[hold-breath-diag-v2] native kbutton -> %s (g_sprintHeld=%d g_adsHeld=%d)",
                holdBreathActive ? "DOWN" : "UP", g_sprintHeld ? 1 : 0, g_adsHeld ? 1 : 0);
            AppendKbuttonSnapshots(hbBuf, sizeof(hbBuf));
            LogFromController(hbBuf);
            if (holdBreathActive) {
                CallKbuttonDown(kHoldBreathAliasAddr, kHoldBreathBindIndex);
            } else {
                CallKbuttonUp(kHoldBreathAliasAddr, kHoldBreathBindIndex);
                // Force-clear the one field live data showed doesn't follow KeyUp on
                // this alias -- see ClearHoldBreathActiveFlag's comment above.
                ClearHoldBreathActiveFlag();
            }
        }
        // else: debounced -- desired state changed again before one engine frame
        // elapsed since the last transition we actually sent. Left unsent on purpose;
        // this same block re-checks every frame, so the moment debounce clears it will
        // send whatever the CURRENT desired state is, coalescing the flicker away
        // instead of forwarding every intermediate toggle to the native handler.
    } else if (!holdBreathActive) {
        // Continual self-heal, every frame while not held (cheap -- a single byte
        // write): the live data showed this flag never recovers on its own once
        // corrupted, so keep stamping it clear the whole time Hold Breath isn't
        // supposed to be engaged, not just once on the edge above.
        ClearHoldBreathActiveFlag();
    }

    // Heartbeat, widened 2026-07-20 to fire for the WHOLE ADS window, not just while
    // our own holdBreathActive tracking is true. The prior version only proved "our
    // own state" was clean; it went silent the instant we sent UP, leaving the entire
    // rest of a scoped session (where the user reports the sway effect still visually
    // stuck) completely uninstrumented. Now logs real kbutton_t state at both
    // addresses throughout the whole ADS period so a live session can show whether
    // either kbutton is ACTUALLY still down at the moment the effect is reported stuck,
    // regardless of what our own g_sprintHeld/g_holdBreathSyntheticHeld believe.
    static DWORD s_lastHoldBreathHeartbeatMs = 0;
    if (g_adsHeld) {
        DWORD nowMs = GetTickCount();
        if (nowMs - s_lastHoldBreathHeartbeatMs >= 500) {
            s_lastHoldBreathHeartbeatMs = nowMs;
            char hbBuf2[320];
            sprintf_s(hbBuf2, "[hold-breath-diag-v2] heartbeat: g_sprintHeld=%d g_adsHeld=%d holdBreathActive=%d t=%lu",
                g_sprintHeld ? 1 : 0, g_adsHeld ? 1 : 0, holdBreathActive ? 1 : 0, nowMs);
            AppendKbuttonSnapshots(hbBuf2, sizeof(hbBuf2));
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

    // Issue #44's real fix (2026-07-31): a genuinely SEPARATE, decoupled extra
    // slowdown for low-zoom weapons (pistols confirmed live to sit at ratio EXACTLY
    // 1.0 -- their ADS never changes FOV at all), instead of lowering
    // adsSlowdownBaseline itself -- that was tried first and reverted after live
    // testing found it also made high-zoom scopes "too harsh" (baseline multiplies
    // EVERY ratio value by the same relative percentage, so there is no way to
    // target low zoom only through it). kCloseRangeFocusPower is a high, internal-
    // only exponent (not configurable -- there's no player-facing reason to change
    // the SHAPE of the taper, only its strength) so ratio^kCloseRangeFocusPower
    // stays close to 1.0 (this term barely engages) once ratio drops much below
    // ~0.9, meaning it's negligible for any real optic with actual zoom (3x+
    // scopes: ratio ~0.3-0.4, ratio^8 is astronomically small) -- restoring their
    // feel to exactly what it was before adsSlowdownBaseline was ever touched
    // today. At ratio=1.0 (pistol), closeRangeFactor = 1 - strength exactly, giving
    // a real, meaningful extra reduction. Mathematically safe for any
    // adsCloseRangeSlowdownStrength in [0, 1] (clamped on config load): ratio is
    // always in (0, 1], so ratio^power is always in (0, 1], so closeRangeFactor is
    // always in [1-strength, 1] -- never negative, never inverts.
    constexpr float kCloseRangeFocusPower = 8.0f;
    float closeRangeFactor = 1.0f - g_modConfig.adsCloseRangeSlowdownStrength * powf(ratio, kCloseRangeFocusPower);
    scale *= closeRangeFactor;

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
        char buf[220];
        sprintf_s(buf,
            "[ads-fov-diag] baseFov=%.3f effectiveFov=%.3f ratio=%.4f closeRangeFactor=%.4f scale=%.4f altFlags=0x%02x",
            baseFov, effectiveFov, ratio, closeRangeFactor, scale, altPathFlags);
        LogFromController(buf);
    }

    return scale;
}

// ---- Look acceleration ramp (2026-07-19, known_issues.md issue #32) ---------------
//
// This project's controller look was deliberately built with NO acceleration/
// smoothing at all (2026-07-14) -- a flat rate, specifically to avoid inheriting
// the mouse pipeline's own filtering. External research (not native RE -- no
// MW3-specific dev documentation exists) found that MW2 and Black Ops, the same
// IW-engine lineage immediately surrounding MW3 (2011), both applied a real ~0.2s
// LINEAR ramp from zero to full turn speed on every stick input, regardless of
// deflection magnitude -- raising the real question of whether retail MW3 had
// similar behavior this project's "instant" look doesn't currently replicate.
// User-requested (2026-07-19): implement it, live-test it, and if it feels right,
// make it the default (not just an opt-in toggle) -- see mod_config.h's
// lookAccelerationRampMs. First shipped at 200ms (the ~0.2s figure from the
// external MW2/Black Ops research) -- user live-tested many values against real
// hardware (2026-07-20) and confirmed 200ms was WRONG: the real ramp is tied to
// this old engine's locked 30fps tick (33.33ms/frame), not an arbitrary
// wall-clock duration. Default is now 33ms (one engine frame), confirmed live.
//
// Mechanism: track how long the stick has been away from neutral (reset to zero
// the instant it returns to neutral, in InjectControllerLookAngles' else-branch),
// and linearly scale the look rate by elapsed/rampMs, capped at 1.0. rampMs=0
// disables this entirely (returns 1.0 unconditionally) -- the old instant-response
// behavior, for a clean revert if live-testing says it doesn't feel right.
DWORD g_lookAccelStartMs = 0;

float GetLookAccelerationScale()
{
    if (g_modConfig.lookAccelerationRampMs == 0) return 1.0f;

    DWORD nowMs = GetTickCount();
    if (g_lookAccelStartMs == 0) {
        g_lookAccelStartMs = nowMs; // rising edge: stick just left neutral this frame
    }
    DWORD elapsed = nowMs - g_lookAccelStartMs;
    if (elapsed >= g_modConfig.lookAccelerationRampMs) return 1.0f;

    return static_cast<float>(elapsed) / static_cast<float>(g_modConfig.lookAccelerationRampMs);
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

    if (lookX != 0.0f || lookY != 0.0f) {
        // Degrees per second at full stick deflection -- independent of every mouse
        // cvar. Horizontal (yaw) and vertical (pitch) are separate config values
        // ([Look] SensitivityHorizontal/SensitivityVertical in mw3ncp_config.ini,
        // task #14, split 2026-07-31 per user request) rather than a single shared
        // rate or a hardcoded constant.
        float sharedScale = GetAdsLookRateScale() * GetLookAccelerationScale();
        float yawRate = g_modConfig.lookDegreesPerSecondHorizontal * sharedScale;
        float pitchRate = g_modConfig.lookDegreesPerSecondVertical * sharedScale;
        float pitchInput = g_modConfig.invertLook ? -lookY : lookY; // OG console "Invert Look"
        *kYawAccum -= lookX * yawRate * dt;
        *kPitchAccum -= pitchInput * pitchRate * dt;
    } else {
        g_lookAccelStartMs = 0; // stick back at neutral -- next push starts the ramp fresh
    }

    // 2026-08-11 (issue #76): additive gyro-aim, PREVIEW/WIP -- see mod_config.h's
    // [Gyro] comment for the full rationale (built to avoid depending on Steam
    // Input for gyro, per explicit user direction) and why this is off by default
    // (never live-tested -- no DualSense available to the developer). Applied
    // regardless of the stick-look block above being active -- a gyro nudge should
    // register even while the right stick sits at neutral, that's the entire point
    // of gyro-assisted aim, unlike the stick path which intentionally does nothing
    // at rest.
    if (g_modConfig.gyroEnabled) {
        float gyroX = 0.0f, gyroY = 0.0f, gyroZ = 0.0f;
        if (Controller_GetGyroRate(gyroX, gyroY, gyroZ)) {
            // Axis-to-yaw/pitch mapping (Z=yaw, X=pitch) is a best-effort guess, not
            // verified against real hardware -- InvertYaw/InvertPitch exist
            // specifically so a live tester can correct this without a rebuild if
            // it's backwards or mapped to the wrong axis entirely.
            float yawDelta = gyroZ * g_modConfig.gyroSensitivity * dt;
            float pitchDelta = gyroX * g_modConfig.gyroSensitivity * dt;
            if (g_modConfig.gyroInvertYaw) yawDelta = -yawDelta;
            if (g_modConfig.gyroInvertPitch) pitchDelta = -pitchDelta;
            if (g_modConfig.invertLook) pitchDelta = -pitchDelta; // OG console "Invert Look" applies uniformly
            *kYawAccum -= yawDelta;
            *kPitchAccum -= pitchDelta;
        }
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
    // BUG-003 follow-up (2026-08-02): clean timestamp anchor for "a round transition's
    // ready-up just fired here" -- lets a future proxy_d3d9.log correlate a reported
    // crouch failure against how long after this line it happened.
    LogFromController("[ready-up-diag] SendSyntheticF5 fired");
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
    // Custom options overlay (2026-08-04): while it's open, B closes IT, not the real
    // Options screen underneath -- CustomOptionsMenu_TickInput (called from
    // InjectControllerMenuNav) handles that close itself. Without this guard, the
    // SAME B press would also forward a real ESC here, backing out of both the
    // overlay AND the real menu in one press.
    if (menuActive && held != g_menuBackHeld && !CustomOptionsMenu_IsOpen()) {
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
bool g_menuNavYHeld = false;
bool g_menuNavXHeld = false;
// LB/RB tab-switch edges for the custom Options replacement screen's own tab bar
// (issue #66) -- raw physical shoulder buttons, deliberately NOT run through
// ButtonMap/IsPhysicalHeld's remapping (same reasoning as D-pad Up/Down/Left/Right
// above: this project's own menu navigation uses fixed physical buttons, unaffected
// by whatever ButtonLayout the player has chosen for gameplay).
bool g_menuNavTabPrevHeld = false;
bool g_menuNavTabNextHeld = false;
bool g_menuNavBackButtonHeld = false; // physical Back/Select/View, distinct from g_menuNavSelectHeld (A) and g_menuBackHeld (B/ESC-forward)

// Live-reported 2026-08-01: the menu-hint glyph work correctly shows a Y icon next
// to "Friends" (console's real mapping, per the user's own earlier correction), but
// pressing physical Y did nothing -- that work only ever replaced the ON-SCREEN
// ICON, it never wired an actual keypress. Friends is a real keyboard bind ("F") the
// game's own key-event handler listens for directly -- NOT one of FUN_004dfd30's
// generic menu-navigation keycodes (Up/Down/Left/Right/Enter above), so this can't
// go through ForwardKeyToMenu the way those do. Uses the exact same technique as
// Survival's ready-up F5 synthesis (SendSyntheticF5): a real WM_KEYDOWN/WM_KEYUP
// posted straight at the game's own window, indistinguishable from an actual
// keypress since this game has no DirectInput import at all (keyboard input is
// genuine window messages either way).
void SendSyntheticF()
{
    HWND hwnd = GetGameWindow();
    if (!hwnd) return;
    PostMessageA(hwnd, WM_KEYDOWN, 'F', 0x00000001);
    PostMessageA(hwnd, WM_KEYUP, 'F', 0xC0000001);
}

// Same technique as SendSyntheticF above -- Game Summary's real bind is "G"
// (live-captured: "Game Summary ^2G^7"). 2026-08-01, quick completeness fix
// (X on controller).
void SendSyntheticG()
{
    HWND hwnd = GetGameWindow();
    if (!hwnd) return;
    PostMessageA(hwnd, WM_KEYDOWN, 'G', 0x00000001);
    PostMessageA(hwnd, WM_KEYUP, 'G', 0xC0000001);
}

// Same technique again -- Leaderboards' real bind is "F1" (live-captured:
// "Leaderboards ^2Right Mouse^7/^2F1^7", the function-key half of the combo
// per explicit user instruction). 2026-08-01, quick completeness fix (real
// Back/Select/View button on controller, PhysicalInput::Back).
void SendSyntheticF1()
{
    HWND hwnd = GetGameWindow();
    if (!hwnd) return;
    PostMessageA(hwnd, WM_KEYDOWN, VK_F1, 0x00000001);
    PostMessageA(hwnd, WM_KEYUP, VK_F1, 0xC0000001);
}
} // namespace

// Physical B state tracked independently here, separate from InjectControllerMenuBack's
// own g_menuBackHeld -- needed to give CustomOptionsMenu_TickInput its own back-edge
// signal without disturbing that function's real ESC-forward edge tracking.
bool g_optMenuBackHeldForCustomMenu = false;

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
        g_menuNavYHeld = false;
        g_menuNavXHeld = false;
        g_menuNavBackButtonHeld = false;
        g_menuNavTabPrevHeld = false;
        g_menuNavTabNextHeld = false;
        g_optMenuBackHeldForCustomMenu = false;
        CustomOptionsMenu_ResetOnMenuClose();
        return;
    }

    unsigned short buttons;
    unsigned char leftTrigger, rightTrigger;
    if (!Controller_GetRawButtonsAndTriggers(buttons, leftTrigger, rightTrigger)) return;

    bool upHeld = (buttons & kXI_DPAD_UP) != 0;
    bool upEdge = upHeld && !g_menuNavUpHeld;
    bool downHeld = (buttons & kXI_DPAD_DOWN) != 0;
    bool downEdge = downHeld && !g_menuNavDownHeld;
    bool leftHeld = (buttons & kXI_DPAD_LEFT) != 0;
    bool leftEdge = leftHeld && !g_menuNavLeftHeld;
    bool rightHeld = (buttons & kXI_DPAD_RIGHT) != 0;
    bool rightEdge = rightHeld && !g_menuNavRightHeld;
    bool selectHeld = IsPhysicalHeld(PhysicalInput::A, buttons, leftTrigger, rightTrigger);
    bool selectEdge = selectHeld && !g_menuNavSelectHeld;
    bool backHeldForCustomMenu = IsPhysicalHeld(PhysicalInput::B, buttons, leftTrigger, rightTrigger);
    bool backEdge = backHeldForCustomMenu && !g_optMenuBackHeldForCustomMenu;
    g_optMenuBackHeldForCustomMenu = backHeldForCustomMenu;
    bool tabPrevHeld = IsPhysicalHeld(PhysicalInput::LB, buttons, leftTrigger, rightTrigger);
    bool tabPrevEdge = tabPrevHeld && !g_menuNavTabPrevHeld;
    bool tabNextHeld = IsPhysicalHeld(PhysicalInput::RB, buttons, leftTrigger, rightTrigger);
    bool tabNextEdge = tabNextHeld && !g_menuNavTabNextHeld;

    // Custom options overlay (2026-08-04, see overlay_hud.h's own comment for the
    // full design). REWORKED same day (live feedback: "the button should be called
    // from the native options button we no longer need the individual mw32011ncp
    // options seperate") -- invocation is now the real pause menu's own "Options"
    // button itself, not a row appended below the real OPTIONS_LIST tab bar.
    // Confirmed via pausedmenu.menu: the pause menu's own button list is group
    // "PAUSE_LIST", index 0 is Resume (real action: `close pausedmenu`), index 1 is
    // Options (real action: `open pc_options_video_ingame; close pausedmenu`). We
    // intercept exactly that button+press combination, one level higher than the
    // old design -- the real Options screen is never entered at all in this flow.
    // pausedmenu.menu is the ONE real asset both Campaign's and Survival's in-game
    // pause use (confirmed -- no separate per-mode pause menu exists), so this
    // single check already covers "any in-game pause."
    //
    // EXTENDED 2026-08-05 (live-reported: "options should work from any in game
    // options prompt") -- traced every real `open pc_options_video[_ingame]` call
    // site across the extracted .menu dump, not just the one already known. Two more
    // real entry points exist, both on the MAIN MENU (not in-game, but still a real
    // "Options" prompt a player can reach): `main_campaign.menu`'s own Options button
    // (group "CAMPAIGN_BUTTON_LIST", index 3, confirmed via its
    // `ui_buttonNavGroupCurrent 3`/`open pc_options_video;` action block) and
    // `main_specops.menu`'s own Options button (group "SPECOPS_BUTTON_LIST", index 5,
    // same confirmation method) -- Special Ops is this game's real internal name for
    // the Campaign/Survival hub screen. No multiplayer main-menu asset exists in this
    // extracted dump at all (likely lives in a different, un-extracted fastfile) --
    // not checked either way, and out of scope regardless per CLAUDE.md's SP/Survival
    // focus. Every OTHER `open pc_options_video` call site found in the dump is purely internal
    // tab-switching within the options screen itself (Video<->Audio<->Controls etc.),
    // not a new external entry point, so intentionally not added here.
    char focusedGroup[128] = {};
    int focusedIndex = -1, siblingCount = -1;
    bool haveFocus = TryGetRealFocusedGroupAndIndex(focusedGroup, sizeof(focusedGroup), focusedIndex, siblingCount);
    bool onPauseMenuOptionsButton = haveFocus && _stricmp(focusedGroup, "PAUSE_LIST") == 0 && focusedIndex == 1;
    bool onCampaignMenuOptionsButton = haveFocus && _stricmp(focusedGroup, "CAMPAIGN_BUTTON_LIST") == 0 && focusedIndex == 3;
    bool onSpecOpsMenuOptionsButton = haveFocus && _stricmp(focusedGroup, "SPECOPS_BUTTON_LIST") == 0 && focusedIndex == 5;
    bool onAnyRealOptionsButton = onPauseMenuOptionsButton || onCampaignMenuOptionsButton || onSpecOpsMenuOptionsButton;
    bool openOptionsRequestedEdge = onAnyRealOptionsButton && selectEdge && g_modConfig.useCustomOptionsScreen;

    if (CustomOptionsMenu_TickInput(openOptionsRequestedEdge,
                                      upEdge, downEdge, leftEdge, rightEdge, selectEdge, backEdge,
                                      tabPrevEdge, tabNextEdge)) {
        // Claimed entirely this tick -- still update the held-state trackers below so
        // edge detection stays correct next tick, but skip every ForwardKeyToMenu call
        // for D-pad/A (the real native menu must see none of this while our own system
        // owns it). B's own real ESC-forward is separately guarded in
        // InjectControllerMenuBack via CustomOptionsMenu_IsOpen().
        g_menuNavUpHeld = upHeld;
        g_menuNavDownHeld = downHeld;
        g_menuNavLeftHeld = leftHeld;
        g_menuNavRightHeld = rightHeld;
        g_menuNavSelectHeld = selectHeld;
        g_menuNavTabPrevHeld = tabPrevHeld;
        g_menuNavTabNextHeld = tabNextHeld;
        return;
    }
    g_menuNavTabPrevHeld = tabPrevHeld;
    g_menuNavTabNextHeld = tabNextHeld;

    if (upHeld != g_menuNavUpHeld) {
        ForwardKeyToMenu(kLocalClientIndex, kKeyPrevItem, upHeld ? 1 : 0);
        g_menuNavUpHeld = upHeld;
    }
    if (downHeld != g_menuNavDownHeld) {
        ForwardKeyToMenu(kLocalClientIndex, kKeyNextItem, downHeld ? 1 : 0);
        g_menuNavDownHeld = downHeld;
    }
    if (leftHeld != g_menuNavLeftHeld) {
        ForwardKeyToMenu(kLocalClientIndex, kKeyLeftNav, leftHeld ? 1 : 0);
        g_menuNavLeftHeld = leftHeld;
    }
    if (rightHeld != g_menuNavRightHeld) {
        ForwardKeyToMenu(kLocalClientIndex, kKeyRightNav, rightHeld ? 1 : 0);
        g_menuNavRightHeld = rightHeld;
    }
    if (selectHeld != g_menuNavSelectHeld) {
        ForwardKeyToMenu(kLocalClientIndex, kKeyEnter, selectHeld ? 1 : 0);
        g_menuNavSelectHeld = selectHeld;
    }
    // Friends (2026-08-01) -- see the big comment above SendSyntheticF. Fires on Y's
    // rising edge only (not a held/repeat key), same convention as a real keypress.
    bool yHeld = IsPhysicalHeld(PhysicalInput::Y, buttons, leftTrigger, rightTrigger);
    if (yHeld && !g_menuNavYHeld) {
        SendSyntheticF();
    }
    g_menuNavYHeld = yHeld;

    // Game Summary (2026-08-01) -- see SendSyntheticG. X isn't used for anything
    // else in the menu-nav context, so no conflict.
    bool xHeld = IsPhysicalHeld(PhysicalInput::X, buttons, leftTrigger, rightTrigger);
    if (xHeld && !g_menuNavXHeld) {
        SendSyntheticG();
    }
    g_menuNavXHeld = xHeld;

    // Leaderboards (2026-08-01) -- see SendSyntheticF1. The physical Back/Select/
    // View button (XInput's own BACK bit) isn't used for anything else in this
    // project at all, so no conflict.
    bool backButtonHeld = IsPhysicalHeld(PhysicalInput::Back, buttons, leftTrigger, rightTrigger);
    if (backButtonHeld && !g_menuNavBackButtonHeld) {
        SendSyntheticF1();
    }
    g_menuNavBackButtonHeld = backButtonHeld;
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

// ---- Menu highlighted-item tracking (2026-08-01, menu-glyph work, issue #48) ------
//
// User's own framing: "highlighted entries must get the appropriate glyph next to
// them" (e.g. an A glyph next to whichever main-menu tile currently has focus) --
// and the correction that this project ALREADY has the RE needed for this, rather
// than it being a fresh unknown: issue #22's own decompile of FUN_006253d0/
// FUN_00625290 (the real native "next item"/"previous item" functions D-pad
// navigation already forwards into) found a real per-list focus-index field at
// `*(int*)(*param_1 + 0x68 + column*4)`, plus a real item-count field (`param_1[0x2a]`)
// and item-pointer array (`param_1[0x2b]`) on the same struct. What issue #22 never
// captured is what that `param_1` pointer actually IS at any given moment -- it's an
// argument threaded through the call chain, not a fixed address. Re-decompiling
// FUN_004dfd30 (the real generic key-to-menu dispatcher this project's own
// ForwardKeyToMenu/FUN_004d9850 already calls into for every D-pad/A/B menu
// keypress -- same function issue #22's own switch-statement trace already came
// from) shows it receives this exact struct as its OWN `param_2` argument, and
// passes it straight through to FUN_006253d0/FUN_00625290 unchanged. Hooking
// FUN_004dfd30 directly (a hook this project has never installed before -- it was
// previously only ever CALLED INTO, via ForwardKeyToMenu, never intercepted) lets
// this project observe and cache that same pointer for its own later use (reading
// the current focus index at render time), with zero risk to the real dispatch
// logic since the hook only reads an argument and forwards everything unmodified.
namespace {
using FUN_004dfd30_t = void(__cdecl*)(int* param_1, int* param_2, unsigned param_3, int param_4);
// Declared as plain void* (not FUN_004dfd30_t) so its address matches MinHook's own
// LPVOID* out-parameter directly, same convention every other hook in this file
// uses (e.g. g_orig_0061f6f0/g_orig_0057de60) -- cast back to the real function type
// only at the call site inside Hook_004dfd30 below.
void* g_orig_004dfd30 = nullptr;
// Cached raw pointer to whatever menu item-list struct most recently received a
// forwarded key -- NOT validated/dereferenced here, only stored. Consumers must
// treat it as untrusted (validate via LooksLikeValidPointer, wrap reads in SEH)
// since it can go stale the instant the menu that owned it closes.
void* g_lastMenuListStruct = nullptr;

void __cdecl Hook_004dfd30(int* param_1, int* param_2, unsigned param_3, int param_4)
{
    g_lastMenuListStruct = param_2;
    reinterpret_cast<FUN_004dfd30_t>(g_orig_004dfd30)(param_1, param_2, param_3, param_4);
}
} // namespace

// Read-only diagnostic (2026-08-01, first live-test pass for the mechanism above,
// same "diagnostic before feature" convention this project uses for every other new
// struct-field read -- see e.g. issue #22's own font-struct diagnostic). Reads the
// cached list struct's real focus-index/item-count fields and logs them, deduped by
// value so it doesn't spam every frame -- confirms live whether this struct pointer
// and field-offset theory are actually correct before anything draws a glyph off of
// them. Was gated on g_modConfig.glyphIconOverlayEnabled (same toggle as the rest of
// the custom-hint-overlay work); that flag was REMOVED 2026-08-16 (issue #74 root-cause
// fix -- see mod_config.h), so this now reuses hudGlyphPositionLogging instead (same
// "investigation scaffolding, default off" intent) rather than logging unconditionally
// forever with no toggle at all.
// Extracted 2026-08-01 from LogMenuFocusDiagnosticIfChanged's own inline reads
// (below) so the highlighted-item A-glyph feature can reuse the exact same,
// already-confirmed-live struct-field theory rather than duplicating it.
// Wrapped in SEH -- never let an unconfirmed offset crash the game.
bool TryGetCurrentMenuFocusIndex(int& outIndex, int& outItemCount)
{
    if (!LooksLikeValidPointer(reinterpret_cast<uintptr_t>(g_lastMenuListStruct))) return false;
    __try {
        int* listStruct = static_cast<int*>(g_lastMenuListStruct);
        int* nested = *reinterpret_cast<int**>(listStruct);
        if (!LooksLikeValidPointer(reinterpret_cast<uintptr_t>(nested))) return false;
        outIndex = *reinterpret_cast<int*>(reinterpret_cast<char*>(nested) + 0x68);
        outItemCount = listStruct[0x2a];
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

void LogMenuFocusDiagnosticIfChanged()
{
    if (!g_modConfig.hudGlyphPositionLogging) return;
    static void* s_lastLoggedStruct = nullptr;
    static int s_lastLoggedFocus = INT_MIN;
    int focusIndex = 0, itemCount = 0;
    if (!TryGetCurrentMenuFocusIndex(focusIndex, itemCount)) return;
    if (g_lastMenuListStruct != s_lastLoggedStruct || focusIndex != s_lastLoggedFocus) {
        s_lastLoggedStruct = g_lastMenuListStruct;
        s_lastLoggedFocus = focusIndex;
        char buf[160];
        sprintf_s(buf, "[menu-focus-diag] listStruct=%p focusIndex=%d itemCount=%d",
                   g_lastMenuListStruct, focusIndex, itemCount);
        LogFromController(buf);
    }
}

// ---- Special Ops flow detection via the real OpenMenuByName call (2026-08-01) -----
//
// Live-reported: a stale corner hint ("Friends") kept showing on modal popups inside
// the Special Ops flow ("Choose Game Mode", and the on-disk-vs-DLC-content picker
// one level deeper) that either have no corner hint of their own, or have one that
// renders through a path this project can't currently hook (confirmed via
// `.menu` file inspection: `popmenu_specops_survival.menu` DOES define its own
// always-visible "@PLATFORM_BACK_SHORTCUT" itemDef, but it never once fired through
// Hook_DrawGlyphText across a full live test -- most likely the still-unidentified
// "System A" itemDef draw path known_issues.md issue #38 already flagged as a
// separate open problem, distinct from the "System B" FUN_00690c80 path this
// project's hook actually sees). A prior attempt to detect "am I inside a nested
// modal" via the D-pad focus-tracking hook (g_lastMenuListStruct) was confirmed
// LIVE to not apply here at all -- Special Ops' tile-row navigation doesn't appear
// to go through the same generic listbox dispatch (FUN_004dfd30) that vertical
// text lists do, so that signal never changed across the whole flow regardless of
// input method.
//
// Simpler, more direct fix per explicit user direction: detect Special Ops opening
// via the one real, universal, input-method-agnostic signal that exists for this --
// `FUN_00544a50` (OpenMenuByName), confirmed via this project's own earlier RE
// (re_notes/iw5sp.md) to be the single real function EVERY menu transition goes
// through, regardless of whether it was triggered by mouse or controller. Hooking
// it directly (first time -- previously only referenced, never intercepted) and
// checking the opened menu's own name against the real file names found in the
// `.menu` corpus ("main_specops", "popmenu_specops_*") gives a simple, exact
// "are we currently inside the Special Ops flow" flag -- while true, hide any
// Friends hint that fires and always show this project's own synthetic Back
// instead; everywhere else, default behavior (the existing span-based Back/Friends
// handling) is untouched.
bool g_inSpecOpsFlow = false;

// ---- Highlighted-item A-glyph investigation (2026-08-01) --------------------------
//
// Per explicit user direction: draw an A-glyph icon after whichever menu item is
// CURRENTLY HIGHLIGHTED, in real console-style vertical LIST menus (confirmed
// distinct from the title screen's tile row, which uses a different, untraced
// navigation mechanism and isn't in scope here). The focus INDEX itself is already
// reliably readable (TryGetCurrentMenuFocusIndex, confirmed live all session via
// real D-pad navigation), but knowing the index alone isn't enough -- this also
// needs to know which THIS FRAME'S Hook_DrawGlyphText call corresponds to that same
// item, to find its real screen position. Scoped (per explicit user agreement) to
// non-scrolled, short lists only for this first pass -- a scrolled list's visible
// window doesn't map 1:1 to absolute item index without also knowing the scroll
// offset, which hasn't been located yet.
//
// Approach: count plain, no-"^N...^7"-span, fonts/smallFont text draws THIS FRAME
// (candidate list items -- corner hints like Back/Friends always DO have a span, so
// this naturally excludes them) in the order they're drawn, which -- for a
// non-scrolled list -- is confirmed live to be a stable, predictable per-item order
// (e.g. the real Special Ops submenu: "FIND ONLINE MATCH" always first, "PRIVATE
// ONLINE MATCH" always second, etc., at fixed 45px row spacing). If ordinal N this
// frame matches the confirmed focus index, that draw call's own (param_2, param_3)
// plus its measured text width gives the real on-screen end-of-text position to
// draw the A icon at. DIAGNOSTIC-ONLY for now -- logs the correlation instead of
// drawing anything, so it can be confirmed live before building the actual feature
// on top of an unverified assumption (this project's own standing methodology).
int g_menuListItemOrdinalThisFrame = 0;

// ---- Auto-mantle's real ledge-availability gate (2026-08-03, issue #62 follow-up) -
//
// g_mantleHintDrawnThisFrame accumulates during THIS frame's Hook_DrawGlyphText
// calls (set true wherever isMantleHint fires, see that block's own comment) --
// rendering and the gameplay-tick InjectControllerButtons run on different hook
// points, so a value can't be read directly across them within the same frame.
// Committed once per frame (in ResetMenuListItemOrdinalForFrame, alongside every
// other per-frame reset) into a last-seen TIMESTAMP rather than a plain last-frame
// bool -- upgraded 2026-08-03 after live-reporting the strict same-frame version
// never fired auto-mantle at all. A single-frame bool assumes render and gameplay-
// tick hooks stay in lockstep (one committed value per gameplay tick); if the
// render hook (EndScene) and the gameplay tick (this old engine's own locked-rate
// simulation) aren't actually 1:1 -- e.g. multiple gameplay ticks landing between
// two renders, or vice versa -- a bool can go stale for an entire tick right when
// auto-mantle checks it, silently missing a real, currently-showing hint.
// IsMantleHintCurrentlyShowing() below instead accepts anything seen within a
// short grace window, tolerating that skew without reintroducing the original
// "fires with no real ledge" regression (see the 750ms auto-mantle cooldown for
// why a slightly stale "yes" still can't cause a wrong repeated trigger).
bool g_mantleHintDrawnThisFrame = false;
DWORD g_mantleHintLastSeenMs = 0;
constexpr DWORD kMantleHintGraceMs = 400;
bool IsMantleHintCurrentlyShowing() { return (GetTickCount() - g_mantleHintLastSeenMs) <= kMantleHintGraceMs; }

// ---- Automatic list-glyph positioning (2026-08-03, issue #51 follow-up) -----------
//
// Every hand-built kManualGlyphPositions entry this session (CAMPAIGN_BUTTON_LIST's
// nested-modal fix, the main-menu row, SWF_COMMON_POPUP_NAME's bottom-anchor variant,
// SO_LEVELS_BUTTON_LIST's depth=4 reuse, ...) was ultimately built from the SAME three
// live-reliable signals: TryGetRealFocusedGroupAndIndex() (real focused index, direct
// itemDef memory read, no script-cooperation dependency), SumDirectIndexedGlyphWidthsBefore()
// (real measured text-end position, no per-screen estimate needed), and this frame's
// own real (param_2, param_3) draw calls. Every regression this session (game_select_button's
// off-by-one, SWF_COMMON_POPUP_NAME's bottom-anchor, SO_LEVELS_BUTTON_LIST's centered-vs-
// right-aligned confusion, the corner-hint slot-starvation bug) came from hand-copying a
// SNAPSHOT of these signals into a static table entry instead of re-deriving them live
// every frame -- a static table can only ever be as correct as the one screen state it was
// calibrated against, and every new reuse of a shared group name needs its own entry, its
// own live capture, its own round of live-testing. This block re-derives the position from
// those same three signals FRESH every frame instead, needing no per-screen table entry at
// all.
//
// User-requested (2026-08-03): keep kManualGlyphPositions / TryGetManualGlyphPosition and
// the ordinal-fallback path's own draw logic FULLY INTACT (not deleted) behind this same
// toggle, until this automatic path is confirmed reliable across the screens the manual
// table already covers. Flip to false to fall back to the old behavior instantly; nothing
// about the old code paths was changed.
//
// DISABLED 2026-08-03, same day, after a live test: user-reported "it regressed, this
// method is proven unreliable and per menu screenshot based setup is the correct method."
// Reverted to false per that direct verdict. Code kept in place (not deleted) in case a
// future attempt wants to build on the run-detection idea, but the manual per-screen
// table + live-measurement calibration approach is the standing method going forward,
// not this one.
constexpr bool kUseAutomaticListGlyphPositioning = false;

struct AutoGlyphCandidate { float y; float textEndX; };
constexpr int kMaxAutoGlyphCandidates = 48;
AutoGlyphCandidate g_autoGlyphCandidates[kMaxAutoGlyphCandidates];
int g_autoGlyphCandidateCount = 0;

// Finds the longest run of candidates (already collected this frame by
// Hook_DrawGlyphText, sorted here by Y) whose consecutive Y-spacing is constant
// (within a small tolerance) -- the signature of a real, evenly-spaced list, which
// unrelated decorative/preview-panel text essentially never coincidentally matches.
// realIndex then indexes directly into that run (0-based). No group-specific
// calibration, no static table, no per-screen offset -- self-calibrates from
// whatever this exact frame actually drew. Returns false (don't draw) if realIndex
// falls outside the best run's length, e.g. the focused item scrolled out of the
// currently-drawn range this frame, rather than guessing a wrong position.
bool TryGetAutomaticGlyphPosition(int realIndex, float& outX, float& outY)
{
    int n = g_autoGlyphCandidateCount;
    if (realIndex < 0 || n < 2) return false;

    AutoGlyphCandidate sorted[kMaxAutoGlyphCandidates];
    memcpy(sorted, g_autoGlyphCandidates, sizeof(AutoGlyphCandidate) * n);
    std::sort(sorted, sorted + n, [](const AutoGlyphCandidate& a, const AutoGlyphCandidate& b) {
        return a.y < b.y;
    });

    constexpr float kMinRealListStepPx = 20.0f; // filters out near-duplicate Y from unrelated overlapping draws
    constexpr float kStepToleragePx = 4.0f;
    int bestStart = 0, bestLen = 1;
    int curStart = 0, curLen = 1;
    float curStep = -1.0f;
    for (int i = 1; i < n; ++i) {
        float step = sorted[i].y - sorted[i - 1].y;
        if (step > kMinRealListStepPx && (curStep < 0.0f || fabsf(step - curStep) < kStepToleragePx)) {
            if (curStep < 0.0f) curStep = step;
            ++curLen;
        } else {
            if (curLen > bestLen) { bestLen = curLen; bestStart = curStart; }
            curStart = i;
            curLen = 1;
            curStep = -1.0f;
        }
    }
    if (curLen > bestLen) { bestLen = curLen; bestStart = curStart; }

    if (bestLen < 2 || realIndex >= bestLen) return false;
    const AutoGlyphCandidate& c = sorted[bestStart + realIndex];
    constexpr float kIconGapAfterText = 12.0f; // matches the ordinal-fallback path's own established gap
    constexpr float kMenuHintVerticalNudge = -18.0f; // matches every other corner/list hint in this file
    outX = c.textEndX + kIconGapAfterText;
    outY = c.y + kMenuHintVerticalNudge;
    return true;
}

namespace {
using OpenMenuByNameFn = int(__cdecl*)(void* ctx, const char* name);
void* g_orig_00544a50 = nullptr;

int __cdecl Hook_00544a50(void* ctx, const char* name)
{
    __try {
        if (LooksLikeValidPointer(reinterpret_cast<uintptr_t>(name))) {
            g_inSpecOpsFlow = (_strnicmp(name, "main_specops", 12) == 0) ||
                               (_strnicmp(name, "popmenu_specops_", 16) == 0);
            // Live-reported 2026-08-01: no observed change in behavior after wiring
            // this up -- first thing to confirm is whether this hook is even firing
            // for real menu transitions at all, and with what name, before assuming
            // the prefix-match logic itself is wrong. Deduped by name so it doesn't
            // spam every re-open of the same menu.
            static char s_lastLoggedName[64] = {};
            if (strncmp(s_lastLoggedName, name, sizeof(s_lastLoggedName) - 1) != 0) {
                strncpy_s(s_lastLoggedName, name, _TRUNCATE);
                char buf[128];
                sprintf_s(buf, "[open-menu-diag] OpenMenuByName(\"%s\") -> g_inSpecOpsFlow=%d",
                           name, static_cast<int>(g_inSpecOpsFlow));
                LogFromController(buf);
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        // Never let this crash the game over an unexpected name-pointer shape.
    }
    return reinterpret_cast<OpenMenuByNameFn>(g_orig_00544a50)(ctx, name);
}
} // namespace

// ---- Local-var lookup trace, FUN_00552e70 (2026-08-01) ---------------------------
//
// Continuation of the highlighted-item A-glyph investigation after the ordinal-count
// approach above was confirmed live to break whenever a background list (e.g. the
// Special Ops root screen) keeps drawing its own items while a DIFFERENT list is
// actually focused on top of it -- ordinals from the wrong list were being compared
// against the wrong focusIndex/itemCount pair. Per explicit user redirect ("why not
// re the menu files directly to find exactly what to look for"), re-read
// main_specops.menu directly and found a real per-list SCOPING mechanism already
// used pervasively in the compiled `.menu` corpus: `ui_buttonNavGroupName` (which
// NAMED list is currently active), `ui_buttonNavGroupCurrent`/`ui_buttonNavGroupOffset`
// (selected index / scroll offset within it), and `ui_swf_selection` (the literal
// name of the selected item) -- all set via `setLocalVarInt`/`setLocalVarString` and
// read via `localvarint()`/`localvarstring()` expression calls.
//
// A raw `grep -a` for these literal name strings across the whole `iw5sp.exe` file
// came back with ZERO matches -- initially looked like the same "compiled away, no
// runtime string" dead end that blocked the "open" keyword investigation above. But
// checking OpenAssetTools' own source (github.com/Laupetin/OpenAssetTools, the actual
// open-source implementation of this exact `.menu` compiler/linker, so this is a
// documented format, not a guess) shows `SetLocalVarData::localVarName` is declared
// as a plain `const char*` in its own runtime asset struct (src/Common/Game/IW5/
// IW5_Assets.h) -- NOT a hash. The name strings are real, uncompressed C strings --
// they just live in the COMPILED `.ff` ZONE DATA loaded into the process heap once
// that menu asset is actually loaded, not in the executable's own static .rdata
// string table. That's exactly why the raw-exe grep found nothing: wrong location,
// not a wrong theory.
//
// This reframes an already-decompiled function as a strong candidate for the real
// native "find local var entry by name" lookup: FUN_00552e70(ctx, name) linearly
// scans an array of pointers (count at *(ctx+0xa8), array at *(ctx+0xac)), string-
// comparing each entry's own first field against `name` via FUN_00463bb0, returning
// the matching array slot. That shape -- "count + pointer array + per-entry string
// compare, called with a runtime name argument" -- matches a local-var-by-name table
// lookup called with literal names like "ui_buttonNavGroupName" far better than a
// coincidence. Hooking it read-only (forward to the real trampoline completely
// unmodified, log ctx/name/result afterward, same "log then forward unchanged"
// convention as every other diagnostic hook in this file) is the direct way to
// confirm this live rather than assuming further from static analysis alone.
namespace {
using FindLocalVarFn = void*(__cdecl*)(void* ctx, const char* name);
void* g_orig_00552e70 = nullptr;

void* __cdecl Hook_00552e70(void* ctx, const char* name)
{
    void* result = reinterpret_cast<FindLocalVarFn>(g_orig_00552e70)(ctx, name);
    __try {
        if (LooksLikeValidPointer(reinterpret_cast<uintptr_t>(name))) {
            static char s_lastLoggedName[64] = {};
            if (strncmp(s_lastLoggedName, name, sizeof(s_lastLoggedName) - 1) != 0) {
                strncpy_s(s_lastLoggedName, name, _TRUNCATE);
                char buf[160];
                sprintf_s(buf, "[localvar-lookup-diag] ctx=%p name=\"%.48s\" -> entry=%p",
                           ctx, name, result);
                LogFromController(buf);
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        // Never let this crash the game over an unexpected name-pointer shape.
    }
    return result;
}
} // namespace

// ---- localvarstring getter hook, FUN_00613ac0 (2026-08-01) -----------------------
//
// Direct follow-up to the FUN_00552e70 crash above. Two sibling forks independently
// traced the real opcode-specific dispatch: the menu-VM's expression evaluator
// (FUN_0054cc50, an 11KB switch keyed on the same opcode numbers OAT's own
// `g_expFunctionNames[]` uses) has cases 0x4f/0x50/0x51/0x52 = 79/80/81/82 =
// localvarint/localvarbool/localvarfloat/localvarstring, each calling a genuinely
// SINGLE-CALLER getter -- FUN_00613b70/b20/bb0/ac0 respectively -- unlike
// FUN_00552e70's 12 call sites spanning dvar/keybinding/tablelookup/etc. Confirmed
// via direct Ghidra re-decompile of FUN_00613ac0 itself (not inferred from its
// siblings' shape, since its call site `FUN_00613ac0(local_88, 0x20)` visibly
// differs from the other three's zero-visible-arg calls):
//
//   void __thiscall FUN_00613ac0(undefined4 param_1, undefined4 param_2, undefined4 param_3)
//   {
//       int *in_EAX; undefined4 *unaff_ESI;
//       if (*in_EAX == 2) {
//           ... resolve in_EAX[1] via FUN_00532ad0/FUN_004566d0 to a name string ...
//           *unaff_ESI = 2;
//           unaff_ESI[1] = FUN_0050f570(resolvedName, param_2 /*buffer*/, param_3 /*size*/);
//           return;
//       }
//       *unaff_ESI = 2; unaff_ESI[1] = &DAT_0085f9c3; // empty-string fallback
//   }
//
// ECX=param_1 (context, __thiscall), EAX=a name-entry pointer populated by the
// caller's own `FUN_00413ef0(param_4, &local_148)` just before the call, ESI=an
// output-operand pointer the callee writes `{2, resolvedStringPtr}` through --
// none of these three are expressible in a clean C signature, exactly the same
// implicit-register shape as this project's existing, proven-safe Hook_0057de60/
// Hook_0061f6f0 naked hooks. A plain MinHook C-signature detour would clobber
// EAX/ESI and almost certainly crash the same way FUN_00552e70's did -- this uses
// the same "stash to globals, tail-jump into the trampoline with everything
// restored, redirect the return address to log afterward" technique as
// Hook_0061f6f0 above instead.
//
// DIAGNOSTIC-ONLY for now: logs the resolved VALUE string (deduped) after the real
// call completes, to confirm live that this is the right function and see real
// ui_buttonNavGroupName/ui_swf_selection-shaped values before building anything on
// top of it. Does not yet capture the NAME being looked up (would require
// replicating FUN_00532ad0/FUN_004566d0's own resolution logic on in_EAX) --
// deferred to a follow-up pass once this hook itself is confirmed safe live.
namespace {
void* g_orig_00613ac0 = nullptr;
uintptr_t g_localVarStringInEax = 0;
uintptr_t g_localVarStringInEcx = 0;
uintptr_t g_localVarStringOutEsi = 0;
uintptr_t g_localVarStringRealRetAddr = 0;

// Live-confirmed 2026-08-01: this hook's resolved values are a mix of real
// ui_buttonNavGroupName-shaped names ("SPECOPS_BUTTON_LIST"), real
// ui_swf_selection-shaped names ("SPECOPS_BUTTON_LIST_3"), and unrelated noise
// from every OTHER localvarstring() call sharing this same opcode (localized
// "@MENU_..." description strings, "SWF_POPUP_BUTTON_NAME(...)"-formatted popup
// text, etc.) -- this hook can't distinguish WHICH query produced a given value
// without also resolving the name argument (in_EAX), not yet attempted. Instead
// of trying to classify "is this a nav-group-name," classify by SHAPE: any value
// ending in "_<digits>" is treated as a ui_swf_selection-style value, giving BOTH
// the owning group's base name AND the selected index in one parse -- removes the
// need to separately track/trust a bare group-name value at all. Anything else
// (including a bare "SPECOPS_BUTTON_LIST" with no suffix) is ignored here.
struct NavGroupCacheEntry { char name[64]; int maxIndexSeen; };
constexpr int kNavGroupCacheSize = 4;
NavGroupCacheEntry g_navGroupCache[kNavGroupCacheSize] = {};
int g_navGroupCacheNext = 0; // round-robin claim slot -- a rolling "recently active groups" cache, not a registry

char g_currentSelGroupName[64] = {};
int g_currentSelIndex = -1;

// Declared here (rather than down by IsInsideSpecOpsNestedModal, which reads it)
// so UpdateNavGroupTrackingFromResolvedValue below can clear it directly -- see
// the big comment right before that clear for why this needed to move.
bool g_specOpsModalSticky = false;

// Shared by both real ui_swf_selection shapes below (plain "<name>_<digit>" and the
// macro-call "SWF_POPUP_BUTTON_NAME(<name>,<index>)") -- factored out (2026-08-02,
// full .menu-file audit) so a future third shape only needs to parse out
// (groupName, index) and call this, rather than re-implementing the sticky-clear +
// nav-group-cache bookkeeping a second time. baseName must already be NUL-terminated
// and within g_currentSelGroupName's size limit -- callers are expected to have
// validated that before calling.
void ApplyResolvedSelection(const char* baseName, int index)
{
    strncpy_s(g_currentSelGroupName, baseName, _TRUNCATE);
    g_currentSelIndex = index;

    // Live-reported 2026-08-01: the modal-sticky clear that lived in
    // IsInsideSpecOpsNestedModal (keyed on getfocuseditemname()) only fired when
    // that call's OWN result actually changed -- but getfocuseditemname() is
    // evidently cached/event-driven, not re-evaluated every frame, so returning
    // from the modal to the SAME index you left from never produces a fresh value
    // to clear against. Symptom: Back kept showing on the real root list
    // (Survival/Special Ops hub) until the player pressed up/down to actually
    // change the highlighted item. ui_swf_selection (this function's own value),
    // by contrast, is confirmed live to re-fire continuously every frame a list is
    // on screen (repeated identical captures seen with zero navigation in
    // between) -- a far more reliable "we are definitely back on a real list"
    // signal. Clearing here instead, on every parse of a real (non-"SWF_"-
    // prefixed) game-specific group name, regardless of whether the index
    // actually changed.
    if (_strnicmp(baseName, "SWF_", 4) != 0) {
        g_specOpsModalSticky = false;
    }

    for (int slot = 0; slot < kNavGroupCacheSize; ++slot) {
        if (strcmp(g_navGroupCache[slot].name, baseName) == 0) {
            if (index > g_navGroupCache[slot].maxIndexSeen) g_navGroupCache[slot].maxIndexSeen = index;
            return;
        }
    }
    NavGroupCacheEntry& fresh = g_navGroupCache[g_navGroupCacheNext];
    g_navGroupCacheNext = (g_navGroupCacheNext + 1) % kNavGroupCacheSize;
    strncpy_s(fresh.name, baseName, _TRUNCATE);
    fresh.maxIndexSeen = index;
}

// Full .menu-file audit (2026-08-02, forked across the whole 319-file ui_swf_selection
// corpus) found exactly TWO real runtime shapes ui_swf_selection ever actually takes
// (a third, "SWF_COMMON_POPUP_NAME_<digit>", already matches the plain shape below and
// needs no new code) -- confirmed via live proxy_d3d9.log capture, not just the .menu
// source: plain "<name>_<digit>" (handled below), and a macro-call-shaped
// "SWF_POPUP_BUTTON_NAME(<name>,<index>)" (real captured examples:
// "SWF_POPUP_BUTTON_NAME(ASR_POPUP,10)", "SWF_POPUP_BUTTON_NAME(LB_FILTER,0)") used by
// difficulty popups, friend/clan/recent context menus, leaderboard filter popups, and
// both character-select screens. This is a real, literal runtime string -- NOT a macro
// that expands to something else at runtime, confirmed by grepping actual resolved
// values in the log. Tried FIRST since it can never collide with the plain shape (it
// ends in ')', which the plain parser's own trailing-digit check already safely
// rejects -- confirmed this does NOT silently misparse today, it just falls through
// unrecognized).
bool TryParsePopupButtonNameSelection(const char* value, size_t len, char* outBaseName, int& outIndex)
{
    constexpr char kPrefix[] = "SWF_POPUP_BUTTON_NAME(";
    constexpr size_t kPrefixLen = sizeof(kPrefix) - 1;
    if (len <= kPrefixLen || value[len - 1] != ')') return false;
    if (_strnicmp(value, kPrefix, kPrefixLen) != 0) return false;

    const char* inner = value + kPrefixLen;
    size_t innerLen = len - kPrefixLen - 1; // drop the trailing ')'
    const char* comma = static_cast<const char*>(memchr(inner, ',', innerLen));
    if (!comma || comma == inner) return false;

    size_t groupLen = comma - inner;
    if (groupLen == 0 || groupLen >= sizeof(g_currentSelGroupName)) return false;
    const char* indexStr = comma + 1;
    size_t indexLen = (inner + innerLen) - indexStr;
    if (indexLen == 0 || !isdigit(static_cast<unsigned char>(indexStr[0]))) return false;

    memcpy(outBaseName, inner, groupLen);
    outBaseName[groupLen] = '\0';
    outIndex = atoi(indexStr);
    return true;
}

void UpdateNavGroupTrackingFromResolvedValue(const char* value)
{
    size_t len = strlen(value);
    if (len == 0 || len >= sizeof(g_currentSelGroupName)) return;

    char macroBaseName[64];
    int macroIndex = 0;
    if (TryParsePopupButtonNameSelection(value, len, macroBaseName, macroIndex)) {
        ApplyResolvedSelection(macroBaseName, macroIndex);
        return;
    }

    size_t i = len;
    while (i > 0 && isdigit(static_cast<unsigned char>(value[i - 1]))) --i;
    if (i == len || i == 0 || value[i - 1] != '_') return; // no trailing "_<digits>" -- not a selection value
    size_t baseLen = i - 1;
    if (baseLen == 0 || baseLen >= sizeof(g_currentSelGroupName)) return;
    int index = atoi(value + i);

    char baseName[64];
    memcpy(baseName, value, baseLen);
    baseName[baseLen] = '\0';
    ApplyResolvedSelection(baseName, index);
}

// Returns the most recently observed (group, index) pair, plus that group's own
// highest-observed index (NOT a reliable item COUNT -- it under-counts until the
// player has actually visited the group's last item at least once this session,
// a known limitation; used as one of two independent cross-check signals below,
// not the sole gate). Kept as its own function so future callers don't need to
// know the parsing/cache details above.
bool TryGetCurrentSelectionGroupAndIndex(int& outIndex, int& outMaxIndexSeen)
{
    if (g_currentSelGroupName[0] == '\0' || g_currentSelIndex < 0) return false;
    for (int slot = 0; slot < kNavGroupCacheSize; ++slot) {
        if (strcmp(g_navGroupCache[slot].name, g_currentSelGroupName) == 0) {
            outIndex = g_currentSelIndex;
            outMaxIndexSeen = g_navGroupCache[slot].maxIndexSeen;
            return true;
        }
    }
    return false;
}

void LogLocalVarStringResult()
{
    __try {
        if (!LooksLikeValidPointer(g_localVarStringOutEsi)) return;
        int* out = reinterpret_cast<int*>(g_localVarStringOutEsi);
        if (out[0] != 2) return; // not the string-operand shape we expect -- skip rather than guess
        const char* value = reinterpret_cast<const char*>(out[1]);
        if (!LooksLikeValidPointer(reinterpret_cast<uintptr_t>(value))) return;
        UpdateNavGroupTrackingFromResolvedValue(value);
        static char s_lastLoggedValue[64] = {};
        if (strncmp(s_lastLoggedValue, value, sizeof(s_lastLoggedValue) - 1) != 0) {
            strncpy_s(s_lastLoggedValue, value, _TRUNCATE);
            char buf[128];
            sprintf_s(buf, "[localvarstring-diag] resolved value=\"%.80s\"", value);
            LogFromController(buf);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        // Never let this crash the game over an unexpected operand shape.
    }
}
} // namespace

__declspec(naked) void Hook_00613ac0()
{
    __asm {
        // Stash the two register args this call depends on (ECX=context,
        // EAX=name-entry pointer) before EAX gets reused as scratch below -- both
        // restored byte-for-byte immediately before the tail-jump. ESI (the
        // output-operand pointer the callee writes through) is never touched here
        // at all, so it reaches the trampoline completely unchanged -- stash a COPY
        // for the post-call logger to read afterward.
        mov dword ptr [g_localVarStringInEax], eax
        mov dword ptr [g_localVarStringInEcx], ecx
        mov dword ptr [g_localVarStringOutEsi], esi

        // Save the real return address, then repoint that same stack slot at our
        // own afterCall label -- in-place overwrite, not a push, so the __thiscall
        // stack shape below it (param_2/param_3) stays exactly what the trampoline
        // expects.
        mov eax, dword ptr [esp]
        mov dword ptr [g_localVarStringRealRetAddr], eax
        mov dword ptr [esp], offset afterCall

        // Restore EAX/ECX exactly as the real caller set them, then tail-jump (NOT
        // call) into the trampoline so it sees the byte-for-byte original frame.
        mov eax, dword ptr [g_localVarStringInEax]
        mov ecx, dword ptr [g_localVarStringInEcx]
        jmp dword ptr [g_orig_00613ac0]

    afterCall:
        // Trampoline's own `ret` landed us here with esp already restored to what
        // the real caller would see post-return. Preserve every register across
        // the log call, then resume the real caller directly.
        pushad
        call LogLocalVarStringResult
        popad
        jmp dword ptr [g_localVarStringRealRetAddr]
    }
}

// ---- getfocuseditemname() hook, FUN_00616230 (2026-08-01, issue #50 follow-up) ---
//
// A sibling research fork (dug into whether the FUN_00552e70/FUN_00613ac0 recipe
// generalizes) found this directly answers issue #50's still-parked "Friends
// persists under Special Ops modal" bug: `getfocuseditemname` is opcode 151/0x97 in
// the same menu-VM opcode space, dispatching to FUN_00616230 -- confirmed via
// FindCallers.java to have exactly 1 caller (the switch itself), same safe class as
// FUN_00613ac0. Its own decompile is even simpler -- genuinely zero input
// arguments:
//
//   void FUN_00616230(void) {
//       undefined4 *unaff_ESI;
//       uVar1 = FUN_00495de0();           // kMenuSystemContext global, already known
//       puVar2 = FUN_004c1220(uVar1);     // -> focused item's name-holding struct
//       ... *unaff_ESI = 2; unaff_ESI[1] = (the resolved name string, or a fallback);
//   }
//
// Confirmed via its own call site in FUN_0054cc50 (case 0x97: `FUN_00616230();
// FUN_006156b0();`, no preceding FUN_00413ef0 call) that EAX/ECX carry no meaningful
// input for this specific getter, unlike FUN_00613ac0 -- only ESI (output pointer)
// needs preserving. DIAGNOSTIC-ONLY for now: logs the resolved focused-item name
// (deduped) so the real values can be seen live (e.g. across a Special-Ops-modal
// open/close) before building any suppress/redirect logic on top of them -- same
// "confirm live before using" discipline as every other new hook this session.
namespace {
void* g_orig_00616230 = nullptr;
uintptr_t g_focusedItemOutEsi = 0;
uintptr_t g_focusedItemRealRetAddr = 0;
char g_focusedItemName[64] = {};

void LogFocusedItemNameResult()
{
    __try {
        if (!LooksLikeValidPointer(g_focusedItemOutEsi)) return;
        int* out = reinterpret_cast<int*>(g_focusedItemOutEsi);
        if (out[0] != 2) return;
        const char* value = reinterpret_cast<const char*>(out[1]);
        if (!LooksLikeValidPointer(reinterpret_cast<uintptr_t>(value))) return;
        strncpy_s(g_focusedItemName, value, _TRUNCATE);
        static char s_lastLoggedValue[64] = {};
        if (strncmp(s_lastLoggedValue, value, sizeof(s_lastLoggedValue) - 1) != 0) {
            strncpy_s(s_lastLoggedValue, value, _TRUNCATE);
            char buf[128];
            sprintf_s(buf, "[focused-item-diag] getfocuseditemname() -> \"%.80s\"", value);
            LogFromController(buf);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        // Never let this crash the game over an unexpected operand shape.
    }
}
} // namespace

// Shared "is THIS list's own selIndex genuinely the currently-focused item" check
// (2026-08-02) -- factored out of the highlighted-item A-glyph block below so the
// manual glyph-position table (see kManualGlyphPositions) can use the exact same
// name-based verification without needing a live text-draw call in scope. Reliable
// two ways: independently confirmed live via `sameObject=1` cross-checks against
// the raw menu-stack array (see known_issues.md issue #51's live-memory-analysis
// section) and via getfocuseditemname()'s own real name string, which cannot
// coincidentally match a background/dimmed layer's item (different group name).
bool GetGenuinelyFocusedGroupSelection(int& outIndex)
{
    int selIndex = -1, selMaxIndexSeen = -1;
    if (!TryGetCurrentSelectionGroupAndIndex(selIndex, selMaxIndexSeen)) return false;
    char expectedFocusedName[80] = {};
    sprintf_s(expectedFocusedName, "%s_%d", g_currentSelGroupName, selIndex);
    if (_stricmp(g_focusedItemName, expectedFocusedName) != 0) return false;
    outIndex = selIndex;
    return true;
}

// Manual per-screen/per-group A-glyph position table (2026-08-02, known_issues.md
// issue #51's "Nested-modal positioning bug"). Six Ghidra passes (static, -noanalysis)
// plus a live full-process-memory dump of the real print-command record (confirmed
// via MiniDumpWriteDump + a raw byte scan for the record's own real X/Y floats) all
// converge on the same conclusion: the engine's text-draw command queue carries
// ONLY rendering data (X, Y, font, X/Y scale, color, inline text) with zero back-
// reference to the itemDef that produced it -- confirmed empirically by reading the
// ENTIRE real 0x54-byte record for this exact popup's "Yes"/"No" buttons out of a
// live memory dump and finding every field accounted for by known rendering fields,
// none of them pointing into the itemDef heap range. This is not a "not found yet"
// gap -- the link does not exist at the data level, so no amount of further static
// or dynamic tracing will recover it. A manual, per-group calibrated position table
// is the correct architecture here, not a stopgap.
//
// Coordinates are REAL, not estimated: read directly from the live record for the
// quit-confirmation variant of this shared popup group (also used for the display-
// resize confirmation) -- "Yes" text drawn at (686.0, 603.0), "No" at (686.0, 653.0),
// both at font scale 1.5. The icon X offset is a fixed estimate past these short
// 2-4 letter labels (not a live text-width measurement, since the manual-position
// path doesn't have a real draw call in scope to measure) -- may want a small visual
// nudge after in-game confirmation.
// Explicit per-index positions rather than a uniform base+delta formula: the
// batch capture below found real lists that aren't evenly spaced in a single
// axis (right-aligned text with variable width, or genuinely irregular gaps),
// so a linear formula would silently be wrong for those. requiredDepth
// disambiguates the same literal group name reused at different nesting depths
// with a genuinely different on-screen position (e.g. LEVELS_BUTTON_LIST is
// reused for both the act's mission list and a deeper per-mission sub-list).
constexpr int kManualGlyphMaxItems = 16; // bumped from 15 (2026-08-03) for Survival's own 16-item map-select list
struct ManualGlyphEntry {
    const char* groupName;
    int requiredDepth; // -1 = match at any depth
    float iconOffsetX; // added to the item's own real X (append-after-text convention)
    int count;
    float itemX[kManualGlyphMaxItems];
    float itemY[kManualGlyphMaxItems];
    // bottomAnchorPopup (2026-08-03, issue #51 follow-up): opt-in only. When true,
    // TryGetManualGlyphPosition() shifts the focused item's own local index up by
    // (count - siblingCount) whenever this instance has fewer real items than
    // `count` -- for shared popup groups genuinely reused across variants with
    // different real item counts, bottom-anchored to the same grid (confirmed live
    // for SWF_COMMON_POPUP_NAME: "Choose Content Pack"'s 2 real items land on the
    // 3-item "Leave Lobby?" table's own index1/index2 rows, not index0/index1).
    // MUST default false: `game_select_button`'s own table entry uses a similar-
    // looking count/index scheme for a COMPLETELY different reason (a placeholder
    // slot 0 that's never a real sibling at all, see its own comment) -- applying
    // this same shift there corrupted an already-correct index by +1, a real,
    // shipped regression caught live the same day this field was added. Only ever
    // enable per-entry, never make this the default behavior.
    bool bottomAnchorPopup = false;
    // requiredTextSubstring (2026-08-16, issue #51 follow-up, user direction: "go off
    // the popup's internal text as another differentiator") -- opt-in disambiguator
    // for groups reused by multiple real popups sharing the same (groupName,
    // requiredDepth) with no other distinguishing signal (e.g.
    // SWF_COMMON_DESC_RESIZE_POPUP_NAME's "New Game overwrite"/Quit vs. "Restart
    // Mission" confirms, both depth=1). nullptr = no requirement, matches
    // unconditionally (the fallback/default entry for a group). When set, this entry
    // only matches while g_lastPopupBodyText (captured live in Hook_DrawGlyphText
    // from whatever real text is currently drawn at this popup family's own shared
    // X=686 convention) contains this substring, case-insensitive. Entries WITH a
    // requirement must be listed before the unconditional fallback for the same
    // (groupName, requiredDepth) -- see TryGetManualGlyphPosition's own matching
    // order.
    const char* requiredTextSubstring = nullptr;
};

// Case-insensitive substring search (2026-08-16) -- no Shlwapi dependency (StrStrIA)
// needed for this one small use. Plain, unoptimized O(n*m) scan; both strings here
// are always short (a captured HUD text line, a small literal disambiguator), so
// this is called at most a handful of times per frame, never a hot path.
bool ContainsSubstringCaseInsensitive(const char* haystack, const char* needle)
{
    if (!haystack || !needle || !needle[0]) return false;
    size_t haystackLen = strlen(haystack), needleLen = strlen(needle);
    if (needleLen > haystackLen) return false;
    for (size_t i = 0; i + needleLen <= haystackLen; ++i) {
        if (_strnicmp(haystack + i, needle, needleLen) == 0) return true;
    }
    return false;
}

// Live capture of whatever real text is CURRENTLY drawn at this popup family's own
// shared, established X=686 left-aligned convention (SWF_COMMON_DESC_RESIZE_POPUP_NAME/
// SWF_COMMON_POPUP_NAME/popmenu_difficulty/popup_friend_list_actions all confirmed
// fixed-left at this exact X regardless of label length -- see kManualGlyphPositions'
// own "KEY FINDING" comment below) -- used as an optional text-based disambiguator
// (requiredTextSubstring above) for groups whose (groupName, requiredDepth) alone
// isn't enough to tell two real popups apart. Filtered to text longer than 10
// characters so short Yes/No-style button labels (drawn at this SAME X) never
// overwrite a genuine description line -- popup descriptions in this game are
// consistently full sentences/phrases, buttons are consistently 2-4 letters.
// Updated directly from Hook_DrawGlyphText, no per-frame reset needed: a popup's own
// description text is drawn fresh every frame it's open with identical content, so
// this naturally stays current without needing the accumulate-then-commit dance
// other per-frame signals in this file use.
char g_lastPopupBodyText[256] = {};

constexpr float kManualGlyphVerticalNudge = -18.0f; // matches kMenuHintVerticalNudge elsewhere

// Batch capture (2026-08-02, 25 live MiniDumpWriteDump snapshots across screens
// reachable in one pass, cross-referenced against each dump's real menu-stack/
// itemDef state). Six Ghidra passes (static, -noanalysis) plus this live dump
// analysis (MiniDumpWriteDump + a raw byte scan for the real print-command
// records) converged on: the engine's text-draw command queue carries only
// rendering data (X, Y, font, scale, color, inline text) with zero itemDef
// back-reference -- confirmed by reading an entire real 0x54-byte record and
// finding every field accounted for by known rendering fields, none pointing
// into the itemDef heap range. Not a "not found yet" gap; the link does not
// exist at the data level, so a manual per-screen table is the correct
// architecture, not a stopgap. Coordinates below are real, read directly from
// the live print-command records, not estimated.
//
// Known partial gap, RESOLVED 2026-08-16 via the in-game click-and-drag editor:
// SWF_COMMON_DESC_RESIZE_POPUP_NAME is not fully uniform -- the quit-confirm and
// video-restore-confirm variants both land at Yes=603/No=653 (used below), but the
// "New Game overwrite" variant (reached via CAMPAIGN_BUTTON_LIST's New Game item)
// shifts to Yes=~627/No=678 because two extra lines of description text push the
// buttons down AND widen the box (so the icon's own X shifts significantly too,
// not just Y -- not anticipated when this gap was first flagged). The
// disambiguating signal this comment originally said couldn't be found turned out
// to be requiredDepth: this variant is reliably depth=1, distinct from the other
// two (kept at -1, i.e. any depth). See the dedicated depth=1 entry below.
//
// KEY FINDING (2026-08-02, verified across 5 independent lists by computing each
// real captured item's own text-end position -- start X + charCount*~13.3px --
// and checking convergence): every vertical hub/list-style menu in this game
// (CAMPAIGN_BUTTON_LIST, OPTIONS_LIST's tab selector, SPECOPS_BUTTON_LIST,
// SWF_BUTTON_LIST, LEVELS_BUTTON_LIST) right-aligns its text to the SAME shared
// column, averaging ~594-608 across all five with no per-list pattern to the
// small variance (just noise from the char-width estimate) -- a single,
// game-wide UI convention, not five separate ones. All of them use a common
// itemX=605 below rather than each item's own real (variable, left-edge) X,
// exactly like LEVELS_BUTTON_LIST's own reasoning. This was caught and fixed
// after an initial version of this table wrongly used each item's own real X
// (a left-aligned assumption) for CAMPAIGN_BUTTON_LIST/OPTIONS_LIST, which
// would have scattered the icon across a ~500-650px range instead of a
// consistent column.
//
// Popups (SWF_COMMON_DESC_RESIZE_POPUP_NAME, SWF_COMMON_POPUP_NAME,
// popmenu_difficulty, popup_friend_list_actions) are DIFFERENT: confirmed
// fixed-left at X=686 regardless of label length (e.g. "Yes"/"No" both at
// exactly 686), not right-aligned -- kept as per-item real X for those.
//
// game_select_button (SP main menu) is ALSO different again: a horizontal row
// of 3 independent buttons (not a shared vertical column), each left-aligned
// within its own button area -- kept as per-item real X + a fixed offset sized
// for the longest label.
//
// Deliberately NOT covered this pass (left on the pre-existing ordinal-based
// fallback, not worse than before):
//  - LEVELS_BUTTON_LIST at depth=4 (a deeper per-mission sub-list reached from
//    within the depth=3 mission list) -- its capture showed duplicate near-
//    identical text at slightly different positions (a drop-shadow/outline
//    rendering artifact, not separate items) mixed with genuinely new content,
//    not clean enough to calibrate confidently.
//  - OPTIONS_LIST indices 3+ (Look/Movement/Actions/Advanced Video/Voice tabs) --
//    RESOLVED 2026-08-16 via the in-game click-and-drag editor (issue #51
//    follow-up); real dragged values for all 7 real tabs now live in the table
//    below (kept at depth=-1, see that entry's own comment). The naive
//    45px-spacing prediction this bullet originally warned against WAS in fact
//    slightly off in practice (real Y values drift a few px off a flat grid past
//    index 2) -- real dragged data sidesteps that guess entirely rather than
//    needing it corrected.
//  - Keybind editing screens (Movement/Actions/Look sub-lists -- real item
//    names like "forward"/"attack" as the focused name, e.g. dump13/14/15) --
//    these don't populate a valid ui_swf_selection group/index pair at all
//    (TryGetCurrentSelectionGroupAndIndex returns false), so neither the
//    ordinal path nor this manual table can key off them today. This is a
//    separate, deeper architectural gap (would need matching directly on
//    g_focusedItemName's own real per-item name, bypassing the group/index
//    scheme entirely), not a missing table entry.
//  - "cardIcon1"-"cardIcon5"/"cardTitle1"-"cardTitle4" at depth=3 (discovered
//    2026-08-16, the Callsign/emblem-card selection screen, a PC-side
//    addition/rework with no real console reference to calibrate a fixed
//    per-index position against). A flat per-index table isn't even the right
//    model here per explicit user direction: emblems want the icon anchored to
//    the SELECTED item's own real box (bottom-right corner), and the callsign
//    name list wants it just to the right of the box -- i.e. relative to each
//    item's real on-screen rect, not an absolute per-index position. This
//    project doesn't have a way to read a real itemDef's box/rect (x/y/w/h) yet,
//    only name+flags (see TryGetRealFocusedGroupAndIndex) -- would need fresh
//    static RE on this specific screen before that's possible. Explicitly
//    disabled for now (IsGlyphDisabledGroup, matched by "cardIcon"/"cardTitle"
//    prefix) rather than left to silently draw nothing by omission, so a future
//    kManualGlyphPositions/kVerifiedGlyphGroups addition can't accidentally
//    re-enable a flat-table position that would be structurally wrong for this
//    screen.
//  - Survival's DLC map select screen (live-reported 2026-08-16, "it doesnt
//    detect the selected entry") -- unlike the keybind-editing gap above, this
//    isn't just missing a table entry: TryGetRealFocusedGroupAndIndex returns no
//    group/index at all here, AND both older fallback signals the editor now
//    reports when that happens (g_currentSelGroupName/g_currentSelIndex,
//    g_focusedItemName) come back empty too -- confirmed live, not just assumed.
//    A genuine architectural gap, same category as the keybind screens: this
//    screen's real items don't populate any of the three selection-tracking
//    mechanisms this project currently knows how to read at all. Would need
//    fresh static RE on this specific screen (a different focus/selection
//    primitive entirely) before any glyph work here is possible.
//
// 2026-08-16, in-game click-and-drag editor pass (issue #51 follow-up): every
// screen reachable from the main menu without actually being in a mission/match
// was gone through and either recalibrated (see individual entries below tagged
// "2026-08-16") or confirmed already correct as-is (kVerifiedGlyphGroups gained a
// documentation entry either way). Genuinely IN-GAME-ONLY screens -- PAUSE_LIST,
// WEAPON_POPUP (Survival's buy station), RESUME_POPUP -- were NOT part of this
// pass (can't be reached without an active mission/Survival run) and still need a
// dedicated in-mission calibration session with the same editor; their existing
// table entries below predate this tool and haven't been re-verified against it.
constexpr ManualGlyphEntry kManualGlyphPositions[] = {
    // Restart Mission confirm, specifically (2026-08-16, user direction: "go off the
    // popups internal text as another differentiator") -- shares (groupName=
    // SWF_COMMON_DESC_RESIZE_POPUP_NAME, requiredDepth=1) with the New Game
    // overwrite/Quit entry below, but its real description text is genuinely
    // different and meaningfully longer (CGAME_RESTART_WARNING, confirmed via
    // zone_dump: "If you restart now, you will lose\nany progress that you have
    // made\nin this mission\n\nContinue restart?" -- 3+ lines vs. Quit's one-line
    // "Are you sure you want to quit?"), which is exactly why its buttons land
    // ~25px lower. requiredTextSubstring="restart" -- matched case-insensitively
    // against g_lastPopupBodyText, live-captured from whatever real text is
    // currently drawn at this popup family's own shared X=686 convention (see that
    // global's own comment). MUST be listed before the unconditional fallback below
    // -- TryGetManualGlyphPosition returns the FIRST match, and the fallback (no
    // text requirement) would otherwise always win first.
    { "SWF_COMMON_DESC_RESIZE_POPUP_NAME", 1, 0.0f, 2, {1199.2f, 1205.2f}, {652.5f, 701.2f}, false, "restart" },
    // "New Game overwrite" / Quit-at-depth-1 fallback (2026-08-16 via the in-game
    // click-and-drag editor) -- reached via CAMPAIGN_BUTTON_LIST's New Game item and
    // an in-mission Quit confirm; extra description text pushes the buttons down AND
    // widens the box relative to the quit-confirm/video-restore-confirm variants
    // below. No requiredTextSubstring -- matches whenever the entry above (Restart
    // Mission specifically) doesn't. Values averaged from two independent captures
    // (New Game overwrite: 1202.2/1206.8, 627.0/678.0; Quit: 1193.2/1199.2,
    // 627.0/675.0 -- close enough to be the same real screen).
    { "SWF_COMMON_DESC_RESIZE_POPUP_NAME", 1, 0.0f, 2, {1197.7f, 1203.0f}, {627.0f, 676.5f} },
    { "SWF_COMMON_DESC_RESIZE_POPUP_NAME", -1, 55.0f, 2, {686.0f, 686.0f}, {603.0f, 653.0f} },
    // Covers "Choose Content Pack" (DLC/on-disk-content picker) and "Leave Lobby?"
    // (Yes/No) -- confirmed live to share this exact container/position despite
    // very different content. bottomAnchorPopup=true (2026-08-03): the 2-item
    // "Choose Content Pack" variant is bottom-anchored to this 3-row grid (its
    // real items land on this table's own index1/index2 Y values, not
    // index0/index1) -- live-confirmed via issue #51's own diagnostic trail.
    //
    // OFFSET MODEL CHANGED (2026-08-03): originally text-relative (686 + a flat
    // offset "past the label"), same convention as the other popups. User
    // feedback on a "Leave Lobby?" screenshot: this box is noticeably WIDER than
    // the others, so a text-relative offset left a large, inconsistent gap before
    // the icon -- wanted it box-relative instead (icon sitting a fixed 10px
    // margin from the box's own right edge, matching how the other popups already
    // read visually). Since itemX is already uniform (686) across every slot in
    // this shared table, a flat offsetX is *already* box-relative in effect once
    // sized correctly -- no structural change needed, just a bigger value.
    // Re-estimated from the screenshot (box right edge back-computed from the
    // known real text-start X=686 and the icon's own previous X=986, using the
    // image's own proportions) at ~1290; minus a 10px margin minus the icon's own
    // ~42px draw width (kHintIconSize) puts the icon's own left edge at ~1238,
    // i.e. offsetX=552. Still a screenshot-based estimate, not a live text-draw
    // measurement (same limitation as every other manual-position entry) -- may
    // need a small nudge once confirmed live.
    //
    // FINAL VISUAL NUDGE (2026-08-03): budged 30px left per live confirmation
    // (552 -> 522).
    { "SWF_COMMON_POPUP_NAME", -1, 522.0f, 3, {686.0f, 686.0f, 686.0f}, {504.0f, 554.0f, 603.0f}, true },
    // Difficulty select (RECRUIT/REGULAR/HARDENED/VETERAN).
    { "popmenu_difficulty", -1, 130.0f, 4, {686.0f, 686.0f, 686.0f, 686.0f}, {455.0f, 504.0f, 554.0f, 603.0f} },
    // Friend context menu, "invite while in a party" variant (Invite to Party /
    // Join Game) -- the fuller context menu (View/Message/Block/Report) elsewhere
    // was not captured this pass and may use a different item count/position.
    { "popup_friend_list_actions", -1, 200.0f, 2, {686.0f, 686.0f}, {554.0f, 603.0f} },
    // Campaign hub (Resume/New Game/Mission Select/Options/Credits/Main Menu/Quit)
    // -- right-aligned, shared column (see KEY FINDING above). requiredDepth
    // relaxed to -1 (2026-08-03, second batch capture, 63 more live dumps):
    // the SAME base=198/45px-spacing grid and ~605-615 right-aligned column
    // was independently confirmed at MANY different depths for this exact
    // group (Barracks tabs, weapon-category tabs, individual weapon lists all
    // reuse "SWF_BUTTON_LIST" 3-6 levels deep with an identical grid) -- this
    // is a single shared game-wide list-menu template, not a per-depth
    // coincidence, so restricting to one specific depth was unnecessarily
    // narrow and would miss dozens of real screens that reuse it.
    { "CAMPAIGN_BUTTON_LIST", -1, 20.0f, 7,
      {605.0f, 605.0f, 605.0f, 605.0f, 605.0f, 605.0f, 605.0f},
      {198.0f, 243.0f, 288.0f, 333.0f, 378.0f, 423.0f, 468.0f} },
    // Options tab selector (Video/Audio/Controls/Look/Movement/Actions/Advanced
    // Video/Voice) -- right-aligned shared column. UPGRADED 2026-08-16 via the new
    // in-game click-and-drag editor: the old entry only covered the first 3 of ~7
    // real tabs (a known gap, see the "Deliberately NOT covered" note above, now
    // stale for this group) -- real dragged values for all 7 real tabs. Kept at
    // depth=-1 (was captured live at depth=2, but this list shares the exact same
    // ~625/45px-grid template as every other -1-gated list in this table --
    // CAMPAIGN_BUTTON_LIST/SPECOPS_BUTTON_LIST/SWF_BUTTON_LIST -- and the real
    // Options tab list is reachable at more than one navigation depth depending on
    // entry point (main menu vs. pause menu), same reasoning CAMPAIGN_BUTTON_LIST's
    // own comment already used to relax IT to -1). Revert to a depth-specific entry
    // if this is ever live-reported wrong from a different entry point.
    { "OPTIONS_LIST", -1, 0.0f, 7,
      {625.0f, 631.5f, 635.2f, 635.2f, 634.5f, 633.0f, 633.0f},
      {198.0f, 245.2f, 289.5f, 333.0f, 378.0f, 423.8f, 468.8f} },
    // SP main menu (SPECIAL OPS / CAMPAIGN / MULTIPLAYER) -- a horizontal row,
    // left-aligned per item (not right-aligned like the vertical lists above).
    // offsetX sized to clear "MULTIPLAYER", the longest of the three.
    //
    // OFF-BY-ONE FIX (2026-08-03): originally indexed 0/1/2 for the three visible
    // tiles, calibrated against the old ui_swf_selection-based selIndex. Once the
    // focus signal switched to TryGetRealFocusedGroupAndIndex() (a direct itemDef-
    // name-suffix read, issue #51's Campaign fix), live diagnostic logging showed
    // realIndex is NEVER 0 across an extended session that cycled through all three
    // tiles -- only 1/2/3 -- and using the old 0/1/2 table against these new 1/2/3
    // indices put every glyph one slot right of the true button (confirmed live:
    // "glyphs way too far horizontal"). Real cause: the itemDef array's own "_0"
    // suffix belongs to a non-focusable background/decoration element sharing this
    // group's naming convention, not a real button -- the 3 real tiles are genuinely
    // suffixed _1/_2/_3. Index 0 kept as an unused placeholder slot (never looked up,
    // since it never receives focus) rather than renumbering, so the real observed
    // indices map directly.
    //
    // OFFSET RECALIBRATED (2026-08-03), same live re-test as the fix above: the
    // original 170px offset landed mid-word on "SPECIAL OPS" ("SPECIAL [X]PS" in
    // a user-supplied screenshot) rather than clearing it, despite the comment
    // above claiming it was sized for "MULTIPLAYER" (a similar-length label) --
    // the original estimate was simply too small for either. Increased to 260px,
    // a screenshot-based re-estimate (roughly: 170px covered ~73% of "SPECIAL
    // OPS"'s own rendered width, so ~233px clears it, +~25px gap). Still an
    // estimate, not a live text-width measurement (this manual-position path has
    // no draw call in scope to measure against, same limitation as every other
    // entry in this table) -- may need another visual nudge once confirmed
    // across all three tiles, not just Special Ops.
    //
    // CAMPAIGN SPECIAL-CASED (2026-08-03): confirmed live -- Special Ops and
    // Multiplayer both look correct at the shared 260px offset now, but a second
    // user-supplied screenshot showed Campaign's glyph landing almost at the
    // tile's own right border, well past the word "CAMPAIGN" itself. Root cause:
    // "CAMPAIGN" is only 8 characters vs. 11 for the other two labels, so the
    // SAME flat 260px offset (correctly sized for the two longer words) overshoots
    // a shorter one by a proportional amount. The struct only supports one shared
    // offsetX per group, so rather than adding per-item offset support for one
    // outlier, Campaign's own stored X (863) is nudged left by ~65px (to 798) to
    // compensate -- 798+260=1058 lands the glyph directly after "CAMPAIGN" instead
    // of near the tile edge. Special Ops (344) unchanged.
    //
    // FINAL VISUAL NUDGE (2026-08-03): Campaign and Multiplayer both budged 5px
    // right per live confirmation (798->803, 1322->1327). Special Ops (344->349)
    // added to the same +5px treatment in a follow-up request ("all main menu
    // type entries...including spec ops, the glyphs are a bit too tight to the
    // text") -- extended to every vertical hub/list entry in this table too, not
    // just this row; see each entry's own iconOffsetX (15->20) below.
    // RECALIBRATED 2026-08-16 via the new in-game click-and-drag editor (issue #51
    // follow-up) -- superseded the old screenshot-estimated offset/positions above
    // with real dragged values (offsetX folded into each item's own X directly,
    // same convention the editor's own export always uses). Index 0 still an
    // unused placeholder (see the off-by-one comment above -- itemDef "_0" never
    // receives real focus).
    { "game_select_button", 1, 0.0f, 4, {0.0f, 630.8f, 1110.8f, 1618.5f}, {0.0f, 457.5f, 457.5f, 456.8f} },
    // Campaign act mission list (e.g. Act I: Hunter Killer/Persona Non
    // Grata/Turbulence/Back on the Grid/Mind the Gap) -- right-aligned shared
    // column. Mission NAMES differ per act, but this Y-grid (five 45px-spaced
    // slots starting at 288) is expected to hold across acts since it's the
    // same shared list container. Kept depth-specific (unlike the others
    // above) -- confirmed live this SAME group name is reused one level
    // deeper for a per-mission sub-list with a genuinely different Y-base.
    { "LEVELS_BUTTON_LIST", 3, 20.0f, 5,
      {605.0f, 605.0f, 605.0f, 605.0f, 605.0f},
      {288.0f, 333.0f, 378.0f, 423.0f, 468.0f} },
    // Same group, one level deeper (2026-08-03, second batch capture): the
    // per-mission sub-list under an Act's own mission list (e.g. Act I:
    // Prologue/Black Tuesday/Hunter Killer/Persona Non Grata/Turbulence/Back
    // on the Grid/Mind the Gap). An earlier pass wrongly flagged this as "not
    // clean enough to calibrate" -- what looked like contamination was just
    // the FIRST entry's own text drawn 3x for a drop-shadow/outline effect
    // (identical text at 3 near-identical Y values, e.g. 187/191/198),
    // trivially distinguishable from real extra rows. Right-aligned column
    // is slightly left of the ~605 norm (~583, still same convention), same
    // base=198/45px grid as everything else. Acts II/III only populate 6 of
    // these 7 slots; harmless, unused higher indices are just never reached.
    { "LEVELS_BUTTON_LIST", 4, 20.0f, 7,
      {583.0f, 583.0f, 583.0f, 583.0f, 583.0f, 583.0f, 583.0f},
      {198.0f, 243.0f, 288.0f, 333.0f, 378.0f, 423.0f, 468.0f} },
    // Special Ops hub (Find Online Match/Private Online Match/Solo Play/
    // Callsign/Barracks/Store/Options/Main Menu/Quit) -- right-aligned shared
    // column, 9 real items.
    { "SPECOPS_BUTTON_LIST", -1, 20.0f, 9,
      {605.0f, 605.0f, 605.0f, 605.0f, 605.0f, 605.0f, 605.0f, 605.0f, 605.0f},
      {198.0f, 243.0f, 288.0f, 333.0f, 378.0f, 423.0f, 468.0f, 513.0f, 558.0f} },
    // The single most-reused list template in the game -- confirmed live as:
    // MP lobby root (Start Op/Change Op/Select Difficulty/Select Role/
    // Callsign/Barracks), Barracks' own top tab row (Leaderboards/Survival
    // Armories), the Survival Armories' Weapons/Equipment/Air Support tabs,
    // each weapon CATEGORY tab row (Handguns/Machine Pistols/Assault
    // Rifles/...), and each individual weapon list within a category
    // (Five Seven/USP .45/MP412/...) -- same base=198/45px grid and shared
    // right-aligned column every time, 3-6 stack levels deep. Extended to 10
    // items (some weapon-category lists run past the original 6).
    { "SWF_BUTTON_LIST", -1, 20.0f, 10,
      {605.0f, 605.0f, 605.0f, 605.0f, 605.0f, 605.0f, 605.0f, 605.0f, 605.0f, 605.0f},
      {198.0f, 243.0f, 288.0f, 333.0f, 378.0f, 423.0f, 468.0f, 513.0f, 558.0f, 603.0f} },
    // Leaderboard map-row list (Village/Interchange/Underground/Dome/
    // Mission/Seatown/Carbon/Bootleg/Hardhat/Fallen/Outpost/Lockdown/Arkaden/
    // Downturn/Bakaara, and separately Bonus Maps variants e.g. Stay Sharp/
    // Milehigh Jack/...) -- right-aligned shared column, 14 real map rows,
    // base Y=288 (NOT 198 -- this screen has 2 real header rows, "Bonus Maps"
    // tab + mode name, before the actual map list starts). This specific
    // group name covers the team-survival leaderboard family; other
    // leaderboard mode/duration combinations use differently-named groups
    // per the .menu audit and were not captured.
    { "LEADERBOARDS_BUTTON_LIST", -1, 20.0f, 14,
      {605.0f, 605.0f, 605.0f, 605.0f, 605.0f, 605.0f, 605.0f, 605.0f, 605.0f, 605.0f, 605.0f, 605.0f, 605.0f, 605.0f},
      {288.0f, 333.0f, 378.0f, 423.0f, 468.0f, 513.0f, 558.0f, 603.0f, 648.0f, 693.0f, 738.0f, 783.0f, 828.0f, 873.0f} },
    // Team Survival leaderboard's map-row list (Village/Interchange/
    // Underground/Dome/Mission/Seatown/Carbon/Bootleg/Hardhat/Fallen/Outpost/
    // Lockdown/Arkaden/Downturn/Bakaara) -- right-aligned shared column, 15
    // real map rows. This specific group name is for the team-survival
    // variant only; other leaderboard mode/duration combinations use
    // differently-named groups per the .menu audit and were not captured.
    { "leaderboard_survival_team_level", 4, 20.0f, 15,
      {605.0f, 605.0f, 605.0f, 605.0f, 605.0f, 605.0f, 605.0f, 605.0f, 605.0f, 605.0f, 605.0f, 605.0f, 605.0f, 605.0f, 605.0f},
      {243.0f, 288.0f, 333.0f, 378.0f, 423.0f, 468.0f, 513.0f, 558.0f, 603.0f, 648.0f, 693.0f, 738.0f, 783.0f, 828.0f, 873.0f} },
    // Survival's own solo/co-op map-select screen (RESISTANCE/VILLAGE/
    // INTERCHANGE/UNDERGROUND/DOME/MISSION/SEATOWN/CARBON/BOOTLEG/HARDHAT/
    // FALLEN/OUTPOST/LOCKDOWN/ARKADEN/DOWNTURN/BAKAARA), depth=4 -- SAME group
    // name as the entry below, genuinely different real layout (2026-08-03,
    // issue #51). First attempt excluded this depth entirely to fall through to
    // the generic ordinal-based fallback path, live-confirmed WRONG: that path's
    // frame-wide ordinal counter also counts this screen's own right-hand
    // preview panel (title/description/difficulty/RANK-PLAYER-WAVES header),
    // which draws in the SAME frame and throws the count off by a large,
    // inconsistent amount -- user-reported the glyph landed 5-6 rows off (a
    // MISSION selection drew the icon on RESISTANCE) and eventually onto the
    // preview panel's OWN text ("Best Wave: 21") once the offset grew large
    // enough. Same root cause this table was originally built to fix for OTHER
    // screens (issue #51's own "ordinal counts every draw call in the frame,
    // not just this list's own items").
    //
    // CORRECTED MODEL (2026-08-03): the first real-position attempt used each
    // item's own real TEXT-START X (captured live) plus a flat 250px estimated
    // offset, reasoning these were CENTERED/variable-width -- live-reported "way
    // out of line horizontally... doesn't correlate with text length... too far
    // right." Root cause of THAT: 250px was estimated from a much larger-font
    // screen (game_select_button's main-menu tiles); this list's real font/scale
    // is the same small size every other list uses. Measuring the REAL text-END
    // position directly (via SumDirectIndexedGlyphWidthsBefore, the same
    // technique the ordinal-fallback path already uses, added as a temporary
    // live diagnostic) proved these items are NOT centered/variable at all --
    // every single label's real end clusters tightly at 595-620 (Resistance=608,
    // Village=603.5, Interchange=595, Dome=620, ...), i.e. this list uses the
    // EXACT SAME universal ~605 right-aligned shared column as every other list
    // in the table (CAMPAIGN_BUTTON_LIST, SPECOPS_BUTTON_LIST, etc.) -- the
    // earlier "centered" read came from looking at each item's real START
    // position only, which naturally varies with length for ANY right-aligned
    // text, and was mistaken for centering. Simplified to the same
    // itemX=605/offsetX=20 model as every other list; no per-item real X needed.
    // MERGED 2026-08-16 (live-reported "its still happening specifically on the map
    // select screens" -- the icon periodically jumping to a different real position
    // on its own, unrelated to any focus-signal debounce). Root cause, confirmed via
    // proxy_d3d9.log: GetMenuStackDepth() itself genuinely, repeatedly flip-flops
    // between 2, 3, AND 4 for this EXACT same on-screen map list (same realGroup,
    // same realIndex, same siblingCount=32, only depth differs -- observed cycling
    // every several seconds over a long session, not a one-off startup transient),
    // almost certainly because some OTHER UI element (a live map-preview/description
    // panel) intermittently pushes/pops itself on the real menu stack in the
    // background without the player navigating anywhere. All three depths were
    // originally SEPARATE entries with slightly different dragged values (each a
    // real capture of what turned out to be the SAME real screen, just calibrated
    // while depth happened to read differently each time) -- since depth itself
    // can't be trusted to stay put here, all three now carry the IDENTICAL, most
    // complete depth=2 capture (has real data for every index including 14/15) so
    // the resolved position can no longer visibly differ depending on which of the
    // three the flicker lands on.
    { "SO_LEVELS_BUTTON_LIST", 4, 0.0f, 16,
      {636.0f, 635.2f, 635.2f, 637.5f, 632.2f, 631.5f, 634.5f, 628.5f, 636.0f, 628.5f, 630.8f, 630.0f, 631.5f, 627.8f, 632.2f, 629.2f},
      {198.8f, 243.8f, 288.8f, 333.8f, 378.8f, 425.2f, 468.8f, 513.8f, 558.0f, 603.8f, 647.2f, 693.8f, 738.8f, 783.0f, 828.0f, 873.8f} },
    { "SO_LEVELS_BUTTON_LIST", 2, 0.0f, 16,
      {636.0f, 635.2f, 635.2f, 637.5f, 632.2f, 631.5f, 634.5f, 628.5f, 636.0f, 628.5f, 630.8f, 630.0f, 631.5f, 627.8f, 632.2f, 629.2f},
      {198.8f, 243.8f, 288.8f, 333.8f, 378.8f, 425.2f, 468.8f, 513.8f, 558.0f, 603.8f, 647.2f, 693.8f, 738.8f, 783.0f, 828.0f, 873.8f} },
    // CORRECTED 2026-08-16, same investigation as the depth=2/4 merge above: this
    // was originally believed to be a genuinely different real layout at depth=3
    // (a 14-item, Y=243-based grid, one row lower than depth=2/4's Y=198.8 grid) --
    // proxy_d3d9.log instead showed depth=3 participating in the EXACT SAME
    // flip-flop as 2/4 for the identical on-screen item (same realGroup/realIndex/
    // siblingCount, only depth differing) -- i.e. this was ALSO just a misreported
    // instance of the SAME real screen, not a real third layout. Merged to the same
    // identical depth=2/4 dataset so the resolved position can't visibly differ
    // regardless of which of the three depth values the flicker lands on.
    { "SO_LEVELS_BUTTON_LIST", 3, 0.0f, 16,
      {636.0f, 635.2f, 635.2f, 637.5f, 632.2f, 631.5f, 634.5f, 628.5f, 636.0f, 628.5f, 630.8f, 630.0f, 631.5f, 627.8f, 632.2f, 629.2f},
      {198.8f, 243.8f, 288.8f, 333.8f, 378.8f, 425.2f, 468.8f, 513.8f, 558.0f, 603.8f, 647.2f, 693.8f, 738.8f, 783.0f, 828.0f, 873.8f} },
    // Special Ops mission list reached from the MP-hosted lobby path (Village/
    // Interchange/Underground/Dome/Mission/Seatown/Carbon/Bootleg/Hardhat/
    // Fallen/Outpost/Lockdown/Arkaden/Downturn/Bakaara) -- right-aligned
    // shared column, base Y=243 (one header row, the mode name, precedes the
    // real map list here -- one row higher than LEADERBOARDS_BUTTON_LIST's
    // base=288 since there's no separate "Bonus Maps" tab row in this path).
    // requiredDepth=-1 (any OTHER depth than 4, handled by the dedicated entry
    // above) -- unchanged from before this investigation.
    { "SO_LEVELS_BUTTON_LIST", -1, 20.0f, 14,
      {605.0f, 605.0f, 605.0f, 605.0f, 605.0f, 605.0f, 605.0f, 605.0f, 605.0f, 605.0f, 605.0f, 605.0f, 605.0f, 605.0f},
      {243.0f, 288.0f, 333.0f, 378.0f, 423.0f, 468.0f, 513.0f, 558.0f, 603.0f, 648.0f, 693.0f, 738.0f, 783.0f, 828.0f} },
    // REAL GAMEPLAY, not a menu: Survival's in-game weapon-armory buy station
    // (Refill Bullet Ammo/Handguns/Machine Pistols/Assault Rifles/Sub Machine
    // Guns/Light Machine Guns/Sniper Rifles/Shotguns). Left-aligned, FIXED
    // column (X=584 constant regardless of label length -- confirmed
    // different convention from every menu list above), base Y=455, ~49.5px
    // spacing. offsetX sized to clear "Light Machine Guns", the longest label.
    { "WEAPON_POPUP", -1, 270.0f, 8,
      {584.0f, 584.0f, 584.0f, 584.0f, 584.0f, 584.0f, 584.0f, 584.0f},
      {455.0f, 504.0f, 554.0f, 603.0f, 653.0f, 702.0f, 752.0f, 801.0f} },
    // Real pause menu (Resume Game/Options/Lower Difficulty/Last Checkpoint/
    // Restart Mission/Quit) -- clean capture this time (the only earlier
    // attempt was contaminated by a background credits scroll); right-aligned
    // shared column, matches the universal base=198/45px grid.
    { "PAUSE_LIST", -1, 20.0f, 6,
      {605.0f, 605.0f, 605.0f, 605.0f, 605.0f, 605.0f},
      {198.0f, 243.0f, 288.0f, 333.0f, 378.0f, 423.0f} },
    // In-game "Resume Game?" / "Leave Lobby?" confirm popup -- fixed-left at
    // X=686 like every other popup, identical position to SWF_COMMON_POPUP_NAME
    // (504/554/603) despite being a different group name -- another instance
    // of the shared popup container reused for different content.
    { "RESUME_POPUP", -1, 300.0f, 3, {686.0f, 686.0f, 686.0f}, {504.0f, 554.0f, 603.0f} },
};

// Verified-screens allowlist (2026-08-03, v0.3.0 release standard, issue #51).
// User direction: "make a config toggle for any unconfirmed screens so glyphs
// wont show broken only on verified screens" -- having an entry in
// kManualGlyphPositions above is NOT evidence a screen actually works
// (WEAPON_POPUP had one and was still broken; several other entries were
// never re-confirmed after this session's repeated regressions). Rather than
// ship a glyph that might be silently wrong on any of those unverified
// screens, the actual visible draw is now gated on an explicit allowlist of
// (groupName, depth) pairs the user has personally confirmed live in THIS
// investigation -- everything else draws nothing (silent, not wrong) until
// individually verified and added here. depth=-1 means "verified at any
// depth" (matches kManualGlyphPositions' own convention). Diagnostic logging
// (manual-glyph-diag / list-item-diag) stays unconditional so future
// verification passes still have real data to work from -- only the actual
// on-screen glyph is suppressed for unverified groups.
struct VerifiedGlyphGroup { const char* groupName; int depth; };
constexpr VerifiedGlyphGroup kVerifiedGlyphGroups[] = {
    { "SWF_COMMON_POPUP_NAME", -1 },  // "Leave Lobby?" and "Choose Content Pack", both confirmed live 2026-08-03
    { "CAMPAIGN_BUTTON_LIST", -1 },   // Campaign hub, confirmed live 2026-08-03
    { "game_select_button", 1 },      // Main menu (Special Ops/Campaign/Multiplayer), confirmed live 2026-08-03,
                                       // recalibrated 2026-08-16 via the in-game click-and-drag editor
    // Everything below confirmed live 2026-08-16, via the new in-game click-and-drag
    // editor (issue #51 follow-up) -- real drag-and-visually-confirmed positions, not
    // an estimate. This gate no longer actually SUPPRESSES the draw for unlisted
    // groups (see havePos's own comment above, removed same day) -- these entries are
    // kept as a documentation record of what's actually been confirmed, not a
    // functional requirement anymore.
    { "SPECOPS_BUTTON_LIST", -1 },    // confirmed at depth=1 (8 items) and depth=2 (9 items)
    { "SWF_BUTTON_LIST", -1 },        // confirmed at depth=1
    { "OPTIONS_LIST", -1 },
    { "SO_LEVELS_BUTTON_LIST", 2 },   // depth=2/3/4 all carry the identical, full 16-item
    { "SO_LEVELS_BUTTON_LIST", 3 },   // dataset -- see that entry's own "MERGED 2026-08-16" comment
    { "SO_LEVELS_BUTTON_LIST", 4 },
    { "SWF_COMMON_DESC_RESIZE_POPUP_NAME", 1 },  // "New Game overwrite" variant, previously an
                                                   // unresolved gap -- depth=1 turned out to be
                                                   // the disambiguating signal
};

bool IsVerifiedGlyphGroup(const char* groupName, int depth)
{
    if (!groupName || groupName[0] == '\0') return false;
    for (const auto& g : kVerifiedGlyphGroups) {
        if (_stricmp(groupName, g.groupName) != 0) continue;
        if (g.depth != -1 && g.depth != depth) continue;
        return true;
    }
    return false;
}

// siblingCount (2026-08-03, issue #51 follow-up): the REAL number of items in
// THIS instance sharing the focused item's group name, from
// TryGetRealFocusedGroupAndIndex's own array walk. Some shared groups are reused
// for variants with fewer real items than the table's calibrated max (e.g.
// SWF_COMMON_POPUP_NAME's 3-slot "Leave Lobby?" table vs. the 2-item "Choose
// Content Pack" variant) -- live-confirmed the real items in a smaller variant
// are BOTTOM-anchored to the shared grid (Content Pack's 2 items land at the
// table's own index1/index2 Y values, not index0/index1), so when the real
// count is smaller than the table's, the focused item's own 0-based local index
// is shifted up by (entry.count - siblingCount) before indexing the table.
// Pass siblingCount <= 0 to skip this entirely (matches old behavior) for
// callers that don't have it available.
// Live direction (2026-08-16): "make all a glyphs that are horizontally close
// to each other (within 15px) be in line horizontally for consistency use the
// most right glyph in that specific vertical row". Per-item itemX values in a
// single entry's own vertical list naturally jitter by a few px (drag
// imprecision when calibrating, or genuine per-item text-width variance at
// capture time) even though the icons should read as one aligned column.
// Applied at lookup time (rather than hand-flattening every table entry) so it
// self-applies to every existing AND future captured entry uniformly. Deliberately
// scoped to ONE entry's own itemX[] (single-linkage clustering by threshold,
// count is always small) -- horizontally-separated items in the same entry
// (e.g. game_select_button's 3 far-apart tiles) fall outside the threshold and
// are left alone, matching the "vertical row" framing.
constexpr float kHorizontalAlignClusterPx = 15.0f;
float SnapToHorizontalClusterMaxX(const float* itemX, int count, int index)
{
    if (count <= 1 || index < 0 || index >= count) return itemX[index];
    int order[kManualGlyphMaxItems];
    for (int i = 0; i < count; ++i) order[i] = i;
    for (int i = 1; i < count; ++i) { // insertion sort by value, ascending
        int key = order[i];
        float keyVal = itemX[key];
        int j = i - 1;
        while (j >= 0 && itemX[order[j]] > keyVal) { order[j + 1] = order[j]; --j; }
        order[j + 1] = key;
    }
    int clusterStart = 0;
    for (int i = 1; i <= count; ++i) {
        bool boundary = (i == count) || (itemX[order[i]] - itemX[order[i - 1]] > kHorizontalAlignClusterPx);
        if (!boundary) continue;
        float clusterMaxVal = itemX[order[i - 1]]; // sorted ascending -> last in range is the max
        for (int k = clusterStart; k < i; ++k) {
            if (order[k] == index) return clusterMaxVal;
        }
        clusterStart = i;
    }
    return itemX[index]; // unreachable
}

bool TryGetManualGlyphPosition(const char* groupName, int depth, int index, int siblingCount, float& outX, float& outY)
{
    for (const auto& entry : kManualGlyphPositions) {
        if (_stricmp(groupName, entry.groupName) != 0) continue;
        if (entry.requiredDepth != -1 && entry.requiredDepth != depth) continue;
        if (entry.requiredTextSubstring &&
            !ContainsSubstringCaseInsensitive(g_lastPopupBodyText, entry.requiredTextSubstring)) continue;
        int effectiveIndex = index;
        if (entry.bottomAnchorPopup && siblingCount > 0 && siblingCount < entry.count) {
            effectiveIndex = index + (entry.count - siblingCount);
        }
        if (effectiveIndex < 0 || effectiveIndex >= entry.count) continue;
        outX = SnapToHorizontalClusterMaxX(entry.itemX, entry.count, effectiveIndex) + entry.iconOffsetX;
        outY = entry.itemY[effectiveIndex] + kManualGlyphVerticalNudge;
        return true;
    }
    return false;
}

bool HasManualGlyphPositionForGroup(const char* groupName, int depth)
{
    for (const auto& entry : kManualGlyphPositions) {
        if (_stricmp(groupName, entry.groupName) != 0) continue;
        if (entry.requiredDepth != -1 && entry.requiredDepth != depth) continue;
        return true;
    }
    return false;
}

// Explicit denylist (2026-08-16, live direction: "for now just disable those
// screens['] glyphs") -- the Callsign/emblem card-select screen ("cardIcon1"-
// "cardIcon5"/"cardTitle1"-"cardTitle4", discovered via the in-game editor,
// kManualGlyphPositions' own "Deliberately NOT covered" comment). No console
// reference exists for this screen to calibrate a fixed per-index position
// against (PC-side addition/rework, per user context) -- the real fix needs the
// icon positioned relative to each selected item's own real box (bottom-right
// corner for emblems, just right of the box for the callsign name list), which
// this project doesn't have a way to read yet (no known itemDef rect/size
// offset, only name+flags). None of these groups currently have a
// kManualGlyphPositions entry or a kVerifiedGlyphGroups listing, so nothing
// should draw for them already -- this is an explicit, self-documenting belt-
// and-suspenders check (matching this project's own "explicit allowlist, not
// implicit-by-omission" convention) rather than relying on that being true only
// by coincidence, since the manual-table draw path no longer checks
// kVerifiedGlyphGroups at all (removed same day, see havePos's own comment).
bool IsGlyphDisabledGroup(const char* groupName)
{
    if (!groupName || groupName[0] == '\0') return false;
    return _strnicmp(groupName, "cardIcon", 8) == 0 || _strnicmp(groupName, "cardTitle", 9) == 0;
}

// ---- Special Ops nested-modal detection v3, allowlist-based (2026-08-01) ---------
//
// v1 (g_inSpecOpsFlow, OpenMenuByName-based): doc-audit found it has NEVER once
// become true across every logged live session -- OpenMenuByName only ever fires
// for "main"/"pausedmenu"/"coop_lobby"/"popup_connecting", never a Special Ops
// name. The shipped Friends-suppress/Back-force logic gated on it was therefore
// completely inert in practice.
//
// v2 (g_currentSelGroupName-staleness-based): "last known selection group was
// Special-Ops-flavored, but current focus doesn't match it" correctly caught the
// real modal, but false-triggered on the plain MAIN MENU tile row -- the staleness
// this relies on (ui_swf_selection freezing once you leave that list, deliberately
// used as part of the signal) doesn't get cleared on returning to the main menu.
// Tried resetting it on OpenMenuByName("main") -- but returning to the main menu
// via an in-menu "MAIN MENU" list item goes through the same compiled `.menu`
// script `open` action already known (from issue #50's own original investigation)
// to bypass OpenMenuByName entirely, so the reset never fired either. Confirmed
// live: OpenMenuByName("main") appears exactly once per session (the very first
// boot), never again on any subsequent return-to-main.
//
// v3, allowlist-based: matched the CURRENTLY focused item's name directly against
// confirmed modal-only names ("Chaos"/"Mission"/"Survival", the mode picker's own
// real mode names -- "game_select_button_1/2/3" was tried first and removed after
// a live-confirmed false positive on the main tile row, a shared naming convention
// not unique to the modal). This correctly covered the mode-picker screen itself,
// but NOT the on-disk/DLC content picker one level deeper -- live-reported still
// showing Friends. Investigated: that screen's own focused-item name resolves to
// "SWF_COMMON_POPUP_NAME_0" -- confirmed via a SEPARATE localvarstring-diag capture
// that "SWF_COMMON_POPUP_NAME" (no suffix) is itself a real, bare ui_swf_selection-
// style value elsewhere in the log, i.e. a genuinely shared/generic popup-button
// template ("COMMON" is literal, not an assumption) -- adding it to the allowlist
// would very likely repeat the same false-positive mistake a third time on some
// unrelated confirm/error dialog using the same template.
//
// v4, this version: sticky state instead of a stateless name match, to span this
// generic-named screen without trusting its own name at all. The mode picker's own
// group context resolves as "MODE_POPUP" (confirmed via localvarstring-diag,
// distinctly named, not generic) right before Chaos/Mission/Survival get focus --
// treat entering that flow as "sticky true," carry the sticky flag forward through
// ambiguous/generic focus states (a generic popup name, or "none" during a
// transition), and only clear it on a CONFIRMED return to the real root list (a
// "<name>_<digits>"-shaped focused item, the same shape ui_swf_selection's own
// per-item names use) -- never on reaching the main menu specifically, sidestepping
// the return-to-main detection problem that broke v2 entirely. A false CLEAR here
// is low-risk (worst case: reverts to the original bug, Friends shows instead of
// Back, on some edge case) -- a false SET is the expensive mistake (a visible
// regression elsewhere), so the trigger stays narrow while the carry-forward stays
// broad on purpose. g_specOpsModalSticky itself is declared earlier in this file
// (right before UpdateNavGroupTrackingFromResolvedValue), which ALSO clears it --
// see that function's own comment for why the clear moved there.
bool IsInsideSpecOpsNestedModal()
{
    if (g_focusedItemName[0] == '\0') return g_specOpsModalSticky;

    static const char* const kKnownModalItemNames[] = {
        "Chaos", "Mission", "Survival",
    };
    for (const char* known : kKnownModalItemNames) {
        if (_stricmp(g_focusedItemName, known) == 0) {
            g_specOpsModalSticky = true;
            return true;
        }
    }

    if (_stricmp(g_focusedItemName, "none") == 0) return g_specOpsModalSticky;

    // Confirmed back on a real, indexed list item ("<name>_<digits>", the same
    // shape ui_swf_selection's own values use) -- definitively not inside a modal
    // anymore, regardless of which list it is. Clears stickiness even for a list
    // this project doesn't otherwise recognize, by design (see the false-CLEAR-is-
    // cheap reasoning above). Explicitly excludes names starting with "SWF_" --
    // caught before this shipped: "SWF_COMMON_POPUP_NAME_0" (the on-disk/DLC
    // picker's own generic button name) ALSO matches "<name>_<digits>", which
    // would have cleared stickiness the instant that screen gained focus, the
    // exact opposite of what this is for. Every confirmed real game list group
    // seen so far (SPECOPS_BUTTON_LIST_N, SO_LEVELS_BUTTON_LIST_N,
    // LEADERBOARDS_BUTTON_LIST_N) uses a game-specific name, never an "SWF_"
    // prefix -- that prefix consistently marks the shared UI-framework template
    // layer instead, both here and in "SWF_COMMON_DESC_RESIZE_POPUP_NAME" seen
    // elsewhere in this file's own captures.
    size_t len = strlen(g_focusedItemName);
    size_t i = len;
    while (i > 0 && isdigit(static_cast<unsigned char>(g_focusedItemName[i - 1]))) --i;
    bool looksLikeIndexedListItem = (i < len && i > 0 && g_focusedItemName[i - 1] == '_') &&
                                      _strnicmp(g_focusedItemName, "SWF_", 4) != 0;
    if (looksLikeIndexedListItem) {
        g_specOpsModalSticky = false;
        return false;
    }

    // Anything else (a generic "SWF_"-prefixed shape, or something not otherwise
    // recognized) carries the sticky flag forward unchanged rather than guessing.
    return g_specOpsModalSticky;
}

// Live-reported 2026-08-01: same class of bug as the Special Ops modal above, this
// time for the Friends list itself -- opening it correctly shows this project's own
// Back hint, but the underlying screen's native "Friends ^2F^7" hint keeps showing
// too, since that screen doesn't know it's now obscured (same root cause as issue
// #50's original bug: this project's own redraw happens at end-of-frame, after
// native paint order would have hidden it). getfocuseditemname() reports
// "friendList" while this screen is focused (confirmed live) -- no sticky/staleness
// concerns needed here, unlike the Special Ops case, since this is a direct,
// stateless check: Friends is either the currently focused thing or it isn't.
bool IsFriendsListOpen()
{
    return _strnicmp(g_focusedItemName, "friendList", 10) == 0;
}

__declspec(naked) void Hook_00616230()
{
    __asm {
        // ESI (output pointer) is never touched here, so it reaches the trampoline
        // completely unchanged -- stash a COPY for the post-call logger. EAX is
        // free to use as pure scratch (confirmed via the decompile above: this
        // getter reads no implicit input register besides ESI), unlike
        // Hook_00613ac0's EAX which had to be preserved.
        mov dword ptr [g_focusedItemOutEsi], esi

        mov eax, dword ptr [esp]
        mov dword ptr [g_focusedItemRealRetAddr], eax
        mov dword ptr [esp], offset afterCall
        jmp dword ptr [g_orig_00616230]

    afterCall:
        pushad
        call LogFocusedItemNameResult
        popad
        jmp dword ptr [g_focusedItemRealRetAddr]
    }
}

// ---- localvarint() hook, FUN_00613b70 (2026-08-01, issue #51 follow-up) ----------
//
// The A-glyph feature's ordinal-vs-logical-index cross-check (issue #51) was
// confirmed live to never agree: the per-frame "ordinal of plain smallFont text"
// counter also catches subtitle/legal text and (live-confirmed by the user) the
// Special-Ops-modal's OWN mode-selection buttons ("CHAOS"/"MISSIONS") drawing in
// the same frame -- not scoped to one list's own items at all. Real fix: read
// `ui_buttonNavGroupCurrent`/`ui_buttonNavGroupOffset` directly (opcode 79/
// localvarint, case 0x4f in FUN_0054cc50) and compute the target row position from
// the `.menu` files' own real formula (`((current - offset) * 20) + 34.667 +
// 0.667`) instead of trying to correlate against draw order at all. Same
// EAX(name-entry)/ECX(context)/ESI(output) shape as FUN_00613ac0, confirmed via
// its own decompile:
//
//   void __fastcall FUN_00613b70(undefined4 param_1) {
//       int *in_EAX; undefined4 *unaff_ESI;
//       if (*in_EAX == 2) { ... unaff_ESI[0] = 0; unaff_ESI[1] = resolvedIntValue; }
//   }
//
// (discriminant 0 = int type here, vs 2 = string type for FUN_00613ac0/FUN_00616230
// above.) DIAGNOSTIC-ONLY: this opcode serves EVERY localvarint() call project-wide,
// not just the two names this feature cares about, so logged values are a mix
// (scroll offsets, selection indices, unrelated flags) -- logs the raw resolved int
// (deduped) to see what's actually live before trying to identify which stream is
// which by behavior (e.g. one value should visibly track D-pad up/down by ±1).
namespace {
void* g_orig_00613b70 = nullptr;
uintptr_t g_localVarIntInEax = 0;
uintptr_t g_localVarIntInEcx = 0;
uintptr_t g_localVarIntOutEsi = 0;
uintptr_t g_localVarIntRealRetAddr = 0;

// Live-confirmed 2026-08-01: dedup-by-value-change alone hid the real signal -- a
// STABLE value (what ui_buttonNavGroupCurrent should be while a given item stays
// selected) only logs once, right when it first becomes that value, then goes
// quiet; meanwhile an unrelated per-frame 0/1 toggle (almost certainly a cursor-
// blink/highlight-pulse animation flag sharing this same opcode) re-logs constantly
// and buries it. Fixed by keying the dedup on the PAIR (this value, the ALREADY-
// tracked ui_swf_selection index from the localvarstring hook above) instead of the
// value alone -- logs a fresh line whenever EITHER changes, so a value that tracks
// the real selection index in lockstep becomes directly visible by comparison
// against selIndex in the SAME line, without needing to manually correlate
// timestamps across two separate diagnostic tags.
void LogLocalVarIntResult()
{
    __try {
        if (!LooksLikeValidPointer(g_localVarIntOutEsi)) return;
        int* out = reinterpret_cast<int*>(g_localVarIntOutEsi);
        if (out[0] != 0) return; // not the int-operand shape we expect -- skip rather than guess
        int value = out[1];
        static int s_lastLoggedValue = INT_MIN;
        static int s_lastLoggedSelIndex = INT_MIN;
        if (value != s_lastLoggedValue || g_currentSelIndex != s_lastLoggedSelIndex) {
            s_lastLoggedValue = value;
            s_lastLoggedSelIndex = g_currentSelIndex;
            char buf[160];
            sprintf_s(buf, "[localvarint-diag] resolved value=%d (current selGroup=\"%s\" selIndex=%d)",
                       value, g_currentSelGroupName, g_currentSelIndex);
            LogFromController(buf);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        // Never let this crash the game over an unexpected operand shape.
    }
}
} // namespace

__declspec(naked) void Hook_00613b70()
{
    __asm {
        mov dword ptr [g_localVarIntInEax], eax
        mov dword ptr [g_localVarIntInEcx], ecx
        mov dword ptr [g_localVarIntOutEsi], esi

        mov eax, dword ptr [esp]
        mov dword ptr [g_localVarIntRealRetAddr], eax
        mov dword ptr [esp], offset afterCall

        mov eax, dword ptr [g_localVarIntInEax]
        mov ecx, dword ptr [g_localVarIntInEcx]
        jmp dword ptr [g_orig_00613b70]

    afterCall:
        pushad
        call LogLocalVarIntResult
        popad
        jmp dword ptr [g_localVarIntRealRetAddr]
    }
}

// ---- Cursor-draw suppression hook, FUN_004d48f0 (2026-08-01, user-requested) -----
//
// Real, generic quad-draw primitive shared by 31 call sites project-wide (confirmed
// via FindCallers.java) -- NOT safe to intercept unconditionally (same class of
// mistake that made the FUN_00552e70 attempt crash the game). Unlike that case,
// though, this function's own signature is completely clean -- confirmed via both
// decompile AND disassembly (not just decompiler inference): plain stack args, no
// implicit registers, and the caller cleans the stack with `ADD ESP,0x24` right
// after the call at the one cursor-specific site (0x004786d7, inside FUN_00478540,
// confirmed via disassembly to be a standard 5-byte `CALL rel32`, so its return
// address is exactly 0x004786DC) -- definitively __cdecl, safe for a plain
// C-signature MinHook detour, no naked-asm trampoline needed here at all.
//
// Gated by return address (same established technique as this file's own
// kMenuBindKeyCaptureCallerRetAddr, issue #35): only the ONE cursor call site is
// suppressed (skipped entirely, not forwarded) -- every other one of the 31 real
// callers passes through to the real trampoline completely unmodified. See
// overlay_hud.cpp's DrawCustomCursorIfNeeded for what redraws the cursor instead,
// as the last thing drawn each frame.
namespace {
constexpr uintptr_t kCursorDrawCallReturnAddr = 0x004786DC;
void* g_orig_004d48f0 = nullptr;

void __cdecl Hook_004d48f0(void* param_1, void* param_2, void* param_3, float param_4, float param_5,
                             void* param_6, void* param_7, void* param_8, void* param_9)
{
    uintptr_t returnAddr = reinterpret_cast<uintptr_t>(_ReturnAddress());
    if (returnAddr == kCursorDrawCallReturnAddr) {
        // Confirmed live 2026-08-01 via a two-point corner calibration (top-left
        // and bottom-right) that the real bug was a window-client-size-vs-D3D9-
        // viewport-size mismatch in WM_MOUSEMOVE's own coordinate space (see
        // DrawCustomCursorIfNeeded in overlay_hud.cpp for the full fix) -- two
        // earlier theories (the internal DAT_01c00468/046c globals, and a naive
        // GetResolutionScale multiply) were both wrong. User-confirmed correct
        // across the whole screen, not just near the origin.
        return; // suppress the native cursor draw entirely -- this project redraws it instead
    }
    using DrawQuadFn = void(__cdecl*)(void*, void*, void*, float, float, void*, void*, void*, void*);
    reinterpret_cast<DrawQuadFn>(g_orig_004d48f0)(param_1, param_2, param_3, param_4, param_5,
                                                    param_6, param_7, param_8, param_9);
}
} // namespace

// ---- Empirical trace of the REAL menu-script "open" builtin (2026-08-01) ----------
//
// Live-confirmed OpenMenuByName (FUN_00544a50) is the WRONG function to watch:
// across a full test navigating the entire Special Ops flow, it only ever fired for
// "main" and "pausedmenu" -- never "main_specops" or any "popmenu_specops_*", despite
// the real `.menu` files containing literal `open main_specops;`-style script
// actions. This means the compiled `.menu` SCRIPT's own "open" keyword and the
// native C-code menu-opening API (FUN_00544a50, confirmed only called from
// SetMenuState's own hardcoded transitions -- pausedmenu/briefing/victoryscreen) are
// two SEPARATE mechanisms, not the same one as originally assumed. Rather than keep
// guessing candidate functions via call-graph inspection (FUN_00518d00 had 21
// callers -- too generic to be a single opcode's dispatch site), this hooks
// FUN_00486990 (the registry search-by-name every "open"-shaped candidate function
// calls, confirmed via FindCallers.java) directly and logs `_ReturnAddress()` --
// the actual calling function's own return address -- alongside the name being
// searched. This identifies the REAL caller empirically, directly, without needing
// a live debugger session or further blind guessing.
namespace {
using RegistrySearchFn = int(__cdecl*)(void* ctx, const char* name);
void* g_orig_00486990 = nullptr;

int __cdecl Hook_00486990(void* ctx, const char* name)
{
    __try {
        if (LooksLikeValidPointer(reinterpret_cast<uintptr_t>(name))) {
            static char s_lastLoggedName[64] = {};
            static void* s_lastLoggedRetAddr = nullptr;
            void* retAddr = _ReturnAddress();
            if (strncmp(s_lastLoggedName, name, sizeof(s_lastLoggedName) - 1) != 0 ||
                retAddr != s_lastLoggedRetAddr) {
                strncpy_s(s_lastLoggedName, name, _TRUNCATE);
                s_lastLoggedRetAddr = retAddr;
                char buf[160];
                sprintf_s(buf, "[registry-search-diag] FUN_00486990(\"%s\") called from retAddr=%p",
                           name, retAddr);
                LogFromController(buf);
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        // Never let this crash the game over an unexpected name-pointer shape.
    }
    return reinterpret_cast<RegistrySearchFn>(g_orig_00486990)(ctx, name);
}
} // namespace

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
constexpr int kAssetTypeMaterial = 5; // matches FUN_004b6b70's own per-asset-type
    // switch case ordering, already confirmed against OpenAssetTools' IW5_Assets.h
    // union ordering elsewhere in this file's own history (see the big comment
    // above this block).

// 2026-08-17 (tools/ui_harness .menu-renderer project, runtime asset capture) --
// hooks this SAME, already-confirmed-safe, plain __cdecl FindOrLoadAsset (no new
// naked-hook/implicit-register risk) purely to observe which material name is
// currently being loaded, so asset_capture.cpp's CreateTexture hook can correlate
// any texture created while a material load is on the stack back to its real
// name. Read-only in every sense that matters to the real engine: the original
// call always runs with its real arguments/return value completely unmodified,
// this only pushes/pops a name onto asset_capture.cpp's own tracking stack around
// it. No-op (AssetCapture_Push/PopMaterialName themselves check the config flag)
// when captureRuntimeMenuAssets is off, so this hook installs unconditionally but
// costs nothing extra when the feature isn't in use.
void* g_origFindOrLoadAsset = nullptr;

void* __cdecl Hook_FindOrLoadAsset(int assetType, const char* name, int flag)
{
    // 2026-08-17 stutter investigation (frame_benchmark.h) -- this hook installs
    // UNCONDITIONALLY (see this block's own header comment: "genuinely hot,
    // widely-shared function"), regardless of whether captureRuntimeMenuAssets is
    // on, so its own overhead is a real candidate for the "still jittery" report
    // even independent of asset_capture.cpp's CreateTexture hook. Times its own
    // added work (everything except the real call itself, same "don't blame the
    // engine's own cost on us" principle as asset_capture.cpp's CreateTexture
    // timing) and folds it into the SAME assetCaptureMs bucket -- both hooks are
    // part of one feature's total cost for this diagnostic's purposes.
    LARGE_INTEGER benchFreq{}, benchStart{}, benchEnd{};
    QueryPerformanceFrequency(&benchFreq);
    QueryPerformanceCounter(&benchStart);

    bool eligible = (assetType == kAssetTypeMaterial) && name && LooksLikeValidPointer(reinterpret_cast<uintptr_t>(name));
    // Only pop if the push actually succeeded (it can decline at its own depth
    // cap) -- see AssetCapture_PushMaterialName's own header comment for why an
    // unconditional pop here would desync the stack for other, still-legitimately-
    // active nested calls.
    bool pushed = eligible && AssetCapture_PushMaterialName(name);

    QueryPerformanceCounter(&benchEnd);
    double ourOwnMs = (static_cast<double>(benchEnd.QuadPart - benchStart.QuadPart) * 1000.0) / static_cast<double>(benchFreq.QuadPart);

    void* result = reinterpret_cast<FindOrLoadAssetFn>(g_origFindOrLoadAsset)(assetType, name, flag);

    QueryPerformanceCounter(&benchStart); // reuse as a second start marker
    if (pushed) AssetCapture_PopMaterialName();
    QueryPerformanceCounter(&benchEnd);
    ourOwnMs += (static_cast<double>(benchEnd.QuadPart - benchStart.QuadPart) * 1000.0) / static_cast<double>(benchFreq.QuadPart);

    FrameBenchmark_AddAssetCaptureMs(ourOwnMs);
    return result;
}

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

// ---- Boot-thunk resolution diagnostic (2026-07-20, task #23 follow-up) -----------
//
// Read-only, zero-mutation diagnostic for the real controller-options-menu work
// (task #23). The boot-splice hook above (on FUN_004ca310 directly) crashed live --
// root-caused (see re_notes/known_issues.md, "## 22." boot-splice discussion,
// "ROOT CAUSE FOUND, definitively") to FUN_004ca310 being an MSVC incremental-link
// thunk (ILT): `CALL 0x00463430; JMP EAX`, where FUN_00463430 uses the CALLER's own
// return address as an input to a relocation computation, then self-patches the
// CALLER'S 5-byte CALL instruction in place to bypass the thunk on future calls.
// Hooking the thunk (or any new call site that calls it directly) corrupts that
// return-address-dependent math -- this is what crashed the game.
//
// The refined plan (known_issues.md, "REFINED, implementation-ready") is to hook
// FUN_00679680 instead -- a real, ordinary function (confirmed via the cached
// disassembly at D:\Tools\ghidra_projects_bootzone\disasm_00679680.txt: plain
// `SUB ESP,0x78; PUSH EBX; PUSH EBP; PUSH ESI` prologue, no stack-args read, plain
// `RET` epilogue -- a genuine `void __cdecl(void)`, safely trampolineable, no thunk
// involved), let the ORIGINAL run completely unmodified, then read out the
// already-resolved real LoadZones address afterward for logging only.
//
// **Correction to the existing plan, found this pass via the cached decompile of
// FUN_00463430 (D:\Tools\ghidra_projects_bootzone\decomp_463430.txt), not
// re-guessed:** the plan's formula ("read `&DAT_008501e8 + *(int*)&DAT_008501e8`
// to get the already-resolved real function address") is an OVERSIMPLIFICATION.
// That expression (`iVar1` in the decompile) is only an INTERMEDIATE value fed into
// a 134-iteration relocation walk (`FUN_006cc460`) and a further resolver call
// (`FUN_0045e910`) -- the actual value FUN_00463430 returns (and which `JMP EAX`
// then jumps to) is `iVar2 + iVar3`, where `iVar2 = FUN_0045e910(...)`'s return and
// `iVar3 = iVar1 - imageBase`. Logging `&DAT_008501e8 + *DAT_008501e8` alone would
// NOT match the real jump target -- it would just be a misleading intermediate.
// Reimplementing FUN_00463430's full relocation/resolution chain ourselves to get
// the true value would be substantial, fragile, unwarranted work for a read-only
// diagnostic.
//
// **A simpler, more direct diagnostic exists and is what this hook actually
// implements**, using the self-patching behavior described in the ROOT CAUSE
// section directly: "rewrites the CALLER's own CALL 0x004ca310 into
// CALL <real_function> in place, so future executions of that exact call site skip
// the thunk entirely." FUN_00679680's own Call 2 (the unconditional one, at
// `0x006797bd`, return address `0x006797c2` -- both addresses already independently
// confirmed and reused elsewhere in this file, e.g. `kBootZoneSpliceReturnAddr`)
// IS that exact call site. So: let FUN_00679680 run its real, unmodified self
// (including this call, executing under completely normal conditions -- nothing
// about this hook alters that call in any way), then afterward simply READ THE
// BYTES at `0x006797bd` directly. If the ILT theory is right, that 5-byte
// instruction will no longer read `E8 <disp to 004ca310>` -- it'll have been
// self-patched by the engine's OWN normal execution to `E8 <disp to the real,
// final LoadZones function>`. Decoding that displacement gives the true resolved
// address directly, with no need to reimplement any of FUN_00463430's internal
// math. This is also a strictly safer read than dereferencing DAT_008501e8's chain,
// since it's reading straight off .text (always mapped, never freed) rather than
// a data slot whose exact lifetime/reinitialization semantics were never traced.
//
// Both readings are logged for completeness (the plan's original DAT_008501e8
// figure, clearly labeled as not-the-real-target; and this hook's own call-site
// decode, labeled as the actually-trustworthy one) so a future pass can compare
// them without needing to re-derive either from scratch.
//
// Scope, deliberately narrow: this hook does NOT touch the zone array, does NOT
// call the resolved address, does NOT construct or append a zone-queue entry --
// pure after-the-fact logging, per the project's own "log before you ever mutate"
// discipline (the exact lesson both the rumble-hook and boot-splice crashes taught,
// see known_issues.md issues #24 and #22/#30). The actual splice-and-call
// implementation, and confirming this diagnostic's own reading live, are follow-up
// work, not done here.
namespace {
using VoidFn = void(__cdecl*)(void);
VoidFn g_origFUN_00679680 = nullptr;
bool g_bootThunkDiagLogged = false; // fire the diagnostic log at most once

// Fixed, statically-known process addresses (same trust level as this file's
// existing kMenuActiveGateAddr / IsMenuActive -- confirmed-real image addresses,
// not heap pointers, so no LooksLikeValidPointer gate is needed here, consistent
// with that precedent).
constexpr uintptr_t kDAT_008501e8 = 0x008501e8;
constexpr uintptr_t kBootZoneCallSiteAddr = 0x006797bd; // FUN_00679680's Call 2 -- the
                                                          // exact CALL 0x004ca310
                                                          // instruction, same call
                                                          // site kBootZoneSpliceReturnAddr
                                                          // (0x006797c2) already
                                                          // targets the return of.

void __cdecl Hook_FUN_00679680()
{
    g_origFUN_00679680(); // real function, completely unmodified -- runs boot exactly
                            // as the engine intends, including both of its real
                            // internal thunk calls.

    if (g_bootThunkDiagLogged) return;
    g_bootThunkDiagLogged = true;

    char buf[256];

    // Reading #1: the plan's original DAT_008501e8-based formula. Logged for
    // completeness/comparison only -- per the correction above, this is NOT
    // expected to equal the real resolved LoadZones address.
    int32_t datValue = *reinterpret_cast<volatile int32_t*>(kDAT_008501e8);
    uintptr_t iVar1Approx = kDAT_008501e8 + static_cast<uintptr_t>(datValue);
    sprintf_s(buf, "[boot-thunk-diag] DAT_008501e8 raw=0x%08X, &DAT_008501e8+val=0x%08X "
        "(NOTE: per FUN_00463430's real decompile this is only an intermediate, "
        "NOT the final resolved address -- see comment above this function)",
        static_cast<unsigned>(datValue), static_cast<unsigned>(iVar1Approx));
    LogFromController(buf);

    // Reading #2: the actually-trustworthy one -- decode whatever is now sitting at
    // the real call site, after a completely normal, unmodified execution of it.
    const unsigned char* callSiteBytes = reinterpret_cast<const unsigned char*>(kBootZoneCallSiteAddr);
    unsigned char opcode = callSiteBytes[0];
    if (opcode == 0xE8) { // CALL rel32
        int32_t disp;
        memcpy(&disp, callSiteBytes + 1, sizeof(disp));
        uintptr_t callSiteEnd = kBootZoneCallSiteAddr + 5; // rel32 is relative to the
                                                             // NEXT instruction, i.e.
                                                             // the return address --
                                                             // matches the already-
                                                             // confirmed 0x006797c2.
        uintptr_t target = callSiteEnd + static_cast<uintptr_t>(disp);
        sprintf_s(buf, "[boot-thunk-diag] call site 0x%08X is CALL rel32, decoded target=0x%08X "
            "(thunk address for comparison: 0x004ca310)",
            static_cast<unsigned>(kBootZoneCallSiteAddr), static_cast<unsigned>(target));
        LogFromController(buf);
        if (target == 0x004ca310) {
            LogFromController("[boot-thunk-diag] call site target is UNCHANGED (still points at the thunk) -- "
                "either the ILT self-patch theory is wrong, or this specific call site didn't self-patch "
                "the way FUN_0067a690/FUN_00481e50's calls might have (each call site patches independently)");
        } else {
            sprintf_s(buf, "[boot-thunk-diag] call site SELF-PATCHED -- real resolved LoadZones address is 0x%08X. "
                "This is the address a future splice implementation should call directly (never re-hook "
                "the thunk or this call site itself).", static_cast<unsigned>(target));
            LogFromController(buf);
        }
    } else {
        sprintf_s(buf, "[boot-thunk-diag] call site 0x%08X first byte is 0x%02X, not 0xE8 -- not a plain "
            "CALL rel32 anymore (or my address/offset assumption is wrong), cannot decode a target",
            static_cast<unsigned>(kBootZoneCallSiteAddr), opcode);
        LogFromController(buf);
    }
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
//
// CORRECTION (2026-07-21, task #34): "fonts/bigfont" was picked as the patch target
// on the 2026-07-18 assumption it was "the best single guess for menu-title text."
// Fresh Ghidra decompile of the real textfont-int -> Font* selector (FUN_005181e0)
// plus a corpus-wide tally of every real `textfont` value across all 512 dumped
// `.menu` files (D:\Tools\OpenAssetTools\zone_dump) proves that guess wrong: bigfont
// is textfont value 2, used in exactly 3 itemDefs anywhere in ui.ff, all three in
// `ui/ui/brightness_adjust.menu` (the brightness-calibration screen) -- and that
// screen only opens when `!getprofiledata("hasEverPlayed_MainMenu")`, i.e. once per
// profile, ever. The main menu's real button-list/title text uses textfont 3
// (smallfont, 4243 real uses) and textfont 9 (hudsmallfont, 866 uses) -- see
// ui_assets.md's 2026-07-21 entry for the full textfont->font-name table. The
// struct-layout diagnostic below is still valid regardless of which font it targets
// (glyphCount/glyphs/material layout is identical across all 9 registered fonts),
// so this was NOT changed to avoid re-deriving an already-live-confirmed-safe
// target; but don't reuse "fonts/bigfont" as a visual test vehicle without first
// reading known_issues.md issue #34 -- it is real but not a practical, repeatable,
// always-visible screen the way earlier notes assumed.
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

// Issue #48 (2026-07-31): sums direct-indexed glyph advance widths (DiagGlyph.dx,
// codepoint-0x20 direct index -- the confirmed-safe common-ASCII region, same
// convention InjectFontStructDebugTest's own dumpGlyph already relies on) for every
// character strictly before charIndex in `text`, giving the RAW (unscaled -- the real
// draw call's own scale param is not confirmed yet, see the hud-glyph-pos diagnostic)
// pixel-width sum leading up to that character. Returns -1 if any preceding character
// falls outside the guaranteed-direct-index-safe range (space through DEL) rather than
// guess at the sorted-extra lookup FUN_0047dfa0 itself uses for extended characters --
// good enough for common ASCII interact-hint text ("Press F to interact"), not meant
// to handle extended/localized text.
int SumDirectIndexedGlyphWidthsBefore(const DiagFont* font, const char* text, size_t charIndex)
{
    if (!font || !text || !LooksLikeValidPointer(reinterpret_cast<uintptr_t>(font->glyphs))) return -1;
    int sum = 0;
    for (size_t i = 0; i < charIndex; ++i) {
        unsigned char c = static_cast<unsigned char>(text[i]);
        if (c < 0x20 || c > 0x7F) return -1;
        int idx = static_cast<int>(c) - 0x20;
        if (idx < 0 || idx >= 96 || idx >= font->glyphCount) return -1;
        sum += font->glyphs[idx].dx;
    }
    return sum;
}

// Issue #48 (2026-07-31 live-test finding): the drawn hint string itself already
// marks the button-name portion with the engine's own "^N...^7" color-code
// convention (^N = a single-digit highlight color, e.g. ^3 or ^2; ^7 = reset to
// plain white) -- confirmed against real captured strings ("Press^3 F ^7to pick
// up", "Hold ^3F^7 to use Weapon Armory", "Back ^2ESC^7"). This is a more robust
// way to locate the button-name span than cross-referencing the bind-resolver
// hook's separately-resolved text (GetLastResolvedBindKeyName): it's self-
// contained in the exact same string this hook already has, and doesn't depend
// on that other hook's read succeeding (which it did NOT do even once during the
// 2026-07-31 live test -- see this file's own bind-resolver section for that
// separate, still-open problem). NOT a claim that ^N is always color 3 or always
// means "this is a button name" in every possible context -- just the pattern
// actually observed marking button names in every hint string captured so far.
// markerStart/markerEnd (added 2026-07-31, custom-hint-overlay pivot) bound the WHOLE
// "^N...^7" run including both marker tokens themselves -- needed to cleanly split
// the full string into prefix (before markerStart) and suffix (after markerEnd) once
// the highlighted content is being replaced by a real icon instead of drawn as text.
struct ColorHighlightSpan { size_t contentStart; size_t contentLen; size_t markerStart; size_t markerEnd; bool found; };

ColorHighlightSpan FindColorHighlightSpan(const char* text, size_t textLen)
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

// Language-independent replacement for matching rendered UI text against a hardcoded
// English literal (issue #68, 2026-08-05 language pass -- see known_issues.md for the
// full root-cause writeup: several hint-detection sites broke entirely under a
// non-English game language because they matched literal English words like "Reload"/
// "Quit"/"Leaderboards" against the game's own LOCALIZED on-screen text). Resolves the
// real internal reference key (e.g. "MENU_QUIT" -- found via the zone_dump
// localizedstrings extraction, real keys confirmed against real .str data, not
// guessed) through the engine's own SEH_GetString-equivalent (GetLocalizedString,
// real_settings.h/.cpp) and compares against THAT live-resolved text instead -- this
// is correct automatically for every language the game supports, since the engine
// itself does the translation; no per-language table to maintain.
// `caseSensitive` defaults to false (most hint text comparisons don't care), but the
// Quit corner-hint specifically needs case-SENSITIVE matching to avoid colliding with
// the Special Ops hub's own separate all-caps "QUIT" item (see that call site's own
// comment) -- exposed as a parameter rather than hardcoding one behavior here.
bool RenderedTextMatchesReferenceKey(const char* renderedText, const char* referenceKey, bool caseSensitive = false)
{
    const char* resolved = GetLocalizedString(referenceKey);
    if (!resolved || !LooksLikeValidPointer(reinterpret_cast<uintptr_t>(resolved))) return false;
    return caseSensitive ? strcmp(renderedText, resolved) == 0 : _stricmp(renderedText, resolved) == 0;
}

// Prefix variant for hints where the real localized string is a TEMPLATE with an
// embedded "^N...^7" bind reference following a fixed leading phrase (e.g.
// `PLATFORM_LEADERBOARDS_SHORTCUT` = "Leaderboards ^2Right Mouse^7/^2F1^7") -- compares
// only up to the template's own first '^' marker (or its full length if it has none),
// so this stays correct even if the embedded bind text itself differs (different
// default bind, different controller layout wording, etc).
bool RenderedTextMatchesReferenceKeyPrefix(const char* renderedText, const char* referenceKey)
{
    const char* resolved = GetLocalizedString(referenceKey);
    if (!resolved || !LooksLikeValidPointer(reinterpret_cast<uintptr_t>(resolved))) return false;
    const char* caret = strchr(resolved, '^');
    size_t prefixLen = caret ? static_cast<size_t>(caret - resolved) : strlen(resolved);
    if (prefixLen == 0) return false;
    return _strnicmp(renderedText, resolved, prefixLen) == 0;
}

// Structural match for a template with an embedded, real substitution marker (issue
// #68 follow-up, 2026-08-05 live retest, generalized 2026-08-06 for the audit --
// "look for any other similar cases... a more universal robust pipeline"). Two real
// marker styles confirmed to exist in this engine's own localized strings, both
// eventually resolved through the same bind-resolver (re_notes/ui_assets.md):
// `"&&1"` (e.g. PLATFORM_MANTLE = "Press^3 &&1 ^7to  ") and `"[{+command}]"` (e.g.
// SENTRY_PLACE = "Press ^3[{+attack}]^7 to place the turret."). Either way the real
// engine splices a bind's own display text in for the marker before drawing, and a
// live Italian screenshot already proved word-based key names ARE translated for
// this splice ("Premi Spazio per" instead of "Press SPACE to") -- so matching the
// SUBSTITUTED key text itself breaks under any language that translates that word.
// This instead compares the parts of the template OUTSIDE the marker -- which never
// change regardless of what gets spliced in -- against the real rendered text's own
// matching prefix/suffix, position-based (not content-based), so it's correct for
// any substituted key length in any language: `renderedText` must start with the
// template's own text before the marker and end with its text after it, verbatim.
bool RenderedTextMatchesSubstitutionTemplateWithMarker(const char* renderedText, const char* referenceKey, const char* marker)
{
    const char* tmpl = GetLocalizedString(referenceKey);
    if (!tmpl || !LooksLikeValidPointer(reinterpret_cast<uintptr_t>(tmpl))) return false;
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

// Thin wrapper for the "&&1" marker style, the one every existing caller uses.
bool RenderedTextMatchesSubstitutionTemplate(const char* renderedText, const char* referenceKey)
{
    return RenderedTextMatchesSubstitutionTemplateWithMarker(renderedText, referenceKey, "&&1");
}

// Restricts the custom hint-overlay replacement (issue #48/#49) to the two real fonts
// actually used for in-game gameplay HUD hints -- confirmed via hud-font-id live
// captures 2026-07-31: "Press F to pick up"/"Hold F to use Weapon Armory" both use
// fonts/extraBigFont; Survival's "Press F5 to ready up" uses fonts/hudSmallFont. Main-
// menu UI hints ("Friends ^2F^7", "Back ^2ESC^7") use fonts/smallFont, and main-menu
// titles use fonts/hudBigFont -- explicitly EXCLUDED: the user confirmed this
// shouldn't apply to menu UI at all (console used a different button there anyway --
// Y for Friends, not X -- so even this project's own gameplay keybind table would map
// it wrong if it were allowed to apply).
// hudBigFont added 2026-07-31: the flashing "Reload" reminder (no "^N...^7" span of
// its own -- a completely different detection path, see the Reload-specific block in
// Hook_DrawGlyphText) uses this font too. Safe to include here even though main-menu
// titles also use it, since those never match either detection path (no span, and
// never literally say "Reload").
//
// bigFont added 2026-08-08 (issue #70 family): live-reported "pickup weapon and
// throw grenade prompts don't show at 4:3, reload does" -- confirmed directly via
// proxy_d3d9.log, not guessed. The exact same native "^3G or Middle Mouse ^7throw
// back" hint uses fonts/extraBigFont at 16:9 but switches to fonts/bigFont at
// 800x600/640x480 -- the real engine picks a different (presumably smaller) font
// asset for this HUD text at non-16:9 resolutions. fonts/bigFont wasn't in this
// allowlist, so IsGameplayHintFont rejected it at those resolutions, skipping the
// whole replacement block and leaving the native PC-keybind text ("G or Middle
// Mouse") on screen instead of the controller icon -- exactly the reported symptom
// (a controller player sees no controller-appropriate prompt at all). Reload was
// unaffected because it uses fonts/hudBigFont at both resolutions, already
// allowlisted above.
// normalFont added 2026-08-08 (same issue, live-reported "still the same at 480p
// no visual changes" right after the bigFont fix above): the real engine steps
// through MULTIPLE font tiers as resolution shrinks, not just one -- the SAME
// grenade-throwback hint confirmed via proxy_d3d9.log uses fonts/extraBigFont at
// 16:9, fonts/bigFont at 800x600, and fonts/normalFont at 640x480. fonts/normalFont
// is a very commonly-used font elsewhere (menus, general HUD text), but the
// existing !IsMenuActive() gate around this whole block plus the specific
// reference-key/template structural matching each hint still has to pass
// (RenderedTextMatchesSubstitutionTemplate against an exact known string, or
// TryGetGlyphAssetNameForKeyName resolving an exact known key name) keeps false
// positives on unrelated gameplay text using this same common font very unlikely.
bool IsGameplayHintFont(const DiagFont* font)
{
    if (!font || !LooksLikeValidPointer(reinterpret_cast<uintptr_t>(font->fontName))) return false;
    return _stricmp(font->fontName, "fonts/extraBigFont") == 0 ||
           _stricmp(font->fontName, "fonts/hudSmallFont") == 0 ||
           _stricmp(font->fontName, "fonts/hudBigFont") == 0 ||
           _stricmp(font->fontName, "fonts/bigFont") == 0 ||
           _stricmp(font->fontName, "fonts/normalFont") == 0;
}

// Menu-hint counterpart to IsGameplayHintFont above (issue #48, menu-glyph pass,
// 2026-08-01) -- fonts/smallFont is the real font used for menu UI hints like
// "Back ^2ESC^7"/"Friends ^2F^7" (confirmed via hud-font-id captures), previously
// EXCLUDED entirely from the custom-hint-overlay pipeline (see IsGameplayHintFont's
// own comment) because the gameplay bind table would have mapped these keys wrong.
// Now handled through its own separate detection block (below, in
// Hook_DrawGlyphText) that resolves through ResolveMenuGlyphAssetNameForKeyName
// instead of the gameplay table.
bool IsMenuHintFont(const DiagFont* font)
{
    if (!font || !LooksLikeValidPointer(reinterpret_cast<uintptr_t>(font->fontName))) return false;
    return _stricmp(font->fontName, "fonts/smallFont") == 0;
}
} // namespace

// Forward declarations -- defined near the bind-resolver hook (issue #35) further down
// this file. Issue #48's position-logging diagnostic (in Hook_DrawGlyphText, above
// that hook in file order) needs to cross-reference the last-resolved key name (e.g.
// "F") against the drawn hint text to find which character it corresponds to, and its
// own icon-drawing block needs to map a color-highlighted key name to a real glyph
// asset name.
const char* GetLastResolvedBindKeyName();
bool TryGetGlyphAssetNameForKeyName(const char* keyName, char* outAssetName, size_t outSize);
bool TryGetMenuGlyphAssetNameForKeyName(const char* keyName, char* outAssetName, size_t outSize);
// Issue #68 follow-up (2026-08-05): resolves the mantle hint's icon directly via its
// known LogicalAction::Jump mapping, bypassing the translated-key-name text lookup
// entirely -- see RenderedTextMatchesSubstitutionTemplate's own comment for why the
// key-name lookup can't be trusted for this hint under other languages.
bool TryGetMantleGlyphAssetName(char* outAssetName, size_t outSize);
// Same technique, for the grenade-throwback hint's known LogicalAction::Lethal mapping.
bool TryGetThrowbackGlyphAssetName(char* outAssetName, size_t outSize);
// Same technique, for the turret-placement hint's known LogicalAction::Fire mapping.
bool TryGetSentryPlaceGlyphAssetName(char* outAssetName, size_t outSize);

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

// ---- Live HUD-text font identification (2026-07-21, task #6/#34 follow-up) --------
//
// issue #34 found the glyph-patch mechanism test's target (fonts/bigfont) was wrong
// -- real interact-hint/HUD text doesn't render through it. Static tracing this pass
// (FUN_00568110, the real weapon-pickup/swap hint-string builder -> FUN_005682f0,
// its one real caller and the actual interact-hint HUD-element drawer -> FUN_0051f6c0
// -> FUN_005342a0 -> FUN_0051b100, which writes an opcode-0x11 "print text" entry
// into a deferred render-command ring buffer (DAT_021ddf30) rather than drawing
// directly -> FUN_00691ca0, the real consumer that walks that ring buffer and reads
// each entry's font pointer from byte offset +0x10 -> FUN_00690c80, which passes
// that pointer straight into FUN_0047dfa0, the already-confirmed real glyph-lookup
// function) proves the font for this class of HUD text is NOT selected via the
// generic textfont-int/FUN_005181e0 menu-itemDef mechanism at all -- it's threaded
// as an explicit argument from a generic, data-driven HUD-element render pipeline.
// The ultimate origin (whatever populates that data-driven element's font field)
// wasn't fully traced -- FUN_005096d0, a 24-parameter generic HUD-element dispatcher,
// is as far upward as this pass reached before concluding further static tracing
// wasn't the efficient path.
//
// Rather than keep chasing the static trace, this hooks the actual render call
// directly: FUN_00690c80's own disassembly (`PUSH EBP; MOV EBP,ESP; AND ESP,
// 0xfffffff8; SUB ESP,0x94`, EBP-relative stack args throughout) confirms a plain,
// ordinary function, no thunk involved -- safe to hook by this project's own
// established standard (same class of confirmation already applied to
// FUN_00679680 this session). Its 4th argument is the real, live Font_s* for
// whatever text is being drawn RIGHT NOW -- confirmed via `*(undefined4*)(param_4+0xc)`
// matching the real Font_s.material field at +0xC exactly. Logging its fontName
// (DiagFont+0x00, already-confirmed struct layout, reused directly) every time it
// CHANGES will empirically reveal every real font that renders during actual play --
// including, whenever an interact hint is genuinely on screen, exactly which one
// that is -- without any further static tracing. Zero mutation: forwards to the
// real trampoline completely unmodified regardless of what's read or logged.
namespace {
using DrawGlyphTextFn = void(__cdecl*)(
    const char* param_1, float param_2, float param_3, void* fontArg, float param_5,
    float param_6, float param_7, float param_8, float param_9, int param_10,
    unsigned param_11, int param_12, unsigned param_13, float param_14, unsigned param_15,
    unsigned param_16, unsigned param_17, unsigned param_18, unsigned param_19,
    unsigned param_20, unsigned param_21);
constexpr uintptr_t kDrawGlyphTextAddr = 0x00690c80;
DrawGlyphTextFn g_origDrawGlyphText = nullptr;

char g_lastLoggedHudFontName[64] = {};

// Issue #48 (2026-07-31): separate dedup state for the new position-logging
// diagnostic below -- dedup'd by DRAWN TEXT changing, not by font changing, since
// the goal here is "what are the real x/y/scale/color params for THIS specific
// interact-hint string", not "when does the font change".
//
// FIXED (2026-08-18, live-reported stutter during combat): the original design
// compared only against the SINGLE most-recently-logged string, which does NOT
// give "once per distinct string" in practice -- a real HUD screen cycles through
// MANY different text elements per frame (title, timer, multiple button labels),
// so each one differs from whatever the immediately-previous draw call logged and
// re-logs essentially every frame. Live evidence: a single combat session produced
// 423,063 of this diagnostic's own lines (91% of the entire log file), confirmed
// via direct correlation with the exact symptom reported ("stutter when we get
// shot or are approaching enemies" -- combat is precisely when HUD text churns
// fastest: hit markers, kill feed, ammo count). Replaced with a real seen-SET
// (fixed-size, non-STL, same "stop tracking new entries once full rather than
// overflow" degrade-gracefully pattern already established in asset_capture.cpp's
// AlreadyCaptured/MarkCaptured) so each genuinely distinct string logs exactly
// once per session, not once per differs-from-the-single-prior-call. A
// continuously-changing string (e.g. a live countdown timer) will still produce
// new entries every tick since each one IS genuinely distinct content -- the seen-
// table's own fixed capacity caps the damage from that case too, once full.
constexpr int kMaxLoggedGlyphPosTexts = 64;
char g_loggedGlyphPosTexts[kMaxLoggedGlyphPosTexts][128];
int g_loggedGlyphPosTextCount = 0;

bool AlreadyLoggedGlyphPosText(const char* text)
{
    for (int i = 0; i < g_loggedGlyphPosTextCount; ++i) {
        if (strncmp(g_loggedGlyphPosTexts[i], text, sizeof(g_loggedGlyphPosTexts[i]) - 1) == 0) return true;
    }
    return false;
}

void MarkGlyphPosTextLogged(const char* text)
{
    if (g_loggedGlyphPosTextCount >= kMaxLoggedGlyphPosTexts) return; // degrade
        // gracefully -- stop tracking NEW distinct strings once the fixed table
        // fills rather than overflow; caps a runaway continuously-changing string
        // (e.g. a live timer) at kMaxLoggedGlyphPosTexts lines instead of unbounded.
    strncpy_s(g_loggedGlyphPosTexts[g_loggedGlyphPosTextCount], text, _TRUNCATE);
    ++g_loggedGlyphPosTextCount;
}

// Issue #48/#49 (2026-07-31 follow-up): "read the weapon name live, no hardcoding"
// -- the weapon name that follows a pickup/swap hint (e.g. " Model 1887") draws as
// its OWN separate Hook_DrawGlyphText call, with no "^N...^7" span of its own, so it
// was never touched by the suppression logic above and kept rendering natively
// (independently positioned from our own replacement). Detected here instead by a
// simple, tight heuristic: whenever a hint IS suppressed below, remember its real
// font pointer and p3 (screen row); the very next call this frame that shares BOTH
// exactly and has no highlight span of its own is treated as that hint's real
// continuation text -- suppressed too, and its actual live string content appended
// to the already-pending composite via AppendCustomHintSuffix (never a hardcoded
// weapon name -- whatever the game's real string says is what gets appended).
// One-shot per hint (cleared once consumed or once a new hint is suppressed).
void* g_awaitingHintContinuationFont = nullptr;
float g_awaitingHintContinuationP3 = 0.0f;
bool g_awaitingHintContinuation = false;
GameplayHintSlotId g_awaitingHintContinuationSlot = GameplayHintSlotId::Interact; // BUG-004 follow-up: which named slot armed this

// Reload-vs-interact-hint suppression (originally live-reported 2026-07-31) now
// lives entirely in DrawGameplayHintSlotsIfRequested (overlay_hud.cpp), decided once
// per frame AFTER every Hook_DrawGlyphText call for that frame has already populated
// its slot -- naturally immune to which of Reload/interact happened to be processed
// first within the frame, which is exactly the ordering problem the old
// WasInteractHintRecentlyActive() 100ms-window heuristic existed to route around (see
// git history / PATCHNOTES for that version if the old approach is ever needed for
// reference). See BUG-004 in re_notes/known_issues.md.

// Live-captured 2026-08-01 (proxy_d3d9.log): the real "Back ^2ESC^7" hint's own
// native REAL screen-space position, at 2560x1440 -- the "standard place" the user
// asked for the synthetic Back hint below to reuse, so it lines up exactly with
// wherever a REAL corner hint would have appeared instead.
//
// 2026-08-08, round 4 (REVERTED, round 5): briefly "fixed" by dividing this
// constant by an assumed 2560/1920 capture-time scale, on the theory that
// "2560x1440" meant the real D3D9 device viewport was that size at capture.
// **Confirmed wrong via direct proxy_d3d9.log evidence** (two adjacent
// [res-scale] lines from the same moment): this user's window/monitor is
// 2560x1440, but the REAL device viewport -- the actual ground truth
// GetResolutionScale(device,...) uses everywhere, including at capture time --
// has always been 1920x1080. "2560x1440" in the original 2026-08-01 comment
// described the monitor, not the viewport. At the real capture-time scale
// (1920x1080 vs the 1920x1080 reference, i.e. exactly 1.0), the raw captured
// value WAS ALREADY a correct design-space number -- dividing it by 4/3 was a
// genuine regression, live-reported as "now back and leaderboard are broken"
// (Leaderboards/Quit detection also reads this same constant, via
// looksLikeCornerHintRow below). Reverted to the raw captured value.
constexpr float kStandardCornerHintX = 1634.0f;
constexpr float kStandardCornerHintY = 995.0f;

// Live-reported 2026-08-08 (640x480/800x600): "the a glyphs on main menu work
// correctly. the back glyph we inject on the spec ops select mode screen... works
// fine, but every other corner glyph in the mod is completely out of position like
// way up to the left." Root cause: `RequestMenuHintOverlay`'s whole contract expects
// DESIGN-SPACE (1920-wide-reference) input -- `DrawOneMenuHintSlot`
// (overlay_hud.cpp) multiplies whatever x/y it receives by `scaleX`/`scaleY` exactly
// once. `kStandardCornerHintX`/`Y` above (used by the synthetic Back hint, which is
// confirmed working) are a fixed reference number consistently divided by 1920 every
// time -- a stable, resolution-independent FRACTION regardless of what resolution
// they were originally eyeballed against. `param_2`/`param_3` (used by every OTHER
// corner hint below -- Quit, Leaderboards, Friends/Game Summary/Back-shortcut) are
// NOT a fixed reference -- they're the real engine's own LIVE, PER-FRAME reported
// screen-space x/y for that exact hint at the CURRENT real resolution (confirmed via
// this project's own prior live-capture research: "param_2/param_3 = real
// screen-space x/y", repeated/consistent across many draws of the same string).
// Passing an ALREADY-real, already-correctly-positioned value through the SAME
// single-multiply pipeline double-scales it -- invisible at 16:9 (scaleX/scaleY are
// always ~1.0 there, so multiplying by ~1.0 twice is still ~a no-op) but severely
// wrong at any other aspect ratio (640x480: scaleX=0.333, so a real x of ~545 near
// the right edge becomes ~182, landing well toward the top-left instead of staying
// in the real corner -- exactly the reported symptom). Converts back to the
// design-space-equivalent value here (dividing by the SAME scale
// `DrawOneMenuHintSlot` will re-apply) so the overall pipeline still only ever
// scales real input once, regardless of which of the two position sources fed it.
void ConvertRealScreenPosToDesignSpace(float realX, float realY, float& outDesignX, float& outDesignY)
{
    float scaleX = 1.0f, scaleY = 1.0f;
    // 2026-08-08 fix (live-reported "you broke 16:9" after this function first
    // shipped): this used to pass a null device, which falls back to the real game
    // window's own GetClientRect -- a DIFFERENT resolution source than the real D3D9
    // viewport the eventual draw call (DrawOneMenuHintSlot, via Hook_EndScene's own
    // GetResolutionScale(device, ...)) re-multiplies by. The divide here and the
    // multiply there only cancel out to reproduce the real position exactly when both
    // use the SAME scale -- GetClientRect and the viewport can disagree (window
    // chrome, DPI virtualization on this non-DPI-aware 2011 game, a render
    // resolution set independently of window size), which silently broke this even
    // at "16:9" whenever the two sources disagreed. Fixed by using the actual device
    // Hook_EndScene already captured this frame, so both sides always agree.
    GetResolutionScale(GetLastKnownRenderDevice(), scaleX, scaleY);
    outDesignX = (scaleX > 0.0001f) ? (realX / scaleX) : realX;
    outDesignY = (scaleY > 0.0001f) ? (realY / scaleY) : realY;
}

// ---- In-game menu-glyph position editor (2026-08-16, issue #51 follow-up) --------
//
// User-requested: "we can finally finish our menu glyphs properly ... use that click
// and drag thing we did to make it accurate per screen" -- reusing the same drag-a-
// handle UX already proven live for the harness-only controller-diagram editor
// (DiagramEditor_ToggleEditMode, overlay_hud.cpp/.h, issue #66), but wired into the
// REAL game this time (explicit follow-up: "that's one of the most useful tools even
// for in engine work as i can give you exact guaranteed feedback") instead of a
// disconnected offline harness -- so the screens kManualGlyphPositions' own
// "Deliberately NOT covered this pass" list left uncalibrated can finally be fixed by
// actually navigating to them and dragging the real icon into place live, replacing
// the old MiniDumpWriteDump-based process entirely.
//
// Gated behind g_modConfig.glyphPositionEditMode (default OFF, see mod_config.h) --
// same rationale as the harness tool being harness-only: an accidental drag mid-game
// must never be able to silently corrupt a real, already-correct calibrated position
// for a normal player. Deliberately bypasses kVerifiedGlyphGroups' allowlist and does
// NOT require an existing kManualGlyphPositions entry -- the whole point is calibrating
// groups that aren't on either yet.
//
// Coordinate space: exactly mirrors kManualGlyphPositions' own convention -- itemX/Y
// stored here are the RAW pre-offset/pre-nudge table values (iconOffsetX=0 for every
// edit-tool-authored entry; any desired offset ends up folded directly into each
// index's own X, an equally valid representation). visX/visY (what's actually drawn
// and what the mouse is compared against) always re-derive from raw the same way
// TryGetManualGlyphPosition does at runtime, so what's dragged on screen is exactly
// what will render once pasted back into the real table.
namespace {
constexpr int kGlyphEditMaxGroups = 24;
struct GlyphEditGroup {
    bool used = false;
    char groupName[64] = {};
    int requiredDepth = -1;
    int count = 0; // highest real index + 1 touched this session
    float itemX[kManualGlyphMaxItems] = {};
    float itemY[kManualGlyphMaxItems] = {};
    bool captured[kManualGlyphMaxItems] = {}; // true once seeded/dragged this session
    // Text-readout handle position (2026-08-16 follow-up, "text should be draggable
    // too") -- independent of the icon handle above, own drag target, own default
    // (offset above the icon so the two don't start on top of each other). Purely a
    // calibration-session visibility aid (lets the readout be moved clear of other
    // list items/icons); NOT part of kManualGlyphPositions and never exported --
    // that table has no separate text-position field, and the real native list text
    // this project draws icons next to is never touched or repositioned by this tool.
    float textX[kManualGlyphMaxItems] = {};
    float textY[kManualGlyphMaxItems] = {};
    bool textCaptured[kManualGlyphMaxItems] = {};
};
GlyphEditGroup g_glyphEditGroups[kGlyphEditMaxGroups];
int g_glyphEditDraggingGroup = -1;
int g_glyphEditDraggingIndex = -1;
bool g_glyphEditDraggingIsText = false; // which of the two handles at [group][index] is held
// Live in-game toggle, F2 -- same key/two-step convention as the harness diagram
// editor (F2 toggle / F3 export). g_modConfig.glyphPositionEditMode is still the
// master gate (F2 is only polled at all while it's on); this is a second, session-
// only on/off on top of it, so the coordinate overlay/drag handles can be toggled
// off to actually navigate a menu normally without the editor's own text cluttering
// every focused item, then back on to resume calibrating.
bool g_glyphEditModeActive = false;

GlyphEditGroup* FindOrCreateGlyphEditGroup(const char* groupName, int depth)
{
    for (auto& g : g_glyphEditGroups) {
        if (g.used && g.requiredDepth == depth && _stricmp(g.groupName, groupName) == 0) return &g;
    }
    for (auto& g : g_glyphEditGroups) {
        if (!g.used) {
            g.used = true;
            strncpy_s(g.groupName, groupName, _TRUNCATE);
            g.requiredDepth = depth;
            g.count = 0;
            for (int i = 0; i < kManualGlyphMaxItems; ++i) { g.captured[i] = false; g.textCaptured[i] = false; }
            return &g;
        }
    }
    return nullptr; // table full -- extremely unlikely (24 groups), silently ignored
}
} // namespace

// Exposed so d3d9_hook.cpp's WndProc subclass can swallow real mouse-click messages
// while the editor is active (2026-08-16, live-reported "it skips through the menu"
// -- a click meant to drag a calibration handle was also reaching the real menu's
// own native mouse-click support). See HookWndProc's own comment for the full
// reasoning.
extern "C" bool IsGlyphPositionEditModeActive()
{
    return g_glyphEditModeActive;
}

// Writes every group touched this session to exported_glyph_positions.txt, next to
// the DLL, as ready-to-paste kManualGlyphPositions entries. Mirrors
// DiagramEditor_ExportCurrentLayout's own file-writing pattern (overlay_hud.cpp) --
// same "read the file, paste the new entries in" workflow, not manual transcription.
// Indices touched non-contiguously (e.g. index 2 dragged but index 0/1 never focused
// this session) export as 0.0f -- left visible in the raw output rather than silently
// guessed, since a real gap should be revisited, not papered over.
void ExportGlyphEditPositions()
{
    char exeDir[MAX_PATH] = {};
    GetModuleFileNameA(nullptr, exeDir, sizeof(exeDir));
    char* lastSlash = strrchr(exeDir, '\\');
    if (lastSlash) *lastSlash = '\0';
    char outPath[MAX_PATH];
    sprintf_s(outPath, "%s\\exported_glyph_positions.txt", exeDir);

    FILE* f = nullptr;
    if (fopen_s(&f, outPath, "w") != 0 || !f) {
        LogFromController("[glyph-editor] failed to open exported_glyph_positions.txt for writing");
        return;
    }
    fprintf(f, "// Exported from the in-game glyph position editor (F3) -- paste each entry\n"
               "// below into kManualGlyphPositions (analog_input_hooks.cpp), then add the\n"
               "// group to kVerifiedGlyphGroups once confirmed live.\n"
               "// Any index left at 0.0f, 0.0f was never focused/dragged this session --\n"
               "// revisit it rather than shipping as-is.\n\n");
    int exported = 0;
    for (auto& g : g_glyphEditGroups) {
        if (!g.used) continue;
        fprintf(f, "{ \"%s\", %d, 0.0f, %d, {", g.groupName, g.requiredDepth, g.count);
        for (int i = 0; i < g.count; ++i) fprintf(f, "%s%.1ff", i == 0 ? "" : ", ", g.itemX[i]);
        fprintf(f, "}, {");
        for (int i = 0; i < g.count; ++i) fprintf(f, "%s%.1ff", i == 0 ? "" : ", ", g.itemY[i]);
        fprintf(f, "} },\n");
        ++exported;
    }
    if (exported == 0) fprintf(f, "// (nothing dragged yet this session)\n");
    fclose(f);

    // Live-reported 2026-08-16, "export crashed it": this buffer was 64 bytes, but
    // the formatted string ("[glyph-editor] exported %d group(s) to
    // exported_glyph_positions.txt") needs ~68 -- sprintf_s correctly detected the
    // overflow and invoked the CRT's invalid-parameter handler, which by default
    // terminates the process outright rather than corrupting memory. The export file
    // itself (above, plain fprintf/fclose, no fixed-size buffer involved) had already
    // written and closed successfully by this point -- only this trailing success-log
    // line crashed, which is why the exported file was complete and correct even
    // though the game died immediately after.
    char msg[128];
    sprintf_s(msg, "[glyph-editor] exported %d group(s) to exported_glyph_positions.txt", exported);
    LogFromController(msg);
}

// Called once per real rendered frame (from ResetMenuListItemOrdinalForFrame below),
// only while g_modConfig.glyphPositionEditMode is on, for whichever real menu item is
// CURRENTLY focused. Draws (and lets the mouse drag) that item's own glyph icon AND,
// independently, the coordinate-readout text next to it (2026-08-16 follow-up: "also
// text should be draggable too") -- two separate handles at one (group, index) slot,
// same "sprite anchor vs. label text, independently draggable" split the harness
// diagram editor already established (DrawAndEditDiagramAnchors vs.
// DrawAndEditLabelHandles, overlay_hud.cpp) -- only the icon handle's position is
// meaningful to export, though; the text handle only exists so the readout can be
// dragged clear of other on-screen list items/icons while calibrating.
void EditGlyphPositionsForFrame(const char* groupName, int depth, int index, int siblingCount)
{
    if (!groupName || groupName[0] == '\0' || index < 0 || index >= kManualGlyphMaxItems) return;

    GlyphEditGroup* group = FindOrCreateGlyphEditGroup(groupName, depth);
    if (!group) return;
    if (index + 1 > group->count) group->count = index + 1;

    if (!group->captured[index]) {
        float seedVisX = 0.0f, seedVisY = 0.0f;
        if (TryGetManualGlyphPosition(groupName, depth, index, siblingCount, seedVisX, seedVisY)) {
            group->itemX[index] = seedVisX;
            group->itemY[index] = seedVisY - kManualGlyphVerticalNudge;
        } else {
            // No existing calibration -- start at an obviously-placeholder screen-
            // center-ish spot rather than (0,0) off in the corner, so it's immediately
            // findable to drag into place.
            group->itemX[index] = 960.0f;
            group->itemY[index] = 400.0f - kManualGlyphVerticalNudge;
        }
        group->captured[index] = true;
    }

    float& rawX = group->itemX[index];
    float& rawY = group->itemY[index];
    float visX = rawX, visY = rawY + kManualGlyphVerticalNudge;

    if (!group->textCaptured[index]) {
        // Default: above the icon, far enough (>20px) that RequestMenuHintOverlay's
        // own same-position-merge dedup (overlay_hud.cpp, kSamePositionToleragePx)
        // doesn't fold the two separate calls below into one slot on the first frame.
        group->textX[index] = visX;
        group->textY[index] = visY - 40.0f;
        group->textCaptured[index] = true;
    }
    float& textX = group->textX[index];
    float& textY = group->textY[index];

    static bool s_lastLeftMouseHeld = false;
    bool leftMouseHeld = IsLeftMouseButtonHeld();
    bool leftClickEdge = leftMouseHeld && !s_lastLeftMouseHeld;
    s_lastLeftMouseHeld = leftMouseHeld;
    int mouseX = 0, mouseY = 0;
    bool haveMouse = GetLastMouseMoveClientPos(mouseX, mouseY);
    float mouseDesignX = 0.0f, mouseDesignY = 0.0f;
    if (haveMouse) {
        // NOT ConvertRealScreenPosToDesignSpace (this file's own function, built for a
        // native draw call's already-viewport-space param_2/param_3) -- a raw
        // WM_MOUSEMOVE position is real WINDOW CLIENT pixels, a different coordinate
        // system whenever this engine's backbuffer/viewport doesn't match the real
        // window size. See ConvertMouseClientPosToDesignSpace's own comment
        // (overlay_hud.h/.cpp) for the full story -- live-reported 2026-08-16 as "we
        // still cant drag the icon," root-caused via a click-diagnostic log showing
        // the old conversion was silently a no-op here.
        ConvertMouseClientPosToDesignSpace(mouseX, mouseY, mouseDesignX, mouseDesignY);
    }

    if (!leftMouseHeld) { g_glyphEditDraggingGroup = -1; g_glyphEditDraggingIndex = -1; }

    int groupSlot = static_cast<int>(group - g_glyphEditGroups);
    bool isDraggingIcon = (g_glyphEditDraggingGroup == groupSlot && g_glyphEditDraggingIndex == index &&
                            !g_glyphEditDraggingIsText);
    bool isDraggingText = (g_glyphEditDraggingGroup == groupSlot && g_glyphEditDraggingIndex == index &&
                            g_glyphEditDraggingIsText);

    constexpr float kHandleHitRadiusDesign = 32.0f; // design-space units -- resolution-independent,
                                                       // same reasoning as ConvertRealScreenPosToDesignSpace above

    if (haveMouse && leftClickEdge && g_glyphEditDraggingIndex < 0) {
        float iconDx = mouseDesignX - visX, iconDy = mouseDesignY - visY;
        float textDx = mouseDesignX - textX, textDy = mouseDesignY - textY;
        bool hitIcon = iconDx * iconDx + iconDy * iconDy <= kHandleHitRadiusDesign * kHandleHitRadiusDesign;
        bool hitText = textDx * textDx + textDy * textDy <= kHandleHitRadiusDesign * kHandleHitRadiusDesign;
        // Icon takes priority when both handles happen to overlap the click (it's the
        // one that actually matters for export) -- text is the fallback hit.
        if (hitIcon) {
            g_glyphEditDraggingGroup = groupSlot; g_glyphEditDraggingIndex = index; g_glyphEditDraggingIsText = false;
            isDraggingIcon = true;
        } else if (hitText) {
            g_glyphEditDraggingGroup = groupSlot; g_glyphEditDraggingIndex = index; g_glyphEditDraggingIsText = true;
            isDraggingText = true;
        }
        // Debugging follow-up (2026-08-16, "click and drag doesn't seem to do
        // anything visually") -- every real click-edge while the editor is active is
        // rare enough on its own to log unconditionally, no dedup needed; shows
        // exactly what the click was compared against so a miss vs. a dead click can
        // be told apart from the log alone.
        char clickBuf[220];
        sprintf_s(clickBuf, "[glyph-editor-click] mouseRaw(%d,%d) mouseDesign(%.0f,%.0f) "
                             "iconVis(%.0f,%.0f) textVis(%.0f,%.0f) hitIcon=%d hitText=%d",
                   mouseX, mouseY, mouseDesignX, mouseDesignY, visX, visY, textX, textY,
                   hitIcon ? 1 : 0, hitText ? 1 : 0);
        LogFromController(clickBuf);
    }

    if (isDraggingIcon && haveMouse) {
        rawX = mouseDesignX;
        rawY = mouseDesignY - kManualGlyphVerticalNudge;
        visX = rawX;
        visY = rawY + kManualGlyphVerticalNudge;
    }
    if (isDraggingText && haveMouse) {
        textX = mouseDesignX;
        textY = mouseDesignY;
    }

    // Real, exact visual feedback (per explicit user request): draws the ACTUAL glyph
    // icon this position would ship with, at its exact spot, through the same
    // RequestMenuHintOverlay path the real overlay uses at runtime -- plus, as a
    // SEPARATE call at the independently-draggable text handle's position, the live
    // raw icon coordinate as its own on-screen text, so what's on screen doubles as
    // the number to transcribe (or just read straight from the F3 export).
    char aAsset[32] = {};
    bool haveAsset = TryGetMenuGlyphAssetNameForKeyName("ENTER", aAsset, sizeof(aAsset));
    if (haveAsset) {
        RequestMenuHintOverlay(visX, visY, "", "", aAsset);
    }
    char coordText[64];
    sprintf_s(coordText, "[d%d i%d/%d] %.0f,%.0f%s%s ", depth, index, siblingCount, rawX, rawY,
               isDraggingIcon ? " DRAG" : "", isDraggingText ? " TXT-DRAG" : "");
    RequestMenuHintOverlay(textX, textY, coordText, "", "");

    // Bounding-box drag handles (2026-08-16, user direction: "add the bounding boxes
    // like in our harness") -- same translucent-quad-plus-label visual as the harness
    // diagram editor's own anchors (DrawAndEditDiagramAnchors, overlay_hud.cpp), drawn
    // at the SAME radius the hit-test above actually uses, so what's visibly boxed IS
    // the real clickable area, not just a decoration that could silently drift out of
    // sync with it. Green while actively held, orange otherwise -- identical color
    // convention to the harness anchors.
    RequestGlyphEditHandleBox(visX, visY, kHandleHitRadiusDesign,
        isDraggingIcon ? 0x9000FF00u : 0x90FFA000u, "ICON");
    RequestGlyphEditHandleBox(textX, textY, kHandleHitRadiusDesign,
        isDraggingText ? 0x9000FF00u : 0x9000AAFFu, "TEXT");

    static bool s_lastF3Held = false;
    bool f3Held = (GetAsyncKeyState(VK_F3) & 0x8000) != 0;
    bool f3Edge = f3Held && !s_lastF3Held;
    s_lastF3Held = f3Held;
    if (f3Edge) ExportGlyphEditPositions();
}

// Live-reported 2026-08-01: "the modal has no back[,] we need to add one in the
// standard place" -- unlike Back/Friends (real native "^N...^7" hints this project
// can intercept and replace), some modals (e.g. "Choose Game Mode" over Special
// Ops) have NO native corner hint at all to hook -- there's nothing to suppress or
// redraw, so a hint has to be synthesized from nothing. Whenever a menu is open
// (IsMenuActive(), the same gate B's own real ESC-forward already relies on --
// confirmed all session to correctly exclude the root main menu, since there's
// nowhere to back out of there) and no REAL corner hint has fired recently (i.e.
// this specific screen doesn't already have its own native Back/Friends), requests
// this project's own "Back" hint at the same standard position. B's ESC-forward
// itself (InjectControllerMenuBack) is completely untouched by this -- pressing B
// already correctly closes any menu regardless of whether a hint is visible for it;
// this only adds the missing VISUAL indicator.
void RequestSyntheticBackHint()
{
    char assetName[32] = {};
    if (!TryGetMenuGlyphAssetNameForKeyName("ESC", assetName, sizeof(assetName))) return;
    constexpr float kMenuHintVerticalNudge = -18.0f; // matches the real corner hints' own empirical nudge
    RequestMenuHintOverlay(kStandardCornerHintX, kStandardCornerHintY + kMenuHintVerticalNudge, "Back ", "", assetName);
}

extern "C" void __cdecl InjectSyntheticBackHintIfNeeded()
{
    if (!ShouldDrawGlyphOverlay()) return;
    if (!IsMenuActive()) return;
    // Live-reported 2026-08-01: simplified per explicit user direction after the
    // focus-struct-based nested-modal detection was confirmed live to not apply to
    // Special Ops' tile-row navigation at all. **Doc-audit correction, same day:**
    // the OpenMenuByName-based g_inSpecOpsFlow this used to gate on was confirmed
    // dead (never once true in any logged session) -- replaced with
    // IsInsideSpecOpsNestedModal(), confirmed live via getfocuseditemname(). Friends
    // is explicitly suppressed for the same condition in the menu-hint detection
    // block above, so there's no competing native hint to worry about overriding
    // incorrectly.
    if (!IsInsideSpecOpsNestedModal()) return;
    RequestSyntheticBackHint();
}

// See the big comment above g_menuListItemOrdinalThisFrame. Called from
// overlay_hud.cpp's Hook_EndScene, once per real rendered frame.
extern "C" void __cdecl ResetMenuListItemOrdinalForFrame()
{
    // REMOVED 2026-08-08 (log-slimming pass, issue #67 -- proxy_d3d9.log had grown to
    // 22GB): this used to log `[ordinal-hypothesis-diag]` unconditionally every frame
    // while any popup was stacked (depth > 1), testing the BUG-051 "active menu's
    // real items are always the LAST N ordinals" hypothesis (2026-08-02). That
    // hypothesis was definitively REFUTED the same day it was added (real captures
    // showed activeMenuItemCount as high as 81 against frameTotalOrdinals as low as
    // 6 -- menu+0xa8 counts EVERY itemDef, not just selectable rows, a fundamental
    // population mismatch no amount of more data could fix) and issue #51 was
    // resolved via a completely different approach (kManualGlyphPositions, a
    // per-group calibrated table, see the block below). This diagnostic has logged
    // dead-end data for every nested-popup frame of every session since -- deleted
    // outright, not just gated, since re-enabling it could never produce anything
    // useful again.

    // Manual per-group A-glyph position (2026-08-02, issue #51) -- fires once per
    // frame, completely decoupled from the ordinal/draw-call matching above, for any
    // group with a real calibrated entry in kManualGlyphPositions. See that table's
    // own comment for why this replaces ordinal-derived position for these groups
    // rather than being a temporary stand-in.
    //
    // Focus signal FIXED 2026-08-03: originally used the same getfocuseditemname()
    // (g_focusedItemName) cross-check the older ordinal-based path uses -- live-
    // confirmed broken for this purpose (not just this one screen): CAMPAIGN_BUTTON_LIST
    // never calls getfocuseditemname() from its own .menu script at all, so
    // g_focusedItemName sat frozen on the LAST screen that happened to trigger it
    // (e.g. "game_select_button_2" from the main menu), never becoming true for
    // CAMPAIGN_BUTTON_LIST's own items no matter how long you sat on that screen.
    // Replaced with TryGetRealFocusedGroupAndIndex(), a direct itemDef-array memory
    // read (same technique this project's own live-memory-dump calibration tooling
    // already proved reliable) -- always live, every frame, regardless of whether
    // any script ever calls the native function.
    if (kUseAutomaticListGlyphPositioning) {
        // NEW automatic path (2026-08-03) -- see TryGetAutomaticGlyphPosition's own
        // big comment. Runs once per frame, after Hook_DrawGlyphText has already
        // buffered every candidate this frame drew (g_autoGlyphCandidates).
        static char s_lastAutoDiagKey[200] = "";
        bool overlayOn = ShouldDrawGlyphOverlay();
        char realGroup[64] = {};
        int realIndex = -1, siblingCount = -1, unusedDepth = -1;
        // Debounced -- see TryGetStableFocusedGroupAndIndex's own comment (same
        // flicker fix applied to the manual path below).
        bool haveFocus = overlayOn &&
            TryGetStableFocusedGroupAndIndex(realGroup, sizeof(realGroup), unusedDepth, realIndex, siblingCount);
        float autoX = 0.0f, autoY = 0.0f;
        bool havePos = haveFocus && TryGetAutomaticGlyphPosition(realIndex, autoX, autoY);
        char dkey[220];
        sprintf_s(dkey, "%d|%d|%d|%s|%d|%d|%d", overlayOn ? 1 : 0, haveFocus ? 1 : 0,
                  havePos ? 1 : 0, realGroup, realIndex, siblingCount, g_autoGlyphCandidateCount);
        if (strcmp(dkey, s_lastAutoDiagKey) != 0) {
            strncpy_s(s_lastAutoDiagKey, dkey, _TRUNCATE);
            char dbuf[280];
            sprintf_s(dbuf, "[auto-glyph-diag] overlayOn=%d haveFocus=%d havePos=%d "
                            "realGroup=\"%s\" realIndex=%d siblingCount=%d candidates=%d x=%.1f y=%.1f",
                      overlayOn ? 1 : 0, haveFocus ? 1 : 0, havePos ? 1 : 0,
                      realGroup, realIndex, siblingCount, g_autoGlyphCandidateCount, autoX, autoY);
            LogFromController(dbuf);
        }
        // Suppressed while the in-game glyph editor is actively toggled on
        // (2026-08-16, live-reported "the old one draws back and it duplicates to
        // two") -- EditGlyphPositionsForFrame below already draws (a possibly
        // dragged-away-from-here) icon for this exact same currently-focused item;
        // leaving this shipped draw active too meant the ORIGINAL, un-dragged
        // position kept redrawing every frame alongside the one actually being
        // dragged, once they were far enough apart to stop sharing a slot.
        if (havePos && !g_glyphEditModeActive && !IsGlyphDisabledGroup(realGroup)) {
            char aAsset[32] = {};
            if (TryGetMenuGlyphAssetNameForKeyName("ENTER", aAsset, sizeof(aAsset))) {
                RequestMenuHintOverlay(autoX, autoY, "", "", aAsset);
            } else {
                static bool s_loggedAutoAssetFail = false;
                if (!s_loggedAutoAssetFail) {
                    s_loggedAutoAssetFail = true;
                    LogFromController("[auto-glyph-diag] TryGetMenuGlyphAssetNameForKeyName(\"ENTER\") FAILED");
                }
            }
        }
        g_autoGlyphCandidateCount = 0;
    } else {
        // OLD manual-table path -- fully preserved, unchanged. See
        // kManualGlyphPositions' own comment for the full history. Kept available
        // behind kUseAutomaticListGlyphPositioning=false until the automatic path
        // above is confirmed reliable across every screen this one already covers.
        static char s_lastDiagKey[200] = "";
        bool overlayOn = ShouldDrawGlyphOverlay();
        char realGroup[64] = {};
        int manualSelIndex = -1;
        int siblingCount = -1;
        int manualDepth = -1;
        // Debounced (2026-08-16, live-reported "it goes to the set position but
        // after x amount of time [it] moves" during ordinary play, not editing) --
        // see TryGetStableFocusedGroupAndIndex's own comment. The raw per-frame
        // itemDef read this used to call directly can briefly flicker onto a stale
        // (group, index) for a few frames during a menu transition, which visibly
        // snapped the real, shipped icon onto a DIFFERENT already-calibrated item's
        // position for a moment before settling back.
        bool haveFocus = overlayOn &&
            TryGetStableFocusedGroupAndIndex(realGroup, sizeof(realGroup), manualDepth, manualSelIndex, siblingCount);
        float manualX = 0.0f, manualY = 0.0f;
        bool havePos = haveFocus &&
            TryGetManualGlyphPosition(realGroup, manualDepth, manualSelIndex, siblingCount, manualX, manualY);
        bool isVerified = havePos && IsVerifiedGlyphGroup(realGroup, manualDepth);
        char dkey[220];
        sprintf_s(dkey, "%d|%d|%d|%d|%s|%d|%d", overlayOn ? 1 : 0, haveFocus ? 1 : 0,
                  havePos ? 1 : 0, isVerified ? 1 : 0, realGroup, manualSelIndex, siblingCount);
        if (strcmp(dkey, s_lastDiagKey) != 0) {
            strncpy_s(s_lastDiagKey, dkey, _TRUNCATE);
            char dbuf[300];
            sprintf_s(dbuf, "[manual-glyph-diag] overlayOn=%d haveFocus=%d havePos=%d verified=%d "
                            "realGroup=\"%s\" realIndex=%d siblingCount=%d depth=%d x=%.1f y=%.1f",
                      overlayOn ? 1 : 0, haveFocus ? 1 : 0, havePos ? 1 : 0, isVerified ? 1 : 0,
                      realGroup, manualSelIndex, siblingCount, manualDepth, manualX, manualY);
            LogFromController(dbuf);
        }
        // v0.3.0 release standard (2026-08-03) was: only draw on groups explicitly
        // verified live (kVerifiedGlyphGroups), since havePos alone (a table entry
        // exists and matched) wasn't itself proof the entry was actually correct.
        // REMOVED 2026-08-16 (issue #51 follow-up, live click-and-drag calibration
        // pass): that gate now only gets in the way -- the whole point of the new
        // in-game editor (EditGlyphPositionsForFrame) is seeing the REAL draw
        // update live as each screen gets dragged into place, which requires it to
        // actually draw, not sit silently gated behind a separate allowlist that
        // has to be hand-maintained after the fact. isVerified is still computed
        // and logged above for now (useful diagnostic signal), just no longer
        // gates the draw itself.
        //
        // ALSO suppressed while the in-game glyph editor is actively toggled on
        // (2026-08-16, live-reported "the old one draws back and it duplicates to
        // two") -- EditGlyphPositionsForFrame below already draws a (possibly
        // dragged-away-from-here) icon for this exact same currently-focused item;
        // leaving this shipped draw active too meant the ORIGINAL, un-dragged
        // position kept redrawing every frame alongside the one actually being
        // dragged, once they were far enough apart to stop sharing a slot.
        if (havePos && !g_glyphEditModeActive && !IsGlyphDisabledGroup(realGroup)) {
            char aAsset[32] = {};
            if (TryGetMenuGlyphAssetNameForKeyName("ENTER", aAsset, sizeof(aAsset))) {
                RequestMenuHintOverlay(manualX, manualY, "", "", aAsset);
            } else {
                static bool s_loggedAssetFail = false;
                if (!s_loggedAssetFail) {
                    s_loggedAssetFail = true;
                    LogFromController("[manual-glyph-diag] TryGetMenuGlyphAssetNameForKeyName(\"ENTER\") FAILED");
                }
            }
        }
    }

    // In-game glyph position editor (2026-08-16, issue #51 follow-up) -- deliberately
    // OUTSIDE the automatic-vs-manual branch above and NOT gated on ShouldDrawGlyphOverlay
    // (that gate requires a controller to be the active input method; calibrating with a
    // mouse must work regardless of what input method the overlay itself currently
    // considers "active"). g_modConfig.glyphPositionEditMode is the master gate; F2 (only
    // polled while that's on) is a second, live, in-session toggle -- same key/two-step
    // convention as the harness diagram editor's own F2 toggle / F3 export.
    if (g_modConfig.glyphPositionEditMode) {
        static bool s_lastF2Held = false;
        bool f2Held = (GetAsyncKeyState(VK_F2) & 0x8000) != 0;
        bool f2Edge = f2Held && !s_lastF2Held;
        s_lastF2Held = f2Held;
        if (f2Edge) {
            g_glyphEditModeActive = !g_glyphEditModeActive;
            g_glyphEditDraggingGroup = -1;
            g_glyphEditDraggingIndex = -1;
            char msg[48];
            sprintf_s(msg, "[glyph-editor] edit mode %s", g_glyphEditModeActive ? "ON" : "off");
            LogFromController(msg);
        }
        // Debounced (2026-08-16, live-reported "it goes to the set position but after
        // x amount of time [it] moves") -- see TryGetStableFocusedGroupAndIndex's own
        // comment. Shares its single debounce state with the shipped manual-table
        // draw above, since only one screen/item can genuinely be focused at a time.
        char s_stableGroup[64] = {};
        int s_stableDepth = -1, s_stableIndex = -1, s_stableSiblingCount = -1;
        bool haveStableFocus = TryGetStableFocusedGroupAndIndex(s_stableGroup, sizeof(s_stableGroup),
                                                                   s_stableDepth, s_stableIndex, s_stableSiblingCount);

        // Always-visible status readout (2026-08-16 debugging follow-up: "click and
        // drag doesn't seem to do anything visually") -- fixed top-left corner, drawn
        // every frame this config flag is on regardless of F2/focus state, since F2
        // itself previously had zero on-screen confirmation (only a log line) --
        // impossible to tell at a glance whether the toggle registered or whether a
        // real item is even focused this frame without this. Shows the DEBOUNCED
        // (stable) target, matching what's actually being edited below, not the raw
        // per-frame read. Uses a fixed fraction-of-screen-independent design-space
        // corner (40,40), same convention every other on-screen text in this project
        // already uses.
        char statusText[220];
        if (!g_glyphEditModeActive) {
            sprintf_s(statusText, "[GLYPH EDITOR OFF] press F2 to activate ");
        } else if (haveStableFocus) {
            sprintf_s(statusText, "[GLYPH EDITOR ON] F3=export | focus=%s d%d i%d/%d ",
                       s_stableGroup, s_stableDepth, s_stableIndex, s_stableSiblingCount);
        } else {
            // Live-reported 2026-08-16 ("the dlc map select on survival etc" -- "it
            // doesnt detect the selected entry"): TryGetRealFocusedGroupAndIndex (the
            // direct itemDef-array read this whole editor/overlay depends on)
            // requires each real item's own name to end in "_<digits>" with two
            // specific flag bits set -- some screens genuinely don't populate that
            // shape at all (already a known architectural gap for keybind-editing
            // screens, see kManualGlyphPositions' own "Deliberately NOT covered"
            // comment). Rather than show nothing useful, fall back to the OLDER,
            // separate selection-tracking signals (g_currentSelGroupName/Index from
            // the compiled .menu script's own SetSelection calls, g_focusedItemName
            // from the real getfocuseditemname() hook) so there's SOMETHING to read
            // off screen/log for this specific screen instead of a dead end.
            sprintf_s(statusText, "[GLYPH EDITOR ON] F3=export | no real item focused | "
                                    "fallback: sel=%s/%d focused=\"%s\" d%d ",
                       g_currentSelGroupName, g_currentSelIndex, g_focusedItemName, GetMenuStackDepth());
            static char s_lastNoFocusDiagKey[220] = "";
            char noFocusKey[220];
            sprintf_s(noFocusKey, "%s|%d|%s|%d", g_currentSelGroupName, g_currentSelIndex,
                       g_focusedItemName, GetMenuStackDepth());
            if (strcmp(noFocusKey, s_lastNoFocusDiagKey) != 0) {
                strncpy_s(s_lastNoFocusDiagKey, noFocusKey, _TRUNCATE);
                char dbuf[256];
                sprintf_s(dbuf, "[glyph-editor-nofocus] sel=\"%s\"/%d focused=\"%s\" depth=%d",
                           g_currentSelGroupName, g_currentSelIndex, g_focusedItemName, GetMenuStackDepth());
                LogFromController(dbuf);
            }
        }
        RequestMenuHintOverlay(40.0f, 40.0f, statusText, "", "");

        if (g_glyphEditModeActive && haveStableFocus) {
            EditGlyphPositionsForFrame(s_stableGroup, s_stableDepth, s_stableIndex, s_stableSiblingCount);
        }
    }

    g_menuListItemOrdinalThisFrame = 0;

    // Auto-mantle's real ledge-availability gate (issue #62 follow-up): commit this
    // frame's accumulated mantle-hint-drawn state to a last-seen timestamp
    // (IsMantleHintCurrentlyShowing()'s own grace-window read), then reset the
    // accumulator for the next frame.
    if (g_mantleHintDrawnThisFrame) {
        g_mantleHintLastSeenMs = GetTickCount();
    }
    g_mantleHintDrawnThisFrame = false;
}

// ---- HUD-text visibility-test state (task #6/#34 follow-up, 2026-07-21) ------------
// Declared here (rather than down by the two Inject* functions that actually set/
// consume them, InjectFontGlyphPatchTest_HudBigFont and the new
// InjectFontGlyphVisibilityTest_HudBigFont, both defined later in this file) because
// Hook_DrawGlyphText needs to see them and is defined first -- same relative-ordering
// convention this file already uses for its other font-test globals (e.g.
// g_hudFontPatchStage is likewise declared well before the function that drives it).
//
// g_hudBigFontPtr / g_hudFontPatchInsertedCodepoint are set ONCE, by
// InjectFontGlyphPatchTest_HudBigFont (LB+RB+B), immediately after it successfully
// inserts a new glyph into the live fonts/hudBigFont array -- 0/nullptr sentinel means
// "no codepoint patched yet this session". g_hudFontVisibilityArmed is set by
// InjectFontGlyphVisibilityTest_HudBigFont (LB+RB+Y) and consumed right here, in
// Hook_DrawGlyphText, on the very next real call whose fontArg is confirmed (by
// pointer identity, not a re-parsed name) to be that exact, already-patched font.
void* g_hudBigFontPtr = nullptr;
unsigned short g_hudFontPatchInsertedCodepoint = 0;
bool g_hudFontVisibilityArmed = false;

void __cdecl Hook_DrawGlyphText(
    const char* param_1, float param_2, float param_3, void* fontArg, float param_5,
    float param_6, float param_7, float param_8, float param_9, int param_10,
    unsigned param_11, int param_12, unsigned param_13, float param_14, unsigned param_15,
    unsigned param_16, unsigned param_17, unsigned param_18, unsigned param_19,
    unsigned param_20, unsigned param_21)
{
    // Popup-body text capture (2026-08-16) -- see g_lastPopupBodyText's own comment.
    // Deliberately the very first thing this hook does: cheap (one float compare, one
    // strlen, maybe one strncpy_s), and every other gate/branch below it is
    // irrelevant to whether this specific text is worth remembering.
    if (fabsf(param_2 - 686.0f) < 3.0f && param_1 && strlen(param_1) > 10) {
        strncpy_s(g_lastPopupBodyText, param_1, _TRUNCATE);
    }

    // Diagnostic (2026-08-11, "major audit" pass -- see re_notes/known_issues.md issue
    // #74): confirms this hook actually fires at all, and captures the exact gate
    // values that decide whether a gameplay hint gets replaced. Added after a live
    // Nexus report (ViperManfred, v0.3.1.h1) tested with ForceGlyphOverlay=1 at 1440p
    // (16:9 -- the already-allowlisted font tier) and still saw zero glyphs, which
    // rules out both previously-fixed causes (XInput slot, font-tier allowlist gap)
    // for that report specifically and leaves "does this hook fire, and which gate
    // blocks it" as the single most valuable unknown. Deliberately NOT a guessed fix --
    // this project's own standard is to verify via logs before changing behavior.
    static LONG s_drawGlyphTextFireCount = 0;
    if (InterlockedIncrement(&s_drawGlyphTextFireCount) == 1) {
        LogFromController("[hud-font-id] Hook_DrawGlyphText fired for the first time -- confirmed alive");
    }
    {
        static char s_lastLoggedGateKey[160] = {};
        char fontNameForLog[64] = "<unreadable-or-null>";
        if (fontArg && LooksLikeValidPointer(reinterpret_cast<uintptr_t>(fontArg))) {
            const DiagFont* fontForLog = reinterpret_cast<const DiagFont*>(fontArg);
            if (LooksLikeValidPointer(reinterpret_cast<uintptr_t>(fontForLog->fontName))) {
                strncpy_s(fontNameForLog, fontForLog->fontName, _TRUNCATE);
            }
        }
        bool wouldDraw = ShouldDrawGlyphOverlay();
        bool menuActive = IsMenuActive();
        bool fontAllowed = (fontArg && LooksLikeValidPointer(reinterpret_cast<uintptr_t>(fontArg)))
            ? IsGameplayHintFont(reinterpret_cast<const DiagFont*>(fontArg)) : false;
        char gateKey[160];
        sprintf_s(gateKey, "%s|%d|%d|%d", fontNameForLog, wouldDraw ? 1 : 0, menuActive ? 1 : 0, fontAllowed ? 1 : 0);
        if (strncmp(s_lastLoggedGateKey, gateKey, sizeof(s_lastLoggedGateKey) - 1) != 0) {
            strncpy_s(s_lastLoggedGateKey, gateKey, _TRUNCATE);
            char gateBuf[256];
            sprintf_s(gateBuf, "[hud-font-id][gate] font=%s ShouldDrawGlyphOverlay=%d IsMenuActive=%d "
                                 "IsGameplayHintFont=%d forceGlyphOverlay=%d",
                fontNameForLog, wouldDraw ? 1 : 0, menuActive ? 1 : 0, fontAllowed ? 1 : 0,
                g_modConfig.forceGlyphOverlay ? 1 : 0);
            LogFromController(gateBuf);
        }
    }

    // TEMP diagnostic (2026-08-03, issue #51 follow-up): LEADERBOARDS_BUTTON_LIST
    // real-position/real-index calibration, per the confirmed manual-per-screen
    // method. Measures each item's real text-end position (SumDirectIndexedGlyphWidthsBefore)
    // AND the real itemDef-based focused index (TryGetRealFocusedGroupAndIndex) together
    // in one line, so the two screens reusing this group name (the map-select list where
    // "BONUS MAPS" is itself a real navigable index, and the separate 6-item Solo/Team
    // mode-category sub-screen) can each get their own correctly-indexed table entry.
    // Remove once both are fixed and confirmed live.
    {
        char diagRealGroup[64] = {};
        int diagRealIndex = -1, diagSiblingCount = -1;
        if (TryGetRealFocusedGroupAndIndex(diagRealGroup, sizeof(diagRealGroup), diagRealIndex, diagSiblingCount) &&
            _stricmp(diagRealGroup, "LEADERBOARDS_BUTTON_LIST") == 0 &&
            LooksLikeValidPointer(reinterpret_cast<uintptr_t>(param_1))) {
            __try {
                int rawWidth = SumDirectIndexedGlyphWidthsBefore(
                    reinterpret_cast<const DiagFont*>(fontArg), param_1, strlen(param_1));
                float textEndX = (rawWidth >= 0) ? (param_2 + static_cast<float>(rawWidth) * param_5) : -1.0f;
                char rawBuf[280];
                sprintf_s(rawBuf, "[leaderboards-verify-diag] text=\"%.60s\" p2=%.1f p3=%.1f p5=%.3f textEndX=%.1f depth=%d realIndex=%d siblingCount=%d",
                    param_1, param_2, param_3, param_5, textEndX, GetMenuStackDepth(), diagRealIndex, diagSiblingCount);
                LogFromController(rawBuf);
            } __except (EXCEPTION_EXECUTE_HANDLER) {
            }
        }
    }

    // ---- one-shot HUD-text visibility injection (task #6/#34 follow-up, 2026-07-21) -
    // See the big comment above InjectFontGlyphVisibilityTest_HudBigFont's own
    // definition for the full rationale. Consumes the arm flag exactly once, regardless
    // of outcome. Compares fontArg against g_hudBigFontPtr by POINTER IDENTITY ONLY (no
    // dereference needed for the compare itself, so this is always safe to check even
    // if fontArg is garbage) -- only proceeds into any actual read once that identity
    // match is confirmed, i.e. this is genuinely the same, already-patched hudBigFont
    // Font* object, not merely a similarly-named one.
    //
    // Safety: builds a LOCAL stack copy of the real text and appends the inserted
    // codepoint to THAT copy only -- the real buffer the game owns (param_1) is only
    // ever read from, never written to, so this cannot corrupt anything the game
    // itself still holds a pointer/iterator into. Wrapped in SEH the same way every
    // other read of a live engine string is in this file.
    if (g_hudFontVisibilityArmed && fontArg != nullptr && fontArg == g_hudBigFontPtr) {
        g_hudFontVisibilityArmed = false; // one-shot regardless of what happens below
        bool injected = false;
        __try {
            if (LooksLikeValidPointer(reinterpret_cast<uintptr_t>(param_1))) {
                char modified[512];
                size_t len = strnlen(param_1, sizeof(modified) - 2);
                memcpy(modified, param_1, len);
                modified[len] = static_cast<char>(static_cast<unsigned char>(g_hudFontPatchInsertedCodepoint));
                modified[len + 1] = '\0';
                char logBuf[400];
                sprintf_s(logBuf, "[hudbigfont-visibility-test] armed injection firing -- real hudBigFont draw call text was \"%.64s\" (len=%zu), appending codepoint 0x%02X and forwarding a modified COPY (real game buffer untouched)",
                    param_1, len, g_hudFontPatchInsertedCodepoint);
                LogFromController(logBuf);
                // param_10 is the real draw loop's explicit character count (NOT
                // null-termination -- confirmed via fresh Ghidra decompile of
                // FUN_00690c80, task #6/#34 root-cause pass, 2026-07-21). It's
                // captured once by the ring-buffer writer (FUN_0051b100, a plain
                // strlen() of the ORIGINAL unmutated string at HUD-text enqueue
                // time) and replayed unchanged by the reader (FUN_00691ca0) on
                // every subsequent draw -- so forwarding the original count here
                // makes the draw loop stop exactly one character short of the
                // codepoint we just appended to `modified`, every time, with no
                // crash and no visible glyph (confirmed live: not even the
                // lookup's own fallback glyph rendered). Forwarding param_10 + 1
                // matches the loop's gate to the actual appended-string length.
                g_origDrawGlyphText(modified, param_2, param_3, fontArg, param_5, param_6, param_7,
                    param_8, param_9, param_10 + 1, param_11, param_12, param_13, param_14, param_15,
                    param_16, param_17, param_18, param_19, param_20, param_21);
                injected = true;
                LogFromController("[hudbigfont-visibility-test] forwarded the modified copy to the real draw call with no exception -- check the screen now for a visible borrowed 'A' glyph appended to that HUD text.");
            } else {
                LogFromController("[hudbigfont-visibility-test] armed, but this call's param_1 didn't look like a valid pointer -- skipped, one-shot arming already consumed, not retrying this session");
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            LogFromController("[hudbigfont-visibility-test] exception while building/forwarding the modified copy -- aborted this attempt; falling through below to the normal, unmodified draw call for this frame instead of retrying");
        }
        if (injected) return; // already forwarded the modified copy above -- don't also forward/log the unmodified original below
    }

    if (g_modConfig.hudFontIdLogging && LooksLikeValidPointer(reinterpret_cast<uintptr_t>(fontArg))) {
        const DiagFont* font = reinterpret_cast<const DiagFont*>(fontArg);
        __try {
            if (LooksLikeValidPointer(reinterpret_cast<uintptr_t>(font->fontName)) &&
                strncmp(font->fontName, g_lastLoggedHudFontName, sizeof(g_lastLoggedHudFontName) - 1) != 0) {
                strncpy_s(g_lastLoggedHudFontName, font->fontName, sizeof(g_lastLoggedHudFontName) - 1);
                char logBuf[160];
                sprintf_s(logBuf, "[hud-font-id] real font in use for on-screen text changed: \"%.63s\" (Font*=0x%08X)",
                    font->fontName, static_cast<unsigned>(reinterpret_cast<uintptr_t>(fontArg)));
                LogFromController(logBuf);
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            // A faulted read just means this call's fontArg wasn't a real Font_s* after
            // all (e.g. param ordering differs at some call site not yet seen) -- never
            // let a read-only diagnostic crash the game over that; forward and move on.
        }
    }

    // Issue #48 (2026-07-31): position/scale/color investigation for the proposed
    // overlay-quad glyph-icon pivot -- dedup'd by DRAWN TEXT changing (not font
    // changing) so a real interact-hint's full raw parameter set gets logged exactly
    // once per distinct string, giving a direct correlation between a known hint
    // string and the params suspected (not yet confirmed) to be its x/y/scale/color.
    // Read-only: never mutates param_1 or anything else, same SEH-guarded pattern as
    // the hud-font-id block above so a bad param_1 pointer on some not-yet-seen call
    // site can't crash the game.
    if (g_modConfig.hudGlyphPositionLogging) {
        __try {
            if (LooksLikeValidPointer(reinterpret_cast<uintptr_t>(param_1)) &&
                !AlreadyLoggedGlyphPosText(param_1)) {
                MarkGlyphPosTextLogged(param_1);
                char logBuf[512];
                sprintf_s(logBuf,
                    "[hud-glyph-pos] text=\"%.63s\" p2=%.3f p3=%.3f p5=%.3f p6=%.3f p7=%.3f "
                    "p8=%.3f p9=%.3f p10=%d p11=%u p12=%d p13=%u p14=%.3f Font*=0x%08X",
                    param_1, param_2, param_3, param_5, param_6, param_7, param_8, param_9,
                    param_10, param_11, param_12, param_13, param_14,
                    static_cast<unsigned>(reinterpret_cast<uintptr_t>(fontArg)));
                LogFromController(logBuf);

                // Issue #48 follow-up, self-contained approach (2026-07-31 live-test
                // finding, see FindColorHighlightSpan's own comment): locate the
                // "^N...^7" highlighted button-name span directly in THIS string, then
                // compute its raw (unscaled) leading pixel-width sum from the same
                // font's own glyph metrics -- this is the actual data a future overlay
                // glyph icon needs to position itself over just that span rather than
                // the whole hint string. Logged in the SAME diagnostic pass as the raw
                // params above so one live test validates all of it at once.
                const DiagFont* font = LooksLikeValidPointer(reinterpret_cast<uintptr_t>(fontArg))
                    ? reinterpret_cast<const DiagFont*>(fontArg) : nullptr;
                size_t textLen = strlen(param_1);
                ColorHighlightSpan span = FindColorHighlightSpan(param_1, textLen);
                if (span.found) {
                    int rawOffset = SumDirectIndexedGlyphWidthsBefore(font, param_1, span.contentStart);
                    char highlighted[64] = {};
                    size_t copyLen = span.contentLen < sizeof(highlighted) - 1 ? span.contentLen : sizeof(highlighted) - 1;
                    memcpy(highlighted, param_1 + span.contentStart, copyLen);
                    highlighted[copyLen] = '\0';
                    char subBuf[256];
                    sprintf_s(subBuf,
                        "[hud-glyph-pos] color-highlight span \"%s\" at char index %zu (len %zu) in this "
                        "string -- raw (unscaled) leading pixel-width sum = %d (-1 means it bailed out, "
                        "e.g. extended-charset char before the span)",
                        highlighted, span.contentStart, span.contentLen, rawOffset);
                    LogFromController(subBuf);
                } else {
                    LogFromController("[hud-glyph-pos] no \"^N...^7\" color-highlight span found in this string");
                }

                // Secondary/independent cross-reference against the bind-resolver
                // hook's last-resolved key name -- kept as a sanity check the two
                // methods agree, not the primary signal anymore (see the big comment
                // above FindColorHighlightSpan for why the color-highlight approach is
                // now preferred).
                const char* resolvedKey = GetLastResolvedBindKeyName();
                if (resolvedKey && resolvedKey[0] != '\0') {
                    const char* match = strstr(param_1, resolvedKey);
                    char subBuf[256];
                    if (match) {
                        sprintf_s(subBuf, "[hud-glyph-pos] (cross-check) resolved key \"%s\" found at char index %zu",
                            resolvedKey, static_cast<size_t>(match - param_1));
                    } else {
                        sprintf_s(subBuf, "[hud-glyph-pos] (cross-check) resolved key \"%s\" NOT found as a substring here",
                            resolvedKey);
                    }
                    LogFromController(subBuf);
                }
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            // Same rationale as the hud-font-id block above -- never let this
            // read-only diagnostic crash the game over an unexpected param_1 shape.
        }
    }

    // Issue #48/#49 (2026-07-31 pivot): replace the WHOLE in-game hint with this
    // project's own text+icon render, gated by ShouldDrawGlyphOverlay(). Superseded
    // the earlier "overlay an icon on top of the game's own text" plan -- lining up a
    // real icon against the game's own pixel-exact font rendering proved fiddly
    // (confirmed live: two rounds of position math still landed visibly off) and
    // multiple RESTRICTED to the two real in-game HUD hint fonts (see
    // IsGameplayHintFont) -- explicitly NOT applied to menu UI hints like
    // "Friends ^2F^7" (fonts/smallFont), which the user confirmed should keep
    // rendering natively (console used a different button there anyway -- Y, not
    // X -- so this project's gameplay keybind table would have mapped it wrong even
    // if it did apply). When this successfully finds a mapped icon, it SUPPRESSES the
    // real draw call entirely (sets suppressRealDraw) rather than drawing an overlay
    // on top of it, since there's no need to align against text that no longer draws.
    // Live-reported 2026-07-31: the replacement hint kept showing while the game was
    // paused, when it shouldn't. Root cause: this HUD text's own draw call chain is
    // tied to the RENDER frame, not the simulation/usercmd tick this project's other
    // injected input halts on during pause -- it (and, by extension, our replacement)
    // keeps getting called every rendered frame even while paused. The real native
    // hint is presumably hidden behind the pause menu's own dimming overlay in that
    // state; our own EndScene-based draw happens AFTER that overlay, so it would show
    // ON TOP of the dimming instead of being hidden by it. Gated behind the same real
    // menu-active gate bit (IsMenuActive(), 0x10 @ 0xB36210) several other features in
    // this file already use for the identical reason.
    bool suppressRealDraw = false;
    if (ShouldDrawGlyphOverlay() && !IsMenuActive()) {
        __try {
            if (LooksLikeValidPointer(reinterpret_cast<uintptr_t>(param_1)) &&
                LooksLikeValidPointer(reinterpret_cast<uintptr_t>(fontArg))) {
                const DiagFont* font = reinterpret_cast<const DiagFont*>(fontArg);
                if (IsGameplayHintFont(font)) {
                    size_t textLen = strlen(param_1);
                    ColorHighlightSpan span = FindColorHighlightSpan(param_1, textLen);
                    if (span.found) {
                        char highlighted[64] = {};
                        size_t copyLen = span.contentLen < sizeof(highlighted) - 1 ? span.contentLen : sizeof(highlighted) - 1;
                        memcpy(highlighted, param_1 + span.contentStart, copyLen);
                        highlighted[copyLen] = '\0';
                        // Bug found 2026-07-31: TryGetGlyphAssetNameForKeyName's own lookup
                        // trims internally, but nothing else using `highlighted` did -- trim
                        // in place here too so every later use sees the same clean form.
                        {
                            size_t start = 0, end = strlen(highlighted);
                            while (start < end && isspace(static_cast<unsigned char>(highlighted[start]))) ++start;
                            while (end > start && isspace(static_cast<unsigned char>(highlighted[end - 1]))) --end;
                            size_t trimmedLen = end - start;
                            if (start > 0) memmove(highlighted, highlighted + start, trimmedLen);
                            highlighted[trimmedLen] = '\0';
                        }

                        // Issue #68 follow-up (2026-08-05 live retest): used to identify the
                        // mantle hint by comparing `highlighted` against the literal English
                        // word "SPACE" -- broken under any language that translates it (live-
                        // confirmed: Italian renders "Premi Spazio per," and "Spazio" matches
                        // nothing in kKeyActionTable, so the icon lookup silently failed and
                        // the real untranslated text fell through instead). Detected
                        // structurally now instead, via RenderedTextMatchesSubstitutionTemplate
                        // against the real PLATFORM_MANTLE template -- correct regardless of
                        // what the substituted key name says in any language -- and resolves
                        // the icon straight from the known LogicalAction::Jump mapping
                        // (TryGetMantleGlyphAssetName) rather than the translated-text lookup.
                        bool isMantleHint = RenderedTextMatchesSubstitutionTemplate(param_1, "PLATFORM_MANTLE");
                        // Auto-mantle gate (issue #62), moved here 2026-08-16: this used to be
                        // set further down, INSIDE the `if (haveAssetName)` block below -- which
                        // meant g_mantleHintDrawnThisFrame only ever became true if
                        // TryGetMantleGlyphAssetName() ALSO succeeded (icon asset loaded/
                        // resolved). Auto-mantle's whole detection was therefore silently tied
                        // to "did THIS PROJECT's own icon resolve," not "is the real native
                        // mantle hint actually showing" -- the real signal (isMantleHint, the
                        // structural PLATFORM_MANTLE template match above) could be perfectly
                        // true while auto-mantle still never fired, if the icon lookup failed
                        // for any unrelated reason (a real, plausible explanation for issue #62's
                        // "doesn't fire even at a mantle point" report, never diagnosed before
                        // now). Set unconditionally on isMantleHint alone -- the actual visible
                        // icon draw further down still separately depends on haveAssetName and is
                        // unaffected by this change either way.
                        if (isMantleHint) g_mantleHintDrawnThisFrame = true;
                        // Same class of bug, same day, user-reported ("just one issue i saw
                        // was the nades in other languages"): ResolveGlyphAssetNameForKeyName's
                        // own `_stricmp(keyName, "G or Middle Mouse") == 0` special case (the
                        // grenade-throwback hint, PLATFORM_THROWBACKGRENADE = "^3&&1 ^7throw
                        // back") compares the SUBSTITUTED combo-bind text, which contains the
                        // English word "or" -- exactly the same translation risk "SPACE" had,
                        // never actually confirmed broken live but fixed proactively using the
                        // identical proven technique rather than waiting for another screenshot.
                        bool isThrowbackHint = RenderedTextMatchesSubstitutionTemplate(param_1, "PLATFORM_THROWBACKGRENADE");
                        // Same audit, same class of bug, different marker style: SENTRY_PLACE
                        // ("Press ^3[{+attack}]^7 to place the turret.") uses the `[{+command}]`
                        // bracket-token substitution instead of "&&1", but resolves through the
                        // same real bind-resolver either way (re_notes/ui_assets.md), and its
                        // own real captured English text ("Left Mouse") contains the same kind
                        // of translatable word "Mouse" that ResolveGlyphAssetNameForKeyName's
                        // `_stricmp(keyName, "Left Mouse")` special case still matches literally.
                        bool isSentryPlaceHint = RenderedTextMatchesSubstitutionTemplateWithMarker(param_1, "SENTRY_PLACE", "[{+attack}]");

                        char assetName[32] = {};
                        bool haveAssetName = isMantleHint ? TryGetMantleGlyphAssetName(assetName, sizeof(assetName))
                            : isThrowbackHint ? TryGetThrowbackGlyphAssetName(assetName, sizeof(assetName))
                            : isSentryPlaceHint ? TryGetSentryPlaceGlyphAssetName(assetName, sizeof(assetName))
                            : TryGetGlyphAssetNameForKeyName(highlighted, assetName, sizeof(assetName));
                        if (haveAssetName) {
                            char prefixText[128] = {};
                            size_t prefixLen = span.markerStart < sizeof(prefixText) - 1 ? span.markerStart : sizeof(prefixText) - 1;
                            memcpy(prefixText, param_1, prefixLen);
                            prefixText[prefixLen] = '\0';

                            // BUG-004 follow-up (2026-08-02): live-captured proof that Survival's
                            // ready-up hint can be a genuinely TWO-LINE native string joined by an
                            // embedded '\n' when a teammate has already readied up --
                            // "Teammate ready\nPress ^3F5^7 to ready up: 23" is ONE draw call, ONE
                            // color-highlight span. Split it here (generic, not ready-up-specific)
                            // so the first line renders as its own row instead of either garbling
                            // the combined text or (after the "Hold Y" override below) silently
                            // losing "Teammate ready" entirely.
                            char topLineText[128] = {};
                            char* embeddedNewline = strchr(prefixText, '\n');
                            if (embeddedNewline) {
                                *embeddedNewline = '\0';
                                strncpy_s(topLineText, prefixText, _TRUNCATE);
                                memmove(prefixText, embeddedNewline + 1, strlen(embeddedNewline + 1) + 1);
                            }

                            char suffixText[128] = {};
                            if (span.markerEnd < textLen) {
                                size_t suffixLen = textLen - span.markerEnd;
                                if (suffixLen >= sizeof(suffixText)) suffixLen = sizeof(suffixText) - 1;
                                memcpy(suffixText, param_1 + span.markerEnd, suffixLen);
                                suffixText[suffixLen] = '\0';
                            }

                            // Round 6: measured directly against real pixels (PowerShell
                            // System.Drawing scan of a live screenshot, not another guess) --
                            // " Model 1887" (a real, STATIC 2D HUD element, confirmed by the
                            // user; the earlier "world-space floating label" theory was wrong)
                            // has its real text ink centered at screen y ~= 698 while sharing
                            // this hint's own p3 (718). X needed NO correction (measured
                            // text-left-edge matched raw param_2 within a few px of normal font
                            // left-bearing). This is the CENTER of the line, not its top -- see
                            // DrawCustomHintIfRequested's own use of this value.
                            //
                            // 2026-08-08 fix (issue #70, round 7): the original Y formula here
                            // multiplied param_3 by param_6 (718 * 0.964 = 692.2, a ~1% match
                            // against the measured target -- "Y needs the same scale factor
                            // param_5/param_6 already apply to glyph advances"). Live-reported
                            // "still far too vertically high on low res" even after the round-6
                            // proportional-nudge fix -- checked directly against a fresh
                            // proxy_d3d9.log rather than guessing again, comparing the SAME hint
                            // text at two resolutions:
                            //   pickup "Press F to pick up": p3=718 at 1920x1080 (718/1080=0.665
                            //     of real screen height) vs p3=319 at 640x480 (319/480=0.665) --
                            //     matches almost exactly.
                            //   mantle "Press Space to  ": p3=844 at 1920x1080 (844/1080=0.782)
                            //     vs p3=375 at 640x480 (375/480=0.781) -- matches almost exactly.
                            // Raw param_3 alone is already consistently proportional to the real
                            // screen height across resolutions AND font tiers (this project's own
                            // established real-position convention everywhere else in this file).
                            // param_6, however, varies by FONT TIER independent of that
                            // proportion (0.964 for fonts/extraBigFont at 16:9 vs 0.545 for
                            // fonts/normalFont at 640x480, per issue #73's own font findings) --
                            // multiplying by it was only ever a ~1%-close coincidence at the ONE
                            // resolution/font it was calibrated against, and badly distorts the
                            // row at any other font tier (692/1080=0.641 vs (718*0.545... using
                            // the WRONG param_6 for that font)/480 dropping to ~0.43 -- nearly
                            // half as far down the screen as intended, exactly the reported
                            // "too vertically high"). Dropped the `* param_6` multiply entirely;
                            // param_3 alone is the real pixel Y, consistent with how every other
                            // real-position value in this project is already treated.
                            // Live-reported 2026-07-31: weapon pickup needs to sit very
                            // slightly lower than the pure measured-center formula above gives
                            // -- small empirical nudge, not a new transform.
                            // ROOT CAUSE CONFIRMED 2026-08-16 via two real before/after
                            // screenshots (2026-07-31 "correct" vs. 2026-08-16 "current", same
                            // Model 1887 pickup prompt): issue #70 round 7 (2026-08-08) dropped
                            // this whole general path's `* param_6` multiply, moving from
                            // `param_3 * param_6` to raw `param_3` -- proven MORE consistent
                            // proportionally across resolutions, but at 16:9 specifically this
                            // was a real, uncompensated increase (718 * 0.964 = 692.2 -> 718, a
                            // ~26-unit drop down the screen) that round 7 itself never got
                            // live-confirmed against ("Not yet live-tested," per that round's own
                            // log entry) -- it silently regressed every hint on this shared path
                            // at the single most common resolution this project is actually
                            // played at. First attempt at fixing this (a blind -5 guess) was
                            // rejected -- this value is instead now anchored to the real,
                            // measured before/after gap the user directly reported (~10px too
                            // low): reduced from 6 to -4 (a 10-unit upward shift). Mantle is
                            // fully excluded from this constant now (see the line below), so
                            // this can never again entangle with mantle's own already-tuned
                            // position the way the earlier attempt did.
                            // ROUND 2, live-retested same day: "better but not perfect... same
                            // issue too low down, just budge them by another 10px" -- another
                            // 10-unit upward shift, -4 -> -14.
                            // ROUND 3, same day: "maybe bump just 4px more and its better" --
                            // -14 -> -18.
                            constexpr float kHintVerticalNudge = -18.0f;
                            // isMantleHint computed above (structural template match, issue #68).
                            // g_mantleHintDrawnThisFrame (auto-mantle's real ledge-availability
                            // gate, issue #62) is now set unconditionally on isMantleHint alone,
                            // right where it's computed above -- see that assignment's own
                            // comment for why this moved out of this asset-resolution-gated block.
                            // Survival's ready-up hint (F5) sits at a genuinely different native
                            // row (p3=329 vs. pickup/buy-station's shared 718) -- user wants its
                            // position kept "similar to original" rather than pulled into the
                            // interact-hint row's own tuning, so it skips both the vertical nudge
                            // (empirically tuned for THAT row specifically) and screen-centering.
                            bool isReadyUpHint = _stricmp(highlighted, "F5") == 0;
                            // BUG-004 (stream co-op report, 2026-08-02): the real native string is
                            // "Press F5 to ready up" -- correct for a tap, but ready-up is actually a
                            // HOLD (see InjectControllerWeaponNext's own g_modConfig.readyUpHoldThresholdMs).
                            // This project already fully replaces the draw for this hint (suppressRealDraw
                            // below), so the wrong verb is entirely on this project, not the game --
                            // override it here rather than passing the native "Press " through.
                            if (isReadyUpHint) {
                                strcpy_s(prefixText, sizeof(prefixText), "Hold ");
                            }
                            float startX = param_2;
                            float startY = param_3;

                            // Live-reported 2026-08-08 (640x480/800x600): "gameplay hints other
                            // than reload are broken." Same root cause as the menu corner hints
                            // (see ConvertRealScreenPosToDesignSpace's own header comment) --
                            // startX/startY above are built from param_2/param_3, REAL
                            // current-resolution screen pixels, but RequestCustomHintOverlay's
                            // consumer (DrawOneGameplayHintSlot) multiplies whatever x/y it
                            // receives by scaleX/scaleY exactly once, expecting DESIGN-SPACE
                            // input -- invisible at 16:9 (scaleX/scaleY~=1.0), badly wrong
                            // otherwise. Reload (the one hint NOT affected, per the same live
                            // report) never goes through this code path at all -- it uses its own
                            // fixed kInteractHintRowY constant further below, not a live param_3
                            // read.
                            ConvertRealScreenPosToDesignSpace(startX, startY, startX, startY);

                            // 2026-08-08 fix (issue #70 family, live-reported "position drifts on
                            // pickup/throwback/mantle prompts" at low resolutions, right after
                            // fixing #73's font gap made these hints visible at low res for the
                            // first time): these two empirical nudges used to be added to
                            // startX/startY BEFORE the conversion above, i.e. as a FIXED number of
                            // REAL pixels at ANY resolution (the conversion's divide-then-multiply
                            // cancels exactly, per that function's own header comment). A fixed
                            // real-pixel offset is a shrinking fraction of a 1920px-wide screen but
                            // a GROWING fraction of a 640px-wide one -- both nudges exist to align
                            // against another real, proportionally-scaled screen element (the
                            // mantle arrow sprite; the interact row's own measured text baseline),
                            // so a nudge that doesn't shrink with the screen overshoots more and
                            // more as resolution drops -- exactly "drift." Moved to AFTER the
                            // conversion, in DESIGN-SPACE units, so `DrawOneGameplayHintSlot`'s
                            // final multiply scales the nudge by the SAME factor as everything
                            // else -- unchanged at 1920x1080 (scale=1.0, where both constants were
                            // originally eyeballed) and proportionally smaller at any lower
                            // resolution, matching the target elements they're aligned against.
                            // Live direction 2026-08-16: mantle used to ALSO pass through this
                            // shared nudge, THEN get its own kMantleHintYNudge added on top --
                            // meaning any future tuning of this shared value (for pickup/
                            // throwback/etc.) would silently drag mantle's position along with
                            // it too, requiring a manual compensating edit every time (exactly
                            // what happened a few edits ago). Mantle now fully excluded here --
                            // its own nudge below is a single, standalone, independent value,
                            // not additive on top of this one.
                            if (!isReadyUpHint && !isMantleHint) startY += kHintVerticalNudge;
                            if (isMantleHint) {
                                // Live-reported 2026-07-31: the mantle/jump prompt sits far to the
                                // left of the real, separately-drawn mantle arrow sprite (~107px
                                // measured against a live screenshot at 1920x1080) -- that sprite
                                // isn't text at all (a distinct native icon draw this project
                                // doesn't hook), so there's no live position to read for it;
                                // nudged empirically instead, scoped to the Jump/mantle hint only.
                                constexpr float kMantleHintXNudge = 82.0f;
                                startX += kMantleHintXNudge;
                                // Live-reported 2026-08-16 (first real screenshot of this hint,
                                // now that auto-mantle round 3 made it trivially reproducible --
                                // see this project's own issue #62 for why this hint was rarely
                                // actually seen up close before): "our text for the mantle is no
                                // longer centered vertically properly on the sprite" -- a red/
                                // green annotated screenshot showed the current row (this
                                // project's own text+A-icon) sitting noticeably BELOW the real
                                // mantle arrow sprite's own vertical center. Measured against that
                                // screenshot using the A-icon's own known real diameter
                                // (kHintIconSize, overlay_hud.cpp) as an in-image ruler (scale-
                                // invariant regardless of the screenshot's own crop/zoom) -- the
                                // gap was consistently close to one full icon-height. Nudged up by
                                // approximately that amount; a first-pass screenshot-based
                                // estimate like every other nudge in this block, may need a small
                                // follow-up correction once re-confirmed live.
                                // ROUND 2, live-retested same day: "now its too high it needs to go
                                // down like 5-10 px" -- reduced -44 -> -36 (~8 units back down, mid
                                // of the reported 5-10px range). Still a design-space unit, so this
                                // stays proportionally correct at any resolution the same way the
                                // original estimate was (see the header comment on this whole nudge
                                // block for why these are applied post-conversion specifically so
                                // they scale, not a hardcoded real-pixel offset).
                                // ROUND 3, live direction 2026-08-16: this used to be ADDITIVE on
                                // top of the shared kHintVerticalNudge above (net effect at the
                                // time: +6 shared, -36 here, -30 total) -- meaning any future
                                // tuning of that shared constant for OTHER hints (pickup/
                                // throwback/etc.) would silently move mantle too, forcing a manual
                                // compensating edit here every single time, exactly what happened a
                                // few edits ago. Mantle is now fully excluded from the shared nudge
                                // (see that line's own comment) and this is the ONLY value driving
                                // its vertical position -- folded to the same already-confirmed-
                                // correct net total (-30) so the actual on-screen position is
                                // completely unchanged by this refactor, just no longer entangled
                                // with a value meant for unrelated hints.
                                constexpr float kMantleHintYNudge = -30.0f;
                                startY += kMantleHintYNudge;
                            }

                            bool centerOnScreen = !isMantleHint && !isReadyUpHint;
                            // BUG-004 follow-up: ready-up gets its own named slot (GameplayHintSlotId::
                            // ReadyUp) instead of sharing the generic Interact slot every other hint here
                            // uses -- see RequestCustomHintOverlay's own comment for why they used to fight
                            // over one slot and silently evict each other.
                            GameplayHintSlotId slotId = isReadyUpHint ? GameplayHintSlotId::ReadyUp : GameplayHintSlotId::Interact;
                            RequestCustomHintOverlay(startX, startY, prefixText, suffixText, assetName, centerOnScreen,
                                                       /*flashIcon=*/false, slotId, topLineText);
                            suppressRealDraw = true;

                            // See the big comment above g_awaitingHintContinuationFont --
                            // arms the next matching call this frame to be treated as this
                            // hint's own live weapon-name continuation, if one exists.
                            g_awaitingHintContinuationFont = fontArg;
                            g_awaitingHintContinuationP3 = param_3;
                            g_awaitingHintContinuationSlot = slotId;
                            g_awaitingHintContinuation = true;
                        }
                    } else {
                        // Live-reported 2026-07-31: the real reload reminder has no
                        // "^N...^7" button-name span at all -- it's just a bare flashed/
                        // pulsed word, a completely different native UI pattern from every
                        // other hint here. User wants it read as "Press X To <word>"
                        // (console's own phrasing) -- "Press " and " To " are this
                        // project's own added template text (the real string has
                        // neither), the actual word is always read live from param_1,
                        // never hardcoded.
                        char trimmedText[32] = {};
                        size_t tlen = textLen < sizeof(trimmedText) - 1 ? textLen : sizeof(trimmedText) - 1;
                        memcpy(trimmedText, param_1, tlen);
                        trimmedText[tlen] = '\0';
                        size_t tStart = 0, tEnd = strlen(trimmedText);
                        while (tStart < tEnd && isspace(static_cast<unsigned char>(trimmedText[tStart]))) ++tStart;
                        while (tEnd > tStart && isspace(static_cast<unsigned char>(trimmedText[tEnd - 1]))) --tEnd;
                        if (tEnd - tStart > 0) memmove(trimmedText, trimmedText + tStart, tEnd - tStart);
                        trimmedText[tEnd - tStart] = '\0';

                        // Live-reported (issue #68 language pass, 2026-08-05): this used
                        // to require the literal English word "Reload" here -- guaranteed
                        // to never match once the game's own UI language translates that
                        // word (e.g. Italian "Ricarica"), silently dropping the icon for
                        // every non-English player on one of the most common gameplay
                        // hints there is. Fixed via a live comparison against the CURRENT
                        // resolved text of either real candidate reference key this exact
                        // word could be sourced from -- confirmed via zone_dump
                        // (code_post_gfx.str has TWO distinct real keys that both resolve
                        // to English "Reload": `MENU_RELOAD_WEAPON` and `PLATFORM_RELOAD` --
                        // checking both since it's not confirmed which one this specific
                        // HUD hint actually uses).
                        //
                        // ORIGINALLY also OR'd against this prompt's own real p3 (626,
                        // documented as distinct from pickup/buy-station's shared 718) as
                        // a "belt-and-braces" fallback -- REMOVED same day, live-reported
                        // regression: a Survival end-of-wave bonus-collect prompt
                        // ("Press X To +$66") shares a similar row under at least one
                        // other language's layout and got hijacked into rendering through
                        // this Reload template instead (real screenshots, see
                        // known_issues.md issue #68). The text check alone already
                        // correctly rejected both false-positive strings ("+$66"/"0%")
                        // and is proven sufficient for the real Reload hint too (this same
                        // retest's own "fixed" confirmation) -- the position fallback was
                        // only ever a hedge against the text check being wrong, and this
                        // incident is direct proof the hedge is actively unsafe, not
                        // merely redundant.
                        bool looksLikeReloadHint =
                            RenderedTextMatchesReferenceKey(trimmedText, "MENU_RELOAD_WEAPON") ||
                            RenderedTextMatchesReferenceKey(trimmedText, "PLATFORM_RELOAD");
                        // BUG-004 follow-up (2026-08-02): used to gate on !WasInteractHintRecentlyActive()
                        // here (a 100ms wall-clock window, since Reload and an interact hint could be
                        // processed in either order within a frame) -- replaced by giving Reload its own
                        // named slot and deciding the actual suppression at draw time
                        // (DrawGameplayHintSlotsIfRequested in overlay_hud.cpp), which can check same-frame
                        // state exactly instead of racing a timer. Always requests the Reload slot here;
                        // whether it actually draws is decided once, at the end of the frame.
                        if (looksLikeReloadHint) {
                            char assetName[32] = {};
                            if (TryGetGlyphAssetNameForKeyName("F", assetName, sizeof(assetName))) {
                                // "F" is the real default keyboard bind for ReloadUse
                                // (kKeyActionTable) -- reused here directly since this
                                // prompt has no key reference of its own to resolve from.
                                char suffixText[48] = {};
                                sprintf_s(suffixText, " To %s", trimmedText);
                                float startX = param_2;
                                // Live-reported 2026-07-31: this prompt's own real p3
                                // (626, vs. pickup/buy-station's shared 718) is a
                                // genuinely different, higher native UI slot -- not a
                                // transform bug -- and rendered noticeably above the
                                // weapon (screen vertical-middle) instead of in the
                                // same row as the other interact hints. User wants it
                                // in that same row, so anchored directly to the same
                                // known-good target pickup/buy-station's own formula
                                // resolves to, rather than deriving a position from
                                // this prompt's unrelated p3.
                                //
                                // CORRECTED 2026-08-16 (live-reported "every hint is
                                // slightly shifted maybe this was from our 4:3 fix
                                // earlier" -- correct diagnosis, confirmed via real
                                // before/after screenshots of this exact pickup prompt).
                                // The original 698 value baked in the OLD pickup/
                                // buy-station formula (718 * param_6(0.964) +
                                // kHintVerticalNudge(6) ~= 698) -- issue #70 round 7
                                // (2026-08-08) later dropped the `* param_6` multiply
                                // entirely for the general path, which (never live-
                                // confirmed at the time) turned out to be a real ~26-unit
                                // regression at 16:9. kHintVerticalNudge itself was then
                                // corrected to -4.0f, then -14.0f after a same-day round 2
                                // (see that constant's own comment). Kept in sync here:
                                // 718 + kHintVerticalNudge(-14) = 704.
                                constexpr float kInteractHintRowY = 700.0f; // 718 + kHintVerticalNudge(-18)
                                float startY = kInteractHintRowY;
                                RequestCustomHintOverlay(startX, startY, "Press ", suffixText, assetName,
                                    /*centerOnScreen=*/true, /*flashIcon=*/true, GameplayHintSlotId::Reload);
                                suppressRealDraw = true;
                            }
                        }
                    }
                }
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            // Same rationale as every other read-only-turned-behavior block in this
            // file -- never let this crash the game over an unexpected param_1/fontArg
            // shape on some not-yet-seen call site.
        }
    }

    // Menu UI hints (2026-08-01, "next step is menus" -- the user's own framing:
    // "highlighted entries must get the appropriate glyph next to them... some dont
    // need to be highlighted for glyphs like esc equivalents which shows B glyph").
    // This block covers the SECOND half of that: static, always-shown menu hints
    // like "Back ^2ESC^7"/"Friends ^2F^7" that don't depend on which item is
    // currently highlighted -- they use the exact same `^N...^7` color-highlight
    // convention and the exact same Hook_DrawGlyphText call site as gameplay hints,
    // just on fonts/smallFont (previously excluded entirely, see IsMenuHintFont's
    // own comment) and resolved through the SEPARATE menu-specific table
    // (ResolveMenuGlyphAssetNameForKeyName) instead of the gameplay one, since the
    // same key text means a different physical button in this context (ESCAPE is
    // Start in gameplay's own table, but B is the real button that forwards ESC to
    // an open menu; F is Y here, not gameplay's X). Deliberately NOT gated on
    // !IsMenuActive() (unlike the gameplay block above) -- these hints only ever
    // draw WHILE a menu is active, so suppressing them in that state would suppress
    // them entirely. Deliberately does NOT reuse any of the gameplay block's
    // position nudges/centering (kHintVerticalNudge, mantle/ready-up special
    // cases) -- none of that has been live-verified for menu hints yet, so this
    // starts as a plain, unmodified reading of the real position, same convention
    // (param_3 * param_6 as vertical CENTER) as everything else in this file.
    // The FIRST half of the user's request -- an A/select glyph next to whichever
    // menu ITEM is currently highlighted (e.g. the CAMPAIGN/MULTIPLAYER tiles) --
    // is a materially different, harder problem (no `^N...^7` span exists on plain
    // itemDef text at all) and needs its own investigation before it can be
    // implemented; not part of this block. See known_issues.md issue #48's newest
    // round for the plan.
    if (!suppressRealDraw && ShouldDrawGlyphOverlay()) {
        __try {
            if (LooksLikeValidPointer(reinterpret_cast<uintptr_t>(param_1)) &&
                LooksLikeValidPointer(reinterpret_cast<uintptr_t>(fontArg))) {
                const DiagFont* font = reinterpret_cast<const DiagFont*>(fontArg);
                if (IsMenuHintFont(font)) {
                    // Leaderboards special case (2026-08-01, quick completeness fix,
                    // per explicit user instruction: "leaderboard should run off the
                    // function key and be bound to back on controller"). Live-captured
                    // real string: "Leaderboards ^2Right Mouse^7/^2F1^7" -- TWO
                    // separate ^N...^7 spans in one string ("Right Mouse" and "F1"),
                    // unlike every other menu hint (exactly one span each).
                    // FindColorHighlightSpan below only ever finds the FIRST span
                    // ("Right Mouse", which doesn't map to any single controller
                    // button anyway), so this needs its own literal-prefix detection
                    // instead -- same precedent as the gameplay Reload hint's bare-
                    // literal-text match. Keys off the function-key half of the real
                    // combo bind ("F1") and maps it to B -- there's no dedicated
                    // separate leaderboard button on console; B is a pragmatic choice
                    // for a quick completeness fix, not a claim console binds it there.
                    constexpr float kMenuHintVerticalNudge = -18.0f; // matches the row's own empirical nudge
                    // Main-title "Quit" (2026-08-01, user-requested visual completeness
                    // fix). Confirmed via `.menu` file inspection (main_selection.menu)
                    // its real action is `open quit_popmenu;` -- a real confirmation
                    // dialog, not an instant exit. Per explicit user direction, input is
                    // already correctly handled: B's existing ESC-forward call
                    // (FUN_004d9850) already reaches this same action natively on this
                    // screen -- this is visual-only, no new input synthesis. "Quit" itself
                    // is a plain itemDef with no "^N...^7" span at all (confirmed live and
                    // via the `.menu` file, `exp text "@MENU_QUIT"` with no bind-resolved
                    // key text appended), so it needs its own literal-text match, same
                    // precedent as the Leaderboards case right below. Case-SENSITIVE and
                    // exact-match deliberately: the Special Ops hub list has its own,
                    // separate "QUIT" item (all-caps, confirmed live via list-item-diag
                    // captures) that leads to a different confirmation flow ("Are you sure
                    // you want to quit?") not confirmed to be reachable via B's existing
                    // ESC-forward the same way -- this must not also match that one.
                    // BUG-006 (stream report, 2026-08-02): both the "Quit" and "Leaderboards"
                    // literal-text matches below used to fire on ANY on-screen text with that
                    // exact/prefix content, with no positional check at all -- live-reported to
                    // occasionally hijack a genuine, unrelated NAVIGABLE MENU LIST ITEM that just
                    // happens to share the same label (e.g. a real "Leaderboards" button that
                    // navigates TO that screen, not the screen's own corner-hint legend), mangling
                    // that item's real text into a corner-hint-style render instead. Both Quit and
                    // Leaderboards' own legend-bar text are already confirmed (via live capture,
                    // see the comments below) to sit at the SAME real corner-hint row as Back/
                    // Friends (p3 ~= 995, kStandardCornerHintY -- see that constant's own
                    // declaration comment for why this is a real, not design-space, number
                    // despite the "design-space" framing used elsewhere in this issue) --
                    // a genuine navigable list item
                    // with the same text sits at a completely different row (wherever that list is
                    // laid out), so a tolerance check against that known row is a real, precedented
                    // discriminator (same technique RequestMenuHintOverlay already uses to collapse
                    // same-position hints) rather than relying on text content alone, per the
                    // report's own suggested fix.
                    // Live-reported 2026-08-08: "main menu quit button doesn't scale
                    // correctly" -- same real-screen-space-vs-design-space bug as
                    // everywhere else in this issue (see ConvertRealScreenPosToDesignSpace's
                    // own header comment). `param_3` is a REAL, current-resolution screen
                    // pixel; `kStandardCornerHintY` (995) is usable directly as the
                    // design-space reference here because it was captured at a REAL device
                    // viewport of exactly 1920x1080 (confirmed via proxy_d3d9.log), which
                    // IS this project's design-space reference resolution -- real and
                    // design-space are numerically identical at that one resolution. (A
                    // round-4 attempt to "generalize" this by dividing it against an
                    // assumed 2560x1440 capture viewport was wrong and reverted -- see
                    // kStandardCornerHintY's own declaration comment.) Converts param_3 to
                    // its design-space equivalent FIRST so both sides of the comparison are
                    // in the same units, at any resolution -- correctly, now that
                    // ConvertRealScreenPosToDesignSpace uses the same real device the
                    // eventual draw call scales by (round 4 fix, live-reported "you broke
                    // 16:9" against the round-3 version of this conversion, which used a
                    // mismatched no-device GetClientRect fallback instead).
                    constexpr float kCornerHintRowTolerancePx = 40.0f;
                    float unusedDesignX = 0.0f, designParam3 = 0.0f;
                    ConvertRealScreenPosToDesignSpace(0.0f, param_3, unusedDesignX, designParam3);
                    bool looksLikeCornerHintRow = fabsf(designParam3 - kStandardCornerHintY) < kCornerHintRowTolerancePx;
                    // Issue #68 (2026-08-05 language pass): replaced the hardcoded
                    // `strcmp(param_1, "Quit")` with a live comparison against the real
                    // MENU_QUIT reference key's CURRENT resolved text -- confirmed real
                    // key via zone_dump (code_post_gfx.str: `REFERENCE MENU_QUIT` /
                    // `LANG_ENGLISH "Quit"`, an exact match for what this code already
                    // expected). Case-sensitive, same as before, to keep excluding the
                    // Special Ops hub's own separate all-caps "QUIT" item.
                    if (looksLikeCornerHintRow && RenderedTextMatchesReferenceKey(param_1, "MENU_QUIT", /*caseSensitive=*/true)) {
                        char bAsset[32] = {};
                        if (TryGetMenuGlyphAssetNameForKeyName("ESC", bAsset, sizeof(bAsset))) {
                            // Live-reported 2026-08-01: without the same vertical nudge every
                            // other corner hint in this file uses, the B icon rendered
                            // spilling outside the "Quit" box's bottom-right edge. This item's
                            // own p3 (995) matches Back/Friends' own baseline (also ~995)
                            // exactly, where kMenuHintVerticalNudge already renders correctly --
                            // applying the same nudge here for consistency.
                            // Live-reported 2026-08-01: icon needed to move slightly left --
                            // dropping the trailing space after "Quit" (present on every other
                            // corner hint's prefix text) tightens the icon gap by exactly one
                            // space-glyph's width, a small, precise nudge rather than adding a
                            // new bespoke X-offset constant.
                            float designX = 0.0f, designY = 0.0f;
                            ConvertRealScreenPosToDesignSpace(param_2, param_3 + kMenuHintVerticalNudge, designX, designY);
                            RequestMenuHintOverlay(designX, designY, "Quit", "", bAsset);
                            suppressRealDraw = true;
                        }
                    }
                    // Issue #68 (2026-08-05 language pass): replaced the hardcoded
                    // `_strnicmp(param_1, "Leaderboards", 12)` prefix match with a live
                    // comparison against PLATFORM_LEADERBOARDS_SHORTCUT's own current
                    // resolved template -- confirmed real key via zone_dump
                    // (code_post_gfx.str: `LANG_ENGLISH "Leaderboards ^2Right
                    // Mouse^7/^2F1^7"`, an exact match for the real live-captured
                    // string this call site was originally built against).
                    if (looksLikeCornerHintRow && RenderedTextMatchesReferenceKeyPrefix(param_1, "PLATFORM_LEADERBOARDS_SHORTCUT")) {
                        char backAsset[32] = {};
                        // "F1" resolves to PhysicalInput::Back (the real Back/Select/View
                        // button, NOT the B face button used for ESC-forward) in
                        // ResolveMenuGlyphAssetNameForKeyName -- per explicit user
                        // correction, these are two distinct physical inputs.
                        if (TryGetMenuGlyphAssetNameForKeyName("F1", backAsset, sizeof(backAsset))) {
                            float designX = 0.0f, designY = 0.0f;
                            ConvertRealScreenPosToDesignSpace(param_2, param_3 + kMenuHintVerticalNudge, designX, designY);
                            RequestMenuHintOverlay(designX, designY, "Leaderboards ", "", backAsset);
                            suppressRealDraw = true;
                        }
                    }
                    size_t textLen = suppressRealDraw ? 0 : strlen(param_1);
                    ColorHighlightSpan span = suppressRealDraw ? ColorHighlightSpan{} : FindColorHighlightSpan(param_1, textLen);
                    if (span.found) {
                        char highlighted[64] = {};
                        size_t copyLen = span.contentLen < sizeof(highlighted) - 1 ? span.contentLen : sizeof(highlighted) - 1;
                        memcpy(highlighted, param_1 + span.contentStart, copyLen);
                        highlighted[copyLen] = '\0';

                        // Issue #68 follow-up (2026-08-06 audit, "look for any other similar
                        // cases"): PLATFORM_FRIENDS_SHORTCUT/GAMESUMMARY_SHORTCUT/BACK_SHORTCUT
                        // are real, fixed reference-key strings ("Friends ^2F^7" etc, confirmed
                        // via zone_dump) with the accelerator letter baked directly into the
                        // template -- NOT run through the "&&1"/bind-resolver substitution that
                        // proved translatable for SPACE/G-or-Middle-Mouse/Left-Mouse, so there's
                        // no direct proof these letters themselves ever get translated. Matching
                        // the FULL real template via RenderedTextMatchesReferenceKey instead of
                        // extracting/comparing just the highlighted letter is still strictly more
                        // robust either way (correct whether or not the letter ever changes per
                        // language) and costs nothing -- still resolves through the same
                        // known-safe TryGetMenuGlyphAssetNameForKeyName call, just gated on the
                        // real string instead of the extracted substring.
                        // Hoisted into a named bool (not just inlined into the if-chain below)
                        // since InjectSyntheticBackHintIfNeeded's own Special-Ops/Friends-list
                        // suppression logic further down needs to know "is this the Friends
                        // hint specifically" too -- reused there instead of re-deriving it from
                        // `highlighted == "F"` a second time (same fix, one source of truth).
                        bool isFriendsShortcut = RenderedTextMatchesReferenceKey(param_1, "PLATFORM_FRIENDS_SHORTCUT");
                        char assetName[32] = {};
                        bool haveMenuAssetName;
                        if (isFriendsShortcut) {
                            haveMenuAssetName = TryGetMenuGlyphAssetNameForKeyName("F", assetName, sizeof(assetName));
                        } else if (RenderedTextMatchesReferenceKey(param_1, "PLATFORM_GAMESUMMARY_SHORTCUT")) {
                            haveMenuAssetName = TryGetMenuGlyphAssetNameForKeyName("G", assetName, sizeof(assetName));
                        } else if (RenderedTextMatchesReferenceKey(param_1, "PLATFORM_BACK_SHORTCUT")) {
                            haveMenuAssetName = TryGetMenuGlyphAssetNameForKeyName("ESC", assetName, sizeof(assetName));
                        } else {
                            haveMenuAssetName = TryGetMenuGlyphAssetNameForKeyName(highlighted, assetName, sizeof(assetName));
                        }
                        if (haveMenuAssetName) {
                            char prefixText[128] = {};
                            size_t prefixLen = span.markerStart < sizeof(prefixText) - 1 ? span.markerStart : sizeof(prefixText) - 1;
                            memcpy(prefixText, param_1, prefixLen);
                            prefixText[prefixLen] = '\0';

                            char suffixText[128] = {};
                            if (span.markerEnd < textLen) {
                                size_t suffixLen = textLen - span.markerEnd;
                                if (suffixLen >= sizeof(suffixText)) suffixLen = sizeof(suffixText) - 1;
                                memcpy(suffixText, param_1 + span.markerEnd, suffixLen);
                                suffixText[suffixLen] = '\0';
                            }

                            // Live-reported 2026-08-01: nothing drew at all (native text
                            // suppressed, no replacement visible) -- proxy_d3d9.log shows why:
                            // "Friends ^2F^7" logs p3=995, p6=1.500, and the gameplay hints'
                            // own "param_3 * param_6 = vertical center" formula (validated only
                            // against extraBigFont/hudSmallFont HUD text) gives 1492.5 here --
                            // off the bottom of a 1080-tall screen. That formula does not
                            // transfer to fonts/smallFont; use the raw, unscaled param_3 instead
                            // (995 alone is a plausible near-bottom-of-screen Y).
                            // Live-reported 2026-08-01 (round 2, screenshot of the real
                            // "Friends" hover tooltip box): drew, but sitting too low --
                            // text/icon read as hanging out the bottom of the highlight box
                            // instead of centered in it. Pixel-measured against that
                            // screenshot (box spans roughly y=94-162 in the cropped image,
                            // center ~128; drawn content's own visible center sat ~144-148,
                            // ~18px low) -- empirical upward nudge, same convention as the
                            // gameplay hints' own kHintVerticalNudge.
                            // Live-reported 2026-08-01 (round 3): "Friends doesn't show on some
                            // screens" / "Friends stays on screen when it should say Back" --
                            // MW3's menu UI shows multiple hints (e.g. Back AND Friends)
                            // simultaneously every frame, not one at a time like a gameplay
                            // interact hint. RequestCustomHintOverlay's single slot could only
                            // hold one of them per frame; switched to RequestMenuHintOverlay's
                            // multi-slot pool (see overlay_hud.h) so both can coexist.
                            //
                            // Live-reported 2026-08-01 (round 4, simplified per explicit user
                            // direction after the focus-struct nested-modal theory was confirmed
                            // live not to apply here at all): Special Ops' own "Friends" hint
                            // keeps firing even under modals that either have no corner hint of
                            // their own or render one through a path this project can't hook
                            // (see the big comment above Hook_00544a50/g_inSpecOpsFlow). **Doc-
                            // audit correction, 2026-08-01: g_inSpecOpsFlow was confirmed dead
                            // (OpenMenuByName never fires for any Special Ops name in any logged
                            // session) -- replaced with IsInsideSpecOpsNestedModal(), the
                            // getfocuseditemname()-based signal (see its own big comment above),
                            // which IS confirmed live to distinguish the nested modal from the
                            // root list.** Suppress Friends specifically while inside the modal --
                            // don't even request our own replacement for it, just hide the native
                            // text -- since InjectSyntheticBackHintIfNeeded unconditionally shows
                            // this project's own Back for the same condition.
                            // Live-reported 2026-08-01: also suppress this same "Friends" hint
                            // while the Friends list itself is open -- the underlying screen
                            // doesn't know it's now obscured by that modal either, same root
                            // cause as the Special Ops case above (see IsFriendsListOpen()'s own
                            // comment).
                            // isFriendsShortcut computed above (issue #68 audit, 2026-08-06) --
                            // was `_stricmp(highlighted, "F") == 0` here, replaced with the same
                            // real reference-key match already used to pick this hint's icon.
                            suppressRealDraw = true;
                            if (!(isFriendsShortcut && (IsInsideSpecOpsNestedModal() || IsFriendsListOpen()))) {
                                constexpr float kMenuHintVerticalNudge = -18.0f;
                                float designX = 0.0f, designY = 0.0f;
                                ConvertRealScreenPosToDesignSpace(param_2, param_3 + kMenuHintVerticalNudge, designX, designY);
                                RequestMenuHintOverlay(designX, designY, prefixText, suffixText,
                                    assetName);
                            }
                        }
                    }
                }
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            // Same rationale as every other read-only-turned-behavior block in this
            // file -- never let this crash the game over an unexpected param_1/fontArg
            // shape on some not-yet-seen call site.
        }
    }

    // Highlighted-item A-glyph investigation (2026-08-01) -- DIAGNOSTIC ONLY, see
    // the big comment above g_menuListItemOrdinalThisFrame. Any call that reached
    // here (not already consumed as Back/Friends/Game Summary/Leaderboards above)
    // and is plain fonts/smallFont text with NO "^N...^7" span is a CANDIDATE list
    // item -- corner hints always have a span, so this naturally excludes them
    // (still may catch unrelated decorative smallFont text with no span, e.g. a
    // header -- a known, accepted imprecision for this diagnostic pass, per the
    // scoping already agreed with the user).
    //
    // Gate changed 2026-08-02 from ShouldDrawGlyphOverlay() to IsMenuActive(): the
    // [list-item-diag] log line below is this project's own live calibration data
    // source for issue #51's manual per-screen position table -- gating it on
    // ShouldDrawGlyphOverlay() made it silently stop firing whenever keyboard/mouse
    // was detected as the active input method (issue #61's own hiding behavior),
    // which is exactly the moment a keyboard-driven capture tool would try to read
    // it -- a self-defeating race. The actual VISIBLE glyph draw request below still
    // checks ShouldDrawGlyphOverlay() on its own (see the trustworthyMatch block),
    // so this change only widens when the DIAGNOSTIC LOG fires, not when the glyph
    // itself is allowed to render.
    if (!suppressRealDraw && IsMenuActive()) {
        __try {
            if (LooksLikeValidPointer(reinterpret_cast<uintptr_t>(param_1)) &&
                LooksLikeValidPointer(reinterpret_cast<uintptr_t>(fontArg))) {
                const DiagFont* font = reinterpret_cast<const DiagFont*>(fontArg);
                if (IsMenuHintFont(font)) {
                    size_t textLen = strlen(param_1);
                    ColorHighlightSpan span = FindColorHighlightSpan(param_1, textLen);
                    // Live-reported 2026-08-01: the ordinal count was thrown off by a
                    // blank/whitespace-only text draw (a real, confirmed-live decorative
                    // element -- almost certainly a per-item subtitle/stat companion the
                    // .menu files gate on `visible when(getfocuseditemname() == "...")`,
                    // which can legitimately render blank content while still firing a
                    // draw call) landing BEFORE the real list items and shifting every
                    // subsequent ordinal by one. Real navigable menu items are never
                    // blank (`.menu` files always give them a real `exp text` locstring),
                    // so blank/whitespace-only text is filtered out of the ordinal count
                    // entirely rather than trying to special-case this one element.
                    bool isBlank = true;
                    for (size_t i = 0; i < textLen; ++i) {
                        if (!isspace(static_cast<unsigned char>(param_1[i]))) { isBlank = false; break; }
                    }
                    if (!span.found && !isBlank && kUseAutomaticListGlyphPositioning) {
                        // NEW automatic path (2026-08-03): just buffer this candidate's
                        // real measured text-end position -- see TryGetAutomaticGlyphPosition's
                        // own big comment for the full rationale. The actual draw decision
                        // happens once per frame in ResetMenuListItemOrdinalForFrame, after
                        // every candidate for this frame has been collected.
                        if (g_autoGlyphCandidateCount < kMaxAutoGlyphCandidates) {
                            int rawWidth = SumDirectIndexedGlyphWidthsBefore(font, param_1, textLen);
                            if (rawWidth >= 0) {
                                float textEndX = param_2 + static_cast<float>(rawWidth) * param_5;
                                g_autoGlyphCandidates[g_autoGlyphCandidateCount++] = { param_3, textEndX };
                            }
                        }
                    } else if (!span.found && !isBlank) {
                        int focusIndex = -1, itemCount = -1;
                        bool haveFocus = TryGetCurrentMenuFocusIndex(focusIndex, itemCount);
                        int selIndex = -1, selMaxIndexSeen = -1;
                        bool haveSelection = TryGetCurrentSelectionGroupAndIndex(selIndex, selMaxIndexSeen);
                        int ordinal = g_menuListItemOrdinalThisFrame;

                        // Cross-check v1 (2026-08-01): required BOTH focusIndex (from
                        // g_lastMenuListStruct/FUN_004dfd30) AND selIndex (from
                        // ui_swf_selection) to agree with the ordinal. Live-reported to
                        // NEVER pass on the Special Ops root list -- and the log confirms
                        // why: selIndex tracks the real navigation correctly (8, 7, 6, 5...
                        // as the player moved up), but focusIndex/itemCount stayed pinned
                        // to a completely unrelated list (itemCount 44/81, nowhere near this
                        // list's real ~9 items) THE WHOLE TIME. This matches an already-
                        // documented finding (known_issues.md issue #50's Attempt 1): Special
                        // Ops' own tile/list navigation never routes through the generic
                        // listbox dispatcher FUN_004dfd30 tracks at all -- focusIndex was
                        // never a valid signal for this specific screen, not an intermittent
                        // failure. Dropped it from the trustworthy condition entirely; still
                        // logged below for visibility. selIndex is kept as the sole ordinal
                        // cross-check, plus !g_specOpsModalSticky (2026-08-01, issue #50) to
                        // skip matching entirely during the one confirmed real background-
                        // bleed case (the root list drawing behind the Chaos/Mission/Survival
                        // modal).
                        //
                        // Live-reported 2026-08-01 (two follow-up bugs after first success):
                        // (1) the A icon persisted on the last-selected item even once nothing
                        // was highlighted at all -- a real keyboard/mouse-only state (cursor
                        // not hovering any item) that never happens on controller. (2) it also
                        // kept showing on the root list's own items while a DIFFERENT modal
                        // (not the one Special-Ops-specific case g_specOpsModalSticky covers)
                        // was open on top of it. Root cause for both: g_currentSelGroupName/
                        // g_currentSelIndex are frozen at their last real value by design
                        // (`ui_swf_selection` stops updating once you leave that list's own
                        // navigation) -- neither "nothing highlighted" nor "a different modal
                        // opened" clears them.
                        //
                        // Fixed both with one general check instead of two narrow ones:
                        // getfocuseditemname() gives the actual CURRENTLY focused item's name,
                        // live, regardless of cause. The expected name for THIS list's own
                        // selIndex is always exactly "<group>_<selIndex>" (confirmed live
                        // shape, e.g. "SPECOPS_BUTTON_LIST_3") -- if the real focused name
                        // doesn't match that exact string, focus is on something else right
                        // now (nothing, a different modal, a different list entirely), so this
                        // list's own A-glyph shouldn't draw. Subsumes both the "nothing
                        // highlighted" case (focused name is "none", never matches) and the
                        // "any modal is open" case (focused name is the modal's own item,
                        // never matches this list's pattern) without needing a per-modal
                        // allowlist or sticky flag at all.
                        // Cross-check switched 2026-08-03 (issue #51 follow-up) from
                        // getfocuseditemname()/g_focusedItemName to TryGetRealFocusedGroupAndIndex(),
                        // the same direct itemDef-array memory read that fixed Campaign's manual-
                        // position glyph. Same root cause here: g_focusedItemName only updates when
                        // a screen's OWN .menu script happens to call getfocuseditemname(), and this
                        // ordinal-based path is the fallback used by every screen WITHOUT a manual
                        // table entry -- any such screen whose script never calls it would silently
                        // never draw, identical to Campaign's symptom before its own fix, just via
                        // this path instead of the manual one. The real-focus read has no such
                        // dependency.
                        char realFocusGroup[64] = {};
                        int realFocusIndex = -1;
                        bool haveRealFocus = TryGetRealFocusedGroupAndIndex(realFocusGroup, sizeof(realFocusGroup), realFocusIndex);
                        bool focusMatchesThisList = haveSelection && haveRealFocus &&
                            _stricmp(realFocusGroup, g_currentSelGroupName) == 0 && realFocusIndex == selIndex;
                        // Real root cause confirmed live (2026-08-02): the ordinal counter
                        // (g_menuListItemOrdinalThisFrame) counts EVERY qualifying text draw
                        // in the whole frame, with no concept of which screen/layer it
                        // belongs to -- when a popup is open on top of another menu (this
                        // engine always dims what's behind a modal, confirmed by the user,
                        // and layers can stack more than once deep), the BACKGROUND screen's
                        // own items are still drawn and counted every frame too. A live
                        // capture caught the popup's own real item landing at ordinal=12
                        // while its real selIndex was 0 -- meaning a "trustworthy" match
                        // essentially never lands on the real item and instead accidentally
                        // attaches to whatever unrelated background item happens to occupy
                        // that small ordinal (captured case: the A-glyph rendered on
                        // "FIND ONLINE MATCH", a Special Ops hub item, while a completely
                        // unrelated DLC/on-disk-content popup was actually open on top of
                        // it).
                        //
                        // REVERTED 2026-08-02: a `GetMenuStackDepth() == 1` requirement was
                        // tried here as a safety net (suppress whenever more than one menu is
                        // open) -- user-caught regression: the stack is NEVER just 1 deep, even
                        // for perfectly ordinary navigation (the main menu itself already sits
                        // nested below a root/splash screen), so this suppressed the A-glyph
                        // almost everywhere, including the main menu where it previously worked
                        // fine. Reverted.
                        //
                        // CLOSED 2026-08-02 (issue #51): six Ghidra passes plus a live full-
                        // process-memory dump (MiniDumpWriteDump, raw byte scan for the actual
                        // print-command record) confirmed the text-draw command queue carries
                        // no itemDef back-reference at all -- the ordinal-vs-selIndex approach
                        // here can never be made reliable for a group whose items aren't first
                        // in the frame's draw order, because background/dimmed layers draw (and
                        // get ordinal-counted) first. Fixed via a manual, per-group calibrated
                        // position table instead (kManualGlyphPositions, populated from real
                        // live-memory-confirmed coordinates) -- see
                        // ResetMenuListItemOrdinalForFrame's own manual-position block, which
                        // fires independently of this ordinal match for any group with a table
                        // entry. Excluding those groups here so they don't also get a (wrong)
                        // second icon from this legacy path; everything without a manual entry
                        // still falls back to the original ordinal-based behavior.
                        bool trustworthyMatch = focusMatchesThisList && selIndex == ordinal &&
                            !HasManualGlyphPositionForGroup(g_currentSelGroupName, GetMenuStackDepth());

                        // Log-slimming pass (2026-08-08, issue #67): this used to fire
                        // unconditionally on every one of these calls (once per menu
                        // list item drawn, on ANY active menu screen) -- a real,
                        // confirmed contributor to proxy_d3d9.log's ~22GB growth.
                        // trustworthyMatch itself (used below regardless of this flag)
                        // is real production logic, not diagnostic-only -- only the
                        // logging of it is gated. See g_modConfig.listItemPositionLogging's
                        // own comment (mod_config.h) for when this is actually useful.
                        if (g_modConfig.listItemPositionLogging) {
                            char buf[300];
                            sprintf_s(buf, "[list-item-diag] ordinal=%d text=\"%.60s\" p2=%.1f p3=%.1f "
                                            "focusIndex=%d itemCount=%d selGroup=\"%s\" selIndex=%d realGroup=\"%s\" "
                                            "realIndex=%d menuDepth=%d trustworthy=%d",
                                       ordinal, param_1, param_2, param_3,
                                       haveFocus ? focusIndex : -1, haveFocus ? itemCount : -1,
                                       haveSelection ? g_currentSelGroupName : "?",
                                       haveSelection ? selIndex : -1,
                                       haveRealFocus ? realFocusGroup : "?",
                                       haveRealFocus ? realFocusIndex : -1,
                                       GetMenuStackDepth(), trustworthyMatch ? 1 : 0);
                            LogFromController(buf);
                        }

                        // ShouldDrawGlyphOverlay() checked here specifically (not on the
                        // outer block, see the comment above it) so the diagnostic log
                        // line above always fires while a menu is open, but the actual
                        // visible icon still respects issue #61's input-method hiding.
                        // v0.3.0 release standard (2026-08-03): also require this group to
                        // be on the explicit verified allowlist -- see kVerifiedGlyphGroups'
                        // own comment. In practice this fallback path (only ever reached for
                        // groups WITHOUT a manual table entry, per trustworthyMatch's own
                        // !HasManualGlyphPositionForGroup requirement) never matches anything
                        // currently on that list, so this suppresses the whole legacy path's
                        // visible output -- kept as an explicit, self-documenting check rather
                        // than relying on that being true only by coincidence.
                        if (trustworthyMatch && ShouldDrawGlyphOverlay() &&
                            IsVerifiedGlyphGroup(g_currentSelGroupName, GetMenuStackDepth()) &&
                            !IsGlyphDisabledGroup(g_currentSelGroupName)) {
                            // Per explicit user direction: don't touch the native text at
                            // all (no suppress, no redraw) -- just add the A/select glyph
                            // icon AFTER it, at its real measured width. Uses
                            // SumDirectIndexedGlyphWidthsBefore (built for issue #48's own
                            // diagnostic pass, common-ASCII-only, returns -1 for anything
                            // outside the safe range) scaled by this draw call's own real
                            // X scale factor (param_5, confirmed live to match the
                            // "p5=1.500"-style values already captured in hud-glyph-pos
                            // diagnostics) to get the item's actual on-screen text width,
                            // rather than assuming a fixed reserved column per list.
                            char aAsset[32] = {};
                            int rawWidth = SumDirectIndexedGlyphWidthsBefore(font, param_1, textLen);
                            if (rawWidth >= 0 && TryGetMenuGlyphAssetNameForKeyName("ENTER", aAsset, sizeof(aAsset))) {
                                constexpr float kIconGapAfterText = 12.0f;
                                // Live-reported 2026-08-01 (first successful appearance --
                                // needed a position tweak): every OTHER corner-hint icon at
                                // this same fonts/smallFont param_3 baseline in this file
                                // already applies kMenuHintVerticalNudge (-18) -- this was
                                // the one call site that skipped it.
                                constexpr float kMenuHintVerticalNudge = -18.0f;
                                float iconX = param_2 + static_cast<float>(rawWidth) * param_5 + kIconGapAfterText;
                                float designX = 0.0f, designY = 0.0f;
                                ConvertRealScreenPosToDesignSpace(iconX, param_3 + kMenuHintVerticalNudge, designX, designY);
                                RequestMenuHintOverlay(designX, designY, "", "", aAsset);
                            }
                        }
                        ++g_menuListItemOrdinalThisFrame;
                    }
                }
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            // Same rationale as every other read-only diagnostic in this file.
        }
    }

    // Issue #48/#49 continuation match -- see the big comment above
    // g_awaitingHintContinuationFont. Independent of the block above (a call is either
    // the highlighted hint itself, handled above, or a plain-text continuation of one
    // already suppressed this frame, handled here -- never both).
    if (g_awaitingHintContinuation && !suppressRealDraw && ShouldDrawGlyphOverlay() && !IsMenuActive()) {
        __try {
            if (LooksLikeValidPointer(reinterpret_cast<uintptr_t>(param_1)) &&
                fontArg == g_awaitingHintContinuationFont &&
                fabsf(param_3 - g_awaitingHintContinuationP3) < 0.5f) {
                size_t textLen = strlen(param_1);
                ColorHighlightSpan span = FindColorHighlightSpan(param_1, textLen);
                if (!span.found) {
                    AppendCustomHintSuffix(param_1, g_awaitingHintContinuationSlot);
                    suppressRealDraw = true;
                }
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            // Same rationale as every other block in this function.
        }
        g_awaitingHintContinuation = false; // one-shot regardless of outcome
    }

    if (!suppressRealDraw) {
        g_origDrawGlyphText(param_1, param_2, param_3, fontArg, param_5, param_6, param_7, param_8,
            param_9, param_10, param_11, param_12, param_13, param_14, param_15, param_16, param_17,
            param_18, param_19, param_20, param_21);
    }
}
} // namespace

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
// KNOWN GAP, not yet closed (2026-07-21, task #34): there is currently no real,
// always-visible on-screen text anywhere that contains byte 0x81, so this patch's
// effect cannot actually be SEEN yet even once it fires -- see known_issues.md
// issue #34 for the full investigation. Short version: `fonts/bigfont` (this
// patch's target) is real but is only ever drawn by `brightness_adjust.menu`
// (3 itemDefs), which the game only opens once per profile ever
// (`!getprofiledata("hasEverPlayed_MainMenu")`); forcing it open synthetically via
// this project's own SetDvarByName("cl_paused",1)+SetPlayerMenuFlags+OpenMenuByName
// recipe is ALREADY confirmed broken (garbled render, see InjectZoneLoadDebugTest's
// own comment above) regardless of content, so that's a dead end, not attempted
// here. Closing this gap for real needs either (a) a fresh player profile to
// naturally retrigger the real screen once, or (b) finding the actual native render
// call site for real gameplay interact-hint text (still untraced -- FUN_00568110
// builds the hint STRING but never selects a font itself, so the font choice
// happens at a separate, not-yet-found call site) and retargeting this whole patch
// at whichever font that turns out to be. Neither attempted this pass -- flagging
// honestly rather than leaving the false impression this is fully closed out.
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

// ---- hudBigFont-targeted retarget of the two tests above (2026-07-21, task #6/#34
// follow-up, real-data-driven) ------------------------------------------------------
//
// The two tests above target fonts/bigfont, confirmed this session (known_issues.md
// issue #34) to be the WRONG font for real interact-hint/HUD text -- bigfont is real
// but only ever drawn by a one-time-per-profile brightness-calibration screen. A live
// playtest of the new Hook_DrawGlyphText diagnostic (see its own comment above)
// gathered real evidence instead of another guess: a tally of every real font logged
// during a long, clean ~18,500-line Survival session --
//   fonts/hudBigFont    7929 uses  (dominant real HUD font, by a wide margin)
//   fonts/smallFont     4860 uses
//   fonts/hudSmallFont  2277 uses
//   fonts/extraBigFont  1648 uses
//   fonts/objectiveFont 1360 uses  (real and substantial, previously flagged as a
//                                   theoretical candidate on name alone -- now backed
//                                   by real usage data too)
//   fonts/bigFont        117 uses  (confirmed genuinely rare, consistent with the
//                                   one-time brightness-screen finding)
// hudBigFont is the strongest real candidate for "the font actual gameplay HUD/
// interact-hint text renders through" -- these two functions retarget the exact same,
// already-proven-safe mechanisms at it, gated behind their OWN distinct combos so
// they can never collide with the bigfont versions (LB+RB, LB+RB+A) or each other.
// The bigfont versions above are left completely untouched -- this is additive, not a
// replacement, since bigfont's struct-layout proof is still a valid, live-confirmed
// reference point regardless of which font turns out to matter for real hint text.
//
// objectiveFont is a real, substantial candidate too (1360 uses) but is NOT retargeted
// here -- one font at a time keeps each test's result unambiguous; if hudBigFont's
// struct dump or patch test doesn't pan out, objectiveFont is the natural next one to
// try, following this exact same pattern.
namespace {
enum class HudFontDiagStage { WaitingForCombo, Done };
HudFontDiagStage g_hudFontDiagStage = HudFontDiagStage::WaitingForCombo;
DWORD g_hudFontDiagHoldStartMs = 0;

enum class HudFontPatchStage { WaitingForCombo, Done };
HudFontPatchStage g_hudFontPatchStage = HudFontPatchStage::WaitingForCombo;
DWORD g_hudFontPatchHoldStartMs = 0;
} // namespace

// Read-only struct-layout diagnostic, retargeted at fonts/hudBigFont. Distinct combo
// (LB+RB+X) from every existing one (LB+RB = bigfont struct diag, LB+RB+A = bigfont
// patch test) so it can never fire alongside or be confused with either.
void InjectFontStructDebugTest_HudBigFont()
{
    if (g_hudFontDiagStage != HudFontDiagStage::WaitingForCombo) return;

    unsigned short buttons;
    unsigned char leftTrigger, rightTrigger;
    if (!Controller_GetRawButtonsAndTriggers(buttons, leftTrigger, rightTrigger)) return;

    bool comboHeld = (buttons & kXI_LEFT_SHOULDER) != 0 && (buttons & kXI_RIGHT_SHOULDER) != 0 &&
        (buttons & kXI_X) != 0;
    if (!comboHeld) {
        g_hudFontDiagHoldStartMs = 0;
        return;
    }
    if (g_hudFontDiagHoldStartMs == 0) {
        g_hudFontDiagHoldStartMs = GetTickCount();
        return;
    }
    if (GetTickCount() - g_hudFontDiagHoldStartMs < 2000) return;

    g_hudFontDiagStage = HudFontDiagStage::Done; // fire once per session regardless of outcome below

    LogFromController("[hudbigfont-struct-diag] LB+RB+X held 2s -- calling FindOrLoadFont(\"fonts/hudbigfont\")");
    void* rawFont = FindOrLoadFont("fonts/hudbigfont");
    char buf[256];
    if (!LooksLikeValidPointer(reinterpret_cast<uintptr_t>(rawFont))) {
        sprintf_s(buf, "[hudbigfont-struct-diag] FindOrLoadFont returned implausible pointer 0x%08X -- aborting dump",
            static_cast<unsigned>(reinterpret_cast<uintptr_t>(rawFont)));
        LogFromController(buf);
        return;
    }
    DiagFont* font = reinterpret_cast<DiagFont*>(rawFont);
    sprintf_s(buf, "[hudbigfont-struct-diag] Font* = 0x%08X, name=0x%08X pixelHeight=%d glyphCount=%d material=0x%08X glowMaterial=0x%08X glyphs=0x%08X",
        static_cast<unsigned>(reinterpret_cast<uintptr_t>(font)),
        static_cast<unsigned>(reinterpret_cast<uintptr_t>(font->fontName)),
        font->pixelHeight, font->glyphCount,
        static_cast<unsigned>(reinterpret_cast<uintptr_t>(font->material)),
        static_cast<unsigned>(reinterpret_cast<uintptr_t>(font->glowMaterial)),
        static_cast<unsigned>(reinterpret_cast<uintptr_t>(font->glyphs)));
    LogFromController(buf);

    if (LooksLikeValidPointer(reinterpret_cast<uintptr_t>(font->fontName))) {
        sprintf_s(buf, "[hudbigfont-struct-diag] fontName string = \"%.63s\"", font->fontName);
        LogFromController(buf);
    }

    if (!LooksLikeValidPointer(reinterpret_cast<uintptr_t>(font->glyphs)) ||
        font->glyphCount < 96 || font->glyphCount > 1000) {
        sprintf_s(buf, "[hudbigfont-struct-diag] glyphs ptr or glyphCount looks implausible (count=%d) -- not dumping entries, struct layout may be WRONG",
            font->glyphCount);
        LogFromController(buf);
        return;
    }

    auto dumpGlyph = [&](int idx, const char* label) {
        if (idx < 0 || idx >= font->glyphCount) return;
        const DiagGlyph& g = font->glyphs[idx];
        char b2[200];
        sprintf_s(b2, "[hudbigfont-struct-diag] glyph[%d] (%s): letter=0x%02X dx=%u pxW=%u pxH=%u s0=%.4f t0=%.4f s1=%.4f t1=%.4f",
            idx, label, g.letter, g.dx, g.pixelWidth, g.pixelHeight, g.s0, g.t0, g.s1, g.t1);
        LogFromController(b2);
    };
    dumpGlyph('A' - 0x20, "'A', direct-indexed");
    dumpGlyph('E' - 0x20, "'E', direct-indexed");
    if (font->glyphCount > 96) dumpGlyph(96, "first sorted-extra entry");
    if (font->glyphCount > 97) dumpGlyph(97, "second sorted-extra entry");

    LogFromController("[hudbigfont-struct-diag] dump complete -- if this struct layout matches the already-confirmed bigfont one (it should, per FUN_0047dfa0's generic lookup logic), hudBigFont is a safe patch target too.");
}

// Borrowed-UV glyph-array patch mechanism test, retargeted at fonts/hudBigFont. Same
// mechanism, same safety ordering (glyphs pointer written before glyphCount), same
// "borrow an existing glyph's UV rather than real new pixel content" scope-limiting
// as the bigfont version -- see the big comment above InjectFontGlyphPatchTest for
// the full mechanism rationale, not repeated here. Distinct combo (LB+RB+B) from
// every existing one.
//
// Unlike bigfont (which has no always-visible, repeatable text surface -- issue #34),
// hudBigFont is confirmed BY REAL USAGE DATA to draw constantly during ordinary HUD
// display (7929 real draws in one session) -- so if this patch fires and a future
// pass adds byte 0x81 into any hudBigFont-rendered string, this is a genuinely
// visible, repeatable test vehicle, not a one-time-per-profile dead end.
void InjectFontGlyphPatchTest_HudBigFont()
{
    if (g_hudFontPatchStage != HudFontPatchStage::WaitingForCombo) return;

    unsigned short buttons;
    unsigned char leftTrigger, rightTrigger;
    if (!Controller_GetRawButtonsAndTriggers(buttons, leftTrigger, rightTrigger)) return;

    bool comboHeld = (buttons & kXI_LEFT_SHOULDER) != 0 && (buttons & kXI_RIGHT_SHOULDER) != 0 &&
        (buttons & kXI_B) != 0;
    if (!comboHeld) {
        g_hudFontPatchHoldStartMs = 0;
        return;
    }
    if (g_hudFontPatchHoldStartMs == 0) {
        g_hudFontPatchHoldStartMs = GetTickCount();
        return;
    }
    if (GetTickCount() - g_hudFontPatchHoldStartMs < 2000) return;

    g_hudFontPatchStage = HudFontPatchStage::Done; // fire once regardless of outcome below

    LogFromController("[hudbigfont-patch-test] LB+RB+B held 2s -- attempting glyph-array patch on fonts/hudbigfont");
    void* rawFont = FindOrLoadFont("fonts/hudbigfont");
    // 400, not 200 -- the final "patch applied" message below is long (~330 chars)
    // once it's sprintf'd with a runtime codepoint instead of being a plain string
    // literal; a 200-byte buffer would make that sprintf_s overflow and abort().
    char buf[400];
    if (!LooksLikeValidPointer(reinterpret_cast<uintptr_t>(rawFont))) {
        LogFromController("[hudbigfont-patch-test] FindOrLoadFont returned implausible pointer -- aborting");
        return;
    }
    DiagFont* font = reinterpret_cast<DiagFont*>(rawFont);
    if (!LooksLikeValidPointer(reinterpret_cast<uintptr_t>(font->glyphs)) ||
        font->glyphCount < 96 || font->glyphCount > 1000) {
        sprintf_s(buf, "[hudbigfont-patch-test] glyphs ptr or glyphCount implausible (count=%d) -- aborting, struct layout may be wrong",
            font->glyphCount);
        LogFromController(buf);
        return;
    }

    const int oldCount = font->glyphCount;

    // Find a genuinely free codepoint at runtime instead of hardcoding 0x81 -- a live
    // playtest (2026-07-21, known_issues.md issue #34) proved 0x81 collides with a
    // real existing hudBigFont glyph at index 128, so the insert path never actually
    // ran. The [96, oldCount) tail is kept in ascending `letter` order (see the big
    // comment above InjectFontGlyphPatchTest), so a single left-to-right walk finds
    // both the free codepoint AND its correct sorted insertion index together:
    // whenever an existing entry's letter equals the current candidate, that
    // candidate is taken -- bump it and keep walking; the first entry whose letter is
    // GREATER than the (possibly bumped) candidate proves the candidate is free and
    // is exactly where it sorts in.
    unsigned int candidate = 0x81;
    int insertAt = -1;
    for (int i = 96; i < oldCount; ++i) {
        const unsigned short letter = font->glyphs[i].letter;
        if (letter < candidate) continue; // existing entry below our search floor
        if (letter == candidate) {
            ++candidate;
            if (candidate > 0xFF) break; // exhausted the whole search range
            continue;
        }
        insertAt = i; // letter > candidate: candidate is free, and sorts in right here
        break;
    }
    if (candidate > 0xFF) {
        LogFromController("[hudbigfont-patch-test] every codepoint from 0x81 to 0xFF is already taken -- aborting, no free codepoint found to insert");
        return;
    }
    if (insertAt < 0) insertAt = oldCount; // walked off the end still free -> append
    const unsigned short kNewCodepoint = static_cast<unsigned short>(candidate);

    DiagGlyph* newArray = new DiagGlyph[oldCount + 1];
    memcpy(newArray, font->glyphs, sizeof(DiagGlyph) * insertAt);
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

    sprintf_s(buf, "[hudbigfont-patch-test] built replacement array (%d -> %d entries), inserted codepoint 0x%02X at index %d, repointing live Font_s now",
        oldCount, oldCount + 1, kNewCodepoint, insertAt);
    LogFromController(buf);

    // Deliberate ordering -- see the big comment above InjectFontGlyphPatchTest.
    font->glyphs = newArray;
    font->glyphCount = oldCount + 1;
    // Old array intentionally leaked, not deleted -- same reasoning as the bigfont
    // version above.

    sprintf_s(buf, "[hudbigfont-patch-test] patch applied -- if the mechanism is sound AND any real HUD text drawn via hudBigFont ever contains byte 0x%02X, it should render as a visible (borrowed) 'A' glyph. hudBigFont draws constantly during real play (7929 uses/session observed) -- far more likely to actually be checkable than bigfont ever was.",
        kNewCodepoint);
    LogFromController(buf);

    // Record what was actually patched (font pointer + the runtime-discovered
    // codepoint) so InjectFontGlyphVisibilityTest_HudBigFont/Hook_DrawGlyphText below
    // can later inject exactly this codepoint into a real draw call for this exact
    // font, once armed. Setting these here rather than duplicating the patch logic is
    // deliberate -- one single source of truth for "what codepoint did we actually
    // insert."
    g_hudBigFontPtr = rawFont;
    g_hudFontPatchInsertedCodepoint = kNewCodepoint;
}

// ---- Visibility test for the hudBigFont glyph-array patch (task #6/#34 follow-up,
// 2026-07-21) ------------------------------------------------------------------------
//
// GAP THIS CLOSES: InjectFontGlyphPatchTest_HudBigFont (LB+RB+B, above) proves the
// insert-and-repoint mechanism itself works -- live-confirmed via log ("built
// replacement array (254 -> 255 entries), inserted codepoint 0xA0 at index 159,
// repointing live Font_s now", no crash) -- but nothing in the actual game currently
// draws any text containing that codepoint, so the newly-inserted glyph has never
// actually been RENDERED. Nothing about the patch mechanism itself is touched here.
//
// APPROACH CHOSEN (option (a) from the two considered): hook the already-installed,
// already-proven-safe Hook_DrawGlyphText (real function FUN_00690c80, the universal
// glyph-draw call every single piece of on-screen HUD/menu text already goes through,
// confirmed via disassembly to be a plain, ordinary function -- no thunk) and, ONLY
// once armed by holding this function's own distinct combo (LB+RB+Y -- a THIRD,
// separate combo from LB+RB / LB+RB+A / LB+RB+X / LB+RB+B, so it can never collide
// with any existing font-test trigger), rewrite a LOCAL STACK COPY of the very next
// matching draw call's real text (appending the inserted codepoint) and forward that
// copy instead of the original. The real buffer the game owns is only ever read, never
// written -- this cannot corrupt anything the game itself still holds a pointer or
// iterator into. See the big comment inside Hook_DrawGlyphText itself for the
// injection logic and its own safety notes (SEH-wrapped, one-shot, falls back to a
// normal unmodified draw call on any exception).
//
// OPTION (b) CONSIDERED AND REJECTED: reusing a known, always-registered console
// command's output as an injection anchor (the same "screenshot" anchor technique
// already used elsewhere in this project to FIND Cbuf_AddText/Cmd_ExecuteString, see
// the 2026-07-15 comment block above InjectControllerWeaponNext). That investigation
// already proved neither "screenshot" nor any other command in the real, live-dumped
// 132-entry Cmd_ExecuteString list produces output that routes through THIS draw path
// at all -- Cbuf_AddText/Cmd_ExecuteString append/execute console COMMANDS, they don't
// print text via FUN_00690c80's glyph-draw pipeline (confirmed separately: real HUD/
// interact-hint text reaches this function via a completely different, data-driven
// deferred-render-command-ring-buffer path, see known_issues.md issue #34's earlier
// entries). There is no known, always-available "print this exact string via
// hudBigFont" call site to anchor on -- so rather than inventing one (which would mean
// finding and calling some other not-yet-confirmed-safe function), this instead
// piggybacks on whatever REAL hudBigFont text is already on screen and already being
// drawn at the moment of arming (ammo counter, compass, interact hint, etc. -- all
// independently confirmed by the existing hud-font-id diagnostic to be real hudBigFont
// content) and appends one byte to it. Lower risk (reuses a real, already-executing
// call verbatim except for one appended character) and requires no new hook or new
// call into engine code.
//
// Gated on g_hudBigFontPtr/g_hudFontPatchInsertedCodepoint already being set (i.e.
// InjectFontGlyphPatchTest_HudBigFont, LB+RB+B, must have already run successfully
// this session) -- arming before that has happened would have nothing valid to
// inject, so it logs a clear message and still consumes the one-shot trigger rather
// than silently doing nothing, matching every other font-test combo's "fires once
// regardless of outcome" convention in this file.
namespace {
enum class HudFontVisibilityStage { WaitingForCombo, Done };
HudFontVisibilityStage g_hudFontVisibilityStage = HudFontVisibilityStage::WaitingForCombo;
DWORD g_hudFontVisibilityHoldStartMs = 0;
} // namespace

void InjectFontGlyphVisibilityTest_HudBigFont()
{
    if (g_hudFontVisibilityStage != HudFontVisibilityStage::WaitingForCombo) return;

    unsigned short buttons;
    unsigned char leftTrigger, rightTrigger;
    if (!Controller_GetRawButtonsAndTriggers(buttons, leftTrigger, rightTrigger)) return;

    bool comboHeld = (buttons & kXI_LEFT_SHOULDER) != 0 && (buttons & kXI_RIGHT_SHOULDER) != 0 &&
        (buttons & kXI_Y) != 0;
    if (!comboHeld) {
        g_hudFontVisibilityHoldStartMs = 0;
        return;
    }
    if (g_hudFontVisibilityHoldStartMs == 0) {
        g_hudFontVisibilityHoldStartMs = GetTickCount();
        return;
    }
    if (GetTickCount() - g_hudFontVisibilityHoldStartMs < 2000) return;

    g_hudFontVisibilityStage = HudFontVisibilityStage::Done; // fire once regardless of outcome below

    if (g_hudBigFontPtr == nullptr || g_hudFontPatchInsertedCodepoint == 0) {
        LogFromController("[hudbigfont-visibility-test] LB+RB+Y held 2s, but no codepoint has been inserted yet this session -- hold LB+RB+B first to run the glyph-array patch test, then this combo would need a fresh session to retry (one-shot per session, same as every other font-test combo in this file)");
        return;
    }

    char buf[300];
    sprintf_s(buf, "[hudbigfont-visibility-test] LB+RB+Y held 2s -- arming Hook_DrawGlyphText to inject codepoint 0x%02X into the very next real hudBigFont draw call as a local-copy-only modification (real game buffer will not be touched)",
        g_hudFontPatchInsertedCodepoint);
    LogFromController(buf);
    g_hudFontVisibilityArmed = true;
}

// ---- REAL glyph font extension: safe manual zone load + full field repoint --------
// (2026-07-19, task #6/#31, supersedes both the crashed boot-splice hook AND the
// borrowed-UV mechanism test above)
//
// The boot-splice hook (hooking FUN_004ca310 to auto-inject into FUN_00679680's
// real boot-time zone queue) crashed live -- disabled, see known_issues.md issue
// #30/#31. This is a DIFFERENT, safer mechanism: call the same real LoadZones
// function directly (a plain function call, NOT a hook on it) from the always-safe
// WndProc/SetTimer tick, well after boot has already finished -- the EXACT
// technique the original "roundtrip.ff" zoneload-test already proved safe (that
// test loaded a real, unmodified game menu this same way, screenshot-verified,
// zero crash). No boot-sequence timing is touched at all.
//
// Naming: `bigfont_ext.ff`'s font/materials/image were all registered under the
// SAME names as the real stock assets (fonts/bigfont, fonts/gamefonts_pc[_glow],
// gamefonts_pc) -- loading that zone as-is would hit the same asset-interning
// collision already found and abandoned for the menu-override work (FindOrLoadAsset
// always hands back the EXISTING cached entry for an already-registered name,
// never adopts new content). Rebuilt under unique names instead
// (`bigfont_glyph_ext.ff`: font "fonts/bigfont_ext", materials
// "fonts/gamefonts_pc_ext"/"fonts/gamefonts_pc_glow_ext", image
// "gamefonts_pc_ext") -- these load as genuinely NEW, independent objects, no
// collision at all.
//
// Once loaded, don't grow/patch the real font's glyph array by hand (like the
// mechanism test above does with a placeholder) -- `bigfont_ext`'s font asset is
// already a COMPLETE, correctly-built replacement (all 191 real glyphs' UVs
// rescaled for the taller extended atlas, plus the new glyph in real position),
// built entirely offline via the already-proven Linker pipeline. So just REPOINT
// the real "fonts/bigfont" Font_s's 4 relevant fields (material, glowMaterial,
// glyphCount, glyphs) at the loaded extended font's own already-correct values --
// no runtime array construction needed at all. Deliberate write order: `glyphs`
// pointer first (so a hypothetical concurrent reader using the OLD glyphCount with
// the NEW pointer just ignores the extra entries, safe), `material`/`glowMaterial`
// next, `glyphCount` last (governs how far the render loop indexes -- must be the
// last thing updated, after everything it depends on is already valid).
namespace {
enum class GlyphFontExtStage { NotStarted, Done };
GlyphFontExtStage g_glyphFontExtStage = GlyphFontExtStage::NotStarted;
} // namespace

void InstallGlyphFontExtension()
{
    if (g_glyphFontExtStage != GlyphFontExtStage::NotStarted) return;
    g_glyphFontExtStage = GlyphFontExtStage::Done; // fire once regardless of outcome below

    LogFromController("[glyph-font-ext] loading bigfont_glyph_ext zone (unique names, no boot hook)");
    ZoneLoadEntry entry{ "bigfont_glyph_ext", 4, 0 }; // flags=4, matches the proven
                                                        // "roundtrip" manual-call precedent
                                                        // (a direct call context, NOT the
                                                        // boot-batch's own flags=1 convention)
    LoadZones(&entry, 1, 0);
    LogFromController("[glyph-font-ext] LoadZones returned without crashing");

    void* rawExtFont = FindOrLoadFont("fonts/bigfont_ext");
    void* rawRealFont = FindOrLoadFont("fonts/bigfont");
    char buf[200];
    if (!LooksLikeValidPointer(reinterpret_cast<uintptr_t>(rawExtFont)) ||
        !LooksLikeValidPointer(reinterpret_cast<uintptr_t>(rawRealFont))) {
        sprintf_s(buf, "[glyph-font-ext] FindOrLoadFont returned implausible pointer(s) (ext=0x%08X real=0x%08X) -- aborting, not patching",
            static_cast<unsigned>(reinterpret_cast<uintptr_t>(rawExtFont)),
            static_cast<unsigned>(reinterpret_cast<uintptr_t>(rawRealFont)));
        LogFromController(buf);
        return;
    }
    if (rawExtFont == rawRealFont) {
        // Would mean the "unique name" load still resolved to the SAME cached
        // object as the real font -- our rename didn't actually work, or the
        // engine's interning is keyed on something other than the name string.
        // Abort rather than repoint a font at itself.
        LogFromController("[glyph-font-ext] ext font pointer == real font pointer -- naming collision NOT actually avoided, aborting");
        return;
    }

    DiagFont* extFont = reinterpret_cast<DiagFont*>(rawExtFont);
    DiagFont* realFont = reinterpret_cast<DiagFont*>(rawRealFont);

    if (!LooksLikeValidPointer(reinterpret_cast<uintptr_t>(extFont->glyphs)) ||
        extFont->glyphCount < 96 || extFont->glyphCount > 1000) {
        sprintf_s(buf, "[glyph-font-ext] loaded ext font's glyphs/count looks implausible (count=%d) -- aborting, not patching",
            extFont->glyphCount);
        LogFromController(buf);
        return;
    }

    sprintf_s(buf, "[glyph-font-ext] real font=0x%08X (glyphCount=%d) ext font=0x%08X (glyphCount=%d) -- repointing real font's fields now",
        static_cast<unsigned>(reinterpret_cast<uintptr_t>(realFont)), realFont->glyphCount,
        static_cast<unsigned>(reinterpret_cast<uintptr_t>(extFont)), extFont->glyphCount);
    LogFromController(buf);

    realFont->glyphs = extFont->glyphs;             // 1st: new pointer, old count still valid
    realFont->material = extFont->material;         // 2nd: new material (same atlas, extended)
    realFont->glowMaterial = extFont->glowMaterial;  // 2nd: new glow material
    realFont->glyphCount = extFont->glyphCount;      // 3rd, last: now safe to advertise the extra entries

    LogFromController("[glyph-font-ext] repoint complete -- real fonts/bigfont now has the extended glyph set and atlas. If sound, codepoint 0x81 should render its real intended glyph wherever bigfont draws text.");
}

// ---- Level-load-safe trigger for the glyph font extension: hook FUN_0053cbc0 ------
// (2026-07-21, task #6/#23 follow-up, "safer address-recovery approach" successor to
// the FUN_00679680 boot-thunk diagnostic above)
//
// That diagnostic solved the wrong problem. It was built to recover the real,
// resolved LoadZones address so a splice could call it directly instead of the
// unsafe FUN_004ca310 thunk -- but this project ALREADY has a proven-safe way to
// call FUN_004ca310 directly: `InstallGlyphFontExtension()` above does exactly this
// (`LoadZones(&entry, 1, 0)`, a plain un-hooked call through the `LoadZones`
// function pointer at the top of this file), and the "roundtrip.ff"/zoneload-test
// precedent already screenshot-confirmed this exact call pattern is safe to fire
// directly, repeatedly ("FUN_004ca310 returned without crashing" dozens of times in
// proxy_d3d9.log). **Calling the thunk directly was never the problem -- only
// HOOKING it is** (that's what corrupts its self-relocation math, see the ROOT
// CAUSE section above `Hook_FUN_00679680`). `InstallGlyphFontExtension()`'s own
// call to `LoadZones` was never unsafe in itself; it's DISABLED only because it was
// wired to fire from the WndProc/`SetTimer` tick, the wrong TIMING for
// material-bearing content (confirmed root cause: `iw5sp.md`'s "Black-screen
// flash... materials" section -- GPU-resource creation outside the engine's own
// controlled frame/thread discipline). `known_issues.md` issue #22/#23 already
// named the fix: "the only path... confirmed safe for material-bearing content is
// routing the load through a real FUN_0053cbc0-driven level-load transition
// instead." This hook is that fix, not a new idea.
//
// **FUN_0053cbc0 confirmed real and safe to hook this session, via fresh Ghidra
// disassembly (not assumed)**: `SUB ESP,0xc8; PUSH EBX; PUSH EBP; PUSH ESI; PUSH
// EDI; MOV EDI,[ESP+0xdc]` at entry, `POP EDI/ESI/EBP/EBX; ADD ESP,0xc8; RET` at
// exit -- a genuine, ordinary `__cdecl`-shaped function (confirmed real signature
// via decompile: `void FUN_0053cbc0(byte *param_1, int param_2)`, param_1 = a map/
// mission name string), body spanning 0053cbc0-0053ce94 (0x2D4 bytes) -- nothing
// like `FUN_004ca310`'s literal 7-byte `CALL;JMP EAX` thunk stub, more than enough
// room for MinHook's trampoline, and its own internal direct calls to
// `FUN_004ca310` (confirmed via decompile: it calls the thunk directly, multiple
// times, for real content packs like `common_specialops`/`common_survival`/
// `patch_<mapname>`) sit well past the hook's overwritten entry bytes, so they keep
// their real, un-relocated return addresses and the ILT self-patch mechanism inside
// the thunk's target keeps working exactly as the engine intends for all of them --
// this hook never touches or hooks the thunk itself, only this function's own
// outer entry/exit.
//
// **Confirmed real call frequency, not assumed**: exactly ONE real call site
// (`FindCallers`-equivalent xref search), inside `FUN_00447ea0` (the real per-
// level-load orchestrator -- decompile confirms "map_restart" command dispatch,
// "Start Level Save" checkpoint handling, and a guarded `if (*param_1 != '\0')
// FUN_0053cbc0(param_1, param_3);" call), itself called from exactly one place.
// So this hook fires once per real level load/restart/checkpoint-reload -- not
// per-frame, not spammy, the correct low-frequency "safe timing window" this
// project's own research already predicted.
namespace {
using Fn0053cbc0 = void(__cdecl*)(void* param1, int param2);
Fn0053cbc0 g_origFUN_0053cbc0 = nullptr;
int g_levelLoadZoneHookFireCount = 0;
}

void __cdecl Hook_FUN_0053cbc0(void* param1, int param2)
{
    g_origFUN_0053cbc0(param1, param2); // real function, completely unmodified --
                                          // its own internal FUN_004ca310 calls
                                          // execute exactly as the engine intends.

    ++g_levelLoadZoneHookFireCount;

    // Read-only diagnostic, logged EVERY call (not just once) so the real call
    // frequency itself is observable live, not just asserted from static analysis --
    // param1 read defensively (SEH-wrapped bounded copy), same pattern already
    // established elsewhere in this file (e.g. BindResolverLogAfterCall) for an
    // untrusted, engine-owned buffer pointer.
    char mapNameBuf[64] = {};
    bool mapNameReadable = false;
    if (LooksLikeValidPointer(reinterpret_cast<uintptr_t>(param1))) {
        __try {
            const char* p = reinterpret_cast<const char*>(param1);
            size_t i = 0;
            for (; i < sizeof(mapNameBuf) - 1; ++i) {
                char c = p[i];
                mapNameBuf[i] = c;
                if (c == '\0') break;
            }
            mapNameBuf[sizeof(mapNameBuf) - 1] = '\0';
            mapNameReadable = true;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            sprintf_s(mapNameBuf, "<faulted reading 0x%08X>", static_cast<unsigned>(reinterpret_cast<uintptr_t>(param1)));
        }
    }

    char buf[300];
    sprintf_s(buf, "[level-load-zone-hook] FUN_0053cbc0 returned (call #%d), param2=%d, mapName=\"%s\"%s",
        g_levelLoadZoneHookFireCount, param2, mapNameBuf,
        mapNameReadable ? "" : " (param1 not plausible/unreadable)");
    LogFromController(buf);

    // DISABLED BY DEFAULT -- the actual splice call. `InstallGlyphFontExtension()`
    // is already fully implemented and idempotent (fires once via its own
    // g_glyphFontExtStage guard, safe to call from multiple level loads). Left
    // commented out, matching this codebase's own established "risky/unverified
    // piece ships disabled" precedent (e.g. Hook_LoadZonesForBootSplice), because
    // this project has crashed live TWICE already from adjacent boot/zone-loading
    // mistakes and this specific call path has never been live-tested. The
    // reasoning above is believed sound (confirmed via fresh disassembly, not
    // assumption), but "believed sound" is not this project's bar for shipping a
    // mutating call live by default -- that bar is an actual confirmed-safe live
    // test. Enable only after the read-only diagnostic above is confirmed live
    // (correct call count/timing, no regression) across at least one real level
    // load.
    // InstallGlyphFontExtension();
}

extern "C" void __cdecl InjectAllControllerInput(unsigned char* cmd)
{
    int32_t inLevelVal = *reinterpret_cast<volatile int32_t*>(kInLevelFlagAddr);
    bool nowInLevel = inLevelVal > 0;
    // Diagnostic (2026-08-16, issue #1 follow-up, live-reported "the mod starts
    // breaking again" after a level RESTART specifically -- as opposed to a fresh
    // level load from the main menu). Hypothesis: issue #1's 3-second gate-clearing
    // window only re-arms on a RISING EDGE of this in-level flag
    // (nowInLevel && !g_wasInLevel, right below) -- if a "Restart Mission" resets
    // the current level in place without this flag ever actually dropping to <=0 in
    // between (unlike a fresh load, which presumably does), the window would never
    // re-arm on restart, and whatever gate-desync issue #1 originally fixed could
    // resurface exactly as described. NOT yet confirmed live -- logs the raw value
    // and the edge/window state on every real change so the next in-game "Restart
    // Mission" repro shows directly whether the flag actually dips to 0 or stays
    // positive throughout, rather than guessing at a fix from reasoning alone.
    {
        static int32_t s_lastLoggedInLevelVal = -999999;
        static bool s_lastLoggedNowInLevel = false;
        if (inLevelVal != s_lastLoggedInLevelVal || nowInLevel != s_lastLoggedNowInLevel) {
            s_lastLoggedInLevelVal = inLevelVal;
            s_lastLoggedNowInLevel = nowInLevel;
            char buf[160];
            sprintf_s(buf, "[inlevel-flag-diag] inLevelVal=%d nowInLevel=%d wasInLevel=%d risingEdge=%d",
                       inLevelVal, nowInLevel ? 1 : 0, g_wasInLevel ? 1 : 0,
                       (nowInLevel && !g_wasInLevel) ? 1 : 0);
            LogFromController(buf);
        }
    }
    // CONFIRMED live 2026-08-16 (see [inlevel-flag-diag] above): a "Restart Mission"
    // never drops this flag to <=0 -- it stays positive the entire time (normal
    // fluctuation observed in a small ~14-19 range) EXCEPT for one distinctive spike
    // (200, >10x the normal range) at the exact moment of restart, before immediately
    // returning to the normal range. The rising-edge re-arm below therefore never
    // fires on restart, matching the live-reported "the mod starts breaking again"
    // symptom exactly -- issue #1's whole 3-second gate-clearing window only ever
    // gets ONE chance to run, at the very first real level load of the session.
    // Second re-arm trigger: a sudden large jump in the raw value from one tick to
    // the next (normal noise is single digits; the observed restart spike was
    // ~180 above baseline) -- doesn't depend on knowing the exact sentinel value
    // (200 may not be universal across every restart/level), just that it's a very
    // large, anomalous jump no ordinary in-level fluctuation produces.
    constexpr int32_t kInLevelValSpikeThreshold = 50;
    static int32_t s_prevInLevelValForSpike = inLevelVal;
    bool inLevelValSpiked = nowInLevel && (inLevelVal - s_prevInLevelValForSpike) > kInLevelValSpikeThreshold;
    s_prevInLevelValForSpike = inLevelVal;

    if ((nowInLevel && !g_wasInLevel) || inLevelValSpiked) {
        g_levelEnterTick = GetTickCount();
        if (inLevelValSpiked && g_wasInLevel) {
            char buf[96];
            sprintf_s(buf, "[inlevel-flag-diag] restart spike detected (val=%d) -- gate window re-armed", inLevelVal);
            LogFromController(buf);
        }
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
    // Vibration "gets stuck on" fix (2026-08-16): a rumble event's own expiry
    // (its real, per-event duration -- a gunshot's short pulse vs. e.g. a longer
    // scripted campaign event, each keeping whatever duration TriggerRumble was
    // called with) only gets enforced by whichever tick actually runs UpdateRumbleOutput.
    // The gameplay tick (Rumble_Tick, InjectAllControllerInput) stops firing
    // entirely during a genuine pause -- same dead-tick problem this function
    // exists to solve for pause-menu input, see the big comment above -- so an
    // event triggered right before a pause would otherwise buzz for the whole
    // paused duration instead of cutting off on its own schedule. This tick
    // keeps running through pause (WndProc subclass + SetTimer), so calling the
    // SAME per-event expiry check from here too closes that gap without touching
    // any event's own configured duration.
    Rumble_TickExpiryWatchdog();
    // Synthetic "Back" hint for modals with no native corner hint of their own
    // (2026-08-01) -- see its own big comment. NOT called from here (live-reported:
    // this 60Hz timer tick isn't synchronized with the render frame that consumes
    // it, causing a visible flicker) -- called from overlay_hud.cpp's Hook_EndScene
    // instead, on the same per-frame cadence as the slot pool it feeds.
    // Menu highlighted-item focus tracking diagnostic (2026-08-01, issue #48
    // menu-glyph work) -- read-only, deduped-by-value logging, see its own comment.
    LogMenuFocusDiagnosticIfChanged();
    // DISABLED for the v0.1.3 public pre-alpha build (2026-07-17): task #23's zone/
    // menu-injection debug trigger (LB+RB hold) is real, working test code, not a
    // finished feature -- it's gated behind an internal combo, has no player-facing
    // purpose yet, and the underlying live-injection approach is now known to be
    // unsafe for real menu content (see re_notes/known_issues.md issue #23). Left
    // defined below for when this resumes (the level-load-transition alternative),
    // just not wired into the live tick for a public build.
    // InjectZoneLoadDebugTest(); // DEBUG ONLY, task #23 -- see comment above its definition
    // TickMenuDefScan(); // DEBUG ONLY, task #23 -- no-op unless a scan is active (StartMenuDefScan called)

    // DISABLED BEFORE EVER BEING LIVE-TESTED (2026-07-19) -- caught a direct
    // contradiction with already-established research before shipping this: loading
    // MATERIAL-bearing content (not a bare menu) from this WndProc/SetTimer tick was
    // already found (iw5sp.md, "Black-screen flash... root cause fully resolved:
    // materials") to trigger a synchronous D3D9 GPU-resource-creation cascade
    // OUTSIDE the engine's own controlled frame/thread discipline -- exactly the
    // class of operation confirmed to cause visible corruption/crashing from this
    // exact call site. bigfont_glyph_ext.ff contains 2 materials + an image +
    // shaders -- precisely the unsafe content class. The only path confirmed safe
    // for material-bearing content in that same research is routing the load
    // through a real FUN_0053cbc0-driven level-load transition instead, which is
    // NOT what this does. Do not re-enable without either finding that safe path or
    // independently re-verifying the WndProc-tick timing risk doesn't apply here.
    // InstallGlyphFontExtension();

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
    // DEBUG ONLY, task #6/#34 follow-up (2026-07-21) -- hudBigFont-targeted retargets
    // of the two tests above, gated behind their own distinct combos (LB+RB+X,
    // LB+RB+B) so they can never collide with the bigfont versions or each other. See
    // the big comment above InjectFontStructDebugTest_HudBigFont's definition for the
    // real-usage-data evidence behind this retarget.
    InjectFontStructDebugTest_HudBigFont();
    InjectFontGlyphPatchTest_HudBigFont();
    // DEBUG ONLY, task #6/#34 follow-up (2026-07-21) -- one-shot visibility test for
    // the patch above (LB+RB+Y, a third distinct combo). Only ever mutates a LOCAL
    // stack copy of one real draw call's text, inside the already-installed, already
    // read-only-proven Hook_DrawGlyphText -- see the big comment above
    // InjectFontGlyphVisibilityTest_HudBigFont's own definition for the full
    // rationale and the option considered and rejected (a console-command anchor).
    InjectFontGlyphVisibilityTest_HudBigFont();

    // Config hot-reload QoL feature (2026-07-31, user request) -- checked from this
    // always-running (WndProc/SetTimer) tick rather than the gameplay tick so it keeps
    // working even at the main menu/while paused, same reasoning as everything else on
    // this function. Internally rate-limited (CheckConfigHotReload only actually stats
    // the file once every ~1s) so this adds no meaningful per-tick cost.
    CheckConfigHotReload();
    // Testing-only overlay variant cycler (2026-07-31) -- a no-op unless [Overlay]
    // TestCycleAllVariants is explicitly enabled; see that config key's own comment.
    TickOverlayTestCycle();
}

// ---- Bind-resolver text hook, LOG-ONLY first pass (task #6/#35, 2026-07-21) -------
//
// Real target: FUN_0061f6f0, the bind->hint-text resolver this project's own research
// (re_notes/ui_assets.md, "Text-swap hook (FUN_0061f6f0)" and "FUN_0061f6f0's real
// calling convention, disassembly-confirmed" sections) already fully traced -- it's
// the single choke point both real hint mechanisms (the "&&1"-token family and the
// "[{+command}]"-bracket family) funnel through, so one hook here covers both without
// needing to find every individual call site.
//
// Confirmed real calling convention (register+stack hybrid, NOT plain __cdecl/
// __thiscall -- re-derive nothing here, this is already disassembly-verified):
//   EAX (register)  = a context value, forwarded into only the FIRST internal resolve
//                     call (not both, per the 2026-07-19 correction in ui_assets.md).
//   ECX (register)  = the bind-name/command context -- the "resolve this bind" arg.
//                     NOTE: which exact register carries a raw C-string bind name into
//                     the deeper FUN_005330a0 lookup was flagged UNRESOLVED by that
//                     research ("likely EAX... not confirmed identical to contextA") --
//                     this hook does NOT assume ECX is safely dereferenceable as a
//                     string; see BindResolverLogAfterCall's guarded handling below.
//   [esp+4]         = pushed by every caller but never read in the function body --
//                     confirmed dead, ignored here too.
//   [esp+8]         = output buffer pointer -- where the real trampoline writes
//                     "KEY_UNBOUND", a single key name, or "%s KEY_OR %s".
//   [esp+0xc]       = bool flag, "limit to 1 bind".
//   Plain RET (caller-cleanup for the stack portion, i.e. cdecl-shaped stack args).
//
// THIS PASS IS DELIBERATELY LOG-ONLY -- no output-buffer mutation, no behavior change
// at all. Two earlier hooks this project shipped without a safe incremental step first
// (the rumble dispatcher hook, known_issues.md issue #24; the boot-zone-splice hook,
// issue #22/#30) both crashed the live game outright. ui_assets.md's own recommendation
// for this specific hook is explicit: "Recommends prototyping log-only first (no buffer
// write) -- same lesson as both crashes this session." This is that first increment.
// Actually swapping in a glyph codepoint is deliberately NOT attempted here.
//
// Implementation shape (also per the research's own recommendation, to avoid one naive
// naked function trying to do everything at once): a small naked shim stashes the
// register args and the output-buffer/flag values into plain globals (this call site is
// not proven multi-threaded, same single-threaded assumption every other naked hook in
// this file already makes), forwards into the real trampoline with the ORIGINAL
// register/stack state fully intact (so real text resolution is completely untouched),
// then -- once the trampoline's own `ret` hands control back -- calls a normal C++
// logging function before resuming the real caller exactly where it would have resumed
// had this hook never existed.
//
// Forwarding mechanic, spelled out since it's easy to get wrong: a plain `call` to the
// trampoline would push a NEW return address on top of the existing 3 stack args,
// shifting the trampoline's own [esp+4]/[esp+8]/[esp+0xc] view down by 4 bytes relative
// to what the real caller set up -- wrong. Instead this OVERWRITES the incoming
// return-address slot in place with the address of the `afterCall` label (saving the
// real return address to a global first) and TAIL-JUMPS (not calls) into the
// trampoline, so it sees the byte-for-byte identical frame the real caller built, just
// with the return-address slot repointed. When the trampoline's own `ret` fires, it
// pops that slot and lands at `afterCall` with esp already exactly where the real
// caller would see it post-return -- at that point this hook only needs to run the
// (register-preserving) log call and then `jmp` to the saved real return address; there
// is no return-address slot left on the stack to `ret` through a second time.
namespace {
void* g_orig_0061f6f0 = nullptr;

// Scratch globals for the naked shim below -- no stack frame exists to hold these
// (naked, no prologue), and this call site is not known/proven to be multi-threaded,
// same assumption every other naked hook in this file already makes (e.g. Hook_0057de60
// below).
uintptr_t g_bindResolverCtxEax = 0;
uintptr_t g_bindResolverCtxEcx = 0;
uintptr_t g_bindResolverBufPtr = 0;
uintptr_t g_bindResolverLimitFlag = 0;
uintptr_t g_bindResolverRealRetAddr = 0;

// Dedup state so a hint that's on screen and getting re-resolved every frame doesn't
// flood the log -- same "change-triggered diagnostic logging" precedent already used
// elsewhere in this file (e.g. the pause-menu-field re-test). Independent of, and in
// addition to, the config toggle below (which is a full off switch).
char g_bindResolverLastLoggedText[128] = {};
} // namespace

// Definition of the forward declaration near Hook_DrawGlyphText (issue #48) -- just
// exposes the dedup buffer above by name. NOT thread-synchronized, same single-
// threaded assumption as the rest of this file's naked-hook globals; safe as long as
// this project never becomes multi-threaded without revisiting that assumption
// everywhere at once.
const char* GetLastResolvedBindKeyName()
{
    return g_bindResolverLastLoggedText;
}

namespace {

// LIVE-TESTED 2026-07-21 (known_issues.md issue #35): a real playtest logged
// implausible ECX(=0)/[esp+8] buffer(=0x100) values every call. Root-caused via
// fresh Ghidra disassembly of all 4 real callers of FUN_0061f6f0
// (FindCallers.java -> FUN_00622020, FUN_00622970, FUN_004be070, FUN_004fafd0):
// the 3 hint-resolution callers (FUN_004be070/004fafd0/00622020, the "&&1"-token
// and "[{...}]"-bracket families) all push their real output-buffer pointer at
// [esp+8], matching this hook's assumed convention exactly. FUN_00622970 does NOT
// -- its own disassembly (single call site, 00622970 -> CALL 0061f6f0 @ 006229a7)
// pushes, in order, PUSH 0x0 (flag, lands at [esp+0xc] -- consistent), then loads
// EAX=&local_100 (a REAL valid stack buffer) and pushes 0x100 (the buffer's SIZE)
// BEFORE pushing that EAX -- so at FUN_0061f6f0's entry from this one caller,
// [esp+4]=the real buffer pointer and [esp+8]=the literal 0x100, the reverse of
// every other caller. That's an exact, disassembly-confirmed match for the live
// symptom (buffer read as 0x100) -- this is caller-shape divergence (hypothesis
// 1), NOT a bug in this hook's own register-stashing (hypothesis 2 is refuted:
// EAX/ECX/[esp+8]/[esp+0xc] are read at the textbook-correct offsets for the
// convention every OTHER caller actually uses). FUN_00622970's own body
// (DAT_01c0b1ac/DAT_01c0b1b4 guard, "@MENU_BIND_KEY_PENDING" string literal)
// matches the 2026-07-18 fork research's prediction exactly: a key-rebind-capture
// UI ("waiting for the next physical key press to bind"), not a hint-text
// resolution call -- explains the live ECX=0 too (no "current bind" exists yet
// while capture is pending). The real return address for this one call site,
// confirmed via disassembly (instruction immediately after `CALL 0x0061f6f0` @
// 006229a7): 0x006229AC. Detected and skipped below rather than logged as
// (meaningless, misleading) hint text.
constexpr uintptr_t kMenuBindKeyCaptureCallerRetAddr = 0x006229AC;
bool g_loggedMenuBindKeyCaptureCallerOnce = false;
}

// ---- Bind-resolver glyph substitution mapping (task #6/#35, 2026-07-21) -----------
//
// Maps a real resolved KEY-NAME string (what FUN_0061f6f0's real trampoline already
// wrote into the output buffer, e.g. "MOUSE1", "SHIFT", "F") to a controller-glyph
// codepoint, so a hint like "Press F to interact" can eventually read as a real
// button icon for a controller player. Built and callable now; NOT wired to mutate
// anything unless g_modConfig.bindResolverGlyphSubstitution is explicitly turned on
// (default off, see that field's own comment in mod_config.h for why -- no font asset
// the running game can load yet actually renders these codepoints). Four-stage design:
//
//   1. key-name string -> LogicalAction (or a fixed D-pad direction) -- sourced
//      directly from this project's own RE-confirmed real default keyboard binds
//      (players2/config.cfg, tabulated in re_notes/iw5sp.md's "Button mapping --
//      approach change and real bind data" section), not guessed or re-derived here.
//   2. LogicalAction -> PhysicalInput, via the EXISTING g_buttonMap (mod_config.h/.cpp)
//      -- already correctly resolved per the player's real ButtonLayout/FlipTriggers
//      choice, reused here instead of re-implementing layout logic. D-pad directions
//      are fixed physical directions in this project's own scheme (not part of
//      ButtonMap, not remapped by any layout), so they skip this step entirely.
//   3. PhysicalInput (or D-pad direction) + the player's configured GlyphStyle -> an
//      actual glyph asset name, restricted to real files that exist in
//      assets/button_glyphs/ -- an unavailable combination is left unmapped (returns
//      false), never guessed/invented.
//   4. glyph asset name -> a single-byte codepoint. PROVISIONAL: only one codepoint
//      (0x81) has ever actually gone through the real font-build pipeline so far (a
//      single borrowed-UV test glyph, see re_notes/ui_assets.md's font-pipeline
//      sections) -- there is no finalized codepoint scheme yet. This table assigns
//      easy-to-change placeholder codepoints (sequential unused extended-ASCII bytes,
//      deliberately skipping 0x81) so the substitution LOGIC can be complete and
//      testable now; whoever finishes the real font-loading work (known_issues.md
//      issue #23) should reconcile these against whatever the actual shipped font
//      assigns, not assume this table is already authoritative.
//
// KNOWN GAP, not papered over: assets/button_glyphs/'s Xbox360 set has no left-stick-
// click/right-stick-click icons at all (only xboxmodern_ls/_rs and ps_l3/_r3 exist) --
// Sprint (LS) and Melee (RS) have no real Xbox360-style glyph asset. Left unmapped for
// that one style rather than substituting a wrong/placeholder icon.
namespace {

// Mirrors ButtonMap's own fields (mod_config.h) -- one entry per logical action this
// project's controller scheme actually assigns a PhysicalInput to. Deliberately NOT
// using PhysicalInput directly as the key-name table's value type, since several real
// keyboard keys map to the same PhysicalInput (e.g. F="+activate" and R="+reload" both
// resolve to this project's own reloadUse=X, which handles both via hold/tap) and
// this project's rebind-aware ButtonMap is the single source of truth for the actual
// PhysicalInput, not this table.
enum class LogicalAction {
    Fire, Ads, Lethal, Tactical, ReloadUse, WeaponSwitch, Jump, CrouchProne, Sprint, Melee, Pause, Scoreboard
};

enum class DpadDirection { Up, Down, Left, Right };

PhysicalInput PhysicalInputForAction(LogicalAction action)
{
    switch (action) {
        case LogicalAction::Fire: return g_buttonMap.fire;
        case LogicalAction::Ads: return g_buttonMap.ads;
        case LogicalAction::Lethal: return g_buttonMap.lethal;
        case LogicalAction::Tactical: return g_buttonMap.tactical;
        case LogicalAction::ReloadUse: return g_buttonMap.reloadUse;
        case LogicalAction::WeaponSwitch: return g_buttonMap.weaponSwitch;
        case LogicalAction::Jump: return g_buttonMap.jump;
        case LogicalAction::CrouchProne: return g_buttonMap.crouchProne;
        case LogicalAction::Sprint: return g_buttonMap.sprint;
        case LogicalAction::Melee: return g_buttonMap.melee;
        case LogicalAction::Pause: return g_buttonMap.pause;
        default: return g_buttonMap.scoreboard;
    }
}

// Real key-name -> LogicalAction, sourced directly from players2/config.cfg's real
// default binds (re_notes/iw5sp.md, "Button mapping" section). weapnext/togglemenu/
// toggleprone are real one-shot console commands, not kbutton binds (per that same
// research), but their BOUND KEYS are still real and the hint-resolver would still
// resolve their key names, so they're included here.
struct KeyActionEntry { const char* keyName; LogicalAction action; };
constexpr KeyActionEntry kKeyActionTable[] = {
    { "MOUSE1", LogicalAction::Fire },     // bind MOUSE1 "+attack"
    { "MOUSE2", LogicalAction::Ads },      // bind MOUSE2 "+toggleads_throw"
    { "G", LogicalAction::Lethal },        // bind G "+frag"
    { "Q", LogicalAction::Tactical },      // bind Q "+smoke"
    { "F", LogicalAction::ReloadUse },     // bind F "+activate"
    { "R", LogicalAction::ReloadUse },     // bind R "+reload" -- same PhysicalInput as F
    { "1", LogicalAction::WeaponSwitch },  // bind 1 "weapnext"
    { "2", LogicalAction::WeaponSwitch },  // bind 2 "weapnext"
    { "SPACE", LogicalAction::Jump },      // bind SPACE "+gostand"
    { "CTRL", LogicalAction::CrouchProne },// bind CTRL "toggleprone"
    { "SHIFT", LogicalAction::Sprint },    // bind SHIFT "+breath_sprint"
    { "E", LogicalAction::Melee },         // bind E "+melee_zoom"
    { "ESCAPE", LogicalAction::Pause },    // bind ESCAPE "togglemenu"
    { "TAB", LogicalAction::Scoreboard },  // bind TAB "+scores"
};

// Real key-name -> fixed D-pad direction (+actionslot 1-4). Not part of ButtonMap --
// this project's D-pad slot assignment is a fixed physical mapping, not layout-
// dependent. Real quirk, already documented elsewhere in this project: key "5" binds
// +actionslot 2, NOT slot 5 -- kept exactly as the real config.cfg has it.
struct KeyDpadEntry { const char* keyName; DpadDirection dir; };
constexpr KeyDpadEntry kKeyDpadTable[] = {
    { "N", DpadDirection::Up },     // bind N "+actionslot 1"
    { "5", DpadDirection::Right },  // bind 5 "+actionslot 2" (real quirk, not slot 5)
    { "3", DpadDirection::Down },   // bind 3 "+actionslot 3"
    { "4", DpadDirection::Left },   // bind 4 "+actionslot 4"
};

// PhysicalInput/GlyphStyle -> real glyph asset name (assets/button_glyphs/*.png,
// already extracted/committed). Empty string means no real asset exists for that
// combination -- checked explicitly by the caller, never falls back to a guess.
const char* GlyphAssetName(PhysicalInput input, GlyphStyle style)
{
    switch (style) {
        case GlyphStyle::Xbox360:
            switch (input) {
                case PhysicalInput::RT: return "xbox360_rt";
                case PhysicalInput::LT: return "xbox360_lt";
                case PhysicalInput::RB: return "xbox360_rb";
                case PhysicalInput::LB: return "xbox360_lb";
                case PhysicalInput::X: return "xbox360_x";
                case PhysicalInput::Y: return "xbox360_y";
                case PhysicalInput::A: return "xbox360_a";
                case PhysicalInput::B: return "xbox360_b";
                case PhysicalInput::Start: return "xbox360_start";
                case PhysicalInput::Back: return "xbox360_back";
                // KNOWN GAP (see the big comment above this whole section): no
                // xbox360_ls/xbox360_rs asset exists -- Sprint/Melee unmapped here.
                default: return "";
            }
        case GlyphStyle::XboxModern:
            switch (input) {
                case PhysicalInput::RT: return "xboxmodern_rt";
                case PhysicalInput::LT: return "xboxmodern_lt";
                case PhysicalInput::RB: return "xboxmodern_rb";
                case PhysicalInput::LB: return "xboxmodern_lb";
                case PhysicalInput::X: return "xboxmodern_x";
                case PhysicalInput::Y: return "xboxmodern_y";
                case PhysicalInput::A: return "xboxmodern_a";
                case PhysicalInput::B: return "xboxmodern_b";
                case PhysicalInput::Start: return "xboxmodern_menu"; // Xbox One/Series
                                                                       // renamed Start->Menu
                case PhysicalInput::Back: return "xboxmodern_view";  // ...and Back->View
                case PhysicalInput::LS: return "xboxmodern_ls";
                case PhysicalInput::RS: return "xboxmodern_rs";
                default: return "";
            }
        case GlyphStyle::PlayStation:
            switch (input) {
                case PhysicalInput::RT: return "ps_r2";
                case PhysicalInput::LT: return "ps_l2";
                case PhysicalInput::RB: return "ps_r1";
                case PhysicalInput::LB: return "ps_l1";
                case PhysicalInput::X: return "ps_square";
                case PhysicalInput::Y: return "ps_triangle";
                case PhysicalInput::A: return "ps_cross";
                case PhysicalInput::B: return "ps_circle";
                case PhysicalInput::Start: return "ps_options";
                case PhysicalInput::Back: return "ps_create";
                case PhysicalInput::LS: return "ps_l3";
                case PhysicalInput::RS: return "ps_r3";
                default: return "";
            }
    }
    return "";
}

const char* DpadGlyphAssetName(DpadDirection dir)
{
    // Universal, brand-independent assets -- same file regardless of GlyphStyle (see
    // ui_assets.md's "Row 4 -- universal" note).
    switch (dir) {
        case DpadDirection::Up: return "dpad_up";
        case DpadDirection::Down: return "dpad_down";
        case DpadDirection::Left: return "dpad_left";
        case DpadDirection::Right: return "dpad_right";
    }
    return "";
}

// Glyph asset name -> PROVISIONAL single-byte codepoint -- see the big comment above
// this section for why these are placeholders, not a finalized scheme. Deliberately
// skips 0x81 (already spoken for by the existing InjectFontGlyphPatchTest mechanism
// test).
struct GlyphCodepointEntry { const char* assetName; unsigned char codepoint; };
constexpr GlyphCodepointEntry kGlyphCodepointTable[] = {
    { "xbox360_a", 0x82 }, { "xbox360_b", 0x83 }, { "xbox360_x", 0x84 }, { "xbox360_y", 0x85 },
    { "xbox360_lb", 0x86 }, { "xbox360_rb", 0x87 }, { "xbox360_lt", 0x88 }, { "xbox360_rt", 0x89 },
    { "xbox360_back", 0x8A }, { "xbox360_start", 0x8B },
    { "xboxmodern_a", 0x8C }, { "xboxmodern_b", 0x8E }, { "xboxmodern_x", 0x8F }, { "xboxmodern_y", 0x90 },
    { "xboxmodern_lb", 0x91 }, { "xboxmodern_rb", 0x92 }, { "xboxmodern_lt", 0x93 }, { "xboxmodern_rt", 0x94 },
    { "xboxmodern_view", 0x95 }, { "xboxmodern_menu", 0x96 }, { "xboxmodern_ls", 0x97 }, { "xboxmodern_rs", 0x98 },
    { "ps_cross", 0x99 }, { "ps_circle", 0x9A }, { "ps_triangle", 0x9B }, { "ps_square", 0x9C },
    { "ps_l1", 0x9D }, { "ps_r1", 0x9E }, { "ps_l2", 0x9F }, { "ps_r2", 0xA0 },
    { "ps_create", 0xA1 }, { "ps_options", 0xA2 }, { "ps_touchpad", 0xA3 }, { "ps_l3", 0xA4 }, { "ps_r3", 0xA5 },
    { "dpad_up", 0xA6 }, { "dpad_down", 0xA7 }, { "dpad_left", 0xA8 }, { "dpad_right", 0xA9 },
};

// Small, local-only style-name helper for log lines -- deliberately NOT reusing
// mod_config.cpp's own GlyphStyleName (that one lives in an anonymous namespace in a
// different translation unit, not exported, per this project's own module-separation
// convention of keeping config plumbing and gameplay/UI translation logic apart).
const char* GlyphStyleLogName(GlyphStyle s)
{
    switch (s) {
        case GlyphStyle::XboxModern: return "XboxModern";
        case GlyphStyle::PlayStation: return "PlayStation";
        default: return "Xbox360";
    }
}

// Shared by both TryGetGlyphCodepointForKeyName (in-font substitution, parked per
// issue #48's pivot) and TryGetGlyphAssetNameForKeyName (the overlay-icon path
// actually being used now) -- the first 3 stages of the 4-stage design described in
// the big comment above this section: key-name string -> LogicalAction/DpadDirection
// -> PhysicalInput -> real asset name. Returns "" (never guessed/invented) if no
// mapping/asset exists for this (key, style) pair, e.g. the Xbox360 LS/RS gap.
//
// Trims surrounding whitespace first (issue #48's own live capture found real drawn
// hint text with the key name padded inside its color-highlight span, e.g. "Press^3
// F ^7to pick up" -> highlighted span " F "), and aliases "ESC" -> "ESCAPE" (the real
// bound key name kKeyActionTable stores, per config.cfg) since "Back ^2ESC^7" is a
// real observed menu-hint abbreviation, not the literal bind name.
const char* ResolveGlyphAssetNameForKeyName(const char* rawKeyName)
{
    char trimmed[32] = {};
    size_t start = 0, end = strlen(rawKeyName);
    while (start < end && isspace(static_cast<unsigned char>(rawKeyName[start]))) ++start;
    while (end > start && isspace(static_cast<unsigned char>(rawKeyName[end - 1]))) --end;
    size_t len = end - start;
    if (len == 0 || len >= sizeof(trimmed)) return "";
    memcpy(trimmed, rawKeyName + start, len);
    trimmed[len] = '\0';

    const char* keyName = (_stricmp(trimmed, "ESC") == 0) ? "ESCAPE" : trimmed;

    // Live-reported 2026-07-31: the real grenade-throwback hint resolves to a
    // genuine combo-key string, "G or Middle Mouse" (confirmed via proxy_d3d9.log --
    // "^3G or Middle Mouse ^7throw back") -- neither half matches a single entry in
    // kKeyActionTable, and combo binds like this were never meant to. This is really
    // just the Lethal action in a different context (catching/throwing back an
    // enemy grenade uses the same physical input as throwing your own) -- resolved
    // through PhysicalInputForAction(LogicalAction::Lethal), NOT a hardcoded
    // PhysicalInput::RB, per explicit standing correction (2026-08-01): "glyphs must
    // represent the current control scheme defined in settings... this doesn't
    // affect menus" -- a hardcoded physical button would show the wrong glyph for
    // any ButtonLayout other than whichever one RB happened to be Lethal on.
    if (_stricmp(keyName, "G or Middle Mouse") == 0) {
        const char* assetName = GlyphAssetName(PhysicalInputForAction(LogicalAction::Lethal), g_modConfig.glyphStyle);
        if (assetName && assetName[0] != '\0') return assetName;
    }
    // Survival's ready-up hint resolves to "F5" (the real synthetic key this project
    // itself presses -- see InjectControllerReadyUp/issue #5's own PostMessage
    // workaround), not a real default keyboard bind, so it was never in
    // kKeyActionTable to begin with. A quick tap of the same button also just does a
    // normal weapon switch (see InjectControllerReadyUp's own release-before-
    // threshold fallback) -- this IS the WeaponSwitch action, so resolved the same
    // layout-aware way as everything else rather than a hardcoded PhysicalInput::Y.
    if (_stricmp(keyName, "F5") == 0) {
        const char* assetName = GlyphAssetName(PhysicalInputForAction(LogicalAction::WeaponSwitch), g_modConfig.glyphStyle);
        if (assetName && assetName[0] != '\0') return assetName;
    }
    // Turret placement (issue #58, 2026-08-02): real string confirmed via a live
    // proxy_d3d9.log capture -- "Press ^3Left Mouse^7 to place the turret." at
    // p2=734/p3=718, the exact same row/font/format the generic pickup/buy-station
    // interact hint already handles correctly, so no new draw-site code is needed --
    // only this resolution. "Left Mouse" is real vanilla Fire's own default bind text
    // (kKeyActionTable already has the raw "MOUSE1" form mapping to the same action,
    // used elsewhere) -- resolved the same layout-aware way as the G-or-Middle-Mouse/
    // F5 cases above (PhysicalInputForAction, not a hardcoded PhysicalInput::RT) so
    // this always shows whichever physical button the current ButtonLayout has Fire on.
    if (_stricmp(keyName, "Left Mouse") == 0) {
        const char* assetName = GlyphAssetName(PhysicalInputForAction(LogicalAction::Fire), g_modConfig.glyphStyle);
        if (assetName && assetName[0] != '\0') return assetName;
    }

    for (const auto& e : kKeyActionTable) {
        if (_stricmp(e.keyName, keyName) == 0) {
            const char* assetName = GlyphAssetName(PhysicalInputForAction(e.action), g_modConfig.glyphStyle);
            if (assetName && assetName[0] != '\0') return assetName;
            break;
        }
    }
    for (const auto& e : kKeyDpadTable) {
        if (_stricmp(e.keyName, keyName) == 0) {
            const char* assetName = DpadGlyphAssetName(e.dir);
            if (assetName && assetName[0] != '\0') return assetName;
            break;
        }
    }
    return "";
}

// Menu UI hints (fonts/smallFont -- "Back ^2ESC^7", "Friends ^2F^7") use a
// COMPLETELY DIFFERENT bind vocabulary than gameplay hints, so this is a
// deliberately SEPARATE table from ResolveGlyphAssetNameForKeyName above, not a
// fallback path through it. Reusing the gameplay table here would silently map
// keys to the wrong physical button: "ESCAPE" is in kKeyActionTable, but it
// resolves to g_buttonMap.pause (Start, whatever opens the pause menu) -- the
// button that actually forwards ESC to an already-open menu is B (this
// project's own real ESC-forward implementation, hardcoded regardless of
// ButtonLayout), a completely different mapping only true in this menu-hint
// context. Likewise "F" here means the Friends list, which the user explicitly
// confirmed is Y on console, not gameplay's F=X (ReloadUse/Interact). Returns ""
// (never guessed) for any key this table doesn't explicitly cover -- menus this
// project hasn't verified a real console mapping for keep rendering natively,
// same fail-closed convention as the gameplay resolver.
const char* ResolveMenuGlyphAssetNameForKeyName(const char* rawKeyName)
{
    char trimmed[32] = {};
    size_t start = 0, end = strlen(rawKeyName);
    while (start < end && isspace(static_cast<unsigned char>(rawKeyName[start]))) ++start;
    while (end > start && isspace(static_cast<unsigned char>(rawKeyName[end - 1]))) --end;
    size_t len = end - start;
    if (len == 0 || len >= sizeof(trimmed)) return "";
    memcpy(trimmed, rawKeyName + start, len);
    trimmed[len] = '\0';

    if (_stricmp(trimmed, "ESC") == 0 || _stricmp(trimmed, "ESCAPE") == 0) {
        const char* assetName = GlyphAssetName(PhysicalInput::B, g_modConfig.glyphStyle);
        if (assetName && assetName[0] != '\0') return assetName;
    }
    if (_stricmp(trimmed, "F") == 0) {
        const char* assetName = GlyphAssetName(PhysicalInput::Y, g_modConfig.glyphStyle);
        if (assetName && assetName[0] != '\0') return assetName;
    }
    if (_stricmp(trimmed, "ENTER") == 0 || _stricmp(trimmed, "RETURN") == 0) {
        const char* assetName = GlyphAssetName(PhysicalInput::A, g_modConfig.glyphStyle);
        if (assetName && assetName[0] != '\0') return assetName;
    }
    if (_stricmp(trimmed, "G") == 0) {
        // Game Summary (2026-08-01, quick completeness fix, real capture: "Game
        // Summary ^2G^7"). Per explicit user instruction: X on controller.
        const char* assetName = GlyphAssetName(PhysicalInput::X, g_modConfig.glyphStyle);
        if (assetName && assetName[0] != '\0') return assetName;
    }
    if (_stricmp(trimmed, "F1") == 0) {
        // Leaderboards (2026-08-01, quick completeness fix) -- see the big comment
        // at the Leaderboards call site in Hook_DrawGlyphText for the full real
        // string this comes from. Per explicit user correction: the real Back/
        // Select/View button (PhysicalInput::Back), NOT the B face button already
        // used for ESC-forward -- these are two distinct physical inputs.
        const char* assetName = GlyphAssetName(PhysicalInput::Back, g_modConfig.glyphStyle);
        if (assetName && assetName[0] != '\0') return assetName;
    }
    return "";
}

} // namespace

// Issue #68 follow-up (2026-08-05 live retest): the mantle hint's icon used to be
// resolved via TryGetGlyphAssetNameForKeyName(highlighted, ...), keying off the
// SUBSTITUTED key-name text extracted from the "^3...^7" span -- broken under any
// language that translates that word (confirmed live: Italian renders it "Spazio",
// which doesn't match kKeyActionTable's English-only "SPACE" entry, so the whole
// lookup silently fails and the icon never shows). Since the caller already confirmed
// this IS the mantle hint via RenderedTextMatchesSubstitutionTemplate (a structural
// match against PLATFORM_MANTLE that doesn't depend on the substituted text at all),
// this resolves the icon directly from the already-known LogicalAction the mantle
// bind maps to (kKeyActionTable's own "SPACE" -> LogicalAction::Jump entry) --
// bypassing the translated-text lookup entirely, same precedent as the G-or-Middle-
// Mouse/F5/Left-Mouse special cases in ResolveGlyphAssetNameForKeyName. Defined here,
// outside the anonymous namespace above, for the same reason TryGetGlyphAssetNameForKeyName
// itself is (its own forward declaration near this file's top is at global scope) --
// GlyphAssetName/PhysicalInputForAction/LogicalAction, though declared with internal
// linkage inside that namespace, are still visible by plain name from here since
// they're in the same translation unit.
bool TryGetMantleGlyphAssetName(char* outAssetName, size_t outSize)
{
    const char* assetName = GlyphAssetName(PhysicalInputForAction(LogicalAction::Jump), g_modConfig.glyphStyle);
    if (!assetName || assetName[0] == '\0') return false;
    strncpy_s(outAssetName, outSize, assetName, _TRUNCATE);
    return true;
}

// Same technique as TryGetMantleGlyphAssetName above, for the grenade-throwback hint
// (PLATFORM_THROWBACKGRENADE) -- its own real substituted key text is a combo string,
// "G or Middle Mouse" (confirmed live via proxy_d3d9.log), whose English word "or" is
// exactly the same translation risk "SPACE" turned out to have. Resolves straight from
// LogicalAction::Lethal (the same action the ORIGINAL "G or Middle Mouse" special case
// in ResolveGlyphAssetNameForKeyName already used) instead of matching that combo text.
bool TryGetThrowbackGlyphAssetName(char* outAssetName, size_t outSize)
{
    const char* assetName = GlyphAssetName(PhysicalInputForAction(LogicalAction::Lethal), g_modConfig.glyphStyle);
    if (!assetName || assetName[0] == '\0') return false;
    strncpy_s(outAssetName, outSize, assetName, _TRUNCATE);
    return true;
}

// Same technique, for the turret-placement hint (SENTRY_PLACE), resolved straight from
// LogicalAction::Fire (the same action the original "Left Mouse" special case in
// ResolveGlyphAssetNameForKeyName already used, since +attack is Fire's own bind).
bool TryGetSentryPlaceGlyphAssetName(char* outAssetName, size_t outSize)
{
    const char* assetName = GlyphAssetName(PhysicalInputForAction(LogicalAction::Fire), g_modConfig.glyphStyle);
    if (!assetName || assetName[0] == '\0') return false;
    strncpy_s(outAssetName, outSize, assetName, _TRUNCATE);
    return true;
}

// Public wrapper (issue #66 follow-up, 2026-08-05, live-reported: "the lack of
// actual button glyphs is" the problem with the custom Options screen -- its
// footer/tab bar was drawing text-only prompts like "LB/RB TABS" instead of real
// controller-glyph icons, unlike every other button hint this project draws).
// GlyphAssetName itself has internal linkage (defined inside the anonymous
// namespace above, alongside this file's other TU-local resolver tables) -- this
// is the one externally-callable form of it, letting overlay_hud.cpp draw real
// icons for its own raw physical-button prompts (LB/RB/A/B) without needing a
// keyboard bind-name to resolve through (unlike TryGetMenuGlyphAssetNameForKeyName,
// whose whole design is keyed off a real keyboard bind string this menu doesn't have
// one of -- its navigation is raw D-pad/A/B/LB/RB, never forwarded through a real
// keybind at all).
extern "C" const char* GetControllerGlyphAssetName(PhysicalInput input, GlyphStyle style)
{
    return GlyphAssetName(input, style);
}

// Real key-name string (already trimmed/validated by the caller) -> a single-byte
// substitution codepoint for the CURRENTLY configured GlyphStyle, or false if no
// mapping/asset exists for this (key, style) pair (e.g. the Xbox360 LS/RS gap, or a
// key this table simply doesn't cover yet). Pure lookup -- no I/O, no mutation.
bool TryGetGlyphCodepointForKeyName(const char* keyName, unsigned char& outCodepoint)
{
    const char* assetName = ResolveGlyphAssetNameForKeyName(keyName);
    if (assetName[0] == '\0') return false;

    for (const auto& e : kGlyphCodepointTable) {
        if (strcmp(e.assetName, assetName) == 0) {
            outCodepoint = e.codepoint;
            return true;
        }
    }
    // Asset exists but has no codepoint assignment in the table above -- shouldn't
    // happen given that table is meant to cover every real asset GlyphAssetName()/
    // DpadGlyphAssetName() can return, but fail closed rather than substitute
    // garbage if it ever does.
    return false;
}

// Issue #48's overlay-icon path: key-name string -> real asset name directly (no
// codepoint indirection needed, since this draws the PNG as its own textured quad
// rather than injecting a codepoint into the game's own font). Returns false if no
// mapping/asset exists, same fail-closed behavior as TryGetGlyphCodepointForKeyName.
bool TryGetGlyphAssetNameForKeyName(const char* keyName, char* outAssetName, size_t outSize)
{
    const char* assetName = ResolveGlyphAssetNameForKeyName(keyName);
    if (assetName[0] == '\0') return false;
    strncpy_s(outAssetName, outSize, assetName, _TRUNCATE);
    return true;
}

// Same as TryGetGlyphAssetNameForKeyName above, but resolves through the SEPARATE
// menu-hint table (ResolveMenuGlyphAssetNameForKeyName) -- see that function's own
// comment for why menu UI hints (fonts/smallFont) can't share the gameplay table.
bool TryGetMenuGlyphAssetNameForKeyName(const char* keyName, char* outAssetName, size_t outSize)
{
    const char* assetName = ResolveMenuGlyphAssetNameForKeyName(keyName);
    if (assetName[0] == '\0') return false;
    strncpy_s(outAssetName, outSize, assetName, _TRUNCATE);
    return true;
}

// Logs the resolved bind name + resolved output text after the real trampoline has
// already run. Deliberately tolerant of EAX/ECX/[esp+8] turning out not to be what
// static analysis expects -- this project's own research flagged real uncertainty
// about ECX's exact semantic identity, so every dereference here is validated and
// wrapped in SEH before use, never assumed safe. extern "C" so the naked shim's plain
// `call BindResolverLogAfterCall` resolves to an unmangled symbol, same treatment
// InjectAllControllerInput already gets for the same reason.
extern "C" void __cdecl BindResolverLogAfterCall()
{
    // See the big comment above kMenuBindKeyCaptureCallerRetAddr -- this one real
    // caller (FUN_00622970, key-rebind-capture UI) pushes a genuinely different
    // stack-arg shape (buffer at [esp+4], size at [esp+8]) than the 3 hint-
    // resolution callers this function is actually meant to observe/substitute for --
    // there's no real hint text here to substitute either. Checked and returned FIRST,
    // ahead of both config toggles below, so this is skipped regardless of either
    // flag's state.
    if (g_bindResolverRealRetAddr == kMenuBindKeyCaptureCallerRetAddr) {
        if (g_modConfig.bindResolverHookLogging && !g_loggedMenuBindKeyCaptureCallerOnce) {
            g_loggedMenuBindKeyCaptureCallerOnce = true;
            LogFromController("[bind-resolver-diag] caller ret=0x006229AC is FUN_00622970 "
                "(key-rebind-capture UI, confirmed via disassembly -- pushes (bufPtr,bufSize,flag) "
                "at [esp+4,+8,+0xc], not (unused,buf,flag) like the 3 hint-resolution callers) -- "
                "not logging as hint text, see known_issues.md issue #35");
        }
        return;
    }

    // Read the resolved text ONCE -- both the substitution step and the logging step
    // below need it, and neither of their two independent toggles gates this read
    // (either feature alone might need it regardless of the other's state).
    char textBuf[128] = {};
    bool textReadOk = false;
    if (LooksLikeValidPointer(g_bindResolverBufPtr)) {
        const char* resolved = reinterpret_cast<const char*>(g_bindResolverBufPtr);
        __try {
            size_t i = 0;
            for (; i < sizeof(textBuf) - 1; ++i) {
                char c = resolved[i];
                textBuf[i] = c;
                if (c == '\0') break;
            }
            textBuf[sizeof(textBuf) - 1] = '\0';
            textReadOk = true;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            sprintf_s(textBuf, "<faulted reading 0x%08X>", static_cast<unsigned>(g_bindResolverBufPtr));
        }
    } else {
        sprintf_s(textBuf, "<buffer ptr 0x%08X not plausible>", static_cast<unsigned>(g_bindResolverBufPtr));
    }

    // ---- Glyph substitution (task #6/#35, 2026-07-21) -- gated ONLY by its own
    // config flag, independent of the logging toggle below. Runs on EVERY call, not
    // gated by the text-changed check further down, since the real hint is re-
    // resolved and re-drawn every frame it's on screen and needs the substitution
    // every time, not just on the frames this function happens to log. See the big
    // comment above the mapping tables (right before this function) and
    // g_modConfig.bindResolverGlyphSubstitution's own comment in mod_config.h for why
    // this is off by default.
    bool substituted = false;
    unsigned char substitutedCodepoint = 0;
    if (g_modConfig.bindResolverGlyphSubstitution && textReadOk && Controller_IsConnected() &&
        strlen(textBuf) >= 1 && TryGetGlyphCodepointForKeyName(textBuf, substitutedCodepoint))
    {
        // Safety invariant: only ever write 2 bytes (codepoint + null terminator),
        // gated above on the real resolved text being at least 1 character -- true
        // for every real single-key resolution (a real key name is never an empty
        // string) -- guaranteeing this can never exceed whatever the real
        // trampoline's own just-completed write already used in this exact buffer,
        // without needing to know that buffer's actual allocated size. Combo binds
        // ("%s KEY_OR %s") are naturally excluded since the lookup only matches
        // single, exact key names.
        __try {
            char* dest = reinterpret_cast<char*>(g_bindResolverBufPtr);
            dest[0] = static_cast<char>(substitutedCodepoint);
            dest[1] = '\0';
            substituted = true;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            // Buffer read fine but the write faulted -- leave the real text in place
            // (already unmodified from the trampoline's own write) rather than risk
            // a partial/garbage write.
        }
    }

    // Dedup gate for LOGGING only (covers both the substitution note and the regular
    // diagnostic line below) -- computed once, against the ORIGINAL resolved text
    // captured above (before any substitution), so this tracker's behavior doesn't
    // depend on whether substitution happens to be enabled. Updated regardless of
    // the logging toggle immediately below, so re-enabling logging later doesn't
    // re-log a hint that's already been sitting on screen unchanged.
    bool textChanged = strcmp(textBuf, g_bindResolverLastLoggedText) != 0;
    if (textChanged) {
        strncpy_s(g_bindResolverLastLoggedText, textBuf, sizeof(g_bindResolverLastLoggedText) - 1);
    }

    if (!g_modConfig.bindResolverHookLogging) return;
    if (!textChanged) return;

    if (substituted) {
        char subBuf[192];
        sprintf_s(subBuf, "[bind-resolver-glyph] substituted \"%s\" -> codepoint 0x%02X (glyphStyle=%s)",
            textBuf, static_cast<unsigned>(substitutedCodepoint), GlyphStyleLogName(g_modConfig.glyphStyle));
        LogFromController(subBuf);
    }

    char ctxBuf[160];
    if (LooksLikeValidPointer(g_bindResolverCtxEcx)) {
        const char* asStr = reinterpret_cast<const char*>(g_bindResolverCtxEcx);
        __try {
            char nameBuf[64];
            size_t i = 0;
            bool stringShaped = true;
            for (; i < sizeof(nameBuf) - 1; ++i) {
                char c = asStr[i];
                if (c == '\0') break;
                if (c < 0x20 || c >= 0x7f) { stringShaped = false; break; }
                nameBuf[i] = c;
            }
            nameBuf[i] = '\0';
            if (stringShaped) {
                sprintf_s(ctxBuf, "EAX(ctx)=0x%08X ECX(bindCtx)=0x%08X as-string=\"%s\"",
                    static_cast<unsigned>(g_bindResolverCtxEax), static_cast<unsigned>(g_bindResolverCtxEcx), nameBuf);
            } else {
                sprintf_s(ctxBuf, "EAX(ctx)=0x%08X ECX(bindCtx)=0x%08X (not string-shaped)",
                    static_cast<unsigned>(g_bindResolverCtxEax), static_cast<unsigned>(g_bindResolverCtxEcx));
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            sprintf_s(ctxBuf, "EAX(ctx)=0x%08X ECX(bindCtx)=0x%08X (deref faulted -- not a valid string ptr)",
                static_cast<unsigned>(g_bindResolverCtxEax), static_cast<unsigned>(g_bindResolverCtxEcx));
        }
    } else {
        sprintf_s(ctxBuf, "EAX(ctx)=0x%08X ECX(bindCtx)=0x%08X (not a plausible pointer)",
            static_cast<unsigned>(g_bindResolverCtxEax), static_cast<unsigned>(g_bindResolverCtxEcx));
    }

    // retAddr included from 2026-07-21 (known_issues.md issue #35, "residual miss"
    // investigation): a real playtest showed this exact garbage shape (ECX=0,
    // buffer=0x100) getting logged even after the FUN_00622970 return-address skip
    // was added, but the skip's own one-time note never appeared anywhere in that
    // log -- meaning the retaddr comparison evaluated false for that one call. A
    // fresh, from-scratch Ghidra re-verification (exhaustive xref count, full
    // FUN_00622970 disassembly) confirms exactly 4 real callers exist total, no
    // second call site inside FUN_00622970, and kMenuBindKeyCaptureCallerRetAddr
    // (0x006229AC) is exactly the correct return address for its one real call site
    // -- so the constant and the caller catalog are NOT the bug. Root cause not
    // conclusively found (see the known_issues.md entry for what was ruled out and
    // what remains suspected but unconfirmed -- possible reentrancy via a message
    // pump inside the key-rebind-capture UI's wait state). Logging the real observed
    // retAddr directly, instead of just inferring it failed to match, means the next
    // occurrence of this garbage shape will show its own real value directly, closing
    // this loop with actual data instead of another inference.
    char buf[420];
    sprintf_s(buf, "[bind-resolver-diag] %s | limitTo1=%u retAddr=0x%08X resolvedText=\"%s\"",
        ctxBuf, static_cast<unsigned>(g_bindResolverLimitFlag),
        static_cast<unsigned>(g_bindResolverRealRetAddr), textBuf);
    LogFromController(buf);
}

__declspec(naked) void Hook_0061f6f0()
{
    __asm {
        // Stash everything meaningful before it can be clobbered -- register args
        // first, then the two stack args this pass cares about ([esp+8]/[esp+0xc]).
        // [esp+4] is confirmed dead by the research above; not read here either.
        mov dword ptr [g_bindResolverCtxEax], eax
        mov dword ptr [g_bindResolverCtxEcx], ecx
        mov eax, dword ptr [esp+8]
        mov dword ptr [g_bindResolverBufPtr], eax
        mov eax, dword ptr [esp+0xc]
        mov dword ptr [g_bindResolverLimitFlag], eax

        // Save the real return address, then repoint that same stack slot at our own
        // afterCall label -- see the big comment above for why this must be an
        // in-place overwrite (no push) rather than a plain `call`.
        mov eax, dword ptr [esp]
        mov dword ptr [g_bindResolverRealRetAddr], eax
        mov dword ptr [esp], offset afterCall

        // Restore EAX/ECX exactly as the real caller set them, then tail-jump (NOT
        // call) into the trampoline so it sees the byte-for-byte original frame.
        mov eax, dword ptr [g_bindResolverCtxEax]
        mov ecx, dword ptr [g_bindResolverCtxEcx]
        jmp dword ptr [g_orig_0061f6f0]

    afterCall:
        // The trampoline's own `ret` landed us here with esp already back to what the
        // real caller would see post-return. Preserve every register across the log
        // call since this function's real return-value convention (if any) in EAX is
        // not confirmed, then resume the real caller directly -- there is no
        // return-address slot left on the stack to `ret` through a second time.
        pushad
        call BindResolverLogAfterCall
        popad
        jmp dword ptr [g_bindResolverRealRetAddr]
    }
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

    // Bind-resolver text hook (task #6/#35, 2026-07-21) -- LOG-ONLY first pass, see the
    // big comment above Hook_0061f6f0's definition. Installed unconditionally (this is
    // a permanent hook, not a manually-triggered debug test), but its LOGGING is gated
    // by g_modConfig.bindResolverHookLogging (default on) so it can be silenced from
    // the INI without a recompile -- the hook forwards to the real trampoline
    // completely unmodified either way, so leaving it installed carries no behavior
    // risk even with logging off.
    MH_STATUS s8 = MH_CreateHook(reinterpret_cast<LPVOID>(0x0061f6f0), &Hook_0061f6f0, &g_orig_0061f6f0);
    sprintf_s(buf, "[hooks] MH_CreateHook(0061f6f0 bind-resolver) = %d", static_cast<int>(s8));
    LogFromController(buf);
    if (s8 == MH_OK) {
        MH_STATUS e8 = MH_EnableHook(reinterpret_cast<LPVOID>(0x0061f6f0));
        sprintf_s(buf, "[hooks] MH_EnableHook(0061f6f0 bind-resolver) = %d", static_cast<int>(e8));
        LogFromController(buf);
    }

    // Menu highlighted-item tracking (2026-08-01, issue #48 menu-glyph work) -- see
    // the big comment above Hook_004dfd30's own definition. First time this project
    // has hooked FUN_004dfd30 itself (previously only ever called INTO via
    // ForwardKeyToMenu) -- purely a read-only argument cache, forwards everything
    // unmodified.
    MH_STATUS sFocus = MH_CreateHook(reinterpret_cast<LPVOID>(0x004dfd30), &Hook_004dfd30, &g_orig_004dfd30);
    sprintf_s(buf, "[hooks] MH_CreateHook(004dfd30 menu-focus-track) = %d", static_cast<int>(sFocus));
    LogFromController(buf);
    if (sFocus == MH_OK) {
        MH_STATUS eFocus = MH_EnableHook(reinterpret_cast<LPVOID>(0x004dfd30));
        sprintf_s(buf, "[hooks] MH_EnableHook(004dfd30 menu-focus-track) = %d", static_cast<int>(eFocus));
        LogFromController(buf);
    }

    // OpenMenuByName hook (2026-08-01, issue #48 menu-glyph work) -- see the big
    // comment above Hook_00544a50/g_inSpecOpsFlow. First time this project has
    // hooked FUN_00544a50 itself (previously only referenced/documented via
    // re_notes/iw5sp.md, never intercepted) -- purely reads the opened menu's own
    // name string and forwards everything unmodified.
    MH_STATUS sOpenMenu = MH_CreateHook(reinterpret_cast<LPVOID>(0x00544a50), &Hook_00544a50, &g_orig_00544a50);
    sprintf_s(buf, "[hooks] MH_CreateHook(00544a50 open-menu-track) = %d", static_cast<int>(sOpenMenu));
    LogFromController(buf);
    if (sOpenMenu == MH_OK) {
        MH_STATUS eOpenMenu = MH_EnableHook(reinterpret_cast<LPVOID>(0x00544a50));
        sprintf_s(buf, "[hooks] MH_EnableHook(00544a50 open-menu-track) = %d", static_cast<int>(eOpenMenu));
        LogFromController(buf);
    }

    // Local-var lookup trace hook (2026-08-01, A-glyph investigation continuation) --
    // DISABLED, live-reported to prevent the game from launching at all (same failure
    // signature as the FUN_00486990 revert below -- see that comment for the general
    // lesson). FUN_00552e70's own decompile looked clean (no unaff_ESI/EDI register
    // weirdness, simple two-arg shape), so a clean-looking signature is NOT sufficient
    // evidence a function is safe to intercept -- call frequency/timing/calling-
    // convention mismatches invisible to static analysis can still crash the game on
    // hook install. Reverted immediately rather than investigated further with a game
    // that won't even boot; do not re-enable without confirming (via a safer method,
    // e.g. a live debugger breakpoint instead of a permanently-installed hook) that
    // this specific function is actually safe to trampoline.
    // MH_STATUS sLocalVar = MH_CreateHook(reinterpret_cast<LPVOID>(0x00552e70), &Hook_00552e70, &g_orig_00552e70);
    // sprintf_s(buf, "[hooks] MH_CreateHook(00552e70 localvar-lookup-track) = %d", static_cast<int>(sLocalVar));
    // LogFromController(buf);
    // if (sLocalVar == MH_OK) {
    //     MH_STATUS eLocalVar = MH_EnableHook(reinterpret_cast<LPVOID>(0x00552e70));
    //     sprintf_s(buf, "[hooks] MH_EnableHook(00552e70 localvar-lookup-track) = %d", static_cast<int>(eLocalVar));
    //     LogFromController(buf);
    // }

    // localvarstring getter hook (2026-08-01, issue #51 follow-up) -- see the big
    // comment above Hook_00613ac0's own definition. Unlike the disabled attempt
    // above, this targets a genuinely single-caller, opcode-specific function
    // (confirmed via direct decompile + xref, not inference) and uses a naked-asm
    // trampoline that preserves EAX/ECX/ESI exactly, the same proven-safe pattern
    // as Hook_0057de60/Hook_0061f6f0 elsewhere in this file -- not a plain
    // MinHook C-signature detour, which is what made the FUN_00552e70 attempt
    // unsafe. Still a genuinely new hook on first live test; watch the first
    // launch closely regardless.
    MH_STATUS sLocalVarStr = MH_CreateHook(reinterpret_cast<LPVOID>(0x00613ac0), &Hook_00613ac0, &g_orig_00613ac0);
    sprintf_s(buf, "[hooks] MH_CreateHook(00613ac0 localvarstring-track) = %d", static_cast<int>(sLocalVarStr));
    LogFromController(buf);
    if (sLocalVarStr == MH_OK) {
        MH_STATUS eLocalVarStr = MH_EnableHook(reinterpret_cast<LPVOID>(0x00613ac0));
        sprintf_s(buf, "[hooks] MH_EnableHook(00613ac0 localvarstring-track) = %d", static_cast<int>(eLocalVarStr));
        LogFromController(buf);
    }

    // getfocuseditemname() hook (2026-08-01, issue #50 follow-up) -- see the big
    // comment above Hook_00616230's own definition. Single confirmed caller, zero
    // implicit input registers (only ESI for output) -- an even simpler shape than
    // Hook_00613ac0 above. Diagnostic-only for now.
    MH_STATUS sFocusedItem = MH_CreateHook(reinterpret_cast<LPVOID>(0x00616230), &Hook_00616230, &g_orig_00616230);
    sprintf_s(buf, "[hooks] MH_CreateHook(00616230 focused-item-track) = %d", static_cast<int>(sFocusedItem));
    LogFromController(buf);
    if (sFocusedItem == MH_OK) {
        MH_STATUS eFocusedItem = MH_EnableHook(reinterpret_cast<LPVOID>(0x00616230));
        sprintf_s(buf, "[hooks] MH_EnableHook(00616230 focused-item-track) = %d", static_cast<int>(eFocusedItem));
        LogFromController(buf);
    }

    // localvarint() hook (2026-08-01, issue #51 follow-up) -- see the big comment
    // above Hook_00613b70's own definition. Same confirmed-safe EAX/ECX/ESI shape
    // as Hook_00613ac0. Diagnostic-only for now.
    MH_STATUS sLocalVarInt = MH_CreateHook(reinterpret_cast<LPVOID>(0x00613b70), &Hook_00613b70, &g_orig_00613b70);
    sprintf_s(buf, "[hooks] MH_CreateHook(00613b70 localvarint-track) = %d", static_cast<int>(sLocalVarInt));
    LogFromController(buf);
    if (sLocalVarInt == MH_OK) {
        MH_STATUS eLocalVarInt = MH_EnableHook(reinterpret_cast<LPVOID>(0x00613b70));
        sprintf_s(buf, "[hooks] MH_EnableHook(00613b70 localvarint-track) = %d", static_cast<int>(eLocalVarInt));
        LogFromController(buf);
    }

    // Cursor-draw suppression hook (2026-08-01, user-requested custom cursor) -- see
    // the big comment above Hook_004d48f0's own definition. Plain C-signature detour
    // (confirmed __cdecl via both decompile and disassembly), gated by return
    // address so only the one cursor-specific call site among 31 real callers is
    // affected.
    MH_STATUS sCursorDraw = MH_CreateHook(reinterpret_cast<LPVOID>(0x004d48f0), &Hook_004d48f0, &g_orig_004d48f0);
    sprintf_s(buf, "[hooks] MH_CreateHook(004d48f0 cursor-suppress) = %d", static_cast<int>(sCursorDraw));
    LogFromController(buf);
    if (sCursorDraw == MH_OK) {
        MH_STATUS eCursorDraw = MH_EnableHook(reinterpret_cast<LPVOID>(0x004d48f0));
        sprintf_s(buf, "[hooks] MH_EnableHook(004d48f0 cursor-suppress) = %d", static_cast<int>(eCursorDraw));
        LogFromController(buf);
    }

    // Registry-search caller trace hook (2026-08-01) -- DISABLED, live-reported to
    // prevent the game from launching at all. FUN_00486990 is called extremely
    // early/frequently (14+ known callers, likely including boot-time menu
    // registration -- see FUN_0050a350/RegisterMenuList) -- hooking it was unsafe in
    // a way static analysis alone didn't catch. Reverted immediately rather than
    // investigated further; do not re-enable without a much more cautious approach
    // (e.g. gating the hook body itself behind a "safe to run yet" flag, or finding
    // a lower-traffic call site closer to the actual menu-script action instead).
    // MH_STATUS sRegSearch = MH_CreateHook(reinterpret_cast<LPVOID>(0x00486990), &Hook_00486990, &g_orig_00486990);
    // sprintf_s(buf, "[hooks] MH_CreateHook(00486990 registry-search-track) = %d", static_cast<int>(sRegSearch));
    // LogFromController(buf);
    // if (sRegSearch == MH_OK) {
    //     MH_STATUS eRegSearch = MH_EnableHook(reinterpret_cast<LPVOID>(0x00486990));
    //     sprintf_s(buf, "[hooks] MH_EnableHook(00486990 registry-search-track) = %d", static_cast<int>(eRegSearch));
    //     LogFromController(buf);
    // }

    // task #23 follow-up (2026-07-20) -- boot-thunk resolution diagnostic, see the
    // big comment above Hook_FUN_00679680's definition for the full rationale/
    // correction. Wired live deliberately (like InjectFontStructDebugTest below):
    // this hooks a real, ordinary function (confirmed safe via its plain
    // disassembled prologue/epilogue, no thunk involved), calls the original
    // completely unmodified, and only reads/logs afterward -- zero mutation, zero
    // interception of the actual zone-loading path that crashed the game twice
    // before (issues #22/#24/#30). Safe by the same standard that governs every
    // other "read-only diagnostic wired live" call in this file.
    MH_STATUS s3 = MH_CreateHook(reinterpret_cast<LPVOID>(0x00679680), &Hook_FUN_00679680, reinterpret_cast<LPVOID*>(&g_origFUN_00679680));
    sprintf_s(buf, "[hooks] MH_CreateHook(00679680 boot-thunk-diag) = %d", static_cast<int>(s3));
    LogFromController(buf);
    if (s3 == MH_OK) {
        MH_STATUS e3 = MH_EnableHook(reinterpret_cast<LPVOID>(0x00679680));
        sprintf_s(buf, "[hooks] MH_EnableHook(00679680 boot-thunk-diag) = %d", static_cast<int>(e3));
        LogFromController(buf);
    }

    // task #6/#23 follow-up (2026-07-21) -- level-load-safe glyph-font-extension
    // trigger, see the big comment above Hook_FUN_0053cbc0's definition for the full
    // rationale. Read-only diagnostic wired live (logs every call + call count +
    // map name); the actual InstallGlyphFontExtension() splice call inside the hook
    // stays commented out/disabled by default until that diagnostic is confirmed
    // live. Hooks a real, ordinary function (confirmed via fresh disassembly this
    // session -- plain prologue/epilogue, no thunk involved), calls the original
    // completely unmodified first.
    MH_STATUS s9 = MH_CreateHook(reinterpret_cast<LPVOID>(0x0053cbc0), &Hook_FUN_0053cbc0, reinterpret_cast<LPVOID*>(&g_origFUN_0053cbc0));
    sprintf_s(buf, "[hooks] MH_CreateHook(0053cbc0 level-load-zone-hook) = %d", static_cast<int>(s9));
    LogFromController(buf);
    if (s9 == MH_OK) {
        MH_STATUS e9 = MH_EnableHook(reinterpret_cast<LPVOID>(0x0053cbc0));
        sprintf_s(buf, "[hooks] MH_EnableHook(0053cbc0 level-load-zone-hook) = %d", static_cast<int>(e9));
        LogFromController(buf);
    }

    // 2026-08-17: only installed when captureRuntimeMenuAssets is actually on --
    // unlike the read-only diagnostics elsewhere in this file (which forward
    // unmodified regardless of their own toggle and so cost nothing meaningful to
    // leave hooked), FindOrLoadAsset is a genuinely hot, widely-shared function
    // (59 distinct callers across the whole binary per re_notes/iw5sp.md) -- no
    // reason to add even a trivial extra call-through for every normal player when
    // this dev-only capture feature is off, so the hook itself isn't installed at
    // all in that case, not just internally gated.
    if (g_modConfig.captureRuntimeMenuAssets) {
        MH_STATUS sCap = MH_CreateHook(reinterpret_cast<LPVOID>(0x004ff000), &Hook_FindOrLoadAsset,
                                        reinterpret_cast<LPVOID*>(&g_origFindOrLoadAsset));
        sprintf_s(buf, "[hooks] MH_CreateHook(004ff000 asset-capture) = %d", static_cast<int>(sCap));
        LogFromController(buf);
        if (sCap == MH_OK) {
            MH_STATUS eCap = MH_EnableHook(reinterpret_cast<LPVOID>(0x004ff000));
            sprintf_s(buf, "[hooks] MH_EnableHook(004ff000 asset-capture) = %d", static_cast<int>(eCap));
            LogFromController(buf);
        }
    }

    // task #6/#34 follow-up (2026-07-21) -- live HUD-text font identification, see the
    // big comment above Hook_DrawGlyphText's definition. Same "read-only, forwards
    // unmodified, wired live because it can never mutate anything" standard as the
    // boot-thunk diagnostic just above.
    MH_STATUS s10 = MH_CreateHook(reinterpret_cast<LPVOID>(kDrawGlyphTextAddr), &Hook_DrawGlyphText, reinterpret_cast<LPVOID*>(&g_origDrawGlyphText));
    sprintf_s(buf, "[hooks] MH_CreateHook(00690c80 hud-font-id) = %d", static_cast<int>(s10));
    LogFromController(buf);
    if (s10 == MH_OK) {
        MH_STATUS e10 = MH_EnableHook(reinterpret_cast<LPVOID>(kDrawGlyphTextAddr));
        sprintf_s(buf, "[hooks] MH_EnableHook(00690c80 hud-font-id) = %d", static_cast<int>(e10));
        LogFromController(buf);
    }

    // Sprint (L3) no longer hooks FUN_00644ed0/FUN_00643ce0 to force the raw pm_flags
    // bit -- superseded 2026-07-19 by driving the real +sprint kbutton_t (0xA98CCC)
    // directly via CallKbuttonDown/CallKbuttonUp from InjectControllerSprint(), same
    // technique as ADS/Reload/Fire. See the big comment block above InjectControllerSprint
    // for the full disassembly trail. Both hooks and their trampolines were removed
    // entirely (not just disabled) now that nothing calls them.

    // TEMPORARILY DISABLED (2026-07-19) -- isolating the Hold Breath stuck-forever
    // bug. Confirmed via live testing (DLL removed = fine; DLL installed + PURE
    // keyboard/mouse, zero controller touch = still stuck) that something in this
    // DLL corrupts the real mechanism unconditionally, regardless of controller
    // input -- ruling out every controller-gated Inject* function (all of them
    // early-return with no meaningful controller state). These two hooks are the
    // only OTHER things installed that run every single frame regardless of input
    // device. Disabling both together to test the combined hypothesis before
    // bisecting further -- re-enable once Hold Breath is confirmed unaffected by
    // these, or keep disabled and investigate further if it IS one of these.
    // MH_STATUS s5 = MH_CreateHook(reinterpret_cast<LPVOID>(kControlsLinkToAddr),
    //     &Hook_ControlsLinkTo, reinterpret_cast<LPVOID*>(&g_origControlsLinkTo));
    // sprintf_s(buf, "[hooks] MH_CreateHook(controlslinkto @ 005d7f20) = %d", static_cast<int>(s5));
    // LogFromController(buf);
    // if (s5 == MH_OK) {
    //     MH_STATUS e5 = MH_EnableHook(reinterpret_cast<LPVOID>(kControlsLinkToAddr));
    //     sprintf_s(buf, "[hooks] MH_EnableHook(controlslinkto) = %d", static_cast<int>(e5));
    //     LogFromController(buf);
    // }

    // MH_STATUS s6 = MH_CreateHook(reinterpret_cast<LPVOID>(kMissileGuidanceDispatchAddr),
    //     &Hook_MissileGuidanceDispatch, reinterpret_cast<LPVOID*>(&g_origMissileGuidanceDispatch));
    // sprintf_s(buf, "[hooks] MH_CreateHook(missile-guidance-dispatch @ 004554d0) = %d", static_cast<int>(s6));
    // LogFromController(buf);
    // if (s6 == MH_OK) {
    //     MH_STATUS e6 = MH_EnableHook(reinterpret_cast<LPVOID>(kMissileGuidanceDispatchAddr));
    //     sprintf_s(buf, "[hooks] MH_EnableHook(missile-guidance-dispatch) = %d", static_cast<int>(e6));
    //     LogFromController(buf);
    // }

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

    // RE-ENABLED (2026-08-03) -- see re_notes/known_issues.md issue #24 for the full
    // history. The original 2026-07-18 crash was caused by hooking FUN_004895b0/
    // FUN_0044cdb0 directly (generic multi-purpose native notify dispatchers, not
    // weapon-fire/damage-specific) -- some other real caller almost certainly passed
    // a genuinely different real argument shape than this hook's fixed signature
    // assumed. Reimplemented from scratch this session: FIRE now hooks FUN_0045e320
    // (a single-purpose function with exactly one real caller, its calling
    // convention re-verified via fresh raw disassembly of that one call site, found
    // at runtime via a byte-pattern scan, never a hardcoded address). DAMAGE is NOT
    // a hook at all -- the documented "safer" replacement candidate for the damage
    // side (FUN_0045f770) was re-checked the same rigorous way and found to have the
    // exact same inconsistent-argument-count problem across its 14 real callers, so
    // it is deliberately not hooked; damage is instead detected via a per-frame poll
    // of the local player's own real health field. See rumble.h/.cpp for the full
    // detail on both mechanisms.
    Rumble_Install(); // task #17 -- its own module, see rumble.h/.cpp

    // Options replacement screen (issue #66 task #11/#21) -- full render-suppression
    // hooks for the real Options menu, its own module, see
    // options_render_suppress.h/.cpp. DISABLED 2026-08-04, SAME DAY AS ADDED:
    // live-reported "doesnt even open now" -- the game fails to launch at all with
    // these two hooks installed, the exact same failure mode this project already
    // documented once for the 2026-08-01 registry-search hook (0x00486990), despite
    // this pair looking structurally safe by the same reasoning that hook wasn't
    // (confirmed-single-caller, once-per-frame, not boot-time/high-frequency --
    // that reasoning was evidently insufficient here too). Reverted immediately per
    // this project's own established response to this exact symptom, not
    // investigated further in place -- see re_notes/known_issues.md issue #66 for
    // the standing note on what to check before ever re-enabling this.
    // InstallOptionsRenderSuppressionHooks();
}
