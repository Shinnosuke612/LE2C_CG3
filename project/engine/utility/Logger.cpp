#include "Logger.h"
namespace Logger{
	void Logger::Log(const std::string& message){
		OutputDebugStringA(message.c_str());
	}
	void Log(const std::wstring& message){
		OutputDebugStringW(message.c_str());
	}
}
