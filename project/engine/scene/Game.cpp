#include "Game.h"
#include "SceneFactory.h"

#include "../base/DirectXCommon.h"
#include "../base/FullscreenCopy.h"
#include "../base/ImGuiManager.h"
#include "../base/SceneRenderTarget.h"
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
#include "../debug/DebugRenderer.h"
#include "../externals/imgui/imgui.h"

void Game::Initialize() {
	// 基底クラスの初期化処理
	Framework::Initialize();

	sceneManager_ = new SceneManager();
	sceneFactory_ = new SceneFactory();
	sceneManager_->SetSceneFactory(sceneFactory_);
	sceneManager_->ChangeScene("TITLE");

	sceneRenderTarget_ = new SceneRenderTarget();
	sceneRenderTarget_->Initialize(
		dxCommon_,
		srvManager_,
		dxCommon_->GetClientWidth(),
		dxCommon_->GetClientHeight()
	);
	fullscreenCopy_ = new FullscreenCopy();
	fullscreenCopy_->Initialize(dxCommon_);
}

void Game::Update() {
	// 基底クラスの更新処理
	Framework::Update();

	if (IsEndRequest()) {
		return;
	}

#if defined(_DEBUG) || defined(DEVELOPMENT)
	DebugRenderer::GetInstance()->Clear();

	Camera* editorCamera =
		Object3dCommon::GetInstance()->GetDefaultCamera();
	if (
		editorCamera &&
		imguiManager_->GetSceneViewHeight() > 0
	) {
		editorCamera->SetAspectRatio(
			static_cast<float>(imguiManager_->GetSceneViewWidth()) /
			static_cast<float>(imguiManager_->GetSceneViewHeight())
		);
	}
#endif

	sceneManager_->Update();

}

void Game::Draw() {
	// 影描画でもスキニングパレットSRVを使うので先に必要
	srvManager_->PreDraw();

	sceneManager_->DrawShadow();

#if defined(_DEBUG) || defined(DEVELOPMENT)
	imguiManager_->DrawEditorWorkspace(
		sceneRenderTarget_->GetSrvGpuHandle(),
		sceneRenderTarget_->GetWidth(),
		sceneRenderTarget_->GetHeight(),
		sceneManager_->GetCurrentSceneName().c_str()
	);
	sceneRenderTarget_->Resize(
		imguiManager_->GetSceneViewWidth(),
		imguiManager_->GetSceneViewHeight()
	);
#else
	sceneRenderTarget_->Resize(
		dxCommon_->GetClientWidth(),
		dxCommon_->GetClientHeight()
	);
#endif

	sceneRenderTarget_->Begin();
	srvManager_->PreDraw();
	Object3dCommon::GetInstance()->SetCommonRenderState();
	sceneManager_->Draw();
#if defined(_DEBUG) || defined(DEVELOPMENT)
	DebugRenderer::GetInstance()->Draw(
		Object3dCommon::GetInstance()->GetDefaultCamera()
	);
#endif
	sceneRenderTarget_->End();

	dxCommon_->PreDraw();
	srvManager_->PreDraw();
	fullscreenCopy_->Draw(sceneRenderTarget_->GetSrvGpuHandle());
#if defined(_DEBUG) || defined(DEVELOPMENT)
	imguiManager_->EndFrame();
#endif

	dxCommon_->PostDraw();
}

void Game::Finalize() {

	delete fullscreenCopy_;
	fullscreenCopy_ = nullptr;

	delete sceneRenderTarget_;
	sceneRenderTarget_ = nullptr;

	if (sceneManager_) {
		delete sceneManager_;
		sceneManager_ = nullptr;
	}

	// 基底クラスの終了処理
	Framework::Finalize();
}
