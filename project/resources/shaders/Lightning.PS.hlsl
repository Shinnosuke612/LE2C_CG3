// 役割: 稲妻メッシュの色と発光表現を出力するピクセルシェーダー。
struct PixelShaderInput
{
    float4 position : SV_POSITION;
    float4 color : COLOR0;
};

float4 main(PixelShaderInput input) : SV_TARGET0
{
    return input.color;
}
