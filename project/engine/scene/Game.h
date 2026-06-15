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
	SceneManager* sceneManager_ = nullptr;
	SceneRenderTarget* sceneRenderTarget_ = nullptr;
	SceneRenderTarget* postProcessRenderTarget_ = nullptr;
	FullscreenCopy* fullscreenCopy_ = nullptr;
	int postProcessEffect_ = 0;
	float vignetteScale_ = 16.0f;
	float vignettePower_ = 0.8f;
	float vignetteIntensity_ = 1.0f;
	int boxBlurKernelSize_ = 3;
	float boxBlurStrength_ = 1.0f;
};
