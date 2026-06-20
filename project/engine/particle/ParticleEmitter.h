#pragma once
#include <string>
#include "../math/Transform.h"
#include "ParticleManager.h"

using ParticleBehavior = ParticleManager::ParticleBehavior;

class ParticleEmitter{
public:
	void Initialize(ParticleManager* particleManager, const std::string& groupName);

	void Update();
	void Emit();
	void ResetEmissionState();

	void SetTranslate(const Vector3& translate){ transform_.translate = translate; }
	void SetScale(const Vector3& scale){ transform_.scale = scale; }
	void SetRotate(const Vector3& rotate){ transform_.rotate = rotate; }

	void SetCount(uint32_t count){ count_ = count; }
	void SetFrequency(float frequency);
	void SetActive(bool isActive){ isActive_ = isActive; }

	void SetSpawnSize(const Vector3& spawnSize){ spawnSize_ = spawnSize; }
	void SetBehavior(const ParticleManager::ParticleBehavior& behavior);
	void SetLightning(const ParticleManager::LightningEmitterDesc& lightning);

	const Vector3& GetSpawnSize() const{ return spawnSize_; }
	const ParticleManager::ParticleBehavior& GetBehavior() const{ return behavior_; }
	const ParticleManager::LightningEmitterDesc& GetLightning() const{ return lightning_; }

private:
	ParticleManager* particleManager_ = nullptr;
	std::string groupName_;
	Transform transform_{ {1.0f,1.0f,1.0f}, {0.0f,0.0f,0.0f}, {0.0f,0.0f,0.0f} };
	ParticleManager::ParticleBehavior behavior_{};
	ParticleManager::LightningEmitterDesc lightning_{};

	uint32_t count_ = 1;
	float frequency_ = 0.4f;
	float frequencyTime_ = 0.0f;
	float deltaTime_ = 1.0f / 60.0f;
	bool isActive_ = true;
	bool hasAutoEmittedLoopParticle_ = false;
	Vector3 spawnSize_ = { 8.0f, 0.0f, 0.0f };
};
