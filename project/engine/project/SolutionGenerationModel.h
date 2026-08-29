// 役割: Solution生成に必要なvalidated source／target modelを構築する。
#pragma once

#include "ProjectBuildSpecification.h"
#include "ProjectDescriptor.h"

#include <filesystem>
#include <string>
#include <vector>

enum class SolutionGenerationFileKind {
	Compile,
	Include
};

enum class SolutionGenerationTargetKind {
	Engine,
	Game,
	Host
};

struct SolutionGenerationSourceFile {
	SolutionGenerationFileKind kind = SolutionGenerationFileKind::Compile;
	std::filesystem::path absolutePath;
	std::filesystem::path projectRelativePath;
};

struct SolutionGenerationTarget {
	SolutionGenerationTargetKind kind = SolutionGenerationTargetKind::Host;
	std::string name;
	std::string stableGuid;
	std::vector<SolutionGenerationSourceFile> sourceFiles;
};

struct SolutionGenerationModel {
	std::filesystem::path projectRoot;
	std::filesystem::path sourceDirectory;
	std::filesystem::path artifactDirectory;
	ProjectBuildToolchain toolchain;
	ProjectBuildExternalReferences externals;
	ProjectBuildRuntime runtime;
	std::vector<std::string> configurations;
	std::vector<SolutionGenerationTarget> targets;
};

class SolutionGenerationModelBuilder {
public:
	bool Build(
		const ProjectDescriptor& descriptor,
		const ProjectBuildSpecification& specification,
		SolutionGenerationModel& output,
		std::string& errorMessage
	) const;
};
