static const uint32_t kGpuParticleEmitFlagEmitParticles = 1u << 0;
static const uint32_t kGpuParticleEmitFlagSeedVisibleParticle = 1u << 1;
static const uint32_t kGpuParticleMaxParticles = 20480;

struct GpuParticle
{
    float32_t3 translate;
    float32_t lifeTime;
    float32_t3 scale;
    float32_t currentTime;
    float32_t3 rotate;
    float32_t rotationSpeed;
    float32_t3 velocity;
    float32_t initialAlpha;
    float32_t4 color;
    float32_t3 acceleration;
    float32_t padding0;
    float32_t3 startScale;
    float32_t padding1;
    float32_t3 endScale;
    float32_t padding2;
    float32_t4 startColor;
    float32_t4 endColor;
    float32_t3 swayAxis;
    float32_t swayPhase;
    float32_t3 vortexCenter;
    float32_t vortexAngle;
    float32_t vortexRadius;
    float32_t vortexHeightOffset;
    float32_t vortexAngularSpeed;
    float32_t vortexInwardSpeed;
    float32_t vortexVerticalSpeed;
    uint32_t vortexAxis;
    float32_t2 vortexPadding;
};

struct EmitterSphere
{
    float32_t3 translate;
    float32_t radius;
    float32_t3 spawnSize;
    uint32_t shape;
    uint32_t count;
    float32_t frequency;
    float32_t frequencyTime;
    uint32_t emit;
};

struct PerView
{
    float32_t4x4 viewProjection;
    float32_t4x4 billboardMatrix;
    float32_t4 renderFlags;
};

struct PerFrame
{
    float32_t time;
    float32_t deltaTime;
    float32_t2 padding;
};

struct GpuParticleBehavior
{
    float32_t4 lifeScaleVelocityMinRotationMin;
    float32_t4 lifeScaleVelocityMaxRotationMax;
    float32_t4 startScaleMin;
    float32_t4 startScaleMax;
    float32_t4 endScaleMin;
    float32_t4 endScaleMax;
    float32_t4 velocityBase;
    float32_t4 velocityRandomRange;
    float32_t4 accelerationBase;
    float32_t4 accelerationRandomRange;
    float32_t4 flags;
    float32_t4 rotationFlags;
    float32_t4 colorMin;
    float32_t4 colorMax;
    float32_t4 endColorMin;
    float32_t4 endColorMax;
    float32_t4 sway;
    float32_t4 pointFieldFlags;
    float32_t4 pointFieldCenter;
    float32_t4 pointFieldStrengths;
    float32_t4 pointFieldOrbitAxis;
    float32_t4 motionFlags;
    float32_t4 vortexCenter;
    float32_t4 vortexAngularInwardSpeed;
    float32_t4 vortexVerticalSpeed;
};

struct GpuParticleDispatch
{
    uint32_t4 flags;
};

float32_t3 NormalizeOrZero(float32_t3 value)
{
    float32_t lengthSquared = dot(value, value);
    if (lengthSquared <= 0.000001f)
    {
        return float32_t3(0.0f, 0.0f, 0.0f);
    }
    return value * rsqrt(lengthSquared);
}

float32_t3 CalculateVelocityAlignedRotation(float32_t3 direction, uint32_t axis)
{
    float32_t3 dir = NormalizeOrZero(direction);
    if (dot(dir, dir) <= 0.000001f)
    {
        return float32_t3(0.0f, 0.0f, 0.0f);
    }

    if (axis == 0)
    {
        float32_t yzLength = sqrt(dir.y * dir.y + dir.z * dir.z);
        return float32_t3(
            0.0f,
            atan2(-dir.z, dir.x),
            atan2(dir.y, yzLength)
        );
    }
    if (axis == 1)
    {
        float32_t yzLength = sqrt(dir.y * dir.y + dir.z * dir.z);
        return float32_t3(
            atan2(dir.z, dir.y),
            0.0f,
            atan2(-dir.x, yzLength)
        );
    }

    float32_t xzLength = sqrt(dir.x * dir.x + dir.z * dir.z);
    return float32_t3(
        atan2(-dir.y, xzLength),
        atan2(dir.x, dir.z),
        0.0f
    );
}
