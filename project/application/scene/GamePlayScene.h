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
#include "../../engine/collision/SphereCollider.h"
#include "../../engine/physics/PhysicsBody.h"
#include "../../engine/physics/PhysicsWorld.h"
#include "../../engine/effect/LightningRenderer.h"
#include "../../engine/effect/WaterSurfaceRenderer.h"
#include "../../engine/math/Vector3.h"
#include "../../engine/math/Vector4.h"
#include "../../engine/3d/StarFieldGenerator.h"
#include "../../engine/3d/CameraPathRuntime.h"
#include "../camera/ThirdPersonCameraController.h"

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
		bool animatorInitialized = false;
		bool hasAnimator = false;
		bool animatorPlayOnStart = true;
		bool animatorLoop = true;
		float animatorSpeed = 1.0f;
		int animatorDefaultClip = 0;
		float animatorTransitionDuration = 0.2f;
		std::string animatorBlendCurve = "SmoothStep";
		OBBCollider boxCollider;
		SphereCollider sphereCollider;
		Collider* collider = nullptr;
		bool hasCollider = false;
		Vector4 colliderDebugColor = { 0.2f, 0.95f, 0.7f, 1.0f };
		bool colliderDebugVisible = true;
		std::string colliderDebugDrawMode = "Wireframe";
		uint32_t colliderDebugSegments = 16;
		PhysicsBody physicsBody;
		bool hasPhysicsBody = false;
	};

	struct SceneSpriteObject {
		Sprite* sprite = nullptr;
		std::string texturePath;
	};

	struct MonitorRuntime {
		Camera* camera = nullptr;
		SceneRenderTarget* renderTarget = nullptr;
		uint32_t width = 512;
		uint32_t height = 512;
		bool hideSelf = true;
		uint64_t debugPassFrame = 0;
		bool debugResolvedCamera = false;
		bool debugRendered = false;
		bool debugTextureApplied = false;
		uint64_t debugTargetCameraId = 0;
		std::string debugTargetCameraName;
		Vector3 debugTargetTranslate{};
		Vector3 debugTargetRotate{};
		bool debugTargetIsMain = false;
		bool debugTargetHasPlayerBehavior = false;
		uint64_t debugSrvPtr = 0;
		uint64_t debugAppliedTextureOverridePtr = 0;
		std::string debugStatus = "Waiting for offscreen pass";
	};

	struct AgentRuntime {
		Vector3 velocity{};
		Vector3 jitterOffset{};
		Vector3 jitterTargetLocal{};
		Vector3 cachedSchoolingSteering{};
		Vector3 cachedSeparationSteering{};
		float phase = 0.0f;
		float jitterTimer = 0.0f;
		float schoolingTimer = 0.0f;
		float separationTimer = 0.0f;
		uint64_t flockSeedId = 0;
		uint32_t jitterStep = 0;
		bool initialized = false;
		bool flockInitialized = false;
		bool schoolingCacheValid = false;
		bool separationCacheValid = false;
	};

	struct TeamRuntime {
		Vector3 center{};
		Vector3 velocity{};
		Vector3 heading = { 0.0f, 0.0f, 1.0f };
		Vector3 rotation{};
		Vector3 wanderDirection = { 0.0f, 0.0f, 1.0f };
		Vector3 desiredDirection = { 0.0f, 0.0f, 1.0f };
		std::string forwardAxis = "+Z";
		float phase = 0.0f;
		float wanderTimer = 0.0f;
		float decisionTimer = 0.0f;
		float desiredSpeed = 0.0f;
		uint64_t seedId = 0;
		uint32_t wanderStep = 0;
		bool initialized = false;
		bool decisionValid = false;
	};

public: //メンバ関数

	//初期化
	void Initialize() override;

	//終了
	void Finalize() override;

	//更新
	void Update() override;
	void UpdatePaused() override;

	//描画
	void Draw() override;
	void DrawForegroundEffects() override;
	void DrawOffscreenViews() override;
	void DrawShadow() override;
	void SetDeferForegroundEffects(bool defer) override {
		deferForegroundEffects_ = defer;
	}

private:
	void SyncSceneModelObjects(float deltaTime);
	void ClearSceneModelObjects();
	void SyncSceneSpriteObjects();
	void ClearSceneSpriteObjects();
	void SyncEnvironmentComponent();
	void RebuildStaticColliders();
	void ApplyPlayerBehaviorComponent(const SceneDocument& document);
	void ApplyPlayerPhysicsComponent(const SceneDocument& document);
	void ApplyWaterVolumes(const SceneDocument& document);
	void UpdateAgentBehaviors(SceneDocument& document, float deltaTime);
	void StepPhysics(float deltaTime);
	void DrawColliderDebug() const;
	void LoadSceneDebugSettings();
	void SaveSceneDebugSettings();
	Object3d* FindSceneModelObjectByName(const char* name) const;
	void SyncMonitorRenderers();
	void ClearMonitorRenderers();
	void DrawSceneView(Camera* viewCamera, uint64_t skipEntityId = 0);
	void DrawWaterSurfaces(
		const SceneDocument& document,
		Camera* viewCamera,
		uint64_t skipEntityId
	);
	void DrawForegroundEffectsForCamera(
		Camera* viewCamera,
		uint64_t skipEntityId = 0
	);
	void DrawRefractedEffectsForCamera(
		Camera* viewCamera,
		uint64_t skipEntityId = 0
	);
	bool ShouldHidePlayerModelForCamera(Camera* viewCamera) const;
	void ApplyRenderCamera(Camera* viewCamera);
	Camera* GetSceneViewCamera() const;
	void InitializePauseDebugCamera();
	void DrawMonitorDebugWindow();
#if defined(_DEBUG) || defined(DEVELOPMENT)
	void DrawAnimationControls(const SceneDocument& document);
#endif
	bool TryStartCameraPath(SceneDocument& document);
	void DrawCameraPathDebug(
		const SceneDocument& document,
		bool showPath,
		bool showPointCameraDirection
	);
	void SyncPlayerCameraControllerFromCurrentCamera(
		const SceneDocument& document
	);
	bool ApplyCameraComponentToCamera(
		const SceneDocument& document,
		const SceneEntity& cameraEntity,
		const SceneComponent& cameraComponent,
		Camera* camera,
		float aspectRatio
	) const;
	bool ApplyPlayerCameraMouseLook(SceneDocument& document);
	void ApplyPlayerCameraDissolve(const SceneDocument& document);

	SpriteCommon* spriteCommon_ = nullptr;
	Camera* camera_ = nullptr;
	Camera* debugCamera_ = nullptr;
	ParticleEmitter* emitter_ = nullptr;

	Object3d* plane_ = nullptr;
	Object3d* axis = nullptr;
	bool showSkeletonDebug_ = false;
	bool showCameraDebug_ = false;
	bool showColliderDebug_ = false;
	bool showCameraPathDebug_ = true;
	bool showCameraPathPointCameraDebug_ = true;
	bool showJointNames_ = false;
	bool showJointAxes_ = true;
	bool deferForegroundEffects_ = false;
	uint64_t animationControlEntityId_ = 0;
	float animationControlTransitionDuration_ = 0.2f;
	float jointRadius_ = 0.018f;
	float jointAxisLength_ = 0.06f;
	Player* player_ = nullptr;
	ThirdPersonCameraController playerCameraController_;
	bool playerCameraInitialized_ = false;
	bool debugCameraInitialized_ = false;
	std::vector<StageObject> stageObjects_;
	std::unordered_map<uint64_t, SceneModelObject> sceneModelObjects_;
	std::unordered_map<uint64_t, SceneSpriteObject> sceneSpriteObjects_;
	std::unordered_map<uint64_t, MonitorRuntime> monitorRuntimes_;
	std::unordered_map<uint64_t, AgentRuntime> agentRuntimes_;
	std::unordered_map<std::string, TeamRuntime> agentTeamRuntimes_;
	uint64_t monitorDebugFrame_ = 0;
	bool monitorDebugForceProbeCamera_ = false;
	std::vector<Collider*> staticColliders_;
	PhysicsWorld physicsWorld_;
	CameraPathRuntime cameraPathRuntime_;
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
	std::unique_ptr<WaterSurfaceRenderer> waterSurfaceRenderer_;
	Skybox* skybox_ = nullptr;
	std::string environmentMapPath_;
	float environmentReflectionIntensity_ = 0.3f;
	StarFieldGenerator starFieldGenerator_;
};

