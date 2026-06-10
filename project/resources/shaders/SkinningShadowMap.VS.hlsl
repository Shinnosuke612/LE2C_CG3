struct VertexShaderInput
{
    float4 position : POSITION0;
    float2 texcoord : TEXCOORD0;
    float3 normal : NORMAL0;
    float4 weight : WEIGHT0;
    uint4 jointIndex : INDEX0;
};

struct PaletteWell
{
    row_major float4x4 skeletonSpaceMatrix;
    row_major float4x4 skeletonSpaceInverseTransposeMatrix;
};

cbuffer ShadowTransformationMatrixCB : register(b0)
{
    float4x4 gLightWVP;
};

StructuredBuffer<PaletteWell> gMatrixPalette : register(t0);

float4 main(VertexShaderInput input) : SV_POSITION
{
    float weightSum =
        input.weight.x +
        input.weight.y +
        input.weight.z +
        input.weight.w;

    float4 skinnedPosition = input.position;
    if (weightSum > 0.0f)
    {
        skinnedPosition =
            mul(input.position, gMatrixPalette[input.jointIndex.x].skeletonSpaceMatrix) * input.weight.x;
        skinnedPosition +=
            mul(input.position, gMatrixPalette[input.jointIndex.y].skeletonSpaceMatrix) * input.weight.y;
        skinnedPosition +=
            mul(input.position, gMatrixPalette[input.jointIndex.z].skeletonSpaceMatrix) * input.weight.z;
        skinnedPosition +=
            mul(input.position, gMatrixPalette[input.jointIndex.w].skeletonSpaceMatrix) * input.weight.w;
        skinnedPosition.w = 1.0f;
    }

    return mul(skinnedPosition, gLightWVP);
}
