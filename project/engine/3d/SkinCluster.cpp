#include "SkinCluster.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstring>
#include <numeric>

#include "Model.h"
#include "Skeleton.h"
#include "SrvManager.h"
#include "../base/DirectXCommon.h"

void SkinCluster::Initialize(
	DirectXCommon* dxCommon,
	SrvManager* srvManager,
	const Skeleton& skeleton,
	const Model& model
) {
	assert(dxCommon);
	assert(srvManager);
	assert(skeleton.IsValid());

	jointCount_ = static_cast<uint32_t>(skeleton.joints.size());
	const uint32_t vertexCount = model.GetVertexCount();

	paletteResource_ = dxCommon->CreateBufferResource(
		sizeof(PaletteWell) * jointCount_
	);
	paletteResource_->Map(
		0,
		nullptr,
		reinterpret_cast<void**>(&mappedPalette_)
	);
	assert(srvManager->CanAllocate());
	paletteSrvIndex_ = srvManager->Allocate();
	srvManager->CreateSRVforStructuredBuffer(
		paletteSrvIndex_,
		paletteResource_.Get(),
		jointCount_,
		sizeof(PaletteWell)
	);

	influenceResource_ = dxCommon->CreateBufferResource(
		sizeof(VertexInfluence) * vertexCount
	);
	influenceResource_->Map(
		0,
		nullptr,
		reinterpret_cast<void**>(&mappedInfluences_)
	);
	std::memset(
		mappedInfluences_,
		0,
		sizeof(VertexInfluence) * vertexCount
	);
	influenceBufferView_.BufferLocation =
		influenceResource_->GetGPUVirtualAddress();
	influenceBufferView_.SizeInBytes = static_cast<UINT>(
		sizeof(VertexInfluence) * vertexCount
	);
	influenceBufferView_.StrideInBytes = sizeof(VertexInfluence);

	inverseBindPoseMatrices_.assign(
		jointCount_,
		MakeIdentity4x4()
	);

	for (const auto& [jointName, jointWeight] :
		model.GetSkinClusterData()) {
		const auto jointIt = skeleton.jointMap.find(jointName);
		if (jointIt == skeleton.jointMap.end()) {
			continue;
		}

		const uint32_t jointIndex =
			static_cast<uint32_t>(jointIt->second);

		inverseBindPoseMatrices_[jointIndex] =
			jointWeight.inverseBindPoseMatrix;

#if defined(_DEBUG) || defined(DEVELOPMENT)
		{
			const Matrix4x4 bindCheck = Multiply(
				inverseBindPoseMatrices_[jointIndex],
				skeleton.joints[jointIndex].skeletonSpaceMatrix
			);

			float maxError = 0.0f;
			for (uint32_t row = 0; row < 4; ++row) {
				for (uint32_t column = 0; column < 4; ++column) {
					const float expected =
						row == column ? 1.0f : 0.0f;
					maxError = (std::max)(
						maxError,
						std::abs(bindCheck.m[row][column] - expected)
					);
				}
			}

			if (maxError > 0.001f) {
				OutputDebugStringA(
					("[SkinCluster] Bind pose mismatch: " +
						jointName +
						" error=" +
						std::to_string(maxError) +
						"\n").c_str()
				);
			}
		}
#endif

		for (const Model::VertexWeightData& vertexWeight :
			jointWeight.vertexWeights) {
			if (vertexWeight.vertexIndex >= vertexCount) {
				continue;
			}
			AddInfluence(
				mappedInfluences_[vertexWeight.vertexIndex],
				vertexWeight.weight,
				jointIndex
			);
		}
	}

	for (uint32_t vertexIndex = 0;
		vertexIndex < vertexCount;
		++vertexIndex) {
		VertexInfluence& influence = mappedInfluences_[vertexIndex];
		const float weightSum = std::accumulate(
			influence.weights.begin(),
			influence.weights.end(),
			0.0f
		);
		if (weightSum > 0.0f) {
			for (float& weight : influence.weights) {
				weight /= weightSum;
			}
		}
	}

	Update(skeleton);
}

void SkinCluster::Update(const Skeleton& skeleton) {
	if (!IsValid()) {
		return;
	}

	const uint32_t updateCount = (std::min)(
		jointCount_,
		static_cast<uint32_t>(skeleton.joints.size())
	);
	for (uint32_t jointIndex = 0;
		jointIndex < updateCount;
		++jointIndex) {
		const Matrix4x4 skinningMatrix = Multiply(
			inverseBindPoseMatrices_[jointIndex],
			skeleton.joints[jointIndex].skeletonSpaceMatrix
		);
		mappedPalette_[jointIndex].skeletonSpaceMatrix =
			skinningMatrix;
		mappedPalette_[jointIndex]
			.skeletonSpaceInverseTransposeMatrix =
			Transpose(Inverse(skinningMatrix));
	}

}

void SkinCluster::AddInfluence(
	VertexInfluence& influence,
	float weight,
	uint32_t jointIndex
) {
	for (uint32_t influenceIndex = 0;
		influenceIndex < kMaxInfluence;
		++influenceIndex) {
		if (influence.weights[influenceIndex] == 0.0f) {
			influence.weights[influenceIndex] = weight;
			influence.jointIndices[influenceIndex] = jointIndex;
			return;
		}
	}

	const auto smallestIt = std::min_element(
		influence.weights.begin(),
		influence.weights.end()
	);
	if (smallestIt == influence.weights.end() || weight <= *smallestIt) {
		return;
	}

	const size_t influenceIndex = static_cast<size_t>(
		std::distance(influence.weights.begin(), smallestIt)
	);
	influence.weights[influenceIndex] = weight;
	influence.jointIndices[influenceIndex] = jointIndex;
}
