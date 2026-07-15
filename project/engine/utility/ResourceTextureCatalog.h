// 役割: エディタで選択できるテクスチャリソースの一覧を定義する。
#pragma once

#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>

#include "EditableResourcePath.h"
#include "StringUtility.h"
#include "../2d/TextureFormat.h"

namespace ResourceTextureCatalog {

inline bool IsSupportedExtension(std::string extension) {
	return TextureFormat::FindByExtension(extension) != nullptr;
}

inline std::vector<std::string> Collect() {
	std::vector<std::string> paths;
	const std::filesystem::path root = EditableResourcePath::Resolve("resources");
	std::error_code error;
	const auto options = std::filesystem::directory_options::skip_permission_denied;
	for (std::filesystem::recursive_directory_iterator iterator(root, options, error), end;
		iterator != end; iterator.increment(error)) {
		if (error) {
			error.clear();
			continue;
		}
		if (!iterator->is_regular_file(error) ||
			!IsSupportedExtension(iterator->path().extension().string())) {
			continue;
		}
		paths.push_back(
			StringUtility::ToUtf8(
				EditableResourcePath::ToProjectRelative(iterator->path())
			)
		);
	}
	std::sort(paths.begin(), paths.end());
	paths.erase(std::unique(paths.begin(), paths.end()), paths.end());
	return paths;
}

} // namespace ResourceTextureCatalog
