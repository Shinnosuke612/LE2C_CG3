struct Input { float3 position : POSITION; float4 color : COLOR; };
struct Output { float4 position : SV_POSITION; float4 color : COLOR; };
cbuffer Camera : register(b0) { float4x4 viewProjection; };
Output main(Input input) { Output output; output.position = mul(float4(input.position, 1.0f), viewProjection); output.color = input.color; return output; }
