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
	ModelManager::GetInstance()->LoadModel("plane.obj");

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
	object3d_->SetScale({ 5.0f, 5.0f, 5.0f });

	plane_ = new Object3d();
	plane_->Initialize(Object3dCommon::GetInstance());
	plane_->SetModel("plane.obj");
	plane_->SetTranslate({ 0.0f, 7.0f, 0.0f });

	Object3dCommon::GetInstance()->SetLightDirection({ 0.0f, -1.0f, 0.0f });
	Object3dCommon::GetInstance()->SetLightColor({ 0.4f, 0.9f, 0.6f, 1.0f });
	Object3dCommon::GetInstance()->SetLightIntensity(0.2f);

	// 世界ライトの設置
	{
		Object3dCommon* object3dCommon = Object3dCommon::GetInstance();

		// PointLight：その位置から周囲へ光るライト
		Object3dCommon::PointLight pointLight{};
		pointLight.color = { 1.0f, 0.85f, 0.65f, 1.0f };
		pointLight.position = { 20.0f, 3.0f, 0.0f };
		pointLight.intensity = 2.0f;
		pointLight.radius = 8.0f;
		pointLight.decay = 1.0f;
		pointLight.enable = true;
		pointLight.padding = 0.0f;

		object3dCommon->SetPointLight(pointLight);

		// SpotLight：位置と向きを持つ、懐中電灯のようなライト
		Object3dCommon::SpotLight spotLight{};
		spotLight.color = { 0.75f, 0.85f, 1.0f, 1.0f };
		spotLight.position = { -20.0f, 5.0f, 0.0f };
		spotLight.intensity = 4.0f;

		// ライトが照らす向き
		// この例では {0,5,-6} から原点方向を見る感じ
		spotLight.direction = { 0.0f, -0.65f, 0.76f };

		spotLight.distance = 15.0f;
		spotLight.decay = 1.0f;

		// 外側の角度 cos(45度) くらい
		spotLight.cosAngle = 0.70710678f;

		// 内側の角度 cos(30度) くらい
		// cosFalloffStart の方が cosAngle より大きい値になる
		spotLight.cosFalloffStart = 0.86602540f;

		spotLight.enable = true;

		object3dCommon->SetSpotLight(spotLight);
	}

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

	delete plane_;
	plane_ = nullptr;

	delete emitter_;
	emitter_ = nullptr;

	delete camera_;
	camera_ = nullptr;
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

	for (Sprite* sprite : sprites_) {
		sprite->Update();
	}

	object3d_->Update();
	plane_->Update();
}

void GamePlayScene::Draw()
{
	object3d_->Draw();
	plane_->Draw();
	//ParticleManager::GetInstance()->Draw();
}
