// 役割: Transform／Active TrackをRuntimeとEditor Previewで共通評価する。
#include "ScenePrefabAnimationEvaluator.h"

#include "SceneDocument.h"
#include "../math/Math.h"

#include <algorithm>
#include <cmath>

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
		if (easing == "Linear") {
			return value;
		}
		if (easing == "EaseIn") {
			return value * value * value;
		}
		if (easing == "EaseOut") {
			return Math::EaseOutCubic(value);
		}
		if (easing == "EaseInOut") {
			return value < 0.5f
				? 4.0f * value * value * value
				: 1.0f - std::pow(-2.0f * value + 2.0f, 3.0f) * 0.5f;
		}
		return Math::SmoothStep(value);
	}

	const std::string& ResolveSegmentEasing(
		const SceneAnimationTrack& track,
		const SceneAnimationKeyframe& startKey
	) {
		return startKey.easingToNext.empty()
			? track.easing
			: startKey.easingToNext;
	}

	Vector3 SampleTrack(
		const SceneAnimationTrack& track,
		float time,
		bool applyPositionBulge
	) {
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
				ResolveSegmentEasing(track, previous)
			);
			Vector3 result{
				Math::Lerp(previous.value.x, next.value.x, amount),
				Math::Lerp(previous.value.y, next.value.y, amount),
				Math::Lerp(previous.value.z, next.value.z, amount)
			};
			if (applyPositionBulge) {
				// 4t(1-t) makes positionBulge the exact offset at the
				// interpolation midpoint while preserving both Pose endpoints.
				const float bulgeAmount = 4.0f * amount * (1.0f - amount);
				result.x += previous.positionBulge.x * bulgeAmount;
				result.y += previous.positionBulge.y * bulgeAmount;
				result.z += previous.positionBulge.z * bulgeAmount;
			}
			return result;
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
				ResolveSegmentEasing(track, previous)
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
			target->transform.translate = SampleTrack(track, time, true);
		} else if (track.property == "LocalScale") {
			target->transform.scale = SampleTrack(track, time, false);
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
