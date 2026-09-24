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
#include "overlay_hud.h" // GetLastKnownRenderDevice/GetRealScreenSize

extern void LogFromController(const char* msg); // dllmain.cpp
extern "C" float GetDvarFloatX64_Exported(const char* name); // analog_input_hooks_x64.cpp,
    // real, already-live-confirmed dvar reader (already used for "cg_fov" itself
    // in that same file's own ADS zoom-slowdown feature).

namespace {

sl::float4x4 g_prevCameraToWorldX64{};
bool g_haveStreamlinePrevFrameX64 = false;
long long g_streamlineCameraTickCountX64 = 0;

// MW32011NCP, 2026-09-24: real 3D perspective projection matrix, built
// independently of the game's own internal matrix. Two real, live-RE'd
// dead ends before this: the struct offset originally assumed to be "the
// projection matrix" (render-state+0x14d0) turned out to be a pure
// width/height screen-space transform (zero FOV/near/far dependency,
// confirmed via its real constants: DAT_1403e3e6c=1.0, DAT_1403e416c=-1.0,
// DAT_1403e52e8=-2.0, solved against live values to recover the real render
// resolution exactly) -- almost certainly feeds a 2D/post-process pass, not
// the 3D scene (explains the earlier jitter test's uniform full-screen-slide
// behavior, the signature of a shifted 2D quad, not real 3D parallax
// jitter). A second lead (FUN_1401e0880's own "+0x17ac..+0x17f8" block) also
// turned out wrong when read live -- a camera-position + monotonic-timer
// record, not FOV/aspect/near/far.
//
// Real, confirmed FOV instead: "cg_fov" (GetDvarFloatX64, already
// live-proven in this exact codebase for ADS zoom-aware look-slowdown).
// Aspect ratio: real render width/height (GetRealScreenSize, same
// convention every other per-frame diagnostic in this codebase uses).
// Standard D3D perspective (LH, row-vector) formula, row-major to match
// sl::float4x4's own row[4] layout:
//   xScale = cot(fovX/2);  yScale = xScale * aspect
//   row0 = (xScale, 0, 0, 0);  row1 = (0, yScale, 0, 0)
//   row2 = (0, 0, zf/(zf-zn), 1);  row3 = (0, 0, -zn*zf/(zf-zn), 0)
//
// Near/far are NOT yet RE'd -- two real, honest attempts (exact "znear"/
// "zfar" string search, and the only real "zfar"-named dvar found,
// "r_zfar") both came up empty or wrong ("r_zfar" is a fog-culling
// distance, not the camera far-clip plane). Using clearly-flagged
// placeholder values for now; these will need real reconciliation once
// resource/depth-buffer tagging (the next real Streamline step) starts,
// since the depth values Streamline actually receives have to be
// consistent with whatever near/far this matrix encodes.
constexpr float kEstimatedNearPlaneX64 = 4.0f;   // UNCONFIRMED, not RE'd
constexpr float kEstimatedFarPlaneX64 = 4000.0f; // UNCONFIRMED, not RE'd

sl::float4x4 BuildStandardProjectionMatrixX64()
{
    float fovDegrees = GetDvarFloatX64_Exported("cg_fov");
    if (fovDegrees <= 0.0f || fovDegrees >= 180.0f) fovDegrees = 65.0f; // real cg_fov
        // default per this project's own already-confirmed reads elsewhere;
        // safe fallback only, never expected to actually trigger live.

    int renderWidth = 1920, renderHeight = 1080;
    GetRealScreenSize(GetLastKnownRenderDevice(), renderWidth, renderHeight);
    if (renderWidth <= 0) renderWidth = 1920;
    if (renderHeight <= 0) renderHeight = 1080;
    float aspect = static_cast<float>(renderWidth) / static_cast<float>(renderHeight);

    float halfFovXRad = (fovDegrees * 3.14159265358979323846f / 180.0f) * 0.5f;
    float xScale = 1.0f / tanf(halfFovXRad);
    float yScale = xScale * aspect;

    constexpr float zn = kEstimatedNearPlaneX64;
    constexpr float zf = kEstimatedFarPlaneX64;
    float m22 = zf / (zf - zn);
    float m32 = -zn * zf / (zf - zn);

    sl::float4x4 m{};
    m[0] = sl::float4(xScale, 0.0f, 0.0f, 0.0f);
    m[1] = sl::float4(0.0f, yScale, 0.0f, 0.0f);
    m[2] = sl::float4(0.0f, 0.0f, m22, 1.0f);
    m[3] = sl::float4(0.0f, 0.0f, m32, 0.0f);
    return m;
}

} // namespace

// Called once per real frame from Hook_ProjectionMatrixBuild
// (analog_input_hooks_x64.cpp), immediately after it reads the confirmed
// pos/fwd/right/up offsets, passing them straight through -- no extra RE,
// no extra hook, just the next real step on data this project already has.
void UpdateStreamlineCameraMatricesX64(const float pos[3], const float fwd[3],
    const float right[3], const float up[3])
{
    ++g_streamlineCameraTickCountX64;

    // Real, live-verification diagnostic for the new standard projection
    // matrix -- heartbeat cadence only (first 5, then every 20000th), same
    // as the rest of this feature's own logging. Zero behavior change (not
    // yet fed into slSetConstants -- see this file's own top comment).
    if (g_streamlineCameraTickCountX64 <= 5 || (g_streamlineCameraTickCountX64 % 20000) == 0) {
        sl::float4x4 proj = BuildStandardProjectionMatrixX64();
        char pbuf[300];
        sprintf_s(pbuf, "[x64-streamline-projmat] tick=%lld xScale=%.5f yScale=%.5f "
            "(zn=%.1f zf=%.1f UNCONFIRMED)",
            g_streamlineCameraTickCountX64, proj[0].x, proj[1].y,
            kEstimatedNearPlaneX64, kEstimatedFarPlaneX64);
        LogFromController(pbuf);
    }

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
