#pragma once
#include <d3d12.h>
#include <d3dx12.h>
#include <wrl.h>
#include "../math/Vector2.h"
#include "../math/Vector3.h"
#include "../math/Vector4.h"
#include "../math/Matrix4x4.h"

class DirectXCommon;
class Camera;

class ParticleCommon {
private:
	static ParticleCommon* instance_;

	ParticleCommon() = default;
	~ParticleCommon() = default;
	ParticleCommon(const ParticleCommon&) = delete;
	ParticleCommon& operator=(const ParticleCommon&) = delete;

public:
	static ParticleCommon* GetInstance();
	static void DeleteInstance();

public:
	struct VertexData{
		Vector4 position;
		Vector2 texcoord;
		Vector3 normal;
		Vector4 color;
	};

	enum class BlendMode {
		kBlendModeNone,
		kBlendModeNormal,
		kBlendModeAdd,
		kBlendModeSubtract,
		kBlendModeMultiply,
		kBlendModeScreen,
		kCountOfBlendMode,
	};

public:
	void Initialize(DirectXCommon* dxCommon);
	void ResetState();
	void SetCommonRenderState();

	void SetDefaultCamera(Camera* camera){ defaultCamera_ = camera; }

	DirectXCommon* GetDxCommon() const{ return dxCommon_; }
	Camera* GetDefaultCamera() const{ return defaultCamera_; }

	ID3D12Resource* GetVertexResource() const{ return vertexResource_.Get(); }
	const D3D12_VERTEX_BUFFER_VIEW& GetVertexBufferView() const{ return vertexBufferView_; }

	void SetBlendMode(BlendMode blendMode) { currentBlendMode_ = blendMode; }
	BlendMode GetBlendMode() const { return currentBlendMode_; }

private:
	void MakeRootSignature();
	void GenerateGraphicsPipeline();
	void CreateVertexResource();
	inline D3D12_STATIC_SAMPLER_DESC MakeStaticSamplerS0();

private:
	DirectXCommon* dxCommon_ = nullptr;
	Camera* defaultCamera_ = nullptr;
	bool isInitialized_ = false;

	Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature_ = nullptr;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> graphicsPipelineStates_[static_cast<uint32_t>(BlendMode::kCountOfBlendMode)];
	BlendMode currentBlendMode_ = BlendMode::kBlendModeNormal;

	Microsoft::WRL::ComPtr<ID3D12Resource> vertexResource_;
	D3D12_VERTEX_BUFFER_VIEW vertexBufferView_{};
	VertexData* vertexData_ = nullptr;
};
