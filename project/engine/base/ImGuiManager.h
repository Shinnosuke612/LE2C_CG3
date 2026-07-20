// 役割: エディタのDockSpace、Hierarchy、Inspector、Project、Scene Viewを管理する。
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>
#include <filesystem>
#include <unordered_map>
#include <unordered_set>
#include "../Audio/Audio.h"
#include "../math/Quaternion.h"
#include "../scene/PrefabAssetRegistry.h"

#include <d3d12.h>
#include "../../externals/imgui/imgui.h"

class WinApp;
class DirectXCommon;
class SrvManager;
class EditorSession;
class PrefabEditorSession;
class SceneCatalog;
class SceneDocument;
class SceneManager;
class SceneTemplateRegistry;
enum class SceneBuildConfiguration : uint8_t;
enum class SceneStartupMode : uint8_t;

enum class SceneAssetOperation {
	None,
	Create,
	Duplicate,
	Rename,
	Delete
};

struct SceneAssetRequest {
	SceneAssetOperation operation = SceneAssetOperation::None;
	std::string sourceSceneId;
	std::string sceneId;
	std::string displayName;
	std::string assetPath;
	std::string templateId;
};

struct PrefabPreviewRequest {
	const SceneDocument* document = nullptr;
	std::string assetPath;
	uint64_t revision = 0;
	float yaw = 0.65f;
	float pitch = 0.25f;
	float zoom = 1.0f;
	uint32_t width = 768;
	uint32_t height = 432;
	uint64_t selectedEntityId = 0;
	bool showSkeleton = true;
	bool showJointAxes = false;
	bool showColliders = true;
	bool showCombatVolumes = true;
	bool showGrid = true;
	uint64_t framingSerial = 0;
};

enum class SceneInstanceOperation {
	None,
	LoadAdditive,
	Unload,
	SetActive,
	SetPersistent
};

struct SceneInstanceRequest {
	SceneInstanceOperation operation = SceneInstanceOperation::None;
	std::string sceneId;
	std::string instanceKey;
	uint64_t instanceId = 0;
	bool persistent = false;
};

class ImGuiManager{
public:
	static ImGuiManager* GetInstance();
	// DirectX初期化前に適用する起動時フルスクリーン設定を読み込む。
	static bool LoadStartFullscreenSetting();

	// 初期化
	void Initialize(WinApp* winApp, DirectXCommon* dxCommon, SrvManager* srvManager);

	// フレーム開始
	void BeginFrame();

	// フレーム終了
	void EndFrame();

	void DrawEditorWorkspace(
		D3D12_GPU_DESCRIPTOR_HANDLE sceneTexture,
		uint32_t textureWidth,
		uint32_t textureHeight,
		const char* sceneName
	);

	// 終了処理
	void Finalize();

	uint32_t GetSceneViewWidth() const { return sceneViewWidth_; }
	uint32_t GetSceneViewHeight() const { return sceneViewHeight_; }
	float GetSceneViewMinX() const { return sceneViewMinX_; }
	float GetSceneViewMinY() const { return sceneViewMinY_; }
	float GetSceneViewMaxX() const { return sceneViewMaxX_; }
	float GetSceneViewMaxY() const { return sceneViewMaxY_; }
	bool IsSceneGridVisible() const { return sceneGridVisible_; }
	static bool IsSceneViewInputActive();
	void SetEditorSession(EditorSession* editorSession) {
		editorSession_ = editorSession;
	}
	void SetSceneCatalog(SceneCatalog* sceneCatalog) {
		sceneCatalog_ = sceneCatalog;
		projectDirectoryCacheDirty_ = true;
	}
	void SetSceneManager(SceneManager* sceneManager) {
		sceneManager_ = sceneManager;
	}
	void SetSceneTemplateRegistry(
		const SceneTemplateRegistry* sceneTemplateRegistry
	) {
		sceneTemplateRegistry_ = sceneTemplateRegistry;
	}
	bool ConsumeOpenSceneRequest(
		std::string& sceneId,
		bool& discardUnsavedChanges
	);
	bool ConsumeSceneAssetRequest(SceneAssetRequest& request);
	bool ConsumeSceneInstanceRequest(SceneInstanceRequest& request);
	bool ConsumeStartSceneRequest(std::string& sceneId);
	bool ConsumeStartupModeRequest(
		SceneBuildConfiguration& configuration,
		SceneStartupMode& mode
	);
	void NotifySceneAssetOperationResult(
		bool success,
		const std::string& message
	);
	void NotifySceneInstanceOperationResult(
		bool success,
		const std::string& message
	);
	void NotifyProjectSettingsResult(
		bool success,
		const std::string& message
	);
	void NotifyEditSceneOpened();
	void SetModelPreviewTexture(
		const std::string& modelPath,
		D3D12_GPU_DESCRIPTOR_HANDLE texture,
		uint32_t width,
		uint32_t height
	);
	bool GetModelPreviewRequest(
		std::string& modelPath,
		float& yaw,
		float& pitch,
		float& zoom
	) const;
	void SetPrefabPreviewTexture(
		const std::string& assetPath,
		uint64_t revision,
		D3D12_GPU_DESCRIPTOR_HANDLE texture,
		uint32_t width,
		uint32_t height,
		const Matrix4x4& viewMatrix,
		const Matrix4x4& projectionMatrix
	);
	bool GetPrefabPreviewRequest(PrefabPreviewRequest& request) const;

	// Communication with scenes
	const std::string& GetSelectedProjectFile() const { return selectedProjectFile_; }
	void ClearSelectedProjectFile() { selectedProjectFile_.clear(); }
	bool GetRequestLoadParticle(std::string& outPath) {
		if (requestLoadParticle_) {
			outPath = particleToLoad_;
			requestLoadParticle_ = false;
			return true;
		}
		return false;
	}

private:
	struct PrefabAssetValidationResult {
		std::string filePath;
		std::string message;
		bool error = true;
	};

	// ドッキング用の土台を作成
	void CreateDockSpace();
	void BuildDefaultLayout();
	void DrawHierarchyWindow(const char* sceneName);
	void DrawInspectorWindow();
	void DrawProjectWindow();
	void DrawConsoleWindow();
	void DrawLoadedScenesWindow();
	void DrawPrefabWindow();
	void RequestOpenPrefab(const std::string& filePath, int historyIndex = -1);
	bool OpenPrefab(const std::string& filePath, int historyIndex = -1);
	void DrawPrefabOpenConfirmation();
	void SelectPrefabAssetInProject(const std::string& filePath);
	uint64_t InstantiatePrefabInEditScene(
		const std::string& filePath,
		uint64_t parentId,
		const Vector3* rootTranslate = nullptr
	);
	void DrawProjectPrefabAccessPanel();
	void DrawPrefabQuickOpenPopup();
	void RequestPrefabQuickOpen();
	const std::vector<std::string>& GetCachedPrefabAssetPaths();
	void RefreshPrefabAssetPathCache();
	void RecordRecentPrefab(const std::string& filePath);
	bool IsFavoritePrefab(const std::string& filePath) const;
	void ToggleFavoritePrefab(const std::string& filePath);
	void ToggleFavoritePrefab(const PrefabAssetReference& reference);
	void DrawPrefabPreview();
	void DrawPrefabAnimationTimeline();
	void RebuildPrefabAnimationPreviewDocument();
	const SceneDocument& GetPrefabStageDocument() const;
	void DrawPrefabGizmo(float x, float y, float width, float height);
	bool PickPrefabEntity(float x, float y, float width, float height);
	void ValidateAllPrefabAssets();
	void DrawPrefabDiagnostics();
	void DrawPrefabHierarchy();
	void DrawPrefabInspector();
	void DrawPlaybackControls();
	void DrawSceneGizmo(
		float x,
		float y,
		float width,
		float height,
		uint32_t textureWidth,
		uint32_t textureHeight
	);
	bool PickSceneEntity(
		float x,
		float y,
		float width,
		float height
	);
	void FocusSceneCameraOnSelection();
	const std::vector<std::string>& GetCachedModelAssetPaths();
	const std::vector<std::string>& GetCachedTextureAssetPaths();
	void RefreshAssetPathCache();
	void InvalidateProjectCache();

	// Helper for project tree
	struct ProjectDirectoryEntry {
		std::string fileName;
		std::string filePath;
		std::string extension;
		bool isDirectory = false;
		bool isTexture = false;
		bool isModel = false;
		bool isScene = false;
	};
	struct ProjectDirectoryNode {
		std::string folderName;
		std::string folderPath;
		std::vector<ProjectDirectoryNode> children;
	};
	void DrawDirectoryTreeNode(const ProjectDirectoryNode& node);
	ProjectDirectoryNode BuildProjectDirectoryNode(const std::filesystem::path& path);
	void RefreshProjectTreeCache();
	const std::vector<ProjectDirectoryEntry>& GetCachedProjectDirectoryEntries();
	void RefreshProjectDirectoryCache();
	// エディタ固有の外観設定を管理する。シーンデータには保存しない。
	enum class EditorFontPreset {
		OriginalWithCjk,
		UnifiedCjk,
		CascadiaMonoWithCjk
	};
	void DrawSettingsMenu();
	void DrawSceneMenu();
	void DrawSceneSwitchConfirmation();
	void DrawSceneAssetDialogs();
	void DrawProjectSettingsDialogs();
	void RequestOpenScene(const std::string& sceneId);
	void QueueSceneAssetRequest(const SceneAssetRequest& request);
	void QueueSceneInstanceRequest(const SceneInstanceRequest& request);
	void LoadEditorSettings();
	void SaveEditorSettings() const;
	void RequestEditorFontRebuild();
	void ApplyPendingEditorFont();
	void ConfigureEditorFont(ImGuiIO& io, float dpiScale);

	WinApp* winApp_ = nullptr;
	DirectXCommon* dxCommon_ = nullptr;
	SrvManager* srvManager_ = nullptr;
	uint32_t sceneViewWidth_ = 1;
	uint32_t sceneViewHeight_ = 1;
	float sceneViewMinX_ = 0.0f;
	float sceneViewMinY_ = 0.0f;
	float sceneViewMaxX_ = 1.0f;
	float sceneViewMaxY_ = 1.0f;
	bool resetLayout_ = false;
	bool showHierarchy_ = true;
	bool showInspector_ = true;
	bool showProject_ = true;
	bool showConsole_ = true;
	bool showLoadedScenes_ = true;
	bool showPrefab_ = false;
	bool sceneGridVisible_ = true;
	bool prefabGridVisible_ = true;
	EditorFontPreset editorFontPreset_ = EditorFontPreset::OriginalWithCjk;
	float editorFontSize_ = 13.0f;
	bool editorFontRebuildRequested_ = false;
	bool startFullscreen_ = false;
	// シーン/Entity/コンポーネント単位のInspector折りたたみ状態。
	std::unordered_map<std::string, bool> componentFoldoutStates_;
	int selectedHierarchyItem_ = 0;
	uint64_t selectedEntityId_ = 0;
	// Hierarchyの複数選択と表示状態を保持する。selectedEntityId_はInspector/Gizmo用の基準Entity。
	std::unordered_set<uint64_t> selectedEntityIds_;
	uint64_t hierarchySelectionAnchorId_ = 0;
	uint64_t hierarchyObservedEntityId_ = 0;
	uint64_t hierarchyRenameEntityId_ = 0;
	char hierarchyRenameBuffer_[128] = {};
	bool hierarchyRenameFocusRequested_ = false;
	bool hierarchyRevealRequested_ = false;
	uint64_t hierarchyDragSourceId_ = 0;
	bool hierarchyDragActive_ = false;
	uint64_t hierarchyAutoOpenFolderId_ = 0;
	double hierarchyAutoOpenStartTime_ = 0.0;
	char hierarchySearchBuffer_[128] = {};
	bool revealInspectorRequested_ = false;
	EditorSession* editorSession_ = nullptr;
	PrefabEditorSession* prefabEditorSession_ = nullptr;
	uint64_t prefabSelectedEntityId_ = 0;
	bool prefabClosePopupRequested_ = false;
	bool prefabOpenPopupRequested_ = false;
	bool prefabFocusRequested_ = false;
	std::string pendingPrefabOpenPath_;
	int pendingPrefabHistoryIndex_ = -1;
	std::vector<PrefabAssetReference> prefabNavigationHistory_;
	int prefabNavigationIndex_ = -1;
	std::string prefabNavigationStatus_;
	SceneCatalog* sceneCatalog_ = nullptr;
	SceneManager* sceneManager_ = nullptr;
	const SceneTemplateRegistry* sceneTemplateRegistry_ = nullptr;
	std::string requestedSceneId_;
	std::string pendingSceneId_;
	bool requestedSceneDiscardUnsavedChanges_ = false;
	bool sceneSwitchPopupRequested_ = false;
	bool sceneSaveFailed_ = false;
	SceneAssetRequest requestedSceneAsset_{};
	bool sceneAssetRequestPending_ = false;
	SceneInstanceRequest requestedSceneInstance_{};
	bool sceneInstanceRequestPending_ = false;
	bool sceneInstanceOperationSucceeded_ = true;
	std::string sceneInstanceStatusMessage_;
	char additiveInstanceKeyBuffer_[64]{};
	bool createScenePopupRequested_ = false;
	bool duplicateScenePopupRequested_ = false;
	bool renameScenePopupRequested_ = false;
	bool deleteScenePopupRequested_ = false;
	bool sceneAssetErrorPopupRequested_ = false;
	std::string sceneAssetTargetId_;
	std::string sceneAssetErrorMessage_;
	std::string requestedStartSceneId_;
	bool startSceneRequestPending_ = false;
	SceneBuildConfiguration requestedStartupConfiguration_{};
	SceneStartupMode requestedStartupMode_{};
	bool startupModeRequestPending_ = false;
	bool projectSettingsErrorPopupRequested_ = false;
	std::string projectSettingsErrorMessage_;
	char sceneAssetNameBuffer_[128]{};
	char sceneAssetIdBuffer_[64]{};
	char sceneAssetFileBuffer_[128]{};
	int sceneTemplateIndex_ = 0;
	int gizmoOperation_ = 0;
	bool gizmoLocalMode_ = true;
	bool gizmoSnapEnabled_ = false;
	float gizmoTranslationSnap_ = 0.5f;
	float gizmoRotationSnapDegrees_ = 15.0f;
	float gizmoScaleSnap_ = 0.1f;
	uint64_t inspectorRotationEntityId_ = 0;
	Vector3 inspectorRotationEuler_{};
	Quaternion inspectorRotationSource_ = { 0.0f, 0.0f, 0.0f, 1.0f };

	static bool sceneViewInputActive_;

	// Singleton instance
	static ImGuiManager* instance;

	// Dynamic asset browser state
	std::string selectedProjectFolder_;
	std::string selectedProjectFile_ = "";
	bool projectFocusRequested_ = false;
	bool projectGridView_ = true;
	bool projectPrefabFilterEnabled_ = false;
	bool prefabQuickOpenFocusRequested_ = false;
	bool prefabQuickOpenPopupRequested_ = false;
	char prefabQuickOpenSearchBuffer_[128] = {};
	float projectThumbnailSize_ = 80.0f;
	std::unordered_set<std::string> projectPreviewLoadAttempted_;
	bool assetPathCacheDirty_ = true;
	std::vector<std::string> cachedModelAssetPaths_;
	std::vector<std::string> cachedTextureAssetPaths_;
	bool prefabAssetPathCacheDirty_ = true;
	std::vector<std::string> cachedPrefabAssetPaths_;
	bool prefabAssetValidationCompleted_ = false;
	std::size_t prefabAssetValidationScannedCount_ = 0;
	std::vector<PrefabAssetValidationResult> prefabAssetValidationResults_;
	std::vector<PrefabAssetReference> recentPrefabReferences_;
	std::vector<PrefabAssetReference> favoritePrefabReferences_;
	bool projectDirectoryCacheDirty_ = true;
	std::string cachedProjectFolder_;
	std::vector<ProjectDirectoryEntry> cachedProjectEntries_;
	bool projectTreeCacheDirty_ = true;
	ProjectDirectoryNode cachedProjectTreeRoot_;
	Audio::SoundData previewSoundData_{};
	std::string modelPreviewRenderedPath_;
	D3D12_GPU_DESCRIPTOR_HANDLE modelPreviewTexture_{};
	uint32_t modelPreviewWidth_ = 1;
	uint32_t modelPreviewHeight_ = 1;
	float modelPreviewYaw_ = 0.65f;
	float modelPreviewPitch_ = 0.25f;
	float modelPreviewZoom_ = 1.0f;
	std::string prefabPreviewRenderedPath_;
	uint64_t prefabPreviewRenderedRevision_ = 0;
	D3D12_GPU_DESCRIPTOR_HANDLE prefabPreviewTexture_{};
	uint32_t prefabPreviewTextureWidth_ = 1;
	uint32_t prefabPreviewTextureHeight_ = 1;
	Matrix4x4 prefabPreviewViewMatrix_ = MakeIdentity4x4();
	Matrix4x4 prefabPreviewProjectionMatrix_ = MakeIdentity4x4();
	bool prefabPreviewCameraValid_ = false;
	uint32_t prefabPreviewRequestedWidth_ = 768;
	uint32_t prefabPreviewRequestedHeight_ = 432;
	float prefabPreviewYaw_ = 0.65f;
	float prefabPreviewPitch_ = 0.25f;
	float prefabPreviewZoom_ = 1.0f;
	uint64_t prefabPreviewFramingSerial_ = 1;
	bool prefabPreviewShowSkeleton_ = true;
	bool prefabPreviewShowJointAxes_ = false;
	bool prefabPreviewShowColliders_ = true;
	bool prefabPreviewShowCombatVolumes_ = true;
	std::string prefabNestedTargetDocumentPath_;
	uint64_t prefabNestedTargetRootId_ = 0;
	SceneDocument* prefabAnimationPreviewDocument_ = nullptr;
	std::string prefabAnimationPreviewAssetPath_;
	uint64_t prefabAnimationPreviewSourceRevision_ = 0;
	uint64_t prefabAnimationPreviewOwnerEntityId_ = 0;
	int prefabAnimationPreviewClipIndex_ = 0;
	float prefabAnimationPreviewTime_ = 0.0f;
	bool prefabAnimationPreviewPlaying_ = false;
	bool prefabAnimationPreviewActive_ = false;
	// ImGui 1.92以降はフォントデータをAtlasの寿命まで保持する必要がある。
	std::vector<uint8_t> editorBaseFontData_;
	std::vector<uint8_t> editorJapaneseFontData_;
	std::vector<uint8_t> editorChineseFontData_;
	ImVector<ImWchar> editorGlyphRanges_;

	// Particle loading request
	bool requestLoadParticle_ = false;
	std::string particleToLoad_ = "";
};
