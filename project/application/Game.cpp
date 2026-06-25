#include "Game.h"
#include "scene/SceneFactory.h"
#include "../engine/scene/EditorSession.h"

#include "../engine/base/DirectXCommon.h"
#include "../engine/base/BloomRenderer.h"
#include "../engine/base/FullscreenCopy.h"
#include "../engine/base/ImGuiManager.h"
#include "../engine/base/RenderFormats.h"
#include "../engine/base/SceneRenderTarget.h"
#include "../engine/io/Input.h"
#include "../engine/2d/Sprite.h"
#include "../engine/2d/SpriteCommon.h"
#include "../engine/2d/TextureManager.h"
#include "../engine/3d/Camera.h"
#include "../engine/3d/Object3dCommon.h"
#include "../engine/3d/Object3d.h"
#include "../engine/3d/Model.h"
#include "../engine/3d/ModelManager.h"
#include "../engine/3d/SrvManager.h"
#include "../engine/particle/ParticleCommon.h"
#include "../engine/particle/ParticleManager.h"
#include "../engine/particle/ParticleEmitter.h"
#include "../engine/debug/DebugRenderer.h"
#include "../externals/imgui/imgui.h"

#include <algorithm>
#include <cmath>
#include <utility>

void Game::Initialize() {
	// 基底クラスの初期化処理
	Framework::Initialize();

	editorSession_ = new EditorSession();
	const bool sceneLoaded = editorSession_->Initialize(
		"GAMEPLAY",
		"resources/scenes/gameplay.scene.json"
	);
	if (!sceneLoaded) {
		SceneDocument& document = editorSession_->GetEditDocument();
		auto addEntity = [&document](
			const char* name,
			const char* modelPath,
			const Transform& transform,
			std::vector<SceneComponent> components
		) {
			SceneEntity& entity = document.CreateEntity(name);
			entity.modelPath = modelPath ? modelPath : "";
			entity.transform = transform;
			entity.components = std::move(components);
			for (SceneComponent& component : entity.components) {
				if (component.type == "MeshRenderer") {
					component.modelPath = entity.modelPath;
				}
			}
		};
		addEntity(
			"Main Camera",
			"",
			Transform{},
			{ "Camera" }
		);
		addEntity(
			"Environment",
			"",
			Transform{},
			{ "Environment" }
		);
		addEntity(
			"Terrain",
			"terrain.obj",
			Transform{
				{ 10.0f, 10.0f, 10.0f },
				{ 0.0f, 0.0f, 0.0f },
				{ 0.0f, -5.0f, 0.0f }
			},
			{ "MeshRenderer" }
		);
		addEntity(
			"Animated Cube",
			"AnimatedCube/AnimatedCube.gltf",
			Transform{
				{ 0.65f, 0.65f, 0.65f },
				{ 0.0f, 0.0f, 0.0f },
				{ 3.0f, 10.5f, -2.0f }
			},
			{ "MeshRenderer", "Animator" }
		);
		addEntity(
			"Human",
			"human/walk.gltf",
			Transform{
				{ 1.0f, 1.0f, 1.0f },
				{ 0.0f, 0.0f, 0.0f },
				{ -2.0f, 0.0f, -2.0f }
			},
			{ "MeshRenderer", "Animator" }
		);
		addEntity(
			"Player",
			"Cube.obj",
			Transform{
				{ 1.0f, 1.0f, 1.0f },
				{ 0.0f, 0.0f, 0.0f },
				{ 0.0f, 1.0f, -4.0f }
			},
			{ "MeshRenderer", "PlayerBehavior", "OBBCollider" }
		);
		editorSession_->Save();
	}

	sceneManager_ = new SceneManager();
	sceneManager_->SetEditorSession(editorSession_);
	sceneFactory_ = new SceneFactory();
	sceneManager_->SetSceneFactory(sceneFactory_);
	sceneManager_->ChangeScene("GAMEPLAY");

#if defined(_DEBUG) || defined(DEVELOPMENT)
	imguiManager_->SetEditorSession(editorSession_);
#endif

	sceneRenderTarget_ = new SceneRenderTarget();
	SceneRenderTarget::Desc sceneTargetDesc{};
	sceneTargetDesc.width = dxCommon_->GetClientWidth();
	sceneTargetDesc.height = dxCommon_->GetClientHeight();
	sceneTargetDesc.format = RenderFormats::kSceneHdrFormat;
	sceneTargetDesc.createDepth = true;
	sceneTargetDesc.clearColor[0] = 0.1f;
	sceneTargetDesc.clearColor[1] = 0.2f;
	sceneTargetDesc.clearColor[2] = 0.8f;
	sceneTargetDesc.clearColor[3] = 1.0f;
	sceneRenderTarget_->Initialize(dxCommon_, srvManager_, sceneTargetDesc);
	for (SceneRenderTarget*& renderTarget : postProcessRenderTargets_) {
		renderTarget = new SceneRenderTarget();
		SceneRenderTarget::Desc postTargetDesc{};
		postTargetDesc.width = dxCommon_->GetClientWidth();
		postTargetDesc.height = dxCommon_->GetClientHeight();
		postTargetDesc.format = RenderFormats::kDisplayFormat;
		postTargetDesc.createDepth = false;
		postTargetDesc.clearColor[0] = 0.0f;
		postTargetDesc.clearColor[1] = 0.0f;
		postTargetDesc.clearColor[2] = 0.0f;
		postTargetDesc.clearColor[3] = 1.0f;
		renderTarget->Initialize(dxCommon_, srvManager_, postTargetDesc);
	}
	fullscreenCopy_ = new FullscreenCopy();
	fullscreenCopy_->Initialize(dxCommon_);
	bloomRenderer_ = new BloomRenderer();
	bloomRenderer_->Initialize(dxCommon_, srvManager_);

#if defined(_DEBUG) || defined(DEVELOPMENT)
	modelPreviewRenderTarget_ = new SceneRenderTarget();
	SceneRenderTarget::Desc modelPreviewDesc{};
	modelPreviewDesc.width = 512;
	modelPreviewDesc.height = 512;
	modelPreviewDesc.format = RenderFormats::kSceneHdrFormat;
	modelPreviewDesc.createDepth = true;
	modelPreviewDesc.clearColor[0] = 0.035f;
	modelPreviewDesc.clearColor[1] = 0.04f;
	modelPreviewDesc.clearColor[2] = 0.05f;
	modelPreviewDesc.clearColor[3] = 1.0f;
	modelPreviewRenderTarget_->Initialize(
		dxCommon_,
		srvManager_,
		modelPreviewDesc
	);
	modelPreviewCamera_ = new Camera();
	modelPreviewCamera_->SetOrbitMode(true);
	modelPreviewCamera_->SetFovY(0.7f);
	modelPreviewCamera_->SetAspectRatio(1.0f);
	modelPreviewCamera_->SetNearClip(0.01f);
	modelPreviewCamera_->SetFarClip(10000.0f);
	modelPreviewObject_ = new Object3d();
	modelPreviewObject_->Initialize(Object3dCommon::GetInstance());
	modelPreviewObject_->SetCamera(modelPreviewCamera_);
	// Asset previews must not depend on the active scene's light bindings.
	modelPreviewObject_->SetEnableLighting(false);
#endif
	baseExposure_ = bloomParameters_.exposure;
	currentExposure_ = baseExposure_;

	TextureManager::GetInstance()->LoadTexture("resources/noise0.png");
	TextureManager::GetInstance()->LoadTexture("resources/noise1.png");
}

void Game::Update() {
	// 基底クラスの更新処理
	Framework::Update();

	if (IsEndRequest()) {
		return;
	}

	if (noiseAnimate_) {
		noiseTime_ += (1.0f / 60.0f) * noiseSpeed_;
	}

#if defined(_DEBUG) || defined(DEVELOPMENT)
	DebugRenderer::GetInstance()->Clear();

	ImGui::Begin("Post Process Stack");
	ImGui::Text("Active: %d", GetEnabledPostEffectCount());
	ImGui::SameLine();
	if (ImGui::SmallButton("Disable All")) {
		bloomParameters_.enabled = 0;
		grayscaleEnabled_ = false;
		vignetteEnabled_ = false;
		boxBlurEnabled_ = false;
		gaussianBlurEnabled_ = false;
		radialBlurEnabled_ = false;
		noiseEnabled_ = false;
		dissolveEnabled_ = false;
		outlineEnabled_ = false;
	}
	ImGui::TextDisabled("Applied from top to bottom");
	ImGui::Separator();

	ImGui::BeginChild("EffectStack", ImVec2(0.0f, 0.0f), true);

	ImGui::PushID("HDRBloom");
	if (ImGui::TreeNodeEx(
		"HDR / Bloom / ToneMap",
		ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth
	)) {
		bool bloomEnabled = bloomParameters_.enabled != 0;
		if (ImGui::Checkbox("Bloom", &bloomEnabled)) {
			bloomParameters_.enabled = bloomEnabled ? 1 : 0;
		}
		ImGui::SliderFloat("Base Exposure", &baseExposure_, 0.01f, 5.0f);
		ImGui::Text("Current Exposure: %.2f", currentExposure_);
		const char* toneMapNames[] = { "ACES", "Reinhard" };
		ImGui::Combo(
			"Tone Map",
			&bloomParameters_.toneMapMode,
			toneMapNames,
			IM_ARRAYSIZE(toneMapNames)
		);
		ImGui::SliderFloat("Threshold", &bloomParameters_.threshold, 0.0f, 10.0f);
		ImGui::SliderFloat("Soft Knee", &bloomParameters_.softKnee, 0.0f, 1.0f);
		ImGui::SliderFloat("Intensity", &bloomParameters_.intensity, 0.0f, 5.0f);
		ImGui::SliderInt("Blur Iterations", &bloomParameters_.blurIterations, 0, 12);
		ImGui::SliderInt("Downsample", &bloomParameters_.downsampleScale, 1, 8);
		ImGui::SliderFloat("Blur Radius", &bloomParameters_.blurRadius, 0.0f, 8.0f);
		ImGui::TreePop();
	}
	ImGui::PopID();
	ImGui::Separator();

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

	ImGui::PushID("Noise");
	ImGui::Checkbox("##Enabled", &noiseEnabled_);
	ImGui::SameLine();
	if (ImGui::TreeNodeEx(
		"Noise",
		ImGuiTreeNodeFlags_SpanAvailWidth
	)) {
		ImGui::Checkbox("Animate", &noiseAnimate_);
		ImGui::SliderFloat("Amount", &noiseAmount_, 0.0f, 1.0f);
		ImGui::SliderFloat("Scale", &noiseScale_, 0.25f, 8.0f);
		if (noiseAnimate_) {
			ImGui::SliderFloat("Speed", &noiseSpeed_, 0.0f, 10.0f);
		}
		ImGui::DragFloat("Seed", &noiseSeed_, 0.01f);
		if (ImGui::SmallButton("Reset Time")) {
			noiseTime_ = 0.0f;
		}
		ImGui::TreePop();
	}
	ImGui::PopID();
	ImGui::Separator();

	ImGui::PushID("Dissolve");
	ImGui::Checkbox("##Enabled", &dissolveEnabled_);
	ImGui::SameLine();
	if (ImGui::TreeNodeEx(
		"Dissolve",
		ImGuiTreeNodeFlags_SpanAvailWidth
	)) {
		const char* maskNames[] = { "Noise 0", "Noise 1" };
		ImGui::Combo(
			"Mask",
			&dissolveMaskIndex_,
			maskNames,
			IM_ARRAYSIZE(maskNames)
		);
		ImGui::SliderFloat(
			"Threshold",
			&dissolveThreshold_,
			0.0f,
			1.0f
		);
		ImGui::SliderFloat(
			"Edge Width",
			&dissolveEdgeWidth_,
			0.001f,
			0.25f
		);
		ImGui::ColorEdit4("Edge Color", dissolveEdgeColor_);
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

	if (editorSession_->ConsumeReloadRequest()) {
		sceneManager_->ReloadCurrentScene();
	}
	if (!editorSession_->IsPaused()) {
		sceneManager_->Update();
	}

	ParticleManager::ExposureFlashEvent exposureFlash{};
	while (ParticleManager::GetInstance()->ConsumeExposureFlashEvent(exposureFlash)) {
		currentExposure_ = (std::max)(
			currentExposure_,
			exposureFlash.exposure
		);
		exposureReturnSpeed_ = (std::max)(
			exposureFlash.returnSpeed,
			0.01f
		);
	}

	const float exposureLerp =
		std::clamp(exposureReturnSpeed_ * (1.0f / 60.0f), 0.0f, 1.0f);
	currentExposure_ += (baseExposure_ - currentExposure_) * exposureLerp;
	bloomParameters_.exposure = currentExposure_;

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

#if defined(_DEBUG) || defined(DEVELOPMENT)
	DrawModelPreview();
#endif

	fullscreenCopy_->BeginFrame();
	bloomRenderer_->BeginFrame();
	bloomRenderer_->SetParameters(bloomParameters_);
	bloomRenderer_->Apply(
		sceneRenderTarget_->GetSrvGpuHandle(),
		postProcessRenderTargets_[0]
	);
	D3D12_GPU_DESCRIPTOR_HANDLE sourceHandle =
		postProcessRenderTargets_[0]->GetSrvGpuHandle();
	const D3D12_GPU_DESCRIPTOR_HANDLE depthHandle =
		sceneRenderTarget_->GetDepthSrvGpuHandle();
	const char* dissolveMaskPath =
		dissolveMaskIndex_ == 1
			? "resources/noise1.png"
			: "resources/noise0.png";
	const D3D12_GPU_DESCRIPTOR_HANDLE maskHandle =
		TextureManager::GetInstance()->GetSrvHandleGPU(dissolveMaskPath);
	int passIndex = 1;
	auto applyEffect = [&](
		FullscreenCopy::Effect effect,
		const FullscreenCopy::Parameters& parameters
	) {
		SceneRenderTarget* destination =
			postProcessRenderTargets_[passIndex % 2];
		destination->Begin();
		srvManager_->PreDraw();
		fullscreenCopy_->SetParameters(parameters);
		fullscreenCopy_->Draw(
			sourceHandle,
			depthHandle,
			maskHandle,
			effect
		);
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
	if (noiseEnabled_) {
		FullscreenCopy::Parameters parameters{};
		parameters.noiseTime = noiseTime_;
		parameters.noiseAmount = noiseAmount_;
		parameters.noiseScale = noiseScale_;
		parameters.noiseSeed = noiseSeed_;
		applyEffect(FullscreenCopy::Effect::kNoise, parameters);
	}
	if (dissolveEnabled_) {
		FullscreenCopy::Parameters parameters{};
		parameters.dissolveThreshold = dissolveThreshold_;
		parameters.dissolveEdgeWidth = dissolveEdgeWidth_;
		for (uint32_t index = 0; index < 4; ++index) {
			parameters.dissolveEdgeColor[index] =
				dissolveEdgeColor_[index];
		}
		applyEffect(FullscreenCopy::Effect::kDissolve, parameters);
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
	dxCommon_->PreDraw();
	srvManager_->PreDraw();
	fullscreenCopy_->Draw(sourceHandle, depthHandle, maskHandle);
#if defined(_DEBUG) || defined(DEVELOPMENT)
	imguiManager_->EndFrame();
#endif

	dxCommon_->PostDraw();
}

void Game::DrawModelPreview() {
	if (
		!imguiManager_ ||
		!modelPreviewRenderTarget_ ||
		!modelPreviewCamera_ ||
		!modelPreviewObject_
	) {
		return;
	}

	std::string modelPath;
	float yaw = 0.0f;
	float pitch = 0.0f;
	float zoom = 1.0f;
	if (!imguiManager_->GetModelPreviewRequest(modelPath, yaw, pitch, zoom)) {
		return;
	}

	if (modelPath != modelPreviewPath_) {
		ModelManager* modelManager = ModelManager::GetInstance();
		modelManager->LoadModel(modelPath);
		Model* model = modelManager->FindModel(modelPath);
		if (!model) {
			return;
		}

		modelPreviewObject_->SetModel(model);
		modelPreviewObject_->SetScale({ 1.0f, 1.0f, 1.0f });
		Vector3 boundsMin{};
		Vector3 boundsMax{};
		if (model->GetLocalBounds(boundsMin, boundsMax)) {
			const Vector3 center = {
				(boundsMin.x + boundsMax.x) * 0.5f,
				(boundsMin.y + boundsMax.y) * 0.5f,
				(boundsMin.z + boundsMax.z) * 0.5f
			};
			const Vector3 halfExtent = {
				(boundsMax.x - boundsMin.x) * 0.5f,
				(boundsMax.y - boundsMin.y) * 0.5f,
				(boundsMax.z - boundsMin.z) * 0.5f
			};
			const float radius = (std::max)(
				std::sqrt(
					halfExtent.x * halfExtent.x +
					halfExtent.y * halfExtent.y +
					halfExtent.z * halfExtent.z
				),
				0.01f
			);
			modelPreviewObject_->SetTranslate({
				-center.x,
				-center.y,
				-center.z
			});
			modelPreviewFitDistance_ =
				radius / std::tan(modelPreviewCamera_->GetFovY() * 0.5f) * 1.25f;
		} else {
			modelPreviewObject_->SetTranslate({ 0.0f, 0.0f, 0.0f });
			modelPreviewFitDistance_ = 5.0f;
		}
		modelPreviewPath_ = modelPath;
	}

	modelPreviewCamera_->SetOrbitTarget({ 0.0f, 0.0f, 0.0f });
	modelPreviewCamera_->SetOrbitAngle(yaw, pitch);
	modelPreviewCamera_->SetOrbitDistance(
		(std::max)(modelPreviewFitDistance_ * zoom, 0.02f)
	);
	modelPreviewCamera_->UpdatePreviewMatrices();
	modelPreviewObject_->Update();

	modelPreviewRenderTarget_->Begin();
	srvManager_->PreDraw();
	Object3dCommon::GetInstance()->SetCommonRenderState();
	modelPreviewObject_->Draw();
	modelPreviewRenderTarget_->End();

	imguiManager_->SetModelPreviewTexture(
		modelPath,
		modelPreviewRenderTarget_->GetSrvGpuHandle(),
		modelPreviewRenderTarget_->GetWidth(),
		modelPreviewRenderTarget_->GetHeight()
	);
}

void Game::Finalize() {

#if defined(_DEBUG) || defined(DEVELOPMENT)
	if (imguiManager_) {
		imguiManager_->SetEditorSession(nullptr);
	}
#endif
	delete modelPreviewObject_;
	modelPreviewObject_ = nullptr;
	delete modelPreviewCamera_;
	modelPreviewCamera_ = nullptr;
	delete modelPreviewRenderTarget_;
	modelPreviewRenderTarget_ = nullptr;

	delete bloomRenderer_;
	bloomRenderer_ = nullptr;

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

	delete editorSession_;
	editorSession_ = nullptr;

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
		static_cast<int>(noiseEnabled_) +
		static_cast<int>(dissolveEnabled_) +
		static_cast<int>(outlineEnabled_);
}

SceneRenderTarget* Game::GetPostProcessOutputTarget() const {
	return postProcessRenderTargets_[GetEnabledPostEffectCount() % 2];
}
