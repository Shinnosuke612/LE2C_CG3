// engine/base/ImGuiManager.h
#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <filesystem>
#include <unordered_set>
#include "../Audio/Audio.h"

#include <d3d12.h>

class WinApp;
class DirectXCommon;
class SrvManager;
class EditorSession;

class ImGuiManager{
public:
	static ImGuiManager* GetInstance();

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
	static bool IsSceneViewInputActive();
	void SetEditorSession(EditorSession* editorSession) {
		editorSession_ = editorSession;
	}
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
	// ドッキング用の土台を作成
	void CreateDockSpace();
	void BuildDefaultLayout();
	void DrawHierarchyWindow(const char* sceneName);
	void DrawInspectorWindow();
	void DrawProjectWindow();
	void DrawConsoleWindow();
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
	int selectedHierarchyItem_ = 0;
	uint64_t selectedEntityId_ = 0;
	uint64_t hierarchyDragSourceId_ = 0;
	bool hierarchyDragActive_ = false;
	char hierarchySearchBuffer_[128] = {};
	bool focusInspectorRequested_ = false;
	EditorSession* editorSession_ = nullptr;
	int gizmoOperation_ = 0;
	bool gizmoLocalMode_ = true;
	bool gizmoSnapEnabled_ = false;
	float gizmoTranslationSnap_ = 0.5f;
	float gizmoRotationSnapDegrees_ = 15.0f;
	float gizmoScaleSnap_ = 0.1f;

	static bool sceneViewInputActive_;

	// Singleton instance
	static ImGuiManager* instance;

	// Dynamic asset browser state
	std::string selectedProjectFolder_ = "resources";
	std::string selectedProjectFile_ = "";
	bool projectGridView_ = true;
	float projectThumbnailSize_ = 80.0f;
	std::unordered_set<std::string> projectPreviewLoadAttempted_;
	bool assetPathCacheDirty_ = true;
	std::vector<std::string> cachedModelAssetPaths_;
	std::vector<std::string> cachedTextureAssetPaths_;
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

	// Particle loading request
	bool requestLoadParticle_ = false;
	std::string particleToLoad_ = "";
};
