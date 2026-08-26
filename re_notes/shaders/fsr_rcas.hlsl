// fsr_rcas.hlsl -- FSR 1.0 RCAS (Robust Contrast Adaptive Sharpening), ported from
// AMD's real FidelityFX-FSR reference source (ffx_fsr1.h, FsrRcasF/FsrRcasCon,
// MIT license):
//   https://github.com/GPUOpen-Effects/FidelityFX-FSR/blob/master/ffx-fsr/ffx_fsr1.h
//
// AMD's own summary of what this does: "CAS uses a simplified mechanism to convert
// local contrast into a variable amount of sharpness. RCAS uses a more exact
// mechanism, solving for the maximum local sharpness possible before clipping.
// RCAS also has a built in process to limit sharpening of what it detects as
// possible noise."
//
// Original license header (reproduced per the MIT license's attribution
// requirement -- this project's own LICENSING section, CLAUDE.md SS6, applies the
// same standard already used for MinHook/HDE):
//   FidelityFX Super Resolution Sample
//   Copyright (c) 2021 Advanced Micro Devices, Inc. All rights reserved.
//   Permission is hereby granted, free of charge, to any person obtaining a copy
//   of this software and associated documentation files(the "Software"), to deal
//   in the Software without restriction, including without limitation the rights
//   to use, copy, modify, merge, publish, distribute, sublicense, and / or sell
//   copies of the Software, and to permit persons to whom the Software is
//   furnished to do so, subject to the following conditions :
//   The above copyright notice and this permission notice shall be included in
//   all copies or substantial portions of the Software.
//   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
//   IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
//   FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
//   AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
//   LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
//   OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
//   THE SOFTWARE.
//
// This is a direct, single-precision (32-bit float) port of FsrRcasF's real
// per-pixel math -- every min/max/lobe/noise-detection step matches the original
// exactly, verified line-for-line against the fetched real source (not
// reconstructed from memory). What's simplified is AMD's surrounding UX/
// portability scaffolding, not the algorithm itself:
//   - FsrRcasCon's real parameter is "sharpness in stops" (0.0=max, higher=weaker,
//     via sharpness=exp2(-stops)). This port takes `sharpness` directly as the
//     already-computed 0..1 con.x scale -- this project's own [Video]
//     FsrSharpenStrength config value, clamped 0..1, passed straight through.
//     Skips the stops/exp2 indirection since that's just AMD's UI framing choice,
//     not part of FsrRcasF's actual per-pixel math (con.x is used as a plain
//     scalar multiplier there either way).
//   - AMD's AF1_AU1 bit-cast plumbing (their shared path for a 16-bit/32-bit
//     dual-precision build) is irrelevant for a single always-32-bit HLSL target
//     and is skipped -- `sharpness` is a real float shader constant directly.
//   - FSR_RCAS_DENOISE is always ON here (the noise-detection multiply is applied
//     unconditionally, not behind a preprocessor branch) -- matches how most real
//     RCAS integrations ship it; a genuine quality improvement with no real
//     downside for a general-purpose full-screen sharpen.
//   - FSR_RCAS_PASSTHROUGH_ALPHA is not implemented -- this pass runs on the
//     opaque, fully-composed backbuffer capture (Phase A's own
//     g_fullscreenCaptureTexture), alpha is not semantically meaningful here;
//     output alpha is fixed at 1.0.
//   - One real, deliberate addition beyond the original: an epsilon guard
//     (max(..., 1e-6)) in the noise-detection divide, where AMD's own
//     APrxMedRcpF1 (a fast hardware reciprocal approximation) implicitly
//     tolerates a near-zero denominator. An exact HLSL divide on a perfectly flat
//     local region (lumaMax == lumaMin) would otherwise risk Inf/NaN on some
//     hardware -- this guard is new, everything else is the original math.
sampler2D tex0 : register(s0);
float4 texelSize : register(c0);  // .xy = (1/capturedFrameWidth, 1/capturedFrameHeight)
float4 sharpness : register(c1);  // .x = con.x, i.e. this project's own
                                    // FsrSharpenStrength clamped 0..1 (see header
                                    // comment -- NOT put through AMD's exp2/stops
                                    // framing, same real math either way)

// FSR_RCAS_LIMIT, AMD's own real constant: 0.25 - (1.0/16.0) = 0.1875.
static const float kRcasLimit = 0.25 - (1.0 / 16.0);

float4 main(float2 uv : TEXCOORD0) : COLOR0
{
    // Real 3x3 plus-shaped neighborhood, exactly matching FsrRcasF's own layout:
    //     b
    //   d e f
    //     h
    float3 b = tex2D(tex0, uv + float2( 0.0, -texelSize.y)).rgb;
    float3 d = tex2D(tex0, uv + float2(-texelSize.x,  0.0)).rgb;
    float3 e = tex2D(tex0, uv).rgb;
    float3 f = tex2D(tex0, uv + float2( texelSize.x,  0.0)).rgb;
    float3 h = tex2D(tex0, uv + float2( 0.0,  texelSize.y)).rgb;

    // Luma times 2 -- AMD's own real weighting: G + 0.5*(R+B), a fast luma proxy
    // (avoids a real luma dot-product on hot per-pixel neighborhood taps).
    float bL = b.b * 0.5 + (b.r * 0.5 + b.g);
    float dL = d.b * 0.5 + (d.r * 0.5 + d.g);
    float eL = e.b * 0.5 + (e.r * 0.5 + e.g);
    float fL = f.b * 0.5 + (f.r * 0.5 + f.g);
    float hL = h.b * 0.5 + (h.r * 0.5 + h.g);

    // Noise detection -- real FsrRcasF math: the 4-neighbor average vs. the
    // center, normalized by the local luma range, used below to suppress
    // sharpening on what looks like noise rather than a real edge.
    float nz = 0.25 * bL + 0.25 * dL + 0.25 * fL + 0.25 * hL - eL;
    float lumaMax = max(max(max(bL, dL), max(eL, fL)), hL);
    float lumaMin = min(min(min(bL, dL), min(eL, fL)), hL);
    nz = saturate(abs(nz) / max(lumaMax - lumaMin, 1e-6)); // epsilon guard, see header comment
    nz = -0.5 * nz + 1.0;

    // Min/max of the 4-neighbor ring only (NOT including the center), per channel.
    float3 mn4 = min(min(b, d), min(f, h));
    float3 mx4 = max(max(b, d), max(f, h));

    // Real RCAS limiter math -- solves for the maximum sharpening lobe possible
    // before clipping against the local min/max (this is the "R" in RCAS: an
    // exact, not approximated, contrast solve, unlike plain CAS).
    float3 hitMin = min(mn4, e) / (4.0 * mx4);
    float3 hitMax = (1.0 - max(mx4, e)) / (4.0 * mn4 - 4.0);
    float3 lobeRGB = max(-hitMin, hitMax);
    float lobe = max(-kRcasLimit, min(max(max(lobeRGB.r, lobeRGB.g), lobeRGB.b), 0.0)) * sharpness.x;
    lobe *= nz; // FSR_RCAS_DENOISE, always applied -- see header comment

    float rcpL = 1.0 / (4.0 * lobe + 1.0);
    float3 outColor = (lobe * (b + d + h + f) + e) * rcpL;

    return float4(outColor, 1.0);
}
