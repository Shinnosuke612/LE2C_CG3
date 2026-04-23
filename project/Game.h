#pragma once
#include <vector>
#include "engine/base/Framework.h"
#include "engine/audio/Audio.h"

class SpriteCommon;
class Sprite;
class Camera;
class Object3dCommon;
class Object3d;
class ParticleCommon;
class ParticleManager;
class ParticleEmitter;

class Game : public Framework {
public:
	// 初期化
	void Initialize() override;

	// 終了
	void Finalize() override;

	// 毎フレーム更新
	void Update() override;

	// 描画
	void Draw() override;

private:
	Camera* camera_ = nullptr;

	ParticleManager* particleManager_ = nullptr;
	ParticleEmitter* emitter_ = nullptr;

	Object3d* object3d_ = nullptr;
	std::vector<Sprite*> sprites_;

	Audio::SoundData soundData_{};
};