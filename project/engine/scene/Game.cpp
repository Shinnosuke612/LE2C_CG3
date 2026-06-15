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
	postProcessRenderTarget_ = new SceneRenderTarget();
	postProcessRenderTarget_->Initialize(
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

	ImGui::Begin("Post Process");
	ImGui::RadioButton("None", &postProcessEffect_, 0);
	ImGui::SameLine();
	ImGui::RadioButton("Grayscale", &postProcessEffect_, 1);
	ImGui::SameLine();
	ImGui::RadioButton("Vignette", &postProcessEffect_, 2);
	ImGui::SameLine();
	ImGui::RadioButton("Box Blur", &postProcessEffect_, 3);
	if (postProcessEffect_ == 2) {
		ImGui::SliderFloat("Scale", &vignetteScale_, 0.0f, 32.0f);
		ImGui::SliderFloat("Power", &vignettePower_, 0.05f, 4.0f);
		ImGui::SliderFloat("Intensity", &vignetteIntensity_, 0.0f, 1.0f);
	}
	else if (postProcessEffect_ == 3) {
		const char* kernelNames[] = { "3 x 3", "5 x 5" };
		int kernelIndex = boxBlurKernelSize_ == 5 ? 1 : 0;
		if (ImGui::Combo(
			"Kernel",
			&kernelIndex,
			kernelNames,
			IM_ARRAYSIZE(kernelNames)
		)) {
			boxBlurKernelSize_ = kernelIndex == 1 ? 5 : 3;
		}
		ImGui::SliderFloat(
			"Strength",
			&boxBlurStrength_,
			0.0f,
			1.0f
		);
	}
	ImGui::End();

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
		postProcessRenderTarget_->GetSrvGpuHandle(),
		postProcessRenderTarget_->GetWidth(),
		postProcessRenderTarget_->GetHeight(),
		sceneManager_->GetCurrentSceneName().c_str()
	);
	sceneRenderTarget_->Resize(
		imguiManager_->GetSceneViewWidth(),
		imguiManager_->GetSceneViewHeight()
	);
	postProcessRenderTarget_->Resize(
		imguiManager_->GetSceneViewWidth(),
		imguiManager_->GetSceneViewHeight()
	);
#else
	sceneRenderTarget_->Resize(
		dxCommon_->GetClientWidth(),
		dxCommon_->GetClientHeight()
	);
	postProcessRenderTarget_->Resize(
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

	postProcessRenderTarget_->Begin();
	srvManager_->PreDraw();
	FullscreenCopy::Parameters parameters{};
	parameters.vignetteScale = vignetteScale_;
	parameters.vignettePower = vignettePower_;
	parameters.vignetteIntensity = vignetteIntensity_;
	parameters.blurStrength = boxBlurStrength_;
	parameters.blurRadius = boxBlurKernelSize_ == 5 ? 2u : 1u;
	fullscreenCopy_->SetParameters(parameters);
	FullscreenCopy::Effect effect = FullscreenCopy::Effect::kCopy;
	if (postProcessEffect_ == 1) {
		effect = FullscreenCopy::Effect::kGrayscale;
	}
	else if (postProcessEffect_ == 2) {
		effect = FullscreenCopy::Effect::kVignette;
	}
	else if (postProcessEffect_ == 3) {
		effect = FullscreenCopy::Effect::kBoxBlur;
	}
	fullscreenCopy_->Draw(
		sceneRenderTarget_->GetSrvGpuHandle(),
		effect
	);
	postProcessRenderTarget_->End();

	dxCommon_->PreDraw();
	srvManager_->PreDraw();
	fullscreenCopy_->Draw(
		postProcessRenderTarget_->GetSrvGpuHandle()
	);
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

	delete postProcessRenderTarget_;
	postProcessRenderTarget_ = nullptr;

	if (sceneManager_) {
		delete sceneManager_;
		sceneManager_ = nullptr;
	}

	// 基底クラスの終了処理
	Framework::Finalize();
}
