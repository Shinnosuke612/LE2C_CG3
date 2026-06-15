#pragma once

#include <string>

class AbstractSceneFactory;
class BaseScene;

class SceneManager
{
public:
	~SceneManager();

	void Update();

	void Draw();
	void DrawShadow();

	void ChangeScene(const std::string& sceneName);
	void SetSceneFactory(AbstractSceneFactory* sceneFactory) {
		sceneFactory_ = sceneFactory;
	}
	const std::string& GetCurrentSceneName() const {
		return currentSceneName_;
	}

private:
	BaseScene* scene_ = nullptr;
	BaseScene* nextScene_ = nullptr;
	AbstractSceneFactory* sceneFactory_ = nullptr;
	std::string currentSceneName_;
	std::string nextSceneName_;
};

