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
	spriteCommon_ = new SpriteCommon();
	spriteCommon_->Initialize(dxCommon_);

	camera_ = new Camera();
	camera_->SetRotate({ 0.0f,0.0f,0.0f });
	camera_->SetTranslate({ 0.0f,0.0f,-10.0f });

	object3dCommon_ = new Object3dCommon();
	object3dCommon_->Initialize(dxCommon_);
	object3dCommon_->SetDefaultCamera(camera_);

	TextureManager::GetInstance()->Initialize(dxCommon_, srvManager_);
	TextureManager::GetInstance()->LoadTexture("resources/monsterBall.png");
	TextureManager::GetInstance()->LoadTexture("resources/uvChecker.png");

	ModelManager::GetInstance()->Initialize(dxCommon_);
	ModelManager::GetInstance()->LoadModel("plane.obj");

	particleCommon_ = new ParticleCommon();
	particleCommon_->Initialize(dxCommon_);
	particleCommon_->SetDefaultCamera(camera_);

	particleManager_ = new ParticleManager();
	particleManager_->Initialize(particleCommon_, srvManager_);
	particleManager_->SetCamera(camera_);
	particleManager_->CreateParticleGroup("particle", "resources/circle.png");

	emitter_ = new ParticleEmitter();
	emitter_->Initialize(particleManager_,"particle");
	emitter_->SetTranslate({ 0.0f, 7.0f, 0.0f });

	object3d_ = new Object3d();
	object3d_->Initialize(object3dCommon_);
	object3d_->SetModel("plane.obj");
	object3d_->SetTranslate({ 0.0f, 0.0f, 0.0f });

	 soundData_ = audio_->SoundLoadWave("resources/fanfare.wav");
}

void Game::Update() {
	// 基底クラスの更新処理
	Framework::Update();

	if (IsEndRequest()) {
		return;
	}

	if (input_->TriggerKey(DIK_1) && soundData_.pBuffer != nullptr) {
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

	delete particleCommon_;
	particleCommon_ = nullptr;

	TextureManager::GetInstance()->Finalize();
	ModelManager::GetInstance()->Finalize();

	delete object3dCommon_;
	object3dCommon_ = nullptr;

	delete spriteCommon_;
	spriteCommon_ = nullptr;

	delete camera_;
	camera_ = nullptr;

	// 基底クラスの終了処理
	Framework::Finalize();
}