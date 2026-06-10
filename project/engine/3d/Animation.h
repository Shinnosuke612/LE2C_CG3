#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include "../math/Quaternion.h"
#include "../math/Vector3.h"

template<typename TValue>
struct Keyframe {
	float time;
	TValue value;
};

template<typename TValue>
struct AnimationCurve {
	std::vector<Keyframe<TValue>> keyframes;
};

struct NodeAnimation {
	AnimationCurve<Vector3> translate;
	AnimationCurve<Quaternion> rotate;
	AnimationCurve<Vector3> scale;
};

struct Animation {
	float duration = 0.0f;
	std::unordered_map<std::string, NodeAnimation> nodeAnimations;

	bool IsValid() const {
		return duration > 0.0f && !nodeAnimations.empty();
	}
};

Animation LoadAnimationFile(
	const std::string& directoryPath,
	const std::string& filename
);

Vector3 CalculateValue(
	const AnimationCurve<Vector3>& curve,
	float time,
	const Vector3& defaultValue
);

Quaternion CalculateValue(
	const AnimationCurve<Quaternion>& curve,
	float time,
	const Quaternion& defaultValue
);

