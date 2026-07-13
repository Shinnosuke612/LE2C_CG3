// 役割: JointPaletteを使ってSkinning済み頂点Bufferを生成するCompute Shader。
struct Vertex
{
    float4 position;
    float2 texcoord;
    float3 normal;
};

struct VertexInfluence
{
    float4 weight;
    uint4 jointIndex;
};

struct PaletteWell
{
    row_major float4x4 skeletonSpaceMatrix;
    row_major float4x4 skeletonSpaceInverseTransposeMatrix;
};

struct SkinningInformation
{
    uint numVertices;
};

StructuredBuffer<PaletteWell> gMatrixPalette : register(t0);
StructuredBuffer<Vertex> gInputVertices : register(t1);
StructuredBuffer<VertexInfluence> gInfluences : register(t2);
RWStructuredBuffer<Vertex> gOutputVertices : register(u0);
ConstantBuffer<SkinningInformation> gSkinningInformation : register(b0);

[numthreads(1024, 1, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    uint vertexIndex = DTid.x;
    if (vertexIndex >= gSkinningInformation.numVertices)
    {
        return;
    }

    Vertex input = gInputVertices[vertexIndex];
    VertexInfluence influence = gInfluences[vertexIndex];

    float weightSum =
        influence.weight.x +
        influence.weight.y +
        influence.weight.z +
        influence.weight.w;

    if (weightSum <= 0.0f)
    {
        gOutputVertices[vertexIndex] = input;
        return;
    }

    Vertex skinned;
    skinned.texcoord = input.texcoord;
    skinned.position =
        mul(input.position, gMatrixPalette[influence.jointIndex.x].skeletonSpaceMatrix) * influence.weight.x;
    skinned.position +=
        mul(input.position, gMatrixPalette[influence.jointIndex.y].skeletonSpaceMatrix) * influence.weight.y;
    skinned.position +=
        mul(input.position, gMatrixPalette[influence.jointIndex.z].skeletonSpaceMatrix) * influence.weight.z;
    skinned.position +=
        mul(input.position, gMatrixPalette[influence.jointIndex.w].skeletonSpaceMatrix) * influence.weight.w;
    skinned.position.w = 1.0f;

    skinned.normal =
        mul(input.normal, (float3x3)gMatrixPalette[influence.jointIndex.x].skeletonSpaceInverseTransposeMatrix) * influence.weight.x;
    skinned.normal +=
        mul(input.normal, (float3x3)gMatrixPalette[influence.jointIndex.y].skeletonSpaceInverseTransposeMatrix) * influence.weight.y;
    skinned.normal +=
        mul(input.normal, (float3x3)gMatrixPalette[influence.jointIndex.z].skeletonSpaceInverseTransposeMatrix) * influence.weight.z;
    skinned.normal +=
        mul(input.normal, (float3x3)gMatrixPalette[influence.jointIndex.w].skeletonSpaceInverseTransposeMatrix) * influence.weight.w;
    skinned.normal = normalize(skinned.normal);

    gOutputVertices[vertexIndex] = skinned;
}
