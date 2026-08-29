// 役割: Main Thread上でMedia FoundationのProcess lifetimeを管理する。
#pragma once

#include <string>

class MediaFoundationRuntime {
public:
	MediaFoundationRuntime() = default;
	MediaFoundationRuntime(const MediaFoundationRuntime&) = delete;
	MediaFoundationRuntime& operator=(const MediaFoundationRuntime&) = delete;

	// MFStartupをProcess内で一回だけ行い、失敗内容を保持する。
	bool Initialize();

	// 全DecoderとAudio Voiceの終了後にMFShutdownを行う。
	void Finalize();

	bool IsInitialized() const { return initialized_; }
	const std::string& GetLastError() const { return lastError_; }

private:
	bool initialized_ = false;
	std::string lastError_;
};
