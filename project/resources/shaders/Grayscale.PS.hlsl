// 役割: 入力画像をグレースケール化するピクセルシェーダー。
#include "Fullscreen.hlsli"

Texture2D<float4> gTexture : register(t0);
SamplerState gSampler : register(s0);

float4 main(VertexShaderOutput input) : SV_TARGET0
{
    const float4 textureColor = gTexture.Sample(gSampler, input.texcoord);
    const float luminance = dot(
        textureColor.rgb,
        float3(0.2125f, 0.7154f, 0.0721f)
    );
    return float4(luminance, luminance, luminance, textureColor.a);
}
