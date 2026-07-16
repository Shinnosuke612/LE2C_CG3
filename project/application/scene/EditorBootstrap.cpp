// 役割: Editor用Scene実行環境を構築し、編集対象Sceneを開始する。
#include "EditorBootstrap.h"

#include "../../engine/scene/EditorSession.h"
#include "../../engine/scene/SceneAssetService.h"
#include "../../engine/scene/SceneCatalog.h"
#include "../../engine/scene/SceneManager.h"
#include "../../engine/scene/SceneTemplateRegistry.h"
#include "../../engine/utility/StringUtility.h"

#include <filesystem>
#include <system_error>

bool EditorBootstrap::Initialize(
	SceneCatalog* sceneCatalog,
	AbstractSceneFactory* sceneFactory,
	const SceneDescriptor& startScene,
	std::string& errorMessage
) {
	if (!sceneCatalog || !sceneFactory) {
		errorMessage = "Editor bootstrap dependencies are not available";
		return false;
	}

	editorSession_ = new EditorSession();
	if (!editorSession_->Initialize(
		startScene.id,
		startScene.displayName,
		startScene.filePath
	)) {
		const std::filesystem::path scenePath =
			StringUtility::ToPath(startScene.filePath);
		std::filesystem::path backupPath = scenePath;
		backupPath += L".bak";
		std::error_code fileError;
		const bool sceneExists = std::filesystem::exists(scenePath, fileError);
		const bool backupExists = !fileError &&
			std::filesystem::exists(backupPath, fileError);
		if (fileError || sceneExists || backupExists) {
			errorMessage = editorSession_->GetLastLoadError();
			if (errorMessage.empty()) {
				errorMessage =
					"Edit Scene could not be loaded: " + startScene.filePath;
			}
			return false;
		}
		if (!editorSession_->Save()) {
			errorMessage =
				"Edit Scene could not be created: " + startScene.filePath;
			return false;
		}
	}

	sceneTemplateRegistry_ = new SceneTemplateRegistry();
	sceneAssetService_ = new SceneAssetService(
		sceneCatalog,
		sceneTemplateRegistry_
	);
	sceneManager_ = new SceneManager();
	sceneManager_->SetExecutionContext(editorSession_);
	sceneManager_->SetSceneCatalog(sceneCatalog);
	sceneManager_->SetSceneFactory(sceneFactory);
	sceneManager_->ChangeScene(startScene.id);

	errorMessage.clear();
	return true;
}

EditorBootstrap::~EditorBootstrap() {
	delete sceneManager_;
	sceneManager_ = nullptr;
	delete sceneAssetService_;
	sceneAssetService_ = nullptr;
	delete sceneTemplateRegistry_;
	sceneTemplateRegistry_ = nullptr;
	delete editorSession_;
	editorSession_ = nullptr;
}
