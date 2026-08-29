// 役割: Source ReaderのPCM sampleをbounded slotへ逐次Decodeする。
#include "AudioStreamDecoder.h"

#include <mfapi.h>
#include <mferror.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <propvarutil.h>
#include <wrl.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <system_error>
#include <thread>
#include <vector>

#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfreadwrite.lib")
#pragma comment(lib, "mfuuid.lib")

namespace {
	using Microsoft::WRL::ComPtr;

	std::string BuildStreamError(const char* operation, HRESULT result) {
		char message[192]{};
		sprintf_s(
			message,
			"%s failed (HRESULT 0x%08X).",
			operation,
			static_cast<unsigned int>(result)
		);
		return message;
	}

	size_t AlignUp(size_t value, size_t alignment) {
		return alignment == 0
			? value
			: ((value + alignment - 1) / alignment) * alignment;
	}
}

class AudioStreamDecoder::Impl {
public:
	enum class SlotState : uint8_t {
		Free,
		Filling,
		Ready,
		Submitted
	};

	struct Slot {
		std::vector<BYTE> bytes;
		uint32_t validBytes = 0;
		bool endOfStream = false;
		SlotState state = SlotState::Free;
	};

	~Impl() {
		Cancel();
		Join();
	}

	bool Start(const std::filesystem::path& path, bool shouldLoop) {
		if (worker_.joinable()) {
			return false;
		}
		filePath_ = path;
		loop_.store(shouldLoop);
		cancelRequested_.store(false);
		workerFinished_.store(false);
		try {
			worker_ = std::thread([this]() { WorkerMain(); });
		} catch (const std::system_error& exception) {
			SetError(std::string("Audio streaming worker creation failed: ") + exception.what());
			workerFinished_.store(true);
			return false;
		}
		return true;
	}

	void Cancel() {
		cancelRequested_.store(true);
		condition_.notify_all();
	}

	void SetLoop(bool loop) {
		loop_.store(loop);
	}

	void Join() {
		if (worker_.joinable()) {
			worker_.join();
		}
	}

	bool TryGetWaveFormat(
		WAVEFORMATEXTENSIBLE& format,
		uint32_t& formatSize
	) const {
		std::scoped_lock lock(mutex_);
		if (!formatReady_) {
			return false;
		}
		format = waveFormat_;
		formatSize = waveFormatSize_;
		return true;
	}

	uint32_t GetReadySlotCount() const {
		std::scoped_lock lock(mutex_);
		return static_cast<uint32_t>(std::count_if(
			slots_.begin(),
			slots_.end(),
			[](const Slot& slot) { return slot.state == SlotState::Ready; }
		));
	}

	bool TryAcquireReadySlot(AudioStreamDecoder::SlotView& view) {
		std::scoped_lock lock(mutex_);
		for (uint32_t index = 0; index < slots_.size(); ++index) {
			Slot& slot = slots_[index];
			if (slot.state != SlotState::Ready) {
				continue;
			}
			slot.state = SlotState::Submitted;
			view = {
				index,
				slot.bytes.data(),
				slot.validBytes,
				slot.endOfStream
			};
			return true;
		}
		return false;
	}

	void ReleaseSubmittedSlot(uint32_t slotIndex) {
		{
			std::scoped_lock lock(mutex_);
			if (slotIndex >= slots_.size()) {
				return;
			}
			Slot& slot = slots_[slotIndex];
			if (slot.state != SlotState::Submitted) {
				return;
			}
			slot.validBytes = 0;
			slot.endOfStream = false;
			slot.state = SlotState::Free;
		}
		condition_.notify_one();
	}

	bool HasFailed() const {
		std::scoped_lock lock(mutex_);
		return !error_.empty();
	}

	std::string GetError() const {
		std::scoped_lock lock(mutex_);
		return error_;
	}

	bool IsWorkerFinished() const {
		return workerFinished_.load();
	}

private:
	void SetError(std::string error) {
		std::scoped_lock lock(mutex_);
		if (error_.empty()) {
			error_ = std::move(error);
		}
	}

	bool AcquireFreeSlot(uint32_t& slotIndex) {
		std::unique_lock lock(mutex_);
		condition_.wait(lock, [this]() {
			return cancelRequested_.load() || std::any_of(
				slots_.begin(),
				slots_.end(),
				[](const Slot& slot) { return slot.state == SlotState::Free; }
			);
		});
		if (cancelRequested_.load()) {
			return false;
		}
		for (uint32_t index = 0; index < slots_.size(); ++index) {
			if (slots_[index].state == SlotState::Free) {
				slots_[index].state = SlotState::Filling;
				slots_[index].validBytes = 0;
				slots_[index].endOfStream = false;
				slotIndex = index;
				return true;
			}
		}
		return false;
	}

	void PublishSlot(uint32_t slotIndex, bool endOfStream) {
		{
			std::scoped_lock lock(mutex_);
			Slot& slot = slots_[slotIndex];
			slot.endOfStream = endOfStream;
			slot.state = SlotState::Ready;
		}
		condition_.notify_all();
	}

	bool SeekToStart(IMFSourceReader* sourceReader) {
		PROPVARIANT position;
		PropVariantInit(&position);
		position.vt = VT_I8;
		position.hVal.QuadPart = 0;
		const HRESULT result = sourceReader->SetCurrentPosition(GUID_NULL, position);
		PropVariantClear(&position);
		if (FAILED(result)) {
			SetError(BuildStreamError("IMFSourceReader::SetCurrentPosition", result));
			return false;
		}
		return true;
	}

	void WorkerMain() {
		const HRESULT comResult = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
		const bool comInitialized = comResult == S_OK || comResult == S_FALSE;
		if (!comInitialized) {
			SetError(BuildStreamError("CoInitializeEx", comResult));
			workerFinished_.store(true);
			condition_.notify_all();
			return;
		}

		ComPtr<IMFSourceReader> sourceReader;
		HRESULT result = MFCreateSourceReaderFromURL(
			filePath_.c_str(),
			nullptr,
			&sourceReader
		);
		if (SUCCEEDED(result)) {
			result = sourceReader->SetStreamSelection(MF_SOURCE_READER_ALL_STREAMS, FALSE);
		}
		if (SUCCEEDED(result)) {
			result = sourceReader->SetStreamSelection(
				MF_SOURCE_READER_FIRST_AUDIO_STREAM,
				TRUE
			);
		}
		ComPtr<IMFMediaType> pcmType;
		if (SUCCEEDED(result)) {
			result = MFCreateMediaType(&pcmType);
		}
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
		ComPtr<IMFMediaType> outputType;
		if (SUCCEEDED(result)) {
			result = sourceReader->GetCurrentMediaType(
				MF_SOURCE_READER_FIRST_AUDIO_STREAM,
				&outputType
			);
		}

		WAVEFORMATEX* allocatedFormat = nullptr;
		UINT32 allocatedFormatSize = 0;
		if (SUCCEEDED(result)) {
			result = MFCreateWaveFormatExFromMFMediaType(
				outputType.Get(),
				&allocatedFormat,
				&allocatedFormatSize
			);
		}
		if (
			FAILED(result) || !allocatedFormat ||
			allocatedFormatSize < sizeof(WAVEFORMATEX) ||
			allocatedFormatSize > sizeof(WAVEFORMATEXTENSIBLE) ||
			allocatedFormat->nBlockAlign == 0 ||
			allocatedFormat->nAvgBytesPerSec == 0
		) {
			if (allocatedFormat) {
				CoTaskMemFree(allocatedFormat);
			}
			SetError(FAILED(result)
				? BuildStreamError("Create streaming PCM format", result)
				: "Media Foundation returned an invalid streaming PCM format.");
			CoUninitialize();
			workerFinished_.store(true);
			condition_.notify_all();
			return;
		}

		const size_t blockAlign = allocatedFormat->nBlockAlign;
		if (blockAlign > 1024 * 1024) {
			SetError("Streaming PCM block alignment exceeds the slot limit.");
			CoTaskMemFree(allocatedFormat);
			CoUninitialize();
			workerFinished_.store(true);
			condition_.notify_all();
			return;
		}
		const size_t minimumBytes = AlignUp(16 * 1024, blockAlign);
		const size_t maximumBytes = (1024 * 1024 / blockAlign) * blockAlign;
		const size_t targetBytes = (std::clamp)(
			AlignUp(allocatedFormat->nAvgBytesPerSec / 4, blockAlign),
			minimumBytes,
			maximumBytes
		);
		{
			std::scoped_lock lock(mutex_);
			std::memcpy(&waveFormat_, allocatedFormat, allocatedFormatSize);
			waveFormatSize_ = allocatedFormatSize;
			for (Slot& slot : slots_) {
				slot.bytes.resize(targetBytes);
			}
			formatReady_ = true;
		}
		CoTaskMemFree(allocatedFormat);
		condition_.notify_all();

		uint32_t fillingSlot = 0;
		bool hasFillingSlot = AcquireFreeSlot(fillingSlot);
		bool decodedAnyBytes = false;
		while (hasFillingSlot && !cancelRequested_.load()) {
			DWORD streamIndex = 0;
			DWORD flags = 0;
			LONGLONG timestamp = 0;
			ComPtr<IMFSample> sample;
			result = sourceReader->ReadSample(
				MF_SOURCE_READER_FIRST_AUDIO_STREAM,
				0,
				&streamIndex,
				&flags,
				&timestamp,
				&sample
			);
			(void)streamIndex;
			(void)timestamp;
			if (FAILED(result) || (flags & MF_SOURCE_READERF_ERROR) != 0) {
				SetError(FAILED(result)
					? BuildStreamError("IMFSourceReader::ReadSample", result)
					: "Media Foundation reported a streaming decode error.");
				break;
			}
			if ((flags & MF_SOURCE_READERF_CURRENTMEDIATYPECHANGED) != 0) {
				SetError("Audio PCM format changed during streaming playback.");
				break;
			}

			if (sample) {
				ComPtr<IMFMediaBuffer> mediaBuffer;
				result = sample->ConvertToContiguousBuffer(&mediaBuffer);
				BYTE* source = nullptr;
				DWORD maximumLength = 0;
				DWORD currentLength = 0;
				if (SUCCEEDED(result)) {
					result = mediaBuffer->Lock(&source, &maximumLength, &currentLength);
				}
				(void)maximumLength;
				if (FAILED(result)) {
					SetError(BuildStreamError("Lock streaming PCM sample", result));
					break;
				}
				if (currentLength % blockAlign != 0) {
					mediaBuffer->Unlock();
					SetError("Streaming PCM sample is not block aligned.");
					break;
				}

				size_t sourceOffset = 0;
				while (sourceOffset < currentLength && !cancelRequested_.load()) {
					Slot& slot = slots_[fillingSlot];
					if (slot.validBytes == slot.bytes.size()) {
						PublishSlot(fillingSlot, false);
						hasFillingSlot = AcquireFreeSlot(fillingSlot);
						if (!hasFillingSlot) {
							break;
						}
						continue;
					}
					const size_t writable = slot.bytes.size() - slot.validBytes;
					const size_t remaining = currentLength - sourceOffset;
					const size_t copySize = (std::min)(writable, remaining);
					std::memcpy(
						slot.bytes.data() + slot.validBytes,
						source + sourceOffset,
						copySize
					);
					slot.validBytes += static_cast<uint32_t>(copySize);
					sourceOffset += copySize;
					decodedAnyBytes = true;
				}
				mediaBuffer->Unlock();
			}

			if ((flags & MF_SOURCE_READERF_ENDOFSTREAM) == 0) {
				continue;
			}
			if (!decodedAnyBytes) {
				SetError("Audio file did not contain streaming PCM samples.");
				break;
			}
			if (loop_.load()) {
				if (!SeekToStart(sourceReader.Get())) {
					break;
				}
				continue;
			}
			if (slots_[fillingSlot].validBytes > 0) {
				PublishSlot(fillingSlot, true);
				hasFillingSlot = false;
			}
			break;
		}

		if (hasFillingSlot) {
			std::scoped_lock lock(mutex_);
			Slot& slot = slots_[fillingSlot];
			if (slot.state == SlotState::Filling) {
				slot.validBytes = 0;
				slot.state = SlotState::Free;
			}
		}
		sourceReader.Reset();
		CoUninitialize();
		workerFinished_.store(true);
		condition_.notify_all();
	}

	std::filesystem::path filePath_;
	std::atomic_bool loop_ = false;
	mutable std::mutex mutex_;
	std::condition_variable condition_;
	std::array<Slot, AudioStreamDecoder::kSlotCount> slots_{};
	WAVEFORMATEXTENSIBLE waveFormat_{};
	uint32_t waveFormatSize_ = 0;
	bool formatReady_ = false;
	std::string error_;
	std::atomic_bool cancelRequested_ = false;
	std::atomic_bool workerFinished_ = false;
	std::thread worker_;
};

AudioStreamDecoder::AudioStreamDecoder() : impl_(std::make_unique<Impl>()) {}
AudioStreamDecoder::~AudioStreamDecoder() = default;

bool AudioStreamDecoder::Start(const std::filesystem::path& filePath, bool loop) {
	return impl_->Start(filePath, loop);
}

void AudioStreamDecoder::Cancel() { impl_->Cancel(); }
void AudioStreamDecoder::SetLoop(bool loop) { impl_->SetLoop(loop); }
void AudioStreamDecoder::Join() { impl_->Join(); }

bool AudioStreamDecoder::TryGetWaveFormat(
	WAVEFORMATEXTENSIBLE& format,
	uint32_t& formatSize
) const {
	return impl_->TryGetWaveFormat(format, formatSize);
}

uint32_t AudioStreamDecoder::GetReadySlotCount() const {
	return impl_->GetReadySlotCount();
}

bool AudioStreamDecoder::TryAcquireReadySlot(SlotView& slot) {
	return impl_->TryAcquireReadySlot(slot);
}

void AudioStreamDecoder::ReleaseSubmittedSlot(uint32_t slotIndex) {
	impl_->ReleaseSubmittedSlot(slotIndex);
}

bool AudioStreamDecoder::IsWorkerFinished() const {
	return impl_->IsWorkerFinished();
}

bool AudioStreamDecoder::HasFailed() const { return impl_->HasFailed(); }
std::string AudioStreamDecoder::GetError() const { return impl_->GetError(); }
