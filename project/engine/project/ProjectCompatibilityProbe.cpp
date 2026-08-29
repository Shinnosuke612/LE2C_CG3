#include "ProjectCompatibilityProbe.h"

#include <Windows.h>

#include <fstream>
#include <iterator>
#include <regex>
#include <set>
#include <sstream>

namespace {
	bool ReadUtf8File(const std::filesystem::path& path, std::string& output) {
		std::ifstream input(path, std::ios::binary);
		if (!input.is_open()) {
			return false;
		}
		output.assign(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
		return !input.bad();
	}

	bool IsReparsePoint(const std::filesystem::path& path) {
		const DWORD attributes = GetFileAttributesW(path.c_str());
		return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
	}

	bool HasReparsePointInExistingPath(const std::filesystem::path& path) {
		std::filesystem::path current = path.root_path();
		if (!current.empty() && IsReparsePoint(current)) {
			return true;
		}
		for (const std::filesystem::path& part : path.relative_path()) {
			current /= part;
			std::error_code error;
			if (!std::filesystem::exists(current, error) || error) {
				break;
			}
			if (IsReparsePoint(current)) {
				return true;
			}
		}
		return false;
	}

	std::set<std::string> ExtractXmlValues(const std::string& text, const char* name) {
		const std::regex expression(std::string("<") + name + R"(>\s*([^<\r\n]+?)\s*</)" + name + ">");
		std::set<std::string> values;
		for (std::sregex_iterator iterator(text.begin(), text.end(), expression), end; iterator != end; ++iterator) {
			values.insert((*iterator)[1].str());
		}
		return values;
	}
}

ProjectCompatibilityReport ProjectCompatibilityProbe::Probe(const ProjectDescriptor& descriptor) const {
	ProjectCompatibilityReport report{};
	const std::filesystem::path root = descriptor.GetProjectRoot();
	if (root.empty() || HasReparsePointInExistingPath(root)) {
		report.projectRootHasReparsePoint = true;
		report.errors.push_back("Project root contains a reparse point.");
		return report;
	}

	const std::filesystem::path solution = descriptor.ResolveProjectPath(descriptor.GetPaths().solution);
	const std::filesystem::path msbuildProject = descriptor.ResolveProjectPath(descriptor.GetPaths().msbuildProject);
	const std::filesystem::path executable = descriptor.ResolveProjectPath(descriptor.GetPaths().developmentExecutable);
	if (HasReparsePointInExistingPath(solution) || HasReparsePointInExistingPath(msbuildProject) ||
		HasReparsePointInExistingPath(executable)) {
		report.errors.push_back("Project descriptor path crosses a reparse point.");
		return report;
	}
	std::error_code error;
	report.solution = std::filesystem::is_regular_file(solution, error) ? ProjectCompatibilityFileState::Available : ProjectCompatibilityFileState::Missing;
	if (report.solution == ProjectCompatibilityFileState::Missing) {
		report.errors.push_back("Solution file is missing.");
	}
	error.clear();
	report.msbuildProject = std::filesystem::is_regular_file(msbuildProject, error) ? ProjectCompatibilityFileState::Available : ProjectCompatibilityFileState::Missing;
	if (report.msbuildProject == ProjectCompatibilityFileState::Missing) {
		report.errors.push_back("MSBuild Project file is missing.");
	}
	error.clear();
	report.developmentExecutable = std::filesystem::is_regular_file(executable, error) ? ProjectCompatibilityFileState::Available : ProjectCompatibilityFileState::NotBuilt;
	if (report.developmentExecutable == ProjectCompatibilityFileState::NotBuilt) {
		report.warnings.push_back("Development executable is not built.");
	}

	std::string solutionText;
	if (report.solution == ProjectCompatibilityFileState::Available && ReadUtf8File(solution, solutionText)) {
		const std::regex version(R"(# Visual Studio Version ([0-9]+))");
		std::smatch match;
		if (std::regex_search(solutionText, match, version)) {
			report.solutionVisualStudioMajor = std::stoi(match[1].str());
		} else {
			report.errors.push_back("Solution Visual Studio version header is missing.");
		}
		const std::string expectedProjectEntry = "\"" + descriptor.GetProjectId() + "\", \"" + descriptor.GetProjectId() + ".vcxproj\"";
		if (solutionText.find(expectedProjectEntry) == std::string::npos) {
			report.errors.push_back("Solution does not contain the descriptor Project ID.");
		}
	} else if (report.solution == ProjectCompatibilityFileState::Available) {
		report.errors.push_back("Solution file could not be read.");
	}

	std::string projectText;
	if (report.msbuildProject == ProjectCompatibilityFileState::Available && ReadUtf8File(msbuildProject, projectText)) {
		const std::set<std::string> toolsets = ExtractXmlValues(projectText, "PlatformToolset");
		const std::set<std::string> sdks = ExtractXmlValues(projectText, "WindowsTargetPlatformVersion");
		if (toolsets.size() == 1) {
			report.platformToolset = *toolsets.begin();
		} else {
			report.errors.push_back("MSBuild PlatformToolset is missing or inconsistent.");
		}
		if (sdks.size() == 1) {
			report.windowsTargetPlatformVersion = *sdks.begin();
		} else {
			report.errors.push_back("WindowsTargetPlatformVersion is missing or inconsistent.");
		}
	} else if (report.msbuildProject == ProjectCompatibilityFileState::Available) {
		report.errors.push_back("MSBuild Project file could not be read.");
	}
	return report;
}
