// 役割: HitReactionとDeathPresentationをCombat判定から分離して評価する。
#include "SceneHitReactionSystem.h"

#include "SceneStateMachineSystem.h"
#include "SceneStatSystem.h"
#include "../SceneRuntimeObjectBinding.h"
#include "../../../engine/physics/PhysicsBody.h"
#include "../../../engine/scene/SceneDocument.h"
#include "../../../engine/scene/SceneEntityQuery.h"

#include <algorithm>

void SceneHitReactionSystem::Update(
	SceneDocument& document,
	const std::vector<SceneRuntimeObjectBinding>& bindings,
	SceneStatSystem& statSystem,
	SceneStateMachineSystem& stateMachineSystem,
	const std::vector<SceneCombatHitEvent>& events,
	float deltaTime
) {
	// Bodyへの書込みは次FrameのPhysics直前へ分離した。
	static_cast<void>(bindings);
	for (auto iterator = pendingDeactivateTimes_.begin();
		iterator != pendingDeactivateTimes_.end();) {
		iterator->second -= deltaTime;
		if (iterator->second > 0.0f) {
			++iterator;
			continue;
		}
		if (SceneEntity* entity = document.FindEntity(iterator->first)) {
			entity->active = false;
		}
		iterator = pendingDeactivateTimes_.erase(iterator);
	}

	for (const SceneCombatHitEvent& event : events) {
		SceneEntity* target = document.FindEntity(event.targetEntityId);
		if (!target || !SceneEntityQuery::IsEntityActiveInHierarchy(document, *target)) {
			continue;
		}
		const SceneComponent* reaction =
			SceneEntityQuery::FindEnabledComponent(*target, "HitReaction");
		const SceneComponent* death =
			SceneEntityQuery::FindEnabledComponent(*target, "DeathPresentation");
		const bool hasStateMachine =
			SceneEntityQuery::FindEnabledComponent(*target, "StateMachine") != nullptr;
		if (reaction && event.knockback > 0.0f) {
			const float amount = event.knockback *
				reaction->hitReactionKnockbackMultiplier;
			const float duration = (std::max)(
				reaction->hitReactionStateDuration,
				0.0f
			);
			if (duration > 0.0f) {
				// CombatはPhysics後に評価される。ここでは次FrameのPhysics直前に
				// 適用する速度を予約し、EnemyBehaviorの速度書込みで消えないようにする。
				knockbackRuntimes_[target->id] = {
					{
						event.knockbackDirection.x * amount,
						0.0f,
						event.knockbackDirection.z * amount
					},
					duration,
					duration
				};
			}
		}
		if (death && statSystem.IsAtMin(target->id, event.healthStatId)) {
			// StateMachine未設定のPrefabへ要求を残さず、演出なしの
			// Pool用敵でもDeactivateだけは確実に完了させる。
			if (hasStateMachine && !death->deathPresentationStateName.empty()) {
				stateMachineSystem.RequestState(
					target->id,
					death->deathPresentationStateName
				);
			}
			pendingDeactivateTimes_[target->id] =
				(std::max)(death->deathPresentationDeactivateDelay, 0.0f);
		} else if (reaction && event.poiseDamage >=
			reaction->hitReactionMinimumPoiseDamage) {
			if (hasStateMachine && !reaction->hitReactionStateName.empty()) {
				stateMachineSystem.RequestState(
					target->id,
					reaction->hitReactionStateName
				);
			}
		}
	}
}

void SceneHitReactionSystem::ApplyMotionOverrides(
	const SceneDocument& document,
	const std::vector<SceneRuntimeObjectBinding>& bindings,
	float deltaTime
) {
	for (auto iterator = knockbackRuntimes_.begin();
		iterator != knockbackRuntimes_.end();) {
		const SceneEntity* entity = document.FindEntity(iterator->first);
		if (!entity || !SceneEntityQuery::IsEntityActiveInHierarchy(document, *entity)) {
			iterator = knockbackRuntimes_.erase(iterator);
			continue;
		}

		PhysicsBody* body = nullptr;
		for (const SceneRuntimeObjectBinding& binding : bindings) {
			if (binding.entity == entity) {
				body = binding.body;
				break;
			}
		}
		if (!body) {
			iterator = knockbackRuntimes_.erase(iterator);
			continue;
		}

		const float duration = (std::max)(iterator->second.duration, 0.0001f);
		const float rate = std::clamp(
			iterator->second.remainingTime / duration,
			0.0f,
			1.0f
		);
		// この時点ではEnemy/Agent/StateMachineの速度決定が完了している。
		// 加算では追跡速度に打ち消されるため、被弾中のXZ移動はReactionが所有する。
		body->velocity.x = iterator->second.velocity.x * rate;
		body->velocity.z = iterator->second.velocity.z * rate;

		iterator->second.remainingTime -= (std::max)(deltaTime, 0.0f);
		if (iterator->second.remainingTime <= 0.0f) {
			iterator = knockbackRuntimes_.erase(iterator);
		} else {
			++iterator;
		}
	}
}

bool SceneHitReactionSystem::IsKnockbackActive(uint64_t entityId) const {
	return knockbackRuntimes_.contains(entityId);
}

void SceneHitReactionSystem::ResetEntity(uint64_t entityId) {
	pendingDeactivateTimes_.erase(entityId);
	knockbackRuntimes_.erase(entityId);
}

void SceneHitReactionSystem::Clear() {
	pendingDeactivateTimes_.clear();
	knockbackRuntimes_.clear();
}
