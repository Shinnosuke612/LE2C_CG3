#pragma once
#include <vector>
#include "BaseScene.h"

class Camera;
class ParticleEmitter;

class TitleScene : public BaseScene
{
public:
	void Initialize() override;
	void Finalize() override;
	void Update() override;
	void Draw() override;

private:
	Camera* camera_ = nullptr;
	std::vector<ParticleEmitter*> emitters_;
};