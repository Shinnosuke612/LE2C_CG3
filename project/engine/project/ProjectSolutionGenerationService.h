// 役割: Project Manager用のSolution preview workspaceと生成復旧診断を所有する。
#pragma once

#include "ProjectRegistry.h"
#include "SolutionGenerationEmitter.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

class ProjectDescriptor;

enum class ProjectSolutionPreviewState {
	Unavailable,
	Ready,
	PreviewReady,
	Failed
};

struct ProjectSolutionPreviewSnapshot {
	ProjectSolutionPreviewState state = ProjectSolutionPreviewState::Unavailable;
	std::string projectId;
	std::string operationId;
	std::filesystem::path descriptorPath;
	std::filesystem::path stagingRoot;
	std::filesystem::path solutionPath;
	std::filesystem::path legacyArtifactDirectory;
	std::filesystem::path groupedArtifactDirectory;
	std::string inputIdentity;
	std::vector<SolutionGenerationArtifact> artifacts;
	std::string detail;
	uint32_t retiredArtifactCount = 0;
	uint32_t modifiedOwnedArtifactCount = 0;
	bool layoutMigrationRequired = false;
	bool canMigrateOutputLayout = false;
};

struct ProjectSolutionRecoverySnapshot {
	std::string operationId;
	std::string projectId;
	SolutionGenerationOperationState state = SolutionGenerationOperationState::RecoveryRequired;
	std::filesystem::path stagingRoot;
	std::filesystem::path rollbackRoot;
	uint32_t fileCount = 0;
	bool canRecheck = false;
	bool canCommitStaged = false;
	bool canResumeCommit = false;
	bool canRestorePrevious = false;
	std::string detail;
};

class ProjectSolutionGenerationService {
public:
	// Registryは呼び出し側が所有し、service lifetime中は有効でなければならない。
	explicit ProjectSolutionGenerationService(ProjectRegistry& registry);

	// descriptorとbuild specificationをread-onlyで診断する。Legacy／managed-sourceは非fatalなUnavailableを返す。
	bool InspectProject(const std::filesystem::path& descriptorPath, ProjectSolutionPreviewSnapshot& output, std::string& errorMessage) const;
	// PC local workspaceへpreviewを生成する。canonical Projectのfileは変更しない。
	bool CreatePreview(const std::filesystem::path& descriptorPath, ProjectSolutionPreviewSnapshot& output, std::string& errorMessage);
	// 新規ProjectのLegacy Root template outputをGrouped V1へ初回確定する。
	bool CreateInitialGroupedGeneration(
		const ProjectDescriptor& descriptor,
		const std::string& operationId,
		const std::vector<std::filesystem::path>& retiredLegacyArtifacts,
		std::string& errorMessage
	);
	bool CanAdoptPreview(const std::filesystem::path& descriptorPath, const std::string& operationId, std::string& errorMessage) const;
	bool AdoptPreview(const std::filesystem::path& descriptorPath, const std::string& operationId, std::string& errorMessage);
	bool CanMigrateOutputLayout(const std::filesystem::path& descriptorPath, const std::string& operationId, std::string& errorMessage) const;
	bool MigrateOutputLayout(const std::filesystem::path& descriptorPath, const std::string& operationId, std::string& errorMessage);
	bool CommitStaged(const std::string& operationId, std::string& errorMessage);
	bool ResumeCommit(const std::string& operationId, std::string& errorMessage);
	// 固定preview rootとRegistry journalを再起動後に安全なread-only状態へ復元する。
	bool DiscoverPreviews(std::string& errorMessage);
	bool ReconcileRecovery(std::string& errorMessage);
	bool RecheckRecovery(const std::string& operationId, std::string& errorMessage);
	bool RestorePrevious(const std::string& operationId, std::string& errorMessage);

	const std::vector<ProjectSolutionPreviewSnapshot>& GetPreviews() const { return previews_; }
	const std::vector<ProjectSolutionRecoverySnapshot>& GetRecoverySnapshots() const { return recoveries_; }

private:
	std::filesystem::path GetPreviewRoot() const;
	void RefreshRecoverySnapshots();
	void UpsertPreview(ProjectSolutionPreviewSnapshot value);
	bool BuildModel(const ProjectDescriptor& descriptor, SolutionGenerationModel& model, std::string& errorMessage) const;
	bool BuildCurrentModel(const std::filesystem::path& descriptorPath, ProjectDescriptor& descriptor,
		SolutionGenerationModel& model, std::string& errorMessage) const;

	ProjectRegistry& registry_;
	std::vector<ProjectSolutionPreviewSnapshot> previews_;
	std::vector<ProjectSolutionRecoverySnapshot> recoveries_;
};
