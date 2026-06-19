#include "Particle.hlsli"

Texture2D<float4> gTexture : register(t0);
SamplerState gSampler : register(s0);

struct Material
{
    float4 color;
    int enableLighting;
    float alphaCutoff;
    int flipU;
    int flipV;
    float4x4 uvTransform;
    float emissiveIntensity;
    float3 padding;
};
ConstantBuffer<Material> gMaterial : register(b0);

struct DirectionalLight
{
    float4 color; //<! ライトの色
    float3 direction; //!< ライトの向き
    float intensity; //!< 輝度
};
ConstantBuffer<DirectionalLight> gDirectionalLight : register(b1);

struct PixelShaderOutput
{
    float4 color : SV_TARGET0;
};
PixelShaderOutput main(VertexShaderOutput input)
{
    PixelShaderOutput output;

    float2 texcoord = input.texcoord;
    if (gMaterial.flipU != 0)
    {
        texcoord.x = 1.0f - texcoord.x;
    }
    if (gMaterial.flipV != 0)
    {
        texcoord.y = 1.0f - texcoord.y;
    }

    float4 transformedUV = mul(float4(texcoord, 0.0f, 1.0f), gMaterial.uvTransform);
    float4 textureColor = gTexture.Sample(gSampler, transformedUV.xy);

    if (textureColor.a <= gMaterial.alphaCutoff)
    {
        discard;
    }

    output.color = gMaterial.color * textureColor * input.color;
    output.color.rgb *= max(gMaterial.emissiveIntensity, 0.0f);

    if (output.color.a <= 0.0f)
    {
        discard;
    }

    return output;
}
