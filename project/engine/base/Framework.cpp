#include "Framework.h"

#include "../utility/Logger.h"
#include "D3DResourceLeadChecker.h"
#include "WinApp.h"
#include "DirectXCommon.h"
#include "ImGuiManager.h"
#include "../io/Input.h"
#include "../3d/SrvManager.h"
#include "../3d/ModelManager.h"
#include "../3d/Object3dCommon.h"
#include "../2d/TextureManager.h"
#include "../2d/SpriteCommon.h"
#include "../particle/ParticleCommon.h"
#include "../particle/ParticleManager.h"
#include "../audio/Audio.h"
#include "../debug/DebugRenderer.h"
#include "../scene/AbstractSceneFactory.h"

void Framework::Run() {
	// ゲームの初期化
	Initialize();

	while (true) // ゲームループ
	{
		// 毎フレーム更新
		Update();

		// 終了リクエストが来たら抜ける
		if (IsEndRequest()) {
			break;
		}

		// 描画
		Draw();
	}

	// ゲームの終了
	Finalize();
}

void Framework::Initialize() {
	Logger::Initialize();

	checker_ = new D3DResourceLeadChecker();

	winApp_ = new WinApp();
	winApp_->Initialize();

	dxCommon_ = new DirectXCommon();
	dxCommon_->Initialize(winApp_);

	ShowWindow(winApp_->GetHwnd(), SW_SHOW);
	srvManager_ = new SrvManager();
	srvManager_->Initialize(dxCommon_);

	Object3dCommon::GetInstance()->Initialize(dxCommon_);
	TextureManager::GetInstance()->Initialize(dxCommon_, srvManager_);
	ModelManager::GetInstance()->Initialize(dxCommon_);
	SpriteCommon::GetInstance()->Initialize(dxCommon_);
	particleCommon_ = ParticleCommon::GetInstance();
	particleCommon_->Initialize(dxCommon_);
	ParticleManager::GetInstance()->Initialize(particleCommon_, srvManager_);

	input_ = Input::GetInstance();
	input_->GetInstance()->Initialize(winApp_);

#if defined(_DEBUG) || defined(DEVELOPMENT)
	DebugRenderer::GetInstance()->Initialize(dxCommon_);

	imguiManager_ = new ImGuiManager();
	imguiManager_->Initialize(winApp_, dxCommon_, srvManager_);
#endif

	audio_ = new Audio();
	audio_->Initialize();

	endRequest_ = false;
}

void Framework::Update() {
	if (winApp_->ProcessMessage()) {
		endRequest_ = true;
		return;
	}

	input_->Update();
	if (input_->TriggerKey(DIK_F11)) {
		winApp_->ToggleFullscreen();

		dxCommon_->ResizeSwapChain(
			winApp_->GetClientWidth(),
			winApp_->GetClientHeight()
		);
	}
#if defined(_DEBUG) || defined(DEVELOPMENT)
	imguiManager_->BeginFrame();
#endif
}
void Framework::Finalize() {
	delete sceneFactory_;
	sceneFactory_ = nullptr;

#if defined(_DEBUG) || defined(DEVELOPMENT)
	DebugRenderer::GetInstance()->Finalize();

	if (imguiManager_) {
		imguiManager_->Finalize();
	}
#endif

	if (audio_) {
		audio_->Finalize();
	}

	TextureManager::GetInstance()->Finalize();
	ModelManager::GetInstance()->Finalize();
	SpriteCommon::DeleteInstance();
	ParticleManager::GetInstance()->Reset();
	ParticleManager::DeleteInstance();

	if (particleCommon_) {
		particleCommon_->ResetState();
	}
	ParticleCommon::DeleteInstance();
	particleCommon_ = nullptr;

	delete audio_;
	audio_ = nullptr;

#if defined(_DEBUG) || defined(DEVELOPMENT)
	delete imguiManager_;
	imguiManager_ = nullptr;
#endif

	delete srvManager_;
	srvManager_ = nullptr;

	Input::GetInstance()->Finalize();

	delete dxCommon_;
	dxCommon_ = nullptr;

	if (winApp_) {
		winApp_->Finalize();
	}
	delete winApp_;
	winApp_ = nullptr;

	delete checker_;
	checker_ = nullptr;

	Logger::Finalize();
}
