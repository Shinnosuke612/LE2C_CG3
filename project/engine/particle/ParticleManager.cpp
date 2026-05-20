#include "ParticleManager.h"
#include "ParticleCommon.h"

#include "../3d/SrvManager.h"
#include "../3d/Camera.h"
#include "../base/DirectXCommon.h"
#include "../2d/TextureManager.h"

#include <cassert>
#include <algorithm>
#include <cmath>
#include <random>

ParticleManager* ParticleManager::instance_ = nullptr;

ParticleManager* ParticleManager::GetInstance() {
	if (instance_ == nullptr) {
		instance_ = new ParticleManager();
	}
	return instance_;
}

void ParticleManager::DeleteInstance() {
	delete instance_;
	instance_ = nullptr;
}

void ParticleManager::Initialize(ParticleCommon* particleCommon, SrvManager* srvManager) {
	assert(particleCommon);
	assert(srvManager);

	particleCommon_ = particleCommon;
	srvManager_ = srvManager;
	camera_ = particleCommon_->GetDefaultCamera();

	CreateDirectionalLightResource();
}

void ParticleManager::Reset() {
	particleGroups_.clear();

	directionalLightResource_.Reset();
	directionalLightData_ = nullptr;

	particleCommon_ = nullptr;
	srvManager_ = nullptr;
	camera_ = nullptr;
}

void ParticleManager::SetGroupBlendMode(
	const std::string& name,
	ParticleCommon::BlendMode blendMode
) {
	auto it = particleGroups_.find(name);
	assert(it != particleGroups_.end());

	it->second.blendMode = blendMode;
}

void ParticleManager::CreateDirectionalLightResource() {
	directionalLightResource_ = particleCommon_->GetDxCommon()->CreateBufferResource(sizeof(DirectionalLight));
	directionalLightResource_->Map(0, nullptr, reinterpret_cast<void**>(&directionalLightData_));

	directionalLightData_->color = { 1.0f, 1.0f, 1.0f, 1.0f };
	directionalLightData_->direction = { 0.0f, -1.0f, 0.0f };
	directionalLightData_->intensity = 1.0f;
}

float ParticleManager::RandomRange(float min, float max) {
	if (min > max) {
		std::swap(min, max);
	}

	static std::random_device seedGenerator;
	static std::mt19937 randomEngine(seedGenerator());

	std::uniform_real_distribution<float> distribution(min, max);
	return distribution(randomEngine);
}

Vector3 ParticleManager::RandomVector3Range(const Vector3& min, const Vector3& max) {
	return {
		RandomRange(min.x, max.x),
		RandomRange(min.y, max.y),
		RandomRange(min.z, max.z)
	};
}

Vector4 ParticleManager::RandomVector4Range(const Vector4& min, const Vector4& max) {
	return {
		RandomRange(min.x, max.x),
		RandomRange(min.y, max.y),
		RandomRange(min.z, max.z),
		RandomRange(min.w, max.w)
	};
}

Vector4 ParticleManager::LerpColor(const Vector4& start, const Vector4& end, float t) {
	t = std::clamp(t, 0.0f, 1.0f);

	return {
		start.x + (end.x - start.x) * t,
		start.y + (end.y - start.y) * t,
		start.z + (end.z - start.z) * t,
		start.w + (end.w - start.w) * t
	};
}

void ParticleManager::CreateParticleGroup(const std::string& name, const std::string& textureFilePath) {
	assert(particleCommon_);
	assert(srvManager_);

	assert(!particleGroups_.contains(name));
	assert(srvManager_->CanAllocate());

	TextureManager::GetInstance()->LoadTexture(textureFilePath);

	ParticleGroup group{};
	group.textureFilePath = textureFilePath;
	group.textureSrvIndex = TextureManager::GetInstance()->GetSrvIndex(textureFilePath);

	group.materialResource = particleCommon_->GetDxCommon()->CreateBufferResource(sizeof(Material));
	group.materialResource->Map(0, nullptr, reinterpret_cast<void**>(&group.materialData));
	group.materialData->color = { 1.0f, 1.0f, 1.0f, 1.0f };
	group.materialData->enableLighting = false;
	group.materialData->uvTransform = MakeIdentity4x4();

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

bool ParticleManager::HasParticleGroup(const std::string& name) const {
	return particleGroups_.contains(name);
}

void ParticleManager::ClearParticleGroup(const std::string& name) {
	auto it = particleGroups_.find(name);
	if (it == particleGroups_.end()) {
		return;
	}

	it->second.particles.clear();
	it->second.instanceCount = 0;
}

void ParticleManager::CreateParticleGroupIfNeeded(
	const std::string& name,
	const std::string& textureFilePath
) {
	if (HasParticleGroup(name)) {
		return;
	}

	CreateParticleGroup(name, textureFilePath);
}

void ParticleManager::InitializeParticleLife(Particle& particle, const ParticleBehavior& behavior) {
	particle.currentTime = 0.0f;
	particle.lifeTime = RandomRange(behavior.life.lifeTimeMin, behavior.life.lifeTimeMax);

	particle.enableLifeFade = behavior.life.enableLifeFade;
	particle.fadeOutStartRatio = std::clamp(behavior.life.fadeOutStartRatio, 0.0f, 0.99f);
}

void ParticleManager::InitializeParticleMotion(Particle& particle, const ParticleBehavior& behavior) {
	particle.movementMode = behavior.motion.mode;

	const ParticleLinearMotionDesc& linear = behavior.motion.linear;

	Vector3 velocityRandom = RandomVector3Range(
		{
			-linear.velocityRandomRange.x,
			-linear.velocityRandomRange.y,
			-linear.velocityRandomRange.z
		},
		{
			linear.velocityRandomRange.x,
			linear.velocityRandomRange.y,
			linear.velocityRandomRange.z
		}
	);

	Vector3 accelerationRandom = RandomVector3Range(
		{
			-linear.accelerationRandomRange.x,
			-linear.accelerationRandomRange.y,
			-linear.accelerationRandomRange.z
		},
		{
			linear.accelerationRandomRange.x,
			linear.accelerationRandomRange.y,
			linear.accelerationRandomRange.z
		}
	);

	particle.velocity = {
		linear.baseVelocity.x + velocityRandom.x,
		linear.baseVelocity.y + velocityRandom.y,
		linear.baseVelocity.z + velocityRandom.z
	};

	particle.acceleration = {
		linear.baseAcceleration.x + accelerationRandom.x,
		linear.baseAcceleration.y + accelerationRandom.y,
		linear.baseAcceleration.z + accelerationRandom.z
	};

	const ParticleSwayDesc& sway = behavior.motion.sway;

	particle.swayTime = 0.0f;
	particle.swayPhase = RandomRange(0.0f, 6.2831853f);
	particle.swayAxis = RandomVector3Range(
		{ -1.0f, -1.0f, -1.0f },
		{ 1.0f, 1.0f, 1.0f }
	);
	particle.swayAmplitude = sway.amplitude;
	particle.swayFrequency = sway.frequency;

	if (particle.movementMode == MovementMode::kVortexInward) {
		const ParticleVortexDesc& vortex = behavior.motion.vortex;

		particle.vortexCenter = vortex.center;
		particle.vortexAxis = vortex.axis;

		Vector3 offset = {
			particle.transform.translate.x - particle.vortexCenter.x,
			particle.transform.translate.y - particle.vortexCenter.y,
			particle.transform.translate.z - particle.vortexCenter.z
		};

		switch (particle.vortexAxis) {
		case VortexAxis::kX:
			// X軸まわり。YZ平面で回転し、Xが軸方向
			particle.vortexRadius = std::sqrt(offset.y * offset.y + offset.z * offset.z);
			particle.vortexAngle = std::atan2(offset.z, offset.y);
			particle.vortexHeightOffset = offset.x;
			break;

		case VortexAxis::kY:
			// Y軸まわり。XZ平面で回転し、Yが軸方向
			particle.vortexRadius = std::sqrt(offset.x * offset.x + offset.z * offset.z);
			particle.vortexAngle = std::atan2(offset.z, offset.x);
			particle.vortexHeightOffset = offset.y;
			break;

		case VortexAxis::kZ:
			// Z軸まわり。XY平面で回転し、Zが軸方向
			particle.vortexRadius = std::sqrt(offset.x * offset.x + offset.y * offset.y);
			particle.vortexAngle = std::atan2(offset.y, offset.x);
			particle.vortexHeightOffset = offset.z;
			break;
		}

		particle.vortexAngularSpeed = RandomRange(
			vortex.angularSpeedMin,
			vortex.angularSpeedMax
		);

		particle.vortexInwardSpeed = RandomRange(
			vortex.inwardSpeedMin,
			vortex.inwardSpeedMax
		);

		particle.vortexVerticalSpeed = RandomRange(
			vortex.verticalSpeedMin,
			vortex.verticalSpeedMax
		);
	}
}

void ParticleManager::InitializeParticleColor(Particle& particle, const ParticleBehavior& behavior) {
	const ParticleColorDesc& color = behavior.color;

	particle.colorChangeMode = color.mode;

	particle.startColor = RandomVector4Range(color.startColorMin, color.startColorMax);
	particle.endColor = RandomVector4Range(color.endColorMin, color.endColorMax);

	particle.randomColorMin = color.randomColorMin;
	particle.randomColorMax = color.randomColorMax;

	particle.randomColorChangeIntervalMin = color.randomColorChangeIntervalMin;
	particle.randomColorChangeIntervalMax = color.randomColorChangeIntervalMax;
	particle.randomColorLerpSpeed = color.randomColorLerpSpeed;

	particle.randomCurrentColor = particle.startColor;
	particle.randomTargetColor = RandomVector4Range(color.randomColorMin, color.randomColorMax);
	particle.randomColorChangeTimer = 0.0f;
	particle.randomColorChangeInterval = RandomRange(
		color.randomColorChangeIntervalMin,
		color.randomColorChangeIntervalMax
	);

	particle.color = particle.startColor;
}

void ParticleManager::Emit(
	const std::string& name,
	const Vector3& position,
	const Vector3& spawnSize,
	uint32_t count,
	const ParticleBehavior& behavior
) {
	auto it = particleGroups_.find(name);
	assert(it != particleGroups_.end());

	ParticleGroup& group = it->second;

	for (uint32_t i = 0; i < count; ++i) {
		if (group.particles.size() >= kMaxInstanceCount) {
			break;
		}

		Particle particle{};

		Vector3 halfSpawnSize = {
			spawnSize.x * 0.5f,
			spawnSize.y * 0.5f,
			spawnSize.z * 0.5f
		};

		Vector3 spawnOffset = RandomVector3Range(
			{ -halfSpawnSize.x, -halfSpawnSize.y, -halfSpawnSize.z },
			{ halfSpawnSize.x, halfSpawnSize.y, halfSpawnSize.z }
		);

		particle.transform.translate = {
			position.x + spawnOffset.x,
			position.y + spawnOffset.y,
			position.z + spawnOffset.z
		};

		particle.transform.rotate = { 0.0f, 0.0f, 0.0f };
		particle.startScale = RandomVector3Range(
			behavior.scale.startScaleMin,
			behavior.scale.startScaleMax
		);

		particle.endScale = RandomVector3Range(
			behavior.scale.endScaleMin,
			behavior.scale.endScaleMax
		);

		particle.enableScaleOverLife = behavior.scale.enableScaleOverLife;
		particle.transform.scale = particle.startScale;

		InitializeParticleLife(particle, behavior);
		InitializeParticleMotion(particle, behavior);
		InitializeParticleColor(particle, behavior);

		particle.billboardMode = behavior.render.billboardMode;

		group.particles.push_back(particle);
	}
}

void ParticleManager::UpdateParticleMotion(Particle& particle) {
	if (particle.movementMode == MovementMode::kLinear) {
		particle.velocity.x += particle.acceleration.x * deltaTime_;
		particle.velocity.y += particle.acceleration.y * deltaTime_;
		particle.velocity.z += particle.acceleration.z * deltaTime_;

		particle.transform.translate.x += particle.velocity.x * deltaTime_;
		particle.transform.translate.y += particle.velocity.y * deltaTime_;
		particle.transform.translate.z += particle.velocity.z * deltaTime_;
	}
	else if (particle.movementMode == MovementMode::kVortexInward) {
		particle.vortexAngle += particle.vortexAngularSpeed * deltaTime_;
		particle.vortexRadius -= particle.vortexInwardSpeed * deltaTime_;
		particle.vortexHeightOffset += particle.vortexVerticalSpeed * deltaTime_;

		if (particle.vortexRadius < 0.0f) {
			particle.vortexRadius = 0.0f;
		}

		const float cosAngle = std::cos(particle.vortexAngle);
		const float sinAngle = std::sin(particle.vortexAngle);

		switch (particle.vortexAxis) {
		case VortexAxis::kX:
			// X軸まわり。YZ平面で回る
			particle.transform.translate.x =
				particle.vortexCenter.x + particle.vortexHeightOffset;

			particle.transform.translate.y =
				particle.vortexCenter.y + cosAngle * particle.vortexRadius;

			particle.transform.translate.z =
				particle.vortexCenter.z + sinAngle * particle.vortexRadius;
			break;

		case VortexAxis::kY:
			// Y軸まわり。XZ平面で回る
			particle.transform.translate.x =
				particle.vortexCenter.x + cosAngle * particle.vortexRadius;

			particle.transform.translate.y =
				particle.vortexCenter.y + particle.vortexHeightOffset;

			particle.transform.translate.z =
				particle.vortexCenter.z + sinAngle * particle.vortexRadius;
			break;

		case VortexAxis::kZ:
			// Z軸まわり。XY平面で回る
			particle.transform.translate.x =
				particle.vortexCenter.x + cosAngle * particle.vortexRadius;

			particle.transform.translate.y =
				particle.vortexCenter.y + sinAngle * particle.vortexRadius;

			particle.transform.translate.z =
				particle.vortexCenter.z + particle.vortexHeightOffset;
			break;
		}
	}

	if (particle.swayAmplitude != 0.0f) {
		particle.swayTime += deltaTime_;

		float swayValue =
			std::sin(particle.swayTime * particle.swayFrequency + particle.swayPhase) *
			particle.swayAmplitude;

		particle.transform.translate.x += particle.swayAxis.x * swayValue * deltaTime_;
		particle.transform.translate.y += particle.swayAxis.y * swayValue * deltaTime_;
		particle.transform.translate.z += particle.swayAxis.z * swayValue * deltaTime_;
	}
}

void ParticleManager::UpdateParticleColor(Particle& particle) {
	float lifeRatio = particle.currentTime / particle.lifeTime;
	lifeRatio = std::clamp(lifeRatio, 0.0f, 1.0f);

	Vector4 baseColor = particle.startColor;

	switch (particle.colorChangeMode) {
	case ColorChangeMode::kConstant:
		baseColor = particle.startColor;
		break;

	case ColorChangeMode::kOverLife:
		baseColor = LerpColor(particle.startColor, particle.endColor, lifeRatio);
		break;

	case ColorChangeMode::kRandomLoop:
		particle.randomColorChangeTimer += deltaTime_;

		if (particle.randomColorChangeTimer >= particle.randomColorChangeInterval) {
			particle.randomColorChangeTimer = 0.0f;

			particle.randomTargetColor = RandomVector4Range(
				particle.randomColorMin,
				particle.randomColorMax
			);

			particle.randomColorChangeInterval = RandomRange(
				particle.randomColorChangeIntervalMin,
				particle.randomColorChangeIntervalMax
			);
		}

		particle.randomCurrentColor = LerpColor(
			particle.randomCurrentColor,
			particle.randomTargetColor,
			std::clamp(particle.randomColorLerpSpeed * deltaTime_, 0.0f, 1.0f)
		);

		baseColor = particle.randomCurrentColor;
		break;
	}

	if (particle.enableLifeFade) {
		float fadeRatio =
			(lifeRatio - particle.fadeOutStartRatio) /
			(1.0f - particle.fadeOutStartRatio);

		fadeRatio = std::clamp(fadeRatio, 0.0f, 1.0f);

		baseColor.w *= (1.0f - fadeRatio);
	}

	particle.color = baseColor;
}

bool ParticleManager::IsDeadParticle(const Particle& particle) const {
	return particle.currentTime >= particle.lifeTime;
}

Vector3 ParticleManager::LerpVector3(const Vector3& start, const Vector3& end, float t) {
	t = std::clamp(t, 0.0f, 1.0f);

	return {
		start.x + (end.x - start.x) * t,
		start.y + (end.y - start.y) * t,
		start.z + (end.z - start.z) * t
	};
}

void ParticleManager::UpdateParticleScale(Particle& particle) {
	if (!particle.enableScaleOverLife) {
		return;
	}

	float lifeRatio = particle.currentTime / particle.lifeTime;
	lifeRatio = std::clamp(lifeRatio, 0.0f, 1.0f);

	particle.transform.scale = LerpVector3(
		particle.startScale,
		particle.endScale,
		lifeRatio
	);
}

void ParticleManager::Update() {
	if (!camera_) {
		return;
	}

	for (auto& [name, group] : particleGroups_) {
		for (auto& particle : group.particles) {
			particle.currentTime += deltaTime_;

			UpdateParticleMotion(particle);
			UpdateParticleScale(particle);
			UpdateParticleColor(particle);
		}

		group.particles.erase(
			std::remove_if(
				group.particles.begin(),
				group.particles.end(),
				[this](const Particle& particle) {
					return IsDeadParticle(particle);
				}
			),
			group.particles.end()
		);

		group.instanceCount = 0;

		for (const auto& particle : group.particles) {
			if (group.instanceCount >= kMaxInstanceCount) {
				break;
			}

			Matrix4x4 worldMatrix{};

			if (particle.billboardMode == BillboardMode::kBillboard) {
				worldMatrix = MakeBillboardMatrix(
					camera_->GetWorldMatrix(),
					particle.transform.scale,
					particle.transform.translate
				);
			}
			else {
				worldMatrix = MakeAffineMatrix(
					particle.transform.scale,
					particle.transform.rotate,
					particle.transform.translate
				);
			}
			Matrix4x4 wvp = Multiply(worldMatrix, camera_->GetViewProjectionMatrix());

			group.instancingData[group.instanceCount].World = worldMatrix;
			group.instancingData[group.instanceCount].WVP = wvp;
			group.instancingData[group.instanceCount].color = particle.color;

			++group.instanceCount;
		}
	}
}

void ParticleManager::Draw() {
	auto* commandList = particleCommon_->GetDxCommon()->GetCommandList();

	for (auto& [name, group] : particleGroups_) {
		if (group.instanceCount == 0) {
			continue;
		}

		// ParticleGroupごとにBlendModeを切り替える
		particleCommon_->SetBlendMode(group.blendMode);
		particleCommon_->SetCommonRenderState();

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