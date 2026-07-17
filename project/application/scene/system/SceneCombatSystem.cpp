// 役割: 物理押し戻しと分離されたTrigger衝突から戦闘ダメージを解決する。
#include "SceneCombatSystem.h"

#include "SceneStatSystem.h"
#include "../../../engine/collision/Collider.h"
#include "../../../engine/scene/SceneDocument.h"
#include "../../../engine/scene/SceneEntityQuery.h"

#include <algorithm>
#include <string>

namespace {
	struct ComponentOwner {
		SceneEntity* entity = nullptr;
		const SceneComponent* component = nullptr;
	};

	ComponentOwner FindInAncestors(
		SceneDocument& document,
		SceneEntity& start,
		const char* componentType
	) {
		SceneEntity* current = &start;
		while (current) {
			if (const SceneComponent* component =
				SceneEntityQuery::FindEnabledComponent(*current, componentType)) {
				return { current, component };
			}
			current = current->parentId != 0
				? document.FindEntity(current->parentId)
				: nullptr;
		}
		return {};
	}

	SceneEntity* ResolveEntity(
		SceneDocument& document,
		uint64_t id,
		const std::string& name
	) {
		if (id != 0) {
			if (SceneEntity* entity = document.FindEntity(id)) {
				return entity;
			}
		}
		return name.empty() ? nullptr : document.FindEntityByName(name);
	}

	std::string ResolveFaction(SceneDocument& document, SceneEntity& entity) {
		const ComponentOwner faction = FindInAncestors(document, entity, "Faction");
		return faction.component ? faction.component->factionName : std::string{};
	}

	uint64_t MakeContactKey(uint64_t hitBoxId, uint64_t hurtBoxId) {
		uint64_t value = hitBoxId + 0x9E3779B97F4A7C15ull;
		value ^= hurtBoxId + 0x9E3779B97F4A7C15ull + (value << 6) + (value >> 2);
		return value;
	}
}

void SceneCombatSystem::Update(
	SceneDocument& document,
	const std::vector<SceneRuntimeObjectBinding>& bindings,
	SceneStatSystem& statSystem
) {
	struct CombatBinding {
		SceneEntity* entity = nullptr;
		Collider* collider = nullptr;
		const SceneComponent* component = nullptr;
	};
	std::vector<CombatBinding> hitBoxes;
	std::vector<CombatBinding> hurtBoxes;
	for (const SceneRuntimeObjectBinding& binding : bindings) {
		if (
			!binding.entity ||
			!binding.collider ||
			!binding.collider->IsTrigger() ||
			!SceneEntityQuery::IsEntityActiveInHierarchy(document, *binding.entity)
		) {
			continue;
		}
		if (const SceneComponent* hitBox =
			SceneEntityQuery::FindEnabledComponent(*binding.entity, "HitBox")) {
			hitBoxes.push_back({ binding.entity, binding.collider, hitBox });
		}
		if (const SceneComponent* hurtBox =
			SceneEntityQuery::FindEnabledComponent(*binding.entity, "HurtBox")) {
			hurtBoxes.push_back({ binding.entity, binding.collider, hurtBox });
		}
	}

	std::unordered_set<uint64_t> currentContacts;
	std::unordered_set<uint64_t> removeProjectiles;
	for (const CombatBinding& hit : hitBoxes) {
		SceneEntity* explicitOwner = ResolveEntity(
			document,
			hit.component->hitBoxOwnerEntityId,
			hit.component->hitBoxOwnerEntityName
		);
		SceneEntity* hitOwner = explicitOwner;
		if (!hitOwner) {
			const ComponentOwner factionOwner =
				FindInAncestors(document, *hit.entity, "Faction");
			hitOwner = factionOwner.entity ? factionOwner.entity : hit.entity;
		}
		const std::string hitFaction = ResolveFaction(document, *hitOwner);

		for (const CombatBinding& hurt : hurtBoxes) {
			if (hit.entity == hurt.entity || hitOwner == hurt.entity) {
				continue;
			}
			SceneEntity* statsEntity = ResolveEntity(
				document,
				hurt.component->hurtBoxStatsEntityId,
				hurt.component->hurtBoxStatsEntityName
			);
			if (!statsEntity) {
				statsEntity = FindInAncestors(
					document,
					*hurt.entity,
					"StatSet"
				).entity;
			}
			if (!statsEntity || statsEntity == hitOwner) {
				continue;
			}
			if (hit.component->hitBoxIgnoreSameFaction) {
				const std::string hurtFaction = ResolveFaction(
					document,
					*statsEntity
				);
				if (
					!hitFaction.empty() &&
					!hurtFaction.empty() &&
					hitFaction == hurtFaction
				) {
					continue;
				}
			}
			if (!hit.collider->Intersects(*hurt.collider)) {
				continue;
			}

			const uint64_t contactKey = MakeContactKey(
				hit.entity->id,
				hurt.entity->id
			);
			currentContacts.insert(contactKey);
			if (activeContacts_.contains(contactKey)) {
				continue;
			}
			const float damage = (std::max)(
				hit.component->hitBoxDamage *
					hurt.component->hurtBoxDamageMultiplier,
				0.0f
			);
			statSystem.Modify(
				statsEntity->id,
				hurt.component->hurtBoxHealthStatId.empty()
					? hit.component->hitBoxDamageStatId
					: hurt.component->hurtBoxHealthStatId,
				"Subtract",
				damage
			);
			if (hit.component->hitBoxPoiseDamage > 0.0f) {
				statSystem.Modify(
					statsEntity->id,
					hit.component->hitBoxPoiseStatId,
					"Subtract",
					hit.component->hitBoxPoiseDamage
				);
			}
			const ComponentOwner projectile = FindInAncestors(
				document,
				*hit.entity,
				"Projectile"
			);
			if (
				projectile.entity &&
				projectile.component->projectileDestroyOnHit
			) {
				projectile.entity->active = false;
				removeProjectiles.insert(projectile.entity->id);
				break;
			}
		}
	}
	activeContacts_ = std::move(currentContacts);

	for (uint64_t entityId : removeProjectiles) {
		pendingRemovals_.insert(entityId);
	}
}

void SceneCombatSystem::FlushRemovals(SceneDocument& document) {
	for (uint64_t entityId : pendingRemovals_) {
		const SceneEntity* entity = document.FindEntity(entityId);
		const bool runtimeOnly = entity && entity->runtimeOnly;
		const bool wasDirty = document.IsDirty();
		document.RemoveEntity(entityId);
		if (runtimeOnly && !wasDirty) {
			document.MarkClean();
		}
	}
	pendingRemovals_.clear();
}

void SceneCombatSystem::Clear() {
	activeContacts_.clear();
	pendingRemovals_.clear();
}
