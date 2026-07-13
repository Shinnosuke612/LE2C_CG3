// 役割: 元画像とBloom画像を合成するピクセルシェーダー。
#include "Bloom.hlsli"

float3 ToneMapAces(float3 color)
{
    const float a = 2.51f;
    const float b = 0.03f;
    const float c = 2.43f;
    const float d = 0.59f;
    const float e = 0.14f;
    return saturate((color * (a * color + b)) / (color * (c * color + d) + e));
}

float3 ToneMapReinhard(float3 color)
{
    return color / (color + 1.0f);
}

PixelShaderOutput main(VertexShaderOutput input)
{
    float3 sceneColor = gTexture.Sample(gSampler, input.texcoord).rgb;
    float3 bloomColor = gBloomTexture.Sample(gSampler, input.texcoord).rgb;
    if (gBloom.enabled != 0)
    {
        sceneColor += bloomColor * gBloom.intensity;
    }

    sceneColor *= max(gBloom.exposure, 0.0f);
    float3 mapped = gBloom.toneMapMode == 1
        ? ToneMapReinhard(sceneColor)
        : ToneMapAces(sceneColor);

    PixelShaderOutput output;
    output.color = float4(mapped, 1.0f);
    return output;
}
