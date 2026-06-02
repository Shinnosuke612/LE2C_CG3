struct VertexShaderInput
{
    float4 position : POSITION0;
};

struct VertexShaderOutput
{
    float4 position : SV_POSITION;
    float3 texcoord : TEXCOORD0;
};