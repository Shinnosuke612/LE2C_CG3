// 役割: Runtime専用SceneDocumentを読み込み、Scene実行環境を構築する。
#include "RuntimeBootstrap.h"

#include "../../engine/scene/RuntimeSession.h"
#include "../../engine/scene/SceneCatalog.h"
#include "../../engine/scene/SceneManager.h"

bool RuntimeBootstrap::Initialize(
	SceneCatalog* sceneCatalog,
	AbstractSceneFactory* sceneFactory,
	const SceneDescriptor& startScene,
	std::string& errorMessage
) {
	if (!sceneCatalog || !sceneFactory) {
		errorMessage = "Runtime bootstrap dependencies are not available";
		return false;
	}

	runtimeSession_ = new RuntimeSession();
	if (!runtimeSession_->Initialize(startScene.id, startScene.filePath)) {
		errorMessage = runtimeSession_->GetLastLoadError();
		if (errorMessage.empty()) {
			errorMessage =
				"Runtime startup Scene could not be loaded: " + startScene.filePath;
		}
		return false;
	}

	sceneManager_ = new SceneManager();
	sceneManager_->SetExecutionContext(runtimeSession_);
	sceneManager_->SetSceneCatalog(sceneCatalog);
	sceneManager_->SetSceneFactory(sceneFactory);
	sceneManager_->ChangeScene(startScene.id);

	errorMessage.clear();
	return true;
}

RuntimeBootstrap::~RuntimeBootstrap() {
	delete sceneManager_;
	sceneManager_ = nullptr;
	delete runtimeSession_;
	runtimeSession_ = nullptr;
}
