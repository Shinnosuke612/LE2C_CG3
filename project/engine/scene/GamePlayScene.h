#pragma once
#include "BaseScene.h"
#include <cstdint>
#include <vector>
#include <memory>
#include <string>
#include <unordered_map>
#include "../audio/Audio.h"
#include "../particle/ParticleEffectResource.h"
#include "../particle/ParticleEffectEditor.h"
#include "../3d/LightManager.h"
#include "../3d/ShadowManager.h"
#include "../collision/OBBCollider.h"
#include "../effect/LightningRenderer.h"
#include "../3d/StarFieldGenerator.h"

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

	struct SceneModelObject {
		Object3d* object = nullptr;
		std::string modelPath;
	};

	struct SceneSpriteObject {
		Sprite* sprite = nullptr;
		std::string texturePath;
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
	void SyncSceneModelObjects();
	void ClearSceneModelObjects();
	void SyncSceneSpriteObjects();
	void ClearSceneSpriteObjects();

	SpriteCommon* spriteCommon_ = nullptr;
	Camera* camera_ = nullptr;
	ParticleEmitter* emitter_ = nullptr;

	Object3d* object3d_ = nullptr;
	Object3d* plane_ = nullptr;
	Object3d* axis = nullptr;
	Object3d* animatedCube_ = nullptr;
	Object3d* human_ = nullptr;
	bool showSkeletonDebug_ = false;
	bool showJointNames_ = false;
	bool showJointAxes_ = true;
	float jointRadius_ = 0.018f;
	float jointAxisLength_ = 0.06f;
	Player* player_ = nullptr;
	std::vector<StageObject> stageObjects_;
	std::unordered_map<uint64_t, SceneModelObject> sceneModelObjects_;
	std::unordered_map<uint64_t, SceneSpriteObject> sceneSpriteObjects_;
	std::vector<OBBCollider*> staticColliders_;
	std::vector<Sprite*> sprites_;
	Audio::SoundData soundData_{};
	
	ParticleEffectDesc editingEffect_{};
	ParticleEmitter* editorPreviewEmitter_ = nullptr;
	ParticleEffectEditor particleEffectEditor_;
	ParticleEffectDesc planeBurstEffect_{};
	ParticleEmitter* planeBurstEmitter_ = nullptr;
	ParticleEffectDesc ringBurstEffect_{};
	ParticleEmitter* ringBurstEmitter_ = nullptr;
	std::unique_ptr<LightManager> lightManager_;
	std::unique_ptr<ShadowManager> shadowManager_;
	std::unique_ptr<LightningRenderer> lightningRenderer_;
	Skybox* skybox_ = nullptr;
	StarFieldGenerator starFieldGenerator_;
};

