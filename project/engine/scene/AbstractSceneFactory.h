// 役割: SceneManagerが利用するシーン生成インターフェースを定義する。
#pragma once

#include <string>

class BaseScene;

// Interface for creating scenes without exposing concrete scene classes.
class AbstractSceneFactory {
public:
	virtual ~AbstractSceneFactory() = default;

	virtual BaseScene* CreateScene(const std::string& runtimeProfile) = 0;
};
