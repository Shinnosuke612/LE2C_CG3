// 役割: HitBox/HurtBoxの重なりをDamageへ変換し、Statへ一度だけ適用する。
#pragma once

#include <cstdint>
#include <unordered_set>
#include <vector>

#include "../SceneRuntimeObjectBinding.h"

class SceneDocument;
class SceneStatSystem;

class SceneCombatSystem {
public:
	void Update(
		SceneDocument& document,
		const std::vector<SceneRuntimeObjectBinding>& bindings,
		SceneStatSystem& statSystem
	);
	void FlushRemovals(SceneDocument& document);
	void Clear();

private:
	std::unordered_set<uint64_t> activeContacts_;
	std::unordered_set<uint64_t> pendingRemovals_;
};
