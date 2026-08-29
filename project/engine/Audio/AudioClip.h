// 役割: Full Decode済み音声のFormat、PCM、metadataを共有immutable Clipとして保持する。
#pragma once

#include <Windows.h>
#include <mmreg.h>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

struct AudioClip {
	std::string resourcePath;
	std::string container;
	WAVEFORMATEXTENSIBLE waveFormat{};
	uint32_t waveFormatSize = 0;
	std::vector<BYTE> pcmData;
	double durationSeconds = 0.0;
	uint32_t channelCount = 0;
	uint32_t sampleRate = 0;
	uint16_t bitsPerSample = 0;

	const WAVEFORMATEX* GetWaveFormat() const {
		return waveFormatSize >= sizeof(WAVEFORMATEX)
			? &waveFormat.Format
			: nullptr;
	}
};

using AudioClipPtr = std::shared_ptr<const AudioClip>;

// 役割: PCM出力Formatだけから得るAudio Assetの軽量metadataを保持する。
// Probe cacheはPCM、Source Reader、Voiceを所有しない。
struct AudioFileMetadata {
	std::string container;
	double durationSeconds = 0.0;
	uint32_t channelCount = 0;
	uint32_t sampleRate = 0;
	uint16_t bitsPerSample = 0;
};
