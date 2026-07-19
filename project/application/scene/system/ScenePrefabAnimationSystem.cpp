// 役割: 軽量なPrefab内Property AnimationをScene Entityへ適用する。
#include "ScenePrefabAnimationSystem.h"

#include "../../../engine/scene/SceneDocument.h"
#include "../../../engine/scene/SceneEntityQuery.h"
#include "../../../engine/scene/ScenePrefabAnimationEvaluator.h"

#include <algorithm>
#include <cmath>
#include <unordered_set>

void ScenePrefabAnimationSystem::Update(
	SceneDocument& document,
	float deltaTime
) {
	std::unordered_set<uint64_t> requiredEntities;
	for (SceneEntity& entity : document.GetEntities()) {
		if (!SceneEntityQuery::IsEntityActiveInHierarchy(document, entity)) {
			continue;
		}
		const SceneComponent* animator =
			SceneEntityQuery::FindEnabledComponent(entity, "PrefabAnimator");
		if (!animator || animator->prefabAnimationClips.empty()) {
			continue;
		}
		requiredEntities.insert(entity.id);
		AnimationRuntime& runtime = runtimes_[entity.id];
		if (!runtime.initialized) {
			runtime.initialized = true;
			for (size_t index = 0;
				index < animator->prefabAnimationClips.size();
				++index) {
				if (animator->prefabAnimationClips[index].playOnStart) {
					runtime.clipIndex = index;
					runtime.playing = true;
					break;
				}
			}
		}
		if (!runtime.playing || runtime.clipIndex >= animator->prefabAnimationClips.size()) {
			continue;
		}
		const ScenePrefabAnimationClip& clip =
			animator->prefabAnimationClips[runtime.clipIndex];
		runtime.time += (std::max)(deltaTime, 0.0f);
		const float duration = (std::max)(clip.duration, 0.001f);
		if (clip.loop) {
			runtime.time = std::fmod(runtime.time, duration);
		} else if (runtime.time >= duration) {
			runtime.time = duration;
			runtime.playing = false;
		}

		ScenePrefabAnimationEvaluator::ApplyClip(
			document,
			entity.id,
			clip,
			runtime.time
		);
	}

	for (auto iterator = runtimes_.begin(); iterator != runtimes_.end();) {
		if (!requiredEntities.contains(iterator->first)) {
			iterator = runtimes_.erase(iterator);
		} else {
			++iterator;
		}
	}
}

bool ScenePrefabAnimationSystem::Play(
	const SceneDocument& document,
	uint64_t entityId,
	const std::string& clipName,
	bool restart
) {
	const SceneEntity* entity = document.FindEntity(entityId);
	const SceneComponent* animator = entity
		? SceneEntityQuery::FindEnabledComponent(*entity, "PrefabAnimator")
		: nullptr;
	if (!animator) {
		return false;
	}
	for (size_t index = 0; index < animator->prefabAnimationClips.size(); ++index) {
		if (animator->prefabAnimationClips[index].name != clipName) {
			continue;
		}
		AnimationRuntime& runtime = runtimes_[entityId];
		if (restart || runtime.clipIndex != index) {
			runtime.time = 0.0f;
		}
		runtime.clipIndex = index;
		runtime.initialized = true;
		runtime.playing = true;
		return true;
	}
	return false;
}

void ScenePrefabAnimationSystem::Clear() {
	runtimes_.clear();
}
