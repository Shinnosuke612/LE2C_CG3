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
    const float4 textureColor = gTexture.Sample(gSampler, input.texcoord);

    const float2 correct = input.texcoord * (1.0f - input.texcoord);
    float vignette = correct.x * correct.y * gParameters.vignetteScale;
    vignette = saturate(pow(saturate(vignette), gParameters.vignettePower));
    vignette = lerp(
        1.0f,
        vignette,
        saturate(gParameters.vignetteIntensity)
    );

    return float4(textureColor.rgb * vignette, textureColor.a);
}
