// 役割: SceneDocumentを実行時オブジェクトへ反映し、更新と描画を管理する。
#pragma once
#include "../../engine/scene/BaseScene.h"
#include "SceneRuntimeObjectBinding.h"
#include "system/SceneAgentSystem.h"
#include "system/SceneAttachmentSystem.h"
#include "system/SceneCameraSystem.h"
#include "system/SceneCombatSystem.h"
#include "system/SceneDebugSystem.h"
#include "system/SceneEnemySystem.h"
#include "system/SceneEventSystem.h"
#include "system/SceneEffectRenderSystem.h"
#include "system/SceneEnvironmentSystem.h"
#include "system/SceneLightingSystem.h"
#include "system/SceneMonitorSystem.h"
#include "system/SceneObjectSystem.h"
#include "system/SceneParticleSystem.h"
#include "system/ScenePhysicsSystem.h"
#include "system/ScenePrefabAnimationSystem.h"
#include "system/SceneProjectileSystem.h"
#include "system/SceneStatSystem.h"
#include "system/SceneStateMachineSystem.h"
#include "system/SceneTransitionSystem.h"

#include <cstdint>
#include <vector>

class Camera;
class Object3d;
class Player;

// 各SystemとCameraを所有し、Component同期から描画までの実行順だけを決定する。
class RuntimeScene : public BaseScene
{
public:
	// BaseSceneのライフサイクル入口。個別機能は対応するSystemへ委譲する。
	void Initialize() override;
	void Finalize() override;
	void Update(float deltaTime) override;
	void UpdatePaused() override;
	void Draw() override;
	Camera* GetRenderCamera() const override;
	void DrawWithCamera(Camera* viewCamera) override;
	void DrawEnvironment(Camera* viewCamera) override;
	void BindLighting() override;
	void DrawSceneContent(Camera* viewCamera) override;
	void PrepareSceneContent(Camera* viewCamera) override;
	void DrawPreparedSceneContent(Camera* viewCamera) override;
	void DrawForegroundEffects() override;
	void DrawForegroundEffectsWithCamera(Camera* viewCamera) override;
	void DrawOffscreenViews() override;
	void DrawShadow() override;
	void CollectShadowCasters(std::vector<Object3d*>& shadowCasters) override;
	void RenderShadowCasters(
		const std::vector<Object3d*>& shadowCasters
	) override;
	void SetDeferForegroundEffects(bool defer) override {
		effectRenderSystem_.SetDeferForegroundEffects(defer);
	}

private:
	void DrawSceneView(Camera* viewCamera, uint64_t skipEntityId = 0);
	void DrawPreparedSceneContentForView(
		Camera* viewCamera,
		uint64_t skipEntityId
	);
	bool ShouldHidePlayerModelForCamera(Camera* viewCamera) const;
	void ApplyRenderCamera(Camera* viewCamera);
	Camera* GetSceneViewCamera() const;

	Camera* camera_ = nullptr;
	Camera* debugCamera_ = nullptr;
	Player* player_ = nullptr;
	std::vector<SceneRuntimeObjectBinding> runtimeObjectBindings_;

	SceneAgentSystem agentSystem_;
	SceneAttachmentSystem attachmentSystem_;
	SceneCameraSystem cameraSystem_;
	SceneCombatSystem combatSystem_;
	SceneDebugSystem debugSystem_;
	SceneEnemySystem enemySystem_;
	SceneEventSystem eventSystem_;
	SceneEffectRenderSystem effectRenderSystem_;
	SceneEnvironmentSystem environmentSystem_;
	SceneLightingSystem lightingSystem_;
	SceneMonitorSystem monitorSystem_;
	SceneObjectSystem objectSystem_;
	SceneTransitionSystem transitionSystem_;
	SceneParticleSystem particleSystem_;
	ScenePhysicsSystem physicsSystem_;
	ScenePrefabAnimationSystem prefabAnimationSystem_;
	SceneProjectileSystem projectileSystem_;
	SceneStatSystem statSystem_;
	SceneStateMachineSystem stateMachineSystem_;
};

