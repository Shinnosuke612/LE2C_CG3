// 役割: Editor起動に必要なSession、SceneManager、Sceneアセットサービスを構築する。
#pragma once

#include <string>

class AbstractSceneFactory;
class EditorSession;
class SceneAssetService;
class SceneCatalog;
struct SceneDescriptor;
class SceneManager;
class SceneTemplateRegistry;

class EditorBootstrap final {
public:
	~EditorBootstrap();

	bool Initialize(
		SceneCatalog* sceneCatalog,
		AbstractSceneFactory* sceneFactory,
		const SceneDescriptor& startScene,
		std::string& errorMessage
	);

	SceneManager* GetSceneManager() const { return sceneManager_; }
	EditorSession* GetEditorSession() const { return editorSession_; }
	SceneAssetService* GetSceneAssetService() const {
		return sceneAssetService_;
	}
	SceneTemplateRegistry* GetSceneTemplateRegistry() const {
		return sceneTemplateRegistry_;
	}

private:
	EditorSession* editorSession_ = nullptr;
	SceneTemplateRegistry* sceneTemplateRegistry_ = nullptr;
	SceneAssetService* sceneAssetService_ = nullptr;
	SceneManager* sceneManager_ = nullptr;
};
