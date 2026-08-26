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
//
// Real center-to-edge radial falloff added 2026-08-26 (direct user request:
// "less blurry in middle more blur on edges") -- a standard technique (keeps
// the real focal point sharp while peripheral vision smears more, matching
// how motion blur is commonly done in racing/FPS titles) -- see
// [Video] MotionBlurCenterFalloff, mod_config.h.
sampler2D tex0 : register(s0);
float4 blurVec : register(c0);      // .xy = full blur extent in UV units this frame, at
                                      // the SCREEN EDGE (see radialParams.x below -- the
                                      // center-vs-edge falloff scales down from this max,
                                      // it never scales above it)
float4 radialParams : register(c1); // .x = center-to-edge falloff strength, 0..1.
                                      // 0 = uniform blur everywhere (old behavior). 1 =
                                      // blur fades to ~0 exactly at screen center, reaching
                                      // full blurVec strength only at the farthest on-
                                      // screen point (a corner). Values in between blend
                                      // linearly between uniform and fully-radial.

static const int kTapCount = 8;

float4 main(float2 uv : TEXCOORD0) : COLOR0
{
    // Real per-pixel falloff: distance from screen center, normalized so a
    // corner (the farthest point from center on any real aspect ratio) maps
    // to exactly 1.0 -- 0.70710678 = sqrt(0.5*0.5 + 0.5*0.5), the UV-space
    // distance from (0.5,0.5) to (0,0)/(1,1)/etc.
    float2 centerOffset = uv - float2(0.5, 0.5);
    float dist = saturate(length(centerOffset) / 0.70710678);
    float radialScale = lerp(1.0, dist, radialParams.x);
    float2 effectiveBlurVec = blurVec.xy * radialScale;

    float3 sum = 0;
    // Samples spread from -0.5*effectiveBlurVec to +0.5*effectiveBlurVec,
    // centered on the real pixel -- a symmetric back-and-forth turn doesn't
    // visibly shift the image, only smear it, which is the actual desired
    // effect.
    for (int i = 0; i < kTapCount; i++) {
        float t = (float(i) / float(kTapCount - 1)) - 0.5;
        sum += tex2D(tex0, uv + effectiveBlurVec * t).rgb;
    }
    return float4(sum / kTapCount, 1.0);
}
