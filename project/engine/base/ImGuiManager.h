// engine/base/ImGuiManager.h
#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <filesystem>
#include "../Audio/Audio.h"

#include <d3d12.h>

class WinApp;
class DirectXCommon;
class SrvManager;

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
	static bool IsSceneViewInputActive();

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

	// Helper for project tree
	void DrawDirectoryTreeNode(const std::filesystem::path& path);

	WinApp* winApp_ = nullptr;
	DirectXCommon* dxCommon_ = nullptr;
	SrvManager* srvManager_ = nullptr;
	uint32_t sceneViewWidth_ = 1;
	uint32_t sceneViewHeight_ = 1;
	bool resetLayout_ = false;
	bool showHierarchy_ = true;
	bool showInspector_ = true;
	bool showProject_ = true;
	bool showConsole_ = true;
	int selectedHierarchyItem_ = 0;

	static bool sceneViewInputActive_;

	// Singleton instance
	static ImGuiManager* instance;

	// Dynamic asset browser state
	std::string selectedProjectFolder_ = "resources";
	std::string selectedProjectFile_ = "";
	Audio::SoundData previewSoundData_{};

	// Particle loading request
	bool requestLoadParticle_ = false;
	std::string particleToLoad_ = "";
};
