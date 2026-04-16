#pragma once
#include <d3d12.h>
#include <d3dx12.h>
#include <wrl.h>
#include "../math/Vector2.h"
#include "../math/Vector3.h"
#include "../math/Vector4.h"
#include "Matrix4x4.h"

class DirectXCommon;
class Camera;

class ParticleCommon{
public:
	struct VertexData{
		Vector4 position;
		Vector2 texcoord;
		Vector3 normal;
	};

public:
	void Initialize(DirectXCommon* dxCommon);
	void SetCommonRenderState();

	void SetDefaultCamera(Camera* camera){ defaultCamera_ = camera; }

	DirectXCommon* GetDxCommon() const{ return dxCommon_; }
	Camera* GetDefaultCamera() const{ return defaultCamera_; }

	ID3D12Resource* GetVertexResource() const{ return vertexResource_.Get(); }
	const D3D12_VERTEX_BUFFER_VIEW& GetVertexBufferView() const{ return vertexBufferView_; }

private:
	void MakeRootSignature();
	void GenerateGraphicsPipeline();
	void CreateVertexResource();
	inline D3D12_STATIC_SAMPLER_DESC MakeStaticSamplerS0();

private:
	DirectXCommon* dxCommon_ = nullptr;
	Camera* defaultCamera_ = nullptr;

	Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature_ = nullptr;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> graphicsPipelineState_ = nullptr;

	Microsoft::WRL::ComPtr<ID3D12Resource> vertexResource_;
	D3D12_VERTEX_BUFFER_VIEW vertexBufferView_{};
	VertexData* vertexData_ = nullptr;
};