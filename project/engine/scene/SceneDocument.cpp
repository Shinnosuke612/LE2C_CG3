#include "SceneDocument.h"
#include "../math/Matrix4x4.h"

#include <algorithm>
#include <cmath>
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

	json VectorToJson(const Vector2& value) {
		return json::array({ value.x, value.y });
	}

	json VectorToJson(const Vector4& value) {
		return json::array({ value.x, value.y, value.z, value.w });
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

	Vector2 JsonToVector(const json& value, const Vector2& fallback) {
		if (!value.is_array() || value.size() != 2) {
			return fallback;
		}
		return { value[0].get<float>(), value[1].get<float>() };
	}

	Vector4 JsonToVector(const json& value, const Vector4& fallback) {
		if (!value.is_array() || value.size() != 4) {
			return fallback;
		}
		return {
			value[0].get<float>(), value[1].get<float>(),
			value[2].get<float>(), value[3].get<float>()
		};
	}

	json ComponentToJson(const SceneComponent& component) {
		json result = {
			{ "type", component.type },
			{ "enabled", component.enabled }
		};
		if (component.type == "MeshRenderer") {
			result["modelPath"] = component.modelPath;
			result["cullMode"] = component.meshCullMode;
		} else if (component.type == "SpriteRenderer") {
			result["texturePath"] = component.texturePath;
			result["size"] = VectorToJson(component.spriteSize);
			result["anchor"] = VectorToJson(component.spriteAnchor);
			result["color"] = VectorToJson(component.spriteColor);
			result["flipX"] = component.spriteFlipX;
			result["flipY"] = component.spriteFlipY;
		} else if (component.type == "Camera") {
			result["isMain"] = component.cameraIsMain;
			result["fovY"] = component.cameraFovY;
			result["nearClip"] = component.cameraNearClip;
			result["farClip"] = component.cameraFarClip;
		} else if (component.type == "MonitorRenderer") {
			result["cameraName"] = component.monitorCameraName;
			result["width"] = component.monitorWidth;
			result["height"] = component.monitorHeight;
			result["hideSelf"] = component.monitorHideSelf;
		}
		return result;
	}

	std::vector<SceneComponent> ComponentsFromJson(const json& source) {
		std::vector<SceneComponent> components;
		if (!source.is_array()) {
			return components;
		}
		for (const json& value : source) {
			SceneComponent component{};
			if (value.is_string()) {
				component.type = value.get<std::string>();
			} else if (value.is_object()) {
				component.type = value.value("type", std::string{});
				component.enabled = value.value("enabled", true);
				component.modelPath = value.value("modelPath", std::string{});
				component.meshCullMode = value.value(
					"cullMode",
					component.meshCullMode
				);
				component.texturePath = value.value("texturePath", std::string{});
				if (value.contains("size")) {
					component.spriteSize = JsonToVector(
						value.at("size"),
						component.spriteSize
					);
				}
				if (value.contains("anchor")) {
					component.spriteAnchor = JsonToVector(
						value.at("anchor"),
						component.spriteAnchor
					);
				}
				if (value.contains("color")) {
					component.spriteColor = JsonToVector(
						value.at("color"),
						component.spriteColor
					);
				}
				component.spriteFlipX = value.value("flipX", false);
				component.spriteFlipY = value.value("flipY", false);
				component.cameraIsMain = value.value("isMain", false);
				component.cameraFovY = value.value("fovY", component.cameraFovY);
				component.cameraNearClip = value.value(
					"nearClip",
					component.cameraNearClip
				);
				component.cameraFarClip = value.value(
					"farClip",
					component.cameraFarClip
				);
				component.monitorCameraName = value.value(
					"cameraName",
					component.monitorCameraName
				);
				component.monitorWidth = value.value(
					"width",
					component.monitorWidth
				);
				component.monitorHeight = value.value(
					"height",
					component.monitorHeight
				);
				component.monitorHideSelf = value.value(
					"hideSelf",
					component.monitorHideSelf
				);
			}
			if (!component.type.empty()) {
				const auto duplicate = std::find_if(
					components.begin(),
					components.end(),
					[&component](const SceneComponent& existing) {
						return existing.type == component.type;
					}
				);
				if (duplicate == components.end()) {
					components.push_back(std::move(component));
				}
			}
		}
		return components;
	}

	SceneComponent* FindComponent(
		SceneEntity& entity,
		const std::string& type
	) {
		const auto found = std::find_if(
			entity.components.begin(),
			entity.components.end(),
			[&type](const SceneComponent& component) {
				return component.type == type;
			}
		);
		return found == entity.components.end() ? nullptr : &(*found);
	}

	const SceneComponent* FindComponent(
		const SceneEntity& entity,
		const std::string& type
	) {
		const auto found = std::find_if(
			entity.components.begin(),
			entity.components.end(),
			[&type](const SceneComponent& component) {
				return component.type == type;
			}
		);
		return found == entity.components.end() ? nullptr : &(*found);
	}

	void SynchronizeLegacyRendererFields(SceneEntity& entity) {
		if (SceneComponent* meshRenderer = FindComponent(entity, "MeshRenderer")) {
			if (meshRenderer->modelPath.empty()) {
				meshRenderer->modelPath = entity.modelPath;
			}
			entity.modelPath = meshRenderer->modelPath;
		}
		if (SceneComponent* spriteRenderer = FindComponent(entity, "SpriteRenderer")) {
			if (spriteRenderer->texturePath.empty() && !entity.spriteTexturePath.empty()) {
				spriteRenderer->texturePath = entity.spriteTexturePath;
				spriteRenderer->spriteSize = entity.spriteSize;
				spriteRenderer->spriteAnchor = entity.spriteAnchor;
				spriteRenderer->spriteColor = entity.spriteColor;
				spriteRenderer->spriteFlipX = entity.spriteFlipX;
				spriteRenderer->spriteFlipY = entity.spriteFlipY;
			}
			entity.spriteTexturePath = spriteRenderer->texturePath;
			entity.spriteSize = spriteRenderer->spriteSize;
			entity.spriteAnchor = spriteRenderer->spriteAnchor;
			entity.spriteColor = spriteRenderer->spriteColor;
			entity.spriteFlipX = spriteRenderer->spriteFlipX;
			entity.spriteFlipY = spriteRenderer->spriteFlipY;
		}
	}

	Matrix4x4 CalculateEntityWorldMatrix(
		const SceneDocument& document,
		const SceneEntity& entity,
		std::unordered_set<uint64_t>& visited
	) {
		const Matrix4x4 local = MakeAffineMatrix(
			entity.transform.scale,
			entity.transform.rotate,
			entity.transform.translate
		);
		if (entity.parentId == 0 || !visited.insert(entity.id).second) {
			return local;
		}
		const SceneEntity* parent = document.FindEntity(entity.parentId);
		if (!parent) {
			return local;
		}
		return Multiply(
			local,
			CalculateEntityWorldMatrix(document, *parent, visited)
		);
	}

	Matrix4x4 CalculateEntityWorldMatrix(
		const SceneDocument& document,
		const SceneEntity& entity
	) {
		std::unordered_set<uint64_t> visited;
		return CalculateEntityWorldMatrix(document, entity, visited);
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
	root["version"] = 7;
	root["sceneName"] = sceneName_;
	root["entities"] = json::array();

	for (const SceneEntity& entity : entities_) {
		json components = json::array();
		for (const SceneComponent& component : entity.components) {
			components.push_back(ComponentToJson(component));
		}
		const SceneComponent* meshRenderer = FindComponent(entity, "MeshRenderer");
		const SceneComponent* spriteRenderer = FindComponent(entity, "SpriteRenderer");
		const std::string modelPath = meshRenderer
			? meshRenderer->modelPath
			: entity.modelPath;
		const std::string spriteTexturePath = spriteRenderer
			? spriteRenderer->texturePath
			: entity.spriteTexturePath;
		const Vector2 spriteSize = spriteRenderer
			? spriteRenderer->spriteSize
			: entity.spriteSize;
		const Vector2 spriteAnchor = spriteRenderer
			? spriteRenderer->spriteAnchor
			: entity.spriteAnchor;
		const Vector4 spriteColor = spriteRenderer
			? spriteRenderer->spriteColor
			: entity.spriteColor;
		const bool spriteFlipX = spriteRenderer
			? spriteRenderer->spriteFlipX
			: entity.spriteFlipX;
		const bool spriteFlipY = spriteRenderer
			? spriteRenderer->spriteFlipY
			: entity.spriteFlipY;
		root["entities"].push_back({
			{ "id", entity.id },
			{ "parentId", entity.parentId },
			{ "name", entity.name },
			{ "active", entity.active },
			{ "locked", entity.locked },
			{ "transform", {
				{ "scale", VectorToJson(entity.transform.scale) },
				{ "rotate", VectorToJson(entity.transform.rotate) },
				{ "translate", VectorToJson(entity.transform.translate) }
			} },
			{ "modelPath", modelPath },
			{ "sprite", {
				{ "texturePath", spriteTexturePath },
				{ "size", VectorToJson(spriteSize) },
				{ "anchor", VectorToJson(spriteAnchor) },
				{ "color", VectorToJson(spriteColor) },
				{ "flipX", spriteFlipX },
				{ "flipY", spriteFlipY }
			} },
			{ "components", components }
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
		duplicate.locked = found->locked;
		duplicate.transform = found->transform;
		duplicate.modelPath = found->modelPath;
		duplicate.spriteTexturePath = found->spriteTexturePath;
		duplicate.spriteSize = found->spriteSize;
		duplicate.spriteAnchor = found->spriteAnchor;
		duplicate.spriteColor = found->spriteColor;
		duplicate.spriteFlipX = found->spriteFlipX;
		duplicate.spriteFlipY = found->spriteFlipY;
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
	const Matrix4x4 worldMatrix = CalculateEntityWorldMatrix(*this, *entity);
	Matrix4x4 localMatrix = worldMatrix;
	if (const SceneEntity* newParent = FindEntity(parentId)) {
		const Matrix4x4 parentWorld =
			CalculateEntityWorldMatrix(*this, *newParent);
		if (std::abs(Determinant(parentWorld)) < 0.000001f) {
			return false;
		}
		localMatrix = Multiply(
			worldMatrix,
			Inverse(parentWorld)
		);
	}
	Vector3 localScale{};
	Vector3 localRotate{};
	Vector3 localTranslate{};
	if (!DecomposeAffineMatrix(
		localMatrix,
		localScale,
		localRotate,
		localTranslate
	)) {
		return false;
	}
	entity->parentId = parentId;
	entity->transform.scale = localScale;
	entity->transform.rotate = localRotate;
	entity->transform.translate = localTranslate;
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

bool SceneDocument::AddComponent(uint64_t id, const std::string& type) {
	if (type.empty()) {
		return false;
	}
	SceneEntity* entity = FindEntity(id);
	if (!entity) {
		return false;
	}
	const auto found = std::find_if(
		entity->components.begin(),
		entity->components.end(),
		[&type](const SceneComponent& component) {
			return component.type == type;
		}
	);
	if (found != entity->components.end()) {
		bool changed = false;
		if (type == "MeshRenderer" && found->modelPath.empty()) {
			found->modelPath = entity->modelPath;
			changed = true;
		} else if (type == "MeshRenderer" && found->meshCullMode.empty()) {
			found->meshCullMode = "Back";
			changed = true;
		} else if (type == "SpriteRenderer" && found->texturePath.empty()) {
			found->texturePath = entity->spriteTexturePath;
			found->spriteSize = entity->spriteSize;
			found->spriteAnchor = entity->spriteAnchor;
			found->spriteColor = entity->spriteColor;
			found->spriteFlipX = entity->spriteFlipX;
			found->spriteFlipY = entity->spriteFlipY;
			changed = true;
		} else if (type == "Camera") {
			if (found->cameraNearClip <= 0.0f) {
				found->cameraNearClip = 0.1f;
				changed = true;
			}
			if (found->cameraFarClip <= found->cameraNearClip) {
				found->cameraFarClip = 1000.0f;
				changed = true;
			}
		} else if (type == "MonitorRenderer") {
			const uint32_t width = std::clamp<uint32_t>(
				found->monitorWidth,
				64,
				2048
			);
			const uint32_t height = std::clamp<uint32_t>(
				found->monitorHeight,
				64,
				2048
			);
			if (found->monitorWidth != width || found->monitorHeight != height) {
				found->monitorWidth = width;
				found->monitorHeight = height;
				changed = true;
			}
		}
		if (!found->enabled) {
			found->enabled = true;
			changed = true;
		}
		if (changed) {
			dirty_ = true;
		}
		return true;
	}
	SceneComponent component{ type, true };
	if (type == "MeshRenderer") {
		component.modelPath = entity->modelPath;
		component.meshCullMode = "Back";
	} else if (type == "SpriteRenderer") {
		component.texturePath = entity->spriteTexturePath;
		component.spriteSize = entity->spriteSize;
		component.spriteAnchor = entity->spriteAnchor;
		component.spriteColor = entity->spriteColor;
		component.spriteFlipX = entity->spriteFlipX;
		component.spriteFlipY = entity->spriteFlipY;
	} else if (type == "Camera") {
		component.cameraIsMain = false;
		component.cameraFovY = 0.45f;
		component.cameraNearClip = 0.1f;
		component.cameraFarClip = 1000.0f;
	} else if (type == "MonitorRenderer") {
		component.monitorCameraName = "";
		component.monitorWidth = 512;
		component.monitorHeight = 512;
		component.monitorHideSelf = true;
	}
	entity->components.push_back(std::move(component));
	dirty_ = true;
	return true;
}

bool SceneDocument::RemoveComponent(uint64_t id, const std::string& type) {
	SceneEntity* entity = FindEntity(id);
	if (!entity) {
		return false;
	}
	const auto oldSize = entity->components.size();
	entity->components.erase(
		std::remove_if(
			entity->components.begin(),
			entity->components.end(),
			[&type](const SceneComponent& component) {
				return component.type == type;
			}
		),
		entity->components.end()
	);
	if (entity->components.size() == oldSize) {
		return false;
	}
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
			entity.locked = source.value("locked", false);
			entity.modelPath = source.value("modelPath", std::string{});
			if (source.contains("sprite") && source.at("sprite").is_object()) {
				const json& sprite = source.at("sprite");
				entity.spriteTexturePath = sprite.value(
					"texturePath",
					std::string{}
				);
				if (sprite.contains("size")) {
					entity.spriteSize = JsonToVector(sprite.at("size"), entity.spriteSize);
				}
				if (sprite.contains("anchor")) {
					entity.spriteAnchor = JsonToVector(sprite.at("anchor"), entity.spriteAnchor);
				}
				if (sprite.contains("color")) {
					entity.spriteColor = JsonToVector(sprite.at("color"), entity.spriteColor);
				}
				entity.spriteFlipX = sprite.value("flipX", false);
				entity.spriteFlipY = sprite.value("flipY", false);
			}
			if (source.contains("components")) {
				entity.components = ComponentsFromJson(source.at("components"));
			}
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
				SynchronizeLegacyRendererFields(entity);
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
