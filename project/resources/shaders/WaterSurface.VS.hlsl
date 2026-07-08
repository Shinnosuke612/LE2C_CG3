struct VertexShaderInput
{
    float3 position : POSITION0;
    float2 uv : TEXCOORD0;
};

struct VertexShaderOutput
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

static const float kPi = 3.14159265359f;

float3 BasePosition(float2 localXZ)
{
    return float3(
        gCenterTime.x + localXZ.x * gHalfSizeAlpha.x,
        gCenterTime.y + gHalfSizeAlpha.y,
        gCenterTime.z + localXZ.y * gHalfSizeAlpha.z
    );
}

float3 GerstnerOffset(float3 basePosition, float4 wave)
{
    float2 direction = normalize(wave.xy);
    float amplitude = max(wave.z, 0.0001f);
    float wavelength = max(wave.w, 0.05f);
    float frequency = 2.0f * kPi / wavelength;
    float speed = sqrt(9.8f / frequency) * 0.85f;
    float phase = frequency * dot(direction, basePosition.xz) + speed * gCenterTime.w;
    float sineWave = sin(phase);
    float cosineWave = cos(phase);
    float steepness = 0.52f / (frequency * amplitude * 3.0f);

    return float3(
        direction.x * steepness * amplitude * cosineWave,
        amplitude * sineWave,
        direction.y * steepness * amplitude * cosineWave
    );
}

float EdgeDamping(float2 localXZ)
{
    float edgeDistance = min(1.0f - abs(localXZ.x), 1.0f - abs(localXZ.y));
    return smoothstep(0.0f, 0.18f, saturate(edgeDistance));
}

float3 WavePosition(float2 localXZ)
{
    float3 basePosition = BasePosition(localXZ);
    float damping = EdgeDamping(localXZ);
    return
        basePosition +
        damping * (
            GerstnerOffset(basePosition, gWaveA) +
            GerstnerOffset(basePosition, gWaveB) +
            GerstnerOffset(basePosition, gWaveC)
        );
}

VertexShaderOutput main(VertexShaderInput input)
{
    VertexShaderOutput output;

    float2 localXZ = input.position.xz;
    float3 position = WavePosition(localXZ);

    float2 normalStep = float2(0.012f, 0.0f);
    float3 positionX = WavePosition(localXZ + normalStep.xy);
    float3 positionZ = WavePosition(localXZ + normalStep.yx);
    float3 normal = normalize(cross(positionZ - position, positionX - position));
    normal = normalize(lerp(float3(0.0f, 1.0f, 0.0f), normal, gHighlightColorNormal.w));

    output.position = mul(float4(position, 1.0f), gViewProjection);
    output.worldPosition = position;
    output.normal = normal;
    output.uv = input.uv;
    output.wave = position.y - (gCenterTime.y + gHalfSizeAlpha.y);
    return output;
}
