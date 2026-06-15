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
	float outlineLuminanceWeight_ = 1.0f;
	float outlineDepthWeight_ = 1.0f;
	float outlineThreshold_ = 0.1f;
	float outlineSoftness_ = 0.05f;
	float outlineThickness_ = 1.0f;
	float outlineColor_[4]{ 0.0f, 0.0f, 0.0f, 1.0f };
};
