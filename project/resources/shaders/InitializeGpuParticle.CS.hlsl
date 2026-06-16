#include "GpuParticle.hlsli"

static const uint32_t kMaxParticles = 1024;
RWStructuredBuffer<GpuParticle> gParticles : register(u0);

[numthreads(1024, 1, 1)]
void main(uint32_t3 dispatchThreadId : SV_DispatchThreadID)
{
    uint32_t particleIndex = dispatchThreadId.x;
    if (particleIndex >= kMaxParticles)
    {
        return;
    }

    GpuParticle particle = (GpuParticle)0;

    if (particleIndex < 16)
    {
        uint32_t column = particleIndex % 4;
        uint32_t row = particleIndex / 4;

        particle.translate = float32_t3(
            -0.75f + float32_t(column) * 0.5f,
            2.0f + float32_t(row) * 0.35f,
            0.0f
        );
        particle.scale = float32_t3(0.22f, 0.22f, 1.0f);
        particle.rotate = float32_t3(0.0f, 0.0f, float32_t(particleIndex) * 0.3926991f);
        particle.lifeTime = 1.0f;
        particle.currentTime = 0.0f;
        particle.color = float32_t4(1.0f, 1.0f, 1.0f, 0.85f);
    }

    gParticles[particleIndex] = particle;
}
