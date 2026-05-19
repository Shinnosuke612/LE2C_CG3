#include "TitleScene.h"

#include "../3d/Camera.h"
#include "../3d/Object3dCommon.h"
#include "../particle/ParticleManager.h"
#include "../particle/ParticleEmitter.h"

#include "../externals/imgui/imgui.h"

void TitleScene::Initialize()
{
	camera_ = new Camera();
	camera_->SetRotate({ 0.0f, 0.0f, 0.0f });
	camera_->SetTranslate({ 0.0f, 0.0f, -10.0f });
	camera_->Update();

	Object3dCommon::GetInstance()->SetDefaultCamera(camera_);
	ParticleManager::GetInstance()->SetCamera(camera_);

	// 下から上へ走る細長い光
	ParticleManager::GetInstance()->CreateParticleGroup(
		"titleLightStreak",
		"resources/circle.png"
	);

	ParticleManager::ParticleBehavior lightStreakBehavior{};

	// 寿命
	lightStreakBehavior.life.lifeTimeMin = 0.45f;
	lightStreakBehavior.life.lifeTimeMax = 0.9f;
	lightStreakBehavior.life.enableLifeFade = true;
	lightStreakBehavior.life.fadeOutStartRatio = 0.45f;

	// 細長い光に見せる
	lightStreakBehavior.scale.startScaleMin = { 0.035f, 0.75f, 0.035f };
	lightStreakBehavior.scale.startScaleMax = { 0.075f, 1.8f, 0.075f };

	// まっすぐ上へ高速移動
	lightStreakBehavior.motion.mode = ParticleManager::MovementMode::kLinear;

	lightStreakBehavior.motion.linear.baseVelocity = { 0.0f, 18.0f, 0.0f };
	lightStreakBehavior.motion.linear.velocityRandomRange = { 0.0f, 5.0f, 0.0f };

	lightStreakBehavior.motion.linear.baseAcceleration = { 0.0f, 1.2f, 0.0f };
	lightStreakBehavior.motion.linear.accelerationRandomRange = { 0.0f, 0.8f, 0.0f };

	// 揺れなし
	lightStreakBehavior.motion.sway.amplitude = 0.0f;
	lightStreakBehavior.motion.sway.frequency = 0.0f;

	// 青白い発光色
	lightStreakBehavior.color.mode = ParticleManager::ColorChangeMode::kOverLife;

	lightStreakBehavior.color.startColorMin = { 0.45f, 0.85f, 1.0f, 1.0f };
	lightStreakBehavior.color.startColorMax = { 0.85f, 1.0f, 1.0f, 1.0f };

	lightStreakBehavior.color.endColorMin = { 0.1f, 0.35f, 1.0f, 0.2f };
	lightStreakBehavior.color.endColorMax = { 0.4f, 0.85f, 1.0f, 0.35f };

	ParticleEmitter* lightStreakEmitter = new ParticleEmitter();
	lightStreakEmitter->Initialize(
		ParticleManager::GetInstance(),
		"titleLightStreak"
	);

	// 画面下側から発生
	lightStreakEmitter->SetTranslate({ 0.0f, -5.5f, 0.0f });

	// 横幅いっぱいに散らす。Zを0にすると奥行きブレしない
	lightStreakEmitter->SetSpawnSize({ 14.0f, 0.2f, 0.0f });

	// たくさん、高頻度
	lightStreakEmitter->SetCount(10);
	lightStreakEmitter->SetFrequency(0.015f);

	lightStreakEmitter->SetBehavior(lightStreakBehavior);
	//emitters_.push_back(lightStreakEmitter);


	// 中心へぐるぐる吸い込まれる星粒
	ParticleManager::GetInstance()->CreateParticleGroup(
		"titleVortexStar",
		"resources/circle.png"
	);

	ParticleManager::ParticleBehavior vortexStarBehavior{};

	// 寿命
	vortexStarBehavior.life.lifeTimeMin = 0.7f;
	vortexStarBehavior.life.lifeTimeMax = 0.7f;
	vortexStarBehavior.life.enableLifeFade = true;
	vortexStarBehavior.life.fadeOutStartRatio = 0.75f;

	// 小さめの星粒
	vortexStarBehavior.scale.startScaleMin = { 0.16f, 0.16f, 0.16f };
	vortexStarBehavior.scale.startScaleMax = { 0.16f, 0.16f, 0.16f };

	// 渦に吸い込まれる動き
	vortexStarBehavior.motion.mode = ParticleManager::MovementMode::kVortexInward;

	vortexStarBehavior.motion.vortex.center = { 0.0f, 0.0f, 0.0f };

	vortexStarBehavior.motion.vortex.angularSpeedMin = 1.0f;
	vortexStarBehavior.motion.vortex.angularSpeedMax = 1.0f;

	vortexStarBehavior.motion.vortex.inwardSpeedMin = -3.8f;
	vortexStarBehavior.motion.vortex.inwardSpeedMax = -3.8f;

	vortexStarBehavior.motion.vortex.verticalSpeedMin = 0.45f;
	vortexStarBehavior.motion.vortex.verticalSpeedMax = 0.45f;

	// 渦側も揺れなし。回転自体が動きになる
	vortexStarBehavior.motion.sway.amplitude = 0.0f;
	vortexStarBehavior.motion.sway.frequency = 0.0f;

	// 星っぽく、青白から紫へ
	vortexStarBehavior.color.mode = ParticleManager::ColorChangeMode::kOverLife;

	vortexStarBehavior.color.startColorMin = { 0.65f, 0.8f, 1.0f, 1.0f };
	vortexStarBehavior.color.startColorMax = { 1.0f, 1.0f, 1.0f, 1.0f };

	vortexStarBehavior.color.endColorMin = { 0.25f, 0.05f, 0.7f, 0.0f };
	vortexStarBehavior.color.endColorMax = { 0.65f, 0.25f, 1.0f, 0.0f };

	ParticleEmitter* vortexStarEmitter = new ParticleEmitter();
	vortexStarEmitter->Initialize(
		ParticleManager::GetInstance(),
		"titleVortexStar"
	);

	// 中心を基準に広い範囲へ発生させる
	vortexStarEmitter->SetTranslate({ 0.0f, 0.0f, 0.0f });

	// XZに広く出すと、中心へ吸い込まれる渦になる
	vortexStarEmitter->SetSpawnSize({ 9.0f, 3.5f, 9.0f });

	vortexStarEmitter->SetCount(4);
	vortexStarEmitter->SetFrequency(0.05f);

	vortexStarEmitter->SetBehavior(vortexStarBehavior);
	//emitters_.push_back(vortexStarEmitter);

	ParticleManager::GetInstance()->CreateParticleGroup(
		"titleCoreBurst",
		"resources/circleEntity.png"
	);

	ParticleManager::ParticleBehavior coreBurstBehavior{};

	// 寿命
	coreBurstBehavior.life.lifeTimeMin = 1.2f;
	coreBurstBehavior.life.lifeTimeMax = 1.8f;
	coreBurstBehavior.life.enableLifeFade = true;
	coreBurstBehavior.life.fadeOutStartRatio = 0.65f;

	// 現状は開始スケールのみ
	// だんだん小さくする機能をまだ入れていない場合、ここは最初の大きさだけになる
	coreBurstBehavior.scale.startScaleMin = { 4.0f, 4.0f, 4.0f };
	coreBurstBehavior.scale.startScaleMax = { 4.0f, 4.0f, 4.0f };
	coreBurstBehavior.scale.enableScaleOverLife = true;
	coreBurstBehavior.scale.endScaleMin = { 0.00f, 0.00f, 0.00f };
	coreBurstBehavior.scale.endScaleMax = { 0.00f, 0.00f, 0.00f };


	// Z軸まわりに回転しながら外へ広がる
	coreBurstBehavior.motion.mode = ParticleManager::MovementMode::kVortexInward;
	coreBurstBehavior.motion.vortex.axis = ParticleManager::VortexAxis::kZ;
	coreBurstBehavior.motion.vortex.center = { 0.0f, 0.0f, 0.0f };

	// 回転速度
	coreBurstBehavior.motion.vortex.angularSpeedMin = 0.5f;
	coreBurstBehavior.motion.vortex.angularSpeedMax = 0.5f;

	// マイナスにすると中心から外へ離れていく
	coreBurstBehavior.motion.vortex.inwardSpeedMin = -1.8f;
	coreBurstBehavior.motion.vortex.inwardSpeedMax = -1.2f;

	// Z方向には動かさない
	coreBurstBehavior.motion.vortex.verticalSpeedMin = 0.0f;
	coreBurstBehavior.motion.vortex.verticalSpeedMax = 0.0f;

	// 揺れなし
	coreBurstBehavior.motion.sway.amplitude = 0.0f;
	coreBurstBehavior.motion.sway.frequency = 0.0f;

	// 黒に近い紫で統一
	coreBurstBehavior.color.mode = ParticleManager::ColorChangeMode::kOverLife;

	// 出現時：暗い紫、少しだけ発光感
	coreBurstBehavior.color.startColorMin = { 0.0f, 0.00f, 0.0f, 1.0f };
	coreBurstBehavior.color.startColorMax = { 0.0f, 0.00f, 0.0f, 1.0f };

	// 消える時：さらに黒紫へ
	coreBurstBehavior.color.endColorMin = { 0.00f, 0.00f, 0.0f, 1.0f };
	coreBurstBehavior.color.endColorMax = { 0.00f, 0.00f, 0.0f, 1.0f };

	ParticleEmitter* coreBurstEmitter = new ParticleEmitter();
	coreBurstEmitter->Initialize(
		ParticleManager::GetInstance(),
		"titleCoreBurst"
	);

	// 中心から発生
	coreBurstEmitter->SetTranslate({ 0.0f, 0.0f, 0.0f });

	// 完全に0だと全粒子の初期角度が同じになりやすいので、少しだけ広げる
	coreBurstEmitter->SetSpawnSize({ 0.15f, 0.15f, 0.0f });

	coreBurstEmitter->SetCount(12);
	coreBurstEmitter->SetFrequency(0.04f);
	coreBurstEmitter->SetBehavior(coreBurstBehavior);

	emitters_.push_back(coreBurstEmitter);

}

void TitleScene::Finalize()
{
	for (ParticleEmitter* emitter : emitters_) {
		delete emitter;
	}
	emitters_.clear();

	delete camera_;
	camera_ = nullptr;
}

void TitleScene::Update()
{
	ImGui::Begin("Title Scene");
	ImGui::Text("TitleScene");
	ImGui::Text("Particles are running.");
	ImGui::End();

	camera_->Update();

	for (ParticleEmitter* emitter : emitters_) {
		emitter->Update();
	}

	ParticleManager::GetInstance()->Update();
}

void TitleScene::Draw()
{
	ParticleManager::GetInstance()->Draw();
}