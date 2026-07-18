// 役割: EventTriggerの条件を評価し、Stat変更・Entity制御・Scene遷移を実行する。
#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

class SceneDocument;
class SceneStatSystem;
class SceneStateMachineSystem;

class SceneEventSystem {
public:
	std::string Update(
		SceneDocument& document,
		SceneStatSystem& statSystem,
		SceneStateMachineSystem& stateMachineSystem,
		float deltaTime
	);
	void Clear();

private:
	struct BindingRuntime {
		float cooldown = 0.0f;
		bool initialized = false;
		bool wasConditionTrue = false;
		bool fired = false;
	};

	std::unordered_map<uint64_t, std::vector<BindingRuntime>> runtimes_;
};
