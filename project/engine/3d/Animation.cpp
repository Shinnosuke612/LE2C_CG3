#include "Animation.h"

#include <algorithm>

#include <assimp/Importer.hpp>
#include <assimp/scene.h>

namespace {

template<typename TValue, typename TInterpolator>
TValue CalculateCurveValue(
	const AnimationCurve<TValue>& curve,
	float time,
	const TValue& defaultValue,
	TInterpolator interpolate
) {
	const auto& keyframes = curve.keyframes;
	if (keyframes.empty()) {
		return defaultValue;
	}
	if (keyframes.size() == 1 || time <= keyframes.front().time) {
		return keyframes.front().value;
	}

	for (size_t index = 0; index + 1 < keyframes.size(); ++index) {
		const Keyframe<TValue>& current = keyframes[index];
		const Keyframe<TValue>& next = keyframes[index + 1];
		if (time <= next.time) {
			const float duration = next.time - current.time;
			const float t = duration > 0.0f
				? (time - current.time) / duration
				: 0.0f;
			return interpolate(current.value, next.value, t);
		}
	}

	return keyframes.back().value;
}

Vector3 LerpVector3(const Vector3& start, const Vector3& end, float t) {
	t = std::clamp(t, 0.0f, 1.0f);
	return {
		start.x + (end.x - start.x) * t,
		start.y + (end.y - start.y) * t,
		start.z + (end.z - start.z) * t
	};
}

} // namespace

Animation LoadAnimationFile(
	const std::string& directoryPath,
	const std::string& filename
) {
	Animation animation{};
	Assimp::Importer importer;
	const std::string filePath = directoryPath + "/" + filename;
	const aiScene* scene = importer.ReadFile(filePath.c_str(), 0);
	if (scene == nullptr || scene->mNumAnimations == 0) {
		return animation;
	}

	const aiAnimation* animationAssimp = scene->mAnimations[0];
	const double ticksPerSecond =
		animationAssimp->mTicksPerSecond != 0.0
		? animationAssimp->mTicksPerSecond
		: 1.0;
	animation.duration = static_cast<float>(
		animationAssimp->mDuration / ticksPerSecond
	);

	for (uint32_t channelIndex = 0;
		channelIndex < animationAssimp->mNumChannels;
		++channelIndex) {
		const aiNodeAnim* nodeAnimationAssimp =
			animationAssimp->mChannels[channelIndex];
		NodeAnimation& nodeAnimation =
			animation.nodeAnimations[nodeAnimationAssimp->mNodeName.C_Str()];

		nodeAnimation.translate.keyframes.reserve(
			nodeAnimationAssimp->mNumPositionKeys
		);
		for (uint32_t keyIndex = 0;
			keyIndex < nodeAnimationAssimp->mNumPositionKeys;
			++keyIndex) {
			const aiVectorKey& key = nodeAnimationAssimp->mPositionKeys[keyIndex];
			nodeAnimation.translate.keyframes.push_back({
				static_cast<float>(key.mTime / ticksPerSecond),
				{ -key.mValue.x, key.mValue.y, key.mValue.z }
			});
		}

		nodeAnimation.rotate.keyframes.reserve(
			nodeAnimationAssimp->mNumRotationKeys
		);
		for (uint32_t keyIndex = 0;
			keyIndex < nodeAnimationAssimp->mNumRotationKeys;
			++keyIndex) {
			const aiQuatKey& key = nodeAnimationAssimp->mRotationKeys[keyIndex];
			nodeAnimation.rotate.keyframes.push_back({
				static_cast<float>(key.mTime / ticksPerSecond),
				Normalize({
					key.mValue.x,
					-key.mValue.y,
					-key.mValue.z,
					key.mValue.w
				})
			});
		}

		nodeAnimation.scale.keyframes.reserve(
			nodeAnimationAssimp->mNumScalingKeys
		);
		for (uint32_t keyIndex = 0;
			keyIndex < nodeAnimationAssimp->mNumScalingKeys;
			++keyIndex) {
			const aiVectorKey& key = nodeAnimationAssimp->mScalingKeys[keyIndex];
			nodeAnimation.scale.keyframes.push_back({
				static_cast<float>(key.mTime / ticksPerSecond),
				{ key.mValue.x, key.mValue.y, key.mValue.z }
			});
		}
	}

	return animation;
}

Vector3 CalculateValue(
	const AnimationCurve<Vector3>& curve,
	float time,
	const Vector3& defaultValue
) {
	return CalculateCurveValue(
		curve,
		time,
		defaultValue,
		[](const Vector3& start, const Vector3& end, float t) {
			return LerpVector3(start, end, t);
		}
	);
}

Quaternion CalculateValue(
	const AnimationCurve<Quaternion>& curve,
	float time,
	const Quaternion& defaultValue
) {
	return CalculateCurveValue(
		curve,
		time,
		defaultValue,
		[](const Quaternion& start, const Quaternion& end, float t) {
			return Slerp(start, end, t);
		}
	);
}

