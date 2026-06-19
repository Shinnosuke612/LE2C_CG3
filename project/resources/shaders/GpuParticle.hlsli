struct GpuParticle
{
    float32_t3 translate;
    float32_t lifeTime;
    float32_t3 scale;
    float32_t currentTime;
    float32_t3 rotate;
    float32_t rotationSpeed;
    float32_t3 velocity;
    float32_t padding0;
    float32_t4 color;
};

struct EmitterSphere
{
    float32_t3 translate;
    float32_t radius;
    uint32_t count;
    float32_t frequency;
    float32_t frequencyTime;
    uint32_t emit;
};

struct PerView
{
    float32_t4x4 viewProjection;
    float32_t4x4 billboardMatrix;
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
    float32_t4 colorMin;
    float32_t4 colorMax;
};
