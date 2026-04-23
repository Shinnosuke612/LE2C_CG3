#include "Logger.h"
#include "StringUtility.h"

#include <filesystem>
#include <fstream>
#include <chrono>
#include <format>
#include <cassert>

namespace {
	std::ofstream gLogStream;
}

namespace Logger {

	void Initialize() {
		std::filesystem::create_directories("Logs");

		// 現在時刻を取得
		std::chrono::system_clock::time_point now = std::chrono::system_clock::now();
		std::chrono::time_point<std::chrono::system_clock, std::chrono::seconds>
			nowSeconds = std::chrono::time_point_cast<std::chrono::seconds>(now);

		// ローカル時間へ変換
		std::chrono::zoned_time localTime{ std::chrono::current_zone(), nowSeconds };

		// ファイル名生成
		std::string dateString = std::format("{:%Y%m%d_%H%M%S}", localTime);
		std::string logFilePath = std::string("Logs/") + dateString + ".log";

		// ファイルオープン
		gLogStream.open(logFilePath, std::ios::out);
		assert(gLogStream.is_open());
	}

	void Log(const std::string& message) {
		OutputDebugStringA(message.c_str());

		if (gLogStream.is_open()) {
			gLogStream << message;
			gLogStream.flush();
		}
	}

	void Log(const std::wstring& message) {
		OutputDebugStringW(message.c_str());

		if (gLogStream.is_open()) {
			gLogStream << StringUtility::ConvertString(message);
			gLogStream.flush();
		}
	}

	void Finalize() {
		if (gLogStream.is_open()) {
			gLogStream.close();
		}
	}
}