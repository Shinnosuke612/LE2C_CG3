// 役割: タイトル画面の初期化、更新、描画を管理する。
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
	void Update(float deltaTime) override;
	void Draw() override;

private:
	Camera* camera_ = nullptr;
	std::vector<ParticleEmitter*> emitters_;
	Input* input = nullptr;

	ParticleEffectDesc editingEffect_{};
	ParticleEffectEditor particleEffectEditor_;
	ParticleEmitter* previewEmitter_ = nullptr;
};
