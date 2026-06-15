#pragma once

#include <cstdint>
#include <d3d12.h>
#include <wrl.h>

class DirectXCommon;

class FullscreenCopy {
public:
	enum class Effect {
		kCopy,
		kGrayscale,
		kVignette,
		kBoxBlur
	};

	struct Parameters {
		float vignetteScale = 16.0f;
		float vignettePower = 0.8f;
		float vignetteIntensity = 1.0f;
		float blurStrength = 1.0f;
		uint32_t blurRadius = 1;
		float padding[3]{};
	};

	void Initialize(DirectXCommon* dxCommon);
	void SetParameters(const Parameters& parameters);
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
	Microsoft::WRL::ComPtr<ID3D12PipelineState> boxBlurPipelineState_;
	Microsoft::WRL::ComPtr<ID3D12Resource> parametersResource_;
	Parameters* parametersData_ = nullptr;
};
