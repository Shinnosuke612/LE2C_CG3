// 役割: EnemyBehaviorの索敵・追跡・攻撃フェーズを更新する。
#pragma once

#include <cstdint>
#include <unordered_map>
#include <vector>

#include "../SceneRuntimeObjectBinding.h"

class SceneDocument;
class ScenePrefabAnimationSystem;
class SceneStatSystem;

class SceneEnemySystem {
public:
	void Update(
		SceneDocument& document,
		const std::vector<SceneRuntimeObjectBinding>& bindings,
		SceneStatSystem& statSystem,
		ScenePrefabAnimationSystem& prefabAnimationSystem,
		float deltaTime
	);
	void Clear();

private:
	enum class AttackPhase {
		None,
		Windup,
		Active,
		Recovery,
		Dead
	};

	struct EnemyRuntime {
		AttackPhase phase = AttackPhase::None;
		float phaseTimer = 0.0f;
		float cooldown = 0.0f;
		bool hasTarget = false;
		bool initialized = false;
	};

	std::unordered_map<uint64_t, EnemyRuntime> runtimes_;
};
