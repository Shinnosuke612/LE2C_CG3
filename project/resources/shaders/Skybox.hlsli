// 役割: Skybox描画で共有する頂点出力とCubeMap定数を定義する。
struct VertexShaderOutput
{
    float4 position : SV_POSITION;
    float3 direction : TEXCOORD0;
};
