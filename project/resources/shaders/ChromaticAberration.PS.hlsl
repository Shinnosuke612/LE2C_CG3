#include "Fullscreen.hlsli"
Texture2D<float4> gTexture : register(t0);
SamplerState gSampler : register(s0);
cbuffer PostProcessParameters : register(b0)
{
    float4 unused0[25];
    float4 chromaticCenterIntensity;
    float4 chromaticParams;
};
float4 main(VertexShaderOutput input) : SV_TARGET0 {
    const float2 direction = input.texcoord - chromaticCenterIntensity.xy;
    const float distance = length(direction);
    const float scale = chromaticCenterIntensity.z * pow(distance, max(chromaticParams.x, 0.01f));
    const float2 offset = normalize(direction + 1e-6f) * scale;
    const float4 source = gTexture.Sample(gSampler, input.texcoord);
    return float4(gTexture.Sample(gSampler, saturate(input.texcoord + offset)).r, source.g, gTexture.Sample(gSampler, saturate(input.texcoord - offset)).b, source.a);
}
