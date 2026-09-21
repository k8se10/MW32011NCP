// FXAA-style edge-directed anti-aliasing (2026-09-21), applied at native resolution to the captured
// scene by DrawFullScreenPass before RCAS -- a DLAA-style "AA at output resolution" pass.
// Algorithm: Timothy Lottes' public-domain FXAA (NVIDIA), edge-direction blur with a 2-tap/4-tap
// span test, written from the published description. ps_3_0. Compile:
//   fxc /T ps_3_0 /E main /Fh fxaa_ps.h /Vn g_fxaaPixelShaderBytecode fxaa.hlsl
sampler2D tex0 : register(s0);
float4 texelSize : register(c0);  // .xy = 1/width,1/height
float4 params : register(c1);     // .x = span max (px), .y = edge threshold

float luma(float3 c) { return dot(c, float3(0.299, 0.587, 0.114)); }

float4 main(float2 uv : TEXCOORD0) : COLOR0
{
    float2 rcp = texelSize.xy;
    float3 rgbNW = tex2D(tex0, uv + float2(-1.0, -1.0) * rcp).rgb;
    float3 rgbNE = tex2D(tex0, uv + float2( 1.0, -1.0) * rcp).rgb;
    float3 rgbSW = tex2D(tex0, uv + float2(-1.0,  1.0) * rcp).rgb;
    float3 rgbSE = tex2D(tex0, uv + float2( 1.0,  1.0) * rcp).rgb;
    float4 texM = tex2D(tex0, uv);
    float3 rgbM = texM.rgb;

    float lumaNW = luma(rgbNW), lumaNE = luma(rgbNE), lumaSW = luma(rgbSW), lumaSE = luma(rgbSE);
    float lumaM = luma(rgbM);
    float lumaMin = min(lumaM, min(min(lumaNW, lumaNE), min(lumaSW, lumaSE)));
    float lumaMax = max(lumaM, max(max(lumaNW, lumaNE), max(lumaSW, lumaSE)));

    // No edge here -- leave the pixel untouched (keeps text/flat areas crisp).
    if ((lumaMax - lumaMin) < max(0.0625, lumaMax * params.y))
        return texM;

    float2 dir;
    dir.x = -((lumaNW + lumaNE) - (lumaSW + lumaSE));
    dir.y =  ((lumaNW + lumaSW) - (lumaNE + lumaSE));
    float dirReduce = max((lumaNW + lumaNE + lumaSW + lumaSE) * (0.25 * 0.125), 1.0 / 128.0);
    float rcpDirMin = 1.0 / (min(abs(dir.x), abs(dir.y)) + dirReduce);
    float span = params.x;
    dir = clamp(dir * rcpDirMin, -span, span) * rcp;

    float3 rgbA = 0.5 * (tex2D(tex0, uv + dir * (1.0 / 3.0 - 0.5)).rgb +
                         tex2D(tex0, uv + dir * (2.0 / 3.0 - 0.5)).rgb);
    float3 rgbB = rgbA * 0.5 + 0.25 * (tex2D(tex0, uv + dir * -0.5).rgb +
                                       tex2D(tex0, uv + dir *  0.5).rgb);
    float lumaB = luma(rgbB);
    float3 outRgb = (lumaB < lumaMin || lumaB > lumaMax) ? rgbA : rgbB;
    return float4(outRgb, texM.a);
}
