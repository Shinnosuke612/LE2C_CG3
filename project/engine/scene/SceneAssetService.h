// 役割: Sceneファイル操作とCatalog更新を一つのトランザクションとして実行する。
#pragma once

#include <filesystem>
#include <string>

class SceneCatalog;
class SceneTemplateRegistry;

class SceneAssetService {
public:
	SceneAssetService(
		SceneCatalog* catalog,
		const SceneTemplateRegistry* templateRegistry
	);

	bool CreateScene(
		const std::string& sceneId,
		const std::string& displayName,
		const std::string& assetPath,
		const std::string& templateId,
		std::string& errorMessage
	);
	bool DuplicateScene(
		const std::string& sourceSceneId,
		const std::string& newSceneId,
		const std::string& displayName,
		const std::string& assetPath,
		std::string& errorMessage
	);
	bool RenameScene(
		const std::string& sceneId,
		const std::string& displayName,
		const std::string& assetPath,
		std::string& errorMessage
	);
	bool DeleteScene(
		const std::string& sceneId,
		std::string& errorMessage
	);

private:
	bool ResolveAssetPath(
		const std::string& requestedPath,
		std::string& assetPath,
		std::filesystem::path& resolvedPath,
		std::string& errorMessage
	) const;
	bool ValidateNewScene(
		const std::string& sceneId,
		const std::string& displayName,
		const std::filesystem::path& resolvedPath,
		std::string& errorMessage
	) const;
	bool FindSceneReference(
		const std::string& sceneId,
		std::string& referencingSceneId,
		std::string& errorMessage
	) const;

	SceneCatalog* catalog_ = nullptr;
	const SceneTemplateRegistry* templateRegistry_ = nullptr;
};
