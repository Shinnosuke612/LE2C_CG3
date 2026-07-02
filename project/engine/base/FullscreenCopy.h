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
		kNoise,
		kDissolve,
		kOutline,
		kDepthOfField
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
		float dissolveThreshold = 0.0f;
		float dissolveEdgeWidth = 0.03f;
		float dissolvePadding[2]{};
		float dissolveEdgeColor[4]{ 1.0f, 0.4f, 0.3f, 1.0f };
		float noiseTime = 0.0f;
		float noiseAmount = 0.25f;
		float noiseScale = 1.0f;
		float noiseSeed = 0.0f;
		float dofFocusDistance = 10.0f;
		float dofFocusRange = 2.0f;
		float dofNearStrength = 0.0f;
		float dofFarStrength = 1.0f;
		float dofMaxRadius = 4.0f;
		float dofPadding[3]{};
	};

	void Initialize(DirectXCommon* dxCommon);
	void BeginFrame();
	void SetParameters(const Parameters& parameters);
	void Draw(
		D3D12_GPU_DESCRIPTOR_HANDLE textureHandle,
		D3D12_GPU_DESCRIPTOR_HANDLE depthTextureHandle,
		D3D12_GPU_DESCRIPTOR_HANDLE maskTextureHandle,
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
	Microsoft::WRL::ComPtr<ID3D12PipelineState> noisePipelineState_;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> dissolvePipelineState_;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> outlinePipelineState_;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> depthOfFieldPipelineState_;
	static constexpr uint32_t kMaxDrawsPerFrame = 16;
	Microsoft::WRL::ComPtr<ID3D12Resource>
		parameterResources_[kMaxDrawsPerFrame];
	Parameters* parameterData_[kMaxDrawsPerFrame]{};
	Parameters pendingParameters_{};
	uint32_t drawIndex_ = 0;
};
