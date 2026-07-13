// 役割: 視線方向からCubeMapをサンプリングするSkybox用ピクセルシェーダー。
#include "Skybox.hlsli"

TextureCube gTexture : register(t0);
SamplerState gSampler : register(s0);

cbuffer MaterialCB : register(b0)
{
    float4 gMaterialColor;
};

struct PixelShaderOutput
{
    float4 color : SV_TARGET0;
};

PixelShaderOutput main(VertexShaderOutput input)
{
    PixelShaderOutput output;

    float4 textureColor = gTexture.Sample(gSampler, normalize(input.direction));
    output.color = textureColor * gMaterialColor;

    return output;
}
