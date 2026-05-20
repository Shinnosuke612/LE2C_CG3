#include "Object3D.hlsli"

Texture2D gTexture : register(t0);
SamplerState gSampler : register(s0);

cbuffer MaterialCB : register(b0)
{
    float4 gMaterialColor;
    int gEnableLighting;
    float3 gMaterialPadding;
    float4x4 gUvTransform;
    float gMaterialShininess;
    float3 gMaterialPadding2;
};

cbuffer LightingCB : register(b1)
{
    // DirectionalLight
    float4 gDirectionalLightColor;
    float3 gDirectionalLightDirection;
    float gDirectionalLightIntensity;
    int gDirectionalLightEnable;
    float3 gDirectionalLightPadding;

    // PointLight
    float4 gPointLightColor;
    float3 gPointLightPosition;
    float gPointLightIntensity;
    float gPointLightRadius;
    float gPointLightDecay;
    int gPointLightEnable;
    float gPointLightPadding;

    // SpotLight
    float4 gSpotLightColor;
    float3 gSpotLightPosition;
    float gSpotLightIntensity;
    float3 gSpotLightDirection;
    float gSpotLightDistance;
    float gSpotLightDecay;
    float gSpotLightCosAngle;
    float gSpotLightCosFalloffStart;
    int gSpotLightEnable;
};

cbuffer CameraCB : register(b2)
{
    float3 gCameraWorldPosition;
    float gCameraPadding;
};

struct PixelShaderOutput
{
    float4 color : SV_TARGET0;
};

float3 CalcLight(
    float3 baseColor,
    float3 lightColor,
    float lightIntensity,
    float3 toLight,
    float attenuation,
    float3 normal,
    float3 toEye)
{
    float NdotL = dot(normal, toLight);
    float halfLambert = pow(saturate(NdotL * 0.5f + 0.5f), 2.0f);

    float3 diffuse =
        baseColor *
        lightColor *
        halfLambert *
        lightIntensity *
        attenuation;

    float3 halfVector = normalize(toLight + toEye);
    float NdotH = saturate(dot(normal, halfVector));
    float specularPow = pow(NdotH, max(gMaterialShininess, 1.0f));

    float3 specular =
        lightColor *
        lightIntensity *
        specularPow *
        attenuation;

    return diffuse + specular;
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

    float3 baseColor = gMaterialColor.rgb * textureColor.rgb;

    if (gEnableLighting != 0)
    {
        float3 normal = normalize(input.normal);
        float3 toEye = normalize(gCameraWorldPosition - input.worldPosition);

        float3 result = float3(0.0f, 0.0f, 0.0f);

        // DirectionalLight
        if (gDirectionalLightEnable != 0)
        {
            // direction は「光が進む向き」として扱う
            float3 toLight = normalize(-gDirectionalLightDirection);

            result += CalcLight(
                baseColor,
                gDirectionalLightColor.rgb,
                gDirectionalLightIntensity,
                toLight,
                1.0f,
                normal,
                toEye
            );
        }

        // PointLight
        if (gPointLightEnable != 0)
        {
            float3 pointToLight = gPointLightPosition - input.worldPosition;
            float distanceToPointLight = length(pointToLight);
            float radius = max(gPointLightRadius, 0.001f);

            if (distanceToPointLight < radius)
            {
                float3 toLight = pointToLight / max(distanceToPointLight, 0.001f);
                float attenuation = pow(saturate(1.0f - distanceToPointLight / radius), gPointLightDecay);

                result += CalcLight(
                    baseColor,
                    gPointLightColor.rgb,
                    gPointLightIntensity,
                    toLight,
                    attenuation,
                    normal,
                    toEye
                );
            }
        }

        // SpotLight
        if (gSpotLightEnable != 0)
        {
            float3 spotToLight = gSpotLightPosition - input.worldPosition;
            float distanceToSpotLight = length(spotToLight);
            float spotDistance = max(gSpotLightDistance, 0.001f);

            if (distanceToSpotLight < spotDistance)
            {
                float3 toLight = spotToLight / max(distanceToSpotLight, 0.001f);

                // direction は「ライトが照らす向き」として扱う
                float3 fromLightToPixel = -toLight;
                float spotCos = dot(normalize(gSpotLightDirection), fromLightToPixel);

                float spotFactor = smoothstep(
                    gSpotLightCosAngle,
                    gSpotLightCosFalloffStart,
                    spotCos
                );

                float distanceFactor = pow(saturate(1.0f - distanceToSpotLight / spotDistance), gSpotLightDecay);
                float attenuation = spotFactor * distanceFactor;

                result += CalcLight(
                    baseColor,
                    gSpotLightColor.rgb,
                    gSpotLightIntensity,
                    toLight,
                    attenuation,
                    normal,
                    toEye
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