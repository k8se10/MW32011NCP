// MW32011NCP, 2026-09-24: real camera-to-world matrix construction + real
// per-frame history tracking, built on top of the just-confirmed view-matrix
// RE (analog_input_hooks_x64.cpp's [x64-view-matrix-diag] -- see
// re_notes/x64_migration/vulkan_dlss_pipeline_research.md item 17 for the
// full RE trail: position @ render-state+0x1590, forward @ +0x159C, right
// @ +0x15A8, up @ +0x15B4, mathematically confirmed via orthonormality and
// a real cross(worldUp, forward) match, not guessed).
//
// This is the next real step toward DLSS/Streamline's sl::Constants,
// specifically the clipToPrevClip/prevClipToClip pair (ProgrammingGuide.md
// section 2.11.1) that Stage 1 camera-only motion vectors need
// (vulkan_dlss_pipeline_research.md section 2.5). sl_matrix_helpers.h (real,
// MIT-licensed, part of the vendored Streamline SDK) provides the exact
// reference construction and math -- adapted here, NOT reused verbatim: its
// own recalculateCameraMatrices() explicitly warns "DO NOT USE THIS IN
// ANYTHING PROPER" about its static, unkeyed previous-frame storage (no
// real per-viewport association, no reset-on-level-load handling) -- this
// file does its own, real, single-viewport-scoped equivalent instead (this
// project only ever has one active render viewport in practice, so a single
// tracked "previous frame" is correct here, not a shortcut).
//
// Matches this project's own "trivial passthrough first" convention: this
// slice builds and tracks the matrix and logs it for live verification --
// it does NOT yet call slSetConstants (needs a real, correctly-formed
// cameraViewToClip projection matrix too, which is a separate, not-yet-done
// piece -- this engine's own native projection-matrix layout is confirmed
// NONSTANDARD, see kProjectionMatrixBuildSignature's own big comment in
// analog_input_hooks_x64.cpp, so a standard D3D/Vulkan-convention
// perspective matrix will need to be built independently, not read from the
// game's own internal one).

#include <windows.h>
#include <cstdio>
#include <cmath>
#include "../third_party/streamline/include/sl.h"
#include "../third_party/streamline/include/sl_matrix_helpers.h"

extern void LogFromController(const char* msg); // dllmain.cpp

namespace {

sl::float4x4 g_prevCameraToWorldX64{};
bool g_haveStreamlinePrevFrameX64 = false;
long long g_streamlineCameraTickCountX64 = 0;

} // namespace

// Called once per real frame from Hook_ProjectionMatrixBuild
// (analog_input_hooks_x64.cpp), immediately after it reads the confirmed
// pos/fwd/right/up offsets, passing them straight through -- no extra RE,
// no extra hook, just the next real step on data this project already has.
void UpdateStreamlineCameraMatricesX64(const float pos[3], const float fwd[3],
    const float right[3], const float up[3])
{
    ++g_streamlineCameraTickCountX64;

    // Real camera-to-world matrix, built EXACTLY the way
    // sl_matrix_helpers.h's own recalculateCameraMatrices() does (right/up/
    // fwd/pos as rows 0-3, row-major, w=0 for the basis rows and w=1 for the
    // position row) -- the documented convention sl::Constants and this
    // whole reference pipeline are built around. Right/forward are
    // normalized and up is RE-DERIVED via cross(fwd, right) rather than
    // trusting the separately-read up value directly, matching Streamline's
    // own reference code exactly -- this project's own live-confirmed "up"
    // reading (+0x15B4) already agrees with this construction (that
    // agreement is PART of how the offsets were confirmed in the first
    // place), so re-deriving it here costs nothing and stays consistent
    // with the one reference implementation this whole feature is built on.
    sl::float3 rightVec(right[0], right[1], right[2]);
    sl::float3 fwdVec(fwd[0], fwd[1], fwd[2]);
    sl::vectorNormalize(rightVec);
    sl::vectorNormalize(fwdVec);
    sl::float3 upVec{};
    sl::vectorCrossProduct(upVec, fwdVec, rightVec);
    sl::vectorNormalize(upVec);

    sl::float4x4 cameraToWorld = {
        sl::float4(rightVec.x, rightVec.y, rightVec.z, 0.0f),
        sl::float4(upVec.x,    upVec.y,    upVec.z,    0.0f),
        sl::float4(fwdVec.x,   fwdVec.y,   fwdVec.z,   0.0f),
        sl::float4(pos[0],     pos[1],     pos[2],     1.0f),
    };

    if (g_haveStreamlinePrevFrameX64) {
        // The real, camera-space, precision-safe relative transform between
        // this frame and the previous one -- sl_matrix_helpers.h's own
        // calcCameraToPrevCamera, unmodified (real, vendored reference math,
        // not reimplemented). For a stationary, unrotated camera this should
        // be very close to identity; the translation row should reflect real
        // per-frame camera-space movement (small at typical frame time and
        // movement speed), not the raw world-space position delta.
        sl::float4x4 cameraToPrevCamera{};
        sl::calcCameraToPrevCamera(cameraToPrevCamera, cameraToWorld, g_prevCameraToWorldX64);

        // 2026-09-24: this function is called on EVERY nonzero-position fire,
        // roughly 13 of which are bit-identical within one real frame (see
        // analog_input_hooks_x64.cpp's own call-site comment) -- a fixed
        // sparse cadence (first 5, every 5000th) landed on identical-data
        // ticks essentially every time by pure chance (~13/14 odds per
        // sample), showing an all-zero translation that looked suspicious
        // but wasn't a bug, just a sampling artifact. Log real motion
        // whenever it actually happens instead of gambling on a fixed tick
        // count, PLUS keep a much sparser heartbeat so a genuinely-idle
        // camera still confirms the pipeline is alive.
        float tx = cameraToPrevCamera[3].x, ty = cameraToPrevCamera[3].y, tz = cameraToPrevCamera[3].z;
        bool realMotion = (fabsf(tx) > 0.0001f) || (fabsf(ty) > 0.0001f) || (fabsf(tz) > 0.0001f);
        bool heartbeat = g_streamlineCameraTickCountX64 <= 5 || (g_streamlineCameraTickCountX64 % 20000) == 0;

        // Real-motion logging is rate-limited (this project's own standing
        // "never unthrottled per-frame" lesson, issue #87) -- a moving
        // player would otherwise log on roughly every 14th tick, easily
        // thousands of lines per minute of real play.
        static ULONGLONG s_lastMotionLogMs = 0;
        ULONGLONG nowMs = GetTickCount64();
        bool motionDue = realMotion && (nowMs - s_lastMotionLogMs >= 250);

        if (motionDue || heartbeat) {
            if (motionDue) s_lastMotionLogMs = nowMs;
            char buf[300];
            sprintf_s(buf, "[x64-streamline-camera] tick=%lld cameraToPrevCamera.translation="
                "[%.5f %.5f %.5f]%s",
                g_streamlineCameraTickCountX64, tx, ty, tz,
                realMotion ? " (real motion)" : " (heartbeat, at/near rest)");
            LogFromController(buf);
        }
    } else {
        g_haveStreamlinePrevFrameX64 = true;
        LogFromController("[x64-streamline-camera] First frame -- camera-to-world tracking "
            "started, no previous frame to compare against yet.");
    }

    g_prevCameraToWorldX64 = cameraToWorld;
}

// Real reset hook for level loads/camera cuts -- calcCameraToPrevCamera
// against a previous frame from a DIFFERENT level/teleport would produce a
// real but meaningless huge "motion", exactly the case sl::Constants::reset
// exists for (Boolean reset, sl_consts.h). Not yet wired to a caller --
// groundwork for when slSetConstants integration actually starts. Real,
// scoped, one job.
void ResetStreamlineCameraHistoryX64()
{
    g_haveStreamlinePrevFrameX64 = false;
}
