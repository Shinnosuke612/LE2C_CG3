#include "GamePlayScene.h"

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

#include "../externals/imgui/imgui.h"

void GamePlayScene::Initialize()
{
	// ゲーム固有の初期化


	camera_ = new Camera();
	camera_->SetRotate({ 0.0f,0.0f,0.0f });
	camera_->SetTranslate({ 0.0f,0.0f,-10.0f });

	Object3dCommon::GetInstance()->SetDefaultCamera(camera_);
	ParticleManager::GetInstance()->SetCamera(camera_);

	TextureManager::GetInstance()->LoadTexture("resources/monsterBall.png");
	TextureManager::GetInstance()->LoadTexture("resources/uvChecker.png");
	ModelManager::GetInstance()->LoadModel("plane.obj");

	ParticleManager::GetInstance()->CreateParticleGroup("particle", "resources/circle.png");
	ParticleManager::ParticleBehavior behavior{};

	// 色変化モード
	behavior.color.mode = ParticleManager::ColorChangeMode::kRandomLoop;

	// スケール
	behavior.scale.startScaleMin = { 0.3f, 0.3f, 0.3f };
	behavior.scale.startScaleMax = { 0.3f, 0.3f, 0.3f };

	// 移動
	behavior.motion.mode = ParticleManager::MovementMode::kLinear;

	behavior.motion.linear.baseVelocity = { 0.0f, 0.0f, -10.0f };
	behavior.motion.linear.velocityRandomRange = { 2.0f, 2.0f, 0.0f };

	behavior.motion.linear.baseAcceleration = { 0.0f, 0.0f, 0.0f };
	behavior.motion.linear.accelerationRandomRange = { 0.0f, 0.0f, 0.0f };

	// 揺れ
	behavior.motion.sway.amplitude = 2.0f;
	behavior.motion.sway.frequency = 1.0f;

	// 開始色
	behavior.color.startColorMin = {
		195.0f / 255.0f,
		145.0f / 255.0f,
		67.0f / 255.0f,
		1.0f
	};

	behavior.color.startColorMax = {
		195.0f / 255.0f,
		145.0f / 255.0f,
		67.0f / 255.0f,
		1.0f
	};

	// kConstant では使われないが、後で kOverLife に変えるなら有効
	behavior.color.endColorMin = { 1.0f, 1.0f, 1.0f, 1.0f };
	behavior.color.endColorMax = { 1.0f, 1.0f, 1.0f, 1.0f };

	// kRandomLoop で使う設定。kConstant では使われない
	behavior.color.randomColorMin = { 0.2f, 0.2f, 0.2f, 1.0f };
	behavior.color.randomColorMax = { 1.0f, 1.0f, 1.0f, 1.0f };

	behavior.color.randomColorChangeIntervalMin = 0.8f;
	behavior.color.randomColorChangeIntervalMax = 0.9f;
	behavior.color.randomColorLerpSpeed = 1.0f;

	// 寿命
	behavior.life.enableLifeFade = true;
	behavior.life.fadeOutStartRatio = 0.9f;

	behavior.life.lifeTimeMin = 3.0f;
	behavior.life.lifeTimeMax = 3.0f;

	emitter_ = new ParticleEmitter();
	emitter_->Initialize(ParticleManager::GetInstance(), "particle");
	emitter_->SetTranslate({ 0.0f, 0.0f, 20.0f });
	emitter_->SetCount(10);
	emitter_->SetBehavior(behavior);
	emitter_->SetSpawnSize({ 2.0f, 2.0f, 0.0f });
	emitter_->SetFrequency(0.01f);

	object3d_ = new Object3d();
	object3d_->Initialize(Object3dCommon::GetInstance());
	object3d_->SetModel("plane.obj");
	object3d_->SetTranslate({ 0.0f, 0.0f, 0.0f });

	//soundData_ = audio_->SoundLoadWave("resources/fanfare.wav");
}

void GamePlayScene::Finalize()
{
	for (Sprite* sprite : sprites_) {
		delete sprite;
	}
	sprites_.clear();

	delete object3d_;
	object3d_ = nullptr;

	delete emitter_;
	emitter_ = nullptr;

	delete camera_;
	camera_ = nullptr;
}

void GamePlayScene::Update()
{
	//if (input_->TriggerKey(DIK_1) && soundData_.pBuffer != nullptr) {
	//	audio_->SoundPlayWave(soundData_);
	//}

	ImGui::Begin("Scene Controls");
	ImGui::End();

	camera_->Update();

	emitter_->Update();
	ParticleManager::GetInstance()->Update();

	for (Sprite* sprite : sprites_) {
		sprite->Update();
	}

	object3d_->Update();
}

void GamePlayScene::Draw()
{
	ParticleManager::GetInstance()->Draw();
}
