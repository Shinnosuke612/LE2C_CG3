struct VertexShaderInput
{
    float3 position : POSITION0;
    float4 color : COLOR0;
};

struct VertexShaderOutput
{
    float4 position : SV_POSITION;
    float4 color : COLOR0;
};

cbuffer CameraCB : register(b0)
{
    float4x4 gViewProjection;
};

VertexShaderOutput main(VertexShaderInput input)
{
    VertexShaderOutput output;
    output.position = mul(float4(input.position, 1.0f), gViewProjection);
    output.color = input.color;
    return output;
}
