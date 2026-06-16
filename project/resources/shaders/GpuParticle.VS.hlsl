#include "Particle.hlsli"
#include "GpuParticle.hlsli"

StructuredBuffer<GpuParticle> gParticles : register(t0);
ConstantBuffer<PerView> gPerView : register(b0);

struct VertexShaderInput
{
    float32_t4 position : POSITION0;
    float32_t2 texcoord : TEXCOORD0;
    float32_t3 normal : NORMAL0;
    float32_t4 color : COLOR0;
};

float32_t4x4 MakeScaleMatrix(float32_t3 scale)
{
    return float32_t4x4(
        scale.x, 0.0f, 0.0f, 0.0f,
        0.0f, scale.y, 0.0f, 0.0f,
        0.0f, 0.0f, scale.z, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f
    );
}

float32_t4x4 MakeRotateXMatrix(float32_t radian)
{
    float32_t s = sin(radian);
    float32_t c = cos(radian);
    return float32_t4x4(
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, c, s, 0.0f,
        0.0f, -s, c, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f
    );
}

float32_t4x4 MakeRotateYMatrix(float32_t radian)
{
    float32_t s = sin(radian);
    float32_t c = cos(radian);
    return float32_t4x4(
        c, 0.0f, -s, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        s, 0.0f, c, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f
    );
}

float32_t4x4 MakeRotateZMatrix(float32_t radian)
{
    float32_t s = sin(radian);
    float32_t c = cos(radian);
    return float32_t4x4(
        c, s, 0.0f, 0.0f,
        -s, c, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f
    );
}

VertexShaderOutput main(
    VertexShaderInput input,
    uint32_t instanceId : SV_InstanceID
)
{
    GpuParticle particle = gParticles[instanceId];

    float32_t4x4 scaleMatrix = MakeScaleMatrix(particle.scale);
    float32_t4x4 rotateMatrix = mul(
        mul(MakeRotateZMatrix(particle.rotate.z), MakeRotateYMatrix(particle.rotate.y)),
        MakeRotateXMatrix(particle.rotate.x)
    );
    float32_t4x4 worldMatrix = mul(mul(scaleMatrix, rotateMatrix), gPerView.billboardMatrix);
    worldMatrix[3].xyz = particle.translate;

    VertexShaderOutput output;
    output.position = mul(input.position, mul(worldMatrix, gPerView.viewProjection));
    output.texcoord = input.texcoord;
    output.normal = normalize(mul(input.normal, (float32_t3x3)worldMatrix));
    output.color = particle.color * input.color;
    return output;
}
