// 役割: Scene保存対象外の敵Prefabを必要数だけSpawnし、Deactivate済みInstanceを優先再利用する。
#include "SceneEnemySpawnerSystem.h"

#include "../../../engine/scene/SceneDocument.h"
#include "../../../engine/scene/SceneEntityQuery.h"

#include <algorithm>
#include <cmath>
#include <utility>
#include <utility>

void SceneEnemySpawnerSystem::Update(SceneDocument& document, float deltaTime) {
	std::vector<uint64_t> spawnerIds;
	for (const SceneEntity& entity : document.GetEntities()) {
		const SceneComponent* spawner = SceneEntityQuery::FindEnabledComponent(
			entity, "EnemySpawner"
		);
		if (!spawner || !spawner->enemySpawnerAutoStart ||
			spawner->enemySpawnerPrefabPath.empty()) {
			continue;
		}
		spawnerIds.push_back(entity.id);
	}

	for (uint64_t spawnerId : spawnerIds) {
		const SceneEntity* spawnerEntity = document.FindEntity(spawnerId);
		const SceneComponent* spawner = spawnerEntity
			? SceneEntityQuery::FindEnabledComponent(*spawnerEntity, "EnemySpawner")
			: nullptr;
		if (!spawner || !spawner->enemySpawnerAutoStart ||
			spawner->enemySpawnerPrefabPath.empty()) {
			continue;
		}
		const std::string prefabPath = spawner->enemySpawnerPrefabPath;
		const int initial = (std::max)(spawner->enemySpawnerInitialCount, 0);
		const int maxAlive = (std::max)(spawner->enemySpawnerMaxAlive, initial);
		const float interval = (std::max)(spawner->enemySpawnerInterval, 0.0f);
		const float radius = (std::max)(spawner->enemySpawnerRadius, 0.0f);
		const Vector3 origin = spawnerEntity->transform.translate;
		Runtime& runtime = runtimes_[spawnerId];
		runtime.instances.erase(
			std::remove_if(
				runtime.instances.begin(),
				runtime.instances.end(),
				[&document](const Instance& instance) {
					return !document.FindEntity(instance.rootEntityId);
				}
			),
			runtime.instances.end()
		);
		int alive = 0;
		Instance* reusable = nullptr;
		for (Instance& instance : runtime.instances) {
			SceneEntity* root = document.FindEntity(instance.rootEntityId);
			if (!root) {
				continue;
			}
			if (root->active) {
				++alive;
			} else if (!reusable) {
				reusable = &instance;
			}
		}
		const bool needsInitial = runtime.spawnedInitial < initial;
		runtime.timer = (std::max)(runtime.timer - deltaTime, 0.0f);
		if (alive >= maxAlive || (!needsInitial && runtime.timer > 0.0f)) { continue; }
		const float angle = static_cast<float>(runtime.spawnIndex) * 2.39996323f;
		const Vector3 spawnPosition = {
			origin.x + std::cos(angle) * radius,
			origin.y,
			origin.z + std::sin(angle) * radius
		};
		if (reusable) {
			if (!RestoreInstance(document, *reusable, spawnPosition)) {
				continue;
			}
		} else {
			const uint64_t id = document.InstantiatePrefab(
				prefabPath, 0, true
			);
			if (SceneEntity* instance = document.FindEntity(id)) {
				runtime.instances.push_back(CaptureInstance(document, id));
				instance->transform.translate = spawnPosition;
			} else {
				continue;
			}
		}
		if (needsInitial) { ++runtime.spawnedInitial; }
		++runtime.spawnIndex;
		runtime.timer = interval;
	}
}

std::vector<uint64_t> SceneEnemySpawnerSystem::ConsumeResetEntityIds() {
	return std::move(resetEntityIds_);
}

void SceneEnemySpawnerSystem::Clear() {
	runtimes_.clear();
	resetEntityIds_.clear();
}

SceneEnemySpawnerSystem::Instance SceneEnemySpawnerSystem::CaptureInstance(
	const SceneDocument& document,
	uint64_t rootEntityId
) const {
	Instance result{};
	result.rootEntityId = rootEntityId;
	for (const SceneEntity& entity : document.GetEntities()) {
		if (entity.id == rootEntityId ||
			document.IsDescendantOf(entity.id, rootEntityId)) {
			result.baselineEntities.emplace(entity.id, entity);
		}
	}
	return result;
}

bool SceneEnemySpawnerSystem::RestoreInstance(
	SceneDocument& document,
	const Instance& instance,
	const Vector3& spawnPosition
) {
	SceneEntity* root = document.FindEntity(instance.rootEntityId);
	if (!root || instance.baselineEntities.empty()) {
		return false;
	}
	for (const auto& entry : instance.baselineEntities) {
		if (!document.FindEntity(entry.first)) {
			return false;
		}
	}
	for (const auto& [entityId, baseline] : instance.baselineEntities) {
		SceneEntity* entity = document.FindEntity(entityId);
		*entity = baseline;
		resetEntityIds_.push_back(entityId);
	}
	root = document.FindEntity(instance.rootEntityId);
	if (!root) {
		return false;
	}
	root->active = true;
	root->transform.translate = spawnPosition;
	return true;
}
