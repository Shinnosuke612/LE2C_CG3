struct VertexShaderInput
{
    float4 position : POSITION0;
    float2 texcoord : TEXCOORD0;
    float3 normal : NORMAL0;
};

cbuffer ShadowTransformationMatrixCB : register(b0)
{
    float4x4 gLightWVP;
};

float4 main(VertexShaderInput input) : SV_POSITION
{
    return mul(input.position, gLightWVP);
}
