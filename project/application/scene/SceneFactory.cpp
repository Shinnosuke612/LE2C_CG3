// 役割: 実行プロファイルに対応するScene Controllerを生成する。
#include "SceneFactory.h"

#include "RuntimeScene.h"

BaseScene* SceneFactory::CreateScene(const std::string& runtimeProfile) {
	if (runtimeProfile == "RUNTIME") {
		return new RuntimeScene();
	}

	return nullptr;
}
