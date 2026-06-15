#include "CopyImage.hlsli"

static const float4 kPositions[3] =
{
    float4(-1.0f,  1.0f, 0.0f, 1.0f),
    float4( 3.0f,  1.0f, 0.0f, 1.0f),
    float4(-1.0f, -3.0f, 0.0f, 1.0f)
};

static const float2 kTexcoords[3] =
{
    float2(0.0f, 0.0f),
    float2(2.0f, 0.0f),
    float2(0.0f, 2.0f)
};

VertexShaderOutput main(uint vertexId : SV_VertexID)
{
    VertexShaderOutput output;
    output.position = kPositions[vertexId];
    output.texcoord = kTexcoords[vertexId];
    return output;
}
