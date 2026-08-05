// options_blur_ps.h -- compiled ps_2_0 pixel shader bytecode for the custom Options
// screen's right-side background blur (issue #66, 2026-08-05). Compiled OFFLINE via
// the Windows SDK's fxc.exe from options_blur.hlsl (source kept in re_notes/ for the
// record, not part of the build) -- embedded directly as a byte array, exactly like
// this project already embeds fonts (.ttf bytes) and button glyphs (.png bytes), so
// there is NO runtime shader-compiler dependency (no D3DX linked into this project
// at all, a deliberate standing choice since day one).
//
// ps_2_0 (not a newer profile) matches this old engine's own real shader era.
// 9-tap weighted blur (center + 4 axis + 4 diagonal taps) sampling a texture the
// caller has already downsampled from the real backbuffer via StretchRect -- see
// overlay_hud.cpp's DrawBlurredBackgroundRegion for how this is actually used.
//
// Original HLSL source (options_blur.hlsl):
//   sampler2D tex0 : register(s0);
//   float4 texelSize : register(c0); // .xy = (1/captureWidth, 1/captureHeight)
//   float4 main(float2 uv : TEXCOORD0) : COLOR0
//   {
//       float2 t = texelSize.xy;
//       float4 c = tex2D(tex0, uv) * 0.28;
//       c += tex2D(tex0, uv + float2( t.x,  0.0)) * 0.11;
//       c += tex2D(tex0, uv + float2(-t.x,  0.0)) * 0.11;
//       c += tex2D(tex0, uv + float2( 0.0,  t.y)) * 0.11;
//       c += tex2D(tex0, uv + float2( 0.0, -t.y)) * 0.11;
//       c += tex2D(tex0, uv + float2( t.x,  t.y)) * 0.07;
//       c += tex2D(tex0, uv + float2(-t.x,  t.y)) * 0.07;
//       c += tex2D(tex0, uv + float2( t.x, -t.y)) * 0.07;
//       c += tex2D(tex0, uv + float2(-t.x, -t.y)) * 0.07;
//       return c;
//   }
// Compiled with: fxc /T ps_2_0 /E main /Fh options_blur_ps.h /Vn g_optionsBlurPixelShaderBytecode options_blur.hlsl
#pragma once

inline const BYTE g_optionsBlurPixelShaderBytecode[] =
{
      0,   2, 255, 255, 254, 255,
     43,   0,  67,  84,  65,  66,
     28,   0,   0,   0, 127,   0,
      0,   0,   0,   2, 255, 255,
      2,   0,   0,   0,  28,   0,
      0,   0,   0,   1,   0,   0,
    120,   0,   0,   0,  68,   0,
      0,   0,   3,   0,   0,   0,
      1,   0,   2,   0,  76,   0,
      0,   0,   0,   0,   0,   0,
     92,   0,   0,   0,   2,   0,
      0,   0,   1,   0,   2,   0,
    104,   0,   0,   0,   0,   0,
      0,   0, 116, 101, 120,  48,
      0, 171, 171, 171,   4,   0,
     12,   0,   1,   0,   1,   0,
      1,   0,   0,   0,   0,   0,
      0,   0, 116, 101, 120, 101,
    108,  83, 105, 122, 101,   0,
    171, 171,   1,   0,   3,   0,
      1,   0,   4,   0,   1,   0,
      0,   0,   0,   0,   0,   0,
    112, 115,  95,  50,  95,  48,
      0,  77, 105,  99, 114, 111,
    115, 111, 102, 116,  32,  40,
     82,  41,  32,  72,  76,  83,
     76,  32,  83, 104,  97, 100,
    101, 114,  32,  67, 111, 109,
    112, 105, 108, 101, 114,  32,
     49,  48,  46,  49,   0, 171,
     81,   0,   0,   5,   1,   0,
     15, 160, 174,  71, 225,  61,
     41,  92, 143,  62,   0,   0,
      0,   0,  41,  92, 143,  61,
     81,   0,   0,   5,   2,   0,
     15, 160,   0,   0, 128, 191,
      0,   0, 128,  63,   0,   0,
      0,   0,   0,   0,   0,   0,
     31,   0,   0,   2,   0,   0,
      0, 128,   0,   0,   3, 176,
     31,   0,   0,   2,   0,   0,
      0, 144,   0,   8,  15, 160,
      2,   0,   0,   3,   0,   0,
      1, 128,   0,   0,   0, 176,
      0,   0,   0, 160,   1,   0,
      0,   2,   0,   0,   2, 128,
      0,   0,  85, 176,   1,   0,
      0,   2,   1,   0,   1, 128,
      0,   0,   0, 161,   1,   0,
      0,   2,   1,   0,   2, 128,
      1,   0, 170, 160,   2,   0,
      0,   3,   1,   0,   3, 128,
      1,   0, 228, 128,   0,   0,
    228, 176,   1,   0,   0,   2,
      2,   0,   1, 128,   0,   0,
      0, 176,   2,   0,   0,   3,
      2,   0,   2, 128,   0,   0,
     85, 176,   0,   0,  85, 160,
      1,   0,   0,   2,   3,   0,
      1, 128,   0,   0,   0, 176,
      2,   0,   0,   3,   3,   0,
      2, 128,   0,   0,  85, 176,
      0,   0,  85, 161,   2,   0,
      0,   3,   4,   0,   3, 128,
      0,   0, 228, 176,   0,   0,
    228, 160,   1,   0,   0,   2,
      5,   0,   3, 128,   0,   0,
    228, 160,   4,   0,   0,   4,
      5,   0,   3, 128,   5,   0,
    228, 128,   2,   0, 228, 160,
      0,   0, 228, 176,   2,   0,
      0,   3,   6,   0,   1, 128,
      0,   0,   0, 176,   0,   0,
      0, 160,   2,   0,   0,   3,
      6,   0,   2, 128,   0,   0,
     85, 176,   0,   0,  85, 161,
      2,   0,   0,   3,   7,   0,
      3, 128,   0,   0, 228, 176,
      0,   0, 228, 161,  66,   0,
      0,   3,   0,   0,  15, 128,
      0,   0, 228, 128,   0,   8,
    228, 160,  66,   0,   0,   3,
      8,   0,  15, 128,   0,   0,
    228, 176,   0,   8, 228, 160,
     66,   0,   0,   3,   1,   0,
     15, 128,   1,   0, 228, 128,
      0,   8, 228, 160,  66,   0,
      0,   3,   2,   0,  15, 128,
      2,   0, 228, 128,   0,   8,
    228, 160,  66,   0,   0,   3,
      3,   0,  15, 128,   3,   0,
    228, 128,   0,   8, 228, 160,
     66,   0,   0,   3,   4,   0,
     15, 128,   4,   0, 228, 128,
      0,   8, 228, 160,  66,   0,
      0,   3,   5,   0,  15, 128,
      5,   0, 228, 128,   0,   8,
    228, 160,  66,   0,   0,   3,
      6,   0,  15, 128,   6,   0,
    228, 128,   0,   8, 228, 160,
     66,   0,   0,   3,   7,   0,
     15, 128,   7,   0, 228, 128,
      0,   8, 228, 160,   5,   0,
      0,   3,   0,   0,  15, 128,
      0,   0, 228, 128,   1,   0,
      0, 160,   4,   0,   0,   4,
      0,   0,  15, 128,   8,   0,
    228, 128,   1,   0,  85, 160,
      0,   0, 228, 128,   4,   0,
      0,   4,   0,   0,  15, 128,
      1,   0, 228, 128,   1,   0,
      0, 160,   0,   0, 228, 128,
      4,   0,   0,   4,   0,   0,
     15, 128,   2,   0, 228, 128,
      1,   0,   0, 160,   0,   0,
    228, 128,   4,   0,   0,   4,
      0,   0,  15, 128,   3,   0,
    228, 128,   1,   0,   0, 160,
      0,   0, 228, 128,   4,   0,
      0,   4,   0,   0,  15, 128,
      4,   0, 228, 128,   1,   0,
    255, 160,   0,   0, 228, 128,
      4,   0,   0,   4,   0,   0,
     15, 128,   5,   0, 228, 128,
      1,   0, 255, 160,   0,   0,
    228, 128,   4,   0,   0,   4,
      0,   0,  15, 128,   6,   0,
    228, 128,   1,   0, 255, 160,
      0,   0, 228, 128,   4,   0,
      0,   4,   0,   0,  15, 128,
      7,   0, 228, 128,   1,   0,
    255, 160,   0,   0, 228, 128,
      1,   0,   0,   2,   0,   8,
     15, 128,   0,   0, 228, 128,
    255, 255,   0,   0
};
