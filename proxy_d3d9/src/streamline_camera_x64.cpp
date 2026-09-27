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
extern "C" bool IsStreamlineInitializedX64(); // streamline_integration_x64.cpp,
    // 2026-09-26 -- see its own comment for the full rationale. MSVC (unlike
    // clang) requires a linkage-specification declaration at true global
    // scope, not inside a function body -- a real C2598 caught before this
    // shipped.
extern "C" float GetDvarFloatX64_Exported(const char* name); // analog_input_hooks_x64.cpp,
    // real, already-live-confirmed dvar reader (already used for "cg_fov" itself
    // in that same file's own ADS zoom-slowdown feature).
extern bool StreamlineSetConstantsX64(const sl::Constants& constants); // streamline_integration_x64.cpp
extern long long GetStreamlineFrameSequenceX64(); // streamline_integration_x64.cpp, 2026-09-27
extern bool GetStreamlineInternalRenderResolutionX64(uint32_t& outWidth, uint32_t& outHeight); // streamline_resources_x64.cpp

namespace {

sl::float4x4 g_prevCameraToWorldX64{};
// Previous frame's own projection matrix -- needed for clipToPrevClip/
// prevClipToClip (sl_matrix_helpers.h's own recalculateCameraMatrices()
// reference math, replicated manually below rather than called directly,
// since that function's OWN previous-frame storage is a static, unkeyed
// pair explicitly marked "DO NOT USE THIS IN ANYTHING PROPER" -- this
// project already tracks its own real previous-frame camera-to-world
// matrix the same way (g_prevCameraToWorldX64 above), so tracking the
// matching previous projection matrix here is the same pattern, not a new
// one.
sl::float4x4 g_prevCameraViewToClipX64{};
bool g_haveStreamlinePrevFrameX64 = false;
long long g_streamlineCameraTickCountX64 = 0;

// REAL FIX, 2026-09-27: live-reported finding -- even with the camera-
// teleport reset detector (this file's own `treatAsReset` logic below), a
// real GPU hang still occurred, and on a SECOND live test it happened even
// EARLIER, mid-transition rather than after settling into first-person
// (map-dependent: Dome hung mid-transition, Underground reached first-
// person first) -- direct user report. Real, honest reasoning: the reset
// detector fixes what DLSS is TOLD (a clean `reset=eTrue` instead of a
// discontinuous reprojection matrix), but doesn't stop `RunDlssEvaluateAndCompositeX64`
// (streamline_evaluate_x64.cpp, gated independently on menu/in-level/
// clcState) from attempting a real evaluate on the VERY NEXT eligible
// frame after a reset -- possibly still mid-transition, while the camera,
// tagged resources (render targets recreated on level load), and the
// engine's own render state generally haven't fully stabilized yet. A
// single-frame reset flag doesn't guarantee the FRAME AFTER it is safe.
// Real fix: track how many real frames have passed since the last reset
// (first-frame OR teleport-detected), and expose that so evaluate can
// require a real minimum number of stable, tracked frames before ever
// attempting DLSS -- not just "not currently mid-reset." 30 ticks
// (matches this project's own already-documented 30Hz-locked simulation
// tick, i.e. roughly one real second) is a generous, deliberately
// conservative grace period for render targets/engine state to settle
// after any level load or camera cut.
long long g_streamlineLastResetTickX64 = 0;
constexpr long long kStreamlineStabilizationTicksX64 = 30;

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

sl::float4x4 BuildStandardProjectionMatrixX64(float* outFovRadians = nullptr, float* outAspect = nullptr)
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

    float fovXRadians = fovDegrees * 3.14159265358979323846f / 180.0f;
    float halfFovXRad = fovXRadians * 0.5f;
    float xScale = 1.0f / tanf(halfFovXRad);
    float yScale = xScale * aspect;

    // sl::Constants::cameraFOV/cameraAspectRatio want the same real FOV/aspect
    // this matrix is built from -- exposed via out-params so the one caller
    // that fills Constants (UpdateStreamlineCameraMatricesX64) doesn't have to
    // re-derive them from cg_fov/GetRealScreenSize a second time.
    if (outFovRadians) *outFovRadians = fovXRadians;
    if (outAspect) *outAspect = aspect;

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
    // Real backend gate, 2026-09-26 (direct instruction, following the
    // GraphicsApi default flip to Vulkan) -- previously ran unconditionally
    // every real frame (real projection-matrix and camera-to-world-matrix
    // construction, real CPU work) regardless of whether Streamline was
    // even enabled, since its own caller (Hook_ProjectionMatrixBuild,
    // analog_input_hooks_x64.cpp) only checked "camera position isn't
    // zero," not backend/feature state. The one real SDK call this
    // function reaches (slSetConstants, via StreamlineSetConstantsX64) was
    // already safely gated on g_streamlineVulkanInfoSet -- never unsafe --
    // but this was genuine wasted per-frame work under LegacyD3D9 or with
    // StreamlineEnabled=0. See IsStreamlineInitializedX64's own comment
    // (streamline_integration_x64.cpp) for why this is the correct signal.
    if (!IsStreamlineInitializedX64()) return;

    // REAL BUG FIX, 2026-09-27: this function is called on EVERY nonzero-
    // position fire -- confirmed live to be ~13 times per real frame, all
    // sharing one render-state struct (see this function's own "roughly 13
    // of which are bit-identical" comment below, and the ROUND-4 finding in
    // vulkan_dlss_pipeline_research.md item 17). Every one of those 13 calls
    // used to call StreamlineSetConstantsX64 unconditionally -- 2026-09-24's
    // own comment reasoned this was harmless ("calcCameraToPrevCamera on
    // identical data just produces an identity-ish transform, not
    // corruption"), which is true for the MATH but not for the real SDK
    // contract: a first live test (2026-09-26/27) found slSetConstants
    // failing with eErrorDuplicatedConstants ("Setting different 'common'
    // constants multiple times within the same frame is NOT allowed!") on
    // the large majority of real frames -- Streamline itself only permits
    // ONE slSetConstants call per real frame/token, and this function was
    // calling it up to 13 times. Fixed by skipping this entire function
    // (not just the SetConstants call -- the redundant compute/logging work
    // is real waste too) once it's already run for the current real frame,
    // tracked via GetStreamlineFrameSequenceX64() (incremented once per real
    // frame by StreamlineFrameTick(), streamline_integration_x64.cpp) rather
    // than by comparing this call's own camera data against the last call's
    // -- a data-based dedup would also incorrectly skip a genuinely NEW
    // frame whose camera happens to be at rest (identical to the previous
    // frame's own final state), which the frame-sequence approach can't get
    // wrong.
    static long long s_lastConstantsFrameSeqX64 = -1;
    long long currentFrameSeq = GetStreamlineFrameSequenceX64();
    if (currentFrameSeq == s_lastConstantsFrameSeqX64) return;
    s_lastConstantsFrameSeqX64 = currentFrameSeq;

    ++g_streamlineCameraTickCountX64;
    bool heartbeat = g_streamlineCameraTickCountX64 <= 5 || (g_streamlineCameraTickCountX64 % 20000) == 0;

    // Real projection matrix, built every frame now (Constants::cameraViewToClip
    // needs it, not just the diagnostic log below).
    float fovRadians = 0.0f, aspect = 0.0f;
    sl::float4x4 proj = BuildStandardProjectionMatrixX64(&fovRadians, &aspect);

    // Real, live-verification diagnostic for the projection matrix -- heartbeat
    // cadence only, same as the rest of this feature's own logging.
    if (heartbeat) {
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

    // sl::Constants fields shared by both the "have previous frame" and
    // "first frame" branches below -- built once here.
    sl::Constants constants{};
    constants.cameraViewToClip = proj;
    constants.cameraPos = sl::float3(pos[0], pos[1], pos[2]);
    constants.cameraUp = upVec;
    constants.cameraRight = rightVec;
    constants.cameraFwd = fwdVec;
    constants.cameraNear = kEstimatedNearPlaneX64;
    constants.cameraFar = kEstimatedFarPlaneX64;
    constants.cameraFOV = fovRadians;
    constants.cameraAspectRatio = aspect;
    // HONEST GAP, real and unresolved: no actual sub-pixel jitter is applied
    // to real 3D geometry rendering anywhere in this pipeline yet -- the one
    // "jitter" this project's own earlier RE round found and injected targets
    // a confirmed 2D screen-space compositing matrix, unrelated to the real
    // 3D scene (see this file's own BuildStandardProjectionMatrixX64 comment
    // and the research doc). (0,0) here is the honest, correct value for
    // "no jitter currently applied", not a placeholder standing in for a
    // real value this project forgot to wire.
    constants.jitterOffset = sl::float2(0.0f, 0.0f);
    // Normalizes motion-vector texel values into Streamline's expected
    // [-1,1] range -- standard 1/resolution scale, matching the motion-
    // vectors buffer's own real resolution (ComputeExpectedInternalResolutionX64,
    // streamline_resources_x64.cpp -- the same internal render-scale
    // resolution the color INPUT tag uses, not the native/output one).
    {
        uint32_t mvW = 0, mvH = 0;
        if (GetStreamlineInternalRenderResolutionX64(mvW, mvH) && mvW > 0 && mvH > 0) {
            constants.mvecScale = sl::float2(1.0f / static_cast<float>(mvW),
                1.0f / static_cast<float>(mvH));
        } else {
            constants.mvecScale = sl::float2(1.0f, 1.0f);
        }
    }
    // Every texel of our own motion-vectors buffer is zero-filled at
    // (re)create time now (streamline_resources_x64.cpp's ColorFill fix,
    // 2026-09-24) -- 0.0f is therefore a real, reliable sentinel, not a
    // guess.
    constants.motionVectorsInvalidValue = 0.0f;
    constants.depthInverted = sl::Boolean::eFalse; // matches this project's
        // own non-reversed-Z projection matrix construction above (m22=zf/(zf-zn),
        // standard forward depth, not reversed).
    constants.cameraMotionIncluded = sl::Boolean::eFalse; // Stage 1: camera-only
        // motion vectors, per this whole feature's own design (research doc
        // section 2.5) -- Streamline computes/accounts for camera motion
        // itself from the matrices above; our own (currently all-zero) MV
        // buffer only ever needs to carry real per-object motion, which
        // doesn't exist yet either (nothing writes non-zero values into it).
    constants.motionVectors3D = sl::Boolean::eFalse;
    constants.orthographicProjection = sl::Boolean::eFalse;
    constants.motionVectorsDilated = sl::Boolean::eFalse;
    constants.motionVectorsJittered = sl::Boolean::eFalse;

    // REAL FIX, 2026-09-27: live-reported GPU hang (a real vkWaitForFences
    // timeout, DLSS's own recorded GPU compute work never completing,
    // eventually degrading to VK_ERROR_DEVICE_LOST), direct user diagnosis:
    // "its right after the camera transition." Real root cause, found via
    // this file's own already-existing, never-wired groundwork:
    // ResetStreamlineCameraHistoryX64() (this file, below) was built
    // specifically for this ("Real reset hook for level loads/camera cuts
    // -- calcCameraToPrevCamera against a previous frame from a DIFFERENT
    // level/teleport would produce a real but meaningless huge 'motion',
    // exactly the case sl::Constants::reset exists for") but had ZERO
    // callers anywhere in the codebase -- confirmed via a full-repo grep.
    // Every real camera cut/level load after the very first tracked frame
    // was silently feeding Streamline `reset=eFalse` with a
    // `clipToPrevClip`/`cameraToPrevCamera` built from a previous frame
    // that has nothing to do with the new scene -- `sl_consts.h`'s own
    // doc comment on `reset` is explicit this is exactly the case it
    // exists to flag ("if previous frame has no connection to the current
    // one"). A wildly discontinuous reprojection matrix feeding a real,
    // multi-pass neural network (this project's own DLSS/DLSS-NR research
    // this session confirmed both are real iterative transformer
    // networks) is a plausible, concrete mechanism for a GPU compute
    // dispatch to genuinely hang rather than fail cleanly (NaN/Inf
    // propagation through iterative refinement passes behaving
    // pathologically on some hardware/drivers, rather than a fast,
    // detectable error) -- consistent with the real fence-timeout
    // symptom, not a guess.
    //
    // Fixed with a real, self-contained "camera teleport" detector rather
    // than wiring a new external level-load/camera-cut signal (this
    // file's own real, camera-space relative transform, computed below,
    // is already the exact right data to detect this from directly --
    // no new RE or cross-file plumbing needed). A generous, decisive
    // threshold (500 world units of relative translation in one tick --
    // this game's real per-tick sprint movement is nowhere close to that,
    // per this project's own already-documented movement-speed research;
    // a genuine level load/teleport/cutscene cut moves the camera by
    // thousands of units or across a totally different part of the map
    // instantly) distinguishes a real camera cut from ordinary fast
    // gameplay movement.
    constexpr float kCameraTeleportThresholdX64 = 500.0f;

    bool treatAsReset = !g_haveStreamlinePrevFrameX64;
    sl::float4x4 cameraToPrevCamera{};
    if (g_haveStreamlinePrevFrameX64) {
        // The real, camera-space, precision-safe relative transform between
        // this frame and the previous one -- sl_matrix_helpers.h's own
        // calcCameraToPrevCamera, unmodified (real, vendored reference math,
        // not reimplemented). For a stationary, unrotated camera this should
        // be very close to identity; the translation row should reflect real
        // per-frame camera-space movement (small at typical frame time and
        // movement speed), not the raw world-space position delta.
        sl::calcCameraToPrevCamera(cameraToPrevCamera, cameraToWorld, g_prevCameraToWorldX64);

        float jumpMag = sqrtf(cameraToPrevCamera[3].x * cameraToPrevCamera[3].x +
            cameraToPrevCamera[3].y * cameraToPrevCamera[3].y +
            cameraToPrevCamera[3].z * cameraToPrevCamera[3].z);
        if (jumpMag > kCameraTeleportThresholdX64) {
            treatAsReset = true;
            char buf[250];
            sprintf_s(buf, "[x64-streamline-camera] Real camera teleport/transition detected "
                "(jump=%.1f units, tick=%lld) -- treating this frame as a reset rather than feeding "
                "DLSS a discontinuous reprojection matrix.", jumpMag, g_streamlineCameraTickCountX64);
            LogFromController(buf);
        }
    }

    if (!treatAsReset) {
        // clipToPrevClip/prevClipToClip -- manually replicating
        // sl_matrix_helpers.h's own recalculateCameraMatrices() real math
        // (clipToPrevCameraView = clipToCameraView * cameraViewToPrevCameraView;
        // clipToPrevClip = clipToPrevCameraView * cameraViewToClipPrev), using
        // THIS project's own real, per-session previous-frame tracking
        // (g_prevCameraToWorldX64/g_prevCameraViewToClipX64) rather than that
        // function's own static, unkeyed storage -- see this file's own top
        // comment for why recalculateCameraMatrices() itself is never called
        // directly.
        sl::matrixFullInvert(constants.clipToCameraView, constants.cameraViewToClip);
        sl::float4x4 clipToPrevCameraView{};
        sl::matrixMul(clipToPrevCameraView, constants.clipToCameraView, cameraToPrevCamera);
        sl::matrixMul(constants.clipToPrevClip, clipToPrevCameraView, g_prevCameraViewToClipX64);
        sl::matrixFullInvert(constants.prevClipToClip, constants.clipToPrevClip);
        constants.reset = sl::Boolean::eFalse;

        // 2026-09-24: this function USED TO be called on every nonzero-
        // position fire, roughly 13 of which were bit-identical within one
        // real frame (see analog_input_hooks_x64.cpp's own call-site
        // comment) -- a fixed sparse cadence (first 5, every 5000th) landed
        // on identical-data ticks essentially every time by pure chance
        // (~13/14 odds per sample), showing an all-zero translation that
        // looked suspicious but wasn't a bug, just a sampling artifact. Log
        // real motion whenever it actually happens instead of gambling on a
        // fixed tick count, PLUS keep a much sparser heartbeat so a
        // genuinely-idle camera still confirms the pipeline is alive.
        // SUPERSEDED, 2026-09-27: the real per-frame dedup guard added at
        // this function's own top now means this code only ever runs once
        // per real frame -- the "13 identical calls" premise this comment
        // describes no longer applies to THIS function's own call rate, but
        // the reasoning for logging on real motion rather than a fixed tick
        // count is still correct and kept as-is.
        float tx = cameraToPrevCamera[3].x, ty = cameraToPrevCamera[3].y, tz = cameraToPrevCamera[3].z;
        bool realMotion = (fabsf(tx) > 0.0001f) || (fabsf(ty) > 0.0001f) || (fabsf(tz) > 0.0001f);

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
        // First frame ever, OR a real detected camera teleport/transition
        // (see this function's own top comment) -- either way, there is no
        // USABLE previous-frame connection for this frame. clipToPrevClip/
        // prevClipToClip are left as their default-constructed (all-zero)
        // matrices, and Constants::reset=eTrue tells Streamline exactly
        // that: "previous frame has no connection to the current one"
        // (sl_consts.h's own doc comment on this flag), so it won't try to
        // use them.
        sl::matrixFullInvert(constants.clipToCameraView, constants.cameraViewToClip);
        constants.reset = sl::Boolean::eTrue;
        g_streamlineLastResetTickX64 = g_streamlineCameraTickCountX64; // real stabilization-window
            // start point -- see this file's own top comment on
            // kStreamlineStabilizationTicksX64 for why this is recorded on
            // EVERY reset (first frame included), not just teleports.

        if (!g_haveStreamlinePrevFrameX64) {
            g_haveStreamlinePrevFrameX64 = true;
            LogFromController("[x64-streamline-camera] First frame -- camera-to-world tracking "
                "started, no previous frame to compare against yet.");
        }
        // else: the teleport-detection branch above already logged its own
        // real reason -- g_haveStreamlinePrevFrameX64 stays true, tracking
        // simply resumes normally from THIS frame's own state next tick.
    }

    bool ok = StreamlineSetConstantsX64(constants);
    if (heartbeat) {
        char cbuf[150];
        sprintf_s(cbuf, "[x64-streamline-constants] tick=%lld slSetConstants call %s",
            g_streamlineCameraTickCountX64, ok ? "issued" : "SKIPPED (not ready)");
        LogFromController(cbuf);
    }

    g_prevCameraToWorldX64 = cameraToWorld;
    g_prevCameraViewToClipX64 = proj;
}

// ---------------------------------------------------------------------------
// 2026-09-27 (ROUND 24): DLSS camera constants from the engine's REAL matrices.
//
// Live report after ROUND 23 (DLAA visible at 200%): "weird ghosting when i
// move" -- a trailing smear behind camera motion. With the motion-vector
// buffer zero-filled and cameraMotionIncluded=false, Streamline computes the
// camera motion itself from depth plus the constants' matrices
// (sl_consts.h: motionVectorsInvalidValue "is only required if
// cameraMotionIncluded is set to false and SL needs to compute it"), so the
// reprojection is only as good as those matrices. They were synthesized:
// cg_fov used as the true horizontal FOV (IW defines cg_fov at 4:3 and widens
// it for the real aspect; ADS zoom changes it entirely), near/far planes
// guessed (4/4000, never RE'd), and the camera basis rebuilt from axis
// vectors. Wrong FOV/planes = every reprojected pixel lands in the wrong
// place = exactly a ghost trail that follows camera movement.
//
// The engine already has the exact matrices: FUN_1401e0880(state, viewport,
// viewParms) -- which post-FX calls with the current scene view right before
// the DLSS boundary -- stores the GfxViewParms pointer at state+0x1790, and
// GfxViewParms begins with viewMatrix (+0x00), projectionMatrix (+0x40),
// viewProjectionMatrix (+0x80), inverseViewProjectionMatrix (+0xC0), then the
// origin (+0x100) -- the same 0x150 bytes FUN_1401e1640 copies into
// state+0x1490 for 3D rendering (renderer_end_to_end.md). IW matrices are
// row-major, row-vector (D3D) convention, exactly what sl::float4x4 expects.
// ---------------------------------------------------------------------------
namespace {

bool g_lastCameraPathWasRealX64 = false;
bool g_haveCameraPathX64 = false;

// SEH-guarded raw read of the current view's view and projection matrices.
// Plain float arrays only (no C++ objects in the __try function).
bool ReadEngineViewMatricesRawX64(const void* engineState, float outView[16], float outProj[16])
{
    if (!engineState) return false;
#ifdef _WIN32
    __try {
        const unsigned char* viewParms =
            *reinterpret_cast<const unsigned char* const*>(static_cast<const unsigned char*>(engineState) + 0x1790);
        if (!viewParms) return false;
        const float* v = reinterpret_cast<const float*>(viewParms + 0x00);
        const float* p = reinterpret_cast<const float*>(viewParms + 0x40);
        for (int i = 0; i < 16; ++i) { outView[i] = v[i]; outProj[i] = p[i]; }
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
#else
    return false;
#endif
}

bool IsFiniteX64(float f) { return f == f && f < 3.0e38f && f > -3.0e38f; }

// A real perspective projection (row-vector D3D form: w' = z, so m[2][3] == 1
// and m[3][3] == 0) and a view matrix whose 3x3 part is a rotation. Rejects
// shadow/ortho views and garbage.
bool ValidateEngineViewX64(const float v[16], const float p[16])
{
    for (int i = 0; i < 16; ++i) if (!IsFiniteX64(v[i]) || !IsFiniteX64(p[i])) return false;
    if (fabsf(p[11] - 1.0f) > 1.0e-3f || fabsf(p[15]) > 1.0e-3f) return false;
    if (p[0] <= 0.0f || p[5] <= 0.0f) return false;
    for (int r = 0; r < 3; ++r) {
        float len = sqrtf(v[r * 4 + 0] * v[r * 4 + 0] + v[r * 4 + 1] * v[r * 4 + 1] + v[r * 4 + 2] * v[r * 4 + 2]);
        if (fabsf(len - 1.0f) > 0.02f) return false;
    }
    return true;
}

sl::float4x4 ToSlMatrixX64(const float m[16])
{
    sl::float4x4 r{};
    for (uint32_t i = 0; i < 4; ++i) r[i] = sl::float4(m[i * 4 + 0], m[i * 4 + 1], m[i * 4 + 2], m[i * 4 + 3]);
    return r;
}

} // namespace

// Called once per real frame at the DLSS evaluate boundary
// (RunDlssEvaluateAndCompositeX64 / the one-shot menu warm-up), BEFORE the
// stabilization gate. engineState = GetEngineMainCmdBufStateX64(). Sets this
// frame's sl::Constants from the real view/projection when readable and valid;
// otherwise falls back to the legacy synthesized builder (camera fields at
// state+0x1590). Exactly one slSetConstants per frame either way.
void UpdateStreamlineCameraFromEngineViewX64(void* engineState)
{
    if (!IsStreamlineInitializedX64() || !engineState) return;

    static long long s_lastFrameSeq = -1;
    long long currentFrameSeq = GetStreamlineFrameSequenceX64();
    if (currentFrameSeq == s_lastFrameSeq) return;

    float rawView[16] = {}, rawProj[16] = {};
    bool real = ReadEngineViewMatricesRawX64(engineState, rawView, rawProj) && ValidateEngineViewX64(rawView, rawProj);

    // A switch between the real and the legacy source is a discontinuity in
    // the previous-frame matrices -- never reproject across it.
    if (g_haveCameraPathX64 && real != g_lastCameraPathWasRealX64) {
        g_haveStreamlinePrevFrameX64 = false;
        char buf[200];
        sprintf_s(buf, "[x64-streamline-camera] camera source switched to %s -- treating this frame as a reset.",
            real ? "the engine's real view matrices" : "the legacy synthesized matrices");
        LogFromController(buf);
    }
    if (!g_haveCameraPathX64) {
        // One-time record of which source the first frame used; on fallback,
        // the raw projection tells us why validation failed (e.g. a transposed
        // layout would show p[14]==1 instead of p[11]==1).
        char buf[400];
        if (real) {
            sprintf_s(buf, "[x64-streamline-camera] camera source: the engine's real view matrices "
                "(GfxViewParms via state+0x1790).");
        } else {
            sprintf_s(buf, "[x64-streamline-camera] camera source: legacy synthesized matrices -- real view "
                "parms unreadable or not a valid perspective view. proj row2=(%.4g %.4g %.4g %.4g) "
                "row3=(%.4g %.4g %.4g %.4g)", rawProj[8], rawProj[9], rawProj[10], rawProj[11],
                rawProj[12], rawProj[13], rawProj[14], rawProj[15]);
        }
        LogFromController(buf);
    }
    g_haveCameraPathX64 = true;
    g_lastCameraPathWasRealX64 = real;

    if (!real) {
        // Legacy fallback: same inputs the old projection-setter hook used.
        const float* base = reinterpret_cast<const float*>(static_cast<unsigned char*>(engineState) + 0x1590);
        if (base[0] != 0.0f || base[1] != 0.0f || base[2] != 0.0f) {
            s_lastFrameSeq = currentFrameSeq;
            UpdateStreamlineCameraMatricesX64(base, base + 3, base + 6, base + 9);
        }
        return;
    }
    s_lastFrameSeq = currentFrameSeq;

    ++g_streamlineCameraTickCountX64;
    bool heartbeat = g_streamlineCameraTickCountX64 <= 5 || (g_streamlineCameraTickCountX64 % 20000) == 0;

    sl::float4x4 view = ToSlMatrixX64(rawView);
    sl::float4x4 proj = ToSlMatrixX64(rawProj);
    sl::float4x4 cameraToWorld{};
    sl::matrixFullInvert(cameraToWorld, view);

    // Clip planes and depth direction straight from the projection:
    // ndc_z(z) = m22 + m32 / z (row-vector form, w' = z).
    const float A = rawProj[10], B = rawProj[14];
    const bool depthInverted = (B > 0.0f); // ndc_z decreasing with distance
    float zNear, zFar;
    if (!depthInverted) {
        zNear = (fabsf(A) > 1.0e-6f) ? -B / A : kEstimatedNearPlaneX64;
        zFar = (fabsf(1.0f - A) > 1.0e-6f) ? B / (1.0f - A) : 100000.0f; // infinite far
    } else {
        zNear = (fabsf(1.0f - A) > 1.0e-6f) ? B / (1.0f - A) : kEstimatedNearPlaneX64;
        zFar = (fabsf(A) > 1.0e-6f) ? -B / A : 100000.0f; // infinite far
    }
    if (!(zNear > 0.0001f && zNear < 1000.0f)) zNear = kEstimatedNearPlaneX64;
    if (!(zFar > zNear && zFar <= 1.0e7f)) zFar = 100000.0f;

    const float fovY = 2.0f * atanf(1.0f / rawProj[5]);
    const float aspect = rawProj[5] / rawProj[0];

    sl::Constants constants{};
    constants.cameraViewToClip = proj;
    sl::matrixFullInvert(constants.clipToCameraView, constants.cameraViewToClip);
    constants.cameraPos = sl::float3(cameraToWorld[3].x, cameraToWorld[3].y, cameraToWorld[3].z);
    constants.cameraRight = sl::float3(cameraToWorld[0].x, cameraToWorld[0].y, cameraToWorld[0].z);
    constants.cameraUp = sl::float3(cameraToWorld[1].x, cameraToWorld[1].y, cameraToWorld[1].z);
    constants.cameraFwd = sl::float3(cameraToWorld[2].x, cameraToWorld[2].y, cameraToWorld[2].z);
    constants.cameraNear = zNear;
    constants.cameraFar = zFar;
    constants.cameraFOV = fovY;
    constants.cameraAspectRatio = aspect;
    constants.jitterOffset = sl::float2(0.0f, 0.0f); // the engine renders unjittered
    {
        uint32_t mvW = 0, mvH = 0;
        if (GetStreamlineInternalRenderResolutionX64(mvW, mvH) && mvW > 0 && mvH > 0) {
            constants.mvecScale = sl::float2(1.0f / static_cast<float>(mvW), 1.0f / static_cast<float>(mvH));
        } else {
            constants.mvecScale = sl::float2(1.0f, 1.0f);
        }
    }
    constants.motionVectorsInvalidValue = 0.0f;
    constants.depthInverted = depthInverted ? sl::Boolean::eTrue : sl::Boolean::eFalse;
    constants.cameraMotionIncluded = sl::Boolean::eFalse; // SL computes camera motion from these matrices
    constants.motionVectors3D = sl::Boolean::eFalse;
    constants.orthographicProjection = sl::Boolean::eFalse;
    constants.motionVectorsDilated = sl::Boolean::eFalse;
    constants.motionVectorsJittered = sl::Boolean::eFalse;

    constexpr float kCameraTeleportThresholdX64 = 500.0f;
    bool treatAsReset = !g_haveStreamlinePrevFrameX64;
    sl::float4x4 cameraToPrevCamera{};
    if (g_haveStreamlinePrevFrameX64) {
        sl::calcCameraToPrevCamera(cameraToPrevCamera, cameraToWorld, g_prevCameraToWorldX64);
        float jumpMag = sqrtf(cameraToPrevCamera[3].x * cameraToPrevCamera[3].x +
            cameraToPrevCamera[3].y * cameraToPrevCamera[3].y + cameraToPrevCamera[3].z * cameraToPrevCamera[3].z);
        if (jumpMag > kCameraTeleportThresholdX64) {
            treatAsReset = true;
            char buf[250];
            sprintf_s(buf, "[x64-streamline-camera] Real camera teleport/transition detected (jump=%.1f units, "
                "tick=%lld) -- treating this frame as a reset.", jumpMag, g_streamlineCameraTickCountX64);
            LogFromController(buf);
        }
    }
    if (!treatAsReset) {
        sl::float4x4 clipToPrevCameraView{};
        sl::matrixMul(clipToPrevCameraView, constants.clipToCameraView, cameraToPrevCamera);
        sl::matrixMul(constants.clipToPrevClip, clipToPrevCameraView, g_prevCameraViewToClipX64);
        sl::matrixFullInvert(constants.prevClipToClip, constants.clipToPrevClip);
        constants.reset = sl::Boolean::eFalse;
    } else {
        constants.reset = sl::Boolean::eTrue;
        g_streamlineLastResetTickX64 = g_streamlineCameraTickCountX64;
        g_haveStreamlinePrevFrameX64 = true;
    }

    bool ok = StreamlineSetConstantsX64(constants);
    if (heartbeat) {
        // Worst case ~370 chars (fov/near/far bounded above; aspect and the
        // three position floats are only known finite, so up to ~40 digits each).
        char buf[512];
        sprintf_s(buf, "[x64-streamline-camera] REAL view matrices: tick=%lld fovY=%.2fdeg aspect=%.3f "
            "near=%.3f far=%.1f depthInverted=%d pos=(%.1f,%.1f,%.1f) reset=%d slSetConstants=%s",
            g_streamlineCameraTickCountX64, fovY * 57.2957795f, aspect, zNear, zFar, depthInverted ? 1 : 0,
            constants.cameraPos.x, constants.cameraPos.y, constants.cameraPos.z,
            treatAsReset ? 1 : 0, ok ? "issued" : "SKIPPED (not ready)");
        LogFromController(buf);
    }

    g_prevCameraToWorldX64 = cameraToWorld;
    g_prevCameraViewToClipX64 = proj;
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

// 2026-09-27: real public accessor -- RunDlssEvaluateAndCompositeX64
// (streamline_evaluate_x64.cpp) checks this before ever attempting a real
// evaluate, requiring kStreamlineStabilizationTicksX64 real, tracked frames
// to have passed since the last reset (level load, camera cut, or the very
// first frame) before DLSS is fed anything at all. See
// kStreamlineStabilizationTicksX64's own comment for the full real finding
// this closes -- a single-frame reset flag alone doesn't guarantee the
// FRAME AFTER it is actually safe.
bool IsStreamlineCameraStableX64()
{
    if (!g_haveStreamlinePrevFrameX64) return false; // no real tracking has started yet at all
    return (g_streamlineCameraTickCountX64 - g_streamlineLastResetTickX64) >= kStreamlineStabilizationTicksX64;
}

// 2026-09-27, ROUND 17 (corrected): real accessor for the menu-active warm-up
// (streamline_evaluate_x64.cpp's RunDlssMainMenuWarmupOnceX64) -- a plain
// `extern bool g_haveStreamlinePrevFrameX64;` in that file will NOT link
// (LNK2001, confirmed live): the global itself lives inside this file's own
// anonymous namespace (see its declaration above, inside the `namespace { ... }`
// block starting this file), giving it internal linkage a same-named extern
// declaration elsewhere can never bind to -- the exact anonymous-namespace-
// linkage trap this project's own CLAUDE.md already documents. A real
// out-of-namespace accessor, the same pattern IsStreamlineCameraStableX64
// above already uses successfully, is the correct fix.
bool HasStreamlineCameraDataX64()
{
    return g_haveStreamlinePrevFrameX64;
}

// 2026-09-27, MP port (mp_port_plan.md step 6): a real, exe-agnostic "is the
// engine actively rendering a genuine 3D camera view right now" check, reusing
// ROUND 24's already-verified real-view read/validate helpers
// (ReadEngineViewMatricesRawX64/ValidateEngineViewX64, this file's own
// anonymous namespace above) -- NO new signature work, since
// kProjectionMatrixBuildSignature (the real vehicle for engineState, via
// GetEngineMainCmdBufStateX64) already hits identically in both exes
// (signature_resolution_sp_mp_2026-09-27.txt). A real perspective view with an
// orthonormal rotation can only exist while genuinely in a level with a live
// camera -- never at a menu, loading screen, or main menu -- making this a
// real, verified substitute for MP's own missing "in level" signal
// (kInLevelFlagSignature's own MP twin search came back REJECTED, 0/16
// neighbours, mp_twins_2026-09-27.txt -- this sidesteps that dead end
// entirely rather than re-attempting it). Pure query, no side effects, safe
// to call from anywhere at any time -- does NOT touch the per-frame
// dedup/reset state UpdateStreamlineCameraFromEngineViewX64 above owns.
extern "C" bool IsRealEngineViewActiveX64(void* engineState)
{
    if (!engineState) return false;
    float rawView[16] = {}, rawProj[16] = {};
    return ReadEngineViewMatricesRawX64(engineState, rawView, rawProj) && ValidateEngineViewX64(rawView, rawProj);
}

