// SMAA 1x passes for this project's D3D9 full-screen pipeline (2026-09-21).
// Based on SMAA by Jorge Jimenez, Jose I. Echevarria, Belen Masia, Fernando Navarro and Diego Gutierrez
// (MIT license, https://github.com/iryoku/smaa -- see SMAA.hlsl's own header). The reference's vertex-shader
// helpers are invoked from the pixel shader here because this pipeline draws with the game's ambient vertex
// state (no vertex shader of our own -- see DrawFullScreenPass, issue #100).
// Compile (ps_3_0), one header per entry point:
//   fxc -T ps_3_0 -E EdgeMain    -Fh smaa_edge_ps.h    -Vn g_smaaEdgePixelShaderBytecode    smaa_passes.hlsl
//   fxc -T ps_3_0 -E WeightsMain -Fh smaa_weights_ps.h -Vn g_smaaWeightsPixelShaderBytecode smaa_passes.hlsl
//   fxc -T ps_3_0 -E BlendMain   -Fh smaa_blend_ps.h   -Vn g_smaaBlendPixelShaderBytecode   smaa_passes.hlsl
float4 rtMetrics : register(c0);   // (1/w, 1/h, w, h)
#define SMAA_RT_METRICS rtMetrics
#define SMAA_HLSL_3 1
#define SMAA_PRESET_HIGH 1
#include "SMAA.hlsl"

sampler2D tex0 : register(s0);
sampler2D tex1 : register(s1);
sampler2D tex2 : register(s2);

// Pass 1: luma edge detection. s0 = scene copy. Output cleared-to-0 target; non-edges are discarded.
float4 EdgeMain(float2 uv : TEXCOORD0) : COLOR0
{
    float4 offset[3];
    SMAAEdgeDetectionVS(uv, offset);
    return float4(SMAALumaEdgeDetectionPS(uv, offset, tex0), 0.0, 0.0);
}

// Pass 2: blending weights. s0 = edges, s1 = area texture, s2 = search texture.
float4 WeightsMain(float2 uv : TEXCOORD0) : COLOR0
{
    float4 offset[3];
    float2 pixcoord;
    SMAABlendingWeightCalculationVS(uv, pixcoord, offset);
    return SMAABlendingWeightCalculationPS(uv, pixcoord, offset, tex0, tex1, tex2, float4(0.0, 0.0, 0.0, 0.0));
}

// Pass 3: neighborhood blending onto the real render target. s0 = scene copy, s1 = blend weights.
float4 BlendMain(float2 uv : TEXCOORD0) : COLOR0
{
    float4 offset;
    SMAANeighborhoodBlendingVS(uv, offset);
    return SMAANeighborhoodBlendingPS(uv, offset, tex0, tex1);
}
