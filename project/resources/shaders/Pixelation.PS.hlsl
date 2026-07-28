#include "Fullscreen.hlsli"

Texture2D<float4> gTexture : register(t0);
SamplerState gSampler : register(s0);

cbuffer PostProcessParameters : register(b0)
{
    float4 unused0[27];
    float4 pixelationParams;
};

float4 main(VertexShaderOutput input) : SV_TARGET0 {
    const float blockSize = max(pixelationParams.x, 1.0f);
    const float2 textureSize = max(pixelationParams.yz, 1.0f);
    const float2 pixel = saturate(input.texcoord) * textureSize;
    const float2 blockOrigin = floor(pixel / blockSize) * blockSize;
    const float2 blockCenter = min(blockOrigin + blockSize * 0.5f, textureSize - 0.5f);
    const float2 sampleUv = saturate(blockCenter / textureSize);
    return gTexture.Sample(gSampler, sampleUv);
}
