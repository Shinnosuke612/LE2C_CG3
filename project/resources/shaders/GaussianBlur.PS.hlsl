// 役割: 画面全体へガウスぼかしを適用するピクセルシェーダー。
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

float Gaussian(float x, float y, float sigma)
{
    const float sigmaSquared = sigma * sigma;
    const float exponent = -(x * x + y * y) / (2.0f * sigmaSquared);
    return exp(exponent) / (2.0f * 3.14159265f * sigmaSquared);
}

float4 main(VertexShaderOutput input) : SV_TARGET0
{
    uint width;
    uint height;
    gTexture.GetDimensions(width, height);
    const float2 texelSize = rcp(float2(width, height));

    const int radius = clamp(int(gParameters.blurRadius), 1, 2);
    const float sigma = max(gParameters.gaussianSigma, 0.01f);
    float4 blurredColor = 0.0f;
    float totalWeight = 0.0f;

    for (int y = -radius; y <= radius; ++y)
    {
        for (int x = -radius; x <= radius; ++x)
        {
            const float weight = Gaussian(float(x), float(y), sigma);
            const float2 offset = float2(x, y) * texelSize;
            blurredColor +=
                gTexture.Sample(gSampler, input.texcoord + offset) * weight;
            totalWeight += weight;
        }
    }

    blurredColor *= rcp(totalWeight);
    const float4 originalColor = gTexture.Sample(gSampler, input.texcoord);
    return lerp(originalColor, blurredColor, saturate(gParameters.blurStrength));
}
