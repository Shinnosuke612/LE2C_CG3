#include "SceneManager.h"

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
	scene_->Draw();
}

void SceneManager::DrawShadow()
{
	if (scene_) {
		scene_->DrawShadow();
	}
}

SceneManager::~SceneManager()
{
	scene_->Finalize();
	delete scene_;
}
