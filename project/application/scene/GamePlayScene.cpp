#include "GamePlayScene.h"

#include "TitleScene.h"
#include "../../engine/scene/SceneManager.h"
#include "../../engine/scene/EditorSession.h"
#include "../../engine/scene/SceneDocument.h"

#include "../../engine/base/DirectXCommon.h"
#include "../../engine/base/RenderFormats.h"
#include "../../engine/base/SceneRenderTarget.h"
#include "../../engine/3d/SrvManager.h"
#include "../../engine/base/ImGuiManager.h"
#include "../../engine/io/Input.h"
#include "../../engine/debug/DebugRenderer.h"

#include "../../engine/2d/SpriteCommon.h"
#include "../../engine/2d/Sprite.h"
#include "../../engine/2d/TextureManager.h"
#include "../../engine/3d/Camera.h"
#include "../../engine/3d/Object3dCommon.h"
#include "../../engine/3d/Object3d.h"
#include "../../engine/3d/ModelManager.h"
#include "../../engine/particle/ParticleManager.h"
#include "../../engine/particle/ParticleEmitter.h"
#include "../../engine/3d/LightManager.h"
#include "../../engine/3d/ShadowManager.h"
#include "../../engine/3d/Skybox.h"
#include "../../engine/effect/LightningRenderer.h"
#include "../../engine/effect/WaterSurfaceRenderer.h"
#include "../player/Player.h"
#include "../../engine/utility/EditableResourcePath.h"
#include "../../engine/math/Math.h"
#include "../../engine/math/Matrix4x4.h"
#include "../../engine/math/Vector2.h"

#include "../../externals/imgui/imgui.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <functional>
#include <unordered_set>

namespace {
	const SceneEntity* FindSceneEntity(
		const SceneManager* sceneManager,
		const char* name
	) {
		SceneDocument* document = sceneManager
			? sceneManager->GetActiveSceneDocument()
			: nullptr;
		return document ? document->FindEntityByName(name) : nullptr;
	}

	Transform GetSceneTransform(
		const SceneManager* sceneManager,
		const char* name,
		const Transform& fallback
	) {
		const SceneEntity* entity = FindSceneEntity(sceneManager, name);
		return entity ? entity->transform : fallback;
	}

	const SceneComponent* FindEnabledComponent(
		const SceneEntity& entity,
		const char* componentName
	);

	Transform ResolveScene3DTransform(
		const SceneDocument& document,
		const SceneEntity& entity
	);

	SceneComponent* FindComponent(
		SceneEntity& entity,
		const char* componentName
	) {
		const auto found = std::find_if(
			entity.components.begin(),
			entity.components.end(),
			[componentName](const SceneComponent& component) {
				return component.type == componentName;
			}
		);
		return found == entity.components.end() ? nullptr : &(*found);
	}

	bool HasComponent(const SceneEntity& entity, const char* componentName) {
		return std::find_if(
			entity.components.begin(),
			entity.components.end(),
			[componentName](const SceneComponent& component) {
				return component.enabled && component.type == componentName;
			}
		) != entity.components.end();
	}

	Object3dCommon::CullMode ToObjectCullMode(const std::string& cullMode) {
		if (cullMode == "None") {
			return Object3dCommon::CullMode::kNone;
		}
		if (cullMode == "Front") {
			return Object3dCommon::CullMode::kFront;
		}
		return Object3dCommon::CullMode::kBack;
	}

	PhysicsBodyType ToPhysicsBodyType(const std::string& bodyType) {
		if (bodyType == "Dynamic") {
			return PhysicsBodyType::Dynamic;
		}
		if (bodyType == "Kinematic") {
			return PhysicsBodyType::Kinematic;
		}
		return PhysicsBodyType::Static;
	}

	float ResolveEnvironmentReflectionIntensity(
		const SceneComponent* meshRenderer,
		float environmentDefault
	) {
		if (
			meshRenderer &&
			meshRenderer->meshEnvironmentReflectionOverride
		) {
			return std::clamp(
				meshRenderer->meshEnvironmentReflectionIntensity,
				0.0f,
				1.0f
			);
		}
		return std::clamp(environmentDefault, 0.0f, 1.0f);
	}

	SceneComponent ResolveAgentBehaviorSettings(
		const SceneDocument& document,
		const SceneEntity& entity,
		const SceneComponent& behavior
	) {
		SceneComponent resolved = behavior;
		const SceneTeamSettings* team = document.ResolveEntityTeam(entity);
		if (
			!team ||
			!team->agentBehaviorOverride ||
			resolved.agentTeamSettingsOverride
		) {
			return resolved;
		}

		resolved.agentGroupName = team->agentGroupName.empty()
			? team->name
			: team->agentGroupName;
		resolved.agentMinSpeed = team->agentMinSpeed;
		resolved.agentMaxSpeed = team->agentMaxSpeed;
		resolved.agentTurnSpeed = team->agentTurnSpeed;
		resolved.agentWanderStrength = team->agentWanderStrength;
		resolved.agentWanderChangeInterval = team->agentWanderChangeInterval;
		resolved.agentWanderDirectionRange = team->agentWanderDirectionRange;
		resolved.agentWanderVerticalRange = team->agentWanderVerticalRange;
		resolved.agentRandomizeSeedOnPlay = team->agentRandomizeSeedOnPlay;
		resolved.agentRandomSeed = team->agentRandomSeed;
		resolved.agentFlockDecisionInterval = team->agentFlockDecisionInterval;
		resolved.agentFlockAcceleration = team->agentFlockAcceleration;
		resolved.agentFlockTurnRate = team->agentFlockTurnRate;
		resolved.agentMemberCenterFollow = team->agentMemberCenterFollow;
		resolved.agentMemberJitterStrength = team->agentMemberJitterStrength;
		resolved.agentMemberJitterFrequency = team->agentMemberJitterFrequency;
		resolved.agentMemberJitterUpdateInterval =
			team->agentMemberJitterUpdateInterval;
		resolved.agentMemberJitterFollowSpeed =
			team->agentMemberJitterFollowSpeed;
		resolved.agentMemberSpeedVariation = team->agentMemberSpeedVariation;
		resolved.agentMemberLeashDistance = team->agentMemberLeashDistance;
		resolved.agentMemberLeashStrength = team->agentMemberLeashStrength;
		resolved.agentMemberCatchupSpeed = team->agentMemberCatchupSpeed;
		resolved.agentMemberSeparationUpdateInterval =
			team->agentMemberSeparationUpdateInterval;
		resolved.agentMemberSeparationBlend = team->agentMemberSeparationBlend;
		resolved.agentUseTeamHeading = team->agentUseTeamHeading;
		resolved.agentTeamHeadingFromAverage =
			team->agentTeamHeadingFromAverage;
		resolved.agentTeamHeadingDirection =
			team->agentTeamHeadingDirection;
		resolved.agentTeamHeadingWeight = team->agentTeamHeadingWeight;
		resolved.agentTeamHeadingFollowSpeed =
			team->agentTeamHeadingFollowSpeed;
		resolved.agentUseTeamRotation = team->agentUseTeamRotation;
		resolved.agentTeamRotationWeight = team->agentTeamRotationWeight;
		resolved.agentTeamRotationFollowSpeed =
			team->agentTeamRotationFollowSpeed;
		resolved.agentAlignForwardToVelocity =
			team->agentAlignForwardToVelocity;
		resolved.agentForwardAxis = team->agentForwardAxis;
		resolved.agentRotateAxisX = team->agentRotateAxisX;
		resolved.agentRotateAxisY = team->agentRotateAxisY;
		resolved.agentRotateAxisZ = team->agentRotateAxisZ;
		resolved.agentRotationFollowSpeed = team->agentRotationFollowSpeed;
		resolved.agentPitchFromVerticalVelocity =
			team->agentPitchFromVerticalVelocity;
		resolved.agentBankingStrength = team->agentBankingStrength;
		resolved.agentSchooling = team->agentSchooling;
		resolved.agentSchoolingUpdateInterval =
			team->agentSchoolingUpdateInterval;
		resolved.agentSchoolingUpdateJitter =
			team->agentSchoolingUpdateJitter;
		resolved.agentNeighborLimit = team->agentNeighborLimit;
		resolved.agentSchoolingBlend = team->agentSchoolingBlend;
		resolved.agentSeparationRadius = team->agentSeparationRadius;
		resolved.agentAlignmentRadius = team->agentAlignmentRadius;
		resolved.agentCohesionRadius = team->agentCohesionRadius;
		resolved.agentSeparationWeight = team->agentSeparationWeight;
		resolved.agentAlignmentWeight = team->agentAlignmentWeight;
		resolved.agentCohesionWeight = team->agentCohesionWeight;
		resolved.agentVisualColor = team->agentVisualColor;
		resolved.agentEnableLighting = team->agentEnableLighting;
		return resolved;
	}

	SceneComponent ResolveTeamLeaderSettings(
		const SceneDocument& document,
		const SceneEntity& entity,
		const SceneComponent& behavior
	) {
		SceneComponent resolved = behavior;
		const SceneTeamSettings* team = document.ResolveEntityTeam(entity);
		if (!team) {
			return resolved;
		}
		resolved.agentMinSpeed = team->agentMinSpeed;
		resolved.agentMaxSpeed = team->agentMaxSpeed;
		resolved.agentWanderStrength = team->agentWanderStrength;
		resolved.agentWanderChangeInterval = team->agentWanderChangeInterval;
		resolved.agentWanderDirectionRange = team->agentWanderDirectionRange;
		resolved.agentWanderVerticalRange = team->agentWanderVerticalRange;
		resolved.agentRandomizeSeedOnPlay = team->agentRandomizeSeedOnPlay;
		resolved.agentRandomSeed = team->agentRandomSeed;
		resolved.agentFlockDecisionInterval = team->agentFlockDecisionInterval;
		resolved.agentFlockAcceleration = team->agentFlockAcceleration;
		resolved.agentFlockTurnRate = team->agentFlockTurnRate;
		resolved.agentUseTeamHeading = team->agentUseTeamHeading;
		resolved.agentTeamHeadingDirection = team->agentTeamHeadingDirection;
		resolved.agentTeamHeadingWeight = team->agentTeamHeadingWeight;
		resolved.agentForwardAxis = team->agentForwardAxis;
		return resolved;
	}

	const SceneComponent* FindEnabledComponent(
		const SceneEntity& entity,
		const char* componentName
	) {
		const auto found = std::find_if(
			entity.components.begin(),
			entity.components.end(),
			[componentName](const SceneComponent& component) {
				return component.enabled && component.type == componentName;
			}
		);
		return found == entity.components.end() ? nullptr : &(*found);
	}

	bool IsSceneEntityActive(
		const SceneManager* sceneManager,
		const char* name
	) {
		const SceneEntity* entity = FindSceneEntity(sceneManager, name);
		return !entity || entity->active;
	}

	bool IsEntityActiveInHierarchy(
		const SceneDocument& document,
		const SceneEntity& entity
	) {
		if (!entity.active) {
			return false;
		}
		const SceneEntity* parent = document.FindEntity(entity.parentId);
		while (parent) {
			if (!parent->active) {
				return false;
			}
			parent = document.FindEntity(parent->parentId);
		}
		return true;
	}

	bool ResolveMonitorTargetCamera(
		const SceneDocument& document,
		const SceneComponent& monitorRenderer,
		const SceneEntity*& cameraEntity,
		const SceneComponent*& cameraComponent
	) {
		cameraEntity = nullptr;
		cameraComponent = nullptr;

		auto tryResolveEntity = [&document](
			const SceneEntity* entity,
			const SceneEntity*& resolvedEntity,
			const SceneComponent*& resolvedCamera
		) {
			if (!entity || !IsEntityActiveInHierarchy(document, *entity)) {
				return false;
			}
			const SceneComponent* camera =
				FindEnabledComponent(*entity, "Camera");
			if (!camera) {
				return false;
			}
			resolvedEntity = entity;
			resolvedCamera = camera;
			return true;
		};

		if (monitorRenderer.monitorCameraEntityId != 0) {
			const SceneEntity* entity =
				document.FindEntity(monitorRenderer.monitorCameraEntityId);
			const bool idMatchesStoredName =
				monitorRenderer.monitorCameraName.empty() ||
				(entity && entity->name == monitorRenderer.monitorCameraName);
			if (
				idMatchesStoredName &&
				tryResolveEntity(entity, cameraEntity, cameraComponent)
			) {
				return true;
			}
		}

		if (!monitorRenderer.monitorCameraName.empty()) {
			const SceneEntity* entity =
				document.FindEntityByName(monitorRenderer.monitorCameraName);
			if (tryResolveEntity(entity, cameraEntity, cameraComponent)) {
				return true;
			}
		}

		if (monitorRenderer.monitorCameraEntityId != 0) {
			const SceneEntity* entity =
				document.FindEntity(monitorRenderer.monitorCameraEntityId);
			if (tryResolveEntity(entity, cameraEntity, cameraComponent)) {
				return true;
			}
		}

		return false;
	}

	bool HasMonitorCameraBinding(const SceneComponent& monitorRenderer) {
		return
			monitorRenderer.monitorCameraEntityId != 0 ||
			!monitorRenderer.monitorCameraName.empty();
	}

	void ApplyMainCameraComponent(
		const SceneDocument& document,
		Camera* camera
	) {
		if (!camera) {
			return;
		}

		const SceneEntity* fallbackEntity = nullptr;
		const SceneComponent* fallbackCamera = nullptr;
		const SceneEntity* mainEntity = nullptr;
		const SceneComponent* mainCamera = nullptr;
		for (const SceneEntity& entity : document.GetEntities()) {
			if (!IsEntityActiveInHierarchy(document, entity)) {
				continue;
			}
			const SceneComponent* cameraComponent =
				FindEnabledComponent(entity, "Camera");
			if (!cameraComponent) {
				continue;
			}
			if (!fallbackEntity) {
				fallbackEntity = &entity;
				fallbackCamera = cameraComponent;
			}
			if (cameraComponent->cameraIsMain) {
				mainEntity = &entity;
				mainCamera = cameraComponent;
				break;
			}
		}

		const SceneEntity* cameraEntity = mainEntity ? mainEntity : fallbackEntity;
		const SceneComponent* cameraComponent =
			mainCamera ? mainCamera : fallbackCamera;
		if (!cameraEntity || !cameraComponent) {
			return;
		}

		camera->SetOrbitMode(false);
		const Transform cameraTransform =
			ResolveScene3DTransform(document, *cameraEntity);
		camera->SetTranslate(cameraTransform.translate);
		camera->SetRotate(cameraTransform.rotate);
		camera->SetFovY(std::clamp(
			cameraComponent->cameraFovY,
			0.0174532925f,
			3.12413936f
		));
		camera->SetNearClip((std::max)(cameraComponent->cameraNearClip, 0.001f));
		camera->SetFarClip((std::max)(
			cameraComponent->cameraFarClip,
			cameraComponent->cameraNearClip + 0.001f
		));
	}

	Transform ResolveScene2DTransform(
		const SceneDocument& document,
		const SceneEntity& entity,
		std::unordered_set<uint64_t>& visited
	) {
		Transform result = entity.transform;
		if (entity.parentId == 0 || !visited.insert(entity.id).second) {
			return result;
		}
		const SceneEntity* parent = document.FindEntity(entity.parentId);
		if (!parent) {
			return result;
		}
		const Transform parentTransform = ResolveScene2DTransform(
			document,
			*parent,
			visited
		);
		const float scaledX = result.translate.x * parentTransform.scale.x;
		const float scaledY = result.translate.y * parentTransform.scale.y;
		const float cosine = std::cos(parentTransform.rotate.z);
		const float sine = std::sin(parentTransform.rotate.z);
		result.translate.x = parentTransform.translate.x + scaledX * cosine - scaledY * sine;
		result.translate.y = parentTransform.translate.y + scaledX * sine + scaledY * cosine;
		result.rotate.z += parentTransform.rotate.z;
		result.scale.x *= parentTransform.scale.x;
		result.scale.y *= parentTransform.scale.y;
		return result;
	}

	Transform ResolveScene2DTransform(
		const SceneDocument& document,
		const SceneEntity& entity
	) {
		std::unordered_set<uint64_t> visited;
		return ResolveScene2DTransform(document, entity, visited);
	}

	Matrix4x4 ResolveSceneWorldMatrix(
		const SceneDocument& document,
		const SceneEntity& entity,
		std::unordered_set<uint64_t>& visited
	) {
		const Matrix4x4 local = MakeAffineMatrix(
			entity.transform.scale,
			entity.transform.rotate,
			entity.transform.translate
		);
		if (entity.parentId == 0 || !visited.insert(entity.id).second) {
			return local;
		}
		const SceneEntity* parent = document.FindEntity(entity.parentId);
		if (!parent) {
			return local;
		}
		return Multiply(
			local,
			ResolveSceneWorldMatrix(document, *parent, visited)
		);
	}

	Transform ResolveScene3DTransform(
		const SceneDocument& document,
		const SceneEntity& entity
	) {
		std::unordered_set<uint64_t> visited;
		const Matrix4x4 world =
			ResolveSceneWorldMatrix(document, entity, visited);
		Transform result = entity.transform;
		Vector3 scale{};
		Vector3 rotate{};
		Vector3 translate{};
		if (DecomposeAffineMatrix(world, scale, rotate, translate)) {
			result.scale = scale;
			result.rotate = rotate;
			result.translate = translate;
		}
		return result;
	}

	bool IsPointInsideWaterVolume(
		const SceneDocument& document,
		const SceneEntity& entity,
		const SceneComponent& waterVolume,
		const Vector3& point
	) {
		const Transform transform = ResolveScene3DTransform(document, entity);
		const Vector3 center = {
			transform.translate.x + waterVolume.waterOffset.x,
			transform.translate.y + waterVolume.waterOffset.y,
			transform.translate.z + waterVolume.waterOffset.z
		};
		const Vector3 halfSize = {
			(std::max)(waterVolume.waterHalfSize.x, 0.001f),
			(std::max)(waterVolume.waterHalfSize.y, 0.001f),
			(std::max)(waterVolume.waterHalfSize.z, 0.001f)
		};

		return
			std::abs(point.x - center.x) <= halfSize.x &&
			std::abs(point.y - center.y) <= halfSize.y &&
			std::abs(point.z - center.z) <= halfSize.z;
	}

	bool TryBuildWaterParticleDrawFilter(
		const SceneDocument& document,
		Camera* camera,
		ParticleManager::WaterDrawFilter& filter
	) {
		if (
			!camera ||
			!document.GetPostProcessSettings().waterRefractionEnabled
		) {
			return false;
		}

		const Vector3 cameraPosition = camera->GetTranslate();
		for (const SceneEntity& entity : document.GetEntities()) {
			if (!IsEntityActiveInHierarchy(document, entity)) {
				continue;
			}

			const SceneComponent* waterVolume =
				FindEnabledComponent(entity, "WaterVolume");
			if (!waterVolume) {
				continue;
			}

			const Transform transform = ResolveScene3DTransform(document, entity);
			const Vector3 center{
				transform.translate.x + waterVolume->waterOffset.x,
				transform.translate.y + waterVolume->waterOffset.y,
				transform.translate.z + waterVolume->waterOffset.z
			};
			const Vector3 halfSize{
				(std::max)(waterVolume->waterHalfSize.x, 0.001f),
				(std::max)(waterVolume->waterHalfSize.y, 0.001f),
				(std::max)(waterVolume->waterHalfSize.z, 0.001f)
			};
			if (
				std::abs(cameraPosition.x - center.x) <= halfSize.x &&
				std::abs(cameraPosition.y - center.y) <= halfSize.y &&
				std::abs(cameraPosition.z - center.z) <= halfSize.z
			) {
				return false;
			}

			filter.cameraPosition = cameraPosition;
			filter.waterCenter = center;
			filter.waterHalfSize = halfSize;
			return true;
		}

		return false;
	}

	struct AgentBounds {
		bool valid = false;
		Vector3 center{};
		Vector3 halfSize{};
	};

	struct AgentAttractorTarget {
		bool valid = false;
		Vector3 position{};
		float radius = 0.0f;
		float strength = 0.0f;
	};

	float Hash01(uint64_t id, uint32_t salt) {
		uint64_t value = id + 0x9E3779B97F4A7C15ull + salt;
		value = (value ^ (value >> 30)) * 0xBF58476D1CE4E5B9ull;
		value = (value ^ (value >> 27)) * 0x94D049BB133111EBull;
		value = value ^ (value >> 31);
		return static_cast<float>(value & 0x00FFFFFFu) /
			static_cast<float>(0x00FFFFFFu);
	}

	uint64_t BuildFlockRuntimeSeed(
		uint64_t stableId,
		int configuredSeed,
		bool randomizeOnPlay
	) {
		uint64_t seed = stableId ^
			(static_cast<uint64_t>((std::max)(configuredSeed, 0)) << 32);
		if (!randomizeOnPlay) {
			return seed;
		}
		static uint64_t sequence = 0;
		const uint64_t ticks = static_cast<uint64_t>(
			std::chrono::high_resolution_clock::now().time_since_epoch().count()
		);
		return seed ^ ticks ^ (++sequence * 0x9E3779B97F4A7C15ull);
	}

	Vector3 AddScaled(Vector3 base, const Vector3& value, float scale) {
		base.x += value.x * scale;
		base.y += value.y * scale;
		base.z += value.z * scale;
		return base;
	}

	Vector3 LerpVector(const Vector3& a, const Vector3& b, float t) {
		return {
			a.x + (b.x - a.x) * t,
			a.y + (b.y - a.y) * t,
			a.z + (b.z - a.z) * t
		};
	}

	Vector3 SafeNormalize(const Vector3& value, const Vector3& fallback) {
		return Math::Length(value) > 0.000001f
			? Math::Normalize(value)
			: fallback;
	}

	Vector3 BuildFlockWanderDirection(
		const Vector3& currentHeading,
		uint64_t seedId,
		uint32_t step,
		float directionRange,
		float verticalRange
	) {
		const Vector3 heading = SafeNormalize(
			currentHeading,
			{ 0.0f, 0.0f, 1.0f }
		);
		const float yaw = std::atan2(heading.x, heading.z);
		const float yawOffset =
			(Hash01(seedId, 307u + step * 3u) * 2.0f - 1.0f) *
			directionRange;
		const float vertical =
			(Hash01(seedId, 311u + step * 3u) * 2.0f - 1.0f) *
			verticalRange;
		return SafeNormalize(
			{
				std::sin(yaw + yawOffset),
				vertical,
				std::cos(yaw + yawOffset)
			},
			heading
		);
	}

	Vector3 BuildFlockMemberJitterTarget(
		uint64_t entityId,
		uint64_t teamSeed,
		uint32_t step,
		float strength
	) {
		const uint64_t seed = entityId ^ teamSeed;
		const uint32_t salt = 601u + step * 5u;
		return {
			(Hash01(seed, salt) * 2.0f - 1.0f) * strength,
			(Hash01(seed, salt + 1u) * 2.0f - 1.0f) * strength * 0.45f,
			(Hash01(seed, salt + 2u) * 2.0f - 1.0f) * strength
		};
	}

	float FollowAmount(float followSpeed, float deltaTime) {
		if (followSpeed <= 0.0f || deltaTime <= 0.0f) {
			return 0.0f;
		}
		return std::clamp(
			1.0f - std::exp(-followSpeed * deltaTime),
			0.0f,
			1.0f
		);
	}

	Vector3 BlendDirections(
		const Vector3& from,
		const Vector3& to,
		float amount,
		const Vector3& fallback
	) {
		const float blend = std::clamp(amount, 0.0f, 1.0f);
		const Vector3 fromDirection = SafeNormalize(from, fallback);
		const Vector3 toDirection = SafeNormalize(to, fromDirection);
		return SafeNormalize(
			LerpVector(fromDirection, toDirection, blend),
			blend < 0.5f ? fromDirection : toDirection
		);
	}

	Vector3 ClampVectorLength(const Vector3& value, float maximumLength) {
		const float length = Math::Length(value);
		if (maximumLength <= 0.0f || length <= maximumLength) {
			return maximumLength <= 0.0f ? Vector3{} : value;
		}
		return Math::Multiply(value, maximumLength / length);
	}

	Vector3 MoveVectorToward(
		const Vector3& current,
		const Vector3& target,
		float maximumDelta
	) {
		if (maximumDelta <= 0.0f) {
			return current;
		}
		const Vector3 delta = Math::Subtract(target, current);
		return Math::Add(current, ClampVectorLength(delta, maximumDelta));
	}

	Vector3 RotateDirectionToward(
		const Vector3& current,
		const Vector3& target,
		float maximumRadians,
		const Vector3& fallback
	) {
		const Vector3 currentDirection = SafeNormalize(current, fallback);
		const Vector3 targetDirection = SafeNormalize(target, currentDirection);
		if (maximumRadians <= 0.0f) {
			return currentDirection;
		}
		const float dot = std::clamp(
			currentDirection.x * targetDirection.x +
				currentDirection.y * targetDirection.y +
				currentDirection.z * targetDirection.z,
			-1.0f,
			1.0f
		);
		const float angle = std::acos(dot);
		if (angle <= maximumRadians || angle <= 0.0001f) {
			return targetDirection;
		}
		return BlendDirections(
			currentDirection,
			targetDirection,
			maximumRadians / angle,
			currentDirection
		);
	}

	void ApplyForwardAxisOffset(Vector3& rotate, const std::string& axis) {
		constexpr float halfPi = 1.57079632679f;
		constexpr float pi = 3.14159265359f;
		if (axis == "-Z") {
			rotate.y += pi;
		} else if (axis == "+X") {
			rotate.y -= halfPi;
		} else if (axis == "-X") {
			rotate.y += halfPi;
		} else if (axis == "+Y") {
			rotate.x += halfPi;
		} else if (axis == "-Y") {
			rotate.x -= halfPi;
		}
	}

	Vector3 ResolveForwardAxisVector(const std::string& axis) {
		if (axis == "-Z") {
			return { 0.0f, 0.0f, -1.0f };
		}
		if (axis == "+X") {
			return { 1.0f, 0.0f, 0.0f };
		}
		if (axis == "-X") {
			return { -1.0f, 0.0f, 0.0f };
		}
		if (axis == "+Y") {
			return { 0.0f, 1.0f, 0.0f };
		}
		if (axis == "-Y") {
			return { 0.0f, -1.0f, 0.0f };
		}
		return { 0.0f, 0.0f, 1.0f };
	}

	Vector3 RotateDirection(const Vector3& direction, const Vector3& rotate) {
		const Matrix4x4 matrix = MakeAffineMatrix(
			{ 1.0f, 1.0f, 1.0f },
			rotate,
			{ 0.0f, 0.0f, 0.0f }
		);
		return {
			direction.x * matrix.m[0][0] +
				direction.y * matrix.m[1][0] +
				direction.z * matrix.m[2][0],
			direction.x * matrix.m[0][1] +
				direction.y * matrix.m[1][1] +
				direction.z * matrix.m[2][1],
			direction.x * matrix.m[0][2] +
				direction.y * matrix.m[1][2] +
				direction.z * matrix.m[2][2]
		};
	}

	Vector3 ForwardDirectionFromRotation(
		const Vector3& rotate,
		const std::string& forwardAxis,
		const Vector3& fallback
	) {
		return SafeNormalize(
			RotateDirection(ResolveForwardAxisVector(forwardAxis), rotate),
			fallback
		);
	}

	Vector3 BuildAgentVelocityRotation(
		const SceneComponent& behavior,
		const Vector3& currentRotate,
		const Vector3& velocity,
		const Vector3& desiredDirection,
		float deltaTime,
		float rotationFollowSpeed
	) {
		Vector3 target = currentRotate;
		if (!behavior.agentAlignForwardToVelocity) {
			return target;
		}

		if (Math::Length(velocity) <= 0.0001f) {
			return target;
		}
		const float horizontalLength = std::sqrt(
			velocity.x * velocity.x +
			velocity.z * velocity.z
		);

		Vector3 velocityRotate = currentRotate;
		if (horizontalLength > 0.0001f) {
			velocityRotate.y = std::atan2(velocity.x, velocity.z);
		}
		velocityRotate.x = std::clamp(
			-std::atan2(velocity.y, horizontalLength) *
				behavior.agentPitchFromVerticalVelocity,
			-1.45f,
			1.45f
		);
		const Vector3 velocityDirection =
			SafeNormalize(velocity, { 0.0f, 0.0f, 1.0f });
		const float turnSign =
			velocityDirection.x * desiredDirection.z -
			velocityDirection.z * desiredDirection.x;
		velocityRotate.z = std::clamp(
			-turnSign * behavior.agentBankingStrength,
			-1.35f,
			1.35f
		);
		ApplyForwardAxisOffset(velocityRotate, behavior.agentForwardAxis);

		const float rotationLerp = std::clamp(
			FollowAmount(rotationFollowSpeed, deltaTime),
			0.0f,
			1.0f
		);
		if (behavior.agentRotateAxisX) {
			target.x = Math::NormalizeAngle(
				Math::LerpAngle(
					currentRotate.x,
					velocityRotate.x,
					rotationLerp
				)
			);
		}
		if (behavior.agentRotateAxisY) {
			target.y = Math::NormalizeAngle(
				Math::LerpAngle(
					currentRotate.y,
					velocityRotate.y,
					rotationLerp
				)
			);
		}
		if (behavior.agentRotateAxisZ) {
			target.z = Math::NormalizeAngle(
				Math::LerpAngle(
					currentRotate.z,
					velocityRotate.z,
					rotationLerp
				)
			);
		}
		return target;
	}

	Vector3 BuildAgentVelocityRotation(
		const SceneComponent& behavior,
		const Vector3& currentRotate,
		const Vector3& velocity,
		const Vector3& desiredDirection,
		float deltaTime
	) {
		return BuildAgentVelocityRotation(
			behavior,
			currentRotate,
			velocity,
			desiredDirection,
			deltaTime,
			behavior.agentRotationFollowSpeed
		);
	}

	std::string ResolveAgentTeamRuntimeKey(
		const SceneDocument& document,
		const SceneEntity& entity,
		const SceneComponent& behavior
	) {
		const SceneTeamSettings* team = document.ResolveEntityTeam(entity);
		if (team && !team->name.empty()) {
			return "team:" + team->name;
		}
		if (!behavior.agentGroupName.empty()) {
			return "group:" + behavior.agentGroupName;
		}
		return {};
	}

	bool TryResolveWaterBounds(
		const SceneDocument& document,
		const SceneEntity& entity,
		const SceneComponent& waterVolume,
		AgentBounds& bounds
	) {
		const Transform transform = ResolveScene3DTransform(document, entity);
		bounds.center = {
			transform.translate.x + waterVolume.waterOffset.x,
			transform.translate.y + waterVolume.waterOffset.y,
			transform.translate.z + waterVolume.waterOffset.z
		};
		bounds.halfSize = {
			(std::max)(waterVolume.waterHalfSize.x, 0.1f),
			(std::max)(waterVolume.waterHalfSize.y, 0.1f),
			(std::max)(waterVolume.waterHalfSize.z, 0.1f)
		};
		bounds.valid = true;
		return true;
	}

	bool TryResolveAgentBounds(
		const SceneDocument& document,
		const SceneComponent& behavior,
		const Vector3& position,
		AgentBounds& bounds
	) {
		const SceneEntity* boundsEntity = nullptr;
		if (behavior.agentBoundsEntityId != 0) {
			boundsEntity = document.FindEntity(behavior.agentBoundsEntityId);
		}
		if (!boundsEntity && !behavior.agentBoundsName.empty()) {
			boundsEntity = document.FindEntityByName(behavior.agentBoundsName);
		}
		if (boundsEntity && IsEntityActiveInHierarchy(document, *boundsEntity)) {
			if (const SceneComponent* waterVolume =
				FindEnabledComponent(*boundsEntity, "WaterVolume")) {
				return TryResolveWaterBounds(
					document,
					*boundsEntity,
					*waterVolume,
					bounds
				);
			}
			const Transform transform =
				ResolveScene3DTransform(document, *boundsEntity);
			bounds.center = transform.translate;
			bounds.halfSize = {
				(std::max)(std::abs(transform.scale.x), 0.1f),
				(std::max)(std::abs(transform.scale.y), 0.1f),
				(std::max)(std::abs(transform.scale.z), 0.1f)
			};
			bounds.valid = true;
			return true;
		}
		if (!behavior.agentUseWaterBounds) {
			return false;
		}

		const SceneEntity* fallback = nullptr;
		const SceneComponent* fallbackWater = nullptr;
		for (const SceneEntity& entity : document.GetEntities()) {
			if (!IsEntityActiveInHierarchy(document, entity)) {
				continue;
			}
			const SceneComponent* waterVolume =
				FindEnabledComponent(entity, "WaterVolume");
			if (!waterVolume) {
				continue;
			}
			if (IsPointInsideWaterVolume(
				document,
				entity,
				*waterVolume,
				position
			)) {
				return TryResolveWaterBounds(
					document,
					entity,
					*waterVolume,
					bounds
				);
			}
			if (!fallback) {
				fallback = &entity;
				fallbackWater = waterVolume;
			}
		}
		return fallback && fallbackWater
			? TryResolveWaterBounds(document, *fallback, *fallbackWater, bounds)
			: false;
	}

	bool AttractorMatchesAgent(
		const SceneComponent& attractor,
		const SceneComponent& behavior
	) {
		if (
			!attractor.attractorTargetBehaviorName.empty() &&
			attractor.attractorTargetBehaviorName != behavior.agentBehaviorName
		) {
			return false;
		}
		if (
			!attractor.attractorTargetProfileName.empty() &&
			attractor.attractorTargetProfileName != behavior.agentProfileName
		) {
			return false;
		}
		return true;
	}

	bool TryResolveAgentAttractor(
		const SceneDocument& document,
		const SceneComponent& behavior,
		AgentAttractorTarget& target
	) {
		const SceneEntity* attractorEntity = nullptr;
		const SceneComponent* attractor = nullptr;
		if (behavior.agentAttractorEntityId != 0) {
			attractorEntity = document.FindEntity(behavior.agentAttractorEntityId);
			attractor = attractorEntity
				? FindEnabledComponent(*attractorEntity, "AgentAttractor")
				: nullptr;
		}
		if (!attractorEntity && !behavior.agentAttractorTag.empty()) {
			for (const SceneEntity& entity : document.GetEntities()) {
				if (!IsEntityActiveInHierarchy(document, entity)) {
					continue;
				}
				const SceneComponent* candidate =
					FindEnabledComponent(entity, "AgentAttractor");
				if (
					!candidate ||
					candidate->attractorTag != behavior.agentAttractorTag ||
					!AttractorMatchesAgent(*candidate, behavior)
				) {
					continue;
				}
				attractorEntity = &entity;
				attractor = candidate;
				break;
			}
		}
		if (
			!attractorEntity ||
			!attractor ||
			!IsEntityActiveInHierarchy(document, *attractorEntity)
		) {
			return false;
		}
		const Transform transform =
			ResolveScene3DTransform(document, *attractorEntity);
		target.valid = true;
		target.position = transform.translate;
		target.radius = (std::max)(attractor->attractorRadius, 0.0f);
		target.strength = (std::max)(attractor->attractorStrength, 0.0f);
		return true;
	}

	Vector3 ComputeBoundsSteering(
		const Vector3& position,
		const AgentBounds& bounds
	) {
		if (!bounds.valid) {
			return {};
		}
		Vector3 steer{};
		const Vector3 min = {
			bounds.center.x - bounds.halfSize.x,
			bounds.center.y - bounds.halfSize.y,
			bounds.center.z - bounds.halfSize.z
		};
		const Vector3 max = {
			bounds.center.x + bounds.halfSize.x,
			bounds.center.y + bounds.halfSize.y,
			bounds.center.z + bounds.halfSize.z
		};
		const Vector3 margin = {
			(std::max)(bounds.halfSize.x * 0.18f, 1.0f),
			(std::max)(bounds.halfSize.y * 0.25f, 0.8f),
			(std::max)(bounds.halfSize.z * 0.18f, 1.0f)
		};
		if (position.x < min.x + margin.x) {
			steer.x += (min.x + margin.x - position.x) / margin.x;
		} else if (position.x > max.x - margin.x) {
			steer.x -= (position.x - (max.x - margin.x)) / margin.x;
		}
		if (position.y < min.y + margin.y) {
			steer.y += (min.y + margin.y - position.y) / margin.y;
		} else if (position.y > max.y - margin.y) {
			steer.y -= (position.y - (max.y - margin.y)) / margin.y;
		}
		if (position.z < min.z + margin.z) {
			steer.z += (min.z + margin.z - position.z) / margin.z;
		} else if (position.z > max.z - margin.z) {
			steer.z -= (position.z - (max.z - margin.z)) / margin.z;
		}
		return steer;
	}

	Vector3 ClampToBounds(const Vector3& position, const AgentBounds& bounds) {
		if (!bounds.valid) {
			return position;
		}
		return {
			std::clamp(
				position.x,
				bounds.center.x - bounds.halfSize.x,
				bounds.center.x + bounds.halfSize.x
			),
			std::clamp(
				position.y,
				bounds.center.y - bounds.halfSize.y,
				bounds.center.y + bounds.halfSize.y
			),
			std::clamp(
				position.z,
				bounds.center.z - bounds.halfSize.z,
				bounds.center.z + bounds.halfSize.z
			)
		};
	}

	Vector3 TransformCoord(const Vector3& value, const Matrix4x4& matrix) {
		const float x =
			value.x * matrix.m[0][0] +
			value.y * matrix.m[1][0] +
			value.z * matrix.m[2][0] +
			matrix.m[3][0];
		const float y =
			value.x * matrix.m[0][1] +
			value.y * matrix.m[1][1] +
			value.z * matrix.m[2][1] +
			matrix.m[3][1];
		const float z =
			value.x * matrix.m[0][2] +
			value.y * matrix.m[1][2] +
			value.z * matrix.m[2][2] +
			matrix.m[3][2];
		const float w =
			value.x * matrix.m[0][3] +
			value.y * matrix.m[1][3] +
			value.z * matrix.m[2][3] +
			matrix.m[3][3];
		const float inverseW = std::abs(w) > 0.000001f ? 1.0f / w : 1.0f;
		return { x * inverseW, y * inverseW, z * inverseW };
	}

	void AddCameraDebugDraw(
		const SceneDocument& document,
		const SceneEntity& entity,
		const SceneComponent& cameraComponent,
		float aspectRatio,
		const Transform* overrideTransform = nullptr,
		const Camera* overrideCamera = nullptr
	) {
#if defined(_DEBUG) || defined(DEVELOPMENT)
		if (!IsEntityActiveInHierarchy(document, entity)) {
			return;
		}

		Matrix4x4 cameraWorld{};
		Matrix4x4 cameraViewProjection{};
		Vector3 origin{};
		if (overrideCamera) {
			cameraWorld = overrideCamera->GetWorldMatrix();
			cameraViewProjection = overrideCamera->GetViewProjectionMatrix();
			origin = overrideCamera->GetTranslate();
		} else {
			const Transform& cameraTransform = overrideTransform
				? *overrideTransform
				: entity.transform;
			Camera debugCamera;
			debugCamera.SetOrbitMode(false);
			debugCamera.SetTranslate(cameraTransform.translate);
			debugCamera.SetRotate(cameraTransform.rotate);
			debugCamera.SetFovY(std::clamp(
				cameraComponent.cameraFovY,
				0.0174532925f,
				3.12413936f
			));
			debugCamera.SetAspectRatio((std::max)(aspectRatio, 0.001f));
			debugCamera.SetNearClip((std::max)(
				cameraComponent.cameraNearClip,
				0.001f
			));
			debugCamera.SetFarClip((std::max)(
				(std::min)(cameraComponent.cameraFarClip, 20.0f),
				cameraComponent.cameraNearClip + 0.001f
			));
			debugCamera.Update();
			cameraWorld = debugCamera.GetWorldMatrix();
			cameraViewProjection = debugCamera.GetViewProjectionMatrix();
			origin = debugCamera.GetTranslate();
		}

		const Vector3 right = Math::Normalize({
			cameraWorld.m[0][0],
			cameraWorld.m[0][1],
			cameraWorld.m[0][2]
		});
		const Vector3 up = Math::Normalize({
			cameraWorld.m[1][0],
			cameraWorld.m[1][1],
			cameraWorld.m[1][2]
		});
		const Vector3 forward = Math::Normalize({
			cameraWorld.m[2][0],
			cameraWorld.m[2][1],
			cameraWorld.m[2][2]
		});

		DebugRenderer* debugRenderer = DebugRenderer::GetInstance();
		debugRenderer->AddSphere(
			origin,
			cameraComponent.cameraIsMain ? 0.16f : 0.11f,
			cameraComponent.cameraIsMain
				? Vector4{ 1.0f, 0.85f, 0.15f, 1.0f }
				: Vector4{ 0.15f, 0.85f, 1.0f, 1.0f },
			8
		);
		debugRenderer->AddLine(
			origin,
			Math::Add(origin, Math::Multiply(forward, 1.2f)),
			{ 0.25f, 0.55f, 1.0f, 1.0f }
		);
		debugRenderer->AddLine(
			origin,
			Math::Add(origin, Math::Multiply(up, 0.7f)),
			{ 0.2f, 1.0f, 0.35f, 1.0f }
		);
		debugRenderer->AddLine(
			origin,
			Math::Add(origin, Math::Multiply(right, 0.7f)),
			{ 1.0f, 0.25f, 0.25f, 1.0f }
		);

		const Matrix4x4 inverseViewProjection = Inverse(
			cameraViewProjection
		);
		Vector3 corners[8]{};
		uint32_t index = 0;
		for (float z : { 0.0f, 1.0f }) {
			for (float y : { -1.0f, 1.0f }) {
				for (float x : { -1.0f, 1.0f }) {
					corners[index++] = TransformCoord(
						{ x, y, z },
						inverseViewProjection
					);
				}
			}
		}
		const Vector4 frustumColor =
			cameraComponent.cameraIsMain
				? Vector4{ 1.0f, 0.8f, 0.1f, 1.0f }
				: Vector4{ 0.2f, 0.85f, 1.0f, 1.0f };
		const uint32_t edges[][2] = {
			{ 0, 1 }, { 1, 3 }, { 3, 2 }, { 2, 0 },
			{ 4, 5 }, { 5, 7 }, { 7, 6 }, { 6, 4 },
			{ 0, 4 }, { 1, 5 }, { 2, 6 }, { 3, 7 }
		};
		for (const auto& edge : edges) {
			debugRenderer->AddLine(
				corners[edge[0]],
				corners[edge[1]],
				frustumColor
			);
		}
#else
		(void)document;
		(void)entity;
		(void)cameraComponent;
		(void)aspectRatio;
		(void)overrideTransform;
		(void)overrideCamera;
#endif
	}

	void AddThirdPersonCameraDebugDraw(
		const SceneDocument& document,
		const SceneEntity& entity,
		const SceneComponent& thirdPersonCamera,
		const Vector3& cameraPosition
	) {
#if defined(_DEBUG) || defined(DEVELOPMENT)
		if (!IsEntityActiveInHierarchy(document, entity)) {
			return;
		}

		const Vector3 focus = Math::Add(
			entity.transform.translate,
			thirdPersonCamera.thirdPersonTargetOffset
		);
		DebugRenderer* debugRenderer = DebugRenderer::GetInstance();
		debugRenderer->AddSphere(
			focus,
			0.12f,
			{ 0.95f, 0.35f, 1.0f, 1.0f },
			8
		);
		debugRenderer->AddLine(
			focus,
			cameraPosition,
			{ 0.95f, 0.35f, 1.0f, 1.0f }
		);
#else
		(void)document;
		(void)entity;
		(void)thirdPersonCamera;
		(void)cameraPosition;
#endif
	}
}

void GamePlayScene::SyncSceneModelObjects() {
	SceneDocument* document = sceneManager_
		? sceneManager_->GetActiveSceneDocument()
		: nullptr;
	if (!document) {
		ClearSceneModelObjects();
		return;
	}

	std::unordered_set<uint64_t> requiredIds;
	for (const SceneEntity& entity : document->GetEntities()) {
		const SceneComponent* meshRenderer =
			FindEnabledComponent(entity, "MeshRenderer");
		const std::string modelPath = meshRenderer
			? meshRenderer->modelPath
			: std::string{};
		const bool hasRenderer = !modelPath.empty();
		requiredIds.insert(entity.id);
		auto found = sceneModelObjects_.find(entity.id);
		if (
			found != sceneModelObjects_.end() &&
			(
				found->second.modelPath != modelPath ||
				found->second.hasRenderer != hasRenderer
			)
		) {
			delete found->second.object;
			sceneModelObjects_.erase(found);
			found = sceneModelObjects_.end();
		}

		if (found == sceneModelObjects_.end()) {
			SceneModelObject sceneObject{};
			sceneObject.object = new Object3d();
			sceneObject.object->Initialize(Object3dCommon::GetInstance());
			if (hasRenderer) {
				ModelManager::GetInstance()->LoadModel(modelPath);
				sceneObject.object->SetModel(modelPath);
			}
			if (HasComponent(entity, "Animator")) {
				sceneObject.object->SetAnimationLoop(true);
				sceneObject.object->SetAnimationSpeed(1.0f);
			}
			if (!environmentMapPath_.empty()) {
				sceneObject.object->SetEnvironmentMap(
					environmentMapPath_,
					ResolveEnvironmentReflectionIntensity(
						meshRenderer,
						environmentReflectionIntensity_
					)
				);
			}
			sceneObject.modelPath = modelPath;
			sceneObject.hasRenderer = hasRenderer;
			found = sceneModelObjects_.emplace(
				entity.id,
				std::move(sceneObject)
			).first;
		}

		found->second.object->GetTransform() = entity.transform;
		const bool isWaterVolume = HasComponent(entity, "WaterVolume");
		found->second.object->SetCullMode(
			isWaterVolume
				? Object3dCommon::CullMode::kNone
				: (
					meshRenderer
						? ToObjectCullMode(meshRenderer->meshCullMode)
						: Object3dCommon::CullMode::kBack
				)
		);
		if (isWaterVolume) {
			found->second.object->SetColor({ 0.08f, 0.48f, 0.95f, 0.34f });
			found->second.object->SetEnableLighting(false);
			found->second.object->SetEnvironmentCoefficient(0.0f);
			found->second.object->SetEmissive(
				0.18f,
				{ 0.30f, 0.78f, 1.0f, 1.0f }
			);
		} else if (const SceneComponent* agentBehavior =
			FindEnabledComponent(entity, "AgentBehavior")) {
			const SceneComponent resolvedAgentBehavior =
				ResolveAgentBehaviorSettings(
					*document,
					entity,
					*agentBehavior
				);
			found->second.object->SetColor(
				resolvedAgentBehavior.agentVisualColor
			);
			found->second.object->SetEnableLighting(
				resolvedAgentBehavior.agentEnableLighting
			);
			found->second.object->SetEnvironmentCoefficient(0.05f);
			found->second.object->SetEmissive(
				resolvedAgentBehavior.agentEnableLighting ? 0.0f : 0.12f,
				resolvedAgentBehavior.agentVisualColor
			);
		} else if (const SceneComponent* agentAttractor =
			FindEnabledComponent(entity, "AgentAttractor")) {
			found->second.object->SetColor(agentAttractor->attractorVisualColor);
			found->second.object->SetEnableLighting(true);
			found->second.object->SetEnvironmentCoefficient(0.08f);
			found->second.object->SetEmissive(
				0.05f,
				agentAttractor->attractorVisualColor
			);
		} else if (HasComponent(entity, "MonitorRenderer")) {
			found->second.object->SetColor({ 1.0f, 1.0f, 1.0f, 1.0f });
			found->second.object->SetEnableLighting(false);
			found->second.object->SetEnvironmentCoefficient(0.0f);
			found->second.object->SetEmissive(0.0f);
		} else {
			found->second.object->SetColor({ 1.0f, 1.0f, 1.0f, 1.0f });
			found->second.object->SetEnableLighting(true);
			found->second.object->SetEmissive(0.0f);
		}
		if (HasComponent(entity, "PlayerBehavior")) {
			found->second.object->SetDissolve(0.0f);
		}
		const SceneComponent* obbCollider =
			FindEnabledComponent(entity, "OBBCollider");
		const bool hasObbCollider = obbCollider != nullptr;
		found->second.hasCollider = hasObbCollider;
		if (hasObbCollider) {
			Collider* runtimeCollider = obbCollider->colliderShape == "Sphere"
				? static_cast<Collider*>(&found->second.sphereCollider)
				: static_cast<Collider*>(&found->second.boxCollider);
			runtimeCollider->SetWorldTransform(
				&found->second.object->GetTransform()
			);
			runtimeCollider->SetOffset(obbCollider->colliderOffset);
			if (obbCollider->colliderShape == "Sphere") {
				found->second.sphereCollider.SetRadius(
					(std::max)(obbCollider->colliderSphereRadius, 0.001f)
				);
			} else {
				found->second.boxCollider.SetHalfSize({
					(std::max)(
						entity.transform.scale.x * obbCollider->colliderSizeMultiplier.x,
						0.001f
					),
					(std::max)(
						entity.transform.scale.y * obbCollider->colliderSizeMultiplier.y,
						0.001f
					),
					(std::max)(
						entity.transform.scale.z * obbCollider->colliderSizeMultiplier.z,
						0.001f
					)
				});
			}
			found->second.collider = runtimeCollider;
			found->second.colliderDebugColor = obbCollider->colliderDebugColor;
			found->second.colliderDebugVisible =
				obbCollider->colliderDebugVisible;
			found->second.colliderDebugDrawMode =
				obbCollider->colliderDebugDrawMode;
			found->second.colliderDebugSegments =
				static_cast<uint32_t>(obbCollider->colliderDebugSegments);
		} else {
			found->second.collider = nullptr;
		}

		const SceneComponent* physicsBody =
			FindEnabledComponent(entity, "PhysicsBody");
		const bool wasPhysicsBody = found->second.hasPhysicsBody;
		found->second.hasPhysicsBody = physicsBody != nullptr;
		if (physicsBody) {
			const Vector3 previousVelocity = found->second.physicsBody.velocity;
			found->second.physicsBody.type =
				ToPhysicsBodyType(physicsBody->physicsBodyType);
			found->second.physicsBody.transform =
				&found->second.object->GetTransform();
			found->second.physicsBody.collider = found->second.collider;
			found->second.physicsBody.mass =
				(std::max)(physicsBody->physicsMass, 0.001f);
			found->second.physicsBody.useGravity =
				physicsBody->physicsUseGravity;
			found->second.physicsBody.gravityScale =
				physicsBody->physicsGravityScale;
			found->second.physicsBody.drag =
				(std::max)(physicsBody->physicsDrag, 0.0f);
			found->second.physicsBody.restitution =
				std::clamp(physicsBody->physicsRestitution, 0.0f, 1.0f);
			found->second.physicsBody.friction =
				std::clamp(physicsBody->physicsFriction, 0.0f, 1.0f);
			found->second.physicsBody.maxFallSpeed =
				(std::max)(physicsBody->physicsMaxFallSpeed, 0.0f);
			const EditorSession* editorSession = sceneManager_
				? sceneManager_->GetEditorSession()
				: nullptr;
			found->second.physicsBody.velocity =
				(!wasPhysicsBody || (editorSession && editorSession->IsEditing()))
				? physicsBody->physicsVelocity
				: previousVelocity;
			found->second.physicsBody.freezePositionX =
				physicsBody->physicsFreezePositionX;
			found->second.physicsBody.freezePositionY =
				physicsBody->physicsFreezePositionY;
			found->second.physicsBody.freezePositionZ =
				physicsBody->physicsFreezePositionZ;
		}
	}

	for (auto iterator = sceneModelObjects_.begin(); iterator != sceneModelObjects_.end();) {
		if (!requiredIds.contains(iterator->first)) {
			delete iterator->second.object;
			iterator = sceneModelObjects_.erase(iterator);
		} else {
			++iterator;
		}
	}

	auto resolveObject = [this, document](uint64_t entityId) -> Object3d* {
		if (entityId == 0) {
			return nullptr;
		}
		const auto generic = sceneModelObjects_.find(entityId);
		if (generic != sceneModelObjects_.end()) {
			return generic->second.object;
		}
		const SceneEntity* entity = document->FindEntity(entityId);
		if (!entity) {
			return nullptr;
		}
		return nullptr;
	};

	for (const SceneEntity& entity : document->GetEntities()) {
		Object3d* object = resolveObject(entity.id);
		if (!object) {
			continue;
		}
		object->SetParent(resolveObject(entity.parentId));
	}

	std::unordered_set<uint64_t> updatedIds;
	std::unordered_set<uint64_t> updatingIds;
	std::function<void(uint64_t)> updateEntity;
	updateEntity = [&](uint64_t entityId) {
		if (updatedIds.contains(entityId) || updatingIds.contains(entityId)) {
			return;
		}
		const SceneEntity* entity = document->FindEntity(entityId);
		Object3d* object = resolveObject(entityId);
		if (!entity || !object) {
			return;
		}
		updatingIds.insert(entityId);
		if (resolveObject(entity->parentId)) {
			updateEntity(entity->parentId);
		}
		if (IsEntityActiveInHierarchy(*document, *entity)) {
			object->Update();
		}
		updatingIds.erase(entityId);
		updatedIds.insert(entityId);
	};
	for (const SceneEntity& entity : document->GetEntities()) {
		updateEntity(entity.id);
	}
}

void GamePlayScene::SyncSceneSpriteObjects() {
	SceneDocument* document = sceneManager_
		? sceneManager_->GetActiveSceneDocument()
		: nullptr;
	if (!document) {
		ClearSceneSpriteObjects();
		return;
	}

	std::unordered_set<uint64_t> requiredIds;
	for (const SceneEntity& entity : document->GetEntities()) {
		const SceneComponent* spriteRenderer =
			FindEnabledComponent(entity, "SpriteRenderer");
		if (!spriteRenderer || spriteRenderer->texturePath.empty()) {
			continue;
		}
		requiredIds.insert(entity.id);
		auto found = sceneSpriteObjects_.find(entity.id);
		if (
			found != sceneSpriteObjects_.end() &&
			found->second.texturePath != spriteRenderer->texturePath
		) {
			delete found->second.sprite;
			sceneSpriteObjects_.erase(found);
			found = sceneSpriteObjects_.end();
		}

		if (found == sceneSpriteObjects_.end()) {
			TextureManager::GetInstance()->LoadTexture(spriteRenderer->texturePath);
			SceneSpriteObject sceneSprite{};
			sceneSprite.sprite = new Sprite();
			sceneSprite.sprite->Initialize(
				SpriteCommon::GetInstance(),
				spriteRenderer->texturePath
			);
			sceneSprite.texturePath = spriteRenderer->texturePath;
			found = sceneSpriteObjects_.emplace(
				entity.id,
				std::move(sceneSprite)
			).first;
		}

		Sprite* sprite = found->second.sprite;
		const Transform spriteTransform = ResolveScene2DTransform(*document, entity);
		sprite->SetPosition({
			spriteTransform.translate.x,
			spriteTransform.translate.y
		});
		sprite->SetRotation(spriteTransform.rotate.z);
		sprite->SetSize({
			spriteRenderer->spriteSize.x * spriteTransform.scale.x,
			spriteRenderer->spriteSize.y * spriteTransform.scale.y
		});
		sprite->SetAnchorPoint(spriteRenderer->spriteAnchor);
		sprite->SetColor(spriteRenderer->spriteColor);
		sprite->SetIsFlipX(spriteRenderer->spriteFlipX);
		sprite->SetIsFlipY(spriteRenderer->spriteFlipY);
		if (IsEntityActiveInHierarchy(*document, entity)) {
			sprite->Update();
		}
	}

	for (auto iterator = sceneSpriteObjects_.begin(); iterator != sceneSpriteObjects_.end();) {
		if (!requiredIds.contains(iterator->first)) {
			delete iterator->second.sprite;
			iterator = sceneSpriteObjects_.erase(iterator);
		} else {
			++iterator;
		}
	}
}

void GamePlayScene::ClearSceneSpriteObjects() {
	for (auto& [entityId, sceneSprite] : sceneSpriteObjects_) {
		(void)entityId;
		delete sceneSprite.sprite;
		sceneSprite.sprite = nullptr;
	}
	sceneSpriteObjects_.clear();
}

void GamePlayScene::SyncEnvironmentComponent() {
	SceneDocument* document = sceneManager_
		? sceneManager_->GetActiveSceneDocument()
		: nullptr;

	const SceneComponent* environment = nullptr;
	if (document) {
		for (const SceneEntity& entity : document->GetEntities()) {
			if (!IsEntityActiveInHierarchy(*document, entity)) {
				continue;
			}
			environment = FindEnabledComponent(entity, "Environment");
			if (environment) {
				break;
			}
		}
	}

	bool skyboxEnabled = false;
	std::string requestedPath;
	float skyboxIntensity = 1.0f;
	float reflectionIntensity = 0.3f;
	if (environment) {
		skyboxEnabled = environment->environmentSkyboxEnabled;
		requestedPath = environment->environmentSkyboxPath.empty()
			? "resources/rostock_laage_airport_4k.dds"
			: environment->environmentSkyboxPath;
		skyboxIntensity =
			(std::max)(0.0f, environment->environmentSkyboxIntensity);
		reflectionIntensity = std::clamp(
			environment->environmentReflectionIntensity,
			0.0f,
			1.0f
		);
	}

	std::string texturePath;
	if (skyboxEnabled && !requestedPath.empty()) {
		std::filesystem::path requestedFilePath(requestedPath);
		texturePath = requestedFilePath.is_absolute()
			? EditableResourcePath::ToProjectRelative(requestedFilePath).generic_string()
			: requestedPath;
	}

	if (!skyboxEnabled || texturePath.empty()) {
		if (skybox_) {
			delete skybox_;
			skybox_ = nullptr;
		}
		environmentMapPath_.clear();
		environmentReflectionIntensity_ = 0.0f;
	} else if (texturePath != environmentMapPath_) {
		if (TextureManager::GetInstance()->LoadTexture(texturePath)) {
			if (skybox_) {
				delete skybox_;
			}
			skybox_ = new Skybox();
			skybox_->Initialize(Object3dCommon::GetInstance(), texturePath);
			skybox_->SetScale({ 100.0f, 100.0f, 100.0f });
			environmentMapPath_ = texturePath;
			environmentReflectionIntensity_ = reflectionIntensity;
		}
	} else {
		environmentReflectionIntensity_ = reflectionIntensity;
	}

	if (skybox_) {
		skybox_->SetColor({
			skyboxIntensity,
			skyboxIntensity,
			skyboxIntensity,
			1.0f
		});
	}

	for (auto& [entityId, sceneObject] : sceneModelObjects_) {
		if (!sceneObject.object) {
			continue;
		}
		const SceneEntity* entity = document
			? document->FindEntity(entityId)
			: nullptr;
		const SceneComponent* meshRenderer = entity
			? FindEnabledComponent(*entity, "MeshRenderer")
			: nullptr;
		const bool isMonitorSurface =
			entity && HasComponent(*entity, "MonitorRenderer");
		sceneObject.object->SetEnvironmentMap(
			environmentMapPath_,
			isMonitorSurface
				? 0.0f
				: ResolveEnvironmentReflectionIntensity(
					meshRenderer,
					environmentReflectionIntensity_
				)
		);
		if (isMonitorSurface) {
			sceneObject.object->SetEnableLighting(false);
		}
	}
}

void GamePlayScene::ClearSceneModelObjects() {
	for (auto& [id, sceneObject] : sceneModelObjects_) {
		(void)id;
		delete sceneObject.object;
		sceneObject.object = nullptr;
	}
	sceneModelObjects_.clear();
}

Object3d* GamePlayScene::FindSceneModelObjectByName(const char* name) const {
	if (!sceneManager_ || !name) {
		return nullptr;
	}
	SceneDocument* document = sceneManager_->GetActiveSceneDocument();
	const SceneEntity* entity = document
		? document->FindEntityByName(name)
		: nullptr;
	if (!entity) {
		return nullptr;
	}
	const auto found = sceneModelObjects_.find(entity->id);
	return found == sceneModelObjects_.end() ? nullptr : found->second.object;
}

void GamePlayScene::ApplyPlayerBehaviorComponent(
	const SceneDocument& document
) {
	if (!player_) {
		return;
	}

	const SceneEntity* playerEntity = document.FindEntityByName("Player");
	if (!playerEntity) {
		return;
	}

	const SceneComponent* playerBehavior =
		FindEnabledComponent(*playerEntity, "PlayerBehavior");
	if (!playerBehavior) {
		return;
	}

	player_->SetBehaviorSettings(
		playerBehavior->playerMoveSpeed,
		playerBehavior->playerJumpVelocity,
		playerBehavior->playerTurnResponsiveness,
		playerBehavior->playerDashMultiplier,
		playerBehavior->playerCameraRelativeMove,
		playerBehavior->playerAllowJump
	);
}

void GamePlayScene::ApplyPlayerPhysicsComponent(
	const SceneDocument& document
) {
	if (!player_ || !player_->GetObject()) {
		return;
	}

	const SceneEntity* playerEntity = document.FindEntityByName("Player");
	if (!playerEntity || !HasComponent(*playerEntity, "PlayerBehavior")) {
		return;
	}
	const auto playerRuntime = sceneModelObjects_.find(playerEntity->id);
	player_->SetCollider(
		playerRuntime != sceneModelObjects_.end()
			? playerRuntime->second.collider
			: nullptr
	);

	const SceneComponent* physicsBody =
		FindEnabledComponent(*playerEntity, "PhysicsBody");
	if (!physicsBody) {
		return;
	}

	PhysicsBody& body = player_->GetPhysicsBody();
	const Vector3 runtimeVelocity = body.velocity;
	body.type = ToPhysicsBodyType(physicsBody->physicsBodyType);
	if (body.type == PhysicsBodyType::Static) {
		body.type = PhysicsBodyType::Dynamic;
	}
	body.transform = &player_->GetObject()->GetTransform();
	body.mass = (std::max)(physicsBody->physicsMass, 0.001f);
	body.useGravity = physicsBody->physicsUseGravity;
	body.gravityScale = physicsBody->physicsGravityScale;
	body.drag = (std::max)(physicsBody->physicsDrag, 0.0f);
	body.restitution = std::clamp(
		physicsBody->physicsRestitution,
		0.0f,
		1.0f
	);
	body.friction = std::clamp(
		physicsBody->physicsFriction,
		0.0f,
		1.0f
	);
	body.maxFallSpeed = (std::max)(physicsBody->physicsMaxFallSpeed, 0.0f);
	body.freezePositionX = physicsBody->physicsFreezePositionX;
	body.freezePositionY = physicsBody->physicsFreezePositionY;
	body.freezePositionZ = physicsBody->physicsFreezePositionZ;

	const EditorSession* editorSession = sceneManager_
		? sceneManager_->GetEditorSession()
		: nullptr;
	body.velocity = (editorSession && editorSession->IsEditing())
		? physicsBody->physicsVelocity
		: runtimeVelocity;
}

void GamePlayScene::ApplyWaterVolumes(const SceneDocument& document) {
	if (!player_ || !player_->GetObject()) {
		return;
	}

	player_->SetWaterState(false, 1.0f, 0.0f);

	const Vector3 playerPosition =
		player_->GetObject()->GetTransform().translate;
	for (const SceneEntity& entity : document.GetEntities()) {
		if (!IsEntityActiveInHierarchy(document, entity)) {
			continue;
		}
		const SceneComponent* waterVolume =
			FindEnabledComponent(entity, "WaterVolume");
		if (!waterVolume) {
			continue;
		}
		if (!IsPointInsideWaterVolume(
			document,
			entity,
			*waterVolume,
			playerPosition
		)) {
			continue;
		}

		player_->SetWaterState(
			true,
			waterVolume->waterMoveSpeedMultiplier,
			waterVolume->waterSwimUpSpeed
		);

		PhysicsBody& body = player_->GetPhysicsBody();
		body.gravityScale = waterVolume->waterGravityScale;
		body.drag = (std::max)(body.drag, waterVolume->waterDrag);
		body.maxFallSpeed =
			(std::max)(waterVolume->waterMaxFallSpeed, 0.0f);
		return;
	}
}

void GamePlayScene::UpdateAgentBehaviors(
	SceneDocument& document,
	float deltaTime
) {
	struct AgentUpdateEntry {
		SceneEntity* entity = nullptr;
		SceneComponent behavior{};
		Object3d* object = nullptr;
		AgentRuntime* runtime = nullptr;
		Transform transform{};
		std::string teamKey;
	};

	const float dt = std::clamp(deltaTime, 0.0f, 0.1f);
	if (dt <= 0.0f) {
		return;
	}

	std::vector<AgentUpdateEntry> agents;
	std::unordered_set<uint64_t> requiredIds;
	for (SceneEntity& entity : document.GetEntities()) {
		if (!IsEntityActiveInHierarchy(document, entity)) {
			continue;
		}
		const SceneComponent* behavior =
			FindEnabledComponent(entity, "AgentBehavior");
		if (!behavior) {
			continue;
		}
		const auto objectIt = sceneModelObjects_.find(entity.id);
		if (
			objectIt == sceneModelObjects_.end() ||
			!objectIt->second.object
		) {
			continue;
		}
		const SceneComponent resolvedBehavior =
			ResolveAgentBehaviorSettings(document, entity, *behavior);

		AgentRuntime& runtime = agentRuntimes_[entity.id];
		if (!runtime.initialized) {
			const float yaw =
				entity.transform.rotate.y +
				Hash01(entity.id, 17u) * 6.28318530718f;
			const float speed =
				resolvedBehavior.agentMinSpeed +
				(resolvedBehavior.agentMaxSpeed -
					resolvedBehavior.agentMinSpeed) *
				Hash01(entity.id, 29u);
			runtime.velocity = {
				std::sin(yaw) * speed,
				(Hash01(entity.id, 43u) - 0.5f) * speed * 0.25f,
				std::cos(yaw) * speed
			};
			runtime.phase = Hash01(entity.id, 59u) * 6.28318530718f;
			runtime.initialized = true;
		}

		requiredIds.insert(entity.id);
		agents.push_back({
			&entity,
			resolvedBehavior,
			objectIt->second.object,
			&runtime,
			objectIt->second.object->GetTransform(),
			ResolveAgentTeamRuntimeKey(document, entity, resolvedBehavior)
		});
	}

	for (auto iterator = agentRuntimes_.begin();
		iterator != agentRuntimes_.end();) {
		if (!requiredIds.contains(iterator->first)) {
			iterator = agentRuntimes_.erase(iterator);
		} else {
			++iterator;
		}
	}

	struct TeamFrameState {
		Vector3 centerSum{};
		SceneComponent motionBehavior{};
		Vector3 leaderStartPosition{};
		uint64_t seedId = 0;
		uint32_t count = 0;
		bool useLeaderStartPosition = false;
		bool hasMotionBehavior = false;
	};

	std::unordered_map<std::string, TeamFrameState> teamFrames;
	std::unordered_set<std::string> requiredTeamKeys;
	for (const AgentUpdateEntry& agent : agents) {
		if (agent.teamKey.empty()) {
			continue;
		}

		TeamFrameState& frame = teamFrames[agent.teamKey];
		frame.centerSum = Math::Add(
			frame.centerSum,
			agent.transform.translate
		);
		if (!frame.hasMotionBehavior) {
			const SceneComponent* sourceBehavior =
				FindEnabledComponent(*agent.entity, "AgentBehavior");
			frame.motionBehavior = sourceBehavior
				? ResolveTeamLeaderSettings(
					document,
					*agent.entity,
					*sourceBehavior
				)
				: agent.behavior;
			if (const SceneTeamSettings* team =
				document.ResolveEntityTeam(*agent.entity)) {
				frame.useLeaderStartPosition =
					team->agentUseLeaderStartPosition;
				frame.leaderStartPosition = team->agentLeaderStartPosition;
			}
			frame.seedId = agent.entity->id;
			frame.hasMotionBehavior = true;
		}
		++frame.count;
		requiredTeamKeys.insert(agent.teamKey);
	}

	for (auto iterator = agentTeamRuntimes_.begin();
		iterator != agentTeamRuntimes_.end();) {
		if (!requiredTeamKeys.contains(iterator->first)) {
			iterator = agentTeamRuntimes_.erase(iterator);
		} else {
			++iterator;
		}
	}

	for (const auto& [teamKey, frame] : teamFrames) {
		if (frame.count == 0) {
			continue;
		}

		const float invCount = 1.0f / static_cast<float>(frame.count);
		const Vector3 center = Math::Multiply(frame.centerSum, invCount);
		TeamRuntime& runtime = agentTeamRuntimes_[teamKey];
		if (!runtime.initialized) {
			runtime.center = frame.useLeaderStartPosition
				? frame.leaderStartPosition
				: center;
			runtime.phase = Hash01(frame.seedId, 211u) * 6.28318530718f;
			runtime.seedId = BuildFlockRuntimeSeed(
				frame.seedId,
				frame.motionBehavior.agentRandomSeed,
				frame.motionBehavior.agentRandomizeSeedOnPlay
			);
			const Vector3 configuredHeading = SafeNormalize(
				frame.motionBehavior.agentTeamHeadingDirection,
				{ 0.0f, 0.0f, 1.0f }
			);
			runtime.heading = frame.motionBehavior.agentUseTeamHeading
				? configuredHeading
				: BuildFlockWanderDirection(
					{ 0.0f, 0.0f, 1.0f },
					runtime.seedId,
					runtime.wanderStep,
					3.14159265359f,
					frame.motionBehavior.agentWanderVerticalRange
				);
			++runtime.wanderStep;
			runtime.wanderDirection =
				frame.motionBehavior.agentWanderChangeInterval > 0.0f
					? BuildFlockWanderDirection(
						runtime.heading,
						runtime.seedId,
						runtime.wanderStep,
						frame.motionBehavior.agentWanderDirectionRange,
						frame.motionBehavior.agentWanderVerticalRange
					)
					: runtime.heading;
			runtime.wanderTimer =
				frame.motionBehavior.agentWanderChangeInterval *
				(0.75f + Hash01(runtime.seedId, 317u) * 0.5f);
			const float initialSpeed =
				(frame.motionBehavior.agentMinSpeed +
					frame.motionBehavior.agentMaxSpeed) * 0.5f;
			runtime.velocity = Math::Multiply(runtime.heading, initialSpeed);
			runtime.desiredDirection = runtime.heading;
			runtime.desiredSpeed = initialSpeed;
			runtime.decisionValid = false;
			runtime.initialized = true;
		}

		const SceneComponent& behavior = frame.motionBehavior;
		const Vector3 velocityDirection = SafeNormalize(
			runtime.velocity,
			runtime.heading
		);
		AgentBounds bounds{};
		TryResolveAgentBounds(document, behavior, runtime.center, bounds);
		runtime.decisionTimer -= dt;
		if (!runtime.decisionValid || runtime.decisionTimer <= 0.0f) {
			Vector3 desired = AddScaled({}, velocityDirection, 0.65f);
			if (behavior.agentWanderChangeInterval > 0.0f) {
				runtime.wanderTimer -= behavior.agentFlockDecisionInterval > 0.0f
					? behavior.agentFlockDecisionInterval
					: dt;
				if (runtime.wanderTimer <= 0.0f) {
					++runtime.wanderStep;
					runtime.wanderDirection = BuildFlockWanderDirection(
						runtime.heading,
						runtime.seedId,
						runtime.wanderStep,
						behavior.agentWanderDirectionRange,
						behavior.agentWanderVerticalRange
					);
					runtime.wanderTimer = behavior.agentWanderChangeInterval *
						(0.75f + Hash01(
							runtime.seedId,
							331u + runtime.wanderStep * 3u
						) * 0.5f);
				}
			}
			desired = AddScaled(
				desired,
				runtime.wanderDirection,
				behavior.agentWanderStrength
			);
			if (behavior.agentUseTeamHeading) {
				desired = AddScaled(
					desired,
					SafeNormalize(
						behavior.agentTeamHeadingDirection,
						velocityDirection
					),
					behavior.agentTeamHeadingWeight
				);
			}
			if (bounds.valid) {
				desired = AddScaled(
					desired,
					ComputeBoundsSteering(runtime.center, bounds),
					behavior.agentBoundsWeight
				);
			}
			AgentAttractorTarget attractor{};
			float speedScale = 1.0f;
			if (
				behavior.agentAttractorWeight > 0.0f &&
				TryResolveAgentAttractor(document, behavior, attractor)
			) {
				const Vector3 toAttractor = Math::Subtract(
					attractor.position,
					runtime.center
				);
				const float distance = Math::Length(toAttractor);
				const float radius = (std::max)(attractor.radius, 0.001f);
				desired = AddScaled(
					desired,
					SafeNormalize(toAttractor, velocityDirection),
					behavior.agentAttractorWeight * attractor.strength *
						std::clamp(distance / radius, 0.0f, 2.0f)
				);
				speedScale = distance < radius ? 0.7f : 1.0f;
			}
			runtime.desiredDirection = SafeNormalize(desired, velocityDirection);
			runtime.desiredSpeed =
				(behavior.agentMinSpeed + behavior.agentMaxSpeed) * 0.5f * speedScale;
			runtime.decisionTimer = behavior.agentFlockDecisionInterval;
			runtime.decisionValid = true;
		}
		runtime.heading = RotateDirectionToward(
			runtime.heading,
			runtime.desiredDirection,
			behavior.agentFlockTurnRate * dt,
			velocityDirection
		);
		runtime.velocity = MoveVectorToward(
			runtime.velocity,
			Math::Multiply(runtime.heading, runtime.desiredSpeed),
			behavior.agentFlockAcceleration * dt
		);
		runtime.center = Math::Add(
			runtime.center,
			Math::Multiply(runtime.velocity, dt)
		);
		if (bounds.valid) {
			runtime.center = ClampToBounds(runtime.center, bounds);
		}
		runtime.forwardAxis = behavior.agentForwardAxis;
		runtime.rotation = BuildAgentVelocityRotation(
			behavior,
			runtime.rotation,
			runtime.velocity,
			runtime.desiredDirection,
			dt
		);
	}

	for (AgentUpdateEntry& agent : agents) {
		const SceneComponent& behavior = agent.behavior;
		AgentRuntime& runtime = *agent.runtime;
		Transform transform = agent.transform;
		Vector3 position = transform.translate;
		const auto teamRuntimeIt =
			agent.teamKey.empty()
				? agentTeamRuntimes_.end()
				: agentTeamRuntimes_.find(agent.teamKey);
		const TeamRuntime* teamRuntime =
			teamRuntimeIt == agentTeamRuntimes_.end()
				? nullptr
				: &teamRuntimeIt->second;
		if (teamRuntime) {
			bool initializedFlockThisFrame = false;
			if (
				!runtime.flockInitialized ||
				runtime.flockSeedId != teamRuntime->seedId
			) {
				runtime.velocity = teamRuntime->velocity;
				runtime.phase = Hash01(
					agent.entity->id ^ teamRuntime->seedId,
					59u
				) * 6.28318530718f;
				runtime.flockSeedId = teamRuntime->seedId;
				runtime.flockInitialized = true;
				initializedFlockThisFrame = true;
			}
			const Vector3 teamDirection = SafeNormalize(
				teamRuntime->velocity,
				teamRuntime->heading
			);
			const float teamHeadingYaw = std::atan2(
				teamRuntime->heading.x,
				teamRuntime->heading.z
			);
			const float jitterUpdateInterval = (std::max)(
				behavior.agentMemberJitterUpdateInterval,
				0.0f
			);
			if (initializedFlockThisFrame) {
				runtime.jitterStep = 0;
				runtime.jitterTargetLocal = BuildFlockMemberJitterTarget(
					agent.entity->id,
					teamRuntime->seedId,
					runtime.jitterStep,
					behavior.agentMemberJitterStrength
				);
				runtime.jitterTimer = jitterUpdateInterval * (
					0.2f + Hash01(agent.entity->id ^ teamRuntime->seedId, 617u) * 0.8f
				);
			} else {
				runtime.jitterTimer = (std::max)(
					runtime.jitterTimer - dt,
					0.0f
				);
				if (jitterUpdateInterval <= 0.0f || runtime.jitterTimer <= 0.0f) {
					++runtime.jitterStep;
					runtime.jitterTargetLocal = BuildFlockMemberJitterTarget(
						agent.entity->id,
						teamRuntime->seedId,
						runtime.jitterStep,
						behavior.agentMemberJitterStrength
					);
					runtime.jitterTimer = jitterUpdateInterval * (
						0.75f + Hash01(
							agent.entity->id ^ teamRuntime->seedId,
							631u + runtime.jitterStep
						) * 0.5f
					);
				}
			}
			runtime.phase += dt * behavior.agentMemberJitterFrequency;
			const Vector3 jitterDetail = {
				std::sin(runtime.phase * 1.37f + Hash01(agent.entity->id, 3u) * 8.0f) *
					behavior.agentMemberJitterStrength * 0.18f,
				std::sin(runtime.phase * 0.83f + Hash01(agent.entity->id, 5u) * 9.0f) *
					behavior.agentMemberJitterStrength * 0.08f,
				std::cos(runtime.phase * 1.11f + Hash01(agent.entity->id, 7u) * 7.0f) *
					behavior.agentMemberJitterStrength * 0.18f
			};
			const Vector3 localJitterTarget = Math::Add(
				runtime.jitterTargetLocal,
				jitterDetail
			);
			const Vector3 jitterTarget = RotateDirection(
				localJitterTarget,
				{ 0.0f, teamHeadingYaw, 0.0f }
			);
			if (initializedFlockThisFrame) {
				runtime.jitterOffset = jitterTarget;
			} else {
				runtime.jitterOffset = LerpVector(
					runtime.jitterOffset,
					jitterTarget,
					FollowAmount(behavior.agentMemberJitterFollowSpeed, dt)
				);
			}
			const Vector3 memberTarget = Math::Add(
				teamRuntime->center,
				runtime.jitterOffset
			);
			const Vector3 toTarget = Math::Subtract(memberTarget, position);
			const float maxDistance = (std::max)(
				behavior.agentMemberLeashDistance,
				0.01f
			);
			const float correctionLimit = (std::max)(
				behavior.agentMemberCatchupSpeed *
					(1.0f + behavior.agentMemberLeashStrength),
				0.01f
			);
			const Vector3 correctionVelocity = ClampVectorLength(
				Math::Multiply(toTarget, behavior.agentMemberCenterFollow),
				correctionLimit
			);
			runtime.velocity = Math::Add(
				teamRuntime->velocity,
				correctionVelocity
			);
			position = Math::Add(position, Math::Multiply(runtime.velocity, dt));
			const Vector3 leaderOffset = Math::Subtract(
				position,
				teamRuntime->center
			);
			const float leaderDistance = Math::Length(leaderOffset);
			if (leaderDistance > maxDistance) {
				position = Math::Add(
					teamRuntime->center,
					Math::Multiply(
						Math::Normalize(leaderOffset),
						maxDistance
					)
				);
			}
			runtime.velocity = Math::Multiply(
				Math::Subtract(position, transform.translate),
				1.0f / dt
			);
			const Vector3 desiredDirection = SafeNormalize(
				runtime.velocity,
				teamDirection
			);
			AgentBounds bounds{};
			if (TryResolveAgentBounds(document, behavior, position, bounds) && bounds.valid) {
				position = ClampToBounds(position, bounds);
			}
			transform.translate = position;
			transform.rotate = BuildAgentVelocityRotation(
				behavior,
				transform.rotate,
				runtime.velocity,
				desiredDirection,
				dt
			);
			agent.object->GetTransform() = transform;
			agent.object->Update();
			agent.entity->transform = transform;
			agent.transform = transform;
			continue;
		}
		runtime.flockInitialized = false;
		runtime.flockSeedId = 0;
		runtime.jitterOffset = {};
		runtime.jitterTargetLocal = {};
		runtime.jitterTimer = 0.0f;
		runtime.jitterStep = 0;
		runtime.separationCacheValid = false;
		runtime.cachedSeparationSteering = {};
		runtime.separationTimer = 0.0f;

		const Vector3 velocityDirection = SafeNormalize(
			runtime.velocity,
			{ 0.0f, 0.0f, 1.0f }
		);
		Vector3 desired = AddScaled({}, velocityDirection, 0.65f);

		runtime.phase += dt * (
			0.7f +
			Hash01(agent.entity->id, 71u) * 0.8f
		);
		const Vector3 wander = SafeNormalize(
			{
				std::sin(runtime.phase * 1.37f + Hash01(agent.entity->id, 3u) * 8.0f),
				std::sin(runtime.phase * 0.83f + Hash01(agent.entity->id, 5u) * 9.0f) * 0.45f,
				std::cos(runtime.phase * 1.11f + Hash01(agent.entity->id, 7u) * 7.0f)
			},
			velocityDirection
		);
		desired = AddScaled(
			desired,
			wander,
			behavior.agentWanderStrength
		);

		AgentBounds bounds{};
		if (TryResolveAgentBounds(document, behavior, position, bounds)) {
			desired = AddScaled(
				desired,
				ComputeBoundsSteering(position, bounds),
				behavior.agentBoundsWeight
			);
		}

		AgentAttractorTarget attractor{};
		const bool hasAttractor =
			behavior.agentAttractorWeight > 0.0f &&
			TryResolveAgentAttractor(document, behavior, attractor);
		float attractorSpeedScale = 1.0f;
		if (hasAttractor) {
			const Vector3 toAttractor =
				Math::Subtract(attractor.position, position);
			const float distance = Math::Length(toAttractor);
			const Vector3 toAttractorDirection =
				SafeNormalize(toAttractor, velocityDirection);
			const float radius = (std::max)(attractor.radius, 0.001f);
			const float strength =
				behavior.agentAttractorWeight * attractor.strength;
			if (distance > radius * 0.45f) {
				const float ratio = std::clamp(distance / radius, 0.0f, 2.0f);
				desired = AddScaled(
					desired,
					toAttractorDirection,
					strength * ratio
				);
			} else {
				const Vector3 tangent = SafeNormalize(
					{
						-toAttractorDirection.z,
						std::sin(runtime.phase) * 0.18f,
						toAttractorDirection.x
					},
					velocityDirection
				);
				desired = AddScaled(desired, tangent, strength);
				desired = AddScaled(
					desired,
					toAttractorDirection,
					strength * 0.25f
				);
			}
			if (distance < radius) {
				attractorSpeedScale = 0.7f;
			}
		}

		runtime.schoolingTimer =
			(std::max)(runtime.schoolingTimer - dt, 0.0f);
		if (behavior.agentSchooling) {
			const bool shouldUpdateSchooling =
				behavior.agentSchoolingUpdateInterval <= 0.0f ||
				!runtime.schoolingCacheValid ||
				runtime.schoolingTimer <= 0.0f;
			if (shouldUpdateSchooling) {
				Vector3 separation{};
				Vector3 alignment{};
				Vector3 cohesion{};
				uint32_t alignmentCount = 0;
				uint32_t cohesionCount = 0;
				int neighborCount = 0;

				for (const AgentUpdateEntry& other : agents) {
					if (
						other.entity == agent.entity ||
						other.behavior.agentGroupName != behavior.agentGroupName ||
						other.behavior.agentBehaviorName != behavior.agentBehaviorName
					) {
						continue;
					}
					if (
						behavior.agentNeighborLimit > 0 &&
						neighborCount >= behavior.agentNeighborLimit
					) {
						break;
					}
					++neighborCount;

					const Vector3 offset =
						Math::Subtract(position, other.transform.translate);
					const float distance = Math::Length(offset);
					if (distance <= 0.0001f) {
						continue;
					}
					if (distance < behavior.agentSeparationRadius) {
						const float ratio = 1.0f -
							distance / (std::max)(
								behavior.agentSeparationRadius,
								0.0001f
							);
						separation = AddScaled(
							separation,
							Math::Normalize(offset),
							ratio
						);
					}
					if (distance < behavior.agentAlignmentRadius) {
						alignment = Math::Add(
							alignment,
							other.runtime->velocity
						);
						++alignmentCount;
					}
					if (distance < behavior.agentCohesionRadius) {
						cohesion = Math::Add(
							cohesion,
							other.transform.translate
						);
						++cohesionCount;
					}
				}

				Vector3 schoolingSteering{};
				if (Math::Length(separation) > 0.0001f) {
					schoolingSteering = AddScaled(
						schoolingSteering,
						Math::Normalize(separation),
						behavior.agentSeparationWeight
					);
				}
				if (alignmentCount > 0) {
					alignment = Math::Multiply(
						alignment,
						1.0f / static_cast<float>(alignmentCount)
					);
					schoolingSteering = AddScaled(
						schoolingSteering,
						SafeNormalize(alignment, velocityDirection),
						behavior.agentAlignmentWeight
					);
				}
				if (cohesionCount > 0) {
					cohesion = Math::Multiply(
						cohesion,
						1.0f / static_cast<float>(cohesionCount)
					);
					schoolingSteering = AddScaled(
						schoolingSteering,
						SafeNormalize(
							Math::Subtract(cohesion, position),
							velocityDirection
						),
						behavior.agentCohesionWeight
					);
				}

				const float schoolingBlend = std::clamp(
					behavior.agentSchoolingBlend,
					0.0f,
					1.0f
				);
				runtime.cachedSchoolingSteering =
					runtime.schoolingCacheValid
						? LerpVector(
							runtime.cachedSchoolingSteering,
							schoolingSteering,
							schoolingBlend
						)
						: schoolingSteering;
				runtime.schoolingCacheValid = true;
				if (behavior.agentSchoolingUpdateInterval > 0.0f) {
					const float jitter =
						(Hash01(agent.entity->id, 193u) * 2.0f - 1.0f) *
						behavior.agentSchoolingUpdateJitter;
					runtime.schoolingTimer = (std::max)(
						behavior.agentSchoolingUpdateInterval *
							(1.0f + jitter),
						0.0f
					);
				} else {
					runtime.schoolingTimer = 0.0f;
				}
			}
			desired = Math::Add(desired, runtime.cachedSchoolingSteering);
		} else {
			runtime.schoolingCacheValid = false;
			runtime.cachedSchoolingSteering = {};
			runtime.schoolingTimer = 0.0f;
		}

		if (
			teamRuntime &&
			behavior.agentUseTeamHeading &&
			behavior.agentTeamHeadingWeight > 0.0f
		) {
			desired = AddScaled(
				desired,
				teamRuntime->heading,
				behavior.agentTeamHeadingWeight
			);
		}

		const Vector3 desiredDirection =
			SafeNormalize(desired, velocityDirection);
		const float speed =
			(
				behavior.agentMinSpeed +
				(behavior.agentMaxSpeed - behavior.agentMinSpeed) *
				Hash01(agent.entity->id, 97u)
			) *
			attractorSpeedScale;
		const Vector3 targetVelocity =
			Math::Multiply(desiredDirection, speed);
		const float turnLerp =
			std::clamp(behavior.agentTurnSpeed * dt, 0.0f, 1.0f);
		runtime.velocity = LerpVector(
			runtime.velocity,
			targetVelocity,
			turnLerp
		);

		position = Math::Add(position, Math::Multiply(runtime.velocity, dt));
		if (bounds.valid) {
			position = ClampToBounds(position, bounds);
		}
		transform.translate = position;

		Vector3 rotationDirection = SafeNormalize(
			runtime.velocity,
			desiredDirection
		);
		if (
			teamRuntime &&
			behavior.agentUseTeamRotation &&
			behavior.agentTeamRotationWeight > 0.0f
		) {
			const Vector3 teamRotationDirection =
				ForwardDirectionFromRotation(
					teamRuntime->rotation,
					teamRuntime->forwardAxis,
					teamRuntime->heading
				);
			rotationDirection = BlendDirections(
				rotationDirection,
				teamRotationDirection,
				behavior.agentTeamRotationWeight,
				rotationDirection
			);
		}
		transform.rotate = BuildAgentVelocityRotation(
			behavior,
			transform.rotate,
			rotationDirection,
			desiredDirection,
			dt
		);

		agent.object->GetTransform() = transform;
		agent.object->Update();
		agent.entity->transform = transform;
		agent.transform = transform;
	}
}

void GamePlayScene::StepPhysics(float deltaTime) {
	EditorSession* editorSession = sceneManager_
		? sceneManager_->GetEditorSession()
		: nullptr;
	if (!editorSession || !editorSession->IsPlaying()) {
		return;
	}

	SceneDocument* document = sceneManager_
		? sceneManager_->GetActiveSceneDocument()
		: nullptr;
	if (!document) {
		return;
	}

	physicsWorld_.Clear();
	for (Collider* staticCollider : staticColliders_) {
		physicsWorld_.AddStaticCollider(staticCollider);
	}
	if (player_ && player_->GetObject()) {
		physicsWorld_.AddBody(&player_->GetPhysicsBody());
	}
	for (auto& [entityId, sceneObject] : sceneModelObjects_) {
		const SceneEntity* entity = document->FindEntity(entityId);
		if (
			!sceneObject.object ||
			!sceneObject.hasPhysicsBody ||
			(entity && HasComponent(*entity, "PlayerBehavior"))
		) {
			continue;
		}
		physicsWorld_.AddBody(&sceneObject.physicsBody);
	}

	physicsWorld_.Step(deltaTime);

	for (const SceneEntity& entity : document->GetEntities()) {
		const auto found = sceneModelObjects_.find(entity.id);
		if (
			found == sceneModelObjects_.end() ||
			!found->second.object ||
			!found->second.hasPhysicsBody ||
			HasComponent(entity, "PlayerBehavior")
		) {
			continue;
		}
		found->second.object->Update();
		if (SceneEntity* writableEntity = document->FindEntity(entity.id)) {
			writableEntity->transform = found->second.object->GetTransform();
		}
	}
}

bool GamePlayScene::ApplyCameraComponentToCamera(
	const SceneDocument& document,
	const SceneEntity& cameraEntity,
	const SceneComponent& cameraComponent,
	Camera* camera,
	float aspectRatio
) const {
	if (!camera || !IsEntityActiveInHierarchy(document, cameraEntity)) {
		return false;
	}
	const Transform cameraTransform =
		ResolveScene3DTransform(document, cameraEntity);
	camera->SetOrbitMode(false);
	camera->SetTranslate(cameraTransform.translate);
	camera->SetRotate(cameraTransform.rotate);
	camera->SetFovY(std::clamp(
		cameraComponent.cameraFovY,
		0.0174532925f,
		3.12413936f
	));
	camera->SetAspectRatio((std::max)(aspectRatio, 0.001f));
	camera->SetNearClip((std::max)(cameraComponent.cameraNearClip, 0.001f));
	camera->SetFarClip((std::max)(
		cameraComponent.cameraFarClip,
		cameraComponent.cameraNearClip + 0.001f
	));
	camera->Update();
	return true;
}

bool GamePlayScene::ApplyPlayerCameraMouseLook(SceneDocument& document) {
	EditorSession* editorSession = sceneManager_
		? sceneManager_->GetEditorSession()
		: nullptr;
	if (!editorSession || !editorSession->IsPlaying() || !camera_ || !player_) {
		playerCameraInitialized_ = false;
		return false;
	}

	const SceneEntity* fallbackEntity = nullptr;
	const SceneComponent* fallbackCamera = nullptr;
	const SceneComponent* fallbackThirdPersonCamera = nullptr;
	const SceneEntity* mainEntity = nullptr;
	const SceneComponent* mainCamera = nullptr;
	const SceneComponent* mainThirdPersonCamera = nullptr;
	for (const SceneEntity& entity : document.GetEntities()) {
		if (!IsEntityActiveInHierarchy(document, entity)) {
			continue;
		}
		const SceneComponent* cameraComponent =
			FindEnabledComponent(entity, "Camera");
		const SceneComponent* thirdPersonCamera =
			FindEnabledComponent(entity, "ThirdPersonCamera");
		if (!cameraComponent || !thirdPersonCamera) {
			continue;
		}
		if (!fallbackEntity) {
			fallbackEntity = &entity;
			fallbackCamera = cameraComponent;
			fallbackThirdPersonCamera = thirdPersonCamera;
		}
		if (cameraComponent->cameraIsMain) {
			mainEntity = &entity;
			mainCamera = cameraComponent;
			mainThirdPersonCamera = thirdPersonCamera;
			break;
		}
	}

	const SceneEntity* cameraEntity = mainEntity ? mainEntity : fallbackEntity;
	const SceneComponent* cameraComponent =
		mainCamera ? mainCamera : fallbackCamera;
	const SceneComponent* thirdPersonCamera =
		mainThirdPersonCamera
			? mainThirdPersonCamera
			: fallbackThirdPersonCamera;
	if (
		!cameraEntity ||
		!cameraComponent ||
		!thirdPersonCamera ||
		!HasComponent(*cameraEntity, "PlayerBehavior")
	) {
		playerCameraInitialized_ = false;
		return false;
	}

	if (!playerCameraInitialized_) {
		playerCameraController_.Initialize(camera_);
		playerCameraController_.SetYawPitch(
			cameraEntity->transform.rotate.y,
			cameraEntity->transform.rotate.x
		);
		playerCameraController_.SetDistance(
			thirdPersonCamera->thirdPersonDistance
		);
		playerCameraController_.SetAimDistance(
			thirdPersonCamera->thirdPersonAimDistance
		);
		playerCameraInitialized_ = true;
	}

	Input* input = Input::GetInstance();
	const bool altHeld =
		input && (input->PushKey(DIK_LMENU) || input->PushKey(DIK_RMENU));
	if (altHeld) {
		return true;
	}

	camera_->SetOrbitMode(false);
	camera_->SetFovY(std::clamp(
		cameraComponent->cameraFovY,
		0.0174532925f,
		3.12413936f
	));
	camera_->SetNearClip((std::max)(cameraComponent->cameraNearClip, 0.001f));
	camera_->SetFarClip((std::max)(
		cameraComponent->cameraFarClip,
		cameraComponent->cameraNearClip + 0.001f
	));
	playerCameraController_.SetTargetOffset(
		thirdPersonCamera->thirdPersonTargetOffset
	);
	playerCameraController_.SetAimTargetOffset(
		thirdPersonCamera->thirdPersonAimTargetOffset
	);
	playerCameraController_.SetMouseSensitivity(
		thirdPersonCamera->thirdPersonMouseSensitivity
	);
	playerCameraController_.SetPitchLimit(
		thirdPersonCamera->thirdPersonMinPitch,
		thirdPersonCamera->thirdPersonMaxPitch
	);
	playerCameraController_.SetOcclusionMargin(
		thirdPersonCamera->thirdPersonOcclusionMargin
	);
	playerCameraController_.SetMouseInvert(
		thirdPersonCamera->thirdPersonInvertYaw,
		thirdPersonCamera->thirdPersonInvertPitch
	);
	std::vector<OBBCollider*> cameraObstacles;
	for (const SceneEntity& entity : document.GetEntities()) {
		if (
			HasComponent(entity, "PlayerBehavior") ||
			!IsEntityActiveInHierarchy(document, entity)
		) {
			continue;
		}
		std::string modelPath = entity.modelPath;
		if (const SceneComponent* meshRenderer =
			FindEnabledComponent(entity, "MeshRenderer")) {
			modelPath = meshRenderer->modelPath;
		}
		std::transform(
			modelPath.begin(),
			modelPath.end(),
			modelPath.begin(),
			[](unsigned char c) {
				return static_cast<char>(std::tolower(c));
			}
		);
		if (modelPath.find("terrain") != std::string::npos) {
			continue;
		}
		const auto found = sceneModelObjects_.find(entity.id);
		if (
			found != sceneModelObjects_.end() &&
			found->second.hasCollider
		) {
			if (
				found->second.collider &&
				found->second.collider->GetType() == Collider::Type::OBB
			) {
				cameraObstacles.push_back(&found->second.boxCollider);
			}
		}
	}
	for (StageObject& stageObject : stageObjects_) {
		if (stageObject.object) {
			cameraObstacles.push_back(&stageObject.collider);
		}
	}
	playerCameraController_.Update(
		player_->GetPosition(),
		cameraObstacles,
		!altHeld
	);
	ApplyPlayerCameraDissolve(document);
	return true;
}

void GamePlayScene::ApplyPlayerCameraDissolve(
	const SceneDocument& document
) {
	EditorSession* editorSession = sceneManager_
		? sceneManager_->GetEditorSession()
		: nullptr;
	if (!editorSession || !editorSession->IsPlaying()) {
		return;
	}

	constexpr float kStartPitch = -0.35f;
	constexpr float kFullPitch = -0.85f;
	const float rawAmount = std::clamp(
		(kStartPitch - playerCameraController_.GetPitch()) /
			(kStartPitch - kFullPitch),
		0.0f,
		1.0f
	);
	const float dissolveAmount =
		rawAmount * rawAmount * (3.0f - 2.0f * rawAmount);

	for (const SceneEntity& entity : document.GetEntities()) {
		if (!HasComponent(entity, "PlayerBehavior")) {
			continue;
		}
		const auto found = sceneModelObjects_.find(entity.id);
		if (found == sceneModelObjects_.end() || !found->second.object) {
			continue;
		}
		found->second.object->SetDissolve(
			dissolveAmount,
			0.08f,
			6.0f
		);
	}
}

void GamePlayScene::ClearMonitorRenderers() {
	for (auto& [entityId, runtime] : monitorRuntimes_) {
		const auto sceneObject = sceneModelObjects_.find(entityId);
		if (sceneObject != sceneModelObjects_.end() && sceneObject->second.object) {
			sceneObject->second.object->ClearTextureOverride();
		}
		delete runtime.camera;
		runtime.camera = nullptr;
		delete runtime.renderTarget;
		runtime.renderTarget = nullptr;
	}
	monitorRuntimes_.clear();
}

void GamePlayScene::SyncMonitorRenderers() {
	SceneDocument* document = sceneManager_
		? sceneManager_->GetActiveSceneDocument()
		: nullptr;
	if (!document) {
		ClearMonitorRenderers();
		return;
	}

	std::unordered_set<uint64_t> requiredIds;
	for (const SceneEntity& entity : document->GetEntities()) {
		const SceneComponent* monitorRenderer =
			FindEnabledComponent(entity, "MonitorRenderer");
		if (!monitorRenderer) {
			continue;
		}
		requiredIds.insert(entity.id);
		MonitorRuntime& runtime = monitorRuntimes_[entity.id];
		const uint32_t width = std::clamp<uint32_t>(
			monitorRenderer->monitorWidth,
			64,
			2048
		);
		const uint32_t height = std::clamp<uint32_t>(
			monitorRenderer->monitorHeight,
			64,
			2048
		);
		runtime.hideSelf = monitorRenderer->monitorHideSelf;
		if (!runtime.camera) {
			runtime.camera = new Camera();
		}
		if (
			!runtime.renderTarget ||
			runtime.width != width ||
			runtime.height != height
		) {
			delete runtime.renderTarget;
			runtime.renderTarget = new SceneRenderTarget();
			SceneRenderTarget::Desc desc{};
			desc.width = width;
			desc.height = height;
			desc.format = RenderFormats::kSceneHdrFormat;
			desc.clearColor[0] = 0.02f;
			desc.clearColor[1] = 0.02f;
			desc.clearColor[2] = 0.025f;
			desc.clearColor[3] = 1.0f;
			runtime.renderTarget->Initialize(
				Object3dCommon::GetInstance()->GetDxCommon(),
				SrvManager::GetInstance(),
				desc
			);
			runtime.width = width;
			runtime.height = height;
		}
	}

	for (auto iterator = monitorRuntimes_.begin(); iterator != monitorRuntimes_.end();) {
		if (!requiredIds.contains(iterator->first)) {
			const auto sceneObject = sceneModelObjects_.find(iterator->first);
			if (sceneObject != sceneModelObjects_.end() && sceneObject->second.object) {
				sceneObject->second.object->ClearTextureOverride();
			}
			delete iterator->second.camera;
			delete iterator->second.renderTarget;
			iterator = monitorRuntimes_.erase(iterator);
		} else {
			++iterator;
		}
	}
}

void GamePlayScene::ApplyRenderCamera(Camera* viewCamera) {
	Object3dCommon::GetInstance()->SetDefaultCamera(viewCamera);
	for (StageObject& stageObject : stageObjects_) {
		if (stageObject.object) {
			stageObject.object->UpdateForCamera(viewCamera);
		}
	}
	for (auto& [entityId, sceneObject] : sceneModelObjects_) {
		(void)entityId;
		if (sceneObject.object) {
			sceneObject.object->UpdateForCamera(viewCamera);
		}
	}
	if (plane_) {
		plane_->UpdateForCamera(viewCamera);
	}
	if (axis) {
		axis->UpdateForCamera(viewCamera);
	}
	if (skybox_) {
		skybox_->SetCamera(viewCamera);
		skybox_->Update();
	}
	ParticleManager::GetInstance()->SetCamera(viewCamera);
}

Camera* GamePlayScene::GetSceneViewCamera() const {
	EditorSession* editorSession = sceneManager_
		? sceneManager_->GetEditorSession()
		: nullptr;
	if (editorSession && editorSession->IsPaused() && debugCamera_) {
		return debugCamera_;
	}
	return camera_;
}

void GamePlayScene::InitializePauseDebugCamera() {
	if (!camera_ || !debugCamera_ || debugCameraInitialized_) {
		return;
	}

	debugCamera_->SetFovY(camera_->GetFovY());
	debugCamera_->SetAspectRatio(camera_->GetAspectRatio());
	debugCamera_->SetNearClip(camera_->GetNearClip());
	debugCamera_->SetFarClip(camera_->GetFarClip());

	const Matrix4x4& cameraWorld = camera_->GetWorldMatrix();
	Vector3 forward = {
		cameraWorld.m[2][0],
		cameraWorld.m[2][1],
		cameraWorld.m[2][2]
	};
	const float forwardLength = Math::Length(forward);
	if (forwardLength > 0.000001f) {
		forward = Math::Multiply(forward, 1.0f / forwardLength);
	} else {
		forward = { 0.0f, 0.0f, 1.0f };
	}

	const float orbitDistance = 10.0f;
	debugCamera_->SetOrbitMode(true);
	debugCamera_->SetOrbitDistance(orbitDistance);
	debugCamera_->SetOrbitTarget(Math::Add(
		camera_->GetTranslate(),
		Math::Multiply(forward, orbitDistance)
	));
	debugCamera_->SetOrbitAngle(
		std::atan2(-forward.x, forward.z),
		std::asin(std::clamp(-forward.y, -1.0f, 1.0f))
	);
	debugCamera_->UpdatePreviewMatrices();
	debugCameraInitialized_ = true;
}

void GamePlayScene::DrawMonitorDebugWindow() {
#if defined(_DEBUG) || defined(DEVELOPMENT)
	if (!ImGui::Begin("Monitor Debug")) {
		ImGui::End();
		return;
	}

	auto drawVector3 = [](const char* label, const Vector3& value) {
		ImGui::Text(
			"%s: %.3f, %.3f, %.3f",
			label,
			value.x,
			value.y,
			value.z
		);
	};
	auto yesNo = [](bool value) {
		return value ? "yes" : "no";
	};

	SceneDocument* document = sceneManager_
		? sceneManager_->GetActiveSceneDocument()
		: nullptr;
	ImGui::Text(
		"Last offscreen pass frame: %llu",
		static_cast<unsigned long long>(monitorDebugFrame_)
	);
	ImGui::Text(
		"Monitor runtimes: %zu",
		monitorRuntimes_.size()
	);
	ImGui::Checkbox(
		"Force probe camera for monitor RT",
		&monitorDebugForceProbeCamera_
	);

	if (Camera* sceneCamera = GetSceneViewCamera()) {
		ImGui::SeparatorText("Scene View Camera");
		drawVector3("Translate", sceneCamera->GetTranslate());
		drawVector3("Rotate", sceneCamera->GetRotate());
		ImGui::Text(
			"FOV: %.3f / Near: %.3f / Far: %.3f",
			sceneCamera->GetFovY(),
			sceneCamera->GetNearClip(),
			sceneCamera->GetFarClip()
		);
	}

	if (!document) {
		ImGui::TextColored(
			ImVec4(0.95f, 0.45f, 0.35f, 1.0f),
			"No active scene document"
		);
		ImGui::End();
		return;
	}

	std::unordered_set<uint64_t> listedRuntimes;
	uint32_t monitorComponentCount = 0;
	for (const SceneEntity& entity : document->GetEntities()) {
		const SceneComponent* monitorRenderer =
			FindEnabledComponent(entity, "MonitorRenderer");
		if (!monitorRenderer) {
			continue;
		}

		++monitorComponentCount;
		listedRuntimes.insert(entity.id);
		const auto runtimeIterator = monitorRuntimes_.find(entity.id);
		const MonitorRuntime* runtime = runtimeIterator != monitorRuntimes_.end()
			? &runtimeIterator->second
			: nullptr;

		std::string header = entity.name.empty()
			? std::string("Monitor")
			: entity.name;
		header += " ##MonitorDebug";
		header += std::to_string(entity.id);
		if (ImGui::TreeNode(header.c_str())) {
			ImGui::Text(
				"Monitor Entity: #%llu / active: %s",
				static_cast<unsigned long long>(entity.id),
				yesNo(IsEntityActiveInHierarchy(*document, entity))
			);
			drawVector3("Monitor Translate", entity.transform.translate);
			drawVector3("Monitor Rotate", entity.transform.rotate);

			ImGui::SeparatorText("Component Target");
			ImGui::Text(
				"Target id: %llu",
				static_cast<unsigned long long>(
					monitorRenderer->monitorCameraEntityId
				)
			);
			ImGui::Text(
				"Target name: %s",
				monitorRenderer->monitorCameraName.empty()
					? "(empty)"
					: monitorRenderer->monitorCameraName.c_str()
			);
			ImGui::Text(
				"Size: %u x %u / Hide self: %s",
				monitorRenderer->monitorWidth,
				monitorRenderer->monitorHeight,
				yesNo(monitorRenderer->monitorHideSelf)
			);
			const SceneComponent* meshRenderer =
				FindEnabledComponent(entity, "MeshRenderer");
			const std::string monitorModelPath = meshRenderer
				? meshRenderer->modelPath
				: std::string{};
			ImGui::Text(
				"Surface mesh: %s",
				monitorModelPath.empty()
					? "(none)"
					: monitorModelPath.c_str()
			);
			if (monitorModelPath == "Cube.obj") {
				ImGui::TextColored(
					ImVec4(0.95f, 0.65f, 0.25f, 1.0f),
					"Cube.obj uses atlas UVs; RT will appear cropped."
				);
			} else if (monitorModelPath == "plane.obj") {
				ImGui::TextColored(
					ImVec4(0.95f, 0.65f, 0.25f, 1.0f),
					"plane.obj faces +Z; use monitor_screen.obj for monitors."
				);
			} else if (monitorModelPath == "monitor_screen.obj") {
				ImGui::TextColored(
					ImVec4(0.45f, 0.95f, 0.55f, 1.0f),
					"monitor_screen.obj faces -Z with full-screen UVs."
				);
			}

			const SceneEntity* liveCameraEntity = nullptr;
			const SceneComponent* liveCameraComponent = nullptr;
			const bool liveResolved = ResolveMonitorTargetCamera(
				*document,
				*monitorRenderer,
				liveCameraEntity,
				liveCameraComponent
			);
			ImGui::SeparatorText("Live Resolve");
			if (liveResolved && liveCameraEntity && liveCameraComponent) {
				const Transform liveCameraTransform =
					ResolveScene3DTransform(*document, *liveCameraEntity);
				ImGui::TextColored(
					ImVec4(0.45f, 0.95f, 0.55f, 1.0f),
					"Resolved: #%llu %s",
					static_cast<unsigned long long>(liveCameraEntity->id),
					liveCameraEntity->name.c_str()
				);
				ImGui::Text(
					"Main: %s / PlayerBehavior: %s",
					yesNo(liveCameraComponent->cameraIsMain),
					yesNo(HasComponent(*liveCameraEntity, "PlayerBehavior"))
				);
				drawVector3(
					"Target Translate",
					liveCameraTransform.translate
				);
				drawVector3(
					"Target Rotate",
					liveCameraTransform.rotate
				);
			} else {
				ImGui::TextColored(
					ImVec4(0.95f, 0.45f, 0.35f, 1.0f),
					"Resolved: no"
				);
			}

			ImGui::SeparatorText("Last Offscreen Pass");
			if (!runtime) {
				ImGui::TextColored(
					ImVec4(0.95f, 0.65f, 0.25f, 1.0f),
					"No runtime has been created yet"
				);
			} else {
				ImGui::Text("Status: %s", runtime->debugStatus.c_str());
				ImGui::Text(
					"Pass frame: %llu",
					static_cast<unsigned long long>(runtime->debugPassFrame)
				);
				ImGui::Text(
					"Resolved: %s / Rendered: %s / Texture applied: %s",
					yesNo(runtime->debugResolvedCamera),
					yesNo(runtime->debugRendered),
					yesNo(runtime->debugTextureApplied)
				);
				ImGui::Text(
					"Last target: #%llu %s",
					static_cast<unsigned long long>(
						runtime->debugTargetCameraId
					),
					runtime->debugTargetCameraName.empty()
						? "(none)"
						: runtime->debugTargetCameraName.c_str()
				);
				ImGui::Text(
					"Last target main: %s / player: %s",
					yesNo(runtime->debugTargetIsMain),
					yesNo(runtime->debugTargetHasPlayerBehavior)
				);
				drawVector3(
					"Last target translate",
					runtime->debugTargetTranslate
				);
				drawVector3(
					"Last target rotate",
					runtime->debugTargetRotate
				);
				if (runtime->camera) {
					drawVector3(
						"Runtime camera translate",
						runtime->camera->GetTranslate()
					);
					drawVector3(
						"Runtime camera rotate",
						runtime->camera->GetRotate()
					);
				}
				ImGui::Text(
					"RT: %s / %u x %u / SRV: 0x%llX",
					runtime->renderTarget ? "yes" : "no",
					runtime->width,
					runtime->height,
					static_cast<unsigned long long>(runtime->debugSrvPtr)
				);
				ImGui::Text(
					"Applied override SRV: 0x%llX / match: %s",
					static_cast<unsigned long long>(
						runtime->debugAppliedTextureOverridePtr
					),
					yesNo(
						runtime->debugSrvPtr != 0 &&
						runtime->debugSrvPtr ==
							runtime->debugAppliedTextureOverridePtr
					)
				);

				if (runtime->renderTarget && runtime->debugSrvPtr != 0) {
					const float availableWidth =
						ImGui::GetContentRegionAvail().x;
					float previewWidth = std::clamp(
						availableWidth,
						160.0f,
						360.0f
					);
					const float aspect =
						runtime->height > 0
							? static_cast<float>(runtime->width) /
								static_cast<float>(runtime->height)
							: 1.0f;
					float previewHeight = previewWidth /
						(aspect > 0.0f ? aspect : 1.0f);
					if (previewHeight > 260.0f) {
						previewHeight = 260.0f;
						previewWidth = previewHeight *
							(aspect > 0.0f ? aspect : 1.0f);
					}
					ImGui::TextDisabled("RT preview from previous pass");
					ImGui::Image(
						ImTextureRef(
							static_cast<ImTextureID>(runtime->debugSrvPtr)
						),
						ImVec2(previewWidth, previewHeight)
					);
				}
			}

			ImGui::TreePop();
		}
	}

	if (monitorComponentCount == 0) {
		ImGui::TextDisabled("No enabled MonitorRenderer components");
	}

	for (const auto& [entityId, runtime] : monitorRuntimes_) {
		if (listedRuntimes.contains(entityId)) {
			continue;
		}
		ImGui::TextColored(
			ImVec4(0.95f, 0.65f, 0.25f, 1.0f),
			"Orphan runtime: #%llu / %s",
			static_cast<unsigned long long>(entityId),
			runtime.debugStatus.c_str()
		);
	}

	ImGui::End();
#endif
}

bool GamePlayScene::TryStartCameraPath(SceneDocument& document) {
	if (!camera_ || cameraPathRuntime_.IsPlaying()) {
		return false;
	}

	Input* input = Input::GetInstance();
	if (!input) {
		return false;
	}

	for (const SceneEntity& entity : document.GetEntities()) {
		if (!IsEntityActiveInHierarchy(document, entity)) {
			continue;
		}
		const SceneComponent* cameraPath =
			FindEnabledComponent(entity, "CameraPath");
		if (!cameraPath) {
			continue;
		}
		if (cameraPath->cameraPathTriggerType != "Key") {
			continue;
		}
		std::string triggerKey = cameraPath->cameraPathTriggerKey;
		std::transform(
			triggerKey.begin(),
			triggerKey.end(),
			triggerKey.begin(),
			[](unsigned char c) {
				return static_cast<char>(std::toupper(c));
			}
		);
		const bool triggered =
			(triggerKey.empty() || triggerKey == "C") &&
			input->TriggerKey(DIK_C);
		if (!triggered) {
			continue;
		}

		const SceneEntity* targetCameraEntity = nullptr;
		const SceneComponent* targetCameraComponent = nullptr;
		if (!cameraPath->cameraPathTargetCameraName.empty()) {
			targetCameraEntity =
				document.FindEntityByName(cameraPath->cameraPathTargetCameraName);
			targetCameraComponent = targetCameraEntity
				? FindEnabledComponent(*targetCameraEntity, "Camera")
				: nullptr;
		} else {
			const SceneEntity* fallbackEntity = nullptr;
			const SceneComponent* fallbackCamera = nullptr;
			for (const SceneEntity& cameraCandidate : document.GetEntities()) {
				if (!IsEntityActiveInHierarchy(document, cameraCandidate)) {
					continue;
				}
				const SceneComponent* cameraComponent =
					FindEnabledComponent(cameraCandidate, "Camera");
				if (!cameraComponent) {
					continue;
				}
				if (!fallbackEntity) {
					fallbackEntity = &cameraCandidate;
					fallbackCamera = cameraComponent;
				}
				if (cameraComponent->cameraIsMain) {
					targetCameraEntity = &cameraCandidate;
					targetCameraComponent = cameraComponent;
					break;
				}
			}
			if (!targetCameraEntity) {
				targetCameraEntity = fallbackEntity;
				targetCameraComponent = fallbackCamera;
			}
		}
		if (!targetCameraEntity || !targetCameraComponent) {
			continue;
		}
		camera_->SetFovY(std::clamp(
			targetCameraComponent->cameraFovY,
			0.0174532925f,
			3.12413936f
		));
		camera_->SetNearClip((std::max)(
			targetCameraComponent->cameraNearClip,
			0.001f
		));
		camera_->SetFarClip((std::max)(
			targetCameraComponent->cameraFarClip,
			targetCameraComponent->cameraNearClip + 0.001f
		));
		camera_->Update();
		cameraPathRuntime_.Play(document, entity, *cameraPath, *camera_);
		return cameraPathRuntime_.IsPlaying();
	}
	return false;
}

void GamePlayScene::SyncPlayerCameraControllerFromCurrentCamera(
	const SceneDocument& document
) {
	if (!camera_ || !player_) {
		return;
	}

	const SceneComponent* targetThirdPersonCamera = nullptr;
	const SceneComponent* fallbackThirdPersonCamera = nullptr;
	for (const SceneEntity& entity : document.GetEntities()) {
		if (!IsEntityActiveInHierarchy(document, entity)) {
			continue;
		}
		const SceneComponent* cameraComponent =
			FindEnabledComponent(entity, "Camera");
		const SceneComponent* thirdPersonCamera =
			FindEnabledComponent(entity, "ThirdPersonCamera");
		if (!cameraComponent || !thirdPersonCamera) {
			continue;
		}
		if (!fallbackThirdPersonCamera) {
			fallbackThirdPersonCamera = thirdPersonCamera;
		}
		if (cameraComponent->cameraIsMain) {
			targetThirdPersonCamera = thirdPersonCamera;
			break;
		}
	}

	const SceneComponent* thirdPersonCamera =
		targetThirdPersonCamera
			? targetThirdPersonCamera
			: fallbackThirdPersonCamera;
	if (!thirdPersonCamera) {
		return;
	}

	const Vector3 focus = Math::Add(
		player_->GetPosition(),
		playerCameraController_.IsAimMode()
			? thirdPersonCamera->thirdPersonAimTargetOffset
			: thirdPersonCamera->thirdPersonTargetOffset
	);
	playerCameraController_.SyncFromCameraPose(
		camera_->GetTranslate(),
		focus
	);
	playerCameraInitialized_ = true;
}

void GamePlayScene::DrawCameraPathDebug(
	const SceneDocument& document,
	bool showPath,
	bool showPointCameraDirection
) {
#if defined(_DEBUG) || defined(DEVELOPMENT)
	if (!showPath && !showPointCameraDirection) {
		return;
	}
	DebugRenderer* debugRenderer = DebugRenderer::GetInstance();
	const float aspectRatio = camera_ ? camera_->GetAspectRatio() : 1.0f;
	SceneComponent pointDebugCamera{};
	pointDebugCamera.type = "Camera";
	pointDebugCamera.enabled = true;
	pointDebugCamera.cameraFovY = 0.45f;
	pointDebugCamera.cameraNearClip = 0.1f;
	pointDebugCamera.cameraFarClip = 8.0f;
	pointDebugCamera.cameraIsMain = false;
	for (const SceneEntity& pathEntity : document.GetEntities()) {
		if (
			!IsEntityActiveInHierarchy(document, pathEntity) ||
			!FindEnabledComponent(pathEntity, "CameraPath")
		) {
			continue;
		}

		std::vector<Transform> points;
		std::vector<const SceneEntity*> pointEntities;
		for (const SceneEntity& pointEntity : document.GetEntities()) {
			if (
				pointEntity.parentId != pathEntity.id ||
				!IsEntityActiveInHierarchy(document, pointEntity) ||
				!FindEnabledComponent(pointEntity, "CameraPathPoint")
			) {
				continue;
			}
			points.push_back(ResolveScene3DTransform(document, pointEntity));
			pointEntities.push_back(&pointEntity);
		}

		for (size_t index = 0; index < points.size(); ++index) {
			const Transform& point = points[index];
			if (showPath) {
				debugRenderer->AddSphere(
					point.translate,
					0.14f,
					{ 1.0f, 0.45f, 0.2f, 1.0f },
					8
				);
				if (index + 1 < points.size()) {
					debugRenderer->AddLine(
						point.translate,
						points[index + 1].translate,
						{ 1.0f, 0.45f, 0.2f, 1.0f }
					);
				}
			}
			if (showPointCameraDirection && index < pointEntities.size()) {
				AddCameraDebugDraw(
					document,
					*pointEntities[index],
					pointDebugCamera,
					aspectRatio,
					&point
				);
			}
		}
	}

	if (showPath &&
		cameraPathRuntime_.IsPlaying() &&
		cameraPathRuntime_.HasCurrentTransform()) {
		const Transform& current = cameraPathRuntime_.GetCurrentTransform();
		debugRenderer->AddSphere(
			current.translate,
			0.18f,
			{ 0.35f, 1.0f, 0.55f, 1.0f },
			8
		);
	}
#else
	(void)document;
	(void)showPath;
	(void)showPointCameraDirection;
#endif
}

void GamePlayScene::DrawSceneView(Camera* viewCamera, uint64_t skipEntityId) {
	ApplyRenderCamera(viewCamera);

	if (skybox_) {
		skybox_->Draw();
	}

	Object3dCommon::GetInstance()->SetCommonRenderState();

	if (lightManager_) {
		lightManager_->Bind(
			Object3dCommon::GetInstance()->GetDxCommon()->GetCommandList(),
			3
		);
	}

	if (shadowManager_) {
		shadowManager_->Bind(
			Object3dCommon::GetInstance()->GetDxCommon()->GetCommandList(),
			5,
			6
		);
	}

	if (plane_) {
		plane_->Draw();
	}
	if (axis) {
		axis->Draw();
	}
	if (SceneDocument* document = sceneManager_->GetActiveSceneDocument()) {
		const bool hidePlayerModel =
			ShouldHidePlayerModelForCamera(viewCamera);
		for (const SceneEntity& entity : document->GetEntities()) {
			if (
				entity.id == skipEntityId ||
				!IsEntityActiveInHierarchy(*document, entity) ||
				HasComponent(entity, "WaterVolume") ||
				(hidePlayerModel && HasComponent(entity, "PlayerBehavior"))
			) {
				continue;
			}
			const auto found = sceneModelObjects_.find(entity.id);
			if (found != sceneModelObjects_.end() && found->second.object) {
				found->second.object->Draw();
			}
		}
	}
	for (StageObject& stageObject : stageObjects_) {
		if (stageObject.object) {
			stageObject.object->Draw();
		}
	}
	if (SceneDocument* document = sceneManager_->GetActiveSceneDocument();
		!deferForegroundEffects_ && document) {
		DrawWaterSurfaces(*document, viewCamera, skipEntityId);
	}

	if (lightningRenderer_) {
		lightningRenderer_->Draw(viewCamera);
	}

	if (deferForegroundEffects_) {
		DrawRefractedEffectsForCamera(viewCamera, skipEntityId);
	} else {
		DrawForegroundEffectsForCamera(viewCamera, skipEntityId);
	}
}

void GamePlayScene::DrawForegroundEffectsForCamera(
	Camera* viewCamera,
	uint64_t skipEntityId
) {
	if (!viewCamera) {
		return;
	}

	if (SceneDocument* document = sceneManager_->GetActiveSceneDocument();
		deferForegroundEffects_ && document) {
		DrawWaterSurfaces(*document, viewCamera, skipEntityId);
	}

	ParticleManager* particleManager = ParticleManager::GetInstance();
	ParticleManager::WaterDrawFilter filter{};
	SceneDocument* particleDocument = sceneManager_
		? sceneManager_->GetActiveSceneDocument()
		: nullptr;
	if (
		deferForegroundEffects_ &&
		particleDocument &&
		TryBuildWaterParticleDrawFilter(
			*particleDocument,
			viewCamera,
			filter
		)
	) {
		filter.mode = ParticleManager::WaterDrawMode::kForeground;
		particleManager->RefreshCpuParticleInstancesForCamera(
			viewCamera,
			filter
		);
		particleManager->Draw(false);
	} else {
		particleManager->RefreshCpuParticleInstancesForCamera(viewCamera);
		particleManager->Draw();
	}

	SceneDocument* spriteDocument = sceneManager_->GetActiveSceneDocument();
	if (!sceneSpriteObjects_.empty() && spriteDocument) {
		SpriteCommon::GetInstance()->SetCommonRenderState();
		for (const SceneEntity& entity : spriteDocument->GetEntities()) {
			const auto found = sceneSpriteObjects_.find(entity.id);
			if (
				entity.id != skipEntityId &&
				found != sceneSpriteObjects_.end() &&
				IsEntityActiveInHierarchy(*spriteDocument, entity) &&
				found->second.sprite
			) {
				found->second.sprite->Draw();
			}
		}
	}
}

void GamePlayScene::DrawRefractedEffectsForCamera(
	Camera* viewCamera,
	uint64_t skipEntityId
) {
	(void)skipEntityId;
	if (!viewCamera) {
		return;
	}

	SceneDocument* document = sceneManager_
		? sceneManager_->GetActiveSceneDocument()
		: nullptr;
	if (!document) {
		return;
	}

	ParticleManager::WaterDrawFilter filter{};
	if (!TryBuildWaterParticleDrawFilter(*document, viewCamera, filter)) {
		return;
	}

	filter.mode = ParticleManager::WaterDrawMode::kRefracted;
	ParticleManager* particleManager = ParticleManager::GetInstance();
	particleManager->RefreshCpuParticleInstancesForCamera(
		viewCamera,
		filter
	);
	particleManager->Draw(true);
}

bool GamePlayScene::ShouldHidePlayerModelForCamera(Camera* viewCamera) const {
	return
		viewCamera == camera_ &&
		playerCameraController_.IsFirstPersonMode();
}

void GamePlayScene::DrawWaterSurfaces(
	const SceneDocument& document,
	Camera* viewCamera,
	uint64_t skipEntityId
) {
	if (
		!waterSurfaceRenderer_ ||
		!viewCamera
	) {
		return;
	}

	for (const SceneEntity& entity : document.GetEntities()) {
		if (
			entity.id == skipEntityId ||
			!IsEntityActiveInHierarchy(document, entity)
		) {
			continue;
		}

		const SceneComponent* waterVolume =
			FindEnabledComponent(entity, "WaterVolume");
		if (!waterVolume) {
			continue;
		}

		const Transform transform = ResolveScene3DTransform(document, entity);
		const Vector3 center{
			transform.translate.x + waterVolume->waterOffset.x,
			transform.translate.y + waterVolume->waterOffset.y,
			transform.translate.z + waterVolume->waterOffset.z
		};
		const Vector3 halfSize{
			(std::max)(waterVolume->waterHalfSize.x, 0.05f),
			(std::max)(waterVolume->waterHalfSize.y, 0.05f),
			(std::max)(waterVolume->waterHalfSize.z, 0.05f)
		};

		WaterSurfaceRenderer::Settings surfaceSettings{};
		surfaceSettings.enabled = waterVolume->waterSurfaceEnabled;
		surfaceSettings.baseColor = waterVolume->waterSurfaceBaseColor;
		surfaceSettings.highlightColor =
			waterVolume->waterSurfaceHighlightColor;
		surfaceSettings.alpha = waterVolume->waterSurfaceAlpha;
		surfaceSettings.waveScale = waterVolume->waterSurfaceWaveScale;
		surfaceSettings.normalStrength =
			waterVolume->waterSurfaceNormalStrength;
		surfaceSettings.fresnelPower = waterVolume->waterSurfaceFresnelPower;

		waterSurfaceRenderer_->Draw(
			viewCamera,
			center,
			halfSize,
			surfaceSettings
		);
	}
}

void GamePlayScene::RebuildStaticColliders() {
	staticColliders_.clear();
	if (SceneDocument* document = sceneManager_
		? sceneManager_->GetActiveSceneDocument()
		: nullptr) {
		for (const SceneEntity& entity : document->GetEntities()) {
			if (
				HasComponent(entity, "PlayerBehavior") ||
				!IsEntityActiveInHierarchy(*document, entity)
			) {
				continue;
			}
			const auto found = sceneModelObjects_.find(entity.id);
			if (
				found != sceneModelObjects_.end() &&
				found->second.hasCollider
			) {
				if (found->second.collider) {
					staticColliders_.push_back(found->second.collider);
				}
			}
		}
	}
	for (StageObject& stageObject : stageObjects_) {
		if (stageObject.object) {
			staticColliders_.push_back(&stageObject.collider);
		}
	}
}

void GamePlayScene::DrawColliderDebug() const {
	constexpr Vector4 kStaticColliderColor = { 0.2f, 0.95f, 0.7f, 1.0f };
	DebugRenderer* debugRenderer = DebugRenderer::GetInstance();
	auto drawCollider = [debugRenderer](
		const Collider* collider,
		const Vector4& color,
		const std::string& mode,
		uint32_t segments
	) {
		if (!collider) {
			return;
		}
		const bool drawWire = mode != "Solid";
		const bool drawSolid = mode != "Wireframe";
		Vector4 solidColor = color;
		solidColor.w = (std::min)(solidColor.w, 0.25f);
		if (collider->GetType() == Collider::Type::Sphere) {
			const auto& sphere = static_cast<const SphereCollider&>(*collider);
			if (drawSolid) {
				debugRenderer->AddSolidSphere(
					sphere.GetWorldCenter(), sphere.GetRadius(), solidColor, segments
				);
			}
			if (drawWire) {
				debugRenderer->AddSphere(
					sphere.GetWorldCenter(), sphere.GetRadius(), color, segments
				);
			}
			return;
		}
		const auto& box = static_cast<const OBBCollider&>(*collider);
		const OBBCollider::OBB obb = box.GetOBB();
		if (drawSolid) {
			debugRenderer->AddSolidOBB(
				obb.center, obb.axis, obb.halfSize, solidColor
			);
		}
		if (drawWire) {
			debugRenderer->AddOBB(obb.center, obb.axis, obb.halfSize, color);
		}
	};
	SceneDocument* document = sceneManager_
		? sceneManager_->GetActiveSceneDocument()
		: nullptr;
	if (document) {
		for (const SceneEntity& entity : document->GetEntities()) {
			const auto found = sceneModelObjects_.find(entity.id);
			if (
				found != sceneModelObjects_.end() &&
				found->second.hasCollider &&
				IsEntityActiveInHierarchy(*document, entity) &&
				found->second.colliderDebugVisible
			) {
				drawCollider(
					found->second.collider,
					found->second.colliderDebugColor,
					found->second.colliderDebugDrawMode,
					found->second.colliderDebugSegments
				);
			}
		}
	}
	for (const StageObject& stageObject : stageObjects_) {
		if (stageObject.object) {
			drawCollider(
				&stageObject.collider,
				kStaticColliderColor,
				"Wireframe",
				12
			);
		}
	}
}

void GamePlayScene::LoadSceneDebugSettings() {
	const SceneDocument* document = sceneManager_
		? sceneManager_->GetActiveSceneDocument()
		: nullptr;
	if (!document) {
		return;
	}

	const SceneDebugSettings& settings = document->GetDebugSettings();
	showCameraDebug_ = settings.showCameraDirection;
	showColliderDebug_ = settings.showColliders;
	showCameraPathDebug_ = settings.showCameraPath;
	showCameraPathPointCameraDebug_ =
		settings.showCameraPathPointCameraDirection;
	showSkeletonDebug_ = settings.showSkeleton;
	showJointNames_ = settings.showJointNames;
	showJointAxes_ = settings.showJointAxes;
	jointRadius_ = settings.jointRadius;
	jointAxisLength_ = settings.jointAxisLength;
}

void GamePlayScene::SaveSceneDebugSettings() {
	SceneDocument* document = sceneManager_
		? sceneManager_->GetActiveSceneDocument()
		: nullptr;
	if (!document) {
		return;
	}

	SceneDebugSettings settings = document->GetDebugSettings();
	settings.showCameraDirection = showCameraDebug_;
	settings.showColliders = showColliderDebug_;
	settings.showCameraPath = showCameraPathDebug_;
	settings.showCameraPathPointCameraDirection =
		showCameraPathPointCameraDebug_;
	settings.showSkeleton = showSkeletonDebug_;
	settings.showJointNames = showJointNames_;
	settings.showJointAxes = showJointAxes_;
	settings.jointRadius = jointRadius_;
	settings.jointAxisLength = jointAxisLength_;
	document->SetDebugSettings(settings);
}

void GamePlayScene::Initialize()
{
	// ゲーム固有の初期化
	LoadSceneDebugSettings();

	camera_ = new Camera();
	camera_->SetOrbitMode(true);
	camera_->SetOrbitTarget({ 0.0f, 0.0f, 0.0f });
	camera_->SetOrbitDistance(10.0f);
	camera_->SetOrbitAngle(0.0f, 0.0f);
	camera_->Update();
	debugCamera_ = new Camera();
	debugCamera_->SetOrbitMode(true);
	debugCamera_->SetOrbitTarget({ 0.0f, 0.0f, 0.0f });
	debugCamera_->SetOrbitDistance(10.0f);
	debugCamera_->SetOrbitAngle(0.0f, 0.0f);
	debugCamera_->Update();

	Object3dCommon::GetInstance()->SetDefaultCamera(camera_);
	ParticleManager* particleManager = ParticleManager::GetInstance();
	particleManager->SetCamera(camera_);
	particleManager->SetGpuParticleEnabled(true);
	particleManager->ClearGpuParticles();
	TextureManager::GetInstance()->LoadTexture("resources/monsterBall.png");
	TextureManager::GetInstance()->LoadTexture("resources/uvChecker.png");
	ModelManager::GetInstance()->LoadModel("terrain.obj");
	ModelManager::GetInstance()->LoadModel("Cube.obj");
	ModelManager::GetInstance()->LoadModel("AnimatedCube/AnimatedCube.gltf");
	ModelManager::GetInstance()->LoadModel("human/walk.gltf");
	if (SceneDocument* document = sceneManager_
		? sceneManager_->GetActiveSceneDocument()
		: nullptr) {
		for (const SceneEntity& entity : document->GetEntities()) {
			const SceneComponent* meshRenderer =
				FindEnabledComponent(entity, "MeshRenderer");
			const std::string modelPath = meshRenderer
				? meshRenderer->modelPath
				: entity.modelPath;
			if (!modelPath.empty()) {
				ModelManager::GetInstance()->LoadModel(modelPath);
			}
		}
	}

	ParticleEffectDesc gameplayEffect{};

	/*if (ParticleEffectResource::Load(
		"resources/particles/fairyParticle.json",
		gameplayEffect
	)) {
		emitter_ = ParticleEffectResource::CreateEmitter(gameplayEffect);
	}*/

	if (ParticleEffectResource::Load(
		"resources/particles/core_burst.json",
		planeBurstEffect_
	)) {
		planeBurstEmitter_ =
			ParticleEffectResource::CreateEmitter(planeBurstEffect_);

		editingEffect_ = planeBurstEffect_;
		particleEffectEditor_.Initialize(
			editingEffect_,
			"resources/particles/core_burst.json"
		);
		editorPreviewEmitter_ =
			ParticleEffectResource::CreateEmitter(editingEffect_);
	}

	if (ParticleEffectResource::Load(
		"resources/particles/ring_burst.json",
		ringBurstEffect_
	)) {
		ringBurstEmitter_ =
			ParticleEffectResource::CreateEmitter(ringBurstEffect_);
	}

	SyncSceneModelObjects();

	Vector3 target = GetSceneTransform(
		sceneManager_,
		"Human",
		Transform{
			{ 1.0f, 1.0f, 1.0f },
			{ 0.0f, 0.0f, 0.0f },
			{ -2.0f, 0.0f, -2.0f }
		}
	).translate;
	camera_->SetOrbitTarget(target);

	player_ = new Player();
	player_->Initialize(FindSceneModelObjectByName("Player"));
	player_->SetTransform(GetSceneTransform(
		sceneManager_,
		"Player",
		Transform{
			{ 1.0f, 1.0f, 1.0f },
			{ 0.0f, 0.0f, 0.0f },
			{ 0.0f, 1.0f, -4.0f }
		}
	));

	auto addStageObject =
		[this](
			const char* modelName,
			const Vector3& translate,
			const Vector3& scale,
			const Vector3& halfSize,
			bool collidable
		) {
			StageObject stageObject{};
			stageObject.object = new Object3d();
			stageObject.object->Initialize(Object3dCommon::GetInstance());
			stageObject.object->SetModel(modelName);
			stageObject.object->SetTranslate(translate);
			stageObject.object->SetScale(scale);
			stageObject.object->Update();

			stageObject.collider.SetWorldTransform(&stageObject.object->GetTransform());
			stageObject.collider.SetHalfSize(halfSize);

			stageObjects_.push_back(stageObject);
			(void)collidable;
		};

	stageObjects_.clear();
	stageObjects_.reserve(4);
	//addStageObject("Cube.obj", { 0.0f, -0.1f, 0.0f }, { 5.0f, 0.1f, 5.0f }, { 5.0f, 0.1f, 5.0f }, false);
	//addStageObject("Cube.obj", { 0.0f, 2.0f, 5.0f }, { 5.0f, 2.0f, 0.2f }, { 5.0f, 2.0f, 0.2f }, true);
	//addStageObject("Cube.obj", { -5.0f, 2.0f, 0.0f }, { 0.2f, 2.0f, 5.0f }, { 0.2f, 2.0f, 5.0f }, true);
	//addStageObject("Cube.obj", { 3.0f, 0.5f, -2.0f }, { 1.0f, 0.5f, 1.0f }, { 1.0f, 0.5f, 1.0f }, true);

	SyncEnvironmentComponent();

	lightManager_ = std::make_unique<LightManager>();
	lightManager_->Initialize(
		Object3dCommon::GetInstance()->GetDxCommon(),
		"resources/lights/gameplay_lights.json"
	);

	shadowManager_ = std::make_unique<ShadowManager>();
	shadowManager_->Initialize(
		Object3dCommon::GetInstance()->GetDxCommon(),
		SrvManager::GetInstance(),
		lightManager_ ? lightManager_->GetShadowMapSize() : 2048u
	);

	lightningRenderer_ = std::make_unique<LightningRenderer>();
	lightningRenderer_->Initialize(Object3dCommon::GetInstance()->GetDxCommon());

	waterSurfaceRenderer_ = std::make_unique<WaterSurfaceRenderer>();
	waterSurfaceRenderer_->Initialize(
		Object3dCommon::GetInstance()->GetDxCommon()
	);

	//soundData_ = audio_->SoundLoadWave("resources/fanfare.wav");
}

void GamePlayScene::Update()
{
	EditorSession* editorSession = sceneManager_
		? sceneManager_->GetEditorSession()
		: nullptr;
	if (editorSession && editorSession->IsEditing() && player_) {
		const SceneEntity* playerEntity = FindSceneEntity(
			sceneManager_,
			"Player"
		);
		if (playerEntity) {
			player_->SetTransform(playerEntity->transform);
		}
	}

	std::string particlePath;
	if (ImGuiManager::GetInstance() && ImGuiManager::GetInstance()->GetRequestLoadParticle(particlePath)) {
		ParticleEffectDesc loadedEffect{};
		if (ParticleEffectResource::Load(particlePath, loadedEffect)) {
			editingEffect_ = loadedEffect;
			particleEffectEditor_.Initialize(
				editingEffect_,
				particlePath
			);
			delete editorPreviewEmitter_;
			editorPreviewEmitter_ = ParticleEffectResource::CreateEmitter(editingEffect_);
		}
	}

	if (
		editorSession &&
		editorSession->IsEditing() &&
		Input::GetInstance()->TriggerKey(DIK_SPACE)
	) {
		ParticleManager::GetInstance()->CycleSceneParticleAssets("GAMEPLAY");
	}

	if (emitter_) {
		emitter_->Update();
	}
	if (editorPreviewEmitter_) {
		editorPreviewEmitter_->Update();
	}
	if (planeBurstEmitter_) {
		planeBurstEmitter_->Update();
	}
	if (ringBurstEmitter_) {
		ringBurstEmitter_->Update();
	}
	ParticleManager::GetInstance()->UpdateSceneParticles("GAMEPLAY");
	ParticleManager::GetInstance()->Update();
	if (lightningRenderer_) {
		ParticleManager::LightningEvent lightningEvent{};
		while (ParticleManager::GetInstance()->ConsumeLightningEvent(lightningEvent)) {
			LightningRenderer::Settings settings = lightningRenderer_->GetSettings();
			settings.start = lightningEvent.start;
			settings.end = lightningEvent.end;
			settings.coreColor = lightningEvent.desc.coreColor;
			settings.branchColor = lightningEvent.desc.branchColor;
			settings.jitter = lightningEvent.desc.jitter;
			settings.branchLength = lightningEvent.desc.branchLength;
			settings.branchProbability = lightningEvent.desc.branchProbability;
			settings.thickness = lightningEvent.desc.thickness;
			settings.duration = lightningEvent.desc.duration;
			settings.segmentCount = lightningEvent.desc.segmentCount;
			settings.seed = lightningEvent.seed;
			lightningRenderer_->Trigger(settings);
		}
		lightningRenderer_->Update(1.0f / 60.0f);
	}
	if (waterSurfaceRenderer_) {
		waterSurfaceRenderer_->Update(1.0f / 60.0f);
	}

#if defined(_DEBUG) || defined(DEVELOPMENT)
	ImGui::Begin("Scene Controls");
	Object3d* animatedCube = FindSceneModelObjectByName("Animated Cube");
	if (animatedCube && animatedCube->HasAnimation()) {
		bool isPlaying = animatedCube->IsAnimationPlaying();
		if (ImGui::Checkbox("Play Animation", &isPlaying)) {
			animatedCube->SetAnimationPlaying(isPlaying);
		}

		bool isLooping = animatedCube->IsAnimationLooping();
		if (ImGui::Checkbox("Loop Animation", &isLooping)) {
			animatedCube->SetAnimationLoop(isLooping);
		}

		float animationSpeed = animatedCube->GetAnimationSpeed();
		if (ImGui::DragFloat(
			"Animation Speed",
			&animationSpeed,
			0.01f,
			-4.0f,
			4.0f
		)) {
			animatedCube->SetAnimationSpeed(animationSpeed);
		}

		if (ImGui::Button("Reset Animation")) {
			animatedCube->ResetAnimation();
		}

		const float duration = animatedCube->GetAnimationDuration();
		const float progress = duration > 0.0f
			? animatedCube->GetAnimationTime() / duration
			: 0.0f;
		ImGui::ProgressBar(progress, ImVec2(-1.0f, 0.0f));
	}
	ImGui::SeparatorText("Debug Draw");
	bool debugSettingsChanged = false;
	debugSettingsChanged |= ImGui::Checkbox(
		"Show Camera Direction",
		&showCameraDebug_
	);
	debugSettingsChanged |= ImGui::Checkbox(
		"Show Colliders",
		&showColliderDebug_
	);
	debugSettingsChanged |= ImGui::Checkbox(
		"Show CameraPath",
		&showCameraPathDebug_
	);
	debugSettingsChanged |= ImGui::Checkbox(
		"Show CameraPath Point Camera Direction",
		&showCameraPathPointCameraDebug_
	);
	ImGui::SeparatorText("Skeleton");
	debugSettingsChanged |= ImGui::Checkbox("Show Skeleton", &showSkeletonDebug_);
	if (showSkeletonDebug_) {
		debugSettingsChanged |= ImGui::Checkbox(
			"Show Joint Names",
			&showJointNames_
		);
		debugSettingsChanged |= ImGui::Checkbox(
			"Show Joint Axes",
			&showJointAxes_
		);
		debugSettingsChanged |= ImGui::DragFloat(
			"Joint Radius",
			&jointRadius_,
			0.001f,
			0.002f,
			0.1f
		);
		debugSettingsChanged |= ImGui::DragFloat(
			"Joint Axis Length",
			&jointAxisLength_,
			0.002f,
			0.01f,
			0.5f
		);
	}
	if (debugSettingsChanged) {
		SaveSceneDebugSettings();
	}
	Object3d* human = FindSceneModelObjectByName("Human");
	if (human && human->GetSkeleton()) {
		bool isPlaying = human->IsAnimationPlaying();
		if (ImGui::Checkbox("Play Skeleton Animation", &isPlaying)) {
			human->SetAnimationPlaying(isPlaying);
		}

		bool isLooping = human->IsAnimationLooping();
		if (ImGui::Checkbox("Loop Skeleton Animation", &isLooping)) {
			human->SetAnimationLoop(isLooping);
		}

		float animationSpeed = human->GetAnimationSpeed();
		if (ImGui::DragFloat(
			"Skeleton Animation Speed",
			&animationSpeed,
			0.01f,
			-4.0f,
			4.0f
		)) {
			human->SetAnimationSpeed(animationSpeed);
		}

		if (ImGui::Button("Reset Skeleton Animation")) {
			human->ResetAnimation();
		}

		ImGui::Text(
			"Joints: %zu",
			human->GetSkeleton()->joints.size()
		);

		const float duration = human->GetAnimationDuration();
		const float progress = duration > 0.0f
			? human->GetAnimationTime() / duration
			: 0.0f;
		ImGui::ProgressBar(progress, ImVec2(-1.0f, 0.0f));
	}
	ImGui::End();

	DrawMonitorDebugWindow();

	if (lightManager_) {
		lightManager_->DrawImGui();
	}

	if (const auto generatedSkybox = starFieldGenerator_.DrawImGui("Environment")) {
		if (TextureManager::GetInstance()->ReloadTexture(*generatedSkybox)) {
			if (SceneDocument* document = sceneManager_
				? sceneManager_->GetActiveSceneDocument()
				: nullptr) {
				SceneComponent* targetEnvironment = nullptr;
				for (SceneEntity& entity : document->GetEntities()) {
					SceneComponent* environment =
						FindComponent(entity, "Environment");
					if (!environment) {
						continue;
					}
					targetEnvironment = environment;
					break;
				}
				if (!targetEnvironment) {
					SceneEntity& environmentEntity =
						document->CreateEntity("Environment");
					environmentEntity.components.push_back(SceneComponent{
						"Environment",
						true
					});
					targetEnvironment = &environmentEntity.components.back();
				}
				targetEnvironment->environmentSkyboxEnabled = true;
				targetEnvironment->environmentSkyboxPath =
					EditableResourcePath::ToProjectRelative(
						std::filesystem::path(*generatedSkybox)
					).generic_string();
				document->MarkDirty();
			}
			SyncEnvironmentComponent();
		}
	}

	particleEffectEditor_.DrawImGui(
		editingEffect_,
		editorPreviewEmitter_,
		"Particle Effect Editor"
	);

	ParticleManager::GetInstance()->DrawSceneParticleImGui("GAMEPLAY", "Scene Particles");
	if (lightningRenderer_) {
		lightningRenderer_->DrawImGui("Lightning");
	}

	if (editingEffect_.name == planeBurstEffect_.name && planeBurstEmitter_) {
		planeBurstEffect_ = editingEffect_;
		ParticleEffectResource::PrepareParticleGroup(planeBurstEffect_, false);
		ParticleEffectResource::ApplyToEmitter(*planeBurstEmitter_, planeBurstEffect_);
	}
	else if (editingEffect_.name == ringBurstEffect_.name && ringBurstEmitter_) {
		ringBurstEffect_ = editingEffect_;
		ParticleEffectResource::PrepareParticleGroup(ringBurstEffect_, false);
		ParticleEffectResource::ApplyToEmitter(*ringBurstEmitter_, ringBurstEffect_);
	}
#endif

	for (Sprite* sprite : sprites_) {
		sprite->Update();
	}

	if (plane_) {
		plane_->Update();
	}
	if (axis) {
		axis->Update();
	}
	SyncSceneModelObjects();
	SyncEnvironmentComponent();
	RebuildStaticColliders();
	SceneDocument* activeDocument = sceneManager_
		? sceneManager_->GetActiveSceneDocument()
		: nullptr;
	if (activeDocument) {
		ApplyPlayerBehaviorComponent(*activeDocument);
		ApplyPlayerPhysicsComponent(*activeDocument);
		ApplyWaterVolumes(*activeDocument);
		if (!editorSession || !editorSession->IsEditing()) {
			UpdateAgentBehaviors(*activeDocument, 1.0f / 60.0f);
		}
	}
	if (
		editorSession &&
		!editorSession->IsEditing() &&
		sceneManager_
	) {
		debugCameraInitialized_ = false;
		if (activeDocument) {
			if (cameraPathRuntime_.IsPlaying()) {
				cameraPathRuntime_.Update(1.0f / 60.0f, *camera_);
				if (cameraPathRuntime_.ConsumeFinishedThisFrame()) {
					SyncPlayerCameraControllerFromCurrentCamera(*activeDocument);
				}
			} else {
				ApplyMainCameraComponent(*activeDocument, camera_);
				ApplyPlayerCameraMouseLook(*activeDocument);
				TryStartCameraPath(*activeDocument);
				if (cameraPathRuntime_.IsPlaying()) {
					cameraPathRuntime_.Update(1.0f / 60.0f, *camera_);
					if (cameraPathRuntime_.ConsumeFinishedThisFrame()) {
						SyncPlayerCameraControllerFromCurrentCamera(
							*activeDocument
						);
					}
				}
			}
			camera_->Update();
		}
	}
	if (player_ && (!editorSession || editorSession->IsPlaying())) {
		player_->Update(camera_);
	}
	StepPhysics(1.0f / 60.0f);
	if (player_ && (!editorSession || editorSession->IsPlaying())) {
		player_->PostPhysicsUpdate();
		SceneDocument* document = sceneManager_->GetActiveSceneDocument();
		SceneEntity* playerEntity = document
			? document->FindEntityByName("Player")
			: nullptr;
		if (playerEntity && player_->GetObject()) {
			playerEntity->transform = player_->GetObject()->GetTransform();
		}
	}
	SyncSceneSpriteObjects();
	EditorSession* activeEditorSession = sceneManager_
		? sceneManager_->GetEditorSession()
		: nullptr;
	if (
		activeEditorSession &&
		!activeEditorSession->IsEditing()
	) {
		if (SceneDocument* document = sceneManager_
			? sceneManager_->GetActiveSceneDocument()
			: nullptr) {
			if (!cameraPathRuntime_.IsPlaying()) {
				ApplyMainCameraComponent(*document, camera_);
				ApplyPlayerCameraMouseLook(*document);
			}
		}
	}
	camera_->Update();

#if defined(_DEBUG) || defined(DEVELOPMENT)
	if (showCameraDebug_) {
		if (SceneDocument* document = sceneManager_
			? sceneManager_->GetActiveSceneDocument()
			: nullptr) {
			const float aspectRatio = camera_
				? camera_->GetAspectRatio()
				: 1.0f;
			for (const SceneEntity& entity : document->GetEntities()) {
				if (const SceneComponent* cameraComponent =
					FindEnabledComponent(entity, "Camera")) {
					const SceneComponent* thirdPersonCamera =
						FindEnabledComponent(entity, "ThirdPersonCamera");
					if (
						thirdPersonCamera &&
						activeEditorSession &&
						activeEditorSession->IsPlaying()
					) {
						continue;
					}
					const bool useRuntimeCamera =
						camera_ &&
						thirdPersonCamera &&
						HasComponent(entity, "PlayerBehavior") &&
						activeEditorSession &&
						!activeEditorSession->IsEditing();
					if (useRuntimeCamera) {
						AddCameraDebugDraw(
							*document,
							entity,
							*cameraComponent,
							aspectRatio,
							nullptr,
							camera_
						);
						AddThirdPersonCameraDebugDraw(
							*document,
							entity,
							*thirdPersonCamera,
							camera_->GetTranslate()
						);
					} else {
						AddCameraDebugDraw(
							*document,
							entity,
							*cameraComponent,
							aspectRatio
						);
					}
				}
			}
		}
	}
	if (showCameraPathDebug_ || showCameraPathPointCameraDebug_) {
		if (SceneDocument* document = sceneManager_
			? sceneManager_->GetActiveSceneDocument()
			: nullptr) {
			DrawCameraPathDebug(
				*document,
				showCameraPathDebug_,
				showCameraPathPointCameraDebug_
			);
		}
	}
	if (showColliderDebug_) {
		DrawColliderDebug();
	}
#endif

	for (StageObject& stageObject : stageObjects_) {
		if (stageObject.object) {
			stageObject.object->Update();
		}
	}
	SyncEnvironmentComponent();
	if (skybox_) {
		skybox_->Update();
	}

#if defined(_DEBUG) || defined(DEVELOPMENT)
	if (showSkeletonDebug_) {
		if (Object3d* human = FindSceneModelObjectByName("Human")) {
			human->DrawSkeletonDebug(
			showJointNames_,
			showJointAxes_,
			jointRadius_,
			jointAxisLength_
			);
		}
	}
#endif
}

void GamePlayScene::UpdatePaused()
{
	InitializePauseDebugCamera();
#if defined(_DEBUG) || defined(DEVELOPMENT)
	ImGui::Begin("Scene Controls");
	ImGui::TextDisabled("Paused Debug View");
	bool debugSettingsChanged = false;
	debugSettingsChanged |= ImGui::Checkbox(
		"Show Camera Direction",
		&showCameraDebug_
	);
	debugSettingsChanged |= ImGui::Checkbox(
		"Show Colliders",
		&showColliderDebug_
	);
	debugSettingsChanged |= ImGui::Checkbox(
		"Show CameraPath",
		&showCameraPathDebug_
	);
	debugSettingsChanged |= ImGui::Checkbox(
		"Show CameraPath Point Camera Direction",
		&showCameraPathPointCameraDebug_
	);
	if (debugSettingsChanged) {
		SaveSceneDebugSettings();
	}
	ImGui::End();
	DrawMonitorDebugWindow();
#endif

	if (debugCamera_) {
		debugCamera_->Update();
	}

#if defined(_DEBUG) || defined(DEVELOPMENT)
	if (showCameraDebug_) {
		if (SceneDocument* document = sceneManager_
			? sceneManager_->GetActiveSceneDocument()
			: nullptr) {
			const float aspectRatio = camera_
				? camera_->GetAspectRatio()
				: 1.0f;
			for (const SceneEntity& entity : document->GetEntities()) {
				if (const SceneComponent* cameraComponent =
					FindEnabledComponent(entity, "Camera")) {
					const SceneComponent* thirdPersonCamera =
						FindEnabledComponent(entity, "ThirdPersonCamera");
					const bool useRuntimeCamera =
						camera_ &&
						thirdPersonCamera &&
						HasComponent(entity, "PlayerBehavior");
					if (useRuntimeCamera) {
						AddCameraDebugDraw(
							*document,
							entity,
							*cameraComponent,
							aspectRatio,
							nullptr,
							camera_
						);
						AddThirdPersonCameraDebugDraw(
							*document,
							entity,
							*thirdPersonCamera,
							camera_->GetTranslate()
						);
					} else {
						AddCameraDebugDraw(
							*document,
							entity,
							*cameraComponent,
							aspectRatio
						);
					}
				}
			}
		}
	}
	if (showCameraPathDebug_ || showCameraPathPointCameraDebug_) {
		if (SceneDocument* document = sceneManager_
			? sceneManager_->GetActiveSceneDocument()
			: nullptr) {
			DrawCameraPathDebug(
				*document,
				showCameraPathDebug_,
				showCameraPathPointCameraDebug_
			);
		}
	}
	if (showColliderDebug_) {
		DrawColliderDebug();
	}
#endif
}

void GamePlayScene::Draw()
{
	Camera* viewCamera = GetSceneViewCamera();
	DrawSceneView(viewCamera);
}

void GamePlayScene::DrawForegroundEffects()
{
	Camera* viewCamera = GetSceneViewCamera();
	ApplyRenderCamera(viewCamera);
	DrawForegroundEffectsForCamera(viewCamera);
}

void GamePlayScene::DrawOffscreenViews()
{
	++monitorDebugFrame_;
	SyncMonitorRenderers();

	SceneDocument* document = sceneManager_
		? sceneManager_->GetActiveSceneDocument()
		: nullptr;
	if (!document) {
		return;
	}

	for (auto& [monitorEntityId, runtime] : monitorRuntimes_) {
		const SceneEntity* monitorEntity = document->FindEntity(monitorEntityId);
		const auto monitorObject = sceneModelObjects_.find(monitorEntityId);
		runtime.debugPassFrame = monitorDebugFrame_;
		runtime.debugResolvedCamera = false;
		runtime.debugRendered = false;
		runtime.debugTextureApplied = false;
		runtime.debugTargetCameraId = 0;
		runtime.debugTargetCameraName.clear();
		runtime.debugTargetTranslate = {};
		runtime.debugTargetRotate = {};
		runtime.debugTargetIsMain = false;
		runtime.debugTargetHasPlayerBehavior = false;
		runtime.debugSrvPtr = runtime.renderTarget
			? runtime.renderTarget->GetSrvGpuHandle().ptr
			: 0;
		runtime.debugAppliedTextureOverridePtr = monitorObject !=
			sceneModelObjects_.end() &&
			monitorObject->second.object
				? monitorObject->second.object->GetTextureOverridePtr()
				: 0;
		runtime.debugStatus = "Pending";
		if (
			!monitorEntity ||
			!IsEntityActiveInHierarchy(*document, *monitorEntity) ||
			!runtime.camera ||
			!runtime.renderTarget ||
			monitorObject == sceneModelObjects_.end() ||
			!monitorObject->second.object
		) {
			if (!monitorEntity) {
				runtime.debugStatus = "Monitor entity missing";
			} else if (!IsEntityActiveInHierarchy(*document, *monitorEntity)) {
				runtime.debugStatus = "Monitor entity inactive";
			} else if (!runtime.camera) {
				runtime.debugStatus = "Runtime camera missing";
			} else if (!runtime.renderTarget) {
				runtime.debugStatus = "Render target missing";
			} else {
				runtime.debugStatus = "Monitor mesh object missing";
			}
			if (monitorObject != sceneModelObjects_.end() && monitorObject->second.object) {
				monitorObject->second.object->ClearTextureOverride();
			}
			runtime.debugAppliedTextureOverridePtr = 0;
			continue;
		}

		const SceneComponent* monitorRenderer =
			FindEnabledComponent(*monitorEntity, "MonitorRenderer");
		if (monitorRenderer) {
			runtime.debugTargetCameraId =
				monitorRenderer->monitorCameraEntityId;
			runtime.debugTargetCameraName =
				monitorRenderer->monitorCameraName;
		}
		const SceneEntity* cameraEntity = nullptr;
		const SceneComponent* cameraComponent = nullptr;
		if (
			!monitorRenderer ||
			!HasMonitorCameraBinding(*monitorRenderer) ||
			!ResolveMonitorTargetCamera(
				*document,
				*monitorRenderer,
				cameraEntity,
				cameraComponent
			)
		) {
			if (!monitorRenderer) {
				runtime.debugStatus = "MonitorRenderer component missing";
			} else if (!HasMonitorCameraBinding(*monitorRenderer)) {
				runtime.debugStatus = "Target camera not assigned";
			} else {
				runtime.debugStatus = "Target camera unresolved";
			}
			monitorObject->second.object->ClearTextureOverride();
			runtime.debugAppliedTextureOverridePtr = 0;
			continue;
		}
		runtime.debugResolvedCamera = true;
		runtime.debugTargetCameraId = cameraEntity->id;
		runtime.debugTargetCameraName = cameraEntity->name;
		const Transform targetCameraTransform =
			ResolveScene3DTransform(*document, *cameraEntity);
		runtime.debugTargetTranslate = targetCameraTransform.translate;
		runtime.debugTargetRotate = targetCameraTransform.rotate;
		runtime.debugTargetIsMain = cameraComponent->cameraIsMain;
		runtime.debugTargetHasPlayerBehavior =
			HasComponent(*cameraEntity, "PlayerBehavior");

		const float aspectRatio =
			static_cast<float>(runtime.width) /
			static_cast<float>((std::max)(runtime.height, 1u));
		if (!ApplyCameraComponentToCamera(
			*document,
			*cameraEntity,
			*cameraComponent,
			runtime.camera,
			aspectRatio
		)) {
			runtime.debugStatus = "Failed to apply camera component";
			monitorObject->second.object->ClearTextureOverride();
			runtime.debugAppliedTextureOverridePtr = 0;
			continue;
		}
		if (monitorDebugForceProbeCamera_) {
			runtime.camera->SetTranslate({ 0.0f, 80.0f, -80.0f });
			runtime.camera->SetRotate({ 0.75f, 0.0f, 0.0f });
			runtime.camera->SetFovY(0.12f);
			runtime.camera->Update();
		}

		runtime.renderTarget->Begin();
		SrvManager::GetInstance()->PreDraw();
		DrawSceneView(
			runtime.camera,
			runtime.hideSelf ? monitorEntityId : 0
		);
		runtime.renderTarget->End();

		runtime.debugSrvPtr = runtime.renderTarget->GetSrvGpuHandle().ptr;
		runtime.debugRendered = true;
		runtime.debugTextureApplied = runtime.debugSrvPtr != 0;
		runtime.debugStatus =
			"Rendered from #" +
			std::to_string(cameraEntity->id) +
			" " +
			cameraEntity->name;
		if (monitorDebugForceProbeCamera_) {
			runtime.debugStatus += " (probe camera forced)";
		}
		monitorObject->second.object->SetTextureOverride(
			runtime.renderTarget->GetSrvGpuHandle()
		);
		runtime.debugAppliedTextureOverridePtr =
			monitorObject->second.object->GetTextureOverridePtr();
	}

	ApplyRenderCamera(GetSceneViewCamera());
}

void GamePlayScene::DrawShadow()
{
	if (!lightManager_ || !shadowManager_) {
		return;
	}
	shadowManager_->SetShadowMapSize(lightManager_->GetShadowMapSize());

	std::vector<Object3d*> shadowCasters;
	shadowCasters.reserve(
		stageObjects_.size() + sceneModelObjects_.size() +
		(player_ ? 1 : 0)
	);
	//if (plane_) {
	//	shadowCasters.push_back(plane_);
	//}
	//if (axis) {
	//	shadowCasters.push_back(axis);
	//}
	//if (player_) {
	//	shadowCasters.push_back(player_->GetObject());
	//}
	const bool hidePlayerModel = ShouldHidePlayerModelForCamera(camera_);
	for (StageObject& stageObject : stageObjects_) {
		if (stageObject.object) {
			shadowCasters.push_back(stageObject.object);
		}
	}
	if (SceneDocument* document = sceneManager_->GetActiveSceneDocument()) {
		for (const SceneEntity& entity : document->GetEntities()) {
			if (
				!IsEntityActiveInHierarchy(*document, entity) ||
				HasComponent(entity, "WaterVolume") ||
				(hidePlayerModel && HasComponent(entity, "PlayerBehavior"))
			) {
				continue;
			}
			const auto found = sceneModelObjects_.find(entity.id);
			if (found != sceneModelObjects_.end() && found->second.object) {
				shadowCasters.push_back(found->second.object);
			}
		}
	}
	if (!shadowCasters.empty()) {
		shadowManager_->Render(
			*lightManager_,
			shadowCasters.data(),
			static_cast<uint32_t>(shadowCasters.size())
		);
	}
}

void GamePlayScene::Finalize()
{
	ClearMonitorRenderers();
	ClearSceneModelObjects();
	ClearSceneSpriteObjects();
	for (Sprite* sprite : sprites_) {
		delete sprite;
	}
	sprites_.clear();

	delete plane_;
	plane_ = nullptr;

	delete axis;
	axis = nullptr;

	if (player_) {
		player_->Finalize();
		delete player_;
		player_ = nullptr;
	}

	for (StageObject& stageObject : stageObjects_) {
		delete stageObject.object;
		stageObject.object = nullptr;
	}
	stageObjects_.clear();
	staticColliders_.clear();

	delete emitter_;
	emitter_ = nullptr;

	delete editorPreviewEmitter_;
	editorPreviewEmitter_ = nullptr;

	delete planeBurstEmitter_;
	planeBurstEmitter_ = nullptr;

	delete ringBurstEmitter_;
	ringBurstEmitter_ = nullptr;

	if (lightningRenderer_) {
		lightningRenderer_->Finalize();
		lightningRenderer_.reset();
	}
	if (waterSurfaceRenderer_) {
		waterSurfaceRenderer_->Finalize();
		waterSurfaceRenderer_.reset();
	}

	delete camera_;
	camera_ = nullptr;

	delete debugCamera_;
	debugCamera_ = nullptr;
	debugCameraInitialized_ = false;

	delete skybox_;
	skybox_ = nullptr;
}
