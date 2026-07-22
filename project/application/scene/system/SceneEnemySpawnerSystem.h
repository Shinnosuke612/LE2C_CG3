// 役割: EnemySpawnerのRuntime専用Prefab生成と非アクティブInstance再利用を管理する。
#pragma once

#include <cstdint>
#include <unordered_map>
#include <vector>

#include "../../../engine/scene/SceneDocument.h"

class SceneEnemySpawnerSystem {
public:
	void Update(SceneDocument& document, float deltaTime);
	std::vector<uint64_t> ConsumeResetEntityIds();
	void Clear();

private:
	struct Instance {
		uint64_t rootEntityId = 0;
		// 生成直後のPrefab枝を保持し、再利用時に子HitBoxを含むAuthoring値へ戻す。
		std::unordered_map<uint64_t, SceneEntity> baselineEntities;
	};

	struct Runtime {
		float timer = 0.0f;
		int spawnedInitial = 0;
		uint32_t spawnIndex = 0;
		std::vector<Instance> instances;
	};

	bool RestoreInstance(
		SceneDocument& document,
		const Instance& instance,
		const Vector3& spawnPosition
	);
	Instance CaptureInstance(
		const SceneDocument& document,
		uint64_t rootEntityId
	) const;

	std::unordered_map<uint64_t, Runtime> runtimes_;
	std::vector<uint64_t> resetEntityIds_;
};
