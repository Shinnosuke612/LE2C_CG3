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

	frequencyTime_ += deltaTime_;
	while(frequencyTime_ >= frequency_){
		Emit();
		frequencyTime_ -= frequency_;
	}
}

void ParticleEmitter::Emit(){
	if(!particleManager_){
		return;
	}

	particleManager_->Emit(groupName_, transform_.translate, spawnSize_, count_, behavior_);
}