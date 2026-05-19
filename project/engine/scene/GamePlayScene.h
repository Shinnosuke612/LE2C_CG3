#pragma once
#include "BaseScene.h"
#include <vector>
#include "../audio/Audio.h"

class SpriteCommon;
class Sprite;
class Camera;
class Object3d;
class ParticleEmitter;

class GamePlayScene : public BaseScene
{

public: //メンバ関数

	//初期化
	void Initialize() override;

	//終了
	void Finalize() override;

	//更新
	void Update() override;

	//描画
	void Draw() override;

private:
	SpriteCommon* spriteCommon_ = nullptr;
	Camera* camera_ = nullptr;
	ParticleEmitter* emitter_ = nullptr;

	Object3d* object3d_ = nullptr;
	std::vector<Sprite*> sprites_;
	Audio::SoundData soundData_{};
};

