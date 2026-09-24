// MW32011NCP, 2026-09-24: real per-object (Dynamic Object / DObj) motion-vector
// data capture -- see re_notes/x64_migration/vulkan_dlss_pipeline_research.md
// section 2.6 (ROUND 1-4, 2026-09-24) for the full RE trail this implements.
// SP-only (gated on GetDetectedGameExecutable(), same convention every other
// native-signature-dependent x64 feature uses -- these signatures were found
// and verified against iw5sp.exe only; iw5mp.exe is a separately-compiled
// binary and no address/signature carries over, CLAUDE.md S10.8).
//
// Real, RE-confirmed field layout (vulkan_dlss_pipeline_research.md section
// 2.6, ROUND 1/2/3 -- decompiled from FUN_1401d62c0/FUN_1401d5840 (the real
// per-DObj registration functions), FUN_1401a5830 ("add scene ent"), and
// cross-verified against the engine's own literal debug string
// "R_AddDObjSurfacesCamera"):
//   - DAT_141c22fac: a dword counter -- the next free slot index into the
//     main DObj pool (cap 0x200/512, enforced by the engine's own bounds
//     check). A second, smaller reserved pool (cap 8, slots 0x200..0x207,
//     viewmodel-class DObjs specifically, FUN_1401d5840's own separate
//     branch) shares the same position/rotation arrays at that index
//     offset -- deliberately NOT captured here (see "Scope" below).
//   - DAT_141c22fb8 (position array base, stride 0x90 bytes/slot): a real
//     float[3] world-space position, written verbatim by the real
//     registration functions from their own engine-computed per-frame
//     transform. This project reads it directly -- no function hooking
//     needed, per the research doc's own "real, final, simplest design"
//     conclusion.
//   - DAT_141c22fb8+0xC (rotation-row block, same stride): 9 floats, a
//     real row-major 3x3 rotation matrix, built by the engine's own
//     Euler-angle expansion (FUN_1402bd380, genuine sin/cos calls) and
//     written to this exact offset every time a DObj registers.
//   - DAT_141c35438 (active-flag array, stride 1 byte/slot): '\x01' when
//     the slot's data is valid/current this frame -- the real signal this
//     capture uses both to skip inactive slots and to detect a slot's
//     "just became active" transition (no valid previous-frame history
//     yet, matches sl::Constants::motionVectorsInvalidValue's own
//     documented meaning for an object with no prior pose).
//
// Scope, Round 1 (this file, first landing): resolve the real addresses,
// read and retain a real current/previous transform snapshot for the MAIN
// DObj pool only (slots 0..0x1FF) every frame, log real, live diagnostics
// for verification. The small 8-slot reserved/viewmodel pool is deliberately
// NOT captured this round -- it is NOT gated by the same DAT_141c35438
// active-flag array (FUN_1401d5840's own reserved-pool branch skips the
// active-registration bookkeeping the main pool's branch performs), so
// reading it the same way would be reading meaning that isn't actually
// there; and first-person weapon-viewmodel geometry is screen-locked to the
// camera rather than an independently-moving world object anyway, a low-
// value target for real per-object motion vectors specifically. Does NOT
// yet feed this data into the actual DLSS motion-vectors D3D9 texture
// (streamline_resources_x64.cpp's own TagMotionVectorsResourceForFrame) --
// that needs a real velocity computation (screen-space reprojection of the
// position/rotation delta through the current+previous camera/projection
// matrices this project's own streamline_camera_x64.cpp already tracks) and
// is the next real step once this round's own capture is live-verified
// correct.

#include <windows.h>
#include <cstdio>
#include <cstring>
#include <cmath>
#include "signature_scan.h"
#include "game_exe_detect.h"

extern void LogFromController(const char* msg); // dllmain.cpp

namespace {

constexpr int kMaxDObjSlotsX64 = 0x200; // main pool only -- see this file's
    // own top comment for why the reserved/viewmodel pool is out of scope.
constexpr size_t kDObjRecordStrideX64 = 0x90;
constexpr size_t kDObjRotationOffsetX64 = 0xC;

struct DObjTransformX64 {
    float pos[3] = {};
    float rot[9] = {};
    bool valid = false;
};

uintptr_t g_dobjCounterAddrX64 = 0;
uintptr_t g_dobjPositionArrayX64 = 0;
uintptr_t g_dobjActiveFlagArrayX64 = 0;
bool g_dobjGlobalsResolveTriedX64 = false;
bool g_dobjGlobalsResolvedX64 = false;

DObjTransformX64 g_dobjCurrentX64[kMaxDObjSlotsX64];
DObjTransformX64 g_dobjPreviousX64[kMaxDObjSlotsX64];

// Real, verified signature (vulkan_dlss_pipeline_research.md section 2.6,
// ROUND 3) built from the actual disassembled bytes of FUN_1401d62c0's own
// real allocation block -- resolves both DAT_141c22fac (the next-free-slot
// counter) and DAT_141c22fb8 (the position array base; rotation lives at
// +0xC, same stride) from ONE match. The two remaining wildcarded fields
// (offsets 39/17 into the pattern) are, respectively, a second write-back
// reference to the same counter and a real JNC rel32 branch target -- both
// genuine PC-relative addresses, wildcarded per this project's own standing
// convention, not needed for resolution.
bool ResolveObjectMotionGlobalsX64()
{
    SigScan::Result r1 = SigScan::FindPatternInMainModule(
        "48 89 74 24 58 8B 35 ?? ?? ?? ?? 81 FE 00 02 00 00 0F 83 ?? ?? ?? ?? "
        "83 E1 7F 48 89 5C 24 50 48 89 7C 24 60 8D 46 01 89 05 ?? ?? ?? ?? "
        "48 8D 3C F6 48 8D 05 ?? ?? ?? ?? 41 C1 E1 0C 48 C1 E7 04 48 03 F8");
    if (r1.found) {
        // "8B 35 ?? ?? ?? ??" starts 5 bytes into the match, 6 bytes long.
        g_dobjCounterAddrX64 = SigScan::ResolveRipRelative(r1.address + 5, 6);
        // "48 8D 05 ?? ?? ?? ??" starts 49 bytes into the match, 7 bytes long.
        g_dobjPositionArrayX64 = SigScan::ResolveRipRelative(r1.address + 49, 7);
    }

    // Real, verified signature built from FUN_1401a5830 ("add scene ent")'s
    // own per-slot active-flag-array walk -- resolves the shared base
    // constant (0x141bb7000) the engine builds several unrelated array
    // addresses off of. DAT_141c35438 (the active-flag array) is that base
    // plus a real, FIXED 0x7e438 literal already baked into this exact
    // instruction sequence's own bytes (a small stack-frame-shape-adjacent
    // displacement, not itself an address needing a separate scan -- same
    // "fixed immediates don't need wildcarding" convention
    // DumpSigBytes.java's own header comment documents).
    SigScan::Result r2 = SigScan::FindPatternInMainModule(
        "4C 8D 05 ?? ?? ?? ?? 41 8B 4D 10 3B D1 0F 83 ?? ?? ?? ?? 2B CA "
        "48 8D 3C D2 48 C1 E7 04 49 8D B0 38 E4 07 00 48 8D 05 ?? ?? ?? ?? "
        "44 8B F9 48 03 F8 48 03 F2 45 33 ED 0F 1F 40 00 80 3E 01 0F 85 ?? ?? ?? ??");
    if (r2.found) {
        uintptr_t base = SigScan::ResolveRipRelative(r2.address, 7);
        if (base) g_dobjActiveFlagArrayX64 = base + 0x7e438;
    }

    char buf[320];
    sprintf_s(buf, "[x64-dobj-motion] signature resolve: counter=%s pos=%s active=%s "
        "(counter=0x%llX pos=0x%llX active=0x%llX)",
        g_dobjCounterAddrX64 ? "OK" : "FAILED", g_dobjPositionArrayX64 ? "OK" : "FAILED",
        g_dobjActiveFlagArrayX64 ? "OK" : "FAILED",
        static_cast<unsigned long long>(g_dobjCounterAddrX64),
        static_cast<unsigned long long>(g_dobjPositionArrayX64),
        static_cast<unsigned long long>(g_dobjActiveFlagArrayX64));
    LogFromController(buf);

    return g_dobjCounterAddrX64 != 0 && g_dobjPositionArrayX64 != 0 &&
        g_dobjActiveFlagArrayX64 != 0;
}

} // namespace

// REAL LIVE BUG, fixed 2026-09-24: this resolve used to run lazily on the
// FIRST call to CaptureObjectMotionSnapshotX64() below -- i.e. from inside
// Hook_EndScene, already deep in the live render loop. signature_scan.cpp's
// own FindPattern is a naive O(module_size * pattern_length) linear byte
// scan (deliberately simple, since every other signature in this codebase
// resolves once at device-creation/DllMain time, before real frames start
// rendering -- see that file's own header comment) -- running TWO ~67-68
// byte patterns against the WHOLE game module from inside a live frame
// stalls that exact frame for a real, user-visible amount of time, live-
// reported as "huge fps cost" the same session this landed. Every sibling
// Streamline piece in this codebase does its own one-time signature/setup
// work at device-creation time instead (InstallDepthStencilHookX64 et al,
// called from InstallEndSceneHook, itself called once per device BEFORE the
// render loop starts spinning) -- this function is the same fix applied
// here: called once from InstallEndSceneHook's own device-creation-time
// init path, not lazily from the render loop.
void EnsureObjectMotionGlobalsResolvedX64()
{
    if (GetDetectedGameExecutable() != GameExecutable::SP) return;
    if (g_dobjGlobalsResolveTriedX64) return;
    g_dobjGlobalsResolveTriedX64 = true;
    g_dobjGlobalsResolvedX64 = ResolveObjectMotionGlobalsX64();
}

// Called once per real frame (Hook_EndScene, overlay_hud.cpp). Pure
// per-frame read -- the one-time signature resolve above must already have
// run (EnsureObjectMotionGlobalsResolvedX64, called at device-creation
// time); this function never scans anything itself.
void CaptureObjectMotionSnapshotX64()
{
    if (!g_dobjGlobalsResolvedX64) return;

    static long long s_tickCount = 0;
    ++s_tickCount;
    bool heartbeat = s_tickCount <= 5 || (s_tickCount % 5000) == 0;

    // Swap current -> previous, THEN refill current -- matches this
    // project's own "capture, don't compute" design (research doc section
    // 2.6 point 1): g_dobjPreviousX64 always holds LAST frame's real,
    // engine-written data once this function returns, ready for the next
    // frame's own diff.
    std::memcpy(g_dobjPreviousX64, g_dobjCurrentX64, sizeof(g_dobjCurrentX64));

    int activeCount = 0;
    int newlyActiveCount = 0;
    float sampleDeltaSq = -1.0f;
    uint32_t liveCounterValue = 0;

    __try {
        liveCounterValue = *reinterpret_cast<const uint32_t*>(g_dobjCounterAddrX64);

        // Scan the full main-pool range unconditionally, gated purely by the
        // real active-flag byte -- deliberately NOT bounded by
        // liveCounterValue (a real, cheap cross-check value, logged below,
        // but not trusted as a hard scan limit since this project hasn't
        // independently confirmed it resets to 0 at a known point every
        // frame vs. accumulating -- the active-flag gate is correct either
        // way, at negligible extra cost: 0x200 single-byte reads per frame).
        for (uint32_t slot = 0; slot < static_cast<uint32_t>(kMaxDObjSlotsX64); ++slot) {
            const uint8_t* activeFlag =
                reinterpret_cast<const uint8_t*>(g_dobjActiveFlagArrayX64) + slot;
            if (*activeFlag != 1) {
                g_dobjCurrentX64[slot].valid = false;
                continue;
            }
            ++activeCount;
            const uint8_t* record = reinterpret_cast<const uint8_t*>(g_dobjPositionArrayX64) +
                static_cast<size_t>(slot) * kDObjRecordStrideX64;
            DObjTransformX64& cur = g_dobjCurrentX64[slot];
            std::memcpy(cur.pos, record, sizeof(cur.pos));
            std::memcpy(cur.rot, record + kDObjRotationOffsetX64, sizeof(cur.rot));
            cur.valid = true;

            const DObjTransformX64& prev = g_dobjPreviousX64[slot];
            if (!prev.valid) {
                ++newlyActiveCount;
            } else if (sampleDeltaSq < 0.0f) {
                float dx = cur.pos[0] - prev.pos[0];
                float dy = cur.pos[1] - prev.pos[1];
                float dz = cur.pos[2] - prev.pos[2];
                sampleDeltaSq = dx * dx + dy * dy + dz * dz;
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        if (heartbeat) {
            LogFromController("[x64-dobj-motion] SEH exception reading DObj arrays -- skipped this frame");
        }
        return;
    }

    if (heartbeat) {
        char buf[280];
        sprintf_s(buf, "[x64-dobj-motion] tick=%lld active=%d newlyActive=%d "
            "sampleDelta=%.4f liveCounter=%u",
            s_tickCount, activeCount, newlyActiveCount,
            sampleDeltaSq >= 0.0f ? sqrtf(sampleDeltaSq) : -1.0f, liveCounterValue);
        LogFromController(buf);
    }
}
