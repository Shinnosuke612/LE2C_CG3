#pragma once

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <string>
#include <vector>

#include "EditableResourcePath.h"

namespace ResourceTextureCatalog {

inline bool IsSupportedExtension(std::string extension) {
	std::transform(extension.begin(), extension.end(), extension.begin(),
		[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
	return extension == ".png" || extension == ".jpg" || extension == ".jpeg" ||
		extension == ".bmp" || extension == ".tif" || extension == ".tiff" ||
		extension == ".dds";
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
			EditableResourcePath::ToProjectRelative(iterator->path()).generic_string()
		);
	}
	std::sort(paths.begin(), paths.end());
	paths.erase(std::unique(paths.begin(), paths.end()), paths.end());
	return paths;
}

} // namespace ResourceTextureCatalog
