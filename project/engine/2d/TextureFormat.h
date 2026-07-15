// 役割: エディタとTextureManagerで共有する画像形式とデコーダーを定義する。
#pragma once

#include <algorithm>
#include <array>
#include <cctype>
#include <filesystem>
#include <string>
#include <string_view>

namespace TextureFormat {

enum class Decoder {
	Dds,
	Tga,
	Hdr,
	Wic
};

enum class DefaultColorSpace {
	Preserve,
	Srgb,
	Linear
};

struct Descriptor {
	std::string_view extension;
	std::string_view displayName;
	Decoder decoder;
	DefaultColorSpace colorSpace;
};

inline constexpr std::array<Descriptor, 10> kFormats = {{
	{ ".dds", "DirectDraw Surface", Decoder::Dds, DefaultColorSpace::Preserve },
	{ ".tga", "Truevision TGA", Decoder::Tga, DefaultColorSpace::Srgb },
	{ ".hdr", "Radiance HDR", Decoder::Hdr, DefaultColorSpace::Linear },
	{ ".png", "PNG", Decoder::Wic, DefaultColorSpace::Srgb },
	{ ".jpg", "JPEG", Decoder::Wic, DefaultColorSpace::Srgb },
	{ ".jpeg", "JPEG", Decoder::Wic, DefaultColorSpace::Srgb },
	{ ".bmp", "Bitmap", Decoder::Wic, DefaultColorSpace::Srgb },
	{ ".tif", "TIFF", Decoder::Wic, DefaultColorSpace::Srgb },
	{ ".tiff", "TIFF", Decoder::Wic, DefaultColorSpace::Srgb },
	{ ".gif", "GIF", Decoder::Wic, DefaultColorSpace::Srgb }
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
	if (!extension.empty() && extension.front() != '.') {
		extension.insert(extension.begin(), '.');
	}
	return extension;
}

inline const Descriptor* FindByExtension(const std::string& extension) {
	const std::string normalized = NormalizeExtension(extension);
	const auto found = std::find_if(
		kFormats.begin(),
		kFormats.end(),
		[&normalized](const Descriptor& descriptor) {
			return descriptor.extension == normalized;
		}
	);
	return found == kFormats.end() ? nullptr : &(*found);
}

inline const Descriptor* FindByPath(const std::filesystem::path& path) {
	return FindByExtension(path.extension().string());
}

inline bool IsSupportedTexturePath(const std::filesystem::path& path) {
	return FindByPath(path) != nullptr;
}

} // namespace TextureFormat
