#include "GpuParticle.hlsli"

RWStructuredBuffer<GpuParticle> gParticles : register(u0);
RWStructuredBuffer<int32_t> gFreeListIndex : register(u1);
RWStructuredBuffer<uint32_t> gFreeList : register(u2);
ConstantBuffer<EmitterSphere> gEmitter : register(b0);
ConstantBuffer<PerFrame> gPerFrame : register(b1);
ConstantBuffer<GpuParticleBehavior> gBehavior : register(b2);
ConstantBuffer<GpuParticleDispatch> gDispatch : register(b3);

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
    if ((gDispatch.flags.x & kGpuParticleEmitFlagEmitParticles) == 0)
    {
        return;
    }

    for (uint32_t countIndex = 0; countIndex < gEmitter.count; ++countIndex)
    {
        int32_t freeListIndex = 0;
        InterlockedAdd(gFreeListIndex[0], -1, freeListIndex);
        if (freeListIndex < 0 || int32_t(kGpuParticleMaxParticles) <= freeListIndex)
        {
            InterlockedAdd(gFreeListIndex[0], 1, freeListIndex);
            break;
        }

        uint32_t particleIndex = gFreeList[freeListIndex];
        float32_t3 seed =
            float32_t3(float32_t(particleIndex), float32_t(countIndex), gPerFrame.time);
        float32_t3 direction = RandomUnitVector(seed);
        float32_t radius = Rand1d(seed + 3.17f) * gEmitter.radius;
        float32_t3 startScale = lerp(
            gBehavior.startScaleMin.xyz,
            gBehavior.startScaleMax.xyz,
            Rand3d(seed + 7.31f)
        );
        float32_t3 endScale = lerp(
            gBehavior.endScaleMin.xyz,
            gBehavior.endScaleMax.xyz,
            Rand3d(seed + 9.43f)
        );

        GpuParticle particle = (GpuParticle)0;
        if (gEmitter.shape == 1)
        {
            particle.translate =
                gEmitter.translate +
                (Rand3d(seed + 5.61f) * 2.0f - 1.0f) *
                gEmitter.spawnSize * 0.5f;
        }
        else
        {
            particle.translate = gEmitter.translate + direction * radius;
        }
        particle.scale = startScale;
        particle.startScale = startScale;
        particle.endScale = endScale;
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
        float32_t speed = lerp(
            gBehavior.lifeScaleVelocityMinRotationMin.z,
            gBehavior.lifeScaleVelocityMaxRotationMax.z,
            Rand1d(seed + 23.11f)
        );
        float32_t3 configuredVelocity =
            gBehavior.velocityBase.xyz +
            (Rand3d(seed + 37.19f) * 2.0f - 1.0f) *
            gBehavior.velocityRandomRange.xyz;
        if (length(configuredVelocity) <= 0.0001f)
        {
            configuredVelocity = direction;
        }
        particle.velocity = configuredVelocity * speed;
        particle.acceleration =
            gBehavior.accelerationBase.xyz +
            (Rand3d(seed + 43.83f) * 2.0f - 1.0f) *
            gBehavior.accelerationRandomRange.xyz;
        particle.swayAxis = Rand3d(seed + 89.37f) * 2.0f - 1.0f;
        particle.swayPhase = Rand1d(seed + 97.11f) * 6.2831853f;
        particle.vortexCenter = gBehavior.vortexCenter.xyz;
        particle.vortexAxis = uint32_t(gBehavior.motionFlags.y + 0.5f);
        float32_t3 vortexOffset = particle.translate - particle.vortexCenter;
        if (particle.vortexAxis == 0)
        {
            particle.vortexRadius =
                sqrt(vortexOffset.y * vortexOffset.y + vortexOffset.z * vortexOffset.z);
            particle.vortexAngle = atan2(vortexOffset.z, vortexOffset.y);
            particle.vortexHeightOffset = vortexOffset.x;
        }
        else if (particle.vortexAxis == 1)
        {
            particle.vortexRadius =
                sqrt(vortexOffset.x * vortexOffset.x + vortexOffset.z * vortexOffset.z);
            particle.vortexAngle = atan2(vortexOffset.z, vortexOffset.x);
            particle.vortexHeightOffset = vortexOffset.y;
        }
        else
        {
            particle.vortexRadius =
                sqrt(vortexOffset.x * vortexOffset.x + vortexOffset.y * vortexOffset.y);
            particle.vortexAngle = atan2(vortexOffset.y, vortexOffset.x);
            particle.vortexHeightOffset = vortexOffset.z;
        }
        particle.vortexAngularSpeed = lerp(
            gBehavior.vortexAngularInwardSpeed.x,
            gBehavior.vortexAngularInwardSpeed.y,
            Rand1d(seed + 101.23f)
        );
        particle.vortexInwardSpeed = lerp(
            gBehavior.vortexAngularInwardSpeed.z,
            gBehavior.vortexAngularInwardSpeed.w,
            Rand1d(seed + 109.57f)
        );
        particle.vortexVerticalSpeed = lerp(
            gBehavior.vortexVerticalSpeed.x,
            gBehavior.vortexVerticalSpeed.y,
            Rand1d(seed + 113.91f)
        );
        if (gBehavior.rotationFlags.x > 0.5f)
        {
            uint32_t alignAxis = uint32_t(gBehavior.rotationFlags.y + 0.5f);
            particle.rotate = CalculateVelocityAlignedRotation(particle.velocity, alignAxis);
        }
        particle.startColor = lerp(
            gBehavior.colorMin,
            gBehavior.colorMax,
            float32_t4(
                Rand1d(seed + 29.73f),
                Rand1d(seed + 41.29f),
                Rand1d(seed + 53.47f),
                Rand1d(seed + 61.91f)
            )
        );
        particle.endColor = lerp(
            gBehavior.endColorMin,
            gBehavior.endColorMax,
            float32_t4(
                Rand1d(seed + 67.13f),
                Rand1d(seed + 71.31f),
                Rand1d(seed + 79.07f),
                Rand1d(seed + 83.53f)
            )
        );
        particle.color = particle.startColor;
        particle.initialAlpha = particle.color.a;

        gParticles[particleIndex] = particle;
    }
}
