// 役割: GPUパーティクルの寿命、速度、位置を更新するCompute Shader。
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

    float32_t3 previousPosition = particle.translate;
    uint32_t movementMode = uint32_t(gBehavior.motionFlags.x + 0.5f);

    if (movementMode == 0)
    {
        if (gBehavior.pointFieldFlags.x > 0.5f)
        {
            float32_t3 toCenter = gBehavior.pointFieldCenter.xyz - particle.translate;
            float32_t distance = length(toCenter);
            float32_t radius = max(gBehavior.pointFieldFlags.y, 0.0f);
            if (distance > 0.00001f && (radius <= 0.0f || distance <= radius))
            {
                float32_t3 radial = toCenter / distance;
                float32_t weight = 1.0f;
                float32_t falloff = max(gBehavior.pointFieldFlags.z, 0.0f);
                if (radius > 0.0f)
                {
                    weight = saturate(1.0f - distance / radius);
                    weight = pow(weight, falloff);
                }
                else if (falloff > 0.0f)
                {
                    weight = 1.0f / (1.0f + falloff * distance * distance);
                }

                float32_t3 orbitAxis = NormalizeOrZero(gBehavior.pointFieldOrbitAxis.xyz);
                if (dot(orbitAxis, orbitAxis) <= 0.000001f)
                {
                    orbitAxis = float32_t3(0.0f, 1.0f, 0.0f);
                }
                float32_t3 tangent = cross(orbitAxis, radial);
                float32_t radialStrength =
                    gBehavior.pointFieldStrengths.x - gBehavior.pointFieldStrengths.y;
                particle.velocity +=
                    (
                        radial * radialStrength +
                        tangent * gBehavior.pointFieldStrengths.z
                    ) *
                    weight *
                    gPerFrame.deltaTime;
            }

            float32_t damping =
                exp(-max(gBehavior.pointFieldFlags.w, 0.0f) * gPerFrame.deltaTime);
            particle.velocity *= damping;
        }

        particle.velocity += particle.acceleration * gPerFrame.deltaTime;
        particle.translate += particle.velocity * gPerFrame.deltaTime;
    }
    else if (movementMode == 1)
    {
        particle.vortexAngle += particle.vortexAngularSpeed * gPerFrame.deltaTime;
        particle.vortexRadius -= particle.vortexInwardSpeed * gPerFrame.deltaTime;
        particle.vortexHeightOffset += particle.vortexVerticalSpeed * gPerFrame.deltaTime;
        particle.vortexRadius = max(particle.vortexRadius, 0.0f);

        float32_t cosAngle = cos(particle.vortexAngle);
        float32_t sinAngle = sin(particle.vortexAngle);
        if (particle.vortexAxis == 0)
        {
            particle.translate = float32_t3(
                particle.vortexCenter.x + particle.vortexHeightOffset,
                particle.vortexCenter.y + cosAngle * particle.vortexRadius,
                particle.vortexCenter.z + sinAngle * particle.vortexRadius
            );
        }
        else if (particle.vortexAxis == 1)
        {
            particle.translate = float32_t3(
                particle.vortexCenter.x + cosAngle * particle.vortexRadius,
                particle.vortexCenter.y + particle.vortexHeightOffset,
                particle.vortexCenter.z + sinAngle * particle.vortexRadius
            );
        }
        else
        {
            particle.translate = float32_t3(
                particle.vortexCenter.x + cosAngle * particle.vortexRadius,
                particle.vortexCenter.y + sinAngle * particle.vortexRadius,
                particle.vortexCenter.z + particle.vortexHeightOffset
            );
        }
    }

    if (gBehavior.sway.x != 0.0f)
    {
        float32_t swayValue =
            sin(particle.currentTime * gBehavior.sway.y + particle.swayPhase) *
            gBehavior.sway.x;
        particle.translate += particle.swayAxis * swayValue * gPerFrame.deltaTime;
    }

    if (gBehavior.rotationFlags.x > 0.5f)
    {
        uint32_t alignAxis = uint32_t(gBehavior.rotationFlags.y + 0.5f);
        float32_t3 moveDirection = particle.translate - previousPosition;
        if (dot(moveDirection, moveDirection) <= 0.000001f)
        {
            moveDirection = particle.velocity;
        }
        particle.rotate = CalculateVelocityAlignedRotation(moveDirection, alignAxis);
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
