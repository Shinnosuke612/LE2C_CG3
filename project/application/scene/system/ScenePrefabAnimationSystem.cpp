// 役割: 軽量なPrefab内Property AnimationをScene Entityへ適用する。
#include "ScenePrefabAnimationSystem.h"

#include "../../../engine/math/Math.h"
#include "../../../engine/scene/SceneDocument.h"
#include "../../../engine/scene/SceneEntityQuery.h"

#include <algorithm>
#include <cmath>
#include <unordered_set>

namespace {
	SceneEntity* ResolveTrackTarget(
		SceneDocument& document,
		const SceneAnimationTrack& track,
		uint64_t ownerEntityId
	) {
		if (track.targetEntityId != 0) {
			if (SceneEntity* entity = document.FindEntity(track.targetEntityId)) {
				return entity;
			}
		}
		if (!track.targetEntityName.empty()) {
			if (SceneEntity* entity =
				document.FindEntityByName(track.targetEntityName)) {
				return entity;
			}
		}
		return document.FindEntity(ownerEntityId);
	}

	float ApplyEasing(float value, const std::string& easing) {
		value = std::clamp(value, 0.0f, 1.0f);
		return easing == "Linear" ? value : Math::SmoothStep(value);
	}

	Vector3 SampleTrack(const SceneAnimationTrack& track, float time) {
		if (track.keyframes.empty()) {
			return {};
		}
		if (time <= track.keyframes.front().time) {
			return track.keyframes.front().value;
		}
		for (size_t index = 1; index < track.keyframes.size(); ++index) {
			const SceneAnimationKeyframe& next = track.keyframes[index];
			if (time > next.time) {
				continue;
			}
			const SceneAnimationKeyframe& previous = track.keyframes[index - 1];
			const float duration = (std::max)(next.time - previous.time, 0.0001f);
			const float amount = ApplyEasing(
				(time - previous.time) / duration,
				track.easing
			);
			return {
				Math::Lerp(previous.value.x, next.value.x, amount),
				Math::Lerp(previous.value.y, next.value.y, amount),
				Math::Lerp(previous.value.z, next.value.z, amount)
			};
		}
		return track.keyframes.back().value;
	}

	Quaternion SampleRotation(const SceneAnimationTrack& track, float time) {
		if (track.keyframes.empty()) {
			return Quaternion{};
		}
		if (time <= track.keyframes.front().time) {
			return MakeQuaternionFromEuler(track.keyframes.front().value);
		}
		for (size_t index = 1; index < track.keyframes.size(); ++index) {
			const SceneAnimationKeyframe& next = track.keyframes[index];
			if (time > next.time) {
				continue;
			}
			const SceneAnimationKeyframe& previous = track.keyframes[index - 1];
			const float duration = (std::max)(next.time - previous.time, 0.0001f);
			const float amount = ApplyEasing(
				(time - previous.time) / duration,
				track.easing
			);
			return Slerp(
				MakeQuaternionFromEuler(previous.value),
				MakeQuaternionFromEuler(next.value),
				amount
			);
		}
		return MakeQuaternionFromEuler(track.keyframes.back().value);
	}
}

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

		for (const SceneAnimationTrack& track : clip.tracks) {
			SceneEntity* target = ResolveTrackTarget(document, track, entity.id);
			if (!target || track.keyframes.empty()) {
				continue;
			}
			if (track.property == "LocalPosition") {
				target->transform.translate = SampleTrack(track, runtime.time);
			} else if (track.property == "LocalScale") {
				target->transform.scale = SampleTrack(track, runtime.time);
			} else if (track.property == "LocalRotation") {
				target->transform.rotate = SampleRotation(track, runtime.time);
			} else if (track.property == "Active") {
				Vector3 value = track.keyframes.front().value;
				for (const SceneAnimationKeyframe& keyframe : track.keyframes) {
					if (keyframe.time > runtime.time) {
						break;
					}
					value = keyframe.value;
				}
				target->active = value.x >= 0.5f;
			}
		}
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
