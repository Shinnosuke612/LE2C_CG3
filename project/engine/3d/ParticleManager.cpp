#include "ParticleManager.h"
#include "ParticleCommon.h"
#include "SrvManager.h"
#include "Camera.h"
#include "../base/DirectXCommon.h"
#include "../2d/TextureManager.h"
#include <cassert>
#include <algorithm>
#include <cmath>
#include <random>

float ParticleManager::RandomRange(float min, float max){
	static std::random_device seedGenerator;
	static std::mt19937 randomEngine(seedGenerator());
	std::uniform_real_distribution<float> distribution(min, max);
	return distribution(randomEngine);
}

Vector3 ParticleManager::RandomVector3Range(const Vector3& min, const Vector3& max){
	return {
		RandomRange(min.x, max.x),
		RandomRange(min.y, max.y),
		RandomRange(min.z, max.z)
	};
}

void ParticleManager::Initialize(ParticleCommon* particleCommon, SrvManager* srvManager){
	particleCommon_ = particleCommon;
	srvManager_ = srvManager;
	camera_ = particleCommon_->GetDefaultCamera();

	CreateDirectionalLightResource();
}

void ParticleManager::CreateDirectionalLightResource(){
	directionalLightResource_ = particleCommon_->GetDxCommon()->CreateBufferResource(sizeof(DirectionalLight));
	directionalLightResource_->Map(0, nullptr, reinterpret_cast<void**>(&directionalLightData_));

	directionalLightData_->color = { 1.0f,1.0f,1.0f,1.0f };
	directionalLightData_->direction = { 0.0f,-1.0f,0.0f };
	directionalLightData_->intensity = 1.0f;
}

void ParticleManager::CreateParticleGroup(const std::string& name, const std::string& textureFilePath){
	assert(!particleGroups_.contains(name));
	assert(srvManager_->CanAllocate());

	TextureManager::GetInstance()->LoadTexture(textureFilePath);

	ParticleGroup group{};
	group.textureFilePath = textureFilePath;
	group.textureSrvIndex = TextureManager::GetInstance()->GetSrvIndex(textureFilePath);

	// Material
	group.materialResource = particleCommon_->GetDxCommon()->CreateBufferResource(sizeof(Material));
	group.materialResource->Map(0, nullptr, reinterpret_cast<void**>(&group.materialData));
	group.materialData->color = { 1.0f,1.0f,1.0f,1.0f };
	group.materialData->enableLighting = false;
	group.materialData->uvTransform = MakeIdentity4x4();

	// Instance buffer
	group.instancingResource =
		particleCommon_->GetDxCommon()->CreateBufferResource(sizeof(TransformationMatrix) * kMaxInstanceCount);
	group.instancingResource->Map(0, nullptr, reinterpret_cast<void**>(&group.instancingData));

	group.instanceSrvIndex = srvManager_->Allocate();
	srvManager_->CreateSRVforStructuredBuffer(
		group.instanceSrvIndex,
		group.instancingResource.Get(),
		kMaxInstanceCount,
		sizeof(TransformationMatrix)
	);

	particleGroups_.emplace(name, std::move(group));
}

void ParticleManager::Emit(const std::string& name, const Vector3& position, const Vector3& spawnSize, uint32_t count, const ParticleBehavior& behavior){
	auto it = particleGroups_.find(name);
	assert(it != particleGroups_.end());

	ParticleGroup& group = it->second;

	for(uint32_t i = 0; i < count; ++i){
		if(group.particles.size() >= kMaxInstanceCount){
			break;
		}

		Particle p{};

		Vector3 halfSpawnSize = {
			spawnSize.x * 0.5f,
			spawnSize.y * 0.5f,
			spawnSize.z * 0.5f
		};

		Vector3 spawnOffset = RandomVector3Range(
			{ -halfSpawnSize.x, -halfSpawnSize.y, -halfSpawnSize.z },
			{ halfSpawnSize.x,  halfSpawnSize.y,  halfSpawnSize.z }
		);

		p.transform.translate = {
			position.x + spawnOffset.x,
			position.y + spawnOffset.y,
			position.z + spawnOffset.z
		};

		p.transform.rotate = { 0.0f, 0.0f, 0.0f };
		p.transform.scale = RandomVector3Range(behavior.startScaleMin, behavior.startScaleMax);

		Vector3 velocityRandom = RandomVector3Range(
			{ -behavior.velocityRandomRange.x, -behavior.velocityRandomRange.y, -behavior.velocityRandomRange.z },
			{ behavior.velocityRandomRange.x,  behavior.velocityRandomRange.y,  behavior.velocityRandomRange.z }
		);

		Vector3 accelerationRandom = RandomVector3Range(
			{ -behavior.accelerationRandomRange.x, -behavior.accelerationRandomRange.y, -behavior.accelerationRandomRange.z },
			{ behavior.accelerationRandomRange.x,  behavior.accelerationRandomRange.y,  behavior.accelerationRandomRange.z }
		);

		p.velocity = {
			behavior.baseVelocity.x + velocityRandom.x,
			behavior.baseVelocity.y + velocityRandom.y,
			behavior.baseVelocity.z + velocityRandom.z
		};

		p.acceleration = {
			behavior.baseAcceleration.x + accelerationRandom.x,
			behavior.baseAcceleration.y + accelerationRandom.y,
			behavior.baseAcceleration.z + accelerationRandom.z
		};

		p.currentTime = 0.0f;
		p.lifeTime = RandomRange(behavior.lifeTimeMin, behavior.lifeTimeMax);
		p.color = { 1.0f, 1.0f, 1.0f, 1.0f };

		p.swayTime = 0.0f;
		p.swayPhase = RandomRange(0.0f, 6.2831853f);
		p.swayAxis = RandomVector3Range({ -1.0f, -1.0f, -1.0f }, { 1.0f, 1.0f, 1.0f });

		p.swayAmplitude = behavior.swayAmplitude;
		p.swayFrequency = behavior.swayFrequency;

		group.particles.push_back(p);
	}
}

void ParticleManager::Update(){
	if(!camera_){
		return;
	}

	for(auto& [name, group] : particleGroups_){
		// 寿命切れ除去
		for(auto& particle : group.particles){
			particle.currentTime += deltaTime_;

			particle.velocity.x += particle.acceleration.x * deltaTime_;
			particle.velocity.y += particle.acceleration.y * deltaTime_;
			particle.velocity.z += particle.acceleration.z * deltaTime_;

			particle.transform.translate.x += particle.velocity.x * deltaTime_;
			particle.transform.translate.y += particle.velocity.y * deltaTime_;
			particle.transform.translate.z += particle.velocity.z * deltaTime_;

			particle.swayTime += deltaTime_;
			float swayValue = std::sin(particle.swayTime * particle.swayFrequency + particle.swayPhase) * particle.swayAmplitude;

			particle.transform.translate.x += particle.swayAxis.x * swayValue * deltaTime_;
			particle.transform.translate.y += particle.swayAxis.y * swayValue * deltaTime_;
			particle.transform.translate.z += particle.swayAxis.z * swayValue * deltaTime_;
		}

		group.particles.erase(
			std::remove_if(
			group.particles.begin(),
			group.particles.end(),
			[](const Particle& p){ return p.currentTime >= p.lifeTime; }
		),
			group.particles.end()
		);

		group.instanceCount = 0;

		for(const auto& particle : group.particles){
			if(group.instanceCount >= kMaxInstanceCount){
				break;
			}

			Matrix4x4 worldMatrix = MakeAffineMatrix(
				particle.transform.scale,
				particle.transform.rotate,
				particle.transform.translate
			);

			Matrix4x4 wvp = Multiply(worldMatrix, camera_->GetViewProjectionMatrix());

			group.instancingData[group.instanceCount].World = worldMatrix;
			group.instancingData[group.instanceCount].WVP = wvp;
			++group.instanceCount;
		}
	}
}

void ParticleManager::Draw(){
	auto* commandList = particleCommon_->GetDxCommon()->GetCommandList();
	particleCommon_->SetBlendMode(ParticleCommon::BlendMode::kBlendModeAdd);
	particleCommon_->SetCommonRenderState();

	for(auto& [name, group] : particleGroups_){
		if(group.instanceCount == 0){
			continue;
		}

		// b0 Material
		commandList->SetGraphicsRootConstantBufferView(
			0,
			group.materialResource->GetGPUVirtualAddress()
		);

		// VS t0 StructuredBuffer<TransformationMatrix>
		srvManager_->SetGraphicsRootDescriptorTable(1, group.instanceSrvIndex);

		// PS t0 Texture2D
		srvManager_->SetGraphicsRootDescriptorTable(2, group.textureSrvIndex);

		// b1 DirectionalLight
		commandList->SetGraphicsRootConstantBufferView(
			3,
			directionalLightResource_->GetGPUVirtualAddress()
		);

		commandList->DrawInstanced(6, group.instanceCount, 0, 0);
	}
}