// 役割: スプライトテクスチャと頂点カラーを合成するピクセルシェーダー。
struct VertexShaderOutput
{
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD0;
};

cbuffer MaterialCB : register(b0)
{
    float4 gMaterialColor;
    int gEnableLighting;
    float3 gMaterialPadding;
    float4x4 gUvTransform;
};

Texture2D gTexture : register(t0);
SamplerState gSampler : register(s0);

struct PixelShaderOutput
{
    float4 color : SV_TARGET0;
};

PixelShaderOutput main(VertexShaderOutput input)
{
    PixelShaderOutput output;

    float4 transformedUV = mul(float4(input.texcoord, 0.0f, 1.0f), gUvTransform);
    float4 textureColor = gTexture.Sample(gSampler, transformedUV.xy);

    if (textureColor.a <= 0.5f)
    {
        discard;
    }

    output.color = gMaterialColor * textureColor;

    if (output.color.a == 0.0f)
    {
        discard;
    }

    return output;
}
