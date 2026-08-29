#include "ProjectDescriptor.h"

#include "../../externals/nlohmann/json.hpp"

#include <Windows.h>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <initializer_list>
#include <iterator>
#include <set>
#include <sstream>
#include <utility>

namespace {
	using json = nlohmann::json;

	bool ReadUtf8File(const std::filesystem::path& path, std::string& output) {
		std::ifstream input(path, std::ios::binary);
		if (!input.is_open()) {
			return false;
		}
		output.assign(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
		return !input.bad();
	}

	bool HasOnlyFields(const json& object, std::initializer_list<const char*> fields) {
		if (!object.is_object()) {
			return false;
		}
		const std::set<std::string> allowed(fields.begin(), fields.end());
		for (const auto& entry : object.items()) {
			if (allowed.find(entry.key()) == allowed.end()) {
				return false;
			}
		}
		return true;
	}

	bool ReadRequiredString(const json& object, const char* key, std::string& output) {
		if (!object.contains(key) || !object.at(key).is_string()) {
			return false;
		}
		output = object.at(key).get<std::string>();
		return true;
	}

	bool WriteUtf8Atomically(
		const std::filesystem::path& target,
		const std::string& content,
		std::string& errorMessage
	) {
		std::error_code error;
		const std::filesystem::path parent = target.parent_path();
		if (parent.empty() || !std::filesystem::exists(parent, error) || error) {
			errorMessage = "Descriptor directory does not exist.";
			return false;
		}
		std::filesystem::path temporary = target;
		temporary += L".tmp";
		{
			std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
			if (!output.is_open()) {
				errorMessage = "Descriptor temporary file could not be opened.";
				return false;
			}
			output.write(content.data(), static_cast<std::streamsize>(content.size()));
			output.flush();
			if (!output.good()) {
				output.close();
				std::filesystem::remove(temporary, error);
				errorMessage = "Descriptor temporary file could not be written.";
				return false;
			}
		}
		if (!MoveFileExW(
			temporary.c_str(), target.c_str(),
			MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH
		)) {
			std::filesystem::remove(temporary, error);
			errorMessage = "Descriptor atomic replace failed.";
			return false;
		}
		return true;
	}
}

void ProjectDescriptor::SetDescriptorPath(const std::filesystem::path& value) {
	descriptorPath_ = std::filesystem::absolute(value).lexically_normal();
	projectRoot_ = descriptorPath_.parent_path();
}

bool ProjectDescriptor::Load(
	const std::filesystem::path& descriptorPath,
	std::string& errorMessage
) {
	SetDescriptorPath(descriptorPath);
	std::string text;
	if (!ReadUtf8File(descriptorPath_, text)) {
		errorMessage = "Project descriptor could not be read.";
		return false;
	}
	try {
		const json root = json::parse(text);
		if (!HasOnlyFields(root, { "dependencies", "displayName", "engine", "ide", "paths", "projectId", "schemaVersion", "template" }) ||
			!root.contains("schemaVersion") || !root.at("schemaVersion").is_number_integer() ||
			root.at("schemaVersion").get<int>() != kSchemaVersion) {
			errorMessage = "Unsupported or invalid project descriptor schema.";
			return false;
		}
		if (!ReadRequiredString(root, "projectId", projectId_) ||
			!ReadRequiredString(root, "displayName", displayName_) ||
			!root.contains("ide") || !HasOnlyFields(root.at("ide"), { "preferredVisualStudioMajor" }) ||
			!root.at("ide").contains("preferredVisualStudioMajor") || !root.at("ide").at("preferredVisualStudioMajor").is_number_integer()) {
			errorMessage = "Project descriptor is missing required metadata.";
			return false;
		}
		preferredVisualStudioMajor_ = root.at("ide").at("preferredVisualStudioMajor").get<int>();

		if (!root.contains("engine") || !HasOnlyFields(root.at("engine"), { "mode", "provenance" }) ||
			!root.at("engine").contains("mode") || !root.at("engine").at("mode").is_string() ||
			!root.at("engine").contains("provenance") || !HasOnlyFields(root.at("engine").at("provenance"), { "repository", "revision" }) ||
			!ReadRequiredString(root.at("engine").at("provenance"), "repository", engineProvenance_.repository) ||
			!ReadRequiredString(root.at("engine").at("provenance"), "revision", engineProvenance_.revision)) {
			errorMessage = "Project descriptor engine metadata is invalid.";
			return false;
		}
		const std::string mode = root.at("engine").at("mode").get<std::string>();
		if (mode == "snapshot") {
			engineMode_ = ProjectEngineMode::Snapshot;
		} else if (mode == "managed-source") {
			engineMode_ = ProjectEngineMode::ManagedSource;
		} else {
			errorMessage = "Project descriptor engine mode is unsupported.";
			return false;
		}

		if (!root.contains("dependencies") || !HasOnlyFields(root.at("dependencies"), { "manifest", "lock" }) ||
			!ReadRequiredString(root.at("dependencies"), "manifest", dependencies_.manifest) ||
			!ReadRequiredString(root.at("dependencies"), "lock", dependencies_.lock) ||
			!root.contains("paths") || !HasOnlyFields(root.at("paths"), { "solution", "msbuildProject", "sceneCatalog", "developmentExecutable" }) ||
			!ReadRequiredString(root.at("paths"), "solution", paths_.solution) ||
			!ReadRequiredString(root.at("paths"), "msbuildProject", paths_.msbuildProject) ||
			!ReadRequiredString(root.at("paths"), "sceneCatalog", paths_.sceneCatalog) ||
			!ReadRequiredString(root.at("paths"), "developmentExecutable", paths_.developmentExecutable) ||
			!root.contains("template") || !HasOnlyFields(root.at("template"), { "id", "schemaVersion", "sourceKind" }) ||
			!ReadRequiredString(root.at("template"), "id", template_.id) ||
			!ReadRequiredString(root.at("template"), "sourceKind", template_.sourceKind) ||
			!root.at("template").contains("schemaVersion") || !root.at("template").at("schemaVersion").is_number_integer()) {
			errorMessage = "Project descriptor paths or template metadata is invalid.";
			return false;
		}
		template_.schemaVersion = root.at("template").at("schemaVersion").get<int>();
		schemaVersion_ = kSchemaVersion;
		return Validate(errorMessage);
	} catch (const json::exception&) {
		errorMessage = "Project descriptor JSON could not be parsed.";
		return false;
	}
}

bool ProjectDescriptor::Save(std::string& errorMessage) const {
	if (!Validate(errorMessage)) {
		return false;
	}
	json root = {
		{ "dependencies", { { "lock", dependencies_.lock }, { "manifest", dependencies_.manifest } } },
		{ "displayName", displayName_ },
		{ "engine", {
			{ "mode", engineMode_ == ProjectEngineMode::Snapshot ? "snapshot" : "managed-source" },
			{ "provenance", { { "repository", engineProvenance_.repository }, { "revision", engineProvenance_.revision } } }
		} },
		{ "ide", { { "preferredVisualStudioMajor", preferredVisualStudioMajor_ } } },
		{ "paths", {
			{ "developmentExecutable", paths_.developmentExecutable },
			{ "msbuildProject", paths_.msbuildProject },
			{ "sceneCatalog", paths_.sceneCatalog },
			{ "solution", paths_.solution }
		} },
		{ "projectId", projectId_ },
		{ "schemaVersion", kSchemaVersion },
		{ "template", { { "id", template_.id }, { "schemaVersion", template_.schemaVersion }, { "sourceKind", template_.sourceKind } } }
	};
	return WriteUtf8Atomically(descriptorPath_, root.dump(2) + "\n", errorMessage);
}

bool ProjectDescriptor::Validate(std::string& errorMessage) const {
	if (descriptorPath_.empty() || projectRoot_.empty()) {
		errorMessage = "Project descriptor path is not set.";
		return false;
	}
	if (schemaVersion_ != kSchemaVersion || !IsValidProjectId(projectId_) || !IsValidDisplayName(displayName_) ||
		preferredVisualStudioMajor_ < 1 || template_.id.empty() || template_.sourceKind.empty() || template_.schemaVersion != 1) {
		errorMessage = "Project descriptor metadata is invalid.";
		return false;
	}
	for (const std::string* path : { &paths_.solution, &paths_.msbuildProject, &paths_.sceneCatalog, &paths_.developmentExecutable, &dependencies_.manifest, &dependencies_.lock }) {
		if (!IsSafeProjectRelativePath(*path)) {
			errorMessage = "Project descriptor contains an unsafe relative path.";
			return false;
		}
	}
	if (!IsExpectedFileName(paths_.solution, ".sln", projectId_) ||
		!IsExpectedFileName(paths_.msbuildProject, ".vcxproj", projectId_)) {
		errorMessage = "Solution or MSBuild Project name does not match projectId.";
		return false;
	}
	return true;
}

std::filesystem::path ProjectDescriptor::ResolveProjectPath(const std::string& relativePath) const {
	if (projectRoot_.empty() || !IsSafeProjectRelativePath(relativePath)) {
		return {};
	}
	return (projectRoot_ / std::filesystem::path(relativePath)).lexically_normal();
}

ProjectOptionalFileState ProjectDescriptor::GetDependencyManifestState() const {
	if (dependencies_.manifest.empty()) {
		return ProjectOptionalFileState::NotConfigured;
	}
	std::error_code error;
	return std::filesystem::is_regular_file(ResolveProjectPath(dependencies_.manifest), error)
		? ProjectOptionalFileState::Available : ProjectOptionalFileState::NotConfigured;
}

ProjectOptionalFileState ProjectDescriptor::GetDependencyLockState() const {
	if (dependencies_.lock.empty()) {
		return ProjectOptionalFileState::NotConfigured;
	}
	std::error_code error;
	return std::filesystem::is_regular_file(ResolveProjectPath(dependencies_.lock), error)
		? ProjectOptionalFileState::Available : ProjectOptionalFileState::NotConfigured;
}

bool ProjectDescriptor::HasKnownSnapshotProvenance() const {
	return engineMode_ == ProjectEngineMode::Snapshot &&
		!engineProvenance_.repository.empty() && !engineProvenance_.revision.empty();
}

bool ProjectDescriptor::IsSafeProjectRelativePath(const std::string& value) {
	if (value.empty()) {
		return false;
	}
	const std::filesystem::path path(value);
	if (path.is_absolute() || path.has_root_name() || path.has_root_directory()) {
		return false;
	}
	for (const std::filesystem::path& part : path) {
		if (part == ".." || part == "." || part.empty()) {
			return false;
		}
	}
	return true;
}

bool ProjectDescriptor::IsValidProjectId(const std::string& value) {
	if (value.empty() || value.size() > 64 || !std::isalpha(static_cast<unsigned char>(value.front()))) {
		return false;
	}
	for (const unsigned char character : value) {
		if (!std::isalnum(character) && character != '_') {
			return false;
		}
	}
	std::string upper = value;
	std::transform(upper.begin(), upper.end(), upper.begin(), [](unsigned char character) { return static_cast<char>(std::toupper(character)); });
	if (upper == "CG2_2025_04_14") {
		return false;
	}
	if (upper == "CON" || upper == "PRN" || upper == "AUX" || upper == "NUL") {
		return false;
	}
	return !((upper.rfind("COM", 0) == 0 || upper.rfind("LPT", 0) == 0) && upper.size() == 4 && upper[3] >= '1' && upper[3] <= '9');
}

bool ProjectDescriptor::IsValidDisplayName(const std::string& value) {
	if (value.empty() || value.size() > 80) {
		return false;
	}
	for (const unsigned char character : value) {
		if (character < 0x20 || character == 0x7F || character == '"' || character == '\\') {
			return false;
		}
	}
	return true;
}

bool ProjectDescriptor::IsExpectedFileName(
	const std::string& value,
	const std::string& extension,
	const std::string& projectId
) {
	const std::filesystem::path path(value);
	return path.extension() == extension && path.stem().string() == projectId;
}
