#pragma once
#include "../base/Framework.h"
#include "SceneManager.h"

class SceneRenderTarget;
class FullscreenCopy;

class Game : public Framework {
public:
	void Initialize() override;
	void Finalize() override;
	void Update() override;
	void Draw() override;

private:
	int GetEnabledPostEffectCount() const;
	SceneRenderTarget* GetPostProcessOutputTarget() const;

	SceneManager* sceneManager_ = nullptr;
	SceneRenderTarget* sceneRenderTarget_ = nullptr;
	SceneRenderTarget* postProcessRenderTargets_[2]{};
	FullscreenCopy* fullscreenCopy_ = nullptr;
	bool grayscaleEnabled_ = false;
	bool vignetteEnabled_ = false;
	bool boxBlurEnabled_ = false;
	bool gaussianBlurEnabled_ = false;
	bool radialBlurEnabled_ = false;
	bool noiseEnabled_ = false;
	bool dissolveEnabled_ = false;
	bool outlineEnabled_ = false;
	bool outlineLuminanceEnabled_ = false;
	bool outlineDepthEnabled_ = true;
	float vignetteScale_ = 16.0f;
	float vignettePower_ = 0.8f;
	float vignetteIntensity_ = 1.0f;
	int boxBlurKernelSize_ = 3;
	float boxBlurStrength_ = 1.0f;
	int gaussianBlurKernelSize_ = 3;
	float gaussianBlurSigma_ = 1.0f;
	float gaussianBlurStrength_ = 1.0f;
	float radialBlurCenter_[2]{ 0.5f, 0.5f };
	float radialBlurWidth_ = 0.01f;
	int radialBlurSamples_ = 10;
	bool noiseAnimate_ = true;
	float noiseTime_ = 0.0f;
	float noiseAmount_ = 0.25f;
	float noiseScale_ = 1.0f;
	float noiseSpeed_ = 1.0f;
	float noiseSeed_ = 0.0f;
	int dissolveMaskIndex_ = 0;
	float dissolveThreshold_ = 0.0f;
	float dissolveEdgeWidth_ = 0.03f;
	float dissolveEdgeColor_[4]{ 1.0f, 0.4f, 0.3f, 1.0f };
	float outlineLuminanceWeight_ = 1.0f;
	float outlineDepthWeight_ = 1.0f;
	float outlineThreshold_ = 0.1f;
	float outlineSoftness_ = 0.05f;
	float outlineThickness_ = 1.0f;
	float outlineColor_[4]{ 0.0f, 0.0f, 0.0f, 1.0f };
};
