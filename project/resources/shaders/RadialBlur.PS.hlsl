#include "Fullscreen.hlsli"

Texture2D<float4> gTexture : register(t0);
SamplerState gSamplerLinear : register(s0);

struct PostEffectParameters
{
    float vignetteScale;
    float vignettePower;
    float vignetteIntensity;
    float blurStrength;
    uint blurRadius;
    float gaussianSigma;
    float2 padding;
    float outlineLuminanceWeight;
    float outlineDepthWeight;
    float outlineThreshold;
    float outlineSoftness;
    float outlineThickness;
    float cameraNear;
    float cameraFar;
    uint outlineFlags;
    float4 outlineColor;
    float2 radialBlurCenter;
    float radialBlurWidth;
    uint radialBlurSamples;
};

ConstantBuffer<PostEffectParameters> gParameters : register(b0);

float4 main(VertexShaderOutput input) : SV_TARGET
{
    const uint sampleCount = clamp(gParameters.radialBlurSamples, 2u, 32u);
    const float2 direction = input.texcoord - gParameters.radialBlurCenter;
    const float denominator = float(sampleCount - 1u);

    float4 color = 0.0f;
    for (uint sampleIndex = 0; sampleIndex < sampleCount; ++sampleIndex)
    {
        const float sampleRatio = float(sampleIndex) / denominator;
        const float2 texcoord =
            input.texcoord - direction * gParameters.radialBlurWidth * sampleRatio;
        color += gTexture.Sample(gSamplerLinear, texcoord);
    }

    return color / float(sampleCount);
}
