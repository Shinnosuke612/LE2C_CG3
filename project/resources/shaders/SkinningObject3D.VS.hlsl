struct VertexShaderInput
{
    float4 position : POSITION0;
    float2 texcoord : TEXCOORD0;
    float3 normal : NORMAL0;
    float4 weight : WEIGHT0;
    uint4 jointIndex : INDEX0;
};

struct VertexShaderOutput
{
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD0;
    float3 normal : NORMAL0;
    float3 worldPosition : WORLDPOSITION0;
};

struct PaletteWell
{
    row_major float4x4 skeletonSpaceMatrix;
    row_major float4x4 skeletonSpaceInverseTransposeMatrix;
};

cbuffer TransformationMatrixCB : register(b0)
{
    float4x4 gWVP;
    float4x4 gWorld;
};

StructuredBuffer<PaletteWell> gMatrixPalette : register(t0);

void Skinning(
    VertexShaderInput input,
    out float4 skinnedPosition,
    out float3 skinnedNormal)
{    
    float weightSum =
        input.weight.x +
        input.weight.y +
        input.weight.z +
        input.weight.w;

    if (weightSum <= 0.0f)
    {
        skinnedPosition = input.position;
        skinnedNormal = input.normal;
        return;
    }

    skinnedPosition =
        mul(input.position, gMatrixPalette[input.jointIndex.x].skeletonSpaceMatrix) * input.weight.x;
    skinnedPosition +=
        mul(input.position, gMatrixPalette[input.jointIndex.y].skeletonSpaceMatrix) * input.weight.y;
    skinnedPosition +=
        mul(input.position, gMatrixPalette[input.jointIndex.z].skeletonSpaceMatrix) * input.weight.z;
    skinnedPosition +=
        mul(input.position, gMatrixPalette[input.jointIndex.w].skeletonSpaceMatrix) * input.weight.w;
    skinnedPosition.w = 1.0f;

    skinnedNormal =
        mul(input.normal, (float3x3)gMatrixPalette[input.jointIndex.x].skeletonSpaceInverseTransposeMatrix) * input.weight.x;
    skinnedNormal +=
        mul(input.normal, (float3x3)gMatrixPalette[input.jointIndex.y].skeletonSpaceInverseTransposeMatrix) * input.weight.y;
    skinnedNormal +=
        mul(input.normal, (float3x3)gMatrixPalette[input.jointIndex.z].skeletonSpaceInverseTransposeMatrix) * input.weight.z;
    skinnedNormal +=
        mul(input.normal, (float3x3)gMatrixPalette[input.jointIndex.w].skeletonSpaceInverseTransposeMatrix) * input.weight.w;
    skinnedNormal = normalize(skinnedNormal);
}

VertexShaderOutput main(VertexShaderInput input)
{
    VertexShaderOutput output;
    float4 skinnedPosition;
    float3 skinnedNormal;
    Skinning(input, skinnedPosition, skinnedNormal);

    output.position = mul(skinnedPosition, gWVP);
    output.texcoord = input.texcoord;
    output.normal = normalize(mul(skinnedNormal, (float3x3)gWorld));
    output.worldPosition = mul(skinnedPosition, gWorld).xyz;
    return output;
}
