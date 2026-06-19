#include "GpuParticle.hlsli"

static const uint32_t kMaxParticles = 1024;

RWStructuredBuffer<GpuParticle> gParticles : register(u0);
RWStructuredBuffer<int32_t> gFreeListIndex : register(u1);
RWStructuredBuffer<uint32_t> gFreeList : register(u2);
ConstantBuffer<PerFrame> gPerFrame : register(b1);

void PushFreeList(uint32_t particleIndex)
{
    int32_t freeListIndex = 0;
    InterlockedAdd(gFreeListIndex[0], 1, freeListIndex);
    if ((freeListIndex + 1) < int32_t(kMaxParticles))
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
    if (particleIndex >= kMaxParticles)
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

    particle.translate += particle.velocity * gPerFrame.deltaTime;
    particle.rotate.z += gPerFrame.deltaTime * particle.rotationSpeed;

    float32_t lifeRatio = saturate(particle.currentTime / particle.lifeTime);
    particle.color.a = saturate(1.0f - lifeRatio);

    gParticles[particleIndex] = particle;
}
