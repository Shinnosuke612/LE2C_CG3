// 役割: Sceneアセットの作成、複製、名前変更、削除を安全に実行する。
#include "SceneAssetService.h"

#include "SceneCatalog.h"
#include "SceneDocument.h"
#include "SceneTemplateRegistry.h"
#include "../utility/EditableResourcePath.h"
#include "../utility/StringUtility.h"

#include <algorithm>
#include <cctype>
#include <system_error>

namespace {
	bool IsValidSceneId(const std::string& sceneId) {
		if (sceneId.empty()) {
			return false;
		}
		return std::all_of(
			sceneId.begin(),
			sceneId.end(),
			[](unsigned char character) {
				return std::isalnum(character) || character == '-' || character == '_';
			}
		);
	}

	std::filesystem::path BackupPath(const std::filesystem::path& path) {
		std::filesystem::path backup = path;
		backup += L".bak";
		return backup;
	}

	std::filesystem::path TemporaryPath(
		const std::filesystem::path& path,
		const wchar_t* suffix
	) {
		std::filesystem::path temporary = path;
		temporary += suffix;
		return temporary;
	}

	void RemoveIfPresent(const std::filesystem::path& path) {
		std::error_code error;
		std::filesystem::remove(path, error);
	}

	void RemoveGeneratedSceneFiles(const std::filesystem::path& path) {
		RemoveIfPresent(path);
		RemoveIfPresent(BackupPath(path));
		RemoveIfPresent(TemporaryPath(path, L".tmp"));
	}

	bool StageIfPresent(
		const std::filesystem::path& source,
		const std::filesystem::path& staged,
		bool& stagedFile,
		std::string& errorMessage
	) {
		stagedFile = false;
		std::error_code error;
		if (!std::filesystem::exists(source, error)) {
			if (error) {
				errorMessage = "Scene asset file could not be inspected: " +
					StringUtility::ToUtf8(source);
				return false;
			}
			return true;
		}
		if (std::filesystem::exists(staged, error) || error) {
			errorMessage = "A temporary Scene asset file already exists: " +
				StringUtility::ToUtf8(staged);
			return false;
		}
		std::filesystem::rename(source, staged, error);
		if (error) {
			errorMessage = "Scene asset file could not be staged: " +
				StringUtility::ToUtf8(source);
			return false;
		}
		stagedFile = true;
		return true;
	}

	void RestoreStagedFile(
		const std::filesystem::path& staged,
		const std::filesystem::path& destination,
		bool stagedFile
	) {
		if (!stagedFile) {
			return;
		}
		RemoveIfPresent(destination);
		std::error_code error;
		std::filesystem::rename(staged, destination, error);
	}

	bool SamePath(
		const std::filesystem::path& left,
		const std::filesystem::path& right
	) {
		std::string leftText = StringUtility::ToUtf8(left.lexically_normal());
		std::string rightText = StringUtility::ToUtf8(right.lexically_normal());
#if defined(_WIN32)
		std::transform(leftText.begin(), leftText.end(), leftText.begin(),
			[](unsigned char value) { return static_cast<char>(std::tolower(value)); });
		std::transform(rightText.begin(), rightText.end(), rightText.begin(),
			[](unsigned char value) { return static_cast<char>(std::tolower(value)); });
#endif
		return leftText == rightText;
	}
}

SceneAssetService::SceneAssetService(
	SceneCatalog* catalog,
	const SceneTemplateRegistry* templateRegistry
) : catalog_(catalog), templateRegistry_(templateRegistry) {
}

bool SceneAssetService::CreateScene(
	const std::string& sceneId,
	const std::string& displayName,
	const std::string& requestedAssetPath,
	const std::string& templateId,
	std::string& errorMessage
) {
	if (!catalog_ || !templateRegistry_) {
		errorMessage = "Scene asset services are not initialized";
		return false;
	}

	std::string assetPath;
	std::filesystem::path resolvedPath;
	if (!ResolveAssetPath(
		requestedAssetPath,
		assetPath,
		resolvedPath,
		errorMessage
	) || !ValidateNewScene(sceneId, displayName, resolvedPath, errorMessage)) {
		return false;
	}

	SceneDocument document;
	if (!templateRegistry_->CreateDocument(
		templateId,
		displayName,
		document,
		errorMessage
	)) {
		return false;
	}
	if (!document.Save(StringUtility::ToUtf8(resolvedPath))) {
		errorMessage = "Scene file could not be created: " + assetPath;
		return false;
	}

	const SceneCatalog catalogSnapshot = *catalog_;
	SceneDescriptor descriptor{};
	descriptor.id = sceneId;
	descriptor.displayName = displayName;
	descriptor.assetPath = assetPath;
	descriptor.runtimeProfile = "RUNTIME";
	if (!catalog_->RegisterScene(descriptor, errorMessage) ||
		!catalog_->Save(errorMessage)) {
		*catalog_ = catalogSnapshot;
		RemoveGeneratedSceneFiles(resolvedPath);
		return false;
	}

	errorMessage.clear();
	return true;
}

bool SceneAssetService::DuplicateScene(
	const std::string& sourceSceneId,
	const std::string& newSceneId,
	const std::string& displayName,
	const std::string& requestedAssetPath,
	std::string& errorMessage
) {
	if (!catalog_) {
		errorMessage = "Scene Catalog is not initialized";
		return false;
	}
	const SceneDescriptor* source = catalog_->Find(sourceSceneId);
	if (!source) {
		errorMessage = "Source Scene is not registered: " + sourceSceneId;
		return false;
	}
	const SceneDescriptor sourceCopy = *source;

	std::string assetPath;
	std::filesystem::path resolvedPath;
	if (!ResolveAssetPath(
		requestedAssetPath,
		assetPath,
		resolvedPath,
		errorMessage
	) || !ValidateNewScene(newSceneId, displayName, resolvedPath, errorMessage)) {
		return false;
	}

	SceneDocument document;
	if (!document.Load(sourceCopy.filePath)) {
		errorMessage = "Source Scene could not be loaded: " +
			sourceCopy.assetPath + " | " + document.GetLastLoadError();
		return false;
	}
	document.SetSceneName(displayName);
	if (!document.Save(StringUtility::ToUtf8(resolvedPath))) {
		errorMessage = "Duplicated Scene file could not be saved: " + assetPath;
		return false;
	}

	const SceneCatalog catalogSnapshot = *catalog_;
	SceneDescriptor descriptor{};
	descriptor.id = newSceneId;
	descriptor.displayName = displayName;
	descriptor.assetPath = assetPath;
	descriptor.runtimeProfile = sourceCopy.runtimeProfile;
	if (!catalog_->RegisterScene(descriptor, errorMessage) ||
		!catalog_->Save(errorMessage)) {
		*catalog_ = catalogSnapshot;
		RemoveGeneratedSceneFiles(resolvedPath);
		return false;
	}

	errorMessage.clear();
	return true;
}

bool SceneAssetService::RenameScene(
	const std::string& sceneId,
	const std::string& displayName,
	const std::string& requestedAssetPath,
	std::string& errorMessage
) {
	if (!catalog_) {
		errorMessage = "Scene Catalog is not initialized";
		return false;
	}
	const SceneDescriptor* source = catalog_->Find(sceneId);
	if (!source) {
		errorMessage = "Scene is not registered: " + sceneId;
		return false;
	}
	if (displayName.empty()) {
		errorMessage = "Scene display name cannot be empty";
		return false;
	}
	const SceneDescriptor sourceCopy = *source;
	std::string sourceAssetPath;
	std::filesystem::path sourcePath;
	if (!ResolveAssetPath(
		sourceCopy.assetPath.empty() ? sourceCopy.filePath : sourceCopy.assetPath,
		sourceAssetPath,
		sourcePath,
		errorMessage
	)) {
		return false;
	}

	std::string assetPath;
	std::filesystem::path targetPath;
	if (!ResolveAssetPath(
		requestedAssetPath,
		assetPath,
		targetPath,
		errorMessage
	)) {
		return false;
	}
	if (!SamePath(sourcePath, targetPath)) {
		std::error_code mainError;
		std::error_code backupError;
		const bool mainExists = std::filesystem::exists(targetPath, mainError);
		const bool backupExists = std::filesystem::exists(
			BackupPath(targetPath),
			backupError
		);
		if (mainExists || backupExists) {
			errorMessage = "Scene asset path is already in use: " + assetPath;
			return false;
		}
		if (mainError || backupError) {
			errorMessage = "Scene asset path could not be inspected: " + assetPath;
			return false;
		}
	}

	SceneDocument document;
	if (!document.Load(sourceCopy.filePath)) {
		errorMessage = "Scene could not be loaded before rename: " +
			sourceAssetPath + " | " + document.GetLastLoadError();
		return false;
	}
	const bool loadedFromBackup = document.IsDirty();
	document.SetSceneName(displayName);

	const std::filesystem::path stagedMain = TemporaryPath(
		sourcePath,
		L".sceneasset.rename.tmp"
	);
	const std::filesystem::path sourceBackup = BackupPath(sourcePath);
	const std::filesystem::path stagedBackup = TemporaryPath(
		sourceBackup,
		L".sceneasset.rename.tmp"
	);
	bool mainStaged = false;
	bool backupStaged = false;
	if (!StageIfPresent(sourcePath, stagedMain, mainStaged, errorMessage) ||
		!StageIfPresent(sourceBackup, stagedBackup, backupStaged, errorMessage)) {
		RestoreStagedFile(stagedMain, sourcePath, mainStaged);
		return false;
	}
	if (!mainStaged && !backupStaged) {
		errorMessage = "Scene file does not exist: " + sourceAssetPath;
		return false;
	}

	const SceneCatalog catalogSnapshot = *catalog_;
	if (!document.Save(StringUtility::ToUtf8(targetPath)) ||
		!catalog_->UpdateScene(sceneId, displayName, assetPath, errorMessage) ||
		!catalog_->Save(errorMessage)) {
		*catalog_ = catalogSnapshot;
		RemoveGeneratedSceneFiles(targetPath);
		RestoreStagedFile(stagedMain, sourcePath, mainStaged);
		RestoreStagedFile(stagedBackup, sourceBackup, backupStaged);
		if (errorMessage.empty()) {
			errorMessage = "Renamed Scene file could not be saved: " + assetPath;
		}
		return false;
	}

	const std::filesystem::path targetBackup = BackupPath(targetPath);
	RemoveIfPresent(targetBackup);
	const std::filesystem::path& preferredBackup =
		loadedFromBackup && backupStaged ? stagedBackup : stagedMain;
	const bool preferStagedBackup = loadedFromBackup && backupStaged;
	const bool hasPreferredBackup = loadedFromBackup && backupStaged
		? backupStaged
		: mainStaged;
	bool preferredBackupMoved = false;
	if (hasPreferredBackup) {
		std::error_code backupError;
		std::filesystem::rename(preferredBackup, targetBackup, backupError);
		preferredBackupMoved = !backupError;
	}
	if (preferStagedBackup || !hasPreferredBackup || preferredBackupMoved) {
		RemoveIfPresent(stagedMain);
	}
	if (!preferStagedBackup || !hasPreferredBackup || preferredBackupMoved) {
		RemoveIfPresent(stagedBackup);
	}
	errorMessage.clear();
	return true;
}

bool SceneAssetService::DeleteScene(
	const std::string& sceneId,
	std::string& errorMessage
) {
	if (!catalog_) {
		errorMessage = "Scene Catalog is not initialized";
		return false;
	}
	const SceneDescriptor* source = catalog_->Find(sceneId);
	if (!source) {
		errorMessage = "Scene is not registered: " + sceneId;
		return false;
	}
	if (source->id == catalog_->GetStartSceneId()) {
		errorMessage = "The start Scene cannot be deleted";
		return false;
	}
	const SceneDescriptor sourceCopy = *source;
	std::string referencingSceneId;
	if (FindSceneReference(sourceCopy.id, referencingSceneId, errorMessage)) {
		errorMessage = "Scene is referenced by another Scene in: " +
			referencingSceneId;
		return false;
	}
	if (!errorMessage.empty()) {
		return false;
	}

	std::string sourceAssetPath;
	std::filesystem::path sourcePath;
	if (!ResolveAssetPath(
		sourceCopy.assetPath.empty() ? sourceCopy.filePath : sourceCopy.assetPath,
		sourceAssetPath,
		sourcePath,
		errorMessage
	)) {
		return false;
	}
	const std::filesystem::path sourceBackup = BackupPath(sourcePath);
	const std::filesystem::path stagedMain = TemporaryPath(
		sourcePath,
		L".sceneasset.delete.tmp"
	);
	const std::filesystem::path stagedBackup = TemporaryPath(
		sourceBackup,
		L".sceneasset.delete.tmp"
	);
	bool mainStaged = false;
	bool backupStaged = false;
	if (!StageIfPresent(sourcePath, stagedMain, mainStaged, errorMessage) ||
		!StageIfPresent(sourceBackup, stagedBackup, backupStaged, errorMessage)) {
		RestoreStagedFile(stagedMain, sourcePath, mainStaged);
		return false;
	}
	if (!mainStaged && !backupStaged) {
		errorMessage = "Scene file does not exist: " + sourceAssetPath;
		return false;
	}

	const SceneCatalog catalogSnapshot = *catalog_;
	if (!catalog_->UnregisterScene(sceneId, errorMessage) ||
		!catalog_->Save(errorMessage)) {
		*catalog_ = catalogSnapshot;
		RestoreStagedFile(stagedMain, sourcePath, mainStaged);
		RestoreStagedFile(stagedBackup, sourceBackup, backupStaged);
		return false;
	}

	RemoveIfPresent(stagedMain);
	RemoveIfPresent(stagedBackup);
	errorMessage.clear();
	return true;
}

bool SceneAssetService::ResolveAssetPath(
	const std::string& requestedPath,
	std::string& assetPath,
	std::filesystem::path& resolvedPath,
	std::string& errorMessage
) const {
	if (requestedPath.empty()) {
		errorMessage = "Scene asset path cannot be empty";
		return false;
	}
	resolvedPath = EditableResourcePath::Resolve(
		StringUtility::ToPath(requestedPath)
	).lexically_normal();
	const std::filesystem::path sceneRoot = EditableResourcePath::Resolve(
		"resources/scenes"
	).lexically_normal();
	std::error_code relativeError;
	const std::filesystem::path relativeToSceneRoot = std::filesystem::relative(
		resolvedPath,
		sceneRoot,
		relativeError
	);
	if (relativeError || relativeToSceneRoot.empty() ||
		relativeToSceneRoot.is_absolute() ||
		*relativeToSceneRoot.begin() == "..") {
		errorMessage = "Scene assets must be inside resources/scenes";
		return false;
	}
	const std::string fileName = StringUtility::ToUtf8(resolvedPath.filename());
	if (!fileName.ends_with(".scene.json")) {
		errorMessage = "Scene asset file must end with .scene.json";
		return false;
	}

	assetPath = StringUtility::ToUtf8(
		EditableResourcePath::ToProjectRelative(resolvedPath)
	);
	std::replace(assetPath.begin(), assetPath.end(), '\\', '/');
	errorMessage.clear();
	return true;
}

bool SceneAssetService::ValidateNewScene(
	const std::string& sceneId,
	const std::string& displayName,
	const std::filesystem::path& resolvedPath,
	std::string& errorMessage
) const {
	if (!IsValidSceneId(sceneId)) {
		errorMessage = "Scene ID may only contain letters, numbers, '-' and '_'";
		return false;
	}
	if (displayName.empty()) {
		errorMessage = "Scene display name cannot be empty";
		return false;
	}
	if (catalog_ && catalog_->Find(sceneId)) {
		errorMessage = "Duplicate scene id: " + sceneId;
		return false;
	}
	std::error_code mainError;
	std::error_code backupError;
	const bool mainExists = std::filesystem::exists(resolvedPath, mainError);
	const bool backupExists = std::filesystem::exists(
		BackupPath(resolvedPath),
		backupError
	);
	if (mainExists || backupExists) {
		errorMessage = "Scene asset path is already in use: " +
			StringUtility::ToUtf8(resolvedPath);
		return false;
	}
	if (mainError || backupError) {
		errorMessage = "Scene asset path could not be inspected: " +
			StringUtility::ToUtf8(resolvedPath);
		return false;
	}
	errorMessage.clear();
	return true;
}

bool SceneAssetService::FindSceneReference(
	const std::string& sceneId,
	std::string& referencingSceneId,
	std::string& errorMessage
) const {
	referencingSceneId.clear();
	errorMessage.clear();
	if (!catalog_) {
		errorMessage = "Scene Catalog is not initialized";
		return false;
	}
	for (const SceneDescriptor& descriptor : catalog_->GetScenes()) {
		if (descriptor.id == sceneId) {
			continue;
		}
		SceneDocument document;
		if (!document.Load(descriptor.filePath)) {
			errorMessage = "Scene references could not be checked because a Scene "
				"failed to load: " + descriptor.assetPath + " | " +
				document.GetLastLoadError();
			return false;
		}
		for (const SceneEntity& entity : document.GetEntities()) {
			for (const SceneComponent& component : entity.components) {
				const bool transitionReference =
					component.type == "SceneTransition" &&
					component.sceneTransitionTargetSceneId == sceneId;
				const bool entityReference =
					component.type == "EntityReference" &&
					component.entityReferenceTarget.sceneId == sceneId;
				if (transitionReference || entityReference) {
					referencingSceneId = descriptor.id;
					return true;
				}
			}
		}
	}
	return false;
}
