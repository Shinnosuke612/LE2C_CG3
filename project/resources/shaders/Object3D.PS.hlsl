#include "Object3D.hlsli"

Texture2D<float32_t4> gTexture : register(t0);
SamplerState gSampler : register(s0);

struct Material
{
    float32_t4 color;
    int32_t enableLighting;
    float32_t4x4 uvTransform;
    float32_t shininess;
};
ConstantBuffer<Material> gMaterial : register(b0);

struct DirectionalLight
{
    float32_t4 color;
    float32_t3 direction;
    float intensity;
};
ConstantBuffer<DirectionalLight> gDirectionalLight : register(b1);

struct Camera
{
    float32_t3 worldPosition;
};
ConstantBuffer<Camera> gCamera : register(b2);

struct PixelShaderOutput
{
    float32_t4 color : SV_TARGET0;
};

PixelShaderOutput main(VertexShaderOutput input)
{
    PixelShaderOutput output;

    // UVTransform を適用してサンプリング
    float32_t4 transformedUV = mul(float32_t4(input.texcoord, 0.0f, 1.0f), gMaterial.uvTransform);
    float32_t4 textureColor = gTexture.Sample(gSampler, transformedUV.xy);

    // 透明破棄（ここは好みで閾値調整）
    if (textureColor.a <= 0.5f)
    {
        discard;
    }

    float32_t4 baseColor = gMaterial.color * textureColor;

    // ライティング無効ならそのまま
    if (gMaterial.enableLighting == 0)
    {
        output.color = baseColor;
        return output;
    }

    // ---- ここから資料の流れ（Camera方向→反射→pow） ----
    float32_t3 N = normalize(input.normal);

    // 拡散反射（あなたの式のまま：cos を作る）
    float NdotL = dot(N, -gDirectionalLight.direction);
    float cosTerm = pow(NdotL * 0.5f + 0.5f, 2.0f);

    float32_t3 diffuse =
        baseColor.rgb * gDirectionalLight.color.rgb * cosTerm * gDirectionalLight.intensity;

    // Camera 方向
    float32_t3 toEye = normalize(gCamera.worldPosition - input.worldPosition);

    // 反射ベクトル（資料通り reflect + pow）
    float32_t3 reflectLight = reflect(gDirectionalLight.direction, N);
    float RdotE = dot(reflectLight, toEye);
    float specularPow = pow(saturate(RdotE), gMaterial.shininess);

    float32_t3 specular =
        gDirectionalLight.color.rgb * gDirectionalLight.intensity * specularPow * float32_t3(1.0f, 1.0f, 1.0f);

    output.color.rgb = diffuse + specular;
    output.color.a = gMaterial.color.a * textureColor.a;
    return output;
}
