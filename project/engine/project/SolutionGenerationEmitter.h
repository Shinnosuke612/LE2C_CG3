// 役割: validated modelからstaging preview用のMSBuild artifactをemitする。
#pragma once

#include "SolutionGenerationModel.h"

#include <filesystem>
#include <string>
#include <vector>

struct SolutionGenerationArtifact {
	std::filesystem::path relativePath;
	std::string contentHash;
};

struct SolutionGenerationPreview {
	std::filesystem::path stagingRoot;
	std::filesystem::path manifestPath;
	std::string inputIdentity;
	std::vector<SolutionGenerationArtifact> artifacts;
};

class SolutionGenerationEmitter {
public:
	std::string ComputeInputIdentity(const SolutionGenerationModel& model) const;
	bool EmitPreview(const SolutionGenerationModel& model, const std::filesystem::path& stagingRoot,
		SolutionGenerationPreview& output, std::string& errorMessage) const;
};
