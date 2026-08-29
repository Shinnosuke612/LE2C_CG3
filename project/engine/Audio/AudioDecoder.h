// 役割: Media Foundation Source Readerで音声FileをPCM ClipへFull Decodeする。
#pragma once

#include "AudioClip.h"

#include <filesystem>
#include <memory>
#include <string>

struct AudioDecodeResult {
	std::shared_ptr<AudioClip> clip;
	std::string error;
};

struct AudioMetadataProbeResult {
	AudioFileMetadata metadata{};
	std::string error;
};

class AudioDecoder {
public:
	// 対応Audio FileをPCMへ全Decodeする。失敗時はClipを返さずErrorを設定する。
	static AudioDecodeResult DecodeFile(
		const std::filesystem::path& filePath,
		const std::string& resourcePath
	);

	// PCM出力Formatだけを確認する。Sample読込やPCM確保は行わない。
	static AudioMetadataProbeResult ProbeFile(
		const std::filesystem::path& filePath
	);
};
