// 役割: Source Readerの出力Format確定、PCM結合、metadata取得を実装する。
#include "AudioDecoder.h"

#include <mfapi.h>
#include <mferror.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <propvarutil.h>
#include <wrl.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <limits>
#include <memory>
#include <new>

#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfreadwrite.lib")
#pragma comment(lib, "mfuuid.lib")

namespace {
	using Microsoft::WRL::ComPtr;

	struct CoTaskMemDeleter {
		void operator()(void* memory) const {
			CoTaskMemFree(memory);
		}
	};

	struct PreparedPcmReader {
		ComPtr<IMFSourceReader> sourceReader;
		WAVEFORMATEXTENSIBLE waveFormat{};
		uint32_t waveFormatSize = 0;
		AudioFileMetadata metadata{};
	};

	std::string BuildDecodeError(const char* operation, HRESULT result) {
		char message[192]{};
		sprintf_s(
			message,
			"%s failed (HRESULT 0x%08X).",
			operation,
			static_cast<unsigned int>(result)
		);
		return message;
	}

	std::string GetContainerName(const std::filesystem::path& filePath) {
		std::string extension = filePath.extension().string();
		std::transform(
			extension.begin(),
			extension.end(),
			extension.begin(),
			[](unsigned char character) {
				return static_cast<char>(std::toupper(character));
			}
		);
		if (!extension.empty() && extension.front() == '.') {
			extension.erase(extension.begin());
		}
		return extension;
	}

	bool AppendSampleBytes(
		IMFSample* sample,
		std::vector<BYTE>& destination,
		std::string& error
	) {
		ComPtr<IMFMediaBuffer> mediaBuffer;
		HRESULT result = sample->ConvertToContiguousBuffer(&mediaBuffer);
		if (FAILED(result)) {
			error = BuildDecodeError("ConvertToContiguousBuffer", result);
			return false;
		}

		BYTE* source = nullptr;
		DWORD maximumLength = 0;
		DWORD currentLength = 0;
		result = mediaBuffer->Lock(&source, &maximumLength, &currentLength);
		if (FAILED(result)) {
			error = BuildDecodeError("IMFMediaBuffer::Lock", result);
			return false;
		}
		(void)maximumLength;

		const size_t oldSize = destination.size();
		const size_t addedSize = static_cast<size_t>(currentLength);
		if (addedSize > static_cast<size_t>((std::numeric_limits<UINT32>::max)()) - oldSize) {
			mediaBuffer->Unlock();
			error = "Decoded audio exceeds the Full Decode size limit; Streaming is required.";
			return false;
		}

		try {
			destination.resize(oldSize + addedSize);
		} catch (const std::bad_alloc&) {
			mediaBuffer->Unlock();
			error = "Insufficient memory while expanding decoded audio data.";
			return false;
		}
		if (addedSize > 0) {
			std::memcpy(destination.data() + oldSize, source, addedSize);
		}
		result = mediaBuffer->Unlock();
		if (FAILED(result)) {
			error = BuildDecodeError("IMFMediaBuffer::Unlock", result);
			return false;
		}
		return true;
	}

	bool PreparePcmReader(
		const std::filesystem::path& filePath,
		PreparedPcmReader& prepared,
		std::string& error
	) {
		ComPtr<IMFSourceReader> sourceReader;
		HRESULT result = MFCreateSourceReaderFromURL(
			filePath.c_str(),
			nullptr,
			&sourceReader
		);
		if (FAILED(result)) {
			error = BuildDecodeError("MFCreateSourceReaderFromURL", result);
			return false;
		}

		result = sourceReader->SetStreamSelection(MF_SOURCE_READER_ALL_STREAMS, FALSE);
		if (FAILED(result)) {
			error = BuildDecodeError("SetStreamSelection(all)", result);
			return false;
		}
		result = sourceReader->SetStreamSelection(
			MF_SOURCE_READER_FIRST_AUDIO_STREAM,
			TRUE
		);
		if (FAILED(result)) {
			error = BuildDecodeError("SetStreamSelection(audio)", result);
			return false;
		}

		ComPtr<IMFMediaType> pcmType;
		result = MFCreateMediaType(&pcmType);
		if (SUCCEEDED(result)) {
			result = pcmType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
		}
		if (SUCCEEDED(result)) {
			result = pcmType->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM);
		}
		if (SUCCEEDED(result)) {
			result = sourceReader->SetCurrentMediaType(
				MF_SOURCE_READER_FIRST_AUDIO_STREAM,
				nullptr,
				pcmType.Get()
			);
		}
		if (FAILED(result)) {
			error = BuildDecodeError("SetCurrentMediaType(PCM)", result);
			return false;
		}

		ComPtr<IMFMediaType> outputType;
		result = sourceReader->GetCurrentMediaType(
			MF_SOURCE_READER_FIRST_AUDIO_STREAM,
			&outputType
		);
		if (FAILED(result)) {
			error = BuildDecodeError("GetCurrentMediaType", result);
			return false;
		}

		WAVEFORMATEX* allocatedWaveFormat = nullptr;
		UINT32 waveFormatSize = 0;
		result = MFCreateWaveFormatExFromMFMediaType(
			outputType.Get(),
			&allocatedWaveFormat,
			&waveFormatSize
		);
		std::unique_ptr<WAVEFORMATEX, CoTaskMemDeleter> waveFormat(
			allocatedWaveFormat
		);
		if (FAILED(result) || !waveFormat || waveFormatSize < sizeof(WAVEFORMATEX)) {
			error = FAILED(result)
				? BuildDecodeError("MFCreateWaveFormatExFromMFMediaType", result)
				: "Media Foundation returned an invalid PCM Wave format.";
			return false;
		}
		if (waveFormatSize > sizeof(WAVEFORMATEXTENSIBLE)) {
			error = "Decoded PCM Wave format contains unsupported extension data.";
			return false;
		}
		if (waveFormat->nChannels == 0 || waveFormat->nSamplesPerSec == 0 ||
			waveFormat->nBlockAlign == 0 || waveFormat->nAvgBytesPerSec == 0) {
			error = "Decoded PCM Wave format is missing required channel or sample data.";
			return false;
		}

		prepared.sourceReader = std::move(sourceReader);
		prepared.waveFormatSize = waveFormatSize;
		std::memcpy(&prepared.waveFormat, waveFormat.get(), waveFormatSize);
		prepared.metadata.container = GetContainerName(filePath);
		prepared.metadata.channelCount = waveFormat->nChannels;
		prepared.metadata.sampleRate = waveFormat->nSamplesPerSec;
		prepared.metadata.bitsPerSample = waveFormat->wBitsPerSample;

		PROPVARIANT durationValue;
		PropVariantInit(&durationValue);
		result = prepared.sourceReader->GetPresentationAttribute(
			MF_SOURCE_READER_MEDIASOURCE,
			MF_PD_DURATION,
			&durationValue
		);
		if (SUCCEEDED(result) && durationValue.vt == VT_UI8) {
			prepared.metadata.durationSeconds =
				static_cast<double>(durationValue.uhVal.QuadPart) / 10000000.0;
		}
		PropVariantClear(&durationValue);
		return true;
	}
}

AudioDecodeResult AudioDecoder::DecodeFile(
	const std::filesystem::path& filePath,
	const std::string& resourcePath
) {
	AudioDecodeResult decoded{};
	PreparedPcmReader prepared{};
	if (!PreparePcmReader(filePath, prepared, decoded.error)) {
		return decoded;
	}
	const WAVEFORMATEX* waveFormat = &prepared.waveFormat.Format;

	auto clip = std::make_shared<AudioClip>();
	clip->resourcePath = resourcePath;
	clip->container = prepared.metadata.container;
	clip->waveFormatSize = prepared.waveFormatSize;
	std::memcpy(
		&clip->waveFormat,
		&prepared.waveFormat,
		prepared.waveFormatSize
	);
	clip->channelCount = prepared.metadata.channelCount;
	clip->sampleRate = prepared.metadata.sampleRate;
	clip->bitsPerSample = prepared.metadata.bitsPerSample;
	clip->durationSeconds = prepared.metadata.durationSeconds;
	if (clip->durationSeconds > 0.0) {
		const long double estimatedBytes =
			static_cast<long double>(clip->durationSeconds) *
			static_cast<long double>(waveFormat->nAvgBytesPerSec);
		if (estimatedBytes > static_cast<long double>((std::numeric_limits<UINT32>::max)())) {
			decoded.error = "Audio is too large for Full Decode; Streaming is required.";
			return decoded;
		}
		try {
			clip->pcmData.reserve(static_cast<size_t>(estimatedBytes));
		} catch (const std::bad_alloc&) {
			decoded.error = "Insufficient memory while reserving decoded audio data.";
			return decoded;
		}
	}

	while (true) {
		DWORD streamIndex = 0;
		DWORD flags = 0;
		LONGLONG timestamp = 0;
		ComPtr<IMFSample> sample;
		const HRESULT result = prepared.sourceReader->ReadSample(
			MF_SOURCE_READER_FIRST_AUDIO_STREAM,
			0,
			&streamIndex,
			&flags,
			&timestamp,
			&sample
		);
		(void)streamIndex;
		(void)timestamp;
		if (FAILED(result)) {
			decoded.error = BuildDecodeError("IMFSourceReader::ReadSample", result);
			return decoded;
		}
		if ((flags & MF_SOURCE_READERF_ERROR) != 0) {
			decoded.error = "Media Foundation reported an error while decoding audio samples.";
			return decoded;
		}
		if ((flags & MF_SOURCE_READERF_CURRENTMEDIATYPECHANGED) != 0) {
			decoded.error = "Audio PCM format changed during Full Decode.";
			return decoded;
		}
		if (sample && !AppendSampleBytes(sample.Get(), clip->pcmData, decoded.error)) {
			return decoded;
		}
		if ((flags & MF_SOURCE_READERF_ENDOFSTREAM) != 0) {
			break;
		}
	}

	if (clip->pcmData.empty()) {
		decoded.error = "Audio file did not contain decoded PCM samples.";
		return decoded;
	}
	if (clip->durationSeconds <= 0.0) {
		clip->durationSeconds = static_cast<double>(clip->pcmData.size()) /
			static_cast<double>(waveFormat->nAvgBytesPerSec);
	}

	decoded.clip = std::move(clip);
	return decoded;
}

AudioMetadataProbeResult AudioDecoder::ProbeFile(
	const std::filesystem::path& filePath
) {
	AudioMetadataProbeResult probe{};
	PreparedPcmReader prepared{};
	if (!PreparePcmReader(filePath, prepared, probe.error)) {
		return probe;
	}
	probe.metadata = std::move(prepared.metadata);
	return probe;
}
