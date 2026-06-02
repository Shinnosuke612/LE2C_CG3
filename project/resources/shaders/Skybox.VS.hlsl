#include "Skybox.hlsli"

cbuffer TransformationMatrixCB : register(b0)
{
    float4x4 gWVP;
    float4x4 gWorld;
};

VertexShaderOutput main(VertexShaderInput input)
{
    VertexShaderOutput output;

    output.position = mul(input.position, gWVP).xyww;
    output.texcoord = input.position.xyz;

    return output;
}