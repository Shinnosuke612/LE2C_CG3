#include "GpuParticle.hlsli"

RWStructuredBuffer<GpuParticle> gParticles : register(u0);
RWStructuredBuffer<int32_t> gFreeListIndex : register(u1);
RWStructuredBuffer<uint32_t> gFreeList : register(u2);
ConstantBuffer<EmitterSphere> gEmitter : register(b0);
ConstantBuffer<GpuParticleBehavior> gBehavior : register(b2);
ConstantBuffer<GpuParticleDispatch> gDispatch : register(b3);

[numthreads(1024, 1, 1)]
void main(uint32_t3 dispatchThreadId : SV_DispatchThreadID)
{
    uint32_t particleIndex = dispatchThreadId.x;
    if (particleIndex >= kGpuParticleMaxParticles)
    {
        return;
    }

    const bool seedVisibleParticle =
        (gDispatch.flags.x & kGpuParticleEmitFlagSeedVisibleParticle) != 0;

    gParticles[particleIndex] = (GpuParticle)0;
    if (particleIndex == 0)
    {
        gFreeListIndex[0] = seedVisibleParticle
            ? int32_t(kGpuParticleMaxParticles) - 2
            : int32_t(kGpuParticleMaxParticles) - 1;
    }

    if (seedVisibleParticle && particleIndex == kGpuParticleMaxParticles - 1)
    {
        GpuParticle particle = (GpuParticle)0;
        particle.translate = float32_t3(0.0f, 2.0f, 0.0f);
        particle.lifeTime = 600.0f;
        particle.currentTime = 0.0f;
        particle.scale = float32_t3(1.0f, 1.0f, 1.0f);
        particle.startScale = particle.scale;
        particle.endScale = particle.scale;
        particle.rotate = float32_t3(0.0f, 0.0f, 0.0f);
        particle.rotationSpeed = 0.0f;
        particle.velocity = float32_t3(0.0f, 0.0f, 0.0f);
        particle.acceleration = float32_t3(0.0f, 0.0f, 0.0f);
        particle.color = float32_t4(8.0f, 0.6f, 0.2f, 1.0f);
        particle.startColor = particle.color;
        particle.endColor = particle.color;
        particle.initialAlpha = particle.color.a;
        gParticles[particleIndex] = particle;
        return;
    }

    gFreeList[particleIndex] = particleIndex;
}
