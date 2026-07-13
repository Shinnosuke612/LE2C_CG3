// 役割: 深度に応じた焦点外ぼかしを画面全体へ適用するピクセルシェーダー。
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
};
ConstantBuffer<PostProcessParameters> gParameters : register(b0);

float ViewDepth(float ndcDepth)
{
    const float nearClip = max(gParameters.cameraNear, 0.0001f);
    const float farClip = max(gParameters.cameraFar, nearClip + 0.0001f);
    return nearClip * farClip /
        max(farClip - ndcDepth * (farClip - nearClip), 0.0001f);
}

float ComputeBlurFactor(float viewDepth)
{
    const float focusDistance = max(gParameters.dofFocusDistance, 0.0001f);
    const float focusRange = max(gParameters.dofFocusRange, 0.0001f);
    const float distanceFromFocus = viewDepth - focusDistance;
    const float unfocusedDistance =
        max(abs(distanceFromFocus) - focusRange, 0.0f);
    const float focusFalloff =
        saturate(unfocusedDistance / max(focusDistance, 1.0f));

    const float sideStrength = distanceFromFocus < 0.0f
        ? gParameters.dofNearStrength
        : gParameters.dofFarStrength;
    return saturate(focusFalloff * sideStrength);
}

float4 main(VertexShaderOutput input) : SV_TARGET0
{
    uint width;
    uint height;
    gTexture.GetDimensions(width, height);
    const float2 texelSize = rcp(float2(width, height));

    const float depth = ViewDepth(
        gDepthTexture.Sample(gPointSampler, input.texcoord)
    );
    const float blurFactor = ComputeBlurFactor(depth);
    const float4 sourceColor = gTexture.Sample(gSampler, input.texcoord);
    if (blurFactor <= 0.001f)
    {
        return sourceColor;
    }

    const float radiusPixels =
        saturate(gParameters.blurStrength) *
        max(gParameters.dofMaxRadius, 0.0f) *
        blurFactor;
    const int radius = clamp((int)ceil(radiusPixels), 1, 8);

    float4 blurredColor = 0.0f;
    float totalWeight = 0.0f;
    for (int y = -radius; y <= radius; ++y)
    {
        for (int x = -radius; x <= radius; ++x)
        {
            const float2 sampleOffset = float2(x, y);
            const float sampleDistance = length(sampleOffset);
            if (sampleDistance > float(radius) + 0.001f)
            {
                continue;
            }

            const float weight =
                1.0f - saturate(sampleDistance / (float(radius) + 1.0f));
            blurredColor +=
                gTexture.Sample(gSampler, input.texcoord + sampleOffset * texelSize) *
                weight;
            totalWeight += weight;
        }
    }

    blurredColor /= max(totalWeight, 0.0001f);
    return lerp(sourceColor, blurredColor, blurFactor);
}
