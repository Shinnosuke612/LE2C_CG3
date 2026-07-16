// 役割: プロジェクトに登録されたScene ID、配置ファイル、実行プロファイルを管理する。
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

struct SceneDescriptor {
	std::string id;
	std::string displayName;
	// Catalogへ保存するプロジェクト相対の論理パス。
	std::string assetPath;
	// 実際の読込に使用する解決済みパス。
	std::string filePath;
	std::string runtimeProfile;
};

enum class SceneBuildConfiguration : uint8_t {
	Debug,
	Development,
	Release
};

enum class SceneStartupMode : uint8_t {
	Editor,
	Runtime
};

struct SceneStartupSettings {
	SceneStartupMode debug = SceneStartupMode::Editor;
	SceneStartupMode development = SceneStartupMode::Editor;
	SceneStartupMode release = SceneStartupMode::Runtime;
};

class SceneCatalog {
public:
	bool Load(const std::string& settingsFilePath, std::string& errorMessage);
	bool Save(std::string& errorMessage) const;
	bool RegisterScene(
		const SceneDescriptor& descriptor,
		std::string& errorMessage
	);
	bool UpdateScene(
		const std::string& sceneId,
		const std::string& displayName,
		const std::string& assetPath,
		std::string& errorMessage
	);
	bool UnregisterScene(
		const std::string& sceneId,
		std::string& errorMessage
	);
	bool SetStartScene(
		const std::string& sceneId,
		std::string& errorMessage
	);
	bool SetStartupMode(
		SceneBuildConfiguration configuration,
		SceneStartupMode mode,
		std::string& errorMessage
	);

	const SceneDescriptor* Find(const std::string& sceneId) const;
	const SceneDescriptor* FindByFilePath(const std::string& filePath) const;
	const SceneDescriptor* GetStartScene() const;
	const std::string& GetStartSceneId() const { return startSceneId_; }
	const std::vector<SceneDescriptor>& GetScenes() const { return scenes_; }
	const std::string& GetCatalogFilePath() const { return catalogFilePath_; }
	SceneStartupMode GetStartupMode(
		SceneBuildConfiguration configuration
	) const;
	const SceneStartupSettings& GetStartupSettings() const {
		return startupSettings_;
	}
	static const char* StartupModeToString(SceneStartupMode mode);

private:
	static std::string NormalizeId(const std::string& sceneId);
	static std::string NormalizeAssetPath(const std::string& assetPath);
	static std::string NormalizePathKey(const std::string& path);
	static bool TryParseStartupMode(
		const std::string& value,
		SceneStartupMode& mode
	);
	bool PrepareDescriptor(
		const SceneDescriptor& source,
		SceneDescriptor& prepared,
		std::string& errorMessage
	) const;
	void RebuildIndices();

	std::string catalogFilePath_;
	std::string startSceneId_;
	SceneStartupSettings startupSettings_{};
	std::vector<SceneDescriptor> scenes_;
	std::unordered_map<std::string, size_t> sceneIndices_;
};
