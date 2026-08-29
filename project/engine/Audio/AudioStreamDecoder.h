// 役割: Worker thread上のMedia Foundation逐次Decodeと固定PCM slotを所有する。
#pragma once

#include <Windows.h>
#include <mmreg.h>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>

class AudioStreamDecoder {
public:
	static constexpr uint32_t kSlotCount = 4;

	struct SlotView {
		uint32_t index = 0;
		const BYTE* data = nullptr;
		uint32_t size = 0;
		bool endOfStream = false;
	};

	AudioStreamDecoder();
	~AudioStreamDecoder();
	AudioStreamDecoder(const AudioStreamDecoder&) = delete;
	AudioStreamDecoder& operator=(const AudioStreamDecoder&) = delete;

	// Source ReaderとCOM apartmentは生成されるWorker threadだけが所有する。
	bool Start(const std::filesystem::path& filePath, bool loop);
	void Cancel();
	void SetLoop(bool loop);
	void Join();

	bool TryGetWaveFormat(WAVEFORMATEXTENSIBLE& format, uint32_t& formatSize) const;
	uint32_t GetReadySlotCount() const;
	bool TryAcquireReadySlot(SlotView& slot);
	void ReleaseSubmittedSlot(uint32_t slotIndex);

	bool IsWorkerFinished() const;
	bool HasFailed() const;
	std::string GetError() const;

private:
	class Impl;
	std::unique_ptr<Impl> impl_;
};
