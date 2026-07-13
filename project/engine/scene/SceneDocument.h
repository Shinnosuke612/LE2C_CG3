#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "../math/Transform.h"
#include "../math/Vector2.h"
#include "../math/Vector4.h"

struct ScenePostProcessSettings {
	bool bloomEnabled = true;
	float baseExposure = 1.0f;
	int toneMapMode = 0;
	float bloomThreshold = 1.0f;
	float bloomSoftKnee = 0.5f;
	float bloomIntensity = 0.7f;
	int bloomBlurIterations = 4;
	int bloomDownsampleScale = 2;
	float bloomBlurRadius = 1.0f;
	bool grayscaleEnabled = false;
	bool vignetteEnabled = false;
	bool boxBlurEnabled = false;
	bool gaussianBlurEnabled = false;
	bool depthOfFieldEnabled = false;
	bool radialBlurEnabled = false;
	bool noiseEnabled = false;
	bool dissolveEnabled = false;
	bool outlineEnabled = false;
	bool underwaterEnabled = false;
	bool waterRefractionEnabled = false;
	float vignetteScale = 16.0f;
	float vignettePower = 0.8f;
	float vignetteIntensity = 1.0f;
	int boxBlurKernelSize = 3;
	float boxBlurStrength = 1.0f;
	int gaussianBlurKernelSize = 3;
	float gaussianBlurSigma = 1.0f;
	float gaussianBlurStrength = 1.0f;
	float depthOfFieldFocusDistance = 10.0f;
	float depthOfFieldFocusRange = 2.0f;
	float depthOfFieldBlurStrength = 1.0f;
	float depthOfFieldNearStrength = 0.0f;
	float depthOfFieldFarStrength = 1.0f;
	float depthOfFieldMaxRadius = 4.0f;
	Vector2 radialBlurCenter = { 0.5f, 0.5f };
	float radialBlurWidth = 0.01f;
	int radialBlurSamples = 10;
	bool noiseAnimate = true;
	float noiseAmount = 0.25f;
	float noiseScale = 1.0f;
	float noiseSpeed = 1.0f;
	float noiseSeed = 0.0f;
	int dissolveMaskIndex = 0;
	float dissolveThreshold = 0.0f;
	float dissolveEdgeWidth = 0.03f;
	Vector4 dissolveEdgeColor = { 1.0f, 0.4f, 0.3f, 1.0f };
	bool outlineLuminanceEnabled = false;
	bool outlineDepthEnabled = true;
	float outlineLuminanceWeight = 1.0f;
	float outlineDepthWeight = 1.0f;
	float outlineThreshold = 0.1f;
	float outlineSoftness = 0.05f;
	float outlineThickness = 1.0f;
	Vector4 outlineColor = { 0.0f, 0.0f, 0.0f, 1.0f };
	Vector4 underwaterTintColor = { 0.02f, 0.45f, 0.68f, 1.0f };
	float underwaterIntensity = 0.65f;
	float underwaterFogDensity = 0.035f;
	float underwaterDistortion = 0.012f;
	Vector4 waterRefractionTintColor = { 0.02f, 0.55f, 0.82f, 1.0f };
	float waterRefractionStrength = 0.018f;
	float waterRefractionEdgeSoftness = 0.08f;
	float waterRefractionTintStrength = 0.12f;
};

struct SceneDebugSettings {
	bool showCameraDirection = false;
	bool showColliders = false;
	bool showCameraPath = true;
	bool showCameraPathPointCameraDirection = true;
	bool showSkeleton = false;
	bool showJointNames = false;
	bool showJointAxes = true;
	float jointRadius = 0.018f;
	float jointAxisLength = 0.06f;
};

struct SceneTeamSettings {
	std::string name = "Team";
	bool agentBehaviorOverride = false;
	std::string agentGroupName;
	float agentMinSpeed = 1.0f;
	float agentMaxSpeed = 3.0f;
	float agentTurnSpeed = 2.5f;
	float agentWanderStrength = 0.8f;
	float agentWanderChangeInterval = 4.0f;
	float agentWanderDirectionRange = 1.1f;
	float agentWanderVerticalRange = 0.18f;
	bool agentRandomizeSeedOnPlay = true;
	int agentRandomSeed = 1;
	bool agentUseLeaderStartPosition = false;
	Vector3 agentLeaderStartPosition{};
	float agentFlockDecisionInterval = 0.25f;
	float agentFlockAcceleration = 4.0f;
	float agentFlockTurnRate = 1.5f;
	float agentMemberCenterFollow = 1.5f;
	float agentMemberJitterStrength = 0.35f;
	float agentMemberJitterFrequency = 0.9f;
	float agentMemberJitterUpdateInterval = 0.5f;
	float agentMemberJitterFollowSpeed = 2.0f;
	float agentMemberSpeedVariation = 0.15f;
	float agentMemberLeashDistance = 4.0f;
	float agentMemberLeashStrength = 1.5f;
	float agentMemberCatchupSpeed = 2.0f;
	float agentMemberSeparationUpdateInterval = 0.1f;
	float agentMemberSeparationBlend = 0.5f;
	bool agentUseTeamHeading = false;
	bool agentTeamHeadingFromAverage = true;
	Vector3 agentTeamHeadingDirection = { 0.0f, 0.0f, 1.0f };
	float agentTeamHeadingWeight = 0.75f;
	float agentTeamHeadingFollowSpeed = 2.5f;
	bool agentUseTeamRotation = false;
	float agentTeamRotationWeight = 0.6f;
	float agentTeamRotationFollowSpeed = 4.0f;
	bool agentAlignForwardToVelocity = true;
	std::string agentForwardAxis = "+Z";
	bool agentRotateAxisX = true;
	bool agentRotateAxisY = true;
	bool agentRotateAxisZ = false;
	float agentRotationFollowSpeed = 12.0f;
	float agentPitchFromVerticalVelocity = 1.0f;
	float agentBankingStrength = 0.0f;
	bool agentSchooling = false;
	float agentSchoolingUpdateInterval = 0.0f;
	float agentSchoolingUpdateJitter = 0.0f;
	int agentNeighborLimit = 0;
	float agentSchoolingBlend = 1.0f;
	float agentSeparationRadius = 1.2f;
	float agentAlignmentRadius = 4.0f;
	float agentCohesionRadius = 5.0f;
	float agentSeparationWeight = 1.8f;
	float agentAlignmentWeight = 0.8f;
	float agentCohesionWeight = 0.9f;
	Vector4 agentVisualColor = { 0.25f, 0.75f, 1.0f, 1.0f };
	bool agentEnableLighting = true;
};

struct SceneComponent {
	SceneComponent() = default;
	SceneComponent(const char* componentType) : type(componentType ? componentType : "") {}
	SceneComponent(std::string componentType, bool componentEnabled = true)
		: type(std::move(componentType)), enabled(componentEnabled) {}

	std::string type;
	bool enabled = true;
	std::string modelPath;
	std::string meshCullMode = "Back";
	bool meshEnvironmentReflectionOverride = false;
	float meshEnvironmentReflectionIntensity = 0.3f;
	std::string texturePath;
	bool environmentSkyboxEnabled = true;
	std::string environmentSkyboxPath;
	float environmentSkyboxIntensity = 1.0f;
	float environmentReflectionIntensity = 0.3f;
	Vector2 spriteSize = { 100.0f, 100.0f };
	Vector2 spriteAnchor = { 0.5f, 0.5f };
	Vector4 spriteColor = { 1.0f, 1.0f, 1.0f, 1.0f };
	bool spriteFlipX = false;
	bool spriteFlipY = false;
	bool cameraIsMain = false;
	float cameraFovY = 0.45f;
	float cameraNearClip = 0.1f;
	float cameraFarClip = 1000.0f;
	bool cameraInvertYaw = false;
	bool cameraInvertPitch = false;
	uint64_t monitorCameraEntityId = 0;
	std::string monitorCameraName;
	std::string monitorResolutionPreset = "Square 512";
	uint32_t monitorWidth = 512;
	uint32_t monitorHeight = 512;
	bool monitorHideSelf = true;
	float thirdPersonDistance = 8.0f;
	float thirdPersonAimDistance = 3.0f;
	Vector3 thirdPersonTargetOffset = { 0.0f, 1.35f, 0.0f };
	Vector3 thirdPersonAimTargetOffset = { 0.0f, 1.55f, 0.0f };
	float thirdPersonMouseSensitivity = 0.005f;
	float thirdPersonMinPitch = -1.45f;
	float thirdPersonMaxPitch = 1.35f;
	float thirdPersonOcclusionMargin = 0.45f;
	bool thirdPersonInvertYaw = false;
	bool thirdPersonInvertPitch = false;
	std::string physicsBodyType = "Static";
	float physicsMass = 1.0f;
	bool physicsUseGravity = true;
	float physicsGravityScale = 1.0f;
	float physicsDrag = 0.0f;
	float physicsRestitution = 0.0f;
	float physicsFriction = 0.0f;
	float physicsMaxFallSpeed = 100.0f;
	Vector3 physicsVelocity = { 0.0f, 0.0f, 0.0f };
	bool physicsFreezePositionX = false;
	bool physicsFreezePositionY = false;
	bool physicsFreezePositionZ = false;
	Vector3 colliderOffset = { 0.0f, 0.0f, 0.0f };
	Vector3 colliderSizeMultiplier = { 1.0f, 1.0f, 1.0f };
	Vector4 colliderDebugColor = { 0.2f, 0.95f, 0.7f, 1.0f };
	std::string colliderShape = "Box";
	float colliderSphereRadius = 0.5f;
	bool colliderDebugVisible = true;
	std::string colliderDebugDrawMode = "Wireframe";
	int colliderDebugSegments = 16;
	float playerMoveSpeed = 10.8f;
	float playerJumpVelocity = 37.2f;
	float playerTurnResponsiveness = 0.018f;
	float playerDashMultiplier = 1.65f;
	bool playerCameraRelativeMove = true;
	bool playerAllowJump = true;
	std::string agentBehaviorName = "Fish";
	std::string agentProfileName = "Default";
	std::string agentGroupName;
	uint64_t agentBoundsEntityId = 0;
	std::string agentBoundsName;
	uint64_t agentAttractorEntityId = 0;
	std::string agentAttractorTag;
	bool agentUseWaterBounds = true;
	float agentMinSpeed = 1.0f;
	float agentMaxSpeed = 3.0f;
	float agentTurnSpeed = 2.5f;
	float agentWanderStrength = 0.8f;
	float agentWanderChangeInterval = 4.0f;
	float agentWanderDirectionRange = 1.1f;
	float agentWanderVerticalRange = 0.18f;
	bool agentRandomizeSeedOnPlay = true;
	int agentRandomSeed = 1;
	float agentFlockDecisionInterval = 0.25f;
	float agentFlockAcceleration = 4.0f;
	float agentFlockTurnRate = 1.5f;
	float agentMemberCenterFollow = 1.5f;
	float agentMemberJitterStrength = 0.35f;
	float agentMemberJitterFrequency = 0.9f;
	float agentMemberJitterUpdateInterval = 0.5f;
	float agentMemberJitterFollowSpeed = 2.0f;
	float agentMemberSpeedVariation = 0.15f;
	float agentMemberLeashDistance = 4.0f;
	float agentMemberLeashStrength = 1.5f;
	float agentMemberCatchupSpeed = 2.0f;
	float agentMemberSeparationUpdateInterval = 0.1f;
	float agentMemberSeparationBlend = 0.5f;
	float agentBoundsWeight = 3.0f;
	bool agentUseTeamHeading = false;
	bool agentTeamHeadingFromAverage = true;
	Vector3 agentTeamHeadingDirection = { 0.0f, 0.0f, 1.0f };
	float agentTeamHeadingWeight = 0.75f;
	float agentTeamHeadingFollowSpeed = 2.5f;
	bool agentUseTeamRotation = false;
	float agentTeamRotationWeight = 0.6f;
	float agentTeamRotationFollowSpeed = 4.0f;
	bool agentAlignForwardToVelocity = true;
	std::string agentForwardAxis = "+Z";
	bool agentRotateAxisX = true;
	bool agentRotateAxisY = true;
	bool agentRotateAxisZ = false;
	float agentRotationFollowSpeed = 12.0f;
	float agentPitchFromVerticalVelocity = 1.0f;
	float agentBankingStrength = 0.0f;
	bool agentSchooling = false;
	float agentSchoolingUpdateInterval = 0.0f;
	float agentSchoolingUpdateJitter = 0.0f;
	int agentNeighborLimit = 0;
	float agentSchoolingBlend = 1.0f;
	float agentSeparationRadius = 1.2f;
	float agentAlignmentRadius = 4.0f;
	float agentCohesionRadius = 5.0f;
	float agentSeparationWeight = 1.8f;
	float agentAlignmentWeight = 0.8f;
	float agentCohesionWeight = 0.9f;
	float agentAttractorWeight = 0.0f;
	bool agentTeamSettingsOverride = false;
	Vector4 agentVisualColor = { 0.25f, 0.75f, 1.0f, 1.0f };
	bool agentEnableLighting = true;
	std::string attractorTag = "Default";
	std::string attractorTargetBehaviorName;
	std::string attractorTargetProfileName;
	float attractorRadius = 6.0f;
	float attractorStrength = 1.0f;
	Vector4 attractorVisualColor = { 1.0f, 0.35f, 0.45f, 1.0f };
	Vector3 waterHalfSize = { 10.0f, 4.0f, 10.0f };
	Vector3 waterOffset = { 0.0f, 0.0f, 0.0f };
	bool waterSurfaceEnabled = true;
	Vector4 waterSurfaceBaseColor = { 0.04f, 0.55f, 0.78f, 1.0f };
	Vector4 waterSurfaceHighlightColor = { 0.42f, 0.95f, 1.20f, 1.0f };
	float waterSurfaceAlpha = 0.36f;
	float waterSurfaceWaveScale = 1.0f;
	float waterSurfaceNormalStrength = 0.75f;
	float waterSurfaceFresnelPower = 3.0f;
	bool waterLightShaftEnabled = true;
	Vector4 waterLightColor = { 0.55f, 0.90f, 1.15f, 1.0f };
	Vector3 waterLightDirection = { -0.25f, -1.0f, 0.18f };
	float waterLightIntensity = 0.55f;
	float waterLightDensity = 0.045f;
	float waterLightCausticsIntensity = 0.35f;
	float waterLightCausticsScale = 0.08f;
	float waterLightCausticsSpeed = 1.0f;
	float waterLightBreakupStrength = 1.0f;
	float waterLightWarpStrength = 1.0f;
	float waterLightNoiseScale = 1.0f;
	int waterLightSampleCount = 16;
	float waterMoveSpeedMultiplier = 0.45f;
	float waterGravityScale = 0.55f;
	float waterDrag = 4.0f;
	float waterMaxFallSpeed = 5.0f;
	float waterSwimUpSpeed = 12.0f;
	std::string cameraPathTargetCameraName;
	std::string cameraPathTriggerType = "Key";
	std::string cameraPathTriggerKey = "C";
	float cameraPathEnterDuration = 0.5f;
	float cameraPathExitDuration = 0.5f;
	std::string cameraPathInterpolation = "Linear";
	std::string cameraPathDefaultEasing = "SmoothStep";
	bool cameraPathReturnToPreviousCamera = true;
	bool cameraPathStartFromCurrentCamera = true;
	bool cameraPathAutoCollectChildPoints = true;
	float cameraPathPointDurationToNext = 1.0f;
	std::string cameraPathPointEasingToNext = "SmoothStep";
};

struct SceneEntity {
	uint64_t id = 0;
	uint64_t parentId = 0;
	std::string name;
	bool folder = false;
	bool folderTeamEnabled = false;
	bool active = true;
	bool locked = false;
	std::string teamName;
	Transform transform{};
	std::string modelPath;
	std::string spriteTexturePath;
	Vector2 spriteSize = { 100.0f, 100.0f };
	Vector2 spriteAnchor = { 0.5f, 0.5f };
	Vector4 spriteColor = { 1.0f, 1.0f, 1.0f, 1.0f };
	bool spriteFlipX = false;
	bool spriteFlipY = false;
	std::vector<SceneComponent> components;
};

class SceneDocument {
public:
	void Clear(const std::string& sceneName = {});

	bool Load(const std::string& filePath);
	bool Save(const std::string& filePath);

	SceneEntity& CreateEntity(const std::string& name, uint64_t parentId = 0);
	bool RemoveEntity(uint64_t id);
	uint64_t DuplicateEntity(uint64_t id);
	bool SetParent(uint64_t id, uint64_t parentId);
	bool MoveEntity(uint64_t id, int direction);
	bool MoveEntityToParent(uint64_t id, uint64_t parentId);
	bool MoveEntityToSibling(uint64_t id, uint64_t siblingId, bool after);
	bool AddComponent(uint64_t id, const std::string& type);
	bool RemoveComponent(uint64_t id, const std::string& type);
	bool IsDescendantOf(uint64_t id, uint64_t potentialAncestorId) const;
	SceneTeamSettings& CreateTeam(const std::string& name);
	bool RenameTeam(const std::string& oldName, const std::string& newName);
	bool RemoveTeam(const std::string& name);
	SceneTeamSettings* FindTeam(const std::string& name);
	const SceneTeamSettings* FindTeam(const std::string& name) const;
	std::string ResolveInheritedFolderTeamName(uint64_t entityId) const;
	const SceneTeamSettings* ResolveEntityTeam(const SceneEntity& entity) const;
	SceneEntity* FindEntity(uint64_t id);
	const SceneEntity* FindEntity(uint64_t id) const;
	SceneEntity* FindEntityByName(const std::string& name);
	const SceneEntity* FindEntityByName(const std::string& name) const;

	const std::string& GetSceneName() const { return sceneName_; }
	void SetSceneName(const std::string& sceneName) {
		sceneName_ = sceneName;
		MarkDirty();
	}
	std::vector<SceneEntity>& GetEntities() { return entities_; }
	const std::vector<SceneEntity>& GetEntities() const { return entities_; }
	std::vector<SceneTeamSettings>& GetTeams() { return teams_; }
	const std::vector<SceneTeamSettings>& GetTeams() const { return teams_; }
	ScenePostProcessSettings& GetPostProcessSettings() {
		return postProcessSettings_;
	}
	const ScenePostProcessSettings& GetPostProcessSettings() const {
		return postProcessSettings_;
	}
	void SetPostProcessSettings(const ScenePostProcessSettings& settings) {
		postProcessSettings_ = settings;
		MarkDirty();
	}
	const SceneDebugSettings& GetDebugSettings() const {
		return debugSettings_;
	}
	void SetDebugSettings(const SceneDebugSettings& settings) {
		debugSettings_ = settings;
		MarkDirty();
	}
	bool IsDirty() const { return dirty_; }
	uint64_t GetRevision() const { return revision_; }
	void MarkDirty() {
		dirty_ = true;
		++revision_;
	}
	void MarkClean() { dirty_ = false; }

private:
	bool LoadInternal(const std::string& filePath);
	void RebuildNextId();
	void ValidateHierarchy();

	std::string sceneName_;
	std::vector<SceneEntity> entities_;
	std::vector<SceneTeamSettings> teams_;
	ScenePostProcessSettings postProcessSettings_{};
	SceneDebugSettings debugSettings_{};
	uint64_t nextId_ = 1;
	bool dirty_ = false;
	uint64_t revision_ = 0;
};
