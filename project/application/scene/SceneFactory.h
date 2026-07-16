// 役割: 実行プロファイルに対応するBaseScene派生クラスを生成する。
#pragma once

#include "../../engine/scene/AbstractSceneFactory.h"

class SceneFactory : public AbstractSceneFactory {
public:
	BaseScene* CreateScene(const std::string& runtimeProfile) override;
};
