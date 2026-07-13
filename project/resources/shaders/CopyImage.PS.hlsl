// 役割: 入力テクスチャをそのまま出力するフルスクリーンコピー用ピクセルシェーダー。
#include "Fullscreen.hlsli"

Texture2D<float4> gTexture : register(t0);
SamplerState gSampler : register(s0);

float4 main(VertexShaderOutput input) : SV_TARGET0
{
    return gTexture.Sample(gSampler, input.texcoord);
}
