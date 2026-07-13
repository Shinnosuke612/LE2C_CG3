// 役割: パーティクルQuadをCamera正面へ展開する頂点シェーダー。
#include "Particle.hlsli"

struct TransformationMatrix
{
    float32_t4x4 WVP;
    float32_t4x4 World;
    float32_t4 color;
};
StructuredBuffer<TransformationMatrix> gTransformationMatices : register(t0);

struct VertexShaderInput
{
    float32_t4 position : POSITION0;
    float32_t2 texcoord : TEXCOORD0;
    float32_t3 normal : NORMAL0;
    float32_t4 color : COLOR0;
};

VertexShaderOutput main(VertexShaderInput input,uint32_t instanceId : SV_InstanceID)
{
    VertexShaderOutput output;
    output.position = mul(input.position, gTransformationMatices[instanceId].WVP);
    output.texcoord = input.texcoord;
    output.normal = normalize(mul(input.normal, (float32_t3x3) gTransformationMatices[instanceId].World));
    output.color = gTransformationMatices[instanceId].color * input.color;
    return output;
}

