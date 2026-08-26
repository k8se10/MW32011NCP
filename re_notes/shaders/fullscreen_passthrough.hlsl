// fullscreen_passthrough.hlsl -- Phase A foundation shader for the visual-suite plan
// (approved plan, C:\Users\kyesa\.claude\plans\twinkly-tickling-gem.md). A trivial
// "output = input" pixel shader, used ONLY to validate the new full-screen
// capture/composite pipeline (DrawFullScreenPass, overlay_hud.cpp) before any real
// effect (RCAS/FXAA/motion blur) is built on top of it -- isolates plumbing bugs
// from shader-content bugs, per the plan's own explicit verification step.
//
// Compiled offline via fxc.exe (Windows SDK), ps_2_0 (matches this project's
// existing options_blur_ps.h convention -- this old engine's own real shader era).
sampler2D tex0 : register(s0);

float4 main(float2 uv : TEXCOORD0) : COLOR0
{
    return tex2D(tex0, uv);
}
