#pragma once

#include <d3d12.h>
#include <wrl.h>

class DirectXCommon;

class FullscreenCopy {
public:
	enum class Effect {
		kCopy,
		kGrayscale,
		kVignette
	};

	struct VignetteParameters {
		float scale = 16.0f;
		float power = 0.8f;
		float intensity = 1.0f;
		float padding = 0.0f;
	};

	void Initialize(DirectXCommon* dxCommon);
	void SetVignetteParameters(const VignetteParameters& parameters);
	void Draw(
		D3D12_GPU_DESCRIPTOR_HANDLE textureHandle,
		Effect effect = Effect::kCopy
	);

private:
	void CreateRootSignature();
	void CreatePipelineState();

	DirectXCommon* dxCommon_ = nullptr;
	Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature_;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> copyPipelineState_;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> grayscalePipelineState_;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> vignettePipelineState_;
	Microsoft::WRL::ComPtr<ID3D12Resource> vignetteResource_;
	VignetteParameters* vignetteData_ = nullptr;
};
