// 役割: SceneやPrefabに依存しないEditor用World Gridの線分を生成する。
#pragma once

#include "../math/Vector3.h"
#include "../math/Vector4.h"

class DebugRenderer;

struct EditorGridSettings {
	Vector3 origin{};
	Vector3 axisU{ 1.0f, 0.0f, 0.0f };
	Vector3 axisV{ 0.0f, 0.0f, 1.0f };
	float spacing = 1.0f;
	float extent = 50.0f;
	int majorLineEvery = 10;
	Vector4 minorColor{ 0.46f, 0.49f, 0.54f, 0.16f };
	Vector4 majorColor{ 0.58f, 0.62f, 0.68f, 0.34f };
	Vector4 axisUColor{ 0.95f, 0.22f, 0.18f, 0.78f };
	Vector4 axisVColor{ 0.20f, 0.46f, 1.0f, 0.78f };
};

class EditorGridRenderer final {
public:
	static void AddGrid(
		DebugRenderer& debugRenderer,
		const EditorGridSettings& settings = {}
	);
};
