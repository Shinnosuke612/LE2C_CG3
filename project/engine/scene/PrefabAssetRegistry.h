// 役割: Prefab Asset IDと現在のProject Pathを対応付ける。
#pragma once

#include <cstddef>
#include <string>

struct PrefabAssetReference {
	std::string assetId;
	std::string fallbackPath;
};

class PrefabAssetRegistry final {
public:
	static std::string CreateAssetId();
	static std::string ReadAssetId(const std::string& filePath);
	static PrefabAssetReference ReadVariantBase(const std::string& filePath);
	static PrefabAssetReference CreateReference(const std::string& filePath);
	static std::string ResolvePath(
		const std::string& assetId,
		const std::string& fallbackPath
	);
	static std::string ResolvePath(const PrefabAssetReference& reference);
	static bool IsSameAsset(
		const PrefabAssetReference& left,
		const PrefabAssetReference& right
	);
	static std::size_t MigrateMissingAssetIds();
	static void Invalidate();
};
