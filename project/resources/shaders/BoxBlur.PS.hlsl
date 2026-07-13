// 役割: 画面全体へ単純なBox Blurを適用するピクセルシェーダー。
#include "Fullscreen.hlsli"

Texture2D<float4> gTexture : register(t0);
SamplerState gSampler : register(s0);

struct PostProcessParameters
{
    float vignetteScale;
    float vignettePower;
    float vignetteIntensity;
    float blurStrength;
    uint blurRadius;
    float gaussianSigma;
    float2 padding;
};
ConstantBuffer<PostProcessParameters> gParameters : register(b0);

float4 main(VertexShaderOutput input) : SV_TARGET0
{
    uint width;
    uint height;
    gTexture.GetDimensions(width, height);
    const float2 texelSize = rcp(float2(width, height));

    const int radius = clamp(int(gParameters.blurRadius), 1, 2);
    float4 blurredColor = 0.0f;
    float sampleCount = 0.0f;

    for (int y = -radius; y <= radius; ++y)
    {
        for (int x = -radius; x <= radius; ++x)
        {
            const float2 offset = float2(x, y) * texelSize;
            blurredColor += gTexture.Sample(gSampler, input.texcoord + offset);
            sampleCount += 1.0f;
        }
    }

    blurredColor /= sampleCount;
    const float4 originalColor = gTexture.Sample(gSampler, input.texcoord);
    return lerp(originalColor, blurredColor, saturate(gParameters.blurStrength));
}
