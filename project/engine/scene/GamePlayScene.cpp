#include "GamePlayScene.h"

#include "TitleScene.h"
#include "SceneManager.h"
#include "EditorSession.h"
#include "SceneDocument.h"

#include "../base/DirectXCommon.h"
#include "../3d/SrvManager.h"
#include "../base/ImGuiManager.h"
#include "../io/Input.h"

#include "../2d/SpriteCommon.h"
#include "../2d/Sprite.h"
#include "../2d/TextureManager.h"
#include "../3d/Camera.h"
#include "../3d/Object3dCommon.h"
#include "../3d/Object3d.h"
#include "../3d/ModelManager.h"
#include "../particle/ParticleManager.h"
#include "../particle/ParticleEmitter.h"
#include "../3d/LightManager.h"
#include "../3d/ShadowManager.h"
#include "../3d/Skybox.h"
#include "../effect/LightningRenderer.h"
#include "../player/Player.h"
#include "../utility/EditableResourcePath.h"

#include "../externals/imgui/imgui.h"

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

	std::string GetSceneModelPath(
		const SceneManager* sceneManager,
		const char* name,
		const char* fallback
	) {
		const SceneEntity* entity = FindSceneEntity(sceneManager, name);
		return entity ? entity->modelPath : fallback;
	}

	void ApplySceneTransform(
		const SceneManager* sceneManager,
		const char* name,
		Object3d* object
	) {
		const SceneEntity* entity = FindSceneEntity(sceneManager, name);
		if (entity && object) {
			object->GetTransform() = entity->transform;
		}
	}

	bool HasComponent(const SceneEntity& entity, const char* componentName) {
		return std::find(
			entity.components.begin(),
			entity.components.end(),
			componentName
		) != entity.components.end();
	}

	bool IsSpecializedSceneEntity(const SceneEntity& entity) {
		return entity.name == "Terrain" ||
			entity.name == "Animated Cube" ||
			entity.name == "Human" ||
			entity.name == "Player";
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
		if (IsSpecializedSceneEntity(entity)) {
			continue;
		}

		const bool hasRenderer =
			!entity.modelPath.empty() &&
			HasComponent(entity, "MeshRenderer");
		requiredIds.insert(entity.id);
		auto found = sceneModelObjects_.find(entity.id);
		if (
			found != sceneModelObjects_.end() &&
			(
				found->second.modelPath != entity.modelPath ||
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
				ModelManager::GetInstance()->LoadModel(entity.modelPath);
				sceneObject.object->SetModel(entity.modelPath);
			}
			sceneObject.modelPath = entity.modelPath;
			sceneObject.hasRenderer = hasRenderer;
			found = sceneModelObjects_.emplace(
				entity.id,
				std::move(sceneObject)
			).first;
		}

		found->second.object->GetTransform() = entity.transform;
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
		if (entity->name == "Terrain") {
			return object3d_;
		}
		if (entity->name == "Animated Cube") {
			return animatedCube_;
		}
		if (entity->name == "Human") {
			return human_;
		}
		if (entity->name == "Player" && player_) {
			return player_->GetObject();
		}
		return nullptr;
	};

	for (const SceneEntity& entity : document->GetEntities()) {
		Object3d* object = resolveObject(entity.id);
		if (!object) {
			continue;
		}
		if (IsSpecializedSceneEntity(entity)) {
			object->GetTransform() = entity.transform;
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
		if (
			entity.spriteTexturePath.empty() ||
			!HasComponent(entity, "SpriteRenderer")
		) {
			continue;
		}
		requiredIds.insert(entity.id);
		auto found = sceneSpriteObjects_.find(entity.id);
		if (
			found != sceneSpriteObjects_.end() &&
			found->second.texturePath != entity.spriteTexturePath
		) {
			delete found->second.sprite;
			sceneSpriteObjects_.erase(found);
			found = sceneSpriteObjects_.end();
		}

		if (found == sceneSpriteObjects_.end()) {
			TextureManager::GetInstance()->LoadTexture(entity.spriteTexturePath);
			SceneSpriteObject sceneSprite{};
			sceneSprite.sprite = new Sprite();
			sceneSprite.sprite->Initialize(
				SpriteCommon::GetInstance(),
				entity.spriteTexturePath
			);
			sceneSprite.texturePath = entity.spriteTexturePath;
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
			entity.spriteSize.x * spriteTransform.scale.x,
			entity.spriteSize.y * spriteTransform.scale.y
		});
		sprite->SetAnchorPoint(entity.spriteAnchor);
		sprite->SetColor(entity.spriteColor);
		sprite->SetIsFlipX(entity.spriteFlipX);
		sprite->SetIsFlipY(entity.spriteFlipY);
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
			if (!entity.modelPath.empty()) {
				ModelManager::GetInstance()->LoadModel(entity.modelPath);
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

	object3d_ = new Object3d();
	object3d_->Initialize(Object3dCommon::GetInstance());
	object3d_->SetModel(GetSceneModelPath(
		sceneManager_,
		"Terrain",
		"terrain.obj"
	));
	object3d_->GetTransform() = GetSceneTransform(
		sceneManager_,
		"Terrain",
		Transform{
			{ 10.0f, 10.0f, 10.0f },
			{ 0.0f, 0.0f, 0.0f },
			{ 0.0f, -5.0f, 0.0f }
		}
	);

	animatedCube_ = new Object3d();
	animatedCube_->Initialize(Object3dCommon::GetInstance());
	animatedCube_->SetModel(GetSceneModelPath(
		sceneManager_,
		"Animated Cube",
		"AnimatedCube/AnimatedCube.gltf"
	));
	animatedCube_->GetTransform() = GetSceneTransform(
		sceneManager_,
		"Animated Cube",
		Transform{
			{ 0.65f, 0.65f, 0.65f },
			{ 0.0f, 0.0f, 0.0f },
			{ 3.0f, 10.5f, -2.0f }
		}
	);
	animatedCube_->SetAnimationLoop(true);
	animatedCube_->SetAnimationSpeed(1.0f);

	human_ = new Object3d();
	human_->Initialize(Object3dCommon::GetInstance());
	human_->SetModel(GetSceneModelPath(
		sceneManager_,
		"Human",
		"human/walk.gltf"
	));
	human_->GetTransform() = GetSceneTransform(
		sceneManager_,
		"Human",
		Transform{
			{ 1.0f, 1.0f, 1.0f },
			{ 0.0f, 0.0f, 0.0f },
			{ -2.0f, 0.0f, -2.0f }
		}
	);
	human_->SetAnimationLoop(true);
	human_->SetAnimationSpeed(1.0f);
	human_->SetEnvironmentMap(
		"resources/rostock_laage_airport_4k.dds",
		0.01f
	);

	Vector3 target = human_->GetTranslate();
	camera_->SetOrbitTarget(target);

	player_ = new Player();
	const std::string playerModelPath = GetSceneModelPath(
		sceneManager_,
		"Player",
		"Cube.obj"
	);
	player_->Initialize(Object3dCommon::GetInstance(), playerModelPath.c_str());
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
			if (collidable) {
				staticColliders_.push_back(&stageObjects_.back().collider);
			}
		};

	stageObjects_.clear();
	stageObjects_.reserve(4);
	staticColliders_.clear();
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
		skybox_ = new Skybox();
		skybox_->Initialize(Object3dCommon::GetInstance(), skyboxPath);
		skybox_->SetScale({ 100.0f, 100.0f, 100.0f });
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
	ApplySceneTransform(sceneManager_, "Terrain", object3d_);
	ApplySceneTransform(sceneManager_, "Animated Cube", animatedCube_);
	ApplySceneTransform(sceneManager_, "Human", human_);
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
	if (animatedCube_ && animatedCube_->HasAnimation()) {
		bool isPlaying = animatedCube_->IsAnimationPlaying();
		if (ImGui::Checkbox("Play Animation", &isPlaying)) {
			animatedCube_->SetAnimationPlaying(isPlaying);
		}

		bool isLooping = animatedCube_->IsAnimationLooping();
		if (ImGui::Checkbox("Loop Animation", &isLooping)) {
			animatedCube_->SetAnimationLoop(isLooping);
		}

		float animationSpeed = animatedCube_->GetAnimationSpeed();
		if (ImGui::DragFloat(
			"Animation Speed",
			&animationSpeed,
			0.01f,
			-4.0f,
			4.0f
		)) {
			animatedCube_->SetAnimationSpeed(animationSpeed);
		}

		if (ImGui::Button("Reset Animation")) {
			animatedCube_->ResetAnimation();
		}

		const float duration = animatedCube_->GetAnimationDuration();
		const float progress = duration > 0.0f
			? animatedCube_->GetAnimationTime() / duration
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
	if (human_ && human_->GetSkeleton()) {
		bool isPlaying = human_->IsAnimationPlaying();
		if (ImGui::Checkbox("Play Skeleton Animation", &isPlaying)) {
			human_->SetAnimationPlaying(isPlaying);
		}

		bool isLooping = human_->IsAnimationLooping();
		if (ImGui::Checkbox("Loop Skeleton Animation", &isLooping)) {
			human_->SetAnimationLoop(isLooping);
		}

		float animationSpeed = human_->GetAnimationSpeed();
		if (ImGui::DragFloat(
			"Skeleton Animation Speed",
			&animationSpeed,
			0.01f,
			-4.0f,
			4.0f
		)) {
			human_->SetAnimationSpeed(animationSpeed);
		}

		if (ImGui::Button("Reset Skeleton Animation")) {
			human_->ResetAnimation();
		}

		ImGui::Text(
			"Joints: %zu",
			human_->GetSkeleton()->joints.size()
		);

		const float duration = human_->GetAnimationDuration();
		const float progress = duration > 0.0f
			? human_->GetAnimationTime() / duration
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
			skybox_ = new Skybox();
			skybox_->Initialize(Object3dCommon::GetInstance(), *generatedSkybox);
			skybox_->SetScale({ 100.0f, 100.0f, 100.0f });
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
	SyncSceneModelObjects();
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
	if (showSkeletonDebug_ && human_) {
		human_->DrawSkeletonDebug(
			showJointNames_,
			showJointAxes_,
			jointRadius_,
			jointAxisLength_
		);
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

	if (object3d_ && IsSceneEntityActive(sceneManager_, "Terrain")) {
		object3d_->Draw();
	}
	if (plane_) {
		plane_->Draw();
	}
	if (axis) {
		axis->Draw();
	}
	if (animatedCube_ && IsSceneEntityActive(sceneManager_, "Animated Cube")) {
		animatedCube_->Draw();
	}
	if (human_ && IsSceneEntityActive(sceneManager_, "Human")) {
		human_->Draw();
	}
	if (player_ && IsSceneEntityActive(sceneManager_, "Player")) {
		player_->Draw();
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
		4 + stageObjects_.size() + sceneModelObjects_.size() +
		(player_ ? 1 : 0)
	);
	if (object3d_ && IsSceneEntityActive(sceneManager_, "Terrain")) {
		shadowCasters.push_back(object3d_);
	}
	//if (plane_) {
	//	shadowCasters.push_back(plane_);
	//}
	//if (axis) {
	//	shadowCasters.push_back(axis);
	//}
	//if (animatedCube_) {
	//	shadowCasters.push_back(animatedCube_);
	//}
	//if (human_) {
	//	shadowCasters.push_back(human_);
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

	delete object3d_;
	object3d_ = nullptr;

	delete plane_;
	plane_ = nullptr;

	delete axis;
	axis = nullptr;

	delete animatedCube_;
	animatedCube_ = nullptr;

	delete human_;
	human_ = nullptr;

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
