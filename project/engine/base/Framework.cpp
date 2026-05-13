#include "Framework.h"

#include "../utility/Logger.h"
#include "D3DResourceLeadChecker.h"
#include "WinApp.h"
#include "DirectXCommon.h"
#include "../io/Input.h"
#include "../3d/SrvManager.h"
#include "ImGuiManager.h"
#include "../audio/Audio.h"

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

	input_ = new Input();
	input_->Initialize(winApp_);

	srvManager_ = new SrvManager();
	srvManager_->Initialize(dxCommon_);

	imguiManager_ = new ImGuiManager();
	imguiManager_->Initialize(winApp_, dxCommon_, srvManager_);

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
	imguiManager_->BeginFrame();
}
void Framework::Finalize() {
	if (imguiManager_) {
		imguiManager_->Finalize();
	}

	if (audio_) {
		audio_->Finalize();
	}

	delete audio_;
	audio_ = nullptr;

	delete imguiManager_;
	imguiManager_ = nullptr;

	delete srvManager_;
	srvManager_ = nullptr;

	delete input_;
	input_ = nullptr;

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