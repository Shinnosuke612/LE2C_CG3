#pragma once
#include <vector>
#include "../../engine/scene/BaseScene.h"
#include "../../engine/particle/ParticleEffectResource.h"
#include "../../engine/particle/ParticleEffectEditor.h"

class Camera;
class ParticleEmitter;
class Input;

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
	Input* input = nullptr;

	ParticleEffectDesc editingEffect_{};
	ParticleEffectEditor particleEffectEditor_;
	ParticleEmitter* previewEmitter_ = nullptr;
};
