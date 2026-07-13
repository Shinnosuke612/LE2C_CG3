// 役割: モデル階層から生成したSkeletonとJoint探索を定義する。
#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "../math/Matrix4x4.h"
#include "../math/Transform.h"
#include "Animation.h"
#include "Model.h"

struct Joint {
	QuaternionTransform transform;
	QuaternionTransform bindTransform;
	Matrix4x4 localMatrix = MakeIdentity4x4();
	Matrix4x4 skeletonSpaceMatrix = MakeIdentity4x4();
	std::string name;
	std::vector<int32_t> children;
	int32_t index = -1;
	std::optional<int32_t> parent;
};

struct Skeleton {
	int32_t root = -1;
	std::map<std::string, int32_t> jointMap;
	std::vector<Joint> joints;

	bool IsValid() const {
		return root >= 0 && root < static_cast<int32_t>(joints.size());
	}
};

Skeleton CreateSkeleton(const Model::Node& rootNode);
void ApplyAnimation(
	Skeleton& skeleton,
	const Animation& animation,
	float animationTime
);
void ApplyAnimationBlend(
	Skeleton& skeleton,
	const Animation& previousAnimation,
	float previousTime,
	const Animation& currentAnimation,
	float currentTime,
	float blendWeight
);
void ResetSkeletonPose(Skeleton& skeleton);
void UpdateSkeleton(Skeleton& skeleton);
