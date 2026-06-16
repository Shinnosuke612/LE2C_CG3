#pragma once

#include <cstdint>
#include <string>

#include <d3d12.h>
#include <wrl.h>

#include "../math/Matrix4x4.h"
#include "../math/Vector3.h"
#include "../math/Vector4.h"

class Camera;
class DirectXCommon;
class ParticleCommon;
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
		float padding0;
		Vector3 velocity;
		float padding1;
		Vector4 color;
	};

	struct Material {
		Vector4 color;
		int32_t enableLighting;
		float alphaCutoff;
		int32_t flipU;
		int32_t flipV;
		Matrix4x4 uvTransform;
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

	void Initialize(
		ParticleCommon* particleCommon,
		SrvManager* srvManager,
		const std::string& textureFilePath = "resources/circle.png"
	);
	void Reset();
	void Update();
	void Draw(Camera* camera);

private:
	void CreateParticleResource();
	void CreateConstantBuffers();
	void CreateRootSignatures();
	void CreatePipelineStates();
	void InitializeParticlesOnGPU();
	void EmitParticlesOnGPU();
	void TransitionParticleResource(D3D12_RESOURCE_STATES stateAfter);
	void TransitionCounterResource(D3D12_RESOURCE_STATES stateAfter);

private:
	ParticleCommon* particleCommon_ = nullptr;
	SrvManager* srvManager_ = nullptr;
	DirectXCommon* dxCommon_ = nullptr;

	std::string textureFilePath_;
	uint32_t textureSrvIndex_ = 0;
	uint32_t particleSrvIndex_ = 0;
	uint32_t particleUavIndex_ = 0;
	uint32_t counterUavIndex_ = 0;

	Microsoft::WRL::ComPtr<ID3D12Resource> particleResource_;
	D3D12_RESOURCE_STATES particleResourceState_ = D3D12_RESOURCE_STATE_COMMON;
	Microsoft::WRL::ComPtr<ID3D12Resource> counterResource_;
	D3D12_RESOURCE_STATES counterResourceState_ = D3D12_RESOURCE_STATE_COMMON;

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

	Microsoft::WRL::ComPtr<ID3D12RootSignature> graphicsRootSignature_;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> graphicsPipelineState_;
	Microsoft::WRL::ComPtr<ID3D12RootSignature> initializeRootSignature_;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> initializePipelineState_;
	Microsoft::WRL::ComPtr<ID3D12RootSignature> emitRootSignature_;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> emitPipelineState_;

	bool needsInitialize_ = true;
	float elapsedTime_ = 0.0f;
	float deltaTime_ = 1.0f / 60.0f;
};
