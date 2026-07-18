// 役割: 汎用Trigger/Actionをゲーム進行要求へ変換する。
#include "SceneEventSystem.h"

#include "SceneStatSystem.h"
#include "SceneStateMachineSystem.h"
#include "../../../engine/math/Math.h"
#include "../../../engine/scene/SceneDocument.h"
#include "../../../engine/scene/SceneEntityQuery.h"
#include "../../../engine/scene/SceneTransformResolver.h"

#include <algorithm>
#include <cmath>
#include <unordered_set>

namespace {
	SceneEntity* ResolveEntity(
		SceneDocument& document,
		uint64_t id,
		const std::string& name,
		uint64_t fallbackId
	) {
		if (id != 0) {
			if (SceneEntity* entity = document.FindEntity(id)) {
				return entity;
			}
		}
		if (!name.empty()) {
			if (SceneEntity* entity = document.FindEntityByName(name)) {
				return entity;
			}
		}
		return document.FindEntity(fallbackId);
	}

	bool CompareStat(float value, const SceneEventBinding& binding) {
		if (binding.statComparison == "GreaterOrEqual") {
			return value >= binding.statValue;
		}
		if (binding.statComparison == "Greater") {
			return value > binding.statValue;
		}
		if (binding.statComparison == "Less") {
			return value < binding.statValue;
		}
		if (binding.statComparison == "Equal") {
			return std::abs(value - binding.statValue) <= 0.0001f;
		}
		return value <= binding.statValue;
	}
}

std::string SceneEventSystem::Update(
	SceneDocument& document,
	SceneStatSystem& statSystem,
	SceneStateMachineSystem& stateMachineSystem,
	float deltaTime
) {
	struct QueuedAction {
		uint64_t ownerEntityId = 0;
		SceneEventAction action{};
	};
	std::vector<QueuedAction> queuedActions;
	std::unordered_set<uint64_t> requiredEntities;

	for (const SceneEntity& entity : document.GetEntities()) {
		if (!SceneEntityQuery::IsEntityActiveInHierarchy(document, entity)) {
			continue;
		}
		const SceneComponent* eventComponent =
			SceneEntityQuery::FindEnabledComponent(entity, "EventTrigger");
		if (!eventComponent) {
			continue;
		}
		requiredEntities.insert(entity.id);
		auto& states = runtimes_[entity.id];
		states.resize(eventComponent->eventBindings.size());
		for (size_t index = 0;
			index < eventComponent->eventBindings.size();
			++index) {
			const SceneEventBinding& binding =
				eventComponent->eventBindings[index];
			BindingRuntime& state = states[index];
			state.cooldown = (std::max)(state.cooldown - deltaTime, 0.0f);
			SceneEntity* target = ResolveEntity(
				document,
				binding.targetEntityId,
				binding.targetEntityName,
				entity.id
			);
			bool condition = false;
			bool shouldFire = false;
			if (binding.triggerType == "OnStart") {
				shouldFire = !state.initialized;
				condition = true;
			} else if (binding.triggerType == "OnInterval") {
				condition = true;
				shouldFire = !state.initialized || state.cooldown <= 0.0f;
			} else if (binding.triggerType == "OnStatReachedMin") {
				condition = target && statSystem.IsAtMin(target->id, binding.statId);
				shouldFire = condition && !state.wasConditionTrue;
			} else if (binding.triggerType == "OnStatCompare") {
				float value = 0.0f;
				condition = target &&
					statSystem.TryGet(target->id, binding.statId, value) &&
					CompareStat(value, binding);
				shouldFire = condition && !state.wasConditionTrue;
			} else if (binding.triggerType == "OnPositionReached") {
				if (target) {
					const Transform transform =
						SceneTransformResolver::ResolveScene3DTransform(
							document,
							*target
						);
					condition = Math::Length(Math::Subtract(
						transform.translate,
						binding.targetPosition
					)) <= (std::max)(binding.radius, 0.0f);
				}
				shouldFire = condition && !state.wasConditionTrue;
			}

			if (
				shouldFire &&
				state.cooldown <= 0.0f &&
				(!binding.triggerOnce || !state.fired)
			) {
				for (const SceneEventAction& action : binding.actions) {
					queuedActions.push_back({ entity.id, action });
				}
				state.fired = true;
				state.cooldown = (std::max)(binding.cooldown, 0.0f);
			}
			state.wasConditionTrue = condition;
			state.initialized = true;
		}
	}

	for (auto iterator = runtimes_.begin(); iterator != runtimes_.end();) {
		if (!requiredEntities.contains(iterator->first)) {
			iterator = runtimes_.erase(iterator);
		} else {
			++iterator;
		}
	}

	std::string sceneTransition;
	for (const QueuedAction& queued : queuedActions) {
		const SceneEventAction& action = queued.action;
		SceneEntity* target = ResolveEntity(
			document,
			action.targetEntityId,
			action.targetEntityName,
			queued.ownerEntityId
		);
		if (action.type == "ModifyStat") {
			if (target) {
				statSystem.Modify(
					target->id,
					action.statId,
					action.statOperation,
					action.value
				);
			}
		} else if (action.type == "SetEntityActive") {
			if (target) {
				target->active = action.active;
			}
		} else if (action.type == "InstantiatePrefab") {
			const uint64_t targetId = target ? target->id : 0;
			Transform spawnTransform{};
			const bool hasSpawnTransform =
				target && action.prefabUseTargetTransform;
			if (hasSpawnTransform) {
				spawnTransform = SceneTransformResolver::ResolveScene3DTransform(
					document,
					*target
				);
			}
			const uint64_t instanceId = document.InstantiatePrefab(
				action.prefabPath,
				action.prefabParentToTarget ? targetId : 0,
				true
			);
			if (
				instanceId != 0 &&
				hasSpawnTransform &&
				!action.prefabParentToTarget
			) {
				if (SceneEntity* instance = document.FindEntity(instanceId)) {
					instance->transform.translate = spawnTransform.translate;
					instance->transform.rotate = spawnTransform.quaternionRotate;
				}
			}
		} else if (action.type == "ChangeState") {
			if (target) {
				stateMachineSystem.RequestState(target->id, action.stateName);
			}
		} else if (
			action.type == "SceneTransition" &&
			sceneTransition.empty()
		) {
			sceneTransition = action.sceneId;
		}
	}
	return sceneTransition;
}

void SceneEventSystem::Clear() {
	runtimes_.clear();
}
