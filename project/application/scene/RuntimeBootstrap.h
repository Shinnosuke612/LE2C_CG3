// 役割: Editorを生成せずRuntime専用SessionとSceneManagerを構築する。
#pragma once

#include <string>

class AbstractSceneFactory;
class RuntimeSession;
class SceneCatalog;
struct SceneDescriptor;
class SceneManager;

class RuntimeBootstrap final {
public:
	~RuntimeBootstrap();

	bool Initialize(
		SceneCatalog* sceneCatalog,
		AbstractSceneFactory* sceneFactory,
		const SceneDescriptor& startScene,
		std::string& errorMessage
	);

	SceneManager* GetSceneManager() const { return sceneManager_; }
	RuntimeSession* GetRuntimeSession() const { return runtimeSession_; }

private:
	RuntimeSession* runtimeSession_ = nullptr;
	SceneManager* sceneManager_ = nullptr;
};
