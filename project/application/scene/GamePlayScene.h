#pragma once
#include "../../engine/scene/BaseScene.h"
#include <cstdint>
#include <vector>
#include <memory>
#include <string>
#include <unordered_map>
#include "../../engine/audio/Audio.h"
#include "../../engine/particle/ParticleEffectResource.h"
#include "../../engine/particle/ParticleEffectEditor.h"
#include "../../engine/3d/LightManager.h"
#include "../../engine/3d/ShadowManager.h"
#include "../../engine/collision/OBBCollider.h"
#include "../../engine/effect/LightningRenderer.h"
#include "../../engine/3d/StarFieldGenerator.h"

class SpriteCommon;
class Sprite;
class Camera;
class Object3d;
class ParticleEmitter;
class Skybox;
class Player;
class SceneRenderTarget;
class SceneDocument;
struct SceneEntity;
struct SceneComponent;

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
		bool hasRenderer = false;
		OBBCollider collider;
		bool hasCollider = false;
	};

	struct SceneSpriteObject {
		Sprite* sprite = nullptr;
		std::string texturePath;
	};

	struct MonitorRuntime {
		Camera* camera = nullptr;
		SceneRenderTarget* renderTarget = nullptr;
		std::string targetCameraName;
		uint32_t width = 512;
		uint32_t height = 512;
		bool hideSelf = true;
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
	void DrawOffscreenViews() override;
	void DrawShadow() override;

private:
	void SyncSceneModelObjects();
	void ClearSceneModelObjects();
	void SyncSceneSpriteObjects();
	void ClearSceneSpriteObjects();
	void RebuildStaticColliders();
	Object3d* FindSceneModelObjectByName(const char* name) const;
	void SyncMonitorRenderers();
	void ClearMonitorRenderers();
	void DrawSceneView(Camera* viewCamera, uint64_t skipEntityId = 0);
	void ApplyRenderCamera(Camera* viewCamera);
	bool ApplyCameraComponentToCamera(
		const SceneDocument& document,
		const SceneEntity& cameraEntity,
		const SceneComponent& cameraComponent,
		Camera* camera,
		float aspectRatio
	) const;

	SpriteCommon* spriteCommon_ = nullptr;
	Camera* camera_ = nullptr;
	ParticleEmitter* emitter_ = nullptr;

	Object3d* plane_ = nullptr;
	Object3d* axis = nullptr;
	bool showSkeletonDebug_ = false;
	bool showJointNames_ = false;
	bool showJointAxes_ = true;
	float jointRadius_ = 0.018f;
	float jointAxisLength_ = 0.06f;
	Player* player_ = nullptr;
	std::vector<StageObject> stageObjects_;
	std::unordered_map<uint64_t, SceneModelObject> sceneModelObjects_;
	std::unordered_map<uint64_t, SceneSpriteObject> sceneSpriteObjects_;
	std::unordered_map<uint64_t, MonitorRuntime> monitorRuntimes_;
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
	std::string environmentMapPath_;
	StarFieldGenerator starFieldGenerator_;
};

