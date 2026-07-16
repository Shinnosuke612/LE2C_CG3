// 役割: SceneのCamera ComponentとRuntimeカメラ制御の状態を同期する。
#include "SceneCameraSystem.h"

#include "../../../engine/3d/Camera.h"
#include "../../../engine/3d/Object3d.h"
#include "../../../engine/collision/Collider.h"
#include "../../../engine/collision/OBBCollider.h"
#include "../../../engine/io/Input.h"
#include "../../../engine/math/Math.h"
#include "../../../engine/math/Matrix4x4.h"
#include "../../../engine/scene/SceneDocument.h"
#include "../../../engine/scene/SceneEntityQuery.h"
#include "../../../engine/scene/SceneTransformResolver.h"
#include "../../player/Player.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <string>

namespace {
	using SceneEntityQuery::FindEnabledComponent;
	using SceneEntityQuery::HasComponent;
	using SceneEntityQuery::IsEntityActiveInHierarchy;

	constexpr float kMinimumFov = 0.0174532925f;
	constexpr float kMaximumFov = 3.12413936f;
}

void SceneCameraSystem::Reset() {
	cameraPathRuntime_.Stop();
	playerCameraController_ = ThirdPersonCameraController{};
	playerCameraInitialized_ = false;
	debugCameraInitialized_ = false;
}

void SceneCameraSystem::UpdateBeforeSimulation(
	SceneDocument& document,
	Camera* camera,
	Player* player,
	const std::vector<SceneRuntimeObjectBinding>& bindings,
	float deltaTime,
	bool runtimeActive,
	bool playing
) {
	if (!camera || !runtimeActive) {
		return;
	}
	debugCameraInitialized_ = false;

	// CameraPathを優先し、終了したフレームだけPlayer追従へ制御を戻す。
	if (cameraPathRuntime_.IsPlaying()) {
		cameraPathRuntime_.Update(deltaTime, *camera);
		if (cameraPathRuntime_.ConsumeFinishedThisFrame()) {
			SyncPlayerController(document, camera, player);
		}
	} else {
		ApplyMainCamera(document, camera);
		UpdateThirdPersonCamera(document, camera, player, bindings, playing);
		TryStartCameraPath(document, camera);
		if (cameraPathRuntime_.IsPlaying()) {
			cameraPathRuntime_.Update(deltaTime, *camera);
			if (cameraPathRuntime_.ConsumeFinishedThisFrame()) {
				SyncPlayerController(document, camera, player);
			}
		}
	}
	camera->Update();
}

void SceneCameraSystem::UpdateAfterSimulation(
	SceneDocument& document,
	Camera* camera,
	Player* player,
	const std::vector<SceneRuntimeObjectBinding>& bindings,
	bool runtimeActive,
	bool playing
) {
	if (!camera) {
		return;
	}
	if (runtimeActive && !cameraPathRuntime_.IsPlaying()) {
		ApplyMainCamera(document, camera);
		UpdateThirdPersonCamera(document, camera, player, bindings, playing);
	}
	camera->Update();
}

void SceneCameraSystem::UpdatePaused(Camera* camera, Camera* debugCamera) {
	InitializePauseDebugCamera(camera, debugCamera);
	if (debugCamera) {
		debugCamera->Update();
	}
}

Camera* SceneCameraSystem::SelectSceneViewCamera(
	Camera* camera,
	Camera* debugCamera,
	bool paused
) const {
	return paused && debugCamera ? debugCamera : camera;
}

bool SceneCameraSystem::ApplyComponentToCamera(
	const SceneDocument& document,
	const SceneEntity& cameraEntity,
	const SceneComponent& cameraComponent,
	Camera* camera,
	float aspectRatio
) const {
	if (!camera || !IsEntityActiveInHierarchy(document, cameraEntity)) {
		return false;
	}
	const Transform transform =
		SceneTransformResolver::ResolveScene3DTransform(document, cameraEntity);
	camera->SetOrbitMode(false);
	camera->SetTranslate(transform.translate);
	camera->SetRotateQuaternion(transform.quaternionRotate);
	camera->SetFovY(std::clamp(
		cameraComponent.cameraFovY,
		kMinimumFov,
		kMaximumFov
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

void SceneCameraSystem::ApplyMainCamera(
	const SceneDocument& document,
	Camera* camera
) const {
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
		const SceneComponent* component =
			FindEnabledComponent(entity, "Camera");
		if (!component) {
			continue;
		}
		if (!fallbackEntity) {
			fallbackEntity = &entity;
			fallbackCamera = component;
		}
		if (component->cameraIsMain) {
			mainEntity = &entity;
			mainCamera = component;
			break;
		}
	}

	const SceneEntity* entity = mainEntity ? mainEntity : fallbackEntity;
	const SceneComponent* component = mainCamera ? mainCamera : fallbackCamera;
	if (!entity || !component) {
		return;
	}

	camera->SetOrbitMode(false);
	const Transform transform =
		SceneTransformResolver::ResolveScene3DTransform(document, *entity);
	camera->SetTranslate(transform.translate);
	camera->SetRotateQuaternion(transform.quaternionRotate);
	camera->SetFovY(std::clamp(
		component->cameraFovY,
		kMinimumFov,
		kMaximumFov
	));
	camera->SetNearClip((std::max)(component->cameraNearClip, 0.001f));
	camera->SetFarClip((std::max)(
		component->cameraFarClip,
		component->cameraNearClip + 0.001f
	));
}

bool SceneCameraSystem::UpdateThirdPersonCamera(
	SceneDocument& document,
	Camera* camera,
	Player* player,
	const std::vector<SceneRuntimeObjectBinding>& bindings,
	bool playing
) {
	if (!playing || !camera || !player) {
		playerCameraInitialized_ = false;
		return false;
	}

	const SceneEntity* fallbackEntity = nullptr;
	const SceneComponent* fallbackCamera = nullptr;
	const SceneComponent* fallbackThirdPerson = nullptr;
	const SceneEntity* mainEntity = nullptr;
	const SceneComponent* mainCamera = nullptr;
	const SceneComponent* mainThirdPerson = nullptr;
	for (const SceneEntity& entity : document.GetEntities()) {
		if (!IsEntityActiveInHierarchy(document, entity)) {
			continue;
		}
		const SceneComponent* cameraComponent =
			FindEnabledComponent(entity, "Camera");
		const SceneComponent* thirdPerson =
			FindEnabledComponent(entity, "ThirdPersonCamera");
		if (!cameraComponent || !thirdPerson) {
			continue;
		}
		if (!fallbackEntity) {
			fallbackEntity = &entity;
			fallbackCamera = cameraComponent;
			fallbackThirdPerson = thirdPerson;
		}
		if (cameraComponent->cameraIsMain) {
			mainEntity = &entity;
			mainCamera = cameraComponent;
			mainThirdPerson = thirdPerson;
			break;
		}
	}

	const SceneEntity* cameraEntity = mainEntity ? mainEntity : fallbackEntity;
	const SceneComponent* cameraComponent = mainCamera ? mainCamera : fallbackCamera;
	const SceneComponent* thirdPerson =
		mainThirdPerson ? mainThirdPerson : fallbackThirdPerson;
	if (
		!cameraEntity ||
		!cameraComponent ||
		!thirdPerson ||
		!HasComponent(*cameraEntity, "PlayerBehavior")
	) {
		playerCameraInitialized_ = false;
		return false;
	}

	if (!playerCameraInitialized_) {
		playerCameraController_.Initialize(camera);
		const Transform initialTransform =
			SceneTransformResolver::ResolveScene3DTransform(
				document,
				*cameraEntity
			);
		playerCameraController_.SetYawPitch(
			initialTransform.rotate.y,
			initialTransform.rotate.x
		);
		playerCameraController_.SetDistance(thirdPerson->thirdPersonDistance);
		playerCameraController_.SetAimDistance(
			thirdPerson->thirdPersonAimDistance
		);
		playerCameraInitialized_ = true;
	}

	Input* input = Input::GetInstance();
	const bool altHeld =
		input && (input->PushKey(DIK_LMENU) || input->PushKey(DIK_RMENU));
	if (altHeld) {
		return true;
	}

	camera->SetOrbitMode(false);
	camera->SetFovY(std::clamp(
		cameraComponent->cameraFovY,
		kMinimumFov,
		kMaximumFov
	));
	camera->SetNearClip((std::max)(cameraComponent->cameraNearClip, 0.001f));
	camera->SetFarClip((std::max)(
		cameraComponent->cameraFarClip,
		cameraComponent->cameraNearClip + 0.001f
	));
	playerCameraController_.SetTargetOffset(
		thirdPerson->thirdPersonTargetOffset
	);
	playerCameraController_.SetAimTargetOffset(
		thirdPerson->thirdPersonAimTargetOffset
	);
	playerCameraController_.SetMouseSensitivity(
		thirdPerson->thirdPersonMouseSensitivity
	);
	playerCameraController_.SetPitchLimit(
		thirdPerson->thirdPersonMinPitch,
		thirdPerson->thirdPersonMaxPitch
	);
	playerCameraController_.SetOcclusionMargin(
		thirdPerson->thirdPersonOcclusionMargin
	);
	playerCameraController_.SetMouseInvert(
		thirdPerson->thirdPersonInvertYaw,
		thirdPerson->thirdPersonInvertPitch
	);

	std::vector<OBBCollider*> obstacles;
	obstacles.reserve(bindings.size());
	for (const SceneRuntimeObjectBinding& binding : bindings) {
		if (
			!binding.entity ||
			!binding.collider ||
			HasComponent(*binding.entity, "PlayerBehavior") ||
			!IsEntityActiveInHierarchy(document, *binding.entity)
		) {
			continue;
		}
		std::string modelPath = binding.entity->modelPath;
		if (const SceneComponent* meshRenderer =
			FindEnabledComponent(*binding.entity, "MeshRenderer")) {
			modelPath = meshRenderer->modelPath;
		}
		std::transform(
			modelPath.begin(),
			modelPath.end(),
			modelPath.begin(),
			[](unsigned char character) {
				return static_cast<char>(std::tolower(character));
			}
		);
		if (
			modelPath.find("terrain") == std::string::npos &&
			binding.collider->GetType() == Collider::Type::OBB
		) {
			obstacles.push_back(static_cast<OBBCollider*>(binding.collider));
		}
	}

	playerCameraController_.Update(
		player->GetPosition(),
		obstacles,
		!altHeld
	);
	ApplyPlayerDissolve(bindings);
	return true;
}

void SceneCameraSystem::ApplyPlayerDissolve(
	const std::vector<SceneRuntimeObjectBinding>& bindings
) const {
	constexpr float kStartPitch = -0.35f;
	constexpr float kFullPitch = -0.85f;
	const float rawAmount = std::clamp(
		(kStartPitch - playerCameraController_.GetPitch()) /
			(kStartPitch - kFullPitch),
		0.0f,
		1.0f
	);
	const float amount = rawAmount * rawAmount * (3.0f - 2.0f * rawAmount);
	for (const SceneRuntimeObjectBinding& binding : bindings) {
		if (
			binding.entity &&
			binding.object &&
			HasComponent(*binding.entity, "PlayerBehavior")
		) {
			binding.object->SetDissolve(amount, 0.08f, 6.0f);
		}
	}
}

bool SceneCameraSystem::TryStartCameraPath(
	SceneDocument& document,
	Camera* camera
) {
	if (!camera || cameraPathRuntime_.IsPlaying()) {
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
		const SceneComponent* path = FindEnabledComponent(entity, "CameraPath");
		if (!path || path->cameraPathTriggerType != "Key") {
			continue;
		}
		std::string triggerKey = path->cameraPathTriggerKey;
		std::transform(
			triggerKey.begin(),
			triggerKey.end(),
			triggerKey.begin(),
			[](unsigned char character) {
				return static_cast<char>(std::toupper(character));
			}
		);
		const bool triggered =
			(triggerKey.empty() || triggerKey == "C") &&
			input->TriggerKey(DIK_C);
		if (!triggered) {
			continue;
		}

		const SceneEntity* targetEntity = nullptr;
		const SceneComponent* targetCamera = nullptr;
		if (!path->cameraPathTargetCameraName.empty()) {
			targetEntity =
				document.FindEntityByName(path->cameraPathTargetCameraName);
			targetCamera = targetEntity
				? FindEnabledComponent(*targetEntity, "Camera")
				: nullptr;
		} else {
			const SceneEntity* fallbackEntity = nullptr;
			const SceneComponent* fallbackCamera = nullptr;
			for (const SceneEntity& candidate : document.GetEntities()) {
				if (!IsEntityActiveInHierarchy(document, candidate)) {
					continue;
				}
				const SceneComponent* component =
					FindEnabledComponent(candidate, "Camera");
				if (!component) {
					continue;
				}
				if (!fallbackEntity) {
					fallbackEntity = &candidate;
					fallbackCamera = component;
				}
				if (component->cameraIsMain) {
					targetEntity = &candidate;
					targetCamera = component;
					break;
				}
			}
			if (!targetEntity) {
				targetEntity = fallbackEntity;
				targetCamera = fallbackCamera;
			}
		}
		if (!targetEntity || !targetCamera) {
			continue;
		}

		camera->SetFovY(std::clamp(
			targetCamera->cameraFovY,
			kMinimumFov,
			kMaximumFov
		));
		camera->SetNearClip((std::max)(targetCamera->cameraNearClip, 0.001f));
		camera->SetFarClip((std::max)(
			targetCamera->cameraFarClip,
			targetCamera->cameraNearClip + 0.001f
		));
		camera->Update();
		cameraPathRuntime_.Play(document, entity, *path, *camera);
		return cameraPathRuntime_.IsPlaying();
	}
	return false;
}

void SceneCameraSystem::SyncPlayerController(
	const SceneDocument& document,
	Camera* camera,
	Player* player
) {
	if (!camera || !player) {
		return;
	}

	const SceneComponent* targetThirdPerson = nullptr;
	const SceneComponent* fallbackThirdPerson = nullptr;
	for (const SceneEntity& entity : document.GetEntities()) {
		if (!IsEntityActiveInHierarchy(document, entity)) {
			continue;
		}
		const SceneComponent* cameraComponent =
			FindEnabledComponent(entity, "Camera");
		const SceneComponent* thirdPerson =
			FindEnabledComponent(entity, "ThirdPersonCamera");
		if (!cameraComponent || !thirdPerson) {
			continue;
		}
		if (!fallbackThirdPerson) {
			fallbackThirdPerson = thirdPerson;
		}
		if (cameraComponent->cameraIsMain) {
			targetThirdPerson = thirdPerson;
			break;
		}
	}

	const SceneComponent* thirdPerson =
		targetThirdPerson ? targetThirdPerson : fallbackThirdPerson;
	if (!thirdPerson) {
		return;
	}
	const Vector3 focus = Math::Add(
		player->GetPosition(),
		playerCameraController_.IsAimMode()
			? thirdPerson->thirdPersonAimTargetOffset
			: thirdPerson->thirdPersonTargetOffset
	);
	playerCameraController_.SyncFromCameraPose(camera->GetTranslate(), focus);
	playerCameraInitialized_ = true;
}

void SceneCameraSystem::InitializePauseDebugCamera(
	Camera* camera,
	Camera* debugCamera
) {
	if (!camera || !debugCamera || debugCameraInitialized_) {
		return;
	}
	debugCamera->SetFovY(camera->GetFovY());
	debugCamera->SetAspectRatio(camera->GetAspectRatio());
	debugCamera->SetNearClip(camera->GetNearClip());
	debugCamera->SetFarClip(camera->GetFarClip());

	const Matrix4x4& cameraWorld = camera->GetWorldMatrix();
	Vector3 forward = {
		cameraWorld.m[2][0],
		cameraWorld.m[2][1],
		cameraWorld.m[2][2]
	};
	const float length = Math::Length(forward);
	forward = length > 0.000001f
		? Math::Multiply(forward, 1.0f / length)
		: Vector3{ 0.0f, 0.0f, 1.0f };

	constexpr float kOrbitDistance = 10.0f;
	debugCamera->SetOrbitMode(true);
	debugCamera->SetOrbitDistance(kOrbitDistance);
	debugCamera->SetOrbitTarget(Math::Add(
		camera->GetTranslate(),
		Math::Multiply(forward, kOrbitDistance)
	));
	debugCamera->SetOrbitAngle(
		std::atan2(-forward.x, forward.z),
		std::asin(std::clamp(-forward.y, -1.0f, 1.0f))
	);
	debugCamera->UpdatePreviewMatrices();
	debugCameraInitialized_ = true;
}
