#include "VisualStudioLocator.h"
#include "../utility/StringUtility.h"

#include "../../externals/nlohmann/json.hpp"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <set>
#include <sstream>

namespace {
	using json = nlohmann::json;

	std::filesystem::path GetEnvironmentPath(const wchar_t* variable) {
		const DWORD required = GetEnvironmentVariableW(variable, nullptr, 0);
		if (required == 0) {
			return {};
		}
		std::vector<wchar_t> buffer(required);
		if (GetEnvironmentVariableW(variable, buffer.data(), required) == 0) {
			return {};
		}
		return std::filesystem::path(buffer.data());
	}

	bool IsRegularFile(const std::filesystem::path& path) {
		std::error_code error;
		return std::filesystem::is_regular_file(path, error) && !error;
	}

	bool IsDirectory(const std::filesystem::path& path) {
		std::error_code error;
		return std::filesystem::is_directory(path, error) && !error;
	}

	bool ReadUtf8File(const std::filesystem::path& path, std::string& output) {
		std::ifstream input(path, std::ios::binary);
		if (!input.is_open()) {
			return false;
		}
		output.assign(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
		return !input.bad();
	}

	bool HasPackage(const json& packages, const char* packageId) {
		if (!packages.is_array()) {
			return false;
		}
		for (const json& package : packages) {
			if (package.is_object() && package.value("id", std::string{}) == packageId) {
				return true;
			}
		}
		return false;
	}
}

bool VisualStudioLocator::StartDiscovery(const std::filesystem::path& outputLogPath, std::string& errorMessage) {
	if (discoveryProcess_.IsRunning()) {
		errorMessage = "Visual Studio discovery is already running.";
		return false;
	}
	instances_.clear();
	usedKnownRootFallback_ = false;
	discoveryComplete_ = false;
	vswherePath_ = FindVsWhere();
	if (vswherePath_.empty()) {
		instances_ = DiscoverKnownRoots();
		usedKnownRootFallback_ = true;
		discoveryComplete_ = true;
		return true;
	}
	if (!outputLogPath.is_absolute()) {
		errorMessage = "Visual Studio discovery log path must be absolute.";
		return false;
	}
	discoveryLogPath_ = outputLogPath;
	WindowsProcessRequest request{};
	request.applicationPath = vswherePath_;
	request.arguments = {
		L"-products", L"*", L"-requires", L"Microsoft.Component.MSBuild",
		L"-format", L"json", L"-utf8", L"-prerelease"
	};
	request.workingDirectory = vswherePath_.parent_path();
	request.outputLogPath = discoveryLogPath_;
	return discoveryProcess_.Start(request, errorMessage);
}

bool VisualStudioLocator::PollDiscovery(std::string& errorMessage) {
	if (discoveryComplete_) {
		return true;
	}
	const WindowsProcessSnapshot snapshot = discoveryProcess_.Poll();
	if (snapshot.state == WindowsProcessState::Running) {
		return true;
	}
	if (snapshot.state != WindowsProcessState::Exited || snapshot.exitCode != 0) {
		errorMessage = "vswhere discovery failed.";
		return false;
	}
	if (!ParseVsWhereOutput(discoveryLogPath_, errorMessage)) {
		return false;
	}
	discoveryComplete_ = true;
	return true;
}

bool VisualStudioLocator::IsDiscoveryRunning() const {
	return discoveryProcess_.Poll().state == WindowsProcessState::Running;
}

std::optional<VisualStudioInstance> VisualStudioLocator::SelectPreferred(
	const VisualStudioSelectionRequest& request
) const {
	const std::string toolset = request.requiredToolset.empty() ? kCurrentProjectToolset : request.requiredToolset;
	const std::string sdk = request.requiredWindowsSdk.empty() ? kCurrentProjectWindowsSdk : request.requiredWindowsSdk;
	auto chooseBest = [](const std::vector<VisualStudioInstance>& candidates) -> std::optional<VisualStudioInstance> {
		if (candidates.empty()) {
			return std::nullopt;
		}
		return *std::max_element(candidates.begin(), candidates.end(), [](const VisualStudioInstance& left, const VisualStudioInstance& right) {
			return CompareVersions(left.installationVersion, right.installationVersion) < 0;
		});
	};
	for (const VisualStudioInstance& instance : instances_) {
		if (!request.preferredInstanceId.empty() && instance.instanceId == request.preferredInstanceId && instance.openable) {
			return instance;
		}
	}
	std::vector<VisualStudioInstance> candidates;
	for (const VisualStudioInstance& instance : instances_) {
		if (instance.majorVersion == kPreferredVisualStudioMajor && !instance.isPrerelease && IsBuildReady(instance, toolset, sdk)) {
			candidates.push_back(instance);
		}
	}
	if (const auto selected = chooseBest(candidates)) {
		return selected;
	}
	if (request.allowPrerelease) {
		for (const VisualStudioInstance& instance : instances_) {
			if (instance.majorVersion == kPreferredVisualStudioMajor && instance.isPrerelease && IsBuildReady(instance, toolset, sdk)) {
				candidates.push_back(instance);
			}
		}
		if (const auto selected = chooseBest(candidates)) {
			return selected;
		}
	}
	for (const VisualStudioInstance& instance : instances_) {
		if ((!instance.isPrerelease || request.allowPrerelease) && IsBuildReady(instance, toolset, sdk)) {
			candidates.push_back(instance);
		}
	}
	if (const auto selected = chooseBest(candidates)) {
		return selected;
	}
	for (const VisualStudioInstance& instance : instances_) {
		if ((!instance.isPrerelease || request.allowPrerelease) && instance.openable) {
			candidates.push_back(instance);
		}
	}
	return chooseBest(candidates);
}

bool VisualStudioLocator::LaunchSolution(
	const VisualStudioInstance& instance,
	const std::filesystem::path& solutionPath,
	WindowsProcess& process,
	std::string& errorMessage
) const {
	if (!instance.openable || !IsRegularFile(instance.devenvPath) || !IsRegularFile(solutionPath) || !solutionPath.is_absolute()) {
		errorMessage = "Visual Studio instance or Solution path is not openable.";
		return false;
	}
	WindowsProcessRequest request{};
	request.applicationPath = instance.devenvPath;
	request.arguments = { solutionPath.native() };
	request.workingDirectory = solutionPath.parent_path();
	return process.Start(request, errorMessage);
}

VisualStudioSelectionRequest VisualStudioLocator::MakeSelectionRequest(
	const std::string& preferredInstanceId,
	bool allowPrerelease,
	const ProjectCompatibilityReport& report
) {
	VisualStudioSelectionRequest request{};
	request.preferredInstanceId = preferredInstanceId;
	request.allowPrerelease = allowPrerelease;
	request.requiredToolset = report.platformToolset;
	request.requiredWindowsSdk = report.windowsTargetPlatformVersion;
	return request;
}

std::filesystem::path VisualStudioLocator::FindVsWhere() {
	const std::filesystem::path programFilesX86 = GetEnvironmentPath(L"ProgramFiles(x86)");
	const std::filesystem::path candidate = programFilesX86 / L"Microsoft Visual Studio" / L"Installer" / L"vswhere.exe";
	return IsRegularFile(candidate) ? candidate : std::filesystem::path{};
}

std::vector<VisualStudioInstance> VisualStudioLocator::DiscoverKnownRoots() {
	std::vector<VisualStudioInstance> instances;
	const std::filesystem::path programFiles = GetEnvironmentPath(L"ProgramFiles");
	if (programFiles.empty()) {
		return instances;
	}
	for (const wchar_t* major : { L"18", L"17" }) {
		for (const wchar_t* edition : { L"Community", L"Professional", L"Enterprise", L"BuildTools" }) {
			const std::filesystem::path installationPath = programFiles / L"Microsoft Visual Studio" / major / edition;
			const std::filesystem::path devenvPath = installationPath / L"Common7" / L"IDE" / L"devenv.exe";
			if (!IsRegularFile(devenvPath)) {
				continue;
		}
		VisualStudioInstance instance{};
		instance.majorVersion = std::stoi(major);
		instance.instanceId = "known-root-" + std::to_string(instance.majorVersion) + "-" + std::to_string(instances.size());
		instance.displayName = "Visual Studio known-root fallback";
			instance.capabilitiesKnown = false;
			instance.openable = true;
			instance.installationPath = installationPath;
			instance.devenvPath = devenvPath;
			instances.push_back(std::move(instance));
		}
	}
	return instances;
}

bool VisualStudioLocator::HasWindowsSdk(const std::string& version) {
	if (version.empty()) {
		return false;
	}
	for (const wchar_t* variable : { L"ProgramFiles(x86)", L"ProgramFiles" }) {
		const std::filesystem::path root = GetEnvironmentPath(variable);
		if (IsDirectory(root / L"Windows Kits" / L"10" / L"Include" / StringUtility::ToPath(version))) {
			return true;
		}
	}
	return false;
}

int VisualStudioLocator::ParseMajorVersion(const std::string& version) {
	const size_t separator = version.find('.');
	try {
		return std::stoi(version.substr(0, separator));
	} catch (const std::exception&) {
		return 0;
	}
}

int VisualStudioLocator::CompareVersions(const std::string& left, const std::string& right) {
	auto parse = [](const std::string& value) {
		std::vector<int> parts;
		std::stringstream stream(value);
		std::string part;
		while (std::getline(stream, part, '.')) {
			try {
				parts.push_back(std::stoi(part));
			} catch (const std::exception&) {
				parts.push_back(0);
			}
		}
		return parts;
	};
	const std::vector<int> leftParts = parse(left);
	const std::vector<int> rightParts = parse(right);
	const size_t count = (std::max)(leftParts.size(), rightParts.size());
	for (size_t index = 0; index < count; ++index) {
		const int leftPart = index < leftParts.size() ? leftParts[index] : 0;
		const int rightPart = index < rightParts.size() ? rightParts[index] : 0;
		if (leftPart != rightPart) {
			return leftPart < rightPart ? -1 : 1;
		}
	}
	return 0;
}

bool VisualStudioLocator::IsBuildReady(
	const VisualStudioInstance& instance,
	const std::string& toolset,
	const std::string& windowsSdk
) {
	return instance.openable && instance.capabilitiesKnown &&
		toolset == kCurrentProjectToolset && instance.hasV143 &&
		windowsSdk == kCurrentProjectWindowsSdk && instance.hasRequiredWindowsSdk;
}

bool VisualStudioLocator::ParseVsWhereOutput(const std::filesystem::path& outputLogPath, std::string& errorMessage) {
	std::string text;
	if (!ReadUtf8File(outputLogPath, text)) {
		errorMessage = "vswhere output could not be read.";
		return false;
	}
	try {
		const json source = json::parse(text);
		if (!source.is_array()) {
			errorMessage = "vswhere output is not a JSON array.";
			return false;
		}
		for (const json& item : source) {
			if (!item.is_object()) {
				continue;
			}
			VisualStudioInstance instance{};
			instance.instanceId = item.value("instanceId", std::string{});
			instance.displayName = item.value("displayName", std::string{});
			instance.installationVersion = item.value("installationVersion", std::string{});
			instance.installationPath = StringUtility::ToPath(item.value("installationPath", std::string{}));
			instance.isPrerelease = item.value("isPrerelease", false);
			instance.majorVersion = ParseMajorVersion(instance.installationVersion);
			instance.devenvPath = instance.installationPath / L"Common7" / L"IDE" / L"devenv.exe";
			instance.hasMSBuild = true;
			instance.capabilitiesKnown = true;
			instance.hasV143 = HasPackage(item.value("packages", json::array()), "Microsoft.VisualStudio.Component.VC.v143.x86.x64");
			instance.hasRequiredWindowsSdk = HasWindowsSdk(kCurrentProjectWindowsSdk);
			instance.openable = IsRegularFile(instance.devenvPath);
			instance.buildReady = IsBuildReady(instance, kCurrentProjectToolset, kCurrentProjectWindowsSdk);
			if (!instance.instanceId.empty()) {
				instances_.push_back(std::move(instance));
			}
		}
		return true;
	} catch (const json::exception&) {
		errorMessage = "vswhere output JSON could not be parsed.";
		return false;
	}
}
