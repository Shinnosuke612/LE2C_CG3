// 役割: CPU、メモリ、フレーム時間などの実行時性能情報を保持する。
#pragma once

#include <cstdint>
#include <string>

class SystemPerformanceMonitor {
public:
	struct Snapshot {
		bool cpuSupported = false;
		bool gpuSupported = false;
		float systemCpuUsage = 0.0f;
		float processCpuUsage = 0.0f;
		float processCpuOneCoreUsage = 0.0f;
		float gpuUsage = 0.0f;
		float processGpuUsage = 0.0f;
		float gpu3DUsage = 0.0f;
		float processGpu3DUsage = 0.0f;
		float gpuComputeUsage = 0.0f;
		float processGpuComputeUsage = 0.0f;
		float gpuCopyUsage = 0.0f;
		float processGpuCopyUsage = 0.0f;
		float gpuRawEngineUsage = 0.0f;
		float processGpuRawEngineUsage = 0.0f;
		uint32_t logicalProcessorCount = 1;
		uint32_t gpuEngineSampleCount = 0;
		uint32_t processGpuEngineSampleCount = 0;
		std::string gpuStatus;
	};

	static SystemPerformanceMonitor& GetInstance();

	void Update();
	const Snapshot& GetSnapshot() const { return snapshot_; }

private:
	SystemPerformanceMonitor() = default;
	~SystemPerformanceMonitor();
	SystemPerformanceMonitor(const SystemPerformanceMonitor&) = delete;
	SystemPerformanceMonitor& operator=(const SystemPerformanceMonitor&) = delete;

	void Initialize();
	void InitializeGpuQuery();
	void UpdateCpu();
	void UpdateGpu();
	void ShutdownGpuQuery();

	struct FileTimeSample {
		uint64_t systemIdle = 0;
		uint64_t systemKernel = 0;
		uint64_t systemUser = 0;
		uint64_t processKernel = 0;
		uint64_t processUser = 0;
	};

	bool initialized_ = false;
	bool hasCpuSample_ = false;
	bool hasUpdateTime_ = false;
	FileTimeSample lastCpuSample_{};
	Snapshot snapshot_{};

	void* gpuQuery_ = nullptr;
	void* gpuCounter_ = nullptr;
	bool gpuQueryReady_ = false;
	uint32_t processId_ = 0;
	double updateIntervalSeconds_ = 0.5;
	double lastUpdateSeconds_ = 0.0;
};
