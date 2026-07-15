// 役割: エディタとモデルローダーで共有する基本対応モデル形式を定義する。
#pragma once

#include <algorithm>
#include <array>
#include <cctype>
#include <filesystem>
#include <string>
#include <string_view>

namespace ModelFormat {

struct Descriptor {
	std::string_view extension;
	std::string_view displayName;
};

// Assimpまたは内蔵ローダーで基本形状・基本材質を読み込む対象。
inline constexpr std::array<Descriptor, 9> kBasicFormats = {{
	{ ".obj", "Wavefront OBJ" },
	{ ".gltf", "glTF" },
	{ ".glb", "glTF Binary" },
	{ ".fbx", "FBX" },
	{ ".dae", "Collada" },
	{ ".3ds", "3D Studio" },
	{ ".ply", "Stanford PLY" },
	{ ".stl", "STL" },
	{ ".pmx", "MikuMikuDance PMX" }
}};

inline std::string NormalizeExtension(std::string extension) {
	std::transform(
		extension.begin(),
		extension.end(),
		extension.begin(),
		[](unsigned char character) {
			return static_cast<char>(std::tolower(character));
		}
	);
	return extension;
}

inline const Descriptor* FindByExtension(const std::string& extension) {
	const std::string normalized = NormalizeExtension(extension);
	const auto found = std::find_if(
		kBasicFormats.begin(),
		kBasicFormats.end(),
		[&normalized](const Descriptor& descriptor) {
			return descriptor.extension == normalized;
		}
	);
	return found == kBasicFormats.end() ? nullptr : &(*found);
}

inline const Descriptor* FindByPath(const std::filesystem::path& path) {
	return FindByExtension(path.extension().string());
}

inline bool IsBasicModelPath(const std::filesystem::path& path) {
	return FindByPath(path) != nullptr;
}

} // namespace ModelFormat
