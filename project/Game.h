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
	void Initialize() override;
	void Finalize() override;
	void Update() override;
	void Draw() override;

private:
	SpriteCommon* spriteCommon_ = nullptr;
	Camera* camera_ = nullptr;
	Object3dCommon* object3dCommon_ = nullptr;

	ParticleCommon* particleCommon_ = nullptr;
	ParticleManager* particleManager_ = nullptr;
	ParticleEmitter* emitter_ = nullptr;

	Object3d* object3d_ = nullptr;
	std::vector<Sprite*> sprites_;

	Audio::SoundData soundData_{};
};