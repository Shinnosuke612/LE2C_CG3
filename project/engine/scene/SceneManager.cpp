#include "SceneManager.h"

#include <cassert>

#include "AbstractSceneFactory.h"
#include "BaseScene.h"

void SceneManager::ChangeScene(const std::string& sceneName) {
	assert(sceneFactory_);
	assert(!nextScene_);
	if (!sceneFactory_ || nextScene_) {
		return;
	}

	BaseScene* newScene = sceneFactory_->CreateScene(sceneName);
	assert(newScene);
	if (!newScene) {
		return;
	}

	nextScene_ = newScene;
	nextSceneName_ = sceneName;
}

void SceneManager::Update()
{
	// 次のシーンの予約があるなら
	if (nextScene_) {
		if (scene_) {
			// 旧シーンの終了
			scene_->Finalize();
			delete scene_;
		}

		// シーン切り替え
		scene_ = nextScene_;
		nextScene_ = nullptr;
		currentSceneName_ = nextSceneName_;
		nextSceneName_.clear();

		scene_->SetSceneManager(this);

		// 次のシーンの初期化
		if (scene_) {
			scene_->Initialize();
		}
	}

	// 現在のシーンがあれば毎フレーム更新
	if (scene_) {
		scene_->Update();
	}
}

void SceneManager::Draw()
{
	if (scene_) {
		scene_->Draw();
	}
}

void SceneManager::DrawShadow()
{
	if (scene_) {
		scene_->DrawShadow();
	}
}

SceneManager::~SceneManager()
{
	if (scene_) {
		scene_->Finalize();
		delete scene_;
		scene_ = nullptr;
	}

	delete nextScene_;
	nextScene_ = nullptr;
}
