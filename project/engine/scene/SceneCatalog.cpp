// 役割: Scene設定JSONを検証し、実行環境で利用できるScene一覧へ変換する。
#include "SceneCatalog.h"

#include "../utility/EditableResourcePath.h"
#include "../utility/StringUtility.h"
#include "../../externals/nlohmann/json.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <utility>

namespace {
	using json = nlohmann::json;
}

bool SceneCatalog::Load(
	const std::string& settingsFilePath,
	std::string& errorMessage
) {
	std::string source;
	if (!EditableResourcePath::ReadText(
		StringUtility::ToPath(settingsFilePath),
		source
	)) {
		errorMessage = "Scene settings file could not be read: " + settingsFilePath;
		return false;
	}

	std::vector<SceneDescriptor> loadedScenes;
	std::unordered_map<std::string, size_t> loadedIndices;
	std::string loadedStartSceneId;
	SceneStartupSettings loadedStartupSettings{};
	try {
		const json root = json::parse(source);
		if (!root.is_object() ||
			!root.contains("scenes") ||
			!root.at("scenes").is_array()) {
			errorMessage = "Scene settings must contain a scenes array";
			return false;
		}

		loadedStartSceneId = NormalizeId(
			root.value("startScene", std::string{})
		);
		if (root.contains("startup")) {
			if (!root.at("startup").is_object()) {
				errorMessage = "Scene settings startup must be an object";
				return false;
			}
			const json& startup = root.at("startup");
			auto readStartupMode = [
				&startup,
				&errorMessage
			](const char* key, SceneStartupMode& mode) {
				if (!startup.contains(key)) {
					return true;
				}
				if (!startup.at(key).is_string() ||
					!TryParseStartupMode(startup.at(key).get<std::string>(), mode)) {
					errorMessage = std::string("Invalid startup mode for ") + key;
					return false;
				}
				return true;
			};
			if (!readStartupMode("debug", loadedStartupSettings.debug) ||
				!readStartupMode(
					"development",
					loadedStartupSettings.development
				) || !readStartupMode("release", loadedStartupSettings.release)) {
				return false;
			}
		}
		if (loadedStartupSettings.release != SceneStartupMode::Runtime) {
			errorMessage = "Release startup mode must be RUNTIME";
			return false;
		}
		for (const json& entry : root.at("scenes")) {
			if (!entry.is_object()) {
				errorMessage = "Scene settings contain an invalid scene entry";
				return false;
			}

			SceneDescriptor descriptor{};
			descriptor.id = NormalizeId(entry.value("id", std::string{}));
			descriptor.displayName = entry.value("name", descriptor.id);
			descriptor.runtimeProfile = entry.value(
				"runtimeProfile",
				std::string{}
			);
			descriptor.assetPath = NormalizeAssetPath(entry.value(
				"path",
				std::string{}
			));
			if (descriptor.id.empty() || descriptor.assetPath.empty() ||
				descriptor.runtimeProfile.empty()) {
				errorMessage = "Each scene requires id, path, and runtimeProfile";
				return false;
			}
			if (loadedIndices.contains(descriptor.id)) {
				errorMessage = "Duplicate scene id: " + descriptor.id;
				return false;
			}

			descriptor.filePath = StringUtility::ToUtf8(
				EditableResourcePath::Resolve(
					StringUtility::ToPath(descriptor.assetPath)
				)
			);
			for (const SceneDescriptor& loaded : loadedScenes) {
				if (NormalizePathKey(loaded.filePath) ==
					NormalizePathKey(descriptor.filePath)) {
					errorMessage = "Duplicate scene path: " + descriptor.assetPath;
					return false;
				}
			}
			loadedIndices.emplace(descriptor.id, loadedScenes.size());
			loadedScenes.push_back(std::move(descriptor));
		}
	}
	catch (const json::exception& exception) {
		errorMessage = "Scene settings JSON is invalid: ";
		errorMessage += exception.what();
		return false;
	}

	if (loadedScenes.empty()) {
		errorMessage = "Scene settings contain no scenes";
		return false;
	}
	if (loadedStartSceneId.empty() ||
		!loadedIndices.contains(loadedStartSceneId)) {
		errorMessage = "startScene does not reference a registered scene";
		return false;
	}

	catalogFilePath_ = StringUtility::ToUtf8(
		EditableResourcePath::Resolve(StringUtility::ToPath(settingsFilePath))
	);
	startSceneId_ = std::move(loadedStartSceneId);
	startupSettings_ = loadedStartupSettings;
	scenes_ = std::move(loadedScenes);
	sceneIndices_ = std::move(loadedIndices);
	errorMessage.clear();
	return true;
}

bool SceneCatalog::Save(std::string& errorMessage) const {
	if (catalogFilePath_.empty()) {
		errorMessage = "Scene Catalog has no save path";
		return false;
	}
	if (scenes_.empty() || !Find(startSceneId_)) {
		errorMessage = "Scene Catalog has no valid startScene";
		return false;
	}

	json root;
	root["version"] = 1;
	root["startScene"] = startSceneId_;
	root["startup"] = {
		{ "debug", StartupModeToString(startupSettings_.debug) },
		{ "development", StartupModeToString(startupSettings_.development) },
		{ "release", StartupModeToString(startupSettings_.release) }
	};
	root["scenes"] = json::array();
	for (const SceneDescriptor& scene : scenes_) {
		std::string assetPath = scene.assetPath;
		if (assetPath.empty()) {
			assetPath = StringUtility::ToUtf8(
				EditableResourcePath::ToProjectRelative(
					StringUtility::ToPath(scene.filePath)
				)
			);
		}
		root["scenes"].push_back({
			{ "id", scene.id },
			{ "name", scene.displayName },
			{ "path", NormalizeAssetPath(assetPath) },
			{ "runtimeProfile", scene.runtimeProfile }
		});
	}

	if (!EditableResourcePath::WriteTextAtomically(
		StringUtility::ToPath(catalogFilePath_),
		root.dump(2)
	)) {
		errorMessage = "Scene Catalog could not be saved: " + catalogFilePath_;
		return false;
	}
	errorMessage.clear();
	return true;
}

bool SceneCatalog::RegisterScene(
	const SceneDescriptor& descriptor,
	std::string& errorMessage
) {
	SceneDescriptor prepared;
	if (!PrepareDescriptor(descriptor, prepared, errorMessage)) {
		return false;
	}
	if (Find(prepared.id)) {
		errorMessage = "Duplicate scene id: " + prepared.id;
		return false;
	}
	if (FindByFilePath(prepared.filePath)) {
		errorMessage = "Duplicate scene path: " + prepared.assetPath;
		return false;
	}

	scenes_.push_back(std::move(prepared));
	RebuildIndices();
	errorMessage.clear();
	return true;
}

bool SceneCatalog::UpdateScene(
	const std::string& sceneId,
	const std::string& displayName,
	const std::string& assetPath,
	std::string& errorMessage
) {
	const std::string normalizedId = NormalizeId(sceneId);
	const auto found = sceneIndices_.find(normalizedId);
	if (found == sceneIndices_.end()) {
		errorMessage = "Scene is not registered: " + normalizedId;
		return false;
	}

	SceneDescriptor source = scenes_[found->second];
	source.displayName = displayName;
	source.assetPath = assetPath;
	source.filePath.clear();
	SceneDescriptor prepared;
	if (!PrepareDescriptor(source, prepared, errorMessage)) {
		return false;
	}
	for (size_t index = 0; index < scenes_.size(); ++index) {
		if (index == found->second) {
			continue;
		}
		if (NormalizePathKey(scenes_[index].filePath) ==
			NormalizePathKey(prepared.filePath)) {
			errorMessage = "Duplicate scene path: " + prepared.assetPath;
			return false;
		}
	}

	scenes_[found->second] = std::move(prepared);
	RebuildIndices();
	errorMessage.clear();
	return true;
}

bool SceneCatalog::UnregisterScene(
	const std::string& sceneId,
	std::string& errorMessage
) {
	const std::string normalizedId = NormalizeId(sceneId);
	const auto found = sceneIndices_.find(normalizedId);
	if (found == sceneIndices_.end()) {
		errorMessage = "Scene is not registered: " + normalizedId;
		return false;
	}
	if (normalizedId == startSceneId_) {
		errorMessage = "The start Scene cannot be unregistered";
		return false;
	}

	scenes_.erase(scenes_.begin() + static_cast<std::ptrdiff_t>(found->second));
	RebuildIndices();
	errorMessage.clear();
	return true;
}

bool SceneCatalog::SetStartScene(
	const std::string& sceneId,
	std::string& errorMessage
) {
	const std::string normalizedId = NormalizeId(sceneId);
	if (!Find(normalizedId)) {
		errorMessage = "startScene does not reference a registered scene";
		return false;
	}
	startSceneId_ = normalizedId;
	errorMessage.clear();
	return true;
}

bool SceneCatalog::SetStartupMode(
	SceneBuildConfiguration configuration,
	SceneStartupMode mode,
	std::string& errorMessage
) {
	if (mode != SceneStartupMode::Editor &&
		mode != SceneStartupMode::Runtime) {
		errorMessage = "Unknown startup mode";
		return false;
	}
	if (configuration == SceneBuildConfiguration::Release &&
		mode != SceneStartupMode::Runtime) {
		errorMessage = "Release startup mode must be RUNTIME";
		return false;
	}
	switch (configuration) {
	case SceneBuildConfiguration::Debug:
		startupSettings_.debug = mode;
		break;
	case SceneBuildConfiguration::Development:
		startupSettings_.development = mode;
		break;
	case SceneBuildConfiguration::Release:
		startupSettings_.release = mode;
		break;
	default:
		errorMessage = "Unknown build configuration";
		return false;
	}
	errorMessage.clear();
	return true;
}

SceneStartupMode SceneCatalog::GetStartupMode(
	SceneBuildConfiguration configuration
) const {
	switch (configuration) {
	case SceneBuildConfiguration::Debug:
		return startupSettings_.debug;
	case SceneBuildConfiguration::Development:
		return startupSettings_.development;
	case SceneBuildConfiguration::Release:
		return startupSettings_.release;
	default:
		return SceneStartupMode::Runtime;
	}
}

const char* SceneCatalog::StartupModeToString(SceneStartupMode mode) {
	return mode == SceneStartupMode::Editor ? "EDITOR" : "RUNTIME";
}

const SceneDescriptor* SceneCatalog::Find(const std::string& sceneId) const {
	const auto found = sceneIndices_.find(NormalizeId(sceneId));
	return found == sceneIndices_.end() ? nullptr : &scenes_[found->second];
}

const SceneDescriptor* SceneCatalog::FindByFilePath(
	const std::string& filePath
) const {
	if (filePath.empty()) {
		return nullptr;
	}
	const std::string requestedPathKey = NormalizePathKey(StringUtility::ToUtf8(
		EditableResourcePath::Resolve(StringUtility::ToPath(filePath))
	));
	for (const SceneDescriptor& scene : scenes_) {
		if (NormalizePathKey(scene.filePath) == requestedPathKey) {
			return &scene;
		}
	}
	return nullptr;
}

const SceneDescriptor* SceneCatalog::GetStartScene() const {
	return Find(startSceneId_);
}

std::string SceneCatalog::NormalizeId(const std::string& sceneId) {
	std::string normalized = sceneId;
	std::transform(
		normalized.begin(),
		normalized.end(),
		normalized.begin(),
		[](unsigned char character) {
			return static_cast<char>(std::tolower(character));
		}
	);
	return normalized;
}

std::string SceneCatalog::NormalizeAssetPath(const std::string& assetPath) {
	if (assetPath.empty()) {
		return {};
	}
	std::string normalized = StringUtility::ToUtf8(
		StringUtility::ToPath(assetPath).lexically_normal()
	);
	std::replace(normalized.begin(), normalized.end(), '\\', '/');
	return normalized;
}

std::string SceneCatalog::NormalizePathKey(const std::string& path) {
	std::string normalized = NormalizeAssetPath(path);
#if defined(_WIN32)
	std::transform(
		normalized.begin(),
		normalized.end(),
		normalized.begin(),
		[](unsigned char character) {
			return static_cast<char>(std::tolower(character));
		}
	);
#endif
	return normalized;
}

bool SceneCatalog::TryParseStartupMode(
	const std::string& value,
	SceneStartupMode& mode
) {
	const std::string normalized = NormalizeId(value);
	if (normalized == "editor") {
		mode = SceneStartupMode::Editor;
		return true;
	}
	if (normalized == "runtime") {
		mode = SceneStartupMode::Runtime;
		return true;
	}
	return false;
}

bool SceneCatalog::PrepareDescriptor(
	const SceneDescriptor& source,
	SceneDescriptor& prepared,
	std::string& errorMessage
) const {
	prepared = source;
	prepared.id = NormalizeId(source.id);
	prepared.displayName = source.displayName.empty()
		? prepared.id
		: source.displayName;
	prepared.assetPath = NormalizeAssetPath(source.assetPath);
	if (prepared.assetPath.empty() && !source.filePath.empty()) {
		prepared.assetPath = NormalizeAssetPath(StringUtility::ToUtf8(
			EditableResourcePath::ToProjectRelative(
				StringUtility::ToPath(source.filePath)
			)
		));
	}
	if (prepared.id.empty() || prepared.assetPath.empty() ||
		prepared.runtimeProfile.empty()) {
		errorMessage = "Each scene requires id, path, and runtimeProfile";
		return false;
	}
	prepared.filePath = StringUtility::ToUtf8(
		EditableResourcePath::Resolve(
			StringUtility::ToPath(prepared.assetPath)
		)
	);
	errorMessage.clear();
	return true;
}

void SceneCatalog::RebuildIndices() {
	sceneIndices_.clear();
	for (size_t index = 0; index < scenes_.size(); ++index) {
		sceneIndices_.emplace(scenes_[index].id, index);
	}
}
