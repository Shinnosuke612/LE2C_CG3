// 役割: 深度と法線の差分から輪郭線を合成するピクセルシェーダー。
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
};
ConstantBuffer<PostProcessParameters> gParameters : register(b0);

static const float kHorizontalKernel[3][3] =
{
    { -1.0f / 6.0f, 0.0f, 1.0f / 6.0f },
    { -1.0f / 6.0f, 0.0f, 1.0f / 6.0f },
    { -1.0f / 6.0f, 0.0f, 1.0f / 6.0f }
};

static const float kVerticalKernel[3][3] =
{
    { -1.0f / 6.0f, -1.0f / 6.0f, -1.0f / 6.0f },
    {  0.0f,         0.0f,         0.0f },
    {  1.0f / 6.0f,  1.0f / 6.0f,  1.0f / 6.0f }
};

float Luminance(float3 color)
{
    return dot(color, float3(0.2125f, 0.7154f, 0.0721f));
}

float ViewDepth(float ndcDepth)
{
    const float nearClip = max(gParameters.cameraNear, 0.0001f);
    const float farClip = max(gParameters.cameraFar, nearClip + 0.0001f);
    return nearClip * farClip /
        max(farClip - ndcDepth * (farClip - nearClip), 0.0001f);
}

float4 main(VertexShaderOutput input) : SV_TARGET0
{
    uint width;
    uint height;
    gTexture.GetDimensions(width, height);
    const float2 texelSize =
        rcp(float2(width, height)) * max(gParameters.outlineThickness, 1.0f);

    float2 luminanceDifference = 0.0f;
    float2 depthDifference = 0.0f;
    const float centerDepth = ViewDepth(
        gDepthTexture.Sample(gPointSampler, input.texcoord)
    );

    for (int y = 0; y < 3; ++y)
    {
        for (int x = 0; x < 3; ++x)
        {
            const float2 offset = float2(x - 1, y - 1) * texelSize;
            const float2 texcoord = input.texcoord + offset;
            const float horizontal = kHorizontalKernel[y][x];
            const float vertical = kVerticalKernel[y][x];

            if ((gParameters.outlineFlags & 1u) != 0u)
            {
                const float luminance = Luminance(
                    gTexture.Sample(gSampler, texcoord).rgb
                );
                luminanceDifference +=
                    float2(horizontal, vertical) * luminance;
            }

            if ((gParameters.outlineFlags & 2u) != 0u)
            {
                const float depth = ViewDepth(
                    gDepthTexture.Sample(gPointSampler, texcoord)
                );
                const float relativeDepth =
                    (depth - centerDepth) / max(centerDepth, 0.0001f);
                depthDifference +=
                    float2(horizontal, vertical) * relativeDepth;
            }
        }
    }

    const float edgeStrength =
        length(luminanceDifference) * gParameters.outlineLuminanceWeight +
        length(depthDifference) * gParameters.outlineDepthWeight;
    const float edge = smoothstep(
        gParameters.outlineThreshold,
        gParameters.outlineThreshold +
            max(gParameters.outlineSoftness, 0.0001f),
        edgeStrength
    );

    const float4 sourceColor = gTexture.Sample(gSampler, input.texcoord);
    return lerp(sourceColor, gParameters.outlineColor, edge);
}
