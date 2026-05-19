#pragma once
#include <vector>
#include "../base/Framework.h"

class BaseScene;

class Game : public Framework {
public:
	void Initialize() override;
	void Finalize() override;
	void Update() override;
	void Draw() override;

private:
	BaseScene* scene_ = nullptr;

};