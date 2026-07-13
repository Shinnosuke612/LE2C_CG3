// 役割: 明るい領域だけをBloom用に抽出するピクセルシェーダー。
#include "Bloom.hlsli"

PixelShaderOutput main(VertexShaderOutput input)
{
    PixelShaderOutput output;
    float3 color = gTexture.Sample(gSampler, input.texcoord).rgb;
    float luminance = Luminance(color);
    float threshold = max(gBloom.threshold, 0.0001f);
    float knee = threshold * saturate(gBloom.softKnee);
    float soft = 0.0f;
    if (knee > 0.0f)
    {
        soft = saturate((luminance - threshold + knee) / (2.0f * knee));
        soft = soft * soft * knee;
    }
    float contribution = max(luminance - threshold, soft);
    float scale = luminance > 0.0f ? contribution / luminance : 0.0f;
    output.color = float4(color * scale, 1.0f);
    return output;
}
