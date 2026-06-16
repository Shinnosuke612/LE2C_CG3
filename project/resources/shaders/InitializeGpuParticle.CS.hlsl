#include "GpuParticle.hlsli"

static const uint32_t kMaxParticles = 1024;
RWStructuredBuffer<GpuParticle> gParticles : register(u0);
RWStructuredBuffer<int32_t> gFreeCounter : register(u1);

[numthreads(1024, 1, 1)]
void main(uint32_t3 dispatchThreadId : SV_DispatchThreadID)
{
    uint32_t particleIndex = dispatchThreadId.x;
    if (particleIndex >= kMaxParticles)
    {
        return;
    }

    if (particleIndex == 0)
    {
        gFreeCounter[0] = 0;
    }

    gParticles[particleIndex] = (GpuParticle)0;
}
