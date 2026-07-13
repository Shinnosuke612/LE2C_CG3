#include "Skeleton.h"

#include <algorithm>
#include <cassert>

namespace {

int32_t CreateJoint(
	const Model::Node& node,
	std::optional<int32_t> parent,
	std::vector<Joint>& joints
) {
	Joint joint{};
	joint.transform = node.transform;
	joint.bindTransform = node.transform;
	joint.localMatrix = node.localMatrix;
	joint.skeletonSpaceMatrix = MakeIdentity4x4();
	joint.name = node.name;
	joint.index = static_cast<int32_t>(joints.size());
	joint.parent = parent;

	const int32_t jointIndex = joint.index;
	joints.push_back(joint);

	for (const Model::Node& child : node.children) {
		const int32_t childIndex = CreateJoint(child, jointIndex, joints);
		joints[jointIndex].children.push_back(childIndex);
	}

	return jointIndex;
}

QuaternionTransform SampleJointTransform(
	const Joint& joint,
	const Animation& animation,
	float animationTime
) {
	QuaternionTransform result = joint.bindTransform;
	const auto animationIt = animation.nodeAnimations.find(joint.name);
	if (animationIt == animation.nodeAnimations.end()) {
		return result;
	}

	const NodeAnimation& nodeAnimation = animationIt->second;
	result.translate = CalculateValue(
		nodeAnimation.translate,
		animationTime,
		joint.bindTransform.translate
	);
	result.rotate = CalculateValue(
		nodeAnimation.rotate,
		animationTime,
		joint.bindTransform.rotate
	);
	result.scale = CalculateValue(
		nodeAnimation.scale,
		animationTime,
		joint.bindTransform.scale
	);
	return result;
}

Vector3 BlendVector3(const Vector3& start, const Vector3& end, float weight) {
	return {
		start.x + (end.x - start.x) * weight,
		start.y + (end.y - start.y) * weight,
		start.z + (end.z - start.z) * weight
	};
}

} // namespace

Skeleton CreateSkeleton(const Model::Node& rootNode) {
	Skeleton skeleton{};
	skeleton.root = CreateJoint(rootNode, std::nullopt, skeleton.joints);

	for (const Joint& joint : skeleton.joints) {
		skeleton.jointMap.emplace(joint.name, joint.index);
	}

	UpdateSkeleton(skeleton);
	return skeleton;
}

void ApplyAnimation(
	Skeleton& skeleton,
	const Animation& animation,
	float animationTime
) {
	for (Joint& joint : skeleton.joints) {
		joint.transform = SampleJointTransform(joint, animation, animationTime);
	}
}

void ApplyAnimationBlend(
	Skeleton& skeleton,
	const Animation& previousAnimation,
	float previousTime,
	const Animation& currentAnimation,
	float currentTime,
	float blendWeight
) {
	blendWeight = std::clamp(blendWeight, 0.0f, 1.0f);
	for (Joint& joint : skeleton.joints) {
		const QuaternionTransform previous = SampleJointTransform(
			joint,
			previousAnimation,
			previousTime
		);
		const QuaternionTransform current = SampleJointTransform(
			joint,
			currentAnimation,
			currentTime
		);
		joint.transform.translate = BlendVector3(
			previous.translate,
			current.translate,
			blendWeight
		);
		joint.transform.rotate = Slerp(
			previous.rotate,
			current.rotate,
			blendWeight
		);
		joint.transform.scale = BlendVector3(
			previous.scale,
			current.scale,
			blendWeight
		);
	}
}

void ResetSkeletonPose(Skeleton& skeleton) {
	for (Joint& joint : skeleton.joints) {
		joint.transform = joint.bindTransform;
	}
}

void UpdateSkeleton(Skeleton& skeleton) {
	for (Joint& joint : skeleton.joints) {
		joint.localMatrix = MakeAffineMatrix(
			joint.transform.scale,
			joint.transform.rotate,
			joint.transform.translate
		);

		if (joint.parent.has_value()) {
			assert(*joint.parent >= 0);
			assert(*joint.parent < joint.index);
			joint.skeletonSpaceMatrix = Multiply(
				joint.localMatrix,
				skeleton.joints[*joint.parent].skeletonSpaceMatrix
			);
		}
		else {
			joint.skeletonSpaceMatrix = joint.localMatrix;
		}
	}
}
