// options_blur.hlsl -- 9-tap weighted blur, ps_2_0 (matches this old engine's own
// shader model era). Compiled OFFLINE via fxc.exe and embedded as bytecode -- no
// D3DX/shader-compiler dependency at runtime, matching this project's standing
// "no extra runtime dependencies" policy. Sampled against a texture the caller has
// already downsampled (StretchRect) from the real backbuffer, so texelSize.xy
// (1/captureWidth, 1/captureHeight) already represents several real screen pixels
// per texel -- combined with this shader's own 9-tap spread, gives a real, properly
// weighted blur rather than a bare bilinear stretch.
sampler2D tex0 : register(s0);
float4 texelSize : register(c0); // .xy = (1/captureTextureWidth, 1/captureTextureHeight)

float4 main(float2 uv : TEXCOORD0) : COLOR0
{
    float2 t = texelSize.xy;
    float4 c = tex2D(tex0, uv) * 0.28;
    c += tex2D(tex0, uv + float2( t.x,  0.0)) * 0.11;
    c += tex2D(tex0, uv + float2(-t.x,  0.0)) * 0.11;
    c += tex2D(tex0, uv + float2( 0.0,  t.y)) * 0.11;
    c += tex2D(tex0, uv + float2( 0.0, -t.y)) * 0.11;
    c += tex2D(tex0, uv + float2( t.x,  t.y)) * 0.07;
    c += tex2D(tex0, uv + float2(-t.x,  t.y)) * 0.07;
    c += tex2D(tex0, uv + float2( t.x, -t.y)) * 0.07;
    c += tex2D(tex0, uv + float2(-t.x, -t.y)) * 0.07;
    return c;
}
