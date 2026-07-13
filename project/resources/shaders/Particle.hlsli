// 役割: CPUパーティクル描画で共有する頂点出力と粒子定数を定義する。
struct VertexShaderOutput
{
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD0;
    float3 normal : NORMAL0;
    float4 color : COLOR0;
};
