// 役割: resources配下のPrefab GUIDを走査し、移動後のPathを解決する。
#include "PrefabAssetRegistry.h"

#include "../utility/EditableResourcePath.h"
#include "../utility/StringUtility.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <random>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "../../externals/nlohmann/json.hpp"

namespace {
	using json = nlohmann::json;

	bool cacheDirty = true;
	std::unordered_map<std::string, std::string> pathByAssetId;
	std::unordered_set<std::string> duplicateAssetIds;

	bool IsPrefabAssetPath(const std::filesystem::path& path) {
		std::string fileName = StringUtility::ToUtf8(path.filename());
		std::transform(
			fileName.begin(),
			fileName.end(),
			fileName.begin(),
			[](unsigned char character) {
				return static_cast<char>(std::tolower(character));
			}
		);
		return fileName.ends_with(".prefab.json");
	}

	std::string NormalizeProjectPath(const std::filesystem::path& path) {
		return StringUtility::ToUtf8(
			EditableResourcePath::ToProjectRelative(path.lexically_normal())
		);
	}

	std::string ReadAssetIdFromResolvedPath(
		const std::filesystem::path& resolvedPath
	) {
		std::ifstream input(resolvedPath, std::ios::binary);
		if (!input.is_open()) {
			return {};
		}
		try {
			const json root = json::parse(input);
			if (!root.is_object()) {
				return {};
			}
			return root.value("assetId", std::string{});
		}
		catch (const json::exception&) {
			return {};
		}
	}

	void RefreshCache() {
		pathByAssetId.clear();
		duplicateAssetIds.clear();
		const std::filesystem::path resourceRoot =
			EditableResourcePath::Resolve("resources");
		std::error_code error;
		std::filesystem::recursive_directory_iterator iterator(
			resourceRoot,
			std::filesystem::directory_options::skip_permission_denied,
			error
		);
		const std::filesystem::recursive_directory_iterator end;
		while (!error && iterator != end) {
			if (
				iterator->is_regular_file(error) &&
				IsPrefabAssetPath(iterator->path())
			) {
				const std::string assetId =
					ReadAssetIdFromResolvedPath(iterator->path());
				if (!assetId.empty() && !duplicateAssetIds.contains(assetId)) {
					const std::string path =
						NormalizeProjectPath(iterator->path());
					if (!pathByAssetId.emplace(assetId, path).second) {
						pathByAssetId.erase(assetId);
						duplicateAssetIds.insert(assetId);
					}
				}
			}
			iterator.increment(error);
		}
		cacheDirty = false;
	}
}

std::string PrefabAssetRegistry::CreateAssetId() {
	std::random_device random;
	std::array<unsigned char, 16> bytes{};
	for (unsigned char& value : bytes) {
		value = static_cast<unsigned char>(random());
	}
	bytes[6] = static_cast<unsigned char>((bytes[6] & 0x0f) | 0x40);
	bytes[8] = static_cast<unsigned char>((bytes[8] & 0x3f) | 0x80);

	constexpr char hex[] = "0123456789abcdef";
	std::string result;
	result.reserve(36);
	for (size_t index = 0; index < bytes.size(); ++index) {
		if (index == 4 || index == 6 || index == 8 || index == 10) {
			result.push_back('-');
		}
		result.push_back(hex[(bytes[index] >> 4) & 0x0f]);
		result.push_back(hex[bytes[index] & 0x0f]);
	}
	return result;
}

std::string PrefabAssetRegistry::ReadAssetId(const std::string& filePath) {
	if (filePath.empty()) {
		return {};
	}
	const std::filesystem::path resolvedPath =
		EditableResourcePath::ResolveResource(
			StringUtility::ToPath(filePath)
		).lexically_normal();
	return ReadAssetIdFromResolvedPath(resolvedPath);
}

PrefabAssetReference PrefabAssetRegistry::ReadVariantBase(
	const std::string& filePath
) {
	PrefabAssetReference reference{};
	if (filePath.empty()) {
		return reference;
	}
	const std::filesystem::path resolvedPath =
		EditableResourcePath::ResolveResource(
			StringUtility::ToPath(filePath)
		).lexically_normal();
	std::ifstream input(resolvedPath, std::ios::binary);
	if (!input.is_open()) {
		return reference;
	}
	try {
		const json root = json::parse(input);
		if (
			!root.is_object() ||
			!root.contains("variant") ||
			!root.at("variant").is_object()
		) {
			return reference;
		}
		const json& variant = root.at("variant");
		if (!variant.contains("base") || !variant.at("base").is_object()) {
			return reference;
		}
		const json& base = variant.at("base");
		reference.assetId = base.value("assetId", std::string{});
		reference.fallbackPath = base.value("fallbackPath", std::string{});
		return reference;
	}
	catch (const json::exception&) {
		return {};
	}
}

PrefabAssetReference PrefabAssetRegistry::CreateReference(
	const std::string& filePath
) {
	PrefabAssetReference reference{};
	reference.fallbackPath = ResolvePath({}, filePath);
	if (reference.fallbackPath.empty() && !filePath.empty()) {
		// 欠損時も元の場所を診断・再解決へ使えるよう参照データには残す。
		reference.fallbackPath = NormalizeProjectPath(
			EditableResourcePath::ResolveResource(
				StringUtility::ToPath(filePath)
			).lexically_normal()
		);
	}
	reference.assetId = ReadAssetId(reference.fallbackPath);
	return reference;
}

std::string PrefabAssetRegistry::ResolvePath(
	const std::string& assetId,
	const std::string& fallbackPath
) {
	if (!assetId.empty()) {
		if (cacheDirty) {
			RefreshCache();
		}
		const auto found = pathByAssetId.find(assetId);
		if (found != pathByAssetId.end()) {
			return found->second;
		}
	}

	if (fallbackPath.empty()) {
		return {};
	}
	const std::filesystem::path resolvedFallback =
		EditableResourcePath::ResolveResource(
			StringUtility::ToPath(fallbackPath)
		).lexically_normal();
	const std::string normalizedFallback =
		NormalizeProjectPath(resolvedFallback);
	std::error_code error;
	if (!std::filesystem::is_regular_file(resolvedFallback, error) || error) {
		// Fallback Pathは参照側に保持し、解決結果には実在するAssetだけを返す。
		return {};
	}
	if (assetId.empty()) {
		return normalizedFallback;
	}
	if (ReadAssetIdFromResolvedPath(resolvedFallback) == assetId) {
		return normalizedFallback;
	}
	// 同じPathが別Assetへ置き換わっている場合も解決成功にしない。
	return {};
}

std::string PrefabAssetRegistry::ResolvePath(
	const PrefabAssetReference& reference
) {
	return ResolvePath(reference.assetId, reference.fallbackPath);
}

bool PrefabAssetRegistry::IsSameAsset(
	const PrefabAssetReference& left,
	const PrefabAssetReference& right
) {
	if (!left.assetId.empty() && !right.assetId.empty()) {
		if (left.assetId != right.assetId) {
			return false;
		}
		if (cacheDirty) {
			RefreshCache();
		}
		if (!duplicateAssetIds.contains(left.assetId)) {
			return true;
		}
		// 重複IDは同一性の根拠にせず、各参照のFallback Pathで区別する。
	}
	const std::string leftPath = ResolvePath(left);
	const std::string rightPath = ResolvePath(right);
	return
		!leftPath.empty() &&
		!rightPath.empty() &&
		StringUtility::ToPath(leftPath).lexically_normal() ==
			StringUtility::ToPath(rightPath).lexically_normal();
}

std::size_t PrefabAssetRegistry::MigrateMissingAssetIds() {
	struct PendingMigration {
		std::filesystem::path path;
		json root;
	};

	std::unordered_set<std::string> usedAssetIds;
	std::vector<PendingMigration> pending;
	const std::filesystem::path resourceRoot =
		EditableResourcePath::Resolve("resources");
	std::error_code error;
	std::filesystem::recursive_directory_iterator iterator(
		resourceRoot,
		std::filesystem::directory_options::skip_permission_denied,
		error
	);
	const std::filesystem::recursive_directory_iterator end;
	while (!error && iterator != end) {
		if (
			iterator->is_regular_file(error) &&
			IsPrefabAssetPath(iterator->path())
		) {
			std::ifstream input(iterator->path(), std::ios::binary);
			try {
				json root = json::parse(input);
				if (root.is_object()) {
					const std::string assetId = root.value(
						"assetId",
						std::string{}
					);
					if (assetId.empty()) {
						pending.push_back({ iterator->path(), std::move(root) });
					} else {
						usedAssetIds.insert(assetId);
					}
				}
			}
			catch (const json::exception&) {
				// 不正PrefabはSceneDocument側のLoad Errorへ委ね、移行では変更しない。
			}
		}
		iterator.increment(error);
	}

	std::size_t migratedCount = 0;
	for (PendingMigration& migration : pending) {
		std::string assetId;
		do {
			assetId = CreateAssetId();
		} while (!usedAssetIds.insert(assetId).second);
		migration.root["assetId"] = assetId;
		if (EditableResourcePath::WriteTextAtomically(
			migration.path,
			migration.root.dump(2)
		)) {
			++migratedCount;
		}
	}
	if (migratedCount != 0) {
		Invalidate();
	}
	return migratedCount;
}

void PrefabAssetRegistry::Invalidate() {
	cacheDirty = true;
}
