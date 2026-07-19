// 役割: Prefab Animation Clipを指定時刻でScene Documentへ適用する。
#pragma once

#include <cstdint>

class SceneDocument;
struct ScenePrefabAnimationClip;

namespace ScenePrefabAnimationEvaluator {
	void ApplyClip(
		SceneDocument& document,
		uint64_t ownerEntityId,
		const ScenePrefabAnimationClip& clip,
		float time
	);
}
