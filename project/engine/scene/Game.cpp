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
	for (SceneRenderTarget*& renderTarget : postProcessRenderTargets_) {
		renderTarget = new SceneRenderTarget();
		renderTarget->Initialize(
			dxCommon_,
			srvManager_,
			dxCommon_->GetClientWidth(),
			dxCommon_->GetClientHeight()
		);
	}
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

	ImGui::Begin("Post Process Stack");
	ImGui::Text("Active: %d", GetEnabledPostEffectCount());
	ImGui::SameLine();
	if (ImGui::SmallButton("Disable All")) {
		grayscaleEnabled_ = false;
		vignetteEnabled_ = false;
		boxBlurEnabled_ = false;
		gaussianBlurEnabled_ = false;
		radialBlurEnabled_ = false;
		outlineEnabled_ = false;
	}
	ImGui::TextDisabled("Applied from top to bottom");
	ImGui::Separator();

	ImGui::BeginChild("EffectStack", ImVec2(0.0f, 0.0f), true);

	ImGui::PushID("Grayscale");
	ImGui::Checkbox("##Enabled", &grayscaleEnabled_);
	ImGui::SameLine();
	ImGui::TextUnformatted("Grayscale");
	ImGui::PopID();
	ImGui::Separator();

	ImGui::PushID("Vignette");
	ImGui::Checkbox("##Enabled", &vignetteEnabled_);
	ImGui::SameLine();
	if (ImGui::TreeNodeEx(
		"Vignette",
		ImGuiTreeNodeFlags_SpanAvailWidth
	)) {
		ImGui::SliderFloat("Scale", &vignetteScale_, 0.0f, 32.0f);
		ImGui::SliderFloat("Power", &vignettePower_, 0.05f, 4.0f);
		ImGui::SliderFloat(
			"Intensity",
			&vignetteIntensity_,
			0.0f,
			1.0f
		);
		ImGui::TreePop();
	}
	ImGui::PopID();
	ImGui::Separator();

	ImGui::PushID("BoxBlur");
	ImGui::Checkbox("##Enabled", &boxBlurEnabled_);
	ImGui::SameLine();
	if (ImGui::TreeNodeEx(
		"Box Blur",
		ImGuiTreeNodeFlags_SpanAvailWidth
	)) {
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
		ImGui::TreePop();
	}
	ImGui::PopID();
	ImGui::Separator();

	ImGui::PushID("GaussianBlur");
	ImGui::Checkbox("##Enabled", &gaussianBlurEnabled_);
	ImGui::SameLine();
	if (ImGui::TreeNodeEx(
		"Gaussian Blur",
		ImGuiTreeNodeFlags_SpanAvailWidth
	)) {
		const char* kernelNames[] = { "3 x 3", "5 x 5" };
		int kernelIndex = gaussianBlurKernelSize_ == 5 ? 1 : 0;
		if (ImGui::Combo(
			"Kernel",
			&kernelIndex,
			kernelNames,
			IM_ARRAYSIZE(kernelNames)
		)) {
			gaussianBlurKernelSize_ = kernelIndex == 1 ? 5 : 3;
		}
		ImGui::SliderFloat(
			"Sigma",
			&gaussianBlurSigma_,
			0.1f,
			5.0f
		);
		ImGui::SliderFloat(
			"Strength",
			&gaussianBlurStrength_,
			0.0f,
			1.0f
		);
		ImGui::TreePop();
	}
	ImGui::PopID();
	ImGui::Separator();

	ImGui::PushID("RadialBlur");
	ImGui::Checkbox("##Enabled", &radialBlurEnabled_);
	ImGui::SameLine();
	if (ImGui::TreeNodeEx(
		"Radial Blur",
		ImGuiTreeNodeFlags_SpanAvailWidth
	)) {
		ImGui::DragFloat2(
			"Center",
			radialBlurCenter_,
			0.005f,
			0.0f,
			1.0f,
			"%.3f"
		);
		ImGui::SliderFloat(
			"Blur Width",
			&radialBlurWidth_,
			0.0f,
			0.1f,
			"%.4f"
		);
		ImGui::SliderInt(
			"Samples",
			&radialBlurSamples_,
			2,
			32
		);
		ImGui::TreePop();
	}
	ImGui::PopID();
	ImGui::Separator();

	ImGui::PushID("Outline");
	ImGui::Checkbox("##Enabled", &outlineEnabled_);
	ImGui::SameLine();
	if (ImGui::TreeNodeEx(
		"Outline",
		ImGuiTreeNodeFlags_SpanAvailWidth
	)) {
		ImGui::TextUnformatted("Sources");
		ImGui::Checkbox("Luminance", &outlineLuminanceEnabled_);
		ImGui::SameLine();
		ImGui::Checkbox("Depth", &outlineDepthEnabled_);
		if (!outlineLuminanceEnabled_ && !outlineDepthEnabled_) {
			ImGui::TextDisabled("Enable at least one source");
		}

		ImGui::SeparatorText("Detection");
		if (outlineLuminanceEnabled_) {
			ImGui::SliderFloat(
				"Luminance Weight",
				&outlineLuminanceWeight_,
				0.0f,
				10.0f
			);
		}
		if (outlineDepthEnabled_) {
			ImGui::SliderFloat(
				"Depth Weight",
				&outlineDepthWeight_,
				0.0f,
				10.0f
			);
		}
		ImGui::SliderFloat(
			"Threshold",
			&outlineThreshold_,
			0.0f,
			2.0f
		);
		ImGui::SliderFloat(
			"Softness",
			&outlineSoftness_,
			0.001f,
			1.0f
		);
		ImGui::SliderFloat(
			"Thickness",
			&outlineThickness_,
			1.0f,
			5.0f
		);
		ImGui::ColorEdit4("Color", outlineColor_);
		ImGui::TreePop();
	}
	ImGui::PopID();

	ImGui::EndChild();
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
		GetPostProcessOutputTarget()->GetSrvGpuHandle(),
		GetPostProcessOutputTarget()->GetWidth(),
		GetPostProcessOutputTarget()->GetHeight(),
		sceneManager_->GetCurrentSceneName().c_str()
	);
	sceneRenderTarget_->Resize(
		imguiManager_->GetSceneViewWidth(),
		imguiManager_->GetSceneViewHeight()
	);
	for (SceneRenderTarget* renderTarget : postProcessRenderTargets_) {
		renderTarget->Resize(
			imguiManager_->GetSceneViewWidth(),
			imguiManager_->GetSceneViewHeight()
		);
	}
#else
	sceneRenderTarget_->Resize(
		dxCommon_->GetClientWidth(),
		dxCommon_->GetClientHeight()
	);
	for (SceneRenderTarget* renderTarget : postProcessRenderTargets_) {
		renderTarget->Resize(
			dxCommon_->GetClientWidth(),
			dxCommon_->GetClientHeight()
		);
	}
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

	fullscreenCopy_->BeginFrame();
	D3D12_GPU_DESCRIPTOR_HANDLE sourceHandle =
		sceneRenderTarget_->GetSrvGpuHandle();
	const D3D12_GPU_DESCRIPTOR_HANDLE depthHandle =
		sceneRenderTarget_->GetDepthSrvGpuHandle();
	int passIndex = 0;
	auto applyEffect = [&](
		FullscreenCopy::Effect effect,
		const FullscreenCopy::Parameters& parameters
	) {
		SceneRenderTarget* destination =
			postProcessRenderTargets_[passIndex % 2];
		destination->Begin();
		srvManager_->PreDraw();
		fullscreenCopy_->SetParameters(parameters);
		fullscreenCopy_->Draw(sourceHandle, depthHandle, effect);
		destination->End();
		sourceHandle = destination->GetSrvGpuHandle();
		++passIndex;
	};

	if (grayscaleEnabled_) {
		applyEffect(
			FullscreenCopy::Effect::kGrayscale,
			FullscreenCopy::Parameters{}
		);
	}
	if (vignetteEnabled_) {
		FullscreenCopy::Parameters parameters{};
		parameters.vignetteScale = vignetteScale_;
		parameters.vignettePower = vignettePower_;
		parameters.vignetteIntensity = vignetteIntensity_;
		applyEffect(FullscreenCopy::Effect::kVignette, parameters);
	}
	if (boxBlurEnabled_) {
		FullscreenCopy::Parameters parameters{};
		parameters.blurStrength = boxBlurStrength_;
		parameters.blurRadius = boxBlurKernelSize_ == 5 ? 2u : 1u;
		applyEffect(FullscreenCopy::Effect::kBoxBlur, parameters);
	}
	if (gaussianBlurEnabled_) {
		FullscreenCopy::Parameters parameters{};
		parameters.blurStrength = gaussianBlurStrength_;
		parameters.blurRadius =
			gaussianBlurKernelSize_ == 5 ? 2u : 1u;
		parameters.gaussianSigma = gaussianBlurSigma_;
		applyEffect(FullscreenCopy::Effect::kGaussianBlur, parameters);
	}
	if (radialBlurEnabled_) {
		FullscreenCopy::Parameters parameters{};
		parameters.radialBlurCenter[0] = radialBlurCenter_[0];
		parameters.radialBlurCenter[1] = radialBlurCenter_[1];
		parameters.radialBlurWidth = radialBlurWidth_;
		parameters.radialBlurSamples =
			static_cast<uint32_t>(radialBlurSamples_);
		applyEffect(FullscreenCopy::Effect::kRadialBlur, parameters);
	}
	if (outlineEnabled_) {
		FullscreenCopy::Parameters parameters{};
		parameters.outlineLuminanceWeight = outlineLuminanceWeight_;
		parameters.outlineDepthWeight = outlineDepthWeight_;
		parameters.outlineThreshold = outlineThreshold_;
		parameters.outlineSoftness = outlineSoftness_;
		parameters.outlineThickness = outlineThickness_;
		Camera* camera =
			Object3dCommon::GetInstance()->GetDefaultCamera();
		if (camera) {
			parameters.cameraNear = camera->GetNearClip();
			parameters.cameraFar = camera->GetFarClip();
		}
		parameters.outlineFlags =
			(outlineLuminanceEnabled_ ? 1u : 0u) |
			(outlineDepthEnabled_ ? 2u : 0u);
		for (uint32_t index = 0; index < 4; ++index) {
			parameters.outlineColor[index] = outlineColor_[index];
		}
		applyEffect(FullscreenCopy::Effect::kOutline, parameters);
	}
	if (passIndex == 0) {
		applyEffect(
			FullscreenCopy::Effect::kCopy,
			FullscreenCopy::Parameters{}
		);
	}

	dxCommon_->PreDraw();
	srvManager_->PreDraw();
	fullscreenCopy_->Draw(sourceHandle, depthHandle);
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

	for (SceneRenderTarget*& renderTarget : postProcessRenderTargets_) {
		delete renderTarget;
		renderTarget = nullptr;
	}

	if (sceneManager_) {
		delete sceneManager_;
		sceneManager_ = nullptr;
	}

	// 基底クラスの終了処理
	Framework::Finalize();
}

int Game::GetEnabledPostEffectCount() const {
	return
		static_cast<int>(grayscaleEnabled_) +
		static_cast<int>(vignetteEnabled_) +
		static_cast<int>(boxBlurEnabled_) +
		static_cast<int>(gaussianBlurEnabled_) +
		static_cast<int>(radialBlurEnabled_) +
		static_cast<int>(outlineEnabled_);
}

SceneRenderTarget* Game::GetPostProcessOutputTarget() const {
	const int effectCount = GetEnabledPostEffectCount();
	const int passCount = effectCount > 0 ? effectCount : 1;
	return postProcessRenderTargets_[(passCount - 1) % 2];
}
