#pragma once
#include "BaseScene.h"

class SceneManager
{
public:

	void Update();

	void Draw();
	void DrawShadow();

	void SetNextScene(BaseScene* nextScene) { nextScene_ = nextScene; }

	~SceneManager();
private:
	BaseScene* scene_ = nullptr;
	BaseScene* nextScene_ = nullptr;
};

