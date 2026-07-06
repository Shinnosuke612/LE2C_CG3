#include "GpuParticle.hlsli"

RWStructuredBuffer<GpuParticle> gParticles : register(u0);
RWStructuredBuffer<int32_t> gFreeListIndex : register(u1);
RWStructuredBuffer<uint32_t> gFreeList : register(u2);
ConstantBuffer<PerFrame> gPerFrame : register(b1);
ConstantBuffer<GpuParticleBehavior> gBehavior : register(b2);

void PushFreeList(uint32_t particleIndex)
{
    int32_t freeListIndex = 0;
    InterlockedAdd(gFreeListIndex[0], 1, freeListIndex);
    if ((freeListIndex + 1) < int32_t(kGpuParticleMaxParticles))
    {
        gFreeList[freeListIndex + 1] = particleIndex;
    }
    else
    {
        InterlockedAdd(gFreeListIndex[0], -1, freeListIndex);
    }
}

[numthreads(1024, 1, 1)]
void main(uint32_t3 dispatchThreadId : SV_DispatchThreadID)
{
    uint32_t particleIndex = dispatchThreadId.x;
    if (particleIndex >= kGpuParticleMaxParticles)
    {
        return;
    }

    GpuParticle particle = gParticles[particleIndex];
    if (particle.color.a <= 0.0f || particle.lifeTime <= 0.0f)
    {
        return;
    }

    particle.currentTime += gPerFrame.deltaTime;
    if (particle.lifeTime <= particle.currentTime)
    {
        particle.color.a = 0.0f;
        particle.scale = float32_t3(0.0f, 0.0f, 0.0f);
        gParticles[particleIndex] = particle;
        PushFreeList(particleIndex);
        return;
    }

    particle.velocity += particle.acceleration * gPerFrame.deltaTime;
    particle.translate += particle.velocity * gPerFrame.deltaTime;
    if (gBehavior.rotationFlags.x > 0.5f)
    {
        uint32_t alignAxis = uint32_t(gBehavior.rotationFlags.y + 0.5f);
        particle.rotate = CalculateVelocityAlignedRotation(particle.velocity, alignAxis);
    }
    else
    {
        particle.rotate.z += gPerFrame.deltaTime * particle.rotationSpeed;
    }

    float32_t lifeRatio = saturate(particle.currentTime / particle.lifeTime);
    if (gBehavior.flags.z > 0.5f)
    {
        particle.scale = lerp(particle.startScale, particle.endScale, lifeRatio);
    }
    if (gBehavior.flags.w > 0.5f)
    {
        particle.color = lerp(particle.startColor, particle.endColor, lifeRatio);
        particle.initialAlpha = particle.color.a;
    }
    else
    {
        particle.color = particle.startColor;
    }
    if (gBehavior.flags.x > 0.5f)
    {
        float32_t fadeStart = saturate(gBehavior.flags.y);
        float32_t fadeRatio = saturate((lifeRatio - fadeStart) / max(1.0f - fadeStart, 0.0001f));
        particle.color.a = particle.initialAlpha * saturate(1.0f - fadeRatio);
    }
    else
    {
        particle.color.a = particle.initialAlpha;
    }

    gParticles[particleIndex] = particle;
}
