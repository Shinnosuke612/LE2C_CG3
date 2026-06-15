#include "Fullscreen.hlsli"

Texture2D<float4> gTexture : register(t0);
SamplerState gSampler : register(s0);

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
    float dissolveThreshold;
    float dissolveEdgeWidth;
    float2 dissolvePadding;
    float4 dissolveEdgeColor;
    float noiseTime;
    float noiseAmount;
    float noiseScale;
    float noiseSeed;
};

ConstantBuffer<PostEffectParameters> gParameters : register(b0);

float Random(float2 seed)
{
    return frac(
        sin(dot(seed, float2(12.9898f, 78.233f))) * 43758.5453f
    );
}

float4 main(VertexShaderOutput input) : SV_TARGET0
{
    uint width;
    uint height;
    gTexture.GetDimensions(width, height);

    const float scale = max(gParameters.noiseScale, 0.0001f);
    const float2 pixel =
        floor(input.texcoord * float2(width, height) / scale);
    const float random = Random(
        pixel + float2(
            gParameters.noiseTime * 37.0f + gParameters.noiseSeed,
            gParameters.noiseTime * 91.0f - gParameters.noiseSeed
        )
    );

    const float4 sourceColor = gTexture.Sample(gSampler, input.texcoord);
    const float noiseMultiplier = lerp(
        1.0f,
        random,
        saturate(gParameters.noiseAmount)
    );
    return float4(sourceColor.rgb * noiseMultiplier, sourceColor.a);
}
