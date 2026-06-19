#include "Fullscreen.hlsli"

struct BloomParameters
{
    int enabled;
    float threshold;
    float softKnee;
    float intensity;

    float exposure;
    int blurIterations;
    int downsampleScale;
    float blurRadius;

    int toneMapMode;
    int horizontal;
    float2 texelSize;
    float2 padding;
};

ConstantBuffer<BloomParameters> gBloom : register(b0);
Texture2D<float4> gTexture : register(t0);
Texture2D<float4> gBloomTexture : register(t1);
SamplerState gSampler : register(s0);

struct PixelShaderOutput
{
    float4 color : SV_TARGET0;
};

float Luminance(float3 value)
{
    return dot(value, float3(0.2125f, 0.7154f, 0.0721f));
}
