// 役割: フルスクリーン描画で共有する頂点出力とテクスチャサンプリングを定義する。
struct VertexShaderOutput
{
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD0;
};
