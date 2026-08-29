// 役割: Game Projectのtracked descriptorを検証・保存する。
#pragma once

#include <filesystem>
#include <string>

enum class ProjectEngineMode {
	Snapshot,
	ManagedSource
};

enum class ProjectOptionalFileState {
	NotConfigured,
	Available
};

struct ProjectDescriptorPaths {
	std::string solution;
	std::string msbuildProject;
	std::string sceneCatalog;
	std::string developmentExecutable;
};

struct ProjectDescriptorTemplate {
	std::string id;
	int schemaVersion = 1;
	std::string sourceKind;
};

struct ProjectEngineProvenance {
	std::string repository;
	std::string revision;
};

struct ProjectDependencyPaths {
	std::string manifest;
	std::string lock;
};

class ProjectDescriptor {
public:
	static constexpr int kSchemaVersion = 1;

	bool Load(
		const std::filesystem::path& descriptorPath,
		std::string& errorMessage
	);
	bool Save(std::string& errorMessage) const;
	bool Validate(std::string& errorMessage) const;

	std::filesystem::path ResolveProjectPath(
		const std::string& relativePath
	) const;
	ProjectOptionalFileState GetDependencyManifestState() const;
	ProjectOptionalFileState GetDependencyLockState() const;
	bool HasKnownSnapshotProvenance() const;

	const std::filesystem::path& GetDescriptorPath() const { return descriptorPath_; }
	const std::filesystem::path& GetProjectRoot() const { return projectRoot_; }
	int GetSchemaVersion() const { return schemaVersion_; }
	const std::string& GetProjectId() const { return projectId_; }
	const std::string& GetDisplayName() const { return displayName_; }
	int GetPreferredVisualStudioMajor() const { return preferredVisualStudioMajor_; }
	ProjectEngineMode GetEngineMode() const { return engineMode_; }
	const ProjectEngineProvenance& GetEngineProvenance() const { return engineProvenance_; }
	const ProjectDependencyPaths& GetDependencies() const { return dependencies_; }
	const ProjectDescriptorPaths& GetPaths() const { return paths_; }
	const ProjectDescriptorTemplate& GetTemplate() const { return template_; }

	void SetProjectId(const std::string& value) { projectId_ = value; }
	void SetDisplayName(const std::string& value) { displayName_ = value; }
	void SetPreferredVisualStudioMajor(int value) { preferredVisualStudioMajor_ = value; }
	void SetEngineMode(ProjectEngineMode value) { engineMode_ = value; }
	void SetEngineProvenance(const ProjectEngineProvenance& value) { engineProvenance_ = value; }
	void SetDependencies(const ProjectDependencyPaths& value) { dependencies_ = value; }
	void SetPaths(const ProjectDescriptorPaths& value) { paths_ = value; }
	void SetTemplate(const ProjectDescriptorTemplate& value) { template_ = value; }
	void SetDescriptorPath(const std::filesystem::path& value);

	static bool IsSafeProjectRelativePath(const std::string& value);

private:
	static bool IsValidProjectId(const std::string& value);
	static bool IsValidDisplayName(const std::string& value);
	static bool IsExpectedFileName(
		const std::string& value,
		const std::string& extension,
		const std::string& projectId
	);

	std::filesystem::path descriptorPath_;
	std::filesystem::path projectRoot_;
	int schemaVersion_ = kSchemaVersion;
	std::string projectId_;
	std::string displayName_;
	int preferredVisualStudioMajor_ = 18;
	ProjectEngineMode engineMode_ = ProjectEngineMode::Snapshot;
	ProjectEngineProvenance engineProvenance_{};
	ProjectDependencyPaths dependencies_{};
	ProjectDescriptorPaths paths_{};
	ProjectDescriptorTemplate template_{};
};
