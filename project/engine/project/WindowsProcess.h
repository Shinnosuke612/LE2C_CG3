// 役割: Launcher用のWin32 Process起動、状態poll、Handle所有を管理する。
#pragma once

#include <Windows.h>

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

enum class WindowsProcessState {
	NotStarted,
	Running,
	Exited,
	Failed
};

struct WindowsProcessRequest {
	std::filesystem::path applicationPath;
	std::vector<std::wstring> arguments;
	std::filesystem::path workingDirectory;
	std::optional<std::filesystem::path> outputLogPath;
};

struct WindowsProcessSnapshot {
	WindowsProcessState state = WindowsProcessState::NotStarted;
	DWORD processId = 0;
	DWORD exitCode = STILL_ACTIVE;
};

class WindowsProcess {
public:
	WindowsProcess() = default;
	~WindowsProcess();
	WindowsProcess(const WindowsProcess&) = delete;
	WindowsProcess& operator=(const WindowsProcess&) = delete;
	WindowsProcess(WindowsProcess&& other) noexcept;
	WindowsProcess& operator=(WindowsProcess&& other) noexcept;

	bool Start(const WindowsProcessRequest& request, std::string& errorMessage);
	WindowsProcessSnapshot Poll() const;
	void Close();

	bool IsRunning() const { return Poll().state == WindowsProcessState::Running; }
	static std::wstring QuoteArgument(const std::wstring& argument);
	static std::wstring BuildCommandLine(const WindowsProcessRequest& request);

private:
	HANDLE processHandle_ = nullptr;
	DWORD processId_ = 0;
	mutable WindowsProcessState state_ = WindowsProcessState::NotStarted;
	mutable DWORD exitCode_ = STILL_ACTIVE;
};
