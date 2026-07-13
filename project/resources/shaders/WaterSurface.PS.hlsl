// 役割: 水面の反射、屈折、深度色を合成するピクセルシェーダー。
struct PixelShaderInput
{
    float4 position : SV_POSITION;
    float3 worldPosition : TEXCOORD0;
    float3 normal : TEXCOORD1;
    float2 uv : TEXCOORD2;
    float wave : TEXCOORD3;
};

cbuffer SurfaceCB : register(b0)
{
    float4x4 gViewProjection;
    float4 gCenterTime;
    float4 gHalfSizeAlpha;
    float4 gCameraPositionFresnel;
    float4 gWaveA;
    float4 gWaveB;
    float4 gWaveC;
    float4 gBaseColor;
    float4 gHighlightColorNormal;
};

float4 main(PixelShaderInput input) : SV_TARGET0
{
    float3 normal = normalize(input.normal);
    float3 viewDirection = normalize(gCameraPositionFresnel.xyz - input.worldPosition);
    float facing = saturate(abs(dot(normal, viewDirection)));
    float fresnel = pow(1.0f - facing, gCameraPositionFresnel.w);
    float waveHighlight = saturate(input.wave * 2.5f + 0.35f);

    float3 lightDirection = normalize(float3(-0.35f, 0.85f, -0.25f));
    float softLight = saturate(dot(normal, lightDirection)) * 0.18f;
    float glint = pow(saturate(dot(reflect(-lightDirection, normal), viewDirection)), 42.0f) * 0.25f;

    float3 waterColor = lerp(
        gBaseColor.rgb,
        gHighlightColorNormal.rgb,
        saturate(fresnel * 0.8f + waveHighlight * 0.22f)
    );
    waterColor += softLight + glint;

    float alpha = saturate(gHalfSizeAlpha.w + fresnel * 0.24f + waveHighlight * 0.04f);
    return float4(waterColor, alpha);
}
