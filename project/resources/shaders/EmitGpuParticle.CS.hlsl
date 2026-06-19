#include "GpuParticle.hlsli"

static const uint32_t kMaxParticles = 1024;

RWStructuredBuffer<GpuParticle> gParticles : register(u0);
RWStructuredBuffer<int32_t> gFreeListIndex : register(u1);
RWStructuredBuffer<uint32_t> gFreeList : register(u2);
ConstantBuffer<EmitterSphere> gEmitter : register(b0);
ConstantBuffer<PerFrame> gPerFrame : register(b1);
ConstantBuffer<GpuParticleBehavior> gBehavior : register(b2);

float32_t Rand1d(float32_t3 seed)
{
    return frac(sin(dot(seed, float32_t3(12.9898f, 78.233f, 37.719f))) * 43758.5453f);
}

float32_t3 Rand3d(float32_t3 seed)
{
    return float32_t3(
        Rand1d(seed + float32_t3(0.0f, 0.0f, 0.0f)),
        Rand1d(seed + float32_t3(31.416f, 17.903f, 11.135f)),
        Rand1d(seed + float32_t3(47.853f, 59.724f, 83.119f))
    );
}

float32_t3 RandomUnitVector(float32_t3 seed)
{
    float32_t3 value = Rand3d(seed) * 2.0f - 1.0f;
    float32_t lengthValue = max(length(value), 0.0001f);
    return value / lengthValue;
}

[numthreads(1, 1, 1)]
void main(uint32_t3 dispatchThreadId : SV_DispatchThreadID)
{
    if (gEmitter.emit == 0)
    {
        return;
    }

    for (uint32_t countIndex = 0; countIndex < gEmitter.count; ++countIndex)
    {
        int32_t freeListIndex = 0;
        InterlockedAdd(gFreeListIndex[0], -1, freeListIndex);
        if (freeListIndex < 0 || int32_t(kMaxParticles) <= freeListIndex)
        {
            InterlockedAdd(gFreeListIndex[0], 1, freeListIndex);
            break;
        }

        uint32_t particleIndex = gFreeList[freeListIndex];
        float32_t3 seed =
            float32_t3(float32_t(particleIndex), float32_t(countIndex), gPerFrame.time);
        float32_t3 direction = RandomUnitVector(seed);
        float32_t radius = Rand1d(seed + 3.17f) * gEmitter.radius;
        float32_t scale = lerp(
            gBehavior.lifeScaleVelocityMinRotationMin.y,
            gBehavior.lifeScaleVelocityMaxRotationMax.y,
            Rand1d(seed + 7.31f)
        );

        GpuParticle particle = (GpuParticle)0;
        particle.translate = gEmitter.translate + direction * radius;
        particle.scale = float32_t3(scale, scale, 1.0f);
        particle.rotate = float32_t3(
            0.0f,
            0.0f,
            Rand1d(seed + 19.57f) * 6.2831853f
        );
        particle.rotationSpeed = lerp(
            gBehavior.lifeScaleVelocityMinRotationMin.w,
            gBehavior.lifeScaleVelocityMaxRotationMax.w,
            Rand1d(seed + 17.41f)
        );
        particle.lifeTime = lerp(
            gBehavior.lifeScaleVelocityMinRotationMin.x,
            gBehavior.lifeScaleVelocityMaxRotationMax.x,
            Rand1d(seed + 13.79f)
        );
        particle.currentTime = 0.0f;
        particle.velocity = direction * lerp(
            gBehavior.lifeScaleVelocityMinRotationMin.z,
            gBehavior.lifeScaleVelocityMaxRotationMax.z,
            Rand1d(seed + 23.11f)
        );
        particle.color = lerp(
            gBehavior.colorMin,
            gBehavior.colorMax,
            float32_t4(
                Rand1d(seed + 29.73f),
                Rand1d(seed + 41.29f),
                Rand1d(seed + 53.47f),
                Rand1d(seed + 61.91f)
            )
        );

        gParticles[particleIndex] = particle;
    }
}
