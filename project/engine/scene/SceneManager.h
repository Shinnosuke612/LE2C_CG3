// 役割: 現在のシーンとSceneFactoryを管理し、シーン遷移を実行する。
#pragma once

#include <string>

class AbstractSceneFactory;
class BaseScene;
class EditorSession;
class SceneDocument;

class SceneManager
{
public:
	~SceneManager();

	void Update(float deltaTime);
	void UpdatePaused();

	void Draw();
	void DrawForegroundEffects();
	void DrawShadow();
	void DrawOffscreenViews();
	void SetDeferForegroundEffects(bool defer);

	void ChangeScene(const std::string& sceneName);
	void ReloadCurrentScene();
	void SetSceneFactory(AbstractSceneFactory* sceneFactory) {
		sceneFactory_ = sceneFactory;
	}
	const std::string& GetCurrentSceneName() const {
		return currentSceneName_;
	}
	void SetEditorSession(EditorSession* editorSession) {
		editorSession_ = editorSession;
	}
	EditorSession* GetEditorSession() const { return editorSession_; }
	SceneDocument* GetActiveSceneDocument() const;

private:
	BaseScene* scene_ = nullptr;
	BaseScene* nextScene_ = nullptr;
	AbstractSceneFactory* sceneFactory_ = nullptr;
	std::string currentSceneName_;
	std::string nextSceneName_;
	EditorSession* editorSession_ = nullptr;
};

