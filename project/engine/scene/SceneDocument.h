#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "../math/Transform.h"
#include "../math/Vector2.h"
#include "../math/Vector4.h"

struct SceneEntity {
	uint64_t id = 0;
	uint64_t parentId = 0;
	std::string name;
	bool active = true;
	Transform transform{};
	std::string modelPath;
	std::string spriteTexturePath;
	Vector2 spriteSize = { 100.0f, 100.0f };
	Vector2 spriteAnchor = { 0.5f, 0.5f };
	Vector4 spriteColor = { 1.0f, 1.0f, 1.0f, 1.0f };
	bool spriteFlipX = false;
	bool spriteFlipY = false;
	std::vector<std::string> components;
};

class SceneDocument {
public:
	void Clear(const std::string& sceneName = {});

	bool Load(const std::string& filePath);
	bool Save(const std::string& filePath);

	SceneEntity& CreateEntity(const std::string& name, uint64_t parentId = 0);
	bool RemoveEntity(uint64_t id);
	uint64_t DuplicateEntity(uint64_t id);
	bool SetParent(uint64_t id, uint64_t parentId);
	bool MoveEntity(uint64_t id, int direction);
	bool IsDescendantOf(uint64_t id, uint64_t potentialAncestorId) const;
	SceneEntity* FindEntity(uint64_t id);
	const SceneEntity* FindEntity(uint64_t id) const;
	SceneEntity* FindEntityByName(const std::string& name);
	const SceneEntity* FindEntityByName(const std::string& name) const;

	const std::string& GetSceneName() const { return sceneName_; }
	void SetSceneName(const std::string& sceneName) {
		sceneName_ = sceneName;
		dirty_ = true;
	}
	std::vector<SceneEntity>& GetEntities() { return entities_; }
	const std::vector<SceneEntity>& GetEntities() const { return entities_; }
	bool IsDirty() const { return dirty_; }
	void MarkDirty() { dirty_ = true; }
	void MarkClean() { dirty_ = false; }

private:
	bool LoadInternal(const std::string& filePath);
	void RebuildNextId();
	void ValidateHierarchy();

	std::string sceneName_;
	std::vector<SceneEntity> entities_;
	uint64_t nextId_ = 1;
	bool dirty_ = false;
};
