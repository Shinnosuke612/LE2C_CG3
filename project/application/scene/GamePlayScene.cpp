#include "GamePlayScene.h"

#include "TitleScene.h"
#include "../../engine/scene/SceneManager.h"
#include "../../engine/scene/EditorSession.h"
#include "../../engine/scene/SceneDocument.h"

#include "../../engine/base/DirectXCommon.h"
#include "../../engine/3d/SrvManager.h"
#include "../../engine/base/ImGuiManager.h"
#include "../../engine/io/Input.h"

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

#include "../../externals/imgui/imgui.h"

#include <algorithm>
#include <cmath>
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

	bool HasComponent(const SceneEntity& entity, const char* componentName) {
		return std::find_if(
			entity.components.begin(),
			entity.components.end(),
			[componentName](const SceneComponent& component) {
				return component.enabled && component.type == componentName;
			}
		) != entity.components.end();
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
				sceneObject.object->SetEnvironmentMap(environmentMapPath_, 0.01f);
			}
			sceneObject.modelPath = modelPath;
			sceneObject.hasRenderer = hasRenderer;
			found = sceneModelObjects_.emplace(
				entity.id,
				std::move(sceneObject)
			).first;
		}

		found->second.object->GetTransform() = entity.transform;
		const bool hasObbCollider = HasComponent(entity, "OBBCollider");
		found->second.hasCollider = hasObbCollider;
		if (hasObbCollider) {
			found->second.collider.SetWorldTransform(
				&found->second.object->GetTransform()
			);
			found->second.collider.SetHalfSize({
				(std::max)(entity.transform.scale.x * 0.5f, 0.001f),
				(std::max)(entity.transform.scale.y * 0.5f, 0.001f),
				(std::max)(entity.transform.scale.z * 0.5f, 0.001f)
			});
			found->second.collider.SetOffset({ 0.0f, 0.0f, 0.0f });
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

	std::string skyboxPath = EditableResourcePath::Resolve(
		"resources/skyboxes/generated_space.dds"
	).generic_string();
	bool skyboxLoaded = TextureManager::GetInstance()->LoadTexture(skyboxPath);
	if (!skyboxLoaded) {
		skyboxPath = EditableResourcePath::Resolve(
			"resources/rostock_laage_airport_4k.dds"
		).generic_string();
		skyboxLoaded = TextureManager::GetInstance()->LoadTexture(skyboxPath);
	}
	if (skyboxLoaded) {
		environmentMapPath_ = skyboxPath;
		skybox_ = new Skybox();
		skybox_->Initialize(Object3dCommon::GetInstance(), skyboxPath);
		skybox_->SetScale({ 100.0f, 100.0f, 100.0f });
		for (auto& [entityId, sceneObject] : sceneModelObjects_) {
			(void)entityId;
			if (sceneObject.object) {
				sceneObject.object->SetEnvironmentMap(skyboxPath, 0.01f);
			}
		}
		if (player_ && player_->GetObject()) {
			player_->GetObject()->SetEnvironmentMap(skyboxPath, 0.01f);
		}
	}

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

	if (Input::GetInstance()->TriggerKey(DIK_SPACE)) {
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
			delete skybox_;
			environmentMapPath_ = *generatedSkybox;
			skybox_ = new Skybox();
			skybox_->Initialize(Object3dCommon::GetInstance(), *generatedSkybox);
			skybox_->SetScale({ 100.0f, 100.0f, 100.0f });
			for (auto& [entityId, sceneObject] : sceneModelObjects_) {
				(void)entityId;
				if (sceneObject.object) {
					sceneObject.object->SetEnvironmentMap(*generatedSkybox, 0.01f);
				}
			}
			if (player_ && player_->GetObject()) {
				player_->GetObject()->SetEnvironmentMap(*generatedSkybox, 0.01f);
			}
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
	RebuildStaticColliders();
	if (player_ && (!editorSession || editorSession->IsPlaying())) {
		player_->Update(staticColliders_);
		SceneDocument* document = sceneManager_->GetActiveSceneDocument();
		SceneEntity* playerEntity = document
			? document->FindEntityByName("Player")
			: nullptr;
		if (playerEntity && player_->GetObject()) {
			playerEntity->transform = player_->GetObject()->GetTransform();
		}
	}
	SyncSceneSpriteObjects();
	camera_->Update();
	for (StageObject& stageObject : stageObjects_) {
		if (stageObject.object) {
			stageObject.object->Update();
		}
	}
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

void GamePlayScene::Draw()
{
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
			if (!IsEntityActiveInHierarchy(*document, entity)) {
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
		lightningRenderer_->Draw(camera_);
	}

	if (skybox_) {
		skybox_->Draw();
	}

	ParticleManager::GetInstance()->Draw();

	SceneDocument* spriteDocument = sceneManager_->GetActiveSceneDocument();
	if (!sceneSpriteObjects_.empty() && spriteDocument) {
		SpriteCommon::GetInstance()->SetCommonRenderState();
		for (const SceneEntity& entity : spriteDocument->GetEntities()) {
			const auto found = sceneSpriteObjects_.find(entity.id);
			if (
				found != sceneSpriteObjects_.end() &&
				IsEntityActiveInHierarchy(*spriteDocument, entity) &&
				found->second.sprite
			) {
				found->second.sprite->Draw();
			}
		}
	}
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

	delete skybox_;
	skybox_ = nullptr;
}
