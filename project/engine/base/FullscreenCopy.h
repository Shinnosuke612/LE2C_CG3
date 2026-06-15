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
		kGaussianBlur,
		kRadialBlur,
		kOutline
	};

	struct Parameters {
		float vignetteScale = 16.0f;
		float vignettePower = 0.8f;
		float vignetteIntensity = 1.0f;
		float blurStrength = 1.0f;
		uint32_t blurRadius = 1;
		float gaussianSigma = 1.0f;
		float padding[2]{};
		float outlineLuminanceWeight = 1.0f;
		float outlineDepthWeight = 1.0f;
		float outlineThreshold = 0.1f;
		float outlineSoftness = 0.05f;
		float outlineThickness = 1.0f;
		float cameraNear = 0.1f;
		float cameraFar = 1000.0f;
		uint32_t outlineFlags = 0;
		float outlineColor[4]{ 0.0f, 0.0f, 0.0f, 1.0f };
		float radialBlurCenter[2]{ 0.5f, 0.5f };
		float radialBlurWidth = 0.01f;
		uint32_t radialBlurSamples = 10;
	};

	void Initialize(DirectXCommon* dxCommon);
	void BeginFrame();
	void SetParameters(const Parameters& parameters);
	void Draw(
		D3D12_GPU_DESCRIPTOR_HANDLE textureHandle,
		D3D12_GPU_DESCRIPTOR_HANDLE depthTextureHandle,
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
	Microsoft::WRL::ComPtr<ID3D12PipelineState> radialBlurPipelineState_;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> outlinePipelineState_;
	static constexpr uint32_t kMaxDrawsPerFrame = 8;
	Microsoft::WRL::ComPtr<ID3D12Resource>
		parameterResources_[kMaxDrawsPerFrame];
	Parameters* parameterData_[kMaxDrawsPerFrame]{};
	Parameters pendingParameters_{};
	uint32_t drawIndex_ = 0;
};
