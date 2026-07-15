// 役割: PMXバイナリを範囲検査しながら読み、基本メッシュと材質を構築する。
#include "PmxLoader.h"

#include "../utility/StringUtility.h"

#include <array>
#include <cstring>
#include <fstream>
#include <limits>
#include <type_traits>
#include <utility>

namespace {

constexpr int32_t kMaxVertexCount = 10'000'000;
constexpr int32_t kMaxIndexCount = 30'000'000;
constexpr int32_t kMaxResourceCount = 1'000'000;
constexpr int32_t kMaxBoneCount = 10'000;
// MMDの人物モデルをエンジンのメートル相当スケールへ合わせる。
constexpr float kPmxUnitScale = 0.1f;

constexpr uint16_t kBoneTailUsesIndex = 0x0001;
constexpr uint16_t kBoneIk = 0x0020;
constexpr uint16_t kBoneInheritRotation = 0x0100;
constexpr uint16_t kBoneInheritTranslation = 0x0200;
constexpr uint16_t kBoneFixedAxis = 0x0400;
constexpr uint16_t kBoneLocalAxis = 0x0800;
constexpr uint16_t kBoneExternalParent = 0x2000;

class BinaryReader {
public:
	explicit BinaryReader(const std::vector<uint8_t>& bytes)
		: bytes_(bytes) {
	}

	template<class T>
	bool Read(T& value) {
		static_assert(std::is_trivially_copyable_v<T>);
		return ReadBytes(&value, sizeof(T));
	}

	bool ReadBytes(void* destination, size_t byteCount) {
		if (byteCount > Remaining()) {
			return false;
		}
		if (byteCount > 0) {
			std::memcpy(destination, bytes_.data() + offset_, byteCount);
		}
		offset_ += byteCount;
		return true;
	}

	bool Skip(size_t byteCount) {
		if (byteCount > Remaining()) {
			return false;
		}
		offset_ += byteCount;
		return true;
	}

	size_t Remaining() const {
		return bytes_.size() - offset_;
	}

private:
	const std::vector<uint8_t>& bytes_;
	size_t offset_ = 0;
};

bool Fail(std::string& errorMessage, const std::string& message) {
	errorMessage = message;
	return false;
}

bool IsSupportedIndexSize(uint8_t indexSize) {
	return indexSize == 1 || indexSize == 2 || indexSize == 4;
}

bool SkipElements(BinaryReader& reader, size_t count, size_t elementSize) {
	if (count > std::numeric_limits<size_t>::max() / elementSize) {
		return false;
	}
	return reader.Skip(count * elementSize);
}

bool ReadText(
	BinaryReader& reader,
	uint8_t encoding,
	std::string& result
) {
	int32_t byteCount = 0;
	if (!reader.Read(byteCount) || byteCount < 0 ||
		static_cast<size_t>(byteCount) > reader.Remaining()) {
		return false;
	}

	if (encoding == 1) {
		result.resize(static_cast<size_t>(byteCount));
		return reader.ReadBytes(result.data(), result.size());
	}
	if (encoding != 0 || (byteCount % 2) != 0) {
		return false;
	}

	static_assert(sizeof(wchar_t) == 2);
	std::wstring wideText(static_cast<size_t>(byteCount) / 2, L'\0');
	if (!reader.ReadBytes(wideText.data(), static_cast<size_t>(byteCount))) {
		return false;
	}
	result = StringUtility::ConvertString(wideText);
	return true;
}

bool ReadSignedIndex(BinaryReader& reader, uint8_t indexSize, int32_t& result) {
	switch (indexSize) {
	case 1: {
		uint8_t value = 0;
		if (!reader.Read(value)) {
			return false;
		}
		result = value == std::numeric_limits<uint8_t>::max()
			? -1
			: static_cast<int32_t>(value);
		return true;
	}
	case 2: {
		uint16_t value = 0;
		if (!reader.Read(value)) {
			return false;
		}
		result = value == std::numeric_limits<uint16_t>::max()
			? -1
			: static_cast<int32_t>(value);
		return true;
	}
	case 4: {
		uint32_t value = 0;
		if (!reader.Read(value)) {
			return false;
		}
		if (value == std::numeric_limits<uint32_t>::max()) {
			result = -1;
			return true;
		}
		if (value > static_cast<uint32_t>(
			std::numeric_limits<int32_t>::max()
		)) {
			return false;
		}
		result = static_cast<int32_t>(value);
		return true;
	}
	default:
		return false;
	}
}

bool ReadVertexIndex(BinaryReader& reader, uint8_t indexSize, uint32_t& result) {
	switch (indexSize) {
	case 1: {
		uint8_t value = 0;
		if (!reader.Read(value)) {
			return false;
		}
		result = value;
		return true;
	}
	case 2: {
		uint16_t value = 0;
		if (!reader.Read(value)) {
			return false;
		}
		result = value;
		return true;
	}
	case 4:
		return reader.Read(result);
	default:
		return false;
	}
}

bool SkipVertexWeight(
	BinaryReader& reader,
	uint8_t deformType,
	uint8_t boneIndexSize
) {
	switch (deformType) {
	case 0: // BDEF1
		return SkipElements(reader, 1, boneIndexSize);
	case 1: // BDEF2
		return SkipElements(reader, 2, boneIndexSize) &&
			reader.Skip(sizeof(float));
	case 2: // BDEF4
	case 4: // QDEF
		return SkipElements(reader, 4, boneIndexSize) &&
			SkipElements(reader, 4, sizeof(float));
	case 3: // SDEF: weight + C/R0/R1
		return SkipElements(reader, 2, boneIndexSize) &&
			SkipElements(reader, 10, sizeof(float));
	default:
		return false;
	}
}

bool ReadFileBytes(
	const std::filesystem::path& filePath,
	std::vector<uint8_t>& bytes,
	std::string& errorMessage
) {
	std::ifstream stream(filePath, std::ios::binary | std::ios::ate);
	if (!stream) {
		return Fail(errorMessage, "PMX file could not be opened");
	}
	const std::streamoff fileSize = stream.tellg();
	if (fileSize <= 0 ||
		static_cast<uint64_t>(fileSize) >
		std::numeric_limits<size_t>::max()) {
		return Fail(errorMessage, "PMX file size is invalid");
	}

	bytes.resize(static_cast<size_t>(fileSize));
	stream.seekg(0, std::ios::beg);
	if (!stream.read(
		reinterpret_cast<char*>(bytes.data()),
		static_cast<std::streamsize>(bytes.size())
	)) {
		return Fail(errorMessage, "PMX file could not be read");
	}
	return true;
}

} // namespace

bool PmxLoader::LoadBasic(
	const std::filesystem::path& filePath,
	ModelData& result,
	std::string& errorMessage
) {
	std::vector<uint8_t> bytes;
	if (!ReadFileBytes(filePath, bytes, errorMessage)) {
		return false;
	}
	BinaryReader reader(bytes);

	std::array<char, 4> signature{};
	float version = 0.0f;
	uint8_t headerSize = 0;
	if (!reader.ReadBytes(signature.data(), signature.size()) ||
		std::memcmp(signature.data(), "PMX ", signature.size()) != 0 ||
		!reader.Read(version) || version < 1.99f || version > 2.11f ||
		!reader.Read(headerSize) || headerSize < 8) {
		return Fail(errorMessage, "PMX header is invalid or unsupported");
	}

	std::array<uint8_t, 8> settings{};
	if (!reader.ReadBytes(settings.data(), settings.size()) ||
		!reader.Skip(static_cast<size_t>(headerSize) - settings.size())) {
		return Fail(errorMessage, "PMX header settings are truncated");
	}
	const uint8_t encoding = settings[0];
	const uint8_t additionalUvCount = settings[1];
	const uint8_t vertexIndexSize = settings[2];
	const uint8_t textureIndexSize = settings[3];
	const uint8_t boneIndexSize = settings[5];
	if ((encoding != 0 && encoding != 1) || additionalUvCount > 4 ||
		!IsSupportedIndexSize(vertexIndexSize) ||
		!IsSupportedIndexSize(textureIndexSize) ||
		!IsSupportedIndexSize(boneIndexSize)) {
		return Fail(errorMessage, "PMX header settings are unsupported");
	}

	ModelData modelData;
	std::string englishName;
	std::string ignoredText;
	if (!ReadText(reader, encoding, modelData.name) ||
		!ReadText(reader, encoding, englishName) ||
		!ReadText(reader, encoding, ignoredText) ||
		!ReadText(reader, encoding, ignoredText)) {
		return Fail(errorMessage, "PMX model information is truncated");
	}
	if (modelData.name.empty()) {
		modelData.name = englishName;
	}

	int32_t vertexCount = 0;
	if (!reader.Read(vertexCount) || vertexCount < 0 ||
		vertexCount > kMaxVertexCount ||
		static_cast<size_t>(vertexCount) > reader.Remaining() / 37) {
		return Fail(errorMessage, "PMX vertex count is invalid");
	}
	modelData.vertices.reserve(static_cast<size_t>(vertexCount));
	for (int32_t vertexIndex = 0; vertexIndex < vertexCount; ++vertexIndex) {
		std::array<float, 3> position{};
		std::array<float, 3> normal{};
		std::array<float, 2> texcoord{};
		uint8_t deformType = 0;
		if (!reader.ReadBytes(position.data(), sizeof(position)) ||
			!reader.ReadBytes(normal.data(), sizeof(normal)) ||
			!reader.ReadBytes(texcoord.data(), sizeof(texcoord)) ||
			!SkipElements(
				reader,
				static_cast<size_t>(additionalUvCount) * 4,
				sizeof(float)
			) ||
			!reader.Read(deformType) ||
			!SkipVertexWeight(reader, deformType, boneIndexSize) ||
			!reader.Skip(sizeof(float))) {
			return Fail(errorMessage, "PMX vertex data is truncated");
		}

		modelData.vertices.push_back({
			{
				-position[0] * kPmxUnitScale,
				position[1] * kPmxUnitScale,
				position[2] * kPmxUnitScale
			},
			{ -normal[0], normal[1], normal[2] },
			{ texcoord[0], texcoord[1] }
		});
	}

	int32_t indexCount = 0;
	if (!reader.Read(indexCount) || indexCount < 0 ||
		indexCount > kMaxIndexCount || (indexCount % 3) != 0 ||
		static_cast<size_t>(indexCount) >
			reader.Remaining() / vertexIndexSize) {
		return Fail(errorMessage, "PMX index count is invalid");
	}
	modelData.indices.resize(static_cast<size_t>(indexCount));
	for (int32_t triangleStart = 0;
		triangleStart < indexCount;
		triangleStart += 3) {
		std::array<uint32_t, 3> triangle{};
		for (uint32_t element = 0; element < triangle.size(); ++element) {
			if (!ReadVertexIndex(reader, vertexIndexSize, triangle[element]) ||
				triangle[element] >= modelData.vertices.size()) {
				return Fail(errorMessage, "PMX contains an invalid vertex index");
			}
		}
		modelData.indices[triangleStart] = triangle[0];
		modelData.indices[triangleStart + 1] = triangle[2];
		modelData.indices[triangleStart + 2] = triangle[1];
	}

	int32_t textureCount = 0;
	if (!reader.Read(textureCount) || textureCount < 0 ||
		textureCount > kMaxResourceCount ||
		static_cast<size_t>(textureCount) > reader.Remaining() / sizeof(int32_t)) {
		return Fail(errorMessage, "PMX texture count is invalid");
	}
	std::vector<std::string> textures(static_cast<size_t>(textureCount));
	for (std::string& texture : textures) {
		if (!ReadText(reader, encoding, texture)) {
			return Fail(errorMessage, "PMX texture list is truncated");
		}
	}

	int32_t materialCount = 0;
	if (!reader.Read(materialCount) || materialCount < 0 ||
		materialCount > kMaxResourceCount) {
		return Fail(errorMessage, "PMX material count is invalid");
	}
	modelData.materials.reserve(static_cast<size_t>(materialCount) + 1);
	uint32_t assignedIndexCount = 0;
	for (int32_t materialIndex = 0;
		materialIndex < materialCount;
		++materialIndex) {
		Material material{};
		std::string englishMaterialName;
		std::array<float, 4> diffuse{};
		int32_t textureIndex = -1;
		int32_t ignoredIndex = -1;
		uint8_t ignoredByte = 0;
		uint8_t toonSharing = 0;
		int32_t surfaceCount = 0;

		if (!ReadText(reader, encoding, material.name) ||
			!ReadText(reader, encoding, englishMaterialName) ||
			!reader.ReadBytes(diffuse.data(), sizeof(diffuse)) ||
			!SkipElements(reader, 7, sizeof(float)) ||
			!reader.Read(ignoredByte) ||
			!SkipElements(reader, 5, sizeof(float)) ||
			!ReadSignedIndex(reader, textureIndexSize, textureIndex) ||
			!ReadSignedIndex(reader, textureIndexSize, ignoredIndex) ||
			!reader.Read(ignoredByte) ||
			!reader.Read(toonSharing)) {
			return Fail(errorMessage, "PMX material data is truncated");
		}
		if (toonSharing == 0) {
			if (!ReadSignedIndex(reader, textureIndexSize, ignoredIndex)) {
				return Fail(errorMessage, "PMX material toon data is truncated");
			}
		}
		else if (toonSharing == 1) {
			if (!reader.Read(ignoredByte)) {
				return Fail(errorMessage, "PMX shared toon data is truncated");
			}
		}
		else {
			return Fail(errorMessage, "PMX material toon setting is invalid");
		}
		if (!ReadText(reader, encoding, ignoredText) ||
			!reader.Read(surfaceCount) || surfaceCount < 0 ||
			(surfaceCount % 3) != 0 ||
			static_cast<uint64_t>(assignedIndexCount) +
				static_cast<uint32_t>(surfaceCount) >
				modelData.indices.size()) {
			return Fail(errorMessage, "PMX material index range is invalid");
		}

		if (material.name.empty()) {
			material.name = englishMaterialName.empty()
				? "Material " + std::to_string(materialIndex + 1)
				: englishMaterialName;
		}
		material.baseColor = {
			diffuse[0], diffuse[1], diffuse[2], diffuse[3]
		};
		material.indexCount = static_cast<uint32_t>(surfaceCount);
		if (textureIndex >= 0 &&
			static_cast<size_t>(textureIndex) < textures.size()) {
			material.texturePath = textures[textureIndex];
		}
		assignedIndexCount += material.indexCount;
		modelData.materials.push_back(std::move(material));
	}

	if (assignedIndexCount < modelData.indices.size()) {
		Material fallback{};
		fallback.name = "Unassigned";
		fallback.indexCount = static_cast<uint32_t>(
			modelData.indices.size() - assignedIndexCount
		);
		modelData.materials.push_back(std::move(fallback));
	}

	int32_t boneCount = 0;
	if (!reader.Read(boneCount) || boneCount < 0 ||
		boneCount > kMaxBoneCount ||
		static_cast<size_t>(boneCount) > reader.Remaining() / sizeof(int32_t)) {
		return Fail(errorMessage, "PMX bone count is invalid");
	}
	modelData.bones.reserve(static_cast<size_t>(boneCount));
	for (int32_t boneIndex = 0; boneIndex < boneCount; ++boneIndex) {
		Bone bone{};
		std::string englishBoneName;
		std::array<float, 3> position{};
		int32_t transformLevel = 0;
		uint16_t flags = 0;
		int32_t ignoredIndex = -1;

		if (!ReadText(reader, encoding, bone.name) ||
			!ReadText(reader, encoding, englishBoneName) ||
			!reader.ReadBytes(position.data(), sizeof(position)) ||
			!ReadSignedIndex(reader, boneIndexSize, bone.parentIndex) ||
			!reader.Read(transformLevel) ||
			!reader.Read(flags)) {
			return Fail(errorMessage, "PMX bone data is truncated");
		}
		if (bone.parentIndex >= boneCount || bone.parentIndex == boneIndex) {
			return Fail(errorMessage, "PMX bone parent index is invalid");
		}

		if ((flags & kBoneTailUsesIndex) != 0) {
			if (!ReadSignedIndex(reader, boneIndexSize, ignoredIndex)) {
				return Fail(errorMessage, "PMX bone tail is truncated");
			}
		}
		else if (!SkipElements(reader, 3, sizeof(float))) {
			return Fail(errorMessage, "PMX bone tail offset is truncated");
		}

		if ((flags & (kBoneInheritRotation | kBoneInheritTranslation)) != 0) {
			if (!ReadSignedIndex(reader, boneIndexSize, ignoredIndex) ||
				!reader.Skip(sizeof(float))) {
				return Fail(errorMessage, "PMX inherited bone data is truncated");
			}
		}
		if ((flags & kBoneFixedAxis) != 0 &&
			!SkipElements(reader, 3, sizeof(float))) {
			return Fail(errorMessage, "PMX fixed axis data is truncated");
		}
		if ((flags & kBoneLocalAxis) != 0 &&
			!SkipElements(reader, 6, sizeof(float))) {
			return Fail(errorMessage, "PMX local axis data is truncated");
		}
		if ((flags & kBoneExternalParent) != 0 &&
			!reader.Skip(sizeof(int32_t))) {
			return Fail(errorMessage, "PMX external parent data is truncated");
		}

		if ((flags & kBoneIk) != 0) {
			int32_t loopCount = 0;
			int32_t linkCount = 0;
			if (!ReadSignedIndex(reader, boneIndexSize, ignoredIndex) ||
				!reader.Read(loopCount) || loopCount < 0 ||
				!reader.Skip(sizeof(float)) ||
				!reader.Read(linkCount) || linkCount < 0 ||
				linkCount > kMaxBoneCount) {
				return Fail(errorMessage, "PMX IK data is invalid");
			}
			for (int32_t linkIndex = 0; linkIndex < linkCount; ++linkIndex) {
				uint8_t hasAngleLimit = 0;
				if (!ReadSignedIndex(reader, boneIndexSize, ignoredIndex) ||
					!reader.Read(hasAngleLimit) || hasAngleLimit > 1) {
					return Fail(errorMessage, "PMX IK link is invalid");
				}
				if (hasAngleLimit == 1 &&
					!SkipElements(reader, 6, sizeof(float))) {
					return Fail(errorMessage, "PMX IK angle limit is truncated");
				}
			}
		}

		if (bone.name.empty()) {
			bone.name = englishBoneName.empty()
				? "Bone " + std::to_string(boneIndex + 1)
				: englishBoneName;
		}
		bone.position = {
			-position[0] * kPmxUnitScale,
			position[1] * kPmxUnitScale,
			position[2] * kPmxUnitScale
		};
		modelData.bones.push_back(std::move(bone));
	}

	// 親チェーンを追跡し、破損データの循環階層を拒否する。
	for (size_t boneIndex = 0;
		boneIndex < modelData.bones.size();
		++boneIndex) {
		int32_t current = static_cast<int32_t>(boneIndex);
		size_t traversed = 0;
		while (current >= 0) {
			if (static_cast<size_t>(current) >= modelData.bones.size() ||
				++traversed > modelData.bones.size()) {
				return Fail(errorMessage, "PMX bone hierarchy contains a cycle");
			}
			current = modelData.bones[current].parentIndex;
		}
	}

	if (modelData.vertices.empty() || modelData.indices.empty()) {
		return Fail(errorMessage, "PMX contains no drawable geometry");
	}

	result = std::move(modelData);
	errorMessage.clear();
	return true;
}
