#include "Skeleton.h"

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
		joint.transform = joint.bindTransform;

		const auto animationIt = animation.nodeAnimations.find(joint.name);
		if (animationIt == animation.nodeAnimations.end()) {
			continue;
		}

		const NodeAnimation& nodeAnimation = animationIt->second;
		joint.transform.translate = CalculateValue(
			nodeAnimation.translate,
			animationTime,
			joint.bindTransform.translate
		);
		joint.transform.rotate = CalculateValue(
			nodeAnimation.rotate,
			animationTime,
			joint.bindTransform.rotate
		);
		joint.transform.scale = CalculateValue(
			nodeAnimation.scale,
			animationTime,
			joint.bindTransform.scale
		);
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
