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
#include "../effect/LightningRenderer.h"
#include "../player/Player.h"
#include "../utility/EditableResourcePath.h"

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
	ParticleManager::GetInstance()->SetGpuParticleEnabled(true);
	TextureManager::GetInstance()->LoadTexture("resources/monsterBall.png");
	TextureManager::GetInstance()->LoadTexture("resources/uvChecker.png");
	ModelManager::GetInstance()->LoadModel("terrain.obj");
	ModelManager::GetInstance()->LoadModel("Cube.obj");
	ModelManager::GetInstance()->LoadModel("AnimatedCube/AnimatedCube.gltf");
	ModelManager::GetInstance()->LoadModel("human/walk.gltf");

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

	animatedCube_ = new Object3d();
	animatedCube_->Initialize(Object3dCommon::GetInstance());
	animatedCube_->SetModel("AnimatedCube/AnimatedCube.gltf");
	animatedCube_->SetTranslate({ 3.0f, 10.5f, -2.0f });
	animatedCube_->SetScale({ 0.65f, 0.65f, 0.65f });
	animatedCube_->SetAnimationLoop(true);
	animatedCube_->SetAnimationSpeed(1.0f);

	human_ = new Object3d();
	human_->Initialize(Object3dCommon::GetInstance());
	human_->SetModel("human/walk.gltf");
	human_->SetTranslate({ -2.0f, 0.0f, -2.0f });
	human_->SetScale({ 1.0f, 1.0f, 1.0f });
	human_->SetAnimationLoop(true);
	human_->SetAnimationSpeed(1.0f);
	human_->SetEnvironmentMap(
		"resources/rostock_laage_airport_4k.dds",
		0.01f
	);

	Vector3 target = human_->GetTranslate();
	camera_->SetOrbitTarget(target);

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

	object3d_->Update();
	if (plane_) {
		plane_->Update();
	}
	if (axis) {
		axis->Update();
	}
	if (animatedCube_) {
		animatedCube_->Update();
	}
	if (human_) {
		human_->Update();
	}
	if (player_) {
		player_->Update(staticColliders_);

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

	object3d_->Draw();
	//if (plane_) {
	//	plane_->Draw();
	//}
	//if (axis) {
	//	axis->Draw();
	//}
	//if (animatedCube_) {
	//	animatedCube_->Draw();
	//}
	//if (human_) {
	//	human_->Draw();
	//}
	//if (player_) {
	//	player_->Draw();
	//}
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
}

void GamePlayScene::DrawShadow()
{
	if (!lightManager_ || !shadowManager_) {
		return;
	}

	std::vector<Object3d*> shadowCasters;
	shadowCasters.reserve(4 + stageObjects_.size() + (player_ ? 1 : 0));
	shadowCasters.push_back(object3d_);
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
