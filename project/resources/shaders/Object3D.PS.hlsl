#include "Object3D.hlsli"

Texture2D gTexture : register(t0);
Texture2DArray gShadowMaps : register(t1);
SamplerState gSampler : register(s0);
SamplerState gShadowSampler : register(s1);

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

struct ShadowInfo
{
    float4x4 viewProjection;
    int enable;
    int mapIndex;
    float bias;
    float normalBias;
    float strength;
    float3 padding;
};

cbuffer ShadowCB : register(b3)
{
    ShadowInfo gDirectionalShadow;
    ShadowInfo gSpotShadows[kMaxSpotLights];
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

float CalcShadowVisibility(ShadowInfo shadow, float3 worldPosition, float3 normal)
{
    if (shadow.enable == 0)
    {
        return 1.0f;
    }

    float3 biasedWorldPosition = worldPosition + normal * shadow.normalBias;
    float4 lightClip = mul(float4(biasedWorldPosition, 1.0f), shadow.viewProjection);

    if (lightClip.w <= 0.0f)
    {
        return 1.0f;
    }

    float3 projected = lightClip.xyz / lightClip.w;
    float2 shadowUV = float2(projected.x * 0.5f + 0.5f, -projected.y * 0.5f + 0.5f);

    if (shadowUV.x < 0.0f || shadowUV.x > 1.0f ||
        shadowUV.y < 0.0f || shadowUV.y > 1.0f ||
        projected.z < 0.0f || projected.z > 1.0f)
    {
        return 1.0f;
    }

    uint width;
    uint height;
    uint elements;
    uint mipLevels;
    gShadowMaps.GetDimensions(0, width, height, elements, mipLevels);
    float2 texelSize = 1.0f / float2(width, height);

    float lit = 0.0f;
    const float receiverDepth = projected.z - shadow.bias;

    [unroll]
    for (int y = -1; y <= 1; ++y)
    {
        [unroll]
        for (int x = -1; x <= 1; ++x)
        {
            float2 offset = float2(x, y) * texelSize;
            float shadowDepth = gShadowMaps.SampleLevel(
                gShadowSampler,
                float3(shadowUV + offset, shadow.mapIndex),
                0.0f
            ).r;

            lit += receiverDepth <= shadowDepth ? 1.0f : 0.0f;
        }
    }

    lit /= 9.0f;
    return lerp(1.0f - shadow.strength, 1.0f, lit);
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
            float shadowVisibility = CalcShadowVisibility(gDirectionalShadow, input.worldPosition, normal);

            result += CalcHalfLambertDiffuse(
                baseColor,
                gDirectionalLight.color.rgb,
                gDirectionalLight.intensity,
                normal,
                toLight,
                shadowVisibility
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
                float shadowVisibility = CalcShadowVisibility(gSpotShadows[i], input.worldPosition, normal);

                result += CalcHalfLambertDiffuse(
                    baseColor,
                    light.color.rgb,
                    light.intensity,
                    normal,
                    toLight,
                    attenuation * shadowVisibility
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
