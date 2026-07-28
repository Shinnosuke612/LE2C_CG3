// 役割: Cameraの前Frame行列とDepthを使い、前Frame colorからCamera Motion Blurを合成する。
#include "Fullscreen.hlsli"

Texture2D<float4> gTexture : register(t0);
Texture2D<float> gDepthTexture : register(t1);
Texture2D<float4> gHistoryTexture : register(t3);
SamplerState gSampler : register(s0);
SamplerState gPointSampler : register(s1);

cbuffer PostProcessParameters : register(b0)
{
    float4 unused0[28];
    float4x4 motionBlurInverseCurrentViewProjection;
    float4x4 motionBlurPreviousViewProjection;
    float4 motionBlurParams;
    float4 motionBlurTextureSizeHistoryValid;
};

float2 ClipToUv(float2 clipPosition)
{
    return float2(
        clipPosition.x * 0.5f + 0.5f,
        0.5f - clipPosition.y * 0.5f
    );
}

float4 main(VertexShaderOutput input) : SV_TARGET0
{
    const float4 sourceColor = gTexture.Sample(gSampler, input.texcoord);
    const float strength = saturate(motionBlurParams.x);
    if (strength <= 0.0f || motionBlurTextureSizeHistoryValid.z < 0.5f)
    {
        return sourceColor;
    }

    const float depth = gDepthTexture.Sample(gPointSampler, input.texcoord);
    const float4 currentClip = float4(
        input.texcoord.x * 2.0f - 1.0f,
        1.0f - input.texcoord.y * 2.0f,
        depth,
        1.0f
    );
    float4 worldPosition = mul(
        currentClip,
        motionBlurInverseCurrentViewProjection
    );
    worldPosition /= max(abs(worldPosition.w), 0.00001f);

    const float4 previousClip = mul(
        worldPosition,
        motionBlurPreviousViewProjection
    );
    if (abs(previousClip.w) < 0.00001f)
    {
        return sourceColor;
    }

    const float2 previousUv = ClipToUv(previousClip.xy / previousClip.w);
    float2 velocity = input.texcoord - previousUv;
    const float2 textureSize = max(
        motionBlurTextureSizeHistoryValid.xy,
        1.0f
    );
    const float maxRadius = max(motionBlurParams.z, 0.0f);
    const float velocityLengthPixels = length(velocity * textureSize);
    if (velocityLengthPixels <= 0.001f || maxRadius <= 0.0f)
    {
        return sourceColor;
    }
    velocity *= min(velocityLengthPixels, maxRadius) /
        velocityLengthPixels * strength;

    const int sampleCount = clamp((int)motionBlurParams.y, 2, 32);
    float4 accumulatedColor = sourceColor;
    float totalWeight = 1.0f;
    [loop]
    for (int sampleIndex = 1; sampleIndex < sampleCount; ++sampleIndex)
    {
        const float progress = float(sampleIndex) / float(sampleCount - 1);
        const float2 historyUv = saturate(input.texcoord - velocity * progress);
        accumulatedColor += gHistoryTexture.Sample(gSampler, historyUv);
        totalWeight += 1.0f;
    }

    const float4 blurredColor = accumulatedColor / totalWeight;
    return float4(blurredColor.rgb, sourceColor.a);
}
