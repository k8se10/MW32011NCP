// motion_blur.hlsl -- Phase E (visual-suite plan): a simple, camera-only
// (view-angle-delta-based) directional motion blur -- NOT per-object motion
// vectors, matching the plan's own explicit scope ("camera-only... tractable
// without deep engine motion-vector integration"). A standard, well-known
// technique (a fixed N-tap average sampled along a supplied 2D blur
// direction/magnitude in UV space), not a port of any external reference the
// way fsr_rcas.hlsl is -- there's no single canonical "motion blur" source to
// cite, this is straightforward, widely-used math.
//
// The actual blur direction/magnitude is computed host-side
// (MotionBlurShaderSetupCallback, overlay_hud.cpp) from this project's own
// real per-frame view-angle delta -- see analog_input_hooks.cpp's
// g_motionBlurYawDeltaDeg/g_motionBlurPitchDeltaDeg, the exact degrees this
// project's own look-injection code (InjectControllerLookAngles) applied to
// the real engine yaw/pitch accumulators THIS frame -- and passed in as
// blurVec, already strength-scaled and clamped. This shader itself is generic
// ("blur along this UV vector"), no knowledge of angles/degrees/FOV baked in.
//
// Real, documented limitation (see analog_input_hooks.cpp's own comment on the
// two globals above): this only reacts to CONTROLLER-driven look (stick and
// gyro, this project's own input paths) -- mouse look is untouched by this
// project's hooks and will not trigger blur. Matches this project's
// controller-first scope, not an oversight.
sampler2D tex0 : register(s0);
float4 blurVec : register(c0);  // .xy = full blur extent in UV units this frame

static const int kTapCount = 8;

float4 main(float2 uv : TEXCOORD0) : COLOR0
{
    float3 sum = 0;
    // Samples spread from -0.5*blurVec to +0.5*blurVec, centered on the real
    // pixel -- a symmetric back-and-forth turn doesn't visibly shift the image,
    // only smear it, which is the actual desired effect.
    for (int i = 0; i < kTapCount; i++) {
        float t = (float(i) / float(kTapCount - 1)) - 0.5;
        sum += tex2D(tex0, uv + blurVec.xy * t).rgb;
    }
    return float4(sum / kTapCount, 1.0);
}
