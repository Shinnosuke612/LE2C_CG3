#include "Game.h"

#include "engine/base/DirectXCommon.h"
#include "engine/3d/SrvManager.h"
#include "engine/base/ImGuiManager.h"
#include "engine/io/Input.h"

#include "engine/2d/SpriteCommon.h"
#include "engine/2d/Sprite.h"
#include "engine/2d/TextureManager.h"
#include "engine/3d/Camera.h"
#include "engine/3d/Object3dCommon.h"
#include "engine/3d/Object3d.h"
#include "engine/3d/ModelManager.h"
#include "engine/3d/ParticleCommon.h"
#include "engine/3d/ParticleManager.h"
#include "engine/3d/ParticleEmitter.h"

#include "externals/imgui/imgui.h"

void Game::Initialize() {
	// 基底クラスの初期化処理
	Framework::Initialize();

	// ゲーム固有の初期化

	camera_ = new Camera();
	camera_->SetRotate({ 0.0f,0.0f,0.0f });
	camera_->SetTranslate({ 0.0f,0.0f,-10.0f });

	object3dCommon_->SetDefaultCamera(camera_);

	TextureManager::GetInstance()->LoadTexture("resources/monsterBall.png");
	TextureManager::GetInstance()->LoadTexture("resources/uvChecker.png");

	ModelManager::GetInstance()->LoadModel("plane.obj");

	particleCommon_->SetDefaultCamera(camera_);

	particleManager_ = new ParticleManager();
	particleManager_->Initialize(particleCommon_, srvManager_);
	particleManager_->SetCamera(camera_);
	particleManager_->CreateParticleGroup("particle", "resources/uvChecker.png");

	emitter_ = new ParticleEmitter();
	emitter_->Initialize(particleManager_, "particle");
	emitter_->SetTranslate({ 0.0f, 0.0f, 0.0f });
	emitter_->SetCount(4);
	emitter_->SetFrequency(0.3f);
	emitter_->SetSpawnSize({ 1.0f, 0.5f, 1.0f });

	Sprite* sprite = new Sprite();
	sprite->Initialize(spriteCommon_, "resources/uvChecker.png");
	sprite->SetPosition({ 0.0f, 0.0f });
	sprites_.push_back(sprite);

	object3d_ = new Object3d();
	object3d_->Initialize(object3dCommon_);
	object3d_->SetModel("plane.obj");

	soundData_ = audio_->SoundLoadWave("resources/fanfare.wav");
}

void Game::Update() {
	// 基底クラスの更新処理
	Framework::Update();

	if (IsEndRequest()) {
		return;
	}

	// ゲーム固有の更新処理
	if (input_->TriggerKey(DIK_1)) {
		audio_->SoundPlayWave(soundData_);
	}

	ImGui::Begin("Scene Controls");
	ImGui::End();

	camera_->Update();

	emitter_->Update();
	particleManager_->Update();

	for (Sprite* sprite : sprites_) {
		sprite->Update();
	}

	object3d_->Update();
}

void Game::Draw() {
	dxCommon_->PreDraw();
	srvManager_->PreDraw();

	object3dCommon_->SetCommonRenderState();
	object3d_->Draw();

	particleManager_->Draw();

	spriteCommon_->SetCommonRenderState();
	for (Sprite* sprite : sprites_) {
		// sprite->Draw();
	}

	imguiManager_->EndFrame();

	dxCommon_->PostDraw();
}

void Game::Finalize() {
	// ゲーム固有の終了処理
	audio_->SoundUnload(&soundData_);

	for (Sprite* sprite : sprites_) {
		delete sprite;
		sprite = nullptr;
	}
	sprites_.clear();

	delete object3d_;
	object3d_ = nullptr;

	delete emitter_;
	emitter_ = nullptr;

	delete particleManager_;
	particleManager_ = nullptr;

	delete camera_;
	camera_ = nullptr;

	// 基底クラスの終了処理
	Framework::Finalize();
}