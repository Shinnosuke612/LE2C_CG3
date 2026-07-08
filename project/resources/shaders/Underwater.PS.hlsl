#include "Fullscreen.hlsli"

Texture2D<float4> gTexture : register(t0);
Texture2D<float> gDepthTexture : register(t1);
SamplerState gSampler : register(s0);
SamplerState gPointSampler : register(s1);

struct PostProcessParameters
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
    float dofFocusDistance;
    float dofFocusRange;
    float dofNearStrength;
    float dofFarStrength;
    float dofMaxRadius;
    float3 dofPadding;
    float4 underwaterTintColor;
    float4 underwaterParams;
    float4 cameraUpTime;
};
ConstantBuffer<PostProcessParameters> gParameters : register(b0);

float ViewDepth(float ndcDepth)
{
    const float nearClip = max(gParameters.cameraNear, 0.0001f);
    const float farClip = max(gParameters.cameraFar, nearClip + 0.0001f);
    return nearClip * farClip /
        max(farClip - ndcDepth * (farClip - nearClip), 0.0001f);
}

float Wave(float2 uv, float time)
{
    const float a = sin((uv.x * 18.0f + uv.y * 7.0f) + time * 1.7f);
    const float b = sin((uv.x * -9.0f + uv.y * 23.0f) + time * 2.3f);
    return (a + b) * 0.5f;
}

float4 main(VertexShaderOutput input) : SV_TARGET0
{
    const float intensity = saturate(gParameters.underwaterParams.x);
    if (intensity <= 0.0001f)
    {
        return gTexture.Sample(gSampler, input.texcoord);
    }

    const float time = gParameters.cameraUpTime.w;
    const float distortionStrength = gParameters.underwaterParams.z;
    const float wave = Wave(input.texcoord, time);
    const float wave2 = Wave(input.texcoord.yx + 0.37f, time * 0.83f);
    const float2 distortion =
        float2(wave, wave2) * distortionStrength * intensity;

    const float2 uv = saturate(input.texcoord + distortion);
    const float4 sourceColor = gTexture.Sample(gSampler, uv);
    const float depth = ViewDepth(gDepthTexture.Sample(gPointSampler, uv));
    const float fogDensity = max(gParameters.underwaterParams.y, 0.0f);
    const float depthFog =
        saturate((1.0f - exp(-depth * fogDensity)) * intensity);

    const float surfaceLight =
        saturate(1.0f - input.texcoord.y) *
        (0.55f + 0.45f * saturate(wave * 0.5f + 0.5f));
    const float3 tintColor = gParameters.underwaterTintColor.rgb;
    const float3 fogged = lerp(sourceColor.rgb, tintColor, depthFog);
    const float3 lightInWater = tintColor * surfaceLight * 0.12f * intensity;

    return float4(fogged + lightInWater, sourceColor.a);
}
