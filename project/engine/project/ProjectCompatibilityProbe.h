// 役割: Project descriptorが指すBuild入力・出力をread-onlyで確認する。
#pragma once

#include "ProjectDescriptor.h"

#include <string>
#include <vector>

enum class ProjectCompatibilityFileState {
	Available,
	Missing,
	NotBuilt
};

struct ProjectCompatibilityReport {
	ProjectCompatibilityFileState solution = ProjectCompatibilityFileState::Missing;
	ProjectCompatibilityFileState msbuildProject = ProjectCompatibilityFileState::Missing;
	ProjectCompatibilityFileState developmentExecutable = ProjectCompatibilityFileState::NotBuilt;
	int solutionVisualStudioMajor = 0;
	std::string platformToolset;
	std::string windowsTargetPlatformVersion;
	bool projectRootHasReparsePoint = false;
	std::vector<std::string> errors;
	std::vector<std::string> warnings;

	bool IsDescriptorCompatible() const { return errors.empty(); }
};

class ProjectCompatibilityProbe {
public:
	ProjectCompatibilityReport Probe(const ProjectDescriptor& descriptor) const;
};
