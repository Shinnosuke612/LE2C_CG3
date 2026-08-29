// 役割: Media Foundationの開始と終了をHRESULTを失わずに実装する。
#include "MediaFoundationRuntime.h"

#include <mfapi.h>

#include <cstdio>

#pragma comment(lib, "mfplat.lib")

namespace {
	std::string BuildMediaFoundationError(const char* operation, HRESULT result) {
		char message[160]{};
		sprintf_s(
			message,
			"%s failed (HRESULT 0x%08X).",
			operation,
			static_cast<unsigned int>(result)
		);
		return message;
	}
}

bool MediaFoundationRuntime::Initialize() {
	Finalize();
	lastError_.clear();

	const HRESULT result = MFStartup(MF_VERSION, MFSTARTUP_FULL);
	if (FAILED(result)) {
		lastError_ = BuildMediaFoundationError("MFStartup", result);
		return false;
	}
	initialized_ = true;
	return true;
}

void MediaFoundationRuntime::Finalize() {
	if (!initialized_) {
		return;
	}
	const HRESULT result = MFShutdown();
	initialized_ = false;
	if (FAILED(result)) {
		lastError_ = BuildMediaFoundationError("MFShutdown", result);
	}
}
