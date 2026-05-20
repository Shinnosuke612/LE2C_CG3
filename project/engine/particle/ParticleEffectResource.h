#pragma once
#include <string>
#include "../math/Vector3.h"
#include "ParticleManager.h"
#include "ParticleCommon.h"

struct ParticleEmitterDesc {
	Vector3 translate = { 0.0f, 0.0f, 0.0f };
	Vector3 spawnSize = { 1.0f, 1.0f, 1.0f };
	uint32_t count = 1;
	float frequency = 0.1f;
	bool isActive = true;
};

struct ParticleEffectDesc {
	std::string name = "newParticle";
	std::string textureFilePath = "resources/circle.png";

	ParticleCommon::BlendMode blendMode =
		ParticleCommon::BlendMode::kBlendModeAdd;

	ParticleEmitterDesc emitter;
	ParticleManager::ParticleBehavior behavior;
};