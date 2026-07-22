// 役割: HitBox/HurtBoxの重なりをDamageへ変換し、Statへ一度だけ適用する。
#pragma once

#include <cstdint>
#include <string>
#include <unordered_set>
#include <vector>

#include "../SceneRuntimeObjectBinding.h"
#include "../../../engine/math/Vector3.h"

class SceneDocument;
class SceneStatSystem;

struct SceneCombatHitEvent {
	uint64_t attackerEntityId = 0;
	uint64_t targetEntityId = 0;
	float damage = 0.0f;
	float poiseDamage = 0.0f;
	float knockback = 0.0f;
	Vector3 knockbackDirection{};
	std::string healthStatId = "hp";
	std::string reactionTag;
};

class SceneCombatSystem {
public:
	void Update(
		SceneDocument& document,
		const std::vector<SceneRuntimeObjectBinding>& bindings,
		SceneStatSystem& statSystem
	);
	void FlushRemovals(SceneDocument& document);
	std::vector<SceneCombatHitEvent> ConsumeHitEvents();
	void Clear();

private:
	std::unordered_set<uint64_t> activeContacts_;
	std::unordered_set<uint64_t> pendingRemovals_;
	std::vector<SceneCombatHitEvent> hitEvents_;
};
