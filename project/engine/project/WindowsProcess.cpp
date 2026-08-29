#include "WindowsProcess.h"

#include <system_error>
#include <utility>

namespace {
	std::string FormatWindowsError(const char* operation) {
		return std::string(operation) + " failed (Win32 error " + std::to_string(GetLastError()) + ").";
	}

	bool IsAbsoluteFile(const std::filesystem::path& path) {
		std::error_code error;
		return path.is_absolute() && std::filesystem::is_regular_file(path, error) && !error;
	}
}

WindowsProcess::~WindowsProcess() {
	Close();
}

WindowsProcess::WindowsProcess(WindowsProcess&& other) noexcept
	: processHandle_(std::exchange(other.processHandle_, nullptr)),
	processId_(std::exchange(other.processId_, 0)),
	state_(std::exchange(other.state_, WindowsProcessState::NotStarted)),
	exitCode_(std::exchange(other.exitCode_, STILL_ACTIVE)) {
}

WindowsProcess& WindowsProcess::operator=(WindowsProcess&& other) noexcept {
	if (this != &other) {
		Close();
		processHandle_ = std::exchange(other.processHandle_, nullptr);
		processId_ = std::exchange(other.processId_, 0);
		state_ = std::exchange(other.state_, WindowsProcessState::NotStarted);
		exitCode_ = std::exchange(other.exitCode_, STILL_ACTIVE);
	}
	return *this;
}

bool WindowsProcess::Start(const WindowsProcessRequest& request, std::string& errorMessage) {
	if (processHandle_ != nullptr && Poll().state == WindowsProcessState::Running) {
		errorMessage = "A process is already running.";
		return false;
	}
	Close();
	if (!IsAbsoluteFile(request.applicationPath) || !request.workingDirectory.is_absolute()) {
		errorMessage = "Application path and working directory must be absolute existing paths.";
		state_ = WindowsProcessState::Failed;
		return false;
	}
	std::error_code directoryError;
	if (!std::filesystem::is_directory(request.workingDirectory, directoryError) || directoryError) {
		errorMessage = "Working directory does not exist.";
		state_ = WindowsProcessState::Failed;
		return false;
	}

	HANDLE logHandle = nullptr;
	STARTUPINFOW startupInfo{};
	startupInfo.cb = sizeof(startupInfo);
	if (request.outputLogPath.has_value()) {
		const std::filesystem::path& logPath = *request.outputLogPath;
		if (!logPath.is_absolute()) {
			errorMessage = "Output log path must be absolute.";
			state_ = WindowsProcessState::Failed;
			return false;
		}
		std::filesystem::create_directories(logPath.parent_path(), directoryError);
		if (directoryError) {
			errorMessage = "Output log directory could not be created.";
			state_ = WindowsProcessState::Failed;
			return false;
		}
		SECURITY_ATTRIBUTES attributes{};
		attributes.nLength = sizeof(attributes);
		attributes.bInheritHandle = TRUE;
		logHandle = CreateFileW(
			logPath.c_str(), GENERIC_WRITE, FILE_SHARE_READ, &attributes,
			CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr
		);
		if (logHandle == INVALID_HANDLE_VALUE) {
			logHandle = nullptr;
			errorMessage = FormatWindowsError("Output log creation");
			state_ = WindowsProcessState::Failed;
			return false;
		}
		startupInfo.dwFlags |= STARTF_USESTDHANDLES;
		startupInfo.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
		startupInfo.hStdOutput = logHandle;
		startupInfo.hStdError = logHandle;
	}

	std::wstring commandLine = BuildCommandLine(request);
	std::vector<wchar_t> mutableCommandLine(commandLine.begin(), commandLine.end());
	mutableCommandLine.push_back(L'\0');
	PROCESS_INFORMATION processInfo{};
	const BOOL created = CreateProcessW(
		request.applicationPath.c_str(), mutableCommandLine.data(), nullptr, nullptr,
		logHandle != nullptr, CREATE_NO_WINDOW, nullptr, request.workingDirectory.c_str(),
		&startupInfo, &processInfo
	);
	if (logHandle != nullptr) {
		CloseHandle(logHandle);
	}
	if (!created) {
		errorMessage = FormatWindowsError("CreateProcessW");
		state_ = WindowsProcessState::Failed;
		return false;
	}
	CloseHandle(processInfo.hThread);
	processHandle_ = processInfo.hProcess;
	processId_ = processInfo.dwProcessId;
	exitCode_ = STILL_ACTIVE;
	state_ = WindowsProcessState::Running;
	return true;
}

WindowsProcessSnapshot WindowsProcess::Poll() const {
	if (processHandle_ == nullptr) {
		return { state_, processId_, exitCode_ };
	}
	DWORD exitCode = STILL_ACTIVE;
	if (!GetExitCodeProcess(processHandle_, &exitCode)) {
		state_ = WindowsProcessState::Failed;
		exitCode_ = STILL_ACTIVE;
		return { state_, processId_, exitCode_ };
	}
	exitCode_ = exitCode;
	state_ = exitCode == STILL_ACTIVE ? WindowsProcessState::Running : WindowsProcessState::Exited;
	return { state_, processId_, exitCode_ };
}

void WindowsProcess::Close() {
	if (processHandle_ != nullptr) {
		CloseHandle(processHandle_);
		processHandle_ = nullptr;
	}
	processId_ = 0;
	if (state_ != WindowsProcessState::Failed) {
		state_ = WindowsProcessState::NotStarted;
	}
	exitCode_ = STILL_ACTIVE;
}

std::wstring WindowsProcess::QuoteArgument(const std::wstring& argument) {
	if (argument.empty()) {
		return L"\"\"";
	}
	if (argument.find_first_of(L" \t\n\v\"") == std::wstring::npos) {
		return argument;
	}
	std::wstring quoted = L"\"";
	size_t backslashCount = 0;
	for (const wchar_t character : argument) {
		if (character == L'\\') {
			++backslashCount;
			continue;
		}
		if (character == L'\"') {
			quoted.append(backslashCount * 2 + 1, L'\\');
			quoted.push_back(L'\"');
			backslashCount = 0;
			continue;
		}
		quoted.append(backslashCount, L'\\');
		backslashCount = 0;
		quoted.push_back(character);
	}
	quoted.append(backslashCount * 2, L'\\');
	quoted.push_back(L'\"');
	return quoted;
}

std::wstring WindowsProcess::BuildCommandLine(const WindowsProcessRequest& request) {
	std::wstring commandLine = QuoteArgument(request.applicationPath.native());
	for (const std::wstring& argument : request.arguments) {
		commandLine += L' ';
		commandLine += QuoteArgument(argument);
	}
	return commandLine;
}
