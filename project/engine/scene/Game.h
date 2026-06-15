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
	FullscreenCopy* fullscreenCopy_ = nullptr;
};
