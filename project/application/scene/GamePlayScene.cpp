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
#include "../player/Player.h"
#include "../../engine/utility/EditableResourcePath.h"
#include "../../engine/math/Math.h"
#include "../../engine/math/Vector2.h"

#include "../../externals/imgui/imgui.h"

#include <algorithm>
#include <cctype>
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
		camera->SetTranslate(cameraEntity->transform.translate);
		camera->SetRotate(cameraEntity->transform.rotate);
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
		found->second.object->SetCullMode(meshRenderer
			? ToObjectCullMode(meshRenderer->meshCullMode)
			: Object3dCommon::CullMode::kBack
		);
		if (HasComponent(entity, "PlayerBehavior")) {
			found->second.object->SetDissolve(0.0f);
		}
		const bool hasObbCollider = HasComponent(entity, "OBBCollider");
		found->second.hasCollider = hasObbCollider;
		if (hasObbCollider) {
			found->second.collider.SetWorldTransform(
				&found->second.object->GetTransform()
			);
			found->second.collider.SetHalfSize({
				(std::max)(entity.transform.scale.x, 0.001f),
				(std::max)(entity.transform.scale.y, 0.001f),
				(std::max)(entity.transform.scale.z, 0.001f)
			});
			found->second.collider.SetOffset({ 0.0f, 0.0f, 0.0f });
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
			found->second.physicsBody.obbCollider = hasObbCollider
				? &found->second.collider
				: nullptr;
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
		sceneObject.object->SetEnvironmentMap(
			environmentMapPath_,
			ResolveEnvironmentReflectionIntensity(
				meshRenderer,
				environmentReflectionIntensity_
			)
		);
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
	for (OBBCollider* staticCollider : staticColliders_) {
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
	camera->SetOrbitMode(false);
	camera->SetTranslate(cameraEntity.transform.translate);
	camera->SetRotate(cameraEntity.transform.rotate);
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
			cameraObstacles.push_back(&found->second.collider);
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
		runtime.targetCameraName = monitorRenderer->monitorCameraName;
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
			stageObject.object->SetCamera(viewCamera);
			stageObject.object->Update();
		}
	}
	for (auto& [entityId, sceneObject] : sceneModelObjects_) {
		(void)entityId;
		if (sceneObject.object) {
			sceneObject.object->SetCamera(viewCamera);
			sceneObject.object->Update();
		}
	}
	if (plane_) {
		plane_->SetCamera(viewCamera);
		plane_->Update();
	}
	if (axis) {
		axis->SetCamera(viewCamera);
		axis->Update();
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
		for (const SceneEntity& entity : document->GetEntities()) {
			if (
				entity.id == skipEntityId ||
				!IsEntityActiveInHierarchy(*document, entity)
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

	if (lightningRenderer_) {
		lightningRenderer_->Draw(viewCamera);
	}

	ParticleManager::GetInstance()->Draw();

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
				staticColliders_.push_back(&found->second.collider);
			}
		}
	}
	for (StageObject& stageObject : stageObjects_) {
		if (stageObject.object) {
			staticColliders_.push_back(&stageObject.collider);
		}
	}
}

void GamePlayScene::Initialize()
{
	// ゲーム固有の初期化

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
	ParticleManager::GetInstance()->SetCamera(camera_);
	ParticleManager::GetInstance()->SetGpuParticleEnabled(true);
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
		SrvManager::GetInstance()
	);

	lightningRenderer_ = std::make_unique<LightningRenderer>();
	lightningRenderer_->Initialize(Object3dCommon::GetInstance()->GetDxCommon());

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
	ImGui::Checkbox("Show Camera Direction", &showCameraDebug_);
	ImGui::Checkbox("Show CameraPath", &showCameraPathDebug_);
	ImGui::Checkbox(
		"Show CameraPath Point Camera Direction",
		&showCameraPathPointCameraDebug_
	);
	ImGui::SeparatorText("Skeleton");
	ImGui::Checkbox("Show Skeleton", &showSkeletonDebug_);
	if (showSkeletonDebug_) {
		ImGui::Checkbox("Show Joint Names", &showJointNames_);
		ImGui::Checkbox("Show Joint Axes", &showJointAxes_);
		ImGui::DragFloat(
			"Joint Radius",
			&jointRadius_,
			0.001f,
			0.002f,
			0.1f
		);
		ImGui::DragFloat(
			"Joint Axis Length",
			&jointAxisLength_,
			0.002f,
			0.01f,
			0.5f
		);
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

	if (ParticleManager::GetInstance()->IsGpuParticleEnabled()) {
		ParticleManager::GetInstance()->DrawGpuParticleImGui("GPU Particle");
	}
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
	ImGui::Checkbox("Show Camera Direction", &showCameraDebug_);
	ImGui::Checkbox("Show CameraPath", &showCameraPathDebug_);
	ImGui::Checkbox(
		"Show CameraPath Point Camera Direction",
		&showCameraPathPointCameraDebug_
	);
	ImGui::End();
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
#endif
}

void GamePlayScene::Draw()
{
	Camera* viewCamera = GetSceneViewCamera();
	ApplyRenderCamera(viewCamera);
	DrawSceneView(viewCamera);
}

void GamePlayScene::DrawOffscreenViews()
{
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
		if (
			!monitorEntity ||
			!IsEntityActiveInHierarchy(*document, *monitorEntity) ||
			runtime.targetCameraName.empty() ||
			!runtime.camera ||
			!runtime.renderTarget ||
			monitorObject == sceneModelObjects_.end() ||
			!monitorObject->second.object
		) {
			if (monitorObject != sceneModelObjects_.end() && monitorObject->second.object) {
				monitorObject->second.object->ClearTextureOverride();
			}
			continue;
		}

		const SceneEntity* cameraEntity =
			document->FindEntityByName(runtime.targetCameraName);
		if (!cameraEntity) {
			monitorObject->second.object->ClearTextureOverride();
			continue;
		}
		const SceneComponent* cameraComponent =
			FindEnabledComponent(*cameraEntity, "Camera");
		if (!cameraComponent) {
			monitorObject->second.object->ClearTextureOverride();
			continue;
		}

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
			monitorObject->second.object->ClearTextureOverride();
			continue;
		}

		ApplyRenderCamera(runtime.camera);
		runtime.renderTarget->Begin();
		SrvManager::GetInstance()->PreDraw();
		DrawSceneView(
			runtime.camera,
			runtime.hideSelf ? monitorEntityId : 0
		);
		runtime.renderTarget->End();

		monitorObject->second.object->SetTextureOverride(
			runtime.renderTarget->GetSrvGpuHandle()
		);
	}

	ApplyRenderCamera(GetSceneViewCamera());
}

void GamePlayScene::DrawShadow()
{
	if (!lightManager_ || !shadowManager_) {
		return;
	}

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
	for (StageObject& stageObject : stageObjects_) {
		if (stageObject.object) {
			shadowCasters.push_back(stageObject.object);
		}
	}
	if (SceneDocument* document = sceneManager_->GetActiveSceneDocument()) {
		for (const SceneEntity& entity : document->GetEntities()) {
			if (!IsEntityActiveInHierarchy(*document, entity)) {
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

	delete camera_;
	camera_ = nullptr;

	delete debugCamera_;
	debugCamera_ = nullptr;
	debugCameraInitialized_ = false;

	delete skybox_;
	skybox_ = nullptr;
}
