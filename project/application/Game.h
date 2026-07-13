// 役割: エディタとゲーム実行を統合し、フレーム更新と描画順序を管理する。
#pragma once
#include "../engine/base/Framework.h"
#include "../engine/base/BloomRenderer.h"
#include "../engine/scene/SceneDocument.h"
#include "../engine/scene/SceneManager.h"
#include "../engine/math/Vector3.h"
#include "../engine/math/Vector4.h"
#include <string>

class SceneRenderTarget;
class FullscreenCopy;
class EditorSession;
class Camera;
class Object3d;

class Game : public Framework {
public:
	void Initialize() override;
	void Finalize() override;
	void Update() override;
	void Draw() override;

private:
	struct CameraSnapshot {
		bool valid = false;
		bool orbitMode = false;
		Vector3 translate{};
		Vector3 rotate{};
		Vector3 orbitTarget{};
		float orbitDistance = 10.0f;
		float orbitYaw = 0.0f;
		float orbitPitch = 0.0f;
		float fovY = 0.45f;
		float aspectRatio = 1.0f;
		float nearClip = 0.1f;
		float farClip = 1000.0f;
	};

	struct WaterPostEffectState {
		bool hasVolume = false;
		bool cameraInside = false;
		Vector3 center{};
		Vector3 halfSize{};
		bool lightShaftEnabled = false;
		Vector4 lightColor{ 0.55f, 0.90f, 1.15f, 1.0f };
		Vector3 lightDirection{ -0.25f, -1.0f, 0.18f };
		float lightIntensity = 0.0f;
		float lightDensity = 0.045f;
		float causticsIntensity = 0.35f;
		float causticsScale = 0.08f;
		float causticsSpeed = 1.0f;
		float lightBreakupStrength = 1.0f;
		float lightWarpStrength = 1.0f;
		float lightNoiseScale = 1.0f;
		int lightSampleCount = 16;
	};

	int GetEnabledPostEffectCount() const;
	SceneRenderTarget* GetPostProcessOutputTarget() const;
	ScenePostProcessSettings CapturePostProcessSettings() const;
	void ApplyPostProcessSettings(const ScenePostProcessSettings& settings);
	void StorePostProcessSettingsToDocument();
	WaterPostEffectState ResolveWaterPostEffectState(const Camera* camera) const;
	void DrawModelPreview();
	CameraSnapshot CaptureCameraSnapshot() const;
	void RestoreCameraSnapshot(const CameraSnapshot& snapshot) const;
	void BeginPauseDebugCamera();
	void EndPauseDebugCamera();

	SceneManager* sceneManager_ = nullptr;
	EditorSession* editorSession_ = nullptr;
	SceneRenderTarget* sceneRenderTarget_ = nullptr;
	SceneRenderTarget* postProcessRenderTargets_[2]{};
	SceneRenderTarget* foregroundComposeRenderTarget_ = nullptr;
	FullscreenCopy* fullscreenCopy_ = nullptr;
	BloomRenderer* bloomRenderer_ = nullptr;
	SceneRenderTarget* modelPreviewRenderTarget_ = nullptr;
	Camera* modelPreviewCamera_ = nullptr;
	Object3d* modelPreviewObject_ = nullptr;
	CameraSnapshot editorCameraSnapshot_{};
	CameraSnapshot pauseMainCameraSnapshot_{};
	bool editorWasEditingLastFrame_ = true;
	bool wasPausedLastFrame_ = false;
	uint64_t appliedPostProcessRevision_ = static_cast<uint64_t>(-1);
	std::string modelPreviewPath_;
	float modelPreviewFitDistance_ = 5.0f;
	BloomRenderer::Parameters bloomParameters_{};
	float baseExposure_ = 1.0f;
	float currentExposure_ = 1.0f;
	float exposureReturnSpeed_ = 6.0f;
	bool grayscaleEnabled_ = false;
	bool vignetteEnabled_ = false;
	bool boxBlurEnabled_ = false;
	bool gaussianBlurEnabled_ = false;
	bool radialBlurEnabled_ = false;
	bool noiseEnabled_ = false;
	bool dissolveEnabled_ = false;
	bool outlineEnabled_ = false;
	bool underwaterEnabled_ = false;
	bool waterRefractionEnabled_ = false;
	bool depthOfFieldEnabled_ = false;
	bool outlineLuminanceEnabled_ = false;
	bool outlineDepthEnabled_ = true;
	float vignetteScale_ = 16.0f;
	float vignettePower_ = 0.8f;
	float vignetteIntensity_ = 1.0f;
	int boxBlurKernelSize_ = 3;
	float boxBlurStrength_ = 1.0f;
	int gaussianBlurKernelSize_ = 3;
	float gaussianBlurSigma_ = 1.0f;
	float gaussianBlurStrength_ = 1.0f;
	float radialBlurCenter_[2]{ 0.5f, 0.5f };
	float radialBlurWidth_ = 0.01f;
	int radialBlurSamples_ = 10;
	bool noiseAnimate_ = true;
	float noiseTime_ = 0.0f;
	float noiseAmount_ = 0.25f;
	float noiseScale_ = 1.0f;
	float noiseSpeed_ = 1.0f;
	float noiseSeed_ = 0.0f;
	int dissolveMaskIndex_ = 0;
	float dissolveThreshold_ = 0.0f;
	float dissolveEdgeWidth_ = 0.03f;
	float dissolveEdgeColor_[4]{ 1.0f, 0.4f, 0.3f, 1.0f };
	float outlineLuminanceWeight_ = 1.0f;
	float outlineDepthWeight_ = 1.0f;
	float outlineThreshold_ = 0.1f;
	float outlineSoftness_ = 0.05f;
	float outlineThickness_ = 1.0f;
	float outlineColor_[4]{ 0.0f, 0.0f, 0.0f, 1.0f };
	float dofFocusDistance_ = 10.0f;
	float dofFocusRange_ = 2.0f;
	float dofBlurStrength_ = 1.0f;
	float dofNearStrength_ = 0.0f;
	float dofFarStrength_ = 1.0f;
	float dofMaxRadius_ = 4.0f;
	float underwaterTintColor_[4]{ 0.02f, 0.45f, 0.68f, 1.0f };
	float underwaterIntensity_ = 0.65f;
	float underwaterFogDensity_ = 0.035f;
	float underwaterDistortion_ = 0.012f;
	float waterRefractionTintColor_[4]{ 0.02f, 0.55f, 0.82f, 1.0f };
	float waterRefractionStrength_ = 0.018f;
	float waterRefractionEdgeSoftness_ = 0.08f;
	float waterRefractionTintStrength_ = 0.12f;
};
