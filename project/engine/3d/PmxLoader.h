// 役割: PMXの基本形状と材質を描画用CPUデータへ変換する。
#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "../math/Vector2.h"
#include "../math/Vector3.h"
#include "../math/Vector4.h"

namespace PmxLoader {

struct Vertex {
	Vector3 position{};
	Vector3 normal{};
	Vector2 texcoord{};
};

struct Material {
	std::string name;
	Vector4 baseColor = { 1.0f, 1.0f, 1.0f, 1.0f };
	std::string texturePath;
	uint32_t indexCount = 0;
};

struct Bone {
	std::string name;
	Vector3 position{};
	int32_t parentIndex = -1;
};

struct ModelData {
	std::string name;
	std::vector<Vertex> vertices;
	std::vector<uint32_t> indices;
	std::vector<Material> materials;
	std::vector<Bone> bones;
};

// PMX 2.0/2.1の表示に必要な基本メッシュ、材質、ボーン階層を読み取る。
bool LoadBasic(
	const std::filesystem::path& filePath,
	ModelData& result,
	std::string& errorMessage
);

} // namespace PmxLoader
