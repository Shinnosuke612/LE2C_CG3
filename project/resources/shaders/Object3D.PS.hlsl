#include "Object3D.hlsli"

Texture2D gTexture : register(t0);
SamplerState gSampler : register(s0);

static const int kMaxPointLights = 16;
static const int kMaxSpotLights = 8;

cbuffer MaterialCB : register(b0)
{
    float4 gMaterialColor;
    int gEnableLighting;
    float3 gMaterialPadding;
    float4x4 gUvTransform;
};

struct DirectionalLight
{
    float4 color;
    float3 direction;
    float intensity;
    int enable;
    float3 padding;
};

struct PointLight
{
    float4 color;
    float3 position;
    float intensity;
    float radius;
    float decay;
    int enable;
    float padding;
};

struct SpotLight
{
    float4 color;
    float3 position;
    float intensity;
    float3 direction;
    float distance;
    float decay;
    float cosAngle;
    float cosFalloffStart;
    int enable;
};

cbuffer LightingCB : register(b1)
{
    DirectionalLight gDirectionalLight;

    int gPointLightCount;
    int gSpotLightCount;
    float2 gLightingPadding;

    PointLight gPointLights[kMaxPointLights];
    SpotLight gSpotLights[kMaxSpotLights];
};

struct PixelShaderOutput
{
    float4 color : SV_TARGET0;
};

float3 CalcHalfLambertDiffuse(
    float3 baseColor,
    float3 lightColor,
    float lightIntensity,
    float3 normal,
    float3 toLight,
    float attenuation)
{
    float NdotL = dot(normal, toLight);
    float halfLambert = pow(saturate(NdotL * 0.5f + 0.5f), 2.0f);

    return baseColor * lightColor * lightIntensity * halfLambert * attenuation;
}

PixelShaderOutput main(VertexShaderOutput input)
{
    PixelShaderOutput output;

    float4 transformedUV = mul(float4(input.texcoord, 0.0f, 1.0f), gUvTransform);
    float4 textureColor = gTexture.Sample(gSampler, transformedUV.xy);

    if (textureColor.a <= 0.5f)
    {
        discard;
    }

    if (gEnableLighting != 0)
    {
        float3 normal = normalize(input.normal);
        float3 baseColor = gMaterialColor.rgb * textureColor.rgb;

        float3 result = float3(0.0f, 0.0f, 0.0f);

        if (gDirectionalLight.enable != 0)
        {
            float3 toLight = normalize(-gDirectionalLight.direction);

            result += CalcHalfLambertDiffuse(
                baseColor,
                gDirectionalLight.color.rgb,
                gDirectionalLight.intensity,
                normal,
                toLight,
                1.0f
            );
        }

        for (int i = 0; i < gPointLightCount; ++i)
        {
            PointLight light = gPointLights[i];

            if (light.enable == 0)
            {
                continue;
            }

            float3 pointToLight = light.position - input.worldPosition;
            float distanceToLight = length(pointToLight);
            float radius = max(light.radius, 0.001f);

            if (distanceToLight < radius)
            {
                float3 toLight = pointToLight / max(distanceToLight, 0.001f);
                float attenuation = pow(
                    saturate(1.0f - distanceToLight / radius),
                    max(light.decay, 0.001f)
                );

                result += CalcHalfLambertDiffuse(
                    baseColor,
                    light.color.rgb,
                    light.intensity,
                    normal,
                    toLight,
                    attenuation
                );
            }
        }

        for (int i = 0; i < gSpotLightCount; ++i)
        {
            SpotLight light = gSpotLights[i];

            if (light.enable == 0)
            {
                continue;
            }

            float3 spotToLight = light.position - input.worldPosition;
            float distanceToLight = length(spotToLight);
            float spotDistance = max(light.distance, 0.001f);

            if (distanceToLight < spotDistance)
            {
                float3 toLight = spotToLight / max(distanceToLight, 0.001f);

                float3 fromLightToPixel = -toLight;
                float spotCos = dot(normalize(light.direction), fromLightToPixel);

                float angleFactor = smoothstep(
                    light.cosAngle,
                    light.cosFalloffStart,
                    spotCos
                );

                float distanceFactor = pow(
                    saturate(1.0f - distanceToLight / spotDistance),
                    max(light.decay, 0.001f)
                );

                float attenuation = angleFactor * distanceFactor;

                result += CalcHalfLambertDiffuse(
                    baseColor,
                    light.color.rgb,
                    light.intensity,
                    normal,
                    toLight,
                    attenuation
                );
            }
        }

        output.color.rgb = result;
        output.color.a = gMaterialColor.a * textureColor.a;
    }
    else
    {
        output.color = gMaterialColor * textureColor;
    }

    if (output.color.a == 0.0f)
    {
        discard;
    }

    return output;
}