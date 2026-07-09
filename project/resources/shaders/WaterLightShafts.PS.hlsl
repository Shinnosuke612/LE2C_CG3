#include "Fullscreen.hlsli"

Texture2D<float4> gTexture : register(t0);
Texture2D<float> gDepthTexture : register(t1);
SamplerState gSampler : register(s0);
SamplerState gPointSampler : register(s1);

struct PostProcessParameters
{
    float vignetteScale;
    float vignettePower;
    float vignetteIntensity;
    float blurStrength;
    uint blurRadius;
    float gaussianSigma;
    float2 padding;
    float outlineLuminanceWeight;
    float outlineDepthWeight;
    float outlineThreshold;
    float outlineSoftness;
    float outlineThickness;
    float cameraNear;
    float cameraFar;
    uint outlineFlags;
    float4 outlineColor;
    float2 radialBlurCenter;
    float radialBlurWidth;
    uint radialBlurSamples;
    float dissolveThreshold;
    float dissolveEdgeWidth;
    float2 dissolvePadding;
    float4 dissolveEdgeColor;
    float noiseTime;
    float noiseAmount;
    float noiseScale;
    float noiseSeed;
    float dofFocusDistance;
    float dofFocusRange;
    float dofNearStrength;
    float dofFarStrength;
    float dofMaxRadius;
    float3 dofPadding;
    float4 underwaterTintColor;
    float4 underwaterParams;
    float4 cameraUpTime;
    float4 cameraPositionFovY;
    float4 cameraRightAspect;
    float4 cameraForwardActive;
    float4 waterVolumeCenterActive;
    float4 waterVolumeHalfSizeEdge;
    float4 waterRefractionTintColor;
    float4 waterRefractionParams;
    float4 waterLightColorIntensity;
    float4 waterLightDirectionDensity;
    float4 waterLightParams;
    float4 waterLightNoiseParams;
};
ConstantBuffer<PostProcessParameters> gParameters : register(b0);

float ViewDepth(float ndcDepth)
{
    const float nearClip = max(gParameters.cameraNear, 0.0001f);
    const float farClip = max(gParameters.cameraFar, nearClip + 0.0001f);
    return nearClip * farClip /
        max(farClip - ndcDepth * (farClip - nearClip), 0.0001f);
}

float3 SafeInverseDirection(float3 direction)
{
    return float3(
        direction.x < 0.0f ? -1.0f : 1.0f,
        direction.y < 0.0f ? -1.0f : 1.0f,
        direction.z < 0.0f ? -1.0f : 1.0f
    ) / max(abs(direction), 0.0001f);
}

bool IntersectAabb(
    float3 origin,
    float3 direction,
    float3 boxMin,
    float3 boxMax,
    out float enter,
    out float exit
)
{
    const float3 invDirection = SafeInverseDirection(direction);
    const float3 t0 = (boxMin - origin) * invDirection;
    const float3 t1 = (boxMax - origin) * invDirection;
    const float3 tNear = min(t0, t1);
    const float3 tFar = max(t0, t1);
    enter = max(max(tNear.x, tNear.y), tNear.z);
    exit = min(min(tFar.x, tFar.y), tFar.z);
    return exit >= max(enter, 0.0f);
}

float3 RayDirectionFromUv(
    float2 uv,
    float3 cameraForward,
    float3 cameraRight,
    float3 cameraUp,
    float aspect,
    float tanHalfFovY
)
{
    const float2 ndc = float2(
        uv.x * 2.0f - 1.0f,
        1.0f - uv.y * 2.0f
    );
    return normalize(
        cameraForward +
        cameraRight * ndc.x * aspect * tanHalfFovY +
        cameraUp * ndc.y * tanHalfFovY
    );
}

float HashNoise(float2 value)
{
    return frac(sin(dot(value, float2(12.9898f, 78.233f))) * 43758.5453f);
}

float ValueNoise(float2 value)
{
    const float2 cell = floor(value);
    const float2 local = frac(value);
    const float2 blend = local * local * (3.0f - 2.0f * local);

    const float a = HashNoise(cell);
    const float b = HashNoise(cell + float2(1.0f, 0.0f));
    const float c = HashNoise(cell + float2(0.0f, 1.0f));
    const float d = HashNoise(cell + float2(1.0f, 1.0f));
    return lerp(lerp(a, b, blend.x), lerp(c, d, blend.x), blend.y);
}

float FractalNoise(float2 value)
{
    float sum = 0.0f;
    float amplitude = 0.5f;
    float frequency = 1.0f;
    [unroll]
    for (int octave = 0; octave < 3; ++octave)
    {
        sum += ValueNoise(value * frequency) * amplitude;
        frequency *= 2.03f;
        amplitude *= 0.5f;
    }
    return saturate(sum);
}

float2 LightPlaneUv(float3 position, float3 lightDirection, float scale)
{
    float3 helper = abs(lightDirection.y) < 0.9f
        ? float3(0.0f, 1.0f, 0.0f)
        : float3(1.0f, 0.0f, 0.0f);
    float3 axisA = normalize(cross(helper, lightDirection));
    float3 axisB = normalize(cross(lightDirection, axisA));
    return float2(dot(position, axisA), dot(position, axisB)) *
        max(scale, 0.001f);
}

float CausticPattern(
    float3 position,
    float3 lightDirection,
    float scale,
    float time,
    float warpStrength,
    float noiseScale
)
{
    float2 uv = LightPlaneUv(position, lightDirection, scale);
    noiseScale = max(noiseScale, 0.001f);
    const float2 slowDrift = float2(time * 0.041f, -time * 0.037f);
    const float2 noiseUv = uv * noiseScale;
    const float2 warp = float2(
        FractalNoise(noiseUv * 0.42f + slowDrift),
        FractalNoise(noiseUv.yx * 0.38f - slowDrift * 1.17f)
    ) * 2.0f - 1.0f;
    uv += warp * 1.55f * max(warpStrength, 0.0f);

    const float veinA = abs(sin(
        uv.x * 2.25f +
        sin(uv.y * 1.05f + time * 0.61f) * 1.15f +
        time * 1.05f
    ));
    const float veinB = abs(sin(
        uv.y * 2.85f +
        sin(uv.x * 1.20f - time * 0.53f) * 0.95f -
        time * 0.83f
    ));
    const float veinC = abs(sin(
        dot(uv, float2(0.73f, 1.21f)) * 1.95f +
        FractalNoise(uv * 0.48f * noiseScale + time * 0.05f) * 2.1f
    ));

    const float strands =
        pow(1.0f - min(min(veinA, veinB), veinC), 1.55f);
    const float breakup =
        lerp(0.45f, 1.25f, FractalNoise(uv * 0.58f * noiseScale + time * 0.09f));
    const float broadPatch =
        smoothstep(
            0.18f,
            0.95f,
            FractalNoise(uv * 0.18f * noiseScale - time * 0.03f)
        );
    return saturate(strands * breakup * lerp(0.68f, 1.0f, broadPatch));
}

float ShaftBreakup(
    float3 position,
    float3 lightDirection,
    float scale,
    float time,
    float breakupStrength,
    float noiseScale
)
{
    noiseScale = max(noiseScale, 0.001f);
    float2 uv = LightPlaneUv(position, lightDirection, scale * 0.45f);
    uv += float2(position.y * 0.07f, position.y * -0.041f);
    const float large =
        FractalNoise(
            uv * 0.55f * noiseScale + float2(time * 0.023f, -time * 0.017f)
        );
    const float small =
        FractalNoise(
            uv * 1.73f * noiseScale + float2(-time * 0.051f, time * 0.039f)
        );
    const float breakup = lerp(0.22f, 1.32f, large * 0.72f + small * 0.28f);
    return max(lerp(1.0f, breakup, max(breakupStrength, 0.0f)), 0.0f);
}

float ShaftBandPattern(float3 position, float3 lightDirection, float scale, float time)
{
    float2 uv = LightPlaneUv(position, lightDirection, max(scale * 0.38f, 0.025f));
    uv += float2(position.y * 0.018f, position.y * -0.012f);
    const float bandA = sin(uv.x * 1.35f + uv.y * 0.32f + time * 0.33f);
    const float bandB = sin(uv.y * 1.10f - uv.x * 0.18f - time * 0.27f);
    return saturate(0.70f + bandA * 0.18f + bandB * 0.12f);
}

float4 main(VertexShaderOutput input) : SV_TARGET0
{
    const float4 sourceColor = gTexture.Sample(gSampler, input.texcoord);
    if (gParameters.waterVolumeCenterActive.w <= 0.5f)
    {
        return sourceColor;
    }

    const float intensity = max(gParameters.waterLightColorIntensity.w, 0.0f);
    if (intensity <= 0.0001f)
    {
        return sourceColor;
    }

    const float3 cameraPosition = gParameters.cameraPositionFovY.xyz;
    const float3 cameraRight = normalize(gParameters.cameraRightAspect.xyz);
    const float3 cameraUp = normalize(gParameters.cameraUpTime.xyz);
    const float3 cameraForward = normalize(gParameters.cameraForwardActive.xyz);
    const float fovY = max(gParameters.cameraPositionFovY.w, 0.0001f);
    const float aspect = max(gParameters.cameraRightAspect.w, 0.0001f);
    const float tanHalfFovY = tan(fovY * 0.5f);
    const float3 center = gParameters.waterVolumeCenterActive.xyz;
    const float3 halfSize = max(gParameters.waterVolumeHalfSizeEdge.xyz, 0.001f);
    const float3 boxMin = center - halfSize;
    const float3 boxMax = center + halfSize;

    const float3 rayDirection = RayDirectionFromUv(
        input.texcoord,
        cameraForward,
        cameraRight,
        cameraUp,
        aspect,
        tanHalfFovY
    );

    float enter;
    float exit;
    if (!IntersectAabb(cameraPosition, rayDirection, boxMin, boxMax, enter, exit))
    {
        return sourceColor;
    }

    const float rawDepth = gDepthTexture.SampleLevel(gPointSampler, input.texcoord, 0.0f);
    const float viewDepth = ViewDepth(rawDepth);
    const float forwardAmount = max(dot(rayDirection, cameraForward), 0.0001f);
    const float sceneRayDistance = min(viewDepth / forwardAmount, gParameters.cameraFar);
    const float startDistance = max(enter, gParameters.cameraNear);
    const float endDistance = min(exit, sceneRayDistance);
    const float thickness = endDistance - startDistance;
    if (thickness <= 0.0001f)
    {
        return sourceColor;
    }

    float3 lightDirection = gParameters.waterLightDirectionDensity.xyz;
    if (length(lightDirection) < 0.0001f)
    {
        lightDirection = normalize(float3(-0.25f, -1.0f, 0.18f));
    }
    else
    {
        lightDirection = normalize(lightDirection);
    }
    const float density = max(gParameters.waterLightDirectionDensity.w, 0.0f);
    const float3 lightColor = gParameters.waterLightColorIntensity.rgb;
    const float causticsIntensity = max(gParameters.waterLightParams.x, 0.0f);
    const float causticsScale = max(gParameters.waterLightParams.y, 0.001f);
    const float causticsSpeed = gParameters.waterLightParams.z;
    const float breakupStrength = max(gParameters.waterLightNoiseParams.x, 0.0f);
    const float warpStrength = max(gParameters.waterLightNoiseParams.y, 0.0f);
    const float noiseScale = max(gParameters.waterLightNoiseParams.z, 0.001f);
    const int sampleCount = min(
        max((int)round(gParameters.waterLightParams.w), 4),
        32
    );
    const float time = gParameters.cameraUpTime.w * causticsSpeed;
    const float jitter = HashNoise(input.texcoord * 1024.0f + time) - 0.5f;

    float shaft = 0.0f;
    [loop]
    for (int sampleIndex = 0; sampleIndex < sampleCount; ++sampleIndex)
    {
        const float rate =
            (sampleIndex + 0.5f + jitter * 0.35f) / (float)sampleCount;
        const float sampleDistance = lerp(startDistance, endDistance, saturate(rate));
        const float3 samplePosition =
            cameraPosition + rayDirection * sampleDistance;
        const float depthFade =
            exp(-max(sampleDistance - startDistance, 0.0f) * density);
        const float phase =
            0.35f + 0.65f * pow(
                saturate(dot(-rayDirection, -lightDirection) * 0.5f + 0.5f),
                2.0f
            );
        const float band =
            ShaftBandPattern(samplePosition, lightDirection, causticsScale, time);
        const float breakup =
            ShaftBreakup(
                samplePosition,
                lightDirection,
                causticsScale,
                time,
                breakupStrength,
                noiseScale
            );
        shaft += depthFade * phase * breakup * band;
    }
    shaft /= (float)sampleCount;

    const float3 scenePosition =
        cameraPosition + rayDirection * sceneRayDistance;
    const bool scenePointInWater =
        all(scenePosition >= boxMin) && all(scenePosition <= boxMax);
    const float surfaceCaustic = scenePointInWater
        ? CausticPattern(
            scenePosition,
            lightDirection,
            causticsScale * 1.35f,
            time,
            warpStrength,
            noiseScale
        ) *
            ShaftBreakup(
                scenePosition,
                lightDirection,
                causticsScale,
                time,
                breakupStrength,
                noiseScale
            ) *
            causticsIntensity
        : 0.0f;

    const float waterAmount = saturate(thickness * max(density, 0.015f));
    const float lightAmount =
        (shaft * waterAmount + surfaceCaustic) * intensity;
    return float4(sourceColor.rgb + lightColor * lightAmount, sourceColor.a);
}
