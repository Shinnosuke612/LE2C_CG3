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
	ModelManager::GetInstance()->LoadModel("plane.gltf");

	ParticleEffectDesc gameplayEffect{};

	if (ParticleEffectResource::Load(
		"resources/particles/fairyParticle.json",
		gameplayEffect
	)) {
		emitter_ = ParticleEffectResource::CreateEmitter(gameplayEffect);
	}

	object3d_ = new Object3d();
	object3d_->Initialize(Object3dCommon::GetInstance());
	object3d_->SetModel("terrain.obj");
	object3d_->SetTranslate({ 0.0f, -5.0f, 0.0f });
	object3d_->SetScale({ 10.0f, 10.0f, 10.0f });

	plane_ = new Object3d();
	plane_->Initialize(Object3dCommon::GetInstance());
	plane_->SetModel("plane.gltf");
	plane_->SetTranslate({ 0.0f, 7.0f, 0.0f });

	lightManager_ = std::make_unique<LightManager>();
	lightManager_->Initialize(
		Object3dCommon::GetInstance()->GetDxCommon(),
		"resources/lights/gameplay_lights.json"
	);

	//soundData_ = audio_->SoundLoadWave("resources/fanfare.wav");
}

void GamePlayScene::Update()
{
	if (Input::GetInstance()->TriggerKey(DIK_RETURN)) {
		BaseScene* scene = new TitleScene;
		sceneManager_->SetNextScene(scene);
	}

	ImGui::Begin("Scene Controls");
	ImGui::End();

	camera_->DrawImGui("Camera");
	camera_->Update();

	emitter_->Update();
	ParticleManager::GetInstance()->Update();

#ifdef _DEBUG
	if (lightManager_) {
		lightManager_->DrawImGui();
	}
#endif

	for (Sprite* sprite : sprites_) {
		sprite->Update();
	}

	object3d_->Update();
	plane_->Update();
}

void GamePlayScene::Draw()
{

	if (lightManager_) {
		lightManager_->Bind(
			Object3dCommon::GetInstance()->GetDxCommon()->GetCommandList(),
			3
		);
	}
	object3d_->Draw();
	plane_->Draw();
	//ParticleManager::GetInstance()->Draw();
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

	delete emitter_;
	emitter_ = nullptr;

	delete camera_;
	camera_ = nullptr;
}