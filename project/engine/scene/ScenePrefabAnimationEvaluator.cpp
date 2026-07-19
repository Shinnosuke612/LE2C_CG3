// 役割: Transform／Active TrackをRuntimeとEditor Previewで共通評価する。
#include "ScenePrefabAnimationEvaluator.h"

#include "SceneDocument.h"
#include "../math/Math.h"

#include <algorithm>

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
			const float duration = (std::max)(
				next.time - previous.time,
				0.0001f
			);
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
			const float duration = (std::max)(
				next.time - previous.time,
				0.0001f
			);
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

void ScenePrefabAnimationEvaluator::ApplyClip(
	SceneDocument& document,
	uint64_t ownerEntityId,
	const ScenePrefabAnimationClip& clip,
	float time
) {
	for (const SceneAnimationTrack& track : clip.tracks) {
		SceneEntity* target = ResolveTrackTarget(
			document,
			track,
			ownerEntityId
		);
		if (!target || track.keyframes.empty()) {
			continue;
		}
		if (track.property == "LocalPosition") {
			target->transform.translate = SampleTrack(track, time);
		} else if (track.property == "LocalScale") {
			target->transform.scale = SampleTrack(track, time);
		} else if (track.property == "LocalRotation") {
			target->transform.rotate = SampleRotation(track, time);
		} else if (track.property == "Active") {
			Vector3 value = track.keyframes.front().value;
			for (const SceneAnimationKeyframe& keyframe : track.keyframes) {
				if (keyframe.time > time) {
					break;
				}
				value = keyframe.value;
			}
			target->active = value.x >= 0.5f;
		}
	}
}
