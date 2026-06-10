#include "ParticleEmitter.h"
#include "ParticleManager.h"

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

	frequencyTime_ += deltaTime_;
	while(frequencyTime_ >= frequency_){
		Emit();
		frequencyTime_ -= frequency_;
	}
}

void ParticleEmitter::SetBehavior(const ParticleManager::ParticleBehavior& behavior) {
	if (behavior_.life.isLooping != behavior.life.isLooping) {
		hasAutoEmittedLoopParticle_ = false;
		frequencyTime_ = 0.0f;
	}
	behavior_ = behavior;
}

void ParticleEmitter::Emit(){
	if(!particleManager_){
		return;
	}

	particleManager_->Emit(groupName_, transform_.translate, spawnSize_, count_, behavior_);
}
