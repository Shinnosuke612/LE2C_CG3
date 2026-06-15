#include "Fullscreen.hlsli"

Texture2D<float4> gTexture : register(t0);
SamplerState gSampler : register(s0);

struct VignetteParameters
{
    float scale;
    float power;
    float intensity;
    float padding;
};
ConstantBuffer<VignetteParameters> gVignette : register(b0);

float4 main(VertexShaderOutput input) : SV_TARGET0
{
    const float4 textureColor = gTexture.Sample(gSampler, input.texcoord);

    const float2 correct = input.texcoord * (1.0f - input.texcoord);
    float vignette = correct.x * correct.y * gVignette.scale;
    vignette = saturate(pow(saturate(vignette), gVignette.power));
    vignette = lerp(1.0f, vignette, saturate(gVignette.intensity));

    return float4(textureColor.rgb * vignette, textureColor.a);
}
