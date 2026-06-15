#pragma once

#include <string>

class BaseScene;

// Interface for creating scenes without exposing concrete scene classes.
class AbstractSceneFactory {
public:
	virtual ~AbstractSceneFactory() = default;

	virtual BaseScene* CreateScene(const std::string& sceneName) = 0;
};
