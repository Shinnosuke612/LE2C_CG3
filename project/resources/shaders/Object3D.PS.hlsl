#include "Object3D.hlsli"

Texture2D<float4> gTexture : register(t0);
SamplerState gSampler : register(s0);

struct Material
{
    float4 color;
    int enableLighting;
    float3 padding;
    float4x4 uvTransform;
    float shininess;
    float3 padding2;
};
ConstantBuffer<Material> gMaterial : register(b0);

struct DirectionalLight
{
    float4 color; //<! ライトの色
    float3 direction; //!< ライトの向き
    float intensity; //!< 輝度
};
ConstantBuffer<DirectionalLight> gDirectionalLight : register(b1);

struct Camera
{
    float3 worldPosition;
};
ConstantBuffer<Camera> gCamera : register(b2);

struct PixelShaderOutput
{
    float4 color : SV_TARGET0;
};
PixelShaderOutput main(VertexShaderOutput input)
{
    PixelShaderOutput output;
    output.color = gMaterial.color;
    float4 textureColor = gTexture.Sample(gSampler, input.texcoord);
    output.color = gMaterial.color * textureColor;
    float4 transformedUV = mul(float4(input.texcoord, 0.0f, 1.0f), gMaterial.uvTransform);
    textureColor = gTexture.Sample(gSampler, transformedUV.xy);
    if (textureColor.a <= 0.5f)
    {
        discard;
    }
    if (gMaterial.enableLighting != 0)
    {
        float3 normal = normalize(input.normal);

    // ライト方向
        float3 lightDirection = normalize(gDirectionalLight.direction);
        float3 toLight = -lightDirection;
    // 拡散反射 Half Lambert
        float NdotL = dot(normal, toLight);
        float cos = pow(saturate(NdotL * 0.5f + 0.5f), 2.0f);

        float3 diffuse =
        gMaterial.color.rgb *
        textureColor.rgb *
        gDirectionalLight.color.rgb *
        cos *
        gDirectionalLight.intensity;

    // カメラ方向
        float3 toEye = normalize(gCamera.worldPosition - input.worldPosition);

    // 鏡面反射
        float3 halfVector = normalize(-gDirectionalLight.direction + toEye);
        float NDotH = dot(normalize(input.normal), halfVector);
        float specularPow = pow(saturate(NDotH), gMaterial.shininess);

        float3 specular =
        gDirectionalLight.color.rgb *
        gDirectionalLight.intensity *
        specularPow *
        float3(1.0f, 1.0f, 1.0f);
        output.color.rgb = diffuse + specular;
        output.color.a = gMaterial.color.a * textureColor.a;
    }
    else
    {
        output.color = gMaterial.color * textureColor;
    }
    if (output.color.a == 0)
    {
        discard;
    }
    return output;
}