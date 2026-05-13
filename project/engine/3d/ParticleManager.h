#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include <wrl.h>
#include "../math/Vector3.h"
#include "../math/Vector4.h"
#include "../math/Transform.h"
#include "../math/Matrix4x4.h"
#include <d3d12.h>  

class ParticleCommon;
class SrvManager;
class Camera;

class ParticleManager{
public:
	struct TransformationMatrix{
		Matrix4x4 WVP;
		Matrix4x4 World;
	};

	struct Material{
		Vector4 color;
		int32_t enableLighting;
		float padding[3];
		Matrix4x4 uvTransform;
	};

	struct DirectionalLight{
		Vector4 color;
		Vector3 direction;
		float intensity;
	};

	struct ParticleBehavior{
		Vector3 baseVelocity = { 0.0f, -0.8f, 0.0f };
		Vector3 baseAcceleration = { 0.0f, -0.001f, 0.0f };

		Vector3 velocityRandomRange = { 0.0f, 0.0f, 0.01f };
		Vector3 accelerationRandomRange = { 0.0f, 0.0f, 0.0f };

		float lifeTimeMin = 15.0f;
		float lifeTimeMax = 15.0f;

		Vector3 startScaleMin = { 1.0f, 1.0f, 1.0f };
		Vector3 startScaleMax = { 1.0f, 1.0f, 1.0f };

		float swayAmplitude = 1.0f;
		float swayFrequency = 1.0f;
	};

	struct Particle{
		Transform transform;
		Vector3 velocity;
		Vector3 acceleration;
		float currentTime;
		float lifeTime;
		Vector4 color;
		float swayTime;
		float swayPhase;
		Vector3 swayAxis;
		float swayAmplitude;
		float swayFrequency;
	};

private:
	struct ParticleGroup{
		std::string textureFilePath;
		uint32_t textureSrvIndex = 0;

		std::vector<Particle> particles;

		Microsoft::WRL::ComPtr<ID3D12Resource> materialResource;
		Material* materialData = nullptr;

		Microsoft::WRL::ComPtr<ID3D12Resource> instancingResource;
		TransformationMatrix* instancingData = nullptr;
		uint32_t instanceSrvIndex = 0;
		uint32_t instanceCount = 0;
	};

public:
	static const uint32_t kMaxInstanceCount = 1024;

public:
	void Initialize(ParticleCommon* particleCommon, SrvManager* srvManager);
	void Update();
	void Draw();

	void CreateParticleGroup(const std::string& name, const std::string& textureFilePath);
	void Emit(const std::string& name, const Vector3& position, const Vector3& spawnSize, uint32_t count, const ParticleBehavior& behavior);

	void SetCamera(Camera* camera){ camera_ = camera; }

private:
	void CreateDirectionalLightResource();
	float RandomRange(float min, float max);
	Vector3 RandomVector3Range(const Vector3& min, const Vector3& max);

private:
	ParticleCommon* particleCommon_ = nullptr;
	SrvManager* srvManager_ = nullptr;
	Camera* camera_ = nullptr;

	std::unordered_map<std::string, ParticleGroup> particleGroups_;

	Microsoft::WRL::ComPtr<ID3D12Resource> directionalLightResource_;
	DirectionalLight* directionalLightData_ = nullptr;

	float deltaTime_ = 1.0f / 60.0f;
};