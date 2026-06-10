#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include <d3d12.h>
#include <wrl.h>

#include "../math/Matrix4x4.h"

class DirectXCommon;
class Model;
class SrvManager;
struct Skeleton;

class SkinCluster {
public:
	static constexpr uint32_t kMaxInfluence = 4;

	struct VertexInfluence {
		std::array<float, kMaxInfluence> weights{};
		std::array<uint32_t, kMaxInfluence> jointIndices{};
	};

	struct PaletteWell {
		Matrix4x4 skeletonSpaceMatrix;
		Matrix4x4 skeletonSpaceInverseTransposeMatrix;
	};

	void Initialize(
		DirectXCommon* dxCommon,
		SrvManager* srvManager,
		const Skeleton& skeleton,
		const Model& model
	);
	void Update(const Skeleton& skeleton);

	const D3D12_VERTEX_BUFFER_VIEW& GetInfluenceBufferView() const {
		return influenceBufferView_;
	}
	uint32_t GetPaletteSrvIndex() const { return paletteSrvIndex_; }
	bool IsValid() const {
		return influenceResource_ != nullptr &&
			paletteResource_ != nullptr;
	}

private:
	void AddInfluence(
		VertexInfluence& influence,
		float weight,
		uint32_t jointIndex
	);

	Microsoft::WRL::ComPtr<ID3D12Resource> influenceResource_;
	D3D12_VERTEX_BUFFER_VIEW influenceBufferView_{};
	VertexInfluence* mappedInfluences_ = nullptr;

	Microsoft::WRL::ComPtr<ID3D12Resource> paletteResource_;
	PaletteWell* mappedPalette_ = nullptr;
	uint32_t paletteSrvIndex_ = 0;
	uint32_t jointCount_ = 0;
	std::vector<Matrix4x4> inverseBindPoseMatrices_;
};
