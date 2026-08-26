// fullscreen_passthrough_ps.h -- compiled ps_2_0 pixel shader bytecode for Phase A of
// the visual-enhancement suite plan (approved plan,
// C:\Users\kyesa\.claude\plans\twinkly-tickling-gem.md). A trivial "output = input"
// shader used ONLY to validate the new full-screen capture/composite pipeline
// (DrawFullScreenPass, overlay_hud.cpp) before any real effect (RCAS/FXAA/motion
// blur) is built on top of it -- isolates plumbing bugs from shader-content bugs.
//
// Compiled OFFLINE via the Windows SDK's fxc.exe from
// re_notes/shaders/fullscreen_passthrough.hlsl (source kept in re_notes/ for the
// record, not part of the build) -- embedded directly as a byte array, exactly like
// this project's existing options_blur_ps.h. No runtime shader-compiler dependency.
//
// ps_2_0 (not a newer profile) matches this project's existing shader convention
// and this old engine's own real shader era.
//
// Original HLSL source (fullscreen_passthrough.hlsl):
//   sampler2D tex0 : register(s0);
//   float4 main(float2 uv : TEXCOORD0) : COLOR0
//   {
//       return tex2D(tex0, uv);
//   }
#pragma once

const BYTE g_fullscreenPassthroughPixelShaderBytecode[] =
{
      0,   2, 255, 255, 254, 255,
     31,   0,  67,  84,  65,  66,
     28,   0,   0,   0,  79,   0,
      0,   0,   0,   2, 255, 255,
      1,   0,   0,   0,  28,   0,
      0,   0,   0,   1,   0,   0,
     72,   0,   0,   0,  48,   0,
      0,   0,   3,   0,   0,   0,
      1,   0,   2,   0,  56,   0,
      0,   0,   0,   0,   0,   0,
    116, 101, 120,  48,   0, 171,
    171, 171,   4,   0,  12,   0,
      1,   0,   1,   0,   1,   0,
      0,   0,   0,   0,   0,   0,
    112, 115,  95,  50,  95,  48,
      0,  77, 105,  99, 114, 111,
    115, 111, 102, 116,  32,  40,
     82,  41,  32,  72,  76,  83,
     76,  32,  83, 104,  97, 100,
    101, 114,  32,  67, 111, 109,
    112, 105, 108, 101, 114,  32,
     49,  48,  46,  49,   0, 171,
     31,   0,   0,   2,   0,   0,
      0, 128,   0,   0,   3, 176,
     31,   0,   0,   2,   0,   0,
      0, 144,   0,   8,  15, 160,
     66,   0,   0,   3,   0,   0,
     15, 128,   0,   0, 228, 176,
      0,   8, 228, 160,   1,   0,
      0,   2,   0,   8,  15, 128,
      0,   0, 228, 128, 255, 255,
      0,   0
};
