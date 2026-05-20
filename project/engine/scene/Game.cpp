#include "Game.h"
#include "BaseScene.h"
#include "TitleScene.h"
#include "GamePlayScene.h"

#include "../base/DirectXCommon.h"
#include "../base/ImGuiManager.h"
#include "../io/Input.h"
#include "../2d/Sprite.h"
#include "../2d/SpriteCommon.h"
#include "../3d/Camera.h"
#include "../3d/Object3dCommon.h"
#include "../3d/Object3d.h"
#include "../3d/SrvManager.h"
#include "../particle/ParticleCommon.h"
#include "../particle/ParticleManager.h"
#include "../particle/ParticleEmitter.h"
#include "../externals/imgui/imgui.h"

void Game::Initialize() {
	// 基底クラスの初期化処理
	Framework::Initialize();

	sceneManager_ = new SceneManager();

	BaseScene* scene = new GamePlayScene();
	sceneManager_->SetNextScene(scene);
}

void Game::Update() {
	// 基底クラスの更新処理
	Framework::Update();

	if (IsEndRequest()) {
		return;
	}

	sceneManager_->Update();

}

void Game::Draw() {
	dxCommon_->PreDraw();
	srvManager_->PreDraw();
	Object3dCommon::GetInstance()->SetCommonRenderState();

	sceneManager_->Draw();

	imguiManager_->EndFrame();

	dxCommon_->PostDraw();
}

void Game::Finalize() {

	if (sceneManager_) {
		delete sceneManager_;
		sceneManager_ = nullptr;
	}

	// 基底クラスの終了処理
	Framework::Finalize();
}