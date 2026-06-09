#pragma once
#include "BaseScene.h"
#include <vector>
#include <memory>
#include "../audio/Audio.h"
#include "../particle/ParticleEffectResource.h"
#include "../3d/LightManager.h"
#include "../3d/ShadowManager.h"
#include "../collision/OBBCollider.h"

class SpriteCommon;
class Sprite;
class Camera;
class Object3d;
class ParticleEmitter;
class Skybox;
class Player;

class GamePlayScene : public BaseScene
{
private:
	struct StageObject {
		Object3d* object = nullptr;
		OBBCollider collider;
	};

public: //メンバ関数

	//初期化
	void Initialize() override;

	//終了
	void Finalize() override;

	//更新
	void Update() override;

	//描画
	void Draw() override;
	void DrawShadow() override;

private:
	SpriteCommon* spriteCommon_ = nullptr;
	Camera* camera_ = nullptr;
	ParticleEmitter* emitter_ = nullptr;

	Object3d* object3d_ = nullptr;
	Object3d* plane_ = nullptr;
	Object3d* axis = nullptr;
	Player* player_ = nullptr;
	std::vector<StageObject> stageObjects_;
	std::vector<OBBCollider*> staticColliders_;
	std::vector<Sprite*> sprites_;
	Audio::SoundData soundData_{};
	
	ParticleEffectDesc editingEffect_{};
	ParticleEmitter* previewEmitter_ = nullptr;
	std::unique_ptr<LightManager> lightManager_;
	std::unique_ptr<ShadowManager> shadowManager_;
	Skybox* skybox_ = nullptr;
};

