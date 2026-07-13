// 役割: ノイズテクスチャを使って画面画像をDissolve表示するピクセルシェーダー。
#include "Fullscreen.hlsli"

Texture2D<float4> gTexture : register(t0);
Texture2D<float4> gMaskTexture : register(t2);
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
};

ConstantBuffer<PostEffectParameters> gParameters : register(b0);

float4 main(VertexShaderOutput input) : SV_TARGET0
{
    const float mask = gMaskTexture.Sample(gSampler, input.texcoord).r;
    clip(mask - gParameters.dissolveThreshold);

    const float edgeWidth = max(gParameters.dissolveEdgeWidth, 0.0001f);
    const float edge = 1.0f - smoothstep(
        gParameters.dissolveThreshold,
        gParameters.dissolveThreshold + edgeWidth,
        mask
    );

    const float4 sourceColor = gTexture.Sample(gSampler, input.texcoord);
    return lerp(sourceColor, gParameters.dissolveEdgeColor, edge);
}
