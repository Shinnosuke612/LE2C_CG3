#include "Skybox.hlsli"

cbuffer TransformationMatrixCB : register(b0)
{
    float4x4 gInverseViewProjection;
    float4 gCameraPosition;
};

VertexShaderOutput main(uint vertexId : SV_VertexID)
{
    static const float2 kPositions[3] =
    {
        float2(-1.0f, -1.0f),
        float2(-1.0f,  3.0f),
        float2( 3.0f, -1.0f),
    };

    VertexShaderOutput output;

    const float2 screenPosition = kPositions[vertexId];
    output.position = float4(screenPosition, 0.0f, 1.0f);

    float4 farWorld = mul(
        float4(screenPosition, 1.0f, 1.0f),
        gInverseViewProjection
    );
    farWorld.xyz /= farWorld.w;
    output.direction = normalize(farWorld.xyz - gCameraPosition.xyz);

    return output;
}
