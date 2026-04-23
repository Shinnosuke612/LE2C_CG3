#define _USE_MATH_DEFINES
#include "Game.h"

#include <cmath>
#include <iostream>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <chrono>
#include <string>
#include <format>
#include <cassert>

#include <vector>

// 入力
#define DIRECTINPUT_VERSION 0x0800
#include <dinput.h>

#pragma comment(lib,"dinput8.lib")
#pragma comment(lib,"dxguid.lib")

#include "engine/io/Input.h"
#include "engine/base/WinApp.h"
#include "engine/base/DirectXCommon.h"
#include "engine/base/D3DResourceLeadChecker.h"
#include "engine/2d/SpriteCommon.h"
#include "engine/2d/Sprite.h"
#include "engine/2d/TextureManager.h"
#include "engine/3d/Object3dCommon.h"
#include "engine/3d/Object3d.h"
#include "engine/3d/Model.h"
#include "engine/3d/ModelManager.h"
#include "engine/3d/Camera.h"
#include "engine/3d/SrvManager.h"
#include "engine/3d/ParticleCommon.h"
#include "engine/3d/ParticleManager.h"
#include "engine/3d/ParticleEmitter.h"
#include "engine/base/ImGuiManager.h"

void Game::Initialize() {
	checker_ = new D3DResourceLeadChecker();

	winApp_ = new WinApp();
	winApp_->Initialize();

	Logger::Initialize();

	dxCommon_ = new DirectXCommon();
	dxCommon_->Initialize(winApp_);

	ShowWindow(winApp_->GetHwnd(), SW_SHOW);

	input_ = new Input();
	input_->Initialize(winApp_);

	spriteCommon_ = new SpriteCommon();
	spriteCommon_->Initialize(dxCommon_);

	camera_ = new Camera();
	camera_->SetRotate({ 0.0f,0.0f,0.0f });
	camera_->SetTranslate({ 0.0f,0.0f,-10.0f });

	object3dCommon_ = new Object3dCommon();
	object3dCommon_->Initialize(dxCommon_);
	object3dCommon_->SetDefaultCamera(camera_);

	srvManager_ = new SrvManager();
	srvManager_->Initialize(dxCommon_);

	imguiManager_ = new ImGuiManager();
	imguiManager_->Initialize(winApp_, dxCommon_, srvManager_);

	audio_ = new Audio();
	audio_->Initialize();

	TextureManager::GetInstance()->Initialize(dxCommon_, srvManager_);

	ModelManager::GetInstance()->Initialize(dxCommon_);

	particleCommon_ = new ParticleCommon();
	particleCommon_->Initialize(dxCommon_);
	particleCommon_->SetDefaultCamera(camera_);

	particleManager_ = new ParticleManager();
	particleManager_->Initialize(particleCommon_, srvManager_);

	isEndRequest_ = false;
}

void Game::Update() {
	if (winApp_->ProcessMessage()) {
		isEndRequest_ = true;
		return;
	}

	imguiManager_->BeginFrame();

	input_->Update();

	camera_->Update();

	particleManager_->Update();
}

void Game::Draw() {
	if (isEndRequest_) {
		return;
	}

	dxCommon_->PreDraw();
	srvManager_->PreDraw();

	object3dCommon_->SetCommonRenderState();

	particleManager_->Draw();

	spriteCommon_->SetCommonRenderState();

	imguiManager_->EndFrame();

	dxCommon_->PostDraw();
}

void Game::Finalize() {
	if (imguiManager_) {
		imguiManager_->Finalize();
	}

	if (audio_) {
		audio_->Finalize();
	}

	TextureManager::GetInstance()->Finalize();
	ModelManager::GetInstance()->Finalize();

	delete particleManager_;
	particleManager_ = nullptr;

	delete particleCommon_;
	particleCommon_ = nullptr;

	delete imguiManager_;
	imguiManager_ = nullptr;

	delete srvManager_;
	srvManager_ = nullptr;

	delete object3dCommon_;
	object3dCommon_ = nullptr;

	delete camera_;
	camera_ = nullptr;

	delete spriteCommon_;
	spriteCommon_ = nullptr;

	delete input_;
	input_ = nullptr;

	delete audio_;
	audio_ = nullptr;

	if (winApp_) {
		winApp_->Finalize();
	}
	delete winApp_;
	winApp_ = nullptr;

	delete dxCommon_;
	dxCommon_ = nullptr;

	delete checker_;
	checker_ = nullptr;
}