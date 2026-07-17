// 役割: PrefabAnimatorのTransform/Activeキーフレームを再生する。
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>

class SceneDocument;

class ScenePrefabAnimationSystem {
public:
	void Update(SceneDocument& document, float deltaTime);
	bool Play(
		const SceneDocument& document,
		uint64_t entityId,
		const std::string& clipName,
		bool restart = true
	);
	void Clear();

private:
	struct AnimationRuntime {
		size_t clipIndex = 0;
		float time = 0.0f;
		bool initialized = false;
		bool playing = false;
	};

	std::unordered_map<uint64_t, AnimationRuntime> runtimes_;
};
