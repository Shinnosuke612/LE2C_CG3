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
		kBoxBlur,
		kGaussianBlur
	};

	struct Parameters {
		float vignetteScale = 16.0f;
		float vignettePower = 0.8f;
		float vignetteIntensity = 1.0f;
		float blurStrength = 1.0f;
		uint32_t blurRadius = 1;
		float gaussianSigma = 1.0f;
		float padding[2]{};
	};

	void Initialize(DirectXCommon* dxCommon);
	void BeginFrame();
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
	Microsoft::WRL::ComPtr<ID3D12PipelineState> gaussianBlurPipelineState_;
	static constexpr uint32_t kMaxDrawsPerFrame = 8;
	Microsoft::WRL::ComPtr<ID3D12Resource>
		parameterResources_[kMaxDrawsPerFrame];
	Parameters* parameterData_[kMaxDrawsPerFrame]{};
	Parameters pendingParameters_{};
	uint32_t drawIndex_ = 0;
};
