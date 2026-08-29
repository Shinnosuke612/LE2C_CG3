// 役割: Visual Studio instance検出、互換評価、Solution起動requestを管理する。
#pragma once

#include "ProjectCompatibilityProbe.h"
#include "WindowsProcess.h"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

struct VisualStudioInstance {
	std::string instanceId;
	std::string displayName;
	std::string installationVersion;
	int majorVersion = 0;
	bool isPrerelease = false;
	bool capabilitiesKnown = false;
	bool hasMSBuild = false;
	bool hasV143 = false;
	bool hasRequiredWindowsSdk = false;
	bool openable = false;
	bool buildReady = false;
	std::filesystem::path installationPath;
	std::filesystem::path devenvPath;
};

struct VisualStudioSelectionRequest {
	std::string preferredInstanceId;
	bool allowPrerelease = false;
	std::string requiredToolset;
	std::string requiredWindowsSdk;
};

class VisualStudioLocator {
public:
	static constexpr int kPreferredVisualStudioMajor = 18;
	static constexpr const char* kCurrentProjectToolset = "v143";
	static constexpr const char* kCurrentProjectWindowsSdk = "10.0.26100.0";

	bool StartDiscovery(const std::filesystem::path& outputLogPath, std::string& errorMessage);
	bool PollDiscovery(std::string& errorMessage);
	bool IsDiscoveryRunning() const;
	const std::vector<VisualStudioInstance>& GetInstances() const { return instances_; }
	const std::filesystem::path& GetVsWherePath() const { return vswherePath_; }
	bool UsedKnownRootFallback() const { return usedKnownRootFallback_; }

	std::optional<VisualStudioInstance> SelectPreferred(
		const VisualStudioSelectionRequest& request
	) const;
	bool LaunchSolution(
		const VisualStudioInstance& instance,
		const std::filesystem::path& solutionPath,
		WindowsProcess& process,
		std::string& errorMessage
	) const;

	static VisualStudioSelectionRequest MakeSelectionRequest(
		const std::string& preferredInstanceId,
		bool allowPrerelease,
		const ProjectCompatibilityReport& report
	);

private:
	static std::filesystem::path FindVsWhere();
	static std::vector<VisualStudioInstance> DiscoverKnownRoots();
	static bool HasWindowsSdk(const std::string& version);
	static int ParseMajorVersion(const std::string& version);
	static int CompareVersions(const std::string& left, const std::string& right);
	static bool IsBuildReady(
		const VisualStudioInstance& instance,
		const std::string& toolset,
		const std::string& windowsSdk
	);
	bool ParseVsWhereOutput(const std::filesystem::path& outputLogPath, std::string& errorMessage);

	WindowsProcess discoveryProcess_;
	std::filesystem::path vswherePath_;
	std::filesystem::path discoveryLogPath_;
	std::vector<VisualStudioInstance> instances_;
	bool usedKnownRootFallback_ = false;
	bool discoveryComplete_ = false;
};
