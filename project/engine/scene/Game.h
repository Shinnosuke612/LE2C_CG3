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
	float vignetteScale_ = 16.0f;
	float vignettePower_ = 0.8f;
	float vignetteIntensity_ = 1.0f;
	int boxBlurKernelSize_ = 3;
	float boxBlurStrength_ = 1.0f;
	int gaussianBlurKernelSize_ = 3;
	float gaussianBlurSigma_ = 1.0f;
	float gaussianBlurStrength_ = 1.0f;
};
