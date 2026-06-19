#pragma once

#include <cstdint>
#include <string>

#include <d3d12.h>
#include <wrl.h>

#include "../math/Matrix4x4.h"
#include "../math/Vector3.h"
#include "../math/Vector4.h"
#include "ParticleCommon.h"

class Camera;
class DirectXCommon;
class SrvManager;

class GpuParticle {
public:
	static constexpr uint32_t kMaxParticles = 1024;

	struct ParticleData {
		Vector3 translate;
		float lifeTime;
		Vector3 scale;
		float currentTime;
		Vector3 rotate;
		float rotationSpeed;
		Vector3 velocity;
		float padding0;
		Vector4 color;
	};

	struct Material {
		Vector4 color;
		int32_t enableLighting;
		float alphaCutoff;
		int32_t flipU;
		int32_t flipV;
		Matrix4x4 uvTransform;
		float emissiveIntensity;
		float padding[3];
	};

	struct DirectionalLight {
		Vector4 color;
		Vector3 direction;
		float intensity;
	};

	struct EmitterSphere {
		Vector3 translate;
		float radius;
		uint32_t count;
		float frequency;
		float frequencyTime;
		uint32_t emit;
	};

	struct PerView {
		Matrix4x4 viewProjection;
		Matrix4x4 billboardMatrix;
	};

	struct PerFrame {
		float time;
		float deltaTime;
		float padding[2];
	};

	struct BehaviorSettings {
		float lifeTimeMin = 0.6f;
		float lifeTimeMax = 1.2f;
		float scaleMin = 0.08f;
		float scaleMax = 0.22f;
		float velocityMin = 0.4f;
		float velocityMax = 1.0f;
		float rotationSpeedMin = -4.0f;
		float rotationSpeedMax = 4.0f;
		Vector4 colorMin = { 0.8f, 0.35f, 0.25f, 0.65f };
		Vector4 colorMax = { 1.0f, 0.75f, 0.45f, 0.95f };
	};

	struct Config {
		std::string filePath = "resources/particles/gpu_particle.json";
		std::string textureFilePath = "resources/circle.png";
		ParticleCommon::BlendMode blendMode =
			ParticleCommon::BlendMode::kBlendModeAdd;
		bool autoEmit = true;
		EmitterSphere emitter{
			{ 0.0f, 1.7f, 0.0f },
			0.35f,
			10,
			0.5f,
			0.0f,
			0
		};
		BehaviorSettings behavior;
	};

	struct BehaviorForGPU {
		Vector4 lifeScaleVelocityMinRotationMin;
		Vector4 lifeScaleVelocityMaxRotationMax;
		Vector4 colorMin;
		Vector4 colorMax;
	};

	void Initialize(
		ParticleCommon* particleCommon,
		SrvManager* srvManager,
		const std::string& textureFilePath = "resources/circle.png"
	);
	void Reset();
	void Update();
	void Draw(Camera* camera);
	void DrawImGui(const char* windowTitle = "GPU Particle");
	bool LoadConfig(const std::string& filePath);
	bool SaveConfig(const std::string& filePath) const;

private:
	void CreateParticleResource();
	void CreateConstantBuffers();
	void CreateRootSignatures();
	void CreatePipelineStates();
	void InitializeParticlesOnGPU();
	void EmitParticlesOnGPU();
	void UpdateParticlesOnGPU();
	void ApplyConfigToGpu();
	bool ApplyTexture(const std::string& textureFilePath);
	void CopyStringsToBuffers();
	void TransitionParticleResource(D3D12_RESOURCE_STATES stateAfter);
	void TransitionFreeListIndexResource(D3D12_RESOURCE_STATES stateAfter);
	void TransitionFreeListResource(D3D12_RESOURCE_STATES stateAfter);

private:
	ParticleCommon* particleCommon_ = nullptr;
	SrvManager* srvManager_ = nullptr;
	DirectXCommon* dxCommon_ = nullptr;

	std::string textureFilePath_;
	uint32_t textureSrvIndex_ = 0;
	uint32_t particleSrvIndex_ = 0;
	uint32_t particleUavIndex_ = 0;
	uint32_t freeListIndexUavIndex_ = 0;
	uint32_t freeListUavIndex_ = 0;

	Microsoft::WRL::ComPtr<ID3D12Resource> particleResource_;
	D3D12_RESOURCE_STATES particleResourceState_ = D3D12_RESOURCE_STATE_COMMON;
	Microsoft::WRL::ComPtr<ID3D12Resource> freeListIndexResource_;
	D3D12_RESOURCE_STATES freeListIndexResourceState_ =
		D3D12_RESOURCE_STATE_COMMON;
	Microsoft::WRL::ComPtr<ID3D12Resource> freeListResource_;
	D3D12_RESOURCE_STATES freeListResourceState_ = D3D12_RESOURCE_STATE_COMMON;

	Microsoft::WRL::ComPtr<ID3D12Resource> materialResource_;
	Material* materialData_ = nullptr;
	Microsoft::WRL::ComPtr<ID3D12Resource> directionalLightResource_;
	DirectionalLight* directionalLightData_ = nullptr;
	Microsoft::WRL::ComPtr<ID3D12Resource> perViewResource_;
	PerView* perViewData_ = nullptr;
	Microsoft::WRL::ComPtr<ID3D12Resource> emitterResource_;
	EmitterSphere* emitterData_ = nullptr;
	Microsoft::WRL::ComPtr<ID3D12Resource> perFrameResource_;
	PerFrame* perFrameData_ = nullptr;
	Microsoft::WRL::ComPtr<ID3D12Resource> behaviorResource_;
	BehaviorForGPU* behaviorData_ = nullptr;

	Microsoft::WRL::ComPtr<ID3D12RootSignature> graphicsRootSignature_;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> graphicsPipelineStates_
		[static_cast<uint32_t>(ParticleCommon::BlendMode::kCountOfBlendMode)];
	Microsoft::WRL::ComPtr<ID3D12RootSignature> initializeRootSignature_;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> initializePipelineState_;
	Microsoft::WRL::ComPtr<ID3D12RootSignature> emitRootSignature_;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> emitPipelineState_;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> updatePipelineState_;

	bool needsInitialize_ = true;
	float elapsedTime_ = 0.0f;
	float deltaTime_ = 1.0f / 60.0f;
	Config config_;
	char configPathBuffer_[256]{};
	char texturePathBuffer_[256]{};
};
