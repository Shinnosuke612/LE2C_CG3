#include "SceneDocument.h"

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <functional>
#include <system_error>
#include <unordered_set>

#include <Windows.h>

#include "../../externals/nlohmann/json.hpp"

namespace {
	using json = nlohmann::json;

	json VectorToJson(const Vector3& value) {
		return json::array({ value.x, value.y, value.z });
	}

	Vector3 JsonToVector(const json& value, const Vector3& fallback) {
		if (!value.is_array() || value.size() != 3) {
			return fallback;
		}
		return {
			value[0].get<float>(),
			value[1].get<float>(),
			value[2].get<float>()
		};
	}
}

void SceneDocument::Clear(const std::string& sceneName) {
	sceneName_ = sceneName;
	entities_.clear();
	nextId_ = 1;
	dirty_ = false;
}

bool SceneDocument::Load(const std::string& filePath) {
	if (LoadInternal(filePath)) {
		return true;
	}

	const std::string backupPath = filePath + ".bak";
	if (!LoadInternal(backupPath)) {
		return false;
	}

	dirty_ = true;
	return true;
}

bool SceneDocument::Save(const std::string& filePath) {
	json root;
	root["version"] = 1;
	root["sceneName"] = sceneName_;
	root["entities"] = json::array();

	for (const SceneEntity& entity : entities_) {
		root["entities"].push_back({
			{ "id", entity.id },
			{ "parentId", entity.parentId },
			{ "name", entity.name },
			{ "active", entity.active },
			{ "transform", {
				{ "scale", VectorToJson(entity.transform.scale) },
				{ "rotate", VectorToJson(entity.transform.rotate) },
				{ "translate", VectorToJson(entity.transform.translate) }
			} },
			{ "modelPath", entity.modelPath },
			{ "components", entity.components }
		});
	}

	const std::filesystem::path target(filePath);
	const std::filesystem::path temporary = target.string() + ".tmp";
	const std::filesystem::path backup = target.string() + ".bak";
	std::error_code error;
	if (!target.parent_path().empty()) {
		std::filesystem::create_directories(target.parent_path(), error);
		if (error) {
			return false;
		}
	}

	{
		std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
		if (!output.is_open()) {
			return false;
		}
		output << root.dump(2);
		output.flush();
		if (!output.good()) {
			output.close();
			std::filesystem::remove(temporary, error);
			return false;
		}
	}

	if (std::filesystem::exists(target, error) && !error) {
		std::filesystem::copy_file(
			target,
			backup,
			std::filesystem::copy_options::overwrite_existing,
			error
		);
		if (error) {
			std::filesystem::remove(temporary, error);
			return false;
		}
	}

	if (!MoveFileExW(
		temporary.c_str(),
		target.c_str(),
		MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH
	)) {
		std::filesystem::remove(temporary, error);
		return false;
	}

	dirty_ = false;
	return true;
}

SceneEntity& SceneDocument::CreateEntity(
	const std::string& name,
	uint64_t parentId
) {
	SceneEntity entity{};
	entity.id = nextId_++;
	entity.parentId = FindEntity(parentId) ? parentId : 0;
	entity.name = name.empty() ? "Entity" : name;
	entities_.push_back(entity);
	dirty_ = true;
	return entities_.back();
}

bool SceneDocument::RemoveEntity(uint64_t id) {
	if (!FindEntity(id)) {
		return false;
	}

	std::unordered_set<uint64_t> removeIds{ id };
	bool foundChild = true;
	while (foundChild) {
		foundChild = false;
		for (const SceneEntity& entity : entities_) {
			if (
				removeIds.contains(entity.parentId) &&
				!removeIds.contains(entity.id)
			) {
				removeIds.insert(entity.id);
				foundChild = true;
			}
		}
	}

	const auto oldSize = entities_.size();
	entities_.erase(
		std::remove_if(
			entities_.begin(),
			entities_.end(),
			[&removeIds](const SceneEntity& entity) {
				return removeIds.contains(entity.id);
			}
		),
		entities_.end()
	);
	if (entities_.size() == oldSize) {
		return false;
	}
	dirty_ = true;
	return true;
}

uint64_t SceneDocument::DuplicateEntity(uint64_t id) {
	const SceneEntity* source = FindEntity(id);
	if (!source) {
		return 0;
	}

	const std::vector<SceneEntity> sourceEntities = entities_;
	std::function<uint64_t(uint64_t, uint64_t, bool)> duplicateBranch;
	duplicateBranch = [this, &sourceEntities, &duplicateBranch](
		uint64_t sourceId,
		uint64_t newParentId,
		bool isRoot
	) -> uint64_t {
		const auto found = std::find_if(
			sourceEntities.begin(),
			sourceEntities.end(),
			[sourceId](const SceneEntity& entity) {
				return entity.id == sourceId;
			}
		);
		if (found == sourceEntities.end()) {
			return 0;
		}

		SceneEntity& duplicate = CreateEntity(
			isRoot ? found->name + " Copy" : found->name,
			newParentId
		);
		const uint64_t duplicateId = duplicate.id;
		duplicate.active = found->active;
		duplicate.transform = found->transform;
		duplicate.modelPath = found->modelPath;
		duplicate.components = found->components;
		for (const SceneEntity& child : sourceEntities) {
			if (child.parentId == sourceId) {
				duplicateBranch(child.id, duplicateId, false);
			}
		}
		return duplicateId;
	};

	return duplicateBranch(id, source->parentId, true);
}

bool SceneDocument::SetParent(uint64_t id, uint64_t parentId) {
	SceneEntity* entity = FindEntity(id);
	if (!entity || id == parentId) {
		return false;
	}
	if (parentId != 0 && !FindEntity(parentId)) {
		return false;
	}
	if (parentId != 0 && IsDescendantOf(parentId, id)) {
		return false;
	}
	if (entity->parentId == parentId) {
		return true;
	}
	entity->parentId = parentId;
	dirty_ = true;
	return true;
}

bool SceneDocument::MoveEntity(uint64_t id, int direction) {
	if (direction == 0) {
		return false;
	}
	const SceneEntity* entity = FindEntity(id);
	if (!entity) {
		return false;
	}
	const uint64_t parentId = entity->parentId;
	std::vector<size_t> siblingIndices;
	for (size_t index = 0; index < entities_.size(); ++index) {
		if (entities_[index].parentId == parentId) {
			siblingIndices.push_back(index);
		}
	}
	const auto sibling = std::find_if(
		siblingIndices.begin(),
		siblingIndices.end(),
		[this, id](size_t index) { return entities_[index].id == id; }
	);
	if (sibling == siblingIndices.end()) {
		return false;
	}
	const std::ptrdiff_t position = std::distance(
		siblingIndices.begin(),
		sibling
	);
	const std::ptrdiff_t targetPosition = position + (direction < 0 ? -1 : 1);
	if (
		targetPosition < 0 ||
		targetPosition >= static_cast<std::ptrdiff_t>(siblingIndices.size())
	) {
		return false;
	}
	std::swap(
		entities_[siblingIndices[position]],
		entities_[siblingIndices[targetPosition]]
	);
	dirty_ = true;
	return true;
}

bool SceneDocument::IsDescendantOf(
	uint64_t id,
	uint64_t potentialAncestorId
) const {
	std::unordered_set<uint64_t> visited;
	const SceneEntity* current = FindEntity(id);
	while (current && current->parentId != 0) {
		if (current->parentId == potentialAncestorId) {
			return true;
		}
		if (!visited.insert(current->id).second) {
			return false;
		}
		current = FindEntity(current->parentId);
	}
	return false;
}

SceneEntity* SceneDocument::FindEntity(uint64_t id) {
	const auto found = std::find_if(
		entities_.begin(),
		entities_.end(),
		[id](const SceneEntity& entity) { return entity.id == id; }
	);
	return found == entities_.end() ? nullptr : &(*found);
}

const SceneEntity* SceneDocument::FindEntity(uint64_t id) const {
	const auto found = std::find_if(
		entities_.begin(),
		entities_.end(),
		[id](const SceneEntity& entity) { return entity.id == id; }
	);
	return found == entities_.end() ? nullptr : &(*found);
}

SceneEntity* SceneDocument::FindEntityByName(const std::string& name) {
	const auto found = std::find_if(
		entities_.begin(),
		entities_.end(),
		[&name](const SceneEntity& entity) { return entity.name == name; }
	);
	return found == entities_.end() ? nullptr : &(*found);
}

const SceneEntity* SceneDocument::FindEntityByName(const std::string& name) const {
	const auto found = std::find_if(
		entities_.begin(),
		entities_.end(),
		[&name](const SceneEntity& entity) { return entity.name == name; }
	);
	return found == entities_.end() ? nullptr : &(*found);
}

bool SceneDocument::LoadInternal(const std::string& filePath) {
	std::ifstream input(filePath, std::ios::binary);
	if (!input.is_open()) {
		return false;
	}

	try {
		const json root = json::parse(input);
		if (!root.is_object() || !root.contains("entities")) {
			return false;
		}

		sceneName_ = root.value("sceneName", std::string{});
		entities_.clear();
		for (const json& source : root.at("entities")) {
			SceneEntity entity{};
			entity.id = source.value("id", uint64_t{});
			entity.parentId = source.value("parentId", uint64_t{});
			entity.name = source.value("name", std::string("Entity"));
			entity.active = source.value("active", true);
			entity.modelPath = source.value("modelPath", std::string{});
			entity.components = source.value(
				"components",
				std::vector<std::string>{}
			);
			if (source.contains("transform")) {
				const json& transform = source.at("transform");
				if (transform.contains("scale")) {
					entity.transform.scale = JsonToVector(
						transform.at("scale"),
						entity.transform.scale
					);
				}
				if (transform.contains("rotate")) {
					entity.transform.rotate = JsonToVector(
						transform.at("rotate"),
						entity.transform.rotate
					);
				}
				if (transform.contains("translate")) {
					entity.transform.translate = JsonToVector(
						transform.at("translate"),
						entity.transform.translate
					);
				}
			}
			if (entity.id != 0) {
				entities_.push_back(std::move(entity));
			}
		}
	}
	catch (...) {
		entities_.clear();
		return false;
	}

	RebuildNextId();
	ValidateHierarchy();
	dirty_ = false;
	return true;
}

void SceneDocument::RebuildNextId() {
	nextId_ = 1;
	for (const SceneEntity& entity : entities_) {
		nextId_ = (std::max)(nextId_, entity.id + 1);
	}
}

void SceneDocument::ValidateHierarchy() {
	for (SceneEntity& entity : entities_) {
		if (
			entity.parentId == entity.id ||
			(entity.parentId != 0 && !FindEntity(entity.parentId))
		) {
			entity.parentId = 0;
			continue;
		}

		std::unordered_set<uint64_t> visited{ entity.id };
		const SceneEntity* parent = FindEntity(entity.parentId);
		while (parent) {
			if (!visited.insert(parent->id).second) {
				entity.parentId = 0;
				break;
			}
			parent = FindEntity(parent->parentId);
		}
	}
}
