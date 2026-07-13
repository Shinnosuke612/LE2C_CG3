// 役割: Windowsの性能情報を取得し、実行時統計を更新する。
#include "SystemPerformanceMonitor.h"

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <pdh.h>
#include <pdhmsg.h>

#include <algorithm>
#include <chrono>
#include <cwctype>
#include <string>
#include <vector>

#pragma comment(lib, "Pdh.lib")

namespace {

uint64_t FileTimeToUint64(const FILETIME& fileTime) {
	ULARGE_INTEGER value{};
	value.LowPart = fileTime.dwLowDateTime;
	value.HighPart = fileTime.dwHighDateTime;
	return value.QuadPart;
}

double NowSeconds() {
	using Clock = std::chrono::steady_clock;
	return std::chrono::duration<double>(
		Clock::now().time_since_epoch()
	).count();
}

std::wstring ToLower(std::wstring text) {
	for (wchar_t& c : text) {
		c = static_cast<wchar_t>(std::towlower(c));
	}
	return text;
}

bool Contains(const std::wstring& text, const std::wstring& needle) {
	return text.find(needle) != std::wstring::npos;
}

float ClampPercent(double value) {
	return static_cast<float>(std::clamp(value, 0.0, 100.0));
}

bool IsValidPdhValue(const PDH_FMT_COUNTERVALUE& value) {
	return value.CStatus == PDH_CSTATUS_VALID_DATA ||
		value.CStatus == PDH_CSTATUS_NEW_DATA;
}

} // namespace

SystemPerformanceMonitor::~SystemPerformanceMonitor() {
	ShutdownGpuQuery();
}

SystemPerformanceMonitor& SystemPerformanceMonitor::GetInstance() {
	static SystemPerformanceMonitor instance;
	return instance;
}

void SystemPerformanceMonitor::Update() {
	Initialize();

	const double now = NowSeconds();
	if (hasUpdateTime_ && now - lastUpdateSeconds_ < updateIntervalSeconds_) {
		return;
	}

	UpdateCpu();
	UpdateGpu();

	lastUpdateSeconds_ = now;
	hasUpdateTime_ = true;
}

void SystemPerformanceMonitor::Initialize() {
	if (initialized_) {
		return;
	}

	initialized_ = true;
	processId_ = GetCurrentProcessId();

	SYSTEM_INFO systemInfo{};
	GetSystemInfo(&systemInfo);
	snapshot_.logicalProcessorCount =
		std::max<DWORD>(1, systemInfo.dwNumberOfProcessors);

	InitializeGpuQuery();
}

void SystemPerformanceMonitor::InitializeGpuQuery() {
	PDH_HQUERY query = nullptr;
	PDH_STATUS status = PdhOpenQueryW(nullptr, 0, &query);
	if (status != ERROR_SUCCESS) {
		snapshot_.gpuStatus = "PDH query unavailable";
		return;
	}

	PDH_HCOUNTER counter = nullptr;
	status = PdhAddEnglishCounterW(
		query,
		L"\\GPU Engine(*)\\Utilization Percentage",
		0,
		&counter
	);
	if (status != ERROR_SUCCESS) {
		PdhCloseQuery(query);
		snapshot_.gpuStatus = "GPU Engine counter unavailable";
		return;
	}

	PdhCollectQueryData(query);
	gpuQuery_ = query;
	gpuCounter_ = counter;
	gpuQueryReady_ = true;
	snapshot_.gpuStatus = "GPU Engine counter ready";
}

void SystemPerformanceMonitor::UpdateCpu() {
	FILETIME idleTime{};
	FILETIME kernelTime{};
	FILETIME userTime{};
	FILETIME createTime{};
	FILETIME exitTime{};
	FILETIME processKernelTime{};
	FILETIME processUserTime{};

	if (!GetSystemTimes(&idleTime, &kernelTime, &userTime) ||
		!GetProcessTimes(
			GetCurrentProcess(),
			&createTime,
			&exitTime,
			&processKernelTime,
			&processUserTime
		)) {
		snapshot_.cpuSupported = false;
		return;
	}

	FileTimeSample current{};
	current.systemIdle = FileTimeToUint64(idleTime);
	current.systemKernel = FileTimeToUint64(kernelTime);
	current.systemUser = FileTimeToUint64(userTime);
	current.processKernel = FileTimeToUint64(processKernelTime);
	current.processUser = FileTimeToUint64(processUserTime);

	if (hasCpuSample_) {
		const uint64_t lastSystemTotal =
			lastCpuSample_.systemKernel + lastCpuSample_.systemUser;
		const uint64_t currentSystemTotal =
			current.systemKernel + current.systemUser;
		const uint64_t systemTotalDelta = currentSystemTotal - lastSystemTotal;
		const uint64_t idleDelta =
			current.systemIdle - lastCpuSample_.systemIdle;
		const uint64_t processDelta =
			(current.processKernel + current.processUser) -
			(lastCpuSample_.processKernel + lastCpuSample_.processUser);

		if (systemTotalDelta > 0) {
			const double systemBusy =
				static_cast<double>(systemTotalDelta - idleDelta) /
				static_cast<double>(systemTotalDelta) * 100.0;
			const double processBusy =
				static_cast<double>(processDelta) /
				static_cast<double>(systemTotalDelta) * 100.0;
			snapshot_.systemCpuUsage = ClampPercent(systemBusy);
			snapshot_.processCpuUsage = ClampPercent(processBusy);
			snapshot_.processCpuOneCoreUsage = static_cast<float>(
				(std::max)(
					0.0,
					processBusy * snapshot_.logicalProcessorCount
				)
			);
			snapshot_.cpuSupported = true;
		}
	}

	lastCpuSample_ = current;
	hasCpuSample_ = true;
}

void SystemPerformanceMonitor::UpdateGpu() {
	if (!gpuQueryReady_ || !gpuQuery_ || !gpuCounter_) {
		snapshot_.gpuSupported = false;
		return;
	}

	PDH_HQUERY query = static_cast<PDH_HQUERY>(gpuQuery_);
	PDH_HCOUNTER counter = static_cast<PDH_HCOUNTER>(gpuCounter_);
	PDH_STATUS status = PdhCollectQueryData(query);
	if (status != ERROR_SUCCESS) {
		snapshot_.gpuSupported = false;
		snapshot_.gpuStatus = "GPU Engine collect failed";
		return;
	}

	DWORD bufferSize = 0;
	DWORD itemCount = 0;
	status = PdhGetFormattedCounterArrayW(
		counter,
		PDH_FMT_DOUBLE,
		&bufferSize,
		&itemCount,
		nullptr
	);
	if (status != PDH_MORE_DATA || bufferSize == 0 || itemCount == 0) {
		snapshot_.gpuSupported = false;
		snapshot_.gpuStatus = "GPU Engine sample pending";
		return;
	}

	std::vector<BYTE> buffer(bufferSize);
	auto* items = reinterpret_cast<PPDH_FMT_COUNTERVALUE_ITEM_W>(
		buffer.data()
	);
	status = PdhGetFormattedCounterArrayW(
		counter,
		PDH_FMT_DOUBLE,
		&bufferSize,
		&itemCount,
		items
	);
	if (status != ERROR_SUCCESS) {
		snapshot_.gpuSupported = false;
		snapshot_.gpuStatus = "GPU Engine sample failed";
		return;
	}

	const std::wstring processToken =
		L"pid_" + std::to_wstring(processId_) + L"_";
	double totalUsage = 0.0;
	double processUsage = 0.0;
	double total3D = 0.0;
	double process3D = 0.0;
	double totalCompute = 0.0;
	double processCompute = 0.0;
	double totalCopy = 0.0;
	double processCopy = 0.0;
	uint32_t validCount = 0;
	uint32_t processCount = 0;

	for (DWORD index = 0; index < itemCount; ++index) {
		if (!items[index].szName || !IsValidPdhValue(items[index].FmtValue)) {
			continue;
		}

		const double value = (std::max)(0.0, items[index].FmtValue.doubleValue);
		const std::wstring instanceName = ToLower(items[index].szName);
		const bool isProcess = Contains(instanceName, processToken);
		const bool is3D = Contains(instanceName, L"engtype_3d");
		const bool isCompute = Contains(instanceName, L"engtype_compute");
		const bool isCopy = Contains(instanceName, L"engtype_copy");

		totalUsage += value;
		++validCount;
		if (isProcess) {
			processUsage += value;
			++processCount;
		}
		if (is3D) {
			total3D += value;
			if (isProcess) process3D += value;
		} else if (isCompute) {
			totalCompute += value;
			if (isProcess) processCompute += value;
		} else if (isCopy) {
			totalCopy += value;
			if (isProcess) processCopy += value;
		}
	}

	snapshot_.gpuRawEngineUsage = static_cast<float>(totalUsage);
	snapshot_.processGpuRawEngineUsage = static_cast<float>(processUsage);
	snapshot_.gpuUsage = ClampPercent(totalUsage);
	snapshot_.processGpuUsage = ClampPercent(processUsage);
	snapshot_.gpu3DUsage = ClampPercent(total3D);
	snapshot_.processGpu3DUsage = ClampPercent(process3D);
	snapshot_.gpuComputeUsage = ClampPercent(totalCompute);
	snapshot_.processGpuComputeUsage = ClampPercent(processCompute);
	snapshot_.gpuCopyUsage = ClampPercent(totalCopy);
	snapshot_.processGpuCopyUsage = ClampPercent(processCopy);
	snapshot_.gpuEngineSampleCount = validCount;
	snapshot_.processGpuEngineSampleCount = processCount;
	snapshot_.gpuSupported = validCount > 0;
	snapshot_.gpuStatus = snapshot_.gpuSupported
		? "GPU Engine counter active"
		: "GPU Engine sample empty";
}

void SystemPerformanceMonitor::ShutdownGpuQuery() {
	if (gpuQuery_) {
		PdhCloseQuery(static_cast<PDH_HQUERY>(gpuQuery_));
	}
	gpuQuery_ = nullptr;
	gpuCounter_ = nullptr;
	gpuQueryReady_ = false;
}
