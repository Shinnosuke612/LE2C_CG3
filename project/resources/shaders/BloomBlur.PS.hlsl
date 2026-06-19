#include "Bloom.hlsli"

PixelShaderOutput main(VertexShaderOutput input)
{
    static const float kWeights[5] = {
        0.204164f,
        0.180174f,
        0.123832f,
        0.066282f,
        0.027631f
    };

    float2 direction = gBloom.horizontal != 0
        ? float2(gBloom.texelSize.x, 0.0f)
        : float2(0.0f, gBloom.texelSize.y);
    direction *= max(gBloom.blurRadius, 0.0f);

    float3 color = gTexture.Sample(gSampler, input.texcoord).rgb * kWeights[0];
    float weight = kWeights[0];
    [unroll]
    for (int index = 1; index < 5; ++index)
    {
        float2 offset = direction * float(index);
        color += gTexture.Sample(gSampler, input.texcoord + offset).rgb * kWeights[index];
        color += gTexture.Sample(gSampler, input.texcoord - offset).rgb * kWeights[index];
        weight += kWeights[index] * 2.0f;
    }

    PixelShaderOutput output;
    output.color = float4(color / weight, 1.0f);
    return output;
}
