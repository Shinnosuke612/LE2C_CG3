// 役割: タイトルとゲームプレイのシーン生成を実装する。
#include "SceneFactory.h"

#include "GamePlayScene.h"
#include "TitleScene.h"

BaseScene* SceneFactory::CreateScene(const std::string& sceneName) {
	if (sceneName == "TITLE") {
		return new TitleScene();
	}
	if (sceneName == "GAMEPLAY") {
		return new GamePlayScene();
	}

	return nullptr;
}
