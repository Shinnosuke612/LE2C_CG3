#include "ParticleEmitter.h"
#include "ParticleManager.h"

namespace {

constexpr float kMinEmitterFrequency = 1.0f / 60.0f;

float NormalizeFrequency(float frequency) {
	return frequency > 0.0f ? frequency : kMinEmitterFrequency;
}

} // namespace

void ParticleEmitter::Initialize(ParticleManager* particleManager, const std::string& groupName){
	particleManager_ = particleManager;
	groupName_ = groupName;
}

void ParticleEmitter::Update(){
	if(!isActive_){
		return;
	}

	if (behavior_.life.isLooping) {
		if (!hasAutoEmittedLoopParticle_) {
			Emit();
			hasAutoEmittedLoopParticle_ = true;
		}
		return;
	}

	frequency_ = NormalizeFrequency(frequency_);
	frequencyTime_ += deltaTime_;
	while(frequencyTime_ >= frequency_){
		Emit();
		frequencyTime_ -= frequency_;
	}
}

void ParticleEmitter::SetFrequency(float frequency) {
	frequency_ = NormalizeFrequency(frequency);
}

void ParticleEmitter::ResetEmissionState() {
	frequencyTime_ = 0.0f;
	hasAutoEmittedLoopParticle_ = false;
}

void ParticleEmitter::SetBehavior(const ParticleManager::ParticleBehavior& behavior) {
	if (behavior_.life.isLooping != behavior.life.isLooping) {
		hasAutoEmittedLoopParticle_ = false;
		frequencyTime_ = 0.0f;
	}
	behavior_ = behavior;
}

void ParticleEmitter::SetLightning(
	const ParticleManager::LightningEmitterDesc& lightning
) {
	lightning_ = lightning;
}

void ParticleEmitter::Emit(){
	if(!particleManager_){
		return;
	}

	particleManager_->Emit(groupName_, transform_.translate, spawnSize_, count_, behavior_);
	if (lightning_.enabled) {
		particleManager_->QueueLightning(lightning_, transform_.translate);
	}
}
