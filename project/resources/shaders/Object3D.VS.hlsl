#include "Object3D.hlsli"

cbuffer TransformationMatrixCB : register(b0)
{
    float4x4 gWVP;
    float4x4 gWorld;
};

VertexShaderOutput main(VertexShaderInput input)
{
    VertexShaderOutput output;

    output.position = mul(input.position, gWVP);
    output.texcoord = input.texcoord;
    output.normal = normalize(mul(input.normal, (float3x3) gWorld));
    output.worldPosition = mul(input.position, gWorld).xyz;

    return output;
}