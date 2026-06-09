#include "GamePlayScene.h"

#include "TitleScene.h"
#include "SceneManager.h"

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
#include "../player/Player.h"

#include "../externals/imgui/imgui.h"

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

	TextureManager::GetInstance()->LoadTexture("resources/monsterBall.png");
	TextureManager::GetInstance()->LoadTexture("resources/uvChecker.png");
	ModelManager::GetInstance()->LoadModel("terrain.obj");
	ModelManager::GetInstance()->LoadModel("Cube.obj");

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
	object3d_->SetModel("terrain.obj");
	object3d_->SetTranslate({ 0.0f, -5.0f, 0.0f });
	object3d_->SetScale({ 10.0f, 10.0f, 10.0f });

	player_ = new Player();
	player_->Initialize(Object3dCommon::GetInstance(), "Cube.obj");

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
	addStageObject("Cube.obj", { 0.0f, -0.1f, 0.0f }, { 5.0f, 0.1f, 5.0f }, { 5.0f, 0.1f, 5.0f }, false);
	addStageObject("Cube.obj", { 0.0f, 2.0f, 5.0f }, { 5.0f, 2.0f, 0.2f }, { 5.0f, 2.0f, 0.2f }, true);
	addStageObject("Cube.obj", { -5.0f, 2.0f, 0.0f }, { 0.2f, 2.0f, 5.0f }, { 0.2f, 2.0f, 5.0f }, true);
	addStageObject("Cube.obj", { 3.0f, 0.5f, -2.0f }, { 1.0f, 0.5f, 1.0f }, { 1.0f, 0.5f, 1.0f }, true);

	skybox_ = new Skybox();
	skybox_->Initialize(
		Object3dCommon::GetInstance(),
		"resources/rostock_laage_airport_4k.dds"
	);
	skybox_->SetScale({ 100.0f, 100.0f, 100.0f });
	if (player_ && player_->GetObject()) {
		player_->GetObject()->SetEnvironmentMap(
			"resources/rostock_laage_airport_4k.dds",
			0.35f
		);
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

	//soundData_ = audio_->SoundLoadWave("resources/fanfare.wav");
}

void GamePlayScene::Update()
{
	if (Input::GetInstance()->TriggerKey(DIK_RETURN)) {
		BaseScene* scene = new TitleScene;
		sceneManager_->SetNextScene(scene);
	}

	if (Input::GetInstance()->TriggerKey(DIK_SPACE)) {
		if (planeBurstEmitter_) {
			planeBurstEmitter_->Emit();
		}
		if (ringBurstEmitter_) {
			ringBurstEmitter_->Emit();
		}
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
	ParticleManager::GetInstance()->Update();

#ifdef _DEBUG
	ImGui::Begin("Scene Controls");
	ImGui::End();

	camera_->DrawImGui("Camera");

	if (lightManager_) {
		lightManager_->DrawImGui();
	}

	particleEffectEditor_.DrawImGui(
		editingEffect_,
		editorPreviewEmitter_,
		"Particle Effect Editor"
	);

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

	object3d_->Update();
	if (plane_) {
		plane_->Update();
	}
	if (axis) {
		axis->Update();
	}
	if (player_) {
		player_->Update(staticColliders_);
		Vector3 target = player_->GetPosition();
		target.y += 0.8f;
		camera_->SetOrbitTarget(target);
		camera_->SetOrbitDistance(10.0f);
	}
	camera_->Update();
	for (StageObject& stageObject : stageObjects_) {
		if (stageObject.object) {
			stageObject.object->Update();
		}
	}
	if (skybox_) {
		skybox_->Update();
	}
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

	object3d_->Draw();
	if (plane_) {
		plane_->Draw();
	}
	if (axis) {
		axis->Draw();
	}
	if (player_) {
		player_->Draw();
	}
	for (StageObject& stageObject : stageObjects_) {
		if (stageObject.object) {
			stageObject.object->Draw();
		}
	}

	if (skybox_) {
		skybox_->Draw();
	}

	ParticleManager::GetInstance()->Draw();
}

void GamePlayScene::DrawShadow()
{
	if (!lightManager_ || !shadowManager_) {
		return;
	}

	std::vector<Object3d*> shadowCasters;
	shadowCasters.reserve(3 + stageObjects_.size() + (player_ ? 1 : 0));
	shadowCasters.push_back(object3d_);
	if (plane_) {
		shadowCasters.push_back(plane_);
	}
	if (axis) {
		shadowCasters.push_back(axis);
	}
	if (player_) {
		shadowCasters.push_back(player_->GetObject());
	}
	for (StageObject& stageObject : stageObjects_) {
		if (stageObject.object) {
			shadowCasters.push_back(stageObject.object);
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

	delete camera_;
	camera_ = nullptr;

	delete skybox_;
	skybox_ = nullptr;
}
