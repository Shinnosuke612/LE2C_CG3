#include "TitleScene.h"
#include "../externals/imgui/imgui.h"
#include "SceneManager.h"
#include "GamePlayScene.h"

#include "../3d/Camera.h"
#include "../3d/Object3dCommon.h"
#include "../particle/ParticleManager.h"
#include "../particle/ParticleEmitter.h"
#include "../io/Input.h"
#include "../particle/ParticleEffectResource.h"

void TitleScene::Initialize()
{
	camera_ = new Camera();
	camera_->SetOrbitMode(true);
	camera_->SetOrbitTarget({ 0.0f, 0.0f, 0.0f });
	camera_->SetOrbitDistance(10.0f);
	camera_->SetOrbitAngle(0.0f, 0.0f);
	camera_->Update();

	Object3dCommon::GetInstance()->SetDefaultCamera(camera_);
	ParticleManager::GetInstance()->SetCamera(camera_);

	ParticleEffectDesc coreBurstEffect{};

	if (ParticleEffectResource::Load(
		"resources/particles/core_burst.json",
		coreBurstEffect
	)) {
		ParticleEmitter* emitter =
			ParticleEffectResource::CreateEmitter(coreBurstEffect);

		emitters_.push_back(emitter);
	}

	ParticleEffectResource::Load(
		"resources/particles/core_burst.json",
		editingEffect_
	);

	particleEffectEditor_.Initialize(
		editingEffect_,
		"resources/particles/core_burst.json"
	);

	previewEmitter_ = ParticleEffectResource::CreateEmitter(editingEffect_);

}

void TitleScene::Finalize()
{
	for (ParticleEmitter* emitter : emitters_) {
		delete emitter;
	}
	emitters_.clear();

	delete camera_;
	camera_ = nullptr;

	delete previewEmitter_;
	previewEmitter_ = nullptr;
}

void TitleScene::Update()
{
	if (Input::GetInstance()->TriggerKey(DIK_RETURN)) {
		BaseScene* scene = new GamePlayScene;
		sceneManager_->SetNextScene(scene);
	}

#if defined(_DEBUG) || defined(DEVELOPMENT)
	ImGui::Begin("Title Scene");
	ImGui::Text("TitleScene");
	ImGui::Text("Particles are running.");
	ImGui::End();

	camera_->DrawImGui("Main Camera");
#endif
	camera_->Update();

#if defined(_DEBUG) || defined(DEVELOPMENT)
	particleEffectEditor_.DrawImGui(editingEffect_, previewEmitter_);
#endif

	if (previewEmitter_) {
		previewEmitter_->Update();
	}

	for (ParticleEmitter* emitter : emitters_) {
		emitter->Update();
	}

	ParticleManager::GetInstance()->Update();
}

void TitleScene::Draw()
{
	ParticleManager::GetInstance()->Draw();
}
