// 役割: SceneDocumentの物理設定とRuntimeオブジェクトの運動を同期する。
#include "ScenePhysicsSystem.h"

#include "../../../engine/3d/Object3d.h"
#include "../../../engine/scene/SceneDocument.h"
#include "../../../engine/scene/SceneEntityQuery.h"
#include "../../../engine/scene/SceneTransformResolver.h"
#include "../../player/Player.h"

#include <algorithm>
#include <cstdint>
#include <cmath>
#include <string>

namespace {
	PhysicsBodyType ToPhysicsBodyType(const std::string& bodyType) {
		if (bodyType == "Dynamic") {
			return PhysicsBodyType::Dynamic;
		}
		if (bodyType == "Kinematic") {
			return PhysicsBodyType::Kinematic;
		}
		return PhysicsBodyType::Static;
	}

	bool IsPointInsideWaterVolume(
		const SceneDocument& document,
		const SceneEntity& entity,
		const SceneComponent& waterVolume,
		const Vector3& point
	) {
		const Transform transform =
			SceneTransformResolver::ResolveScene3DTransform(document, entity);
		const Vector3 center = {
			transform.translate.x + waterVolume.waterOffset.x,
			transform.translate.y + waterVolume.waterOffset.y,
			transform.translate.z + waterVolume.waterOffset.z
		};
		const Vector3 halfSize = {
			(std::max)(waterVolume.waterHalfSize.x, 0.001f),
			(std::max)(waterVolume.waterHalfSize.y, 0.001f),
			(std::max)(waterVolume.waterHalfSize.z, 0.001f)
		};

		return
			std::abs(point.x - center.x) <= halfSize.x &&
			std::abs(point.y - center.y) <= halfSize.y &&
			std::abs(point.z - center.z) <= halfSize.z;
	}

	const SceneRuntimeObjectBinding* FindBinding(
		const std::vector<SceneRuntimeObjectBinding>& bindings,
		uint64_t entityId
	) {
		for (const SceneRuntimeObjectBinding& binding : bindings) {
			if (binding.entity && binding.entity->id == entityId) {
				return &binding;
			}
		}
		return nullptr;
	}
}

void ScenePhysicsSystem::ApplyBodyComponent(
	PhysicsBody& body,
	const SceneComponent& component,
	Object3d* object,
	Collider* collider,
	bool resetVelocity
) const {
	const Vector3 runtimeVelocity = body.velocity;
	body.type = ToPhysicsBodyType(component.physicsBodyType);
	body.transform = object ? &object->GetTransform() : nullptr;
	body.syncTransform = [object]() {
		if (object) {
			object->Update();
		}
	};
	body.collider = collider;
	body.mass = (std::max)(component.physicsMass, 0.001f);
	body.useGravity = component.physicsUseGravity;
	body.gravityScale = component.physicsGravityScale;
	body.drag = (std::max)(component.physicsDrag, 0.0f);
	body.restitution = std::clamp(component.physicsRestitution, 0.0f, 1.0f);
	body.friction = std::clamp(component.physicsFriction, 0.0f, 1.0f);
	body.maxFallSpeed = (std::max)(component.physicsMaxFallSpeed, 0.0f);
	body.freezePositionX = component.physicsFreezePositionX;
	body.freezePositionY = component.physicsFreezePositionY;
	body.freezePositionZ = component.physicsFreezePositionZ;
	body.velocity = resetVelocity ? component.physicsVelocity : runtimeVelocity;
}

void ScenePhysicsSystem::SyncSceneSettings(
	const SceneDocument& document,
	Player* player,
	const std::vector<SceneRuntimeObjectBinding>& bindings,
	bool editing
) {
	ApplyPlayerBehavior(document, player);
	ApplyPlayerPhysics(document, player, bindings, editing);
	ApplyWaterVolumes(document, player);
	RebuildStaticColliders(document, bindings);
}

void ScenePhysicsSystem::Step(
	Player* player,
	const std::vector<SceneRuntimeObjectBinding>& bindings,
	float deltaTime,
	bool playing
) {
	if (!playing) {
		return;
	}

	// BodyとColliderは非所有なので、現在のbindingsからWorld登録を毎Step再構築する。
	physicsWorld_.Clear();
	for (Collider* collider : staticColliders_) {
		physicsWorld_.AddStaticCollider(collider);
	}
	if (player && player->GetObject()) {
		physicsWorld_.AddBody(&player->GetPhysicsBody());
	}
	for (const SceneRuntimeObjectBinding& binding : bindings) {
		if (
			!binding.entity ||
			!binding.object ||
			!binding.body ||
			SceneEntityQuery::HasComponent(*binding.entity, "PlayerBehavior")
		) {
			continue;
		}
		physicsWorld_.AddBody(binding.body);
	}

	physicsWorld_.Step(deltaTime);

	for (const SceneRuntimeObjectBinding& binding : bindings) {
		if (
			!binding.entity ||
			!binding.object ||
			!binding.body ||
			SceneEntityQuery::HasComponent(*binding.entity, "PlayerBehavior")
		) {
			continue;
		}
		binding.object->Update();
		const Transform& runtimeTransform = binding.object->GetTransform();
		binding.entity->transform.scale = runtimeTransform.scale;
		binding.entity->transform.rotate = runtimeTransform.useQuaternionRotation
			? runtimeTransform.quaternionRotate
			: MakeQuaternionFromEuler(runtimeTransform.rotate);
		binding.entity->transform.translate = runtimeTransform.translate;
	}
}

void ScenePhysicsSystem::ResetBodies(
	const std::vector<SceneRuntimeObjectBinding>& bindings,
	const std::vector<uint64_t>& entityIds
) const {
	for (const SceneRuntimeObjectBinding& binding : bindings) {
		if (!binding.entity || !binding.body ||
			std::find(entityIds.begin(), entityIds.end(), binding.entity->id) ==
				entityIds.end()) {
			continue;
		}
		const SceneComponent* component =
			SceneEntityQuery::FindEnabledComponent(*binding.entity, "PhysicsBody");
		if (component) {
			binding.body->velocity = component->physicsVelocity;
		}
	}
}

void ScenePhysicsSystem::Clear() {
	physicsWorld_.Clear();
	staticColliders_.clear();
}

void ScenePhysicsSystem::ApplyPlayerBehavior(
	const SceneDocument& document,
	Player* player
) const {
	if (!player) {
		return;
	}
	const SceneEntity* playerEntity = document.FindEntityByName("Player");
	const SceneComponent* behavior = playerEntity
		? SceneEntityQuery::FindEnabledComponent(*playerEntity, "PlayerBehavior")
		: nullptr;
	if (!behavior) {
		return;
	}

	player->SetBehaviorSettings(
		behavior->playerMoveSpeed,
		behavior->playerJumpVelocity,
		behavior->playerTurnResponsiveness,
		behavior->playerDashMultiplier,
		behavior->playerCameraRelativeMove,
		behavior->playerAllowJump
	);
}

void ScenePhysicsSystem::ApplyPlayerPhysics(
	const SceneDocument& document,
	Player* player,
	const std::vector<SceneRuntimeObjectBinding>& bindings,
	bool editing
) const {
	if (!player || !player->GetObject()) {
		return;
	}
	const SceneEntity* playerEntity = document.FindEntityByName("Player");
	if (
		!playerEntity ||
		!SceneEntityQuery::HasComponent(*playerEntity, "PlayerBehavior")
	) {
		return;
	}

	const SceneRuntimeObjectBinding* binding =
		FindBinding(bindings, playerEntity->id);
	player->SetCollider(binding ? binding->collider : nullptr);
	const SceneComponent* component =
		SceneEntityQuery::FindEnabledComponent(*playerEntity, "PhysicsBody");
	if (!component) {
		return;
	}

	PhysicsBody& body = player->GetPhysicsBody();
	ApplyBodyComponent(
		body,
		*component,
		player->GetObject(),
		player->GetCollider(),
		editing
	);
	if (body.type == PhysicsBodyType::Static) {
		body.type = PhysicsBodyType::Dynamic;
	}
}

void ScenePhysicsSystem::ApplyWaterVolumes(
	const SceneDocument& document,
	Player* player
) const {
	if (!player || !player->GetObject()) {
		return;
	}
	player->SetWaterState(false, 1.0f, 0.0f);

	const Vector3 playerPosition =
		player->GetObject()->GetTransform().translate;
	for (const SceneEntity& entity : document.GetEntities()) {
		if (!SceneEntityQuery::IsEntityActiveInHierarchy(document, entity)) {
			continue;
		}
		const SceneComponent* waterVolume =
			SceneEntityQuery::FindEnabledComponent(entity, "WaterVolume");
		if (
			!waterVolume ||
			!IsPointInsideWaterVolume(
				document,
				entity,
				*waterVolume,
				playerPosition
			)
		) {
			continue;
		}

		player->SetWaterState(
			true,
			waterVolume->waterMoveSpeedMultiplier,
			waterVolume->waterSwimUpSpeed
		);
		PhysicsBody& body = player->GetPhysicsBody();
		body.gravityScale = waterVolume->waterGravityScale;
		body.drag = (std::max)(body.drag, waterVolume->waterDrag);
		body.maxFallSpeed =
			(std::max)(waterVolume->waterMaxFallSpeed, 0.0f);
		return;
	}
}

void ScenePhysicsSystem::RebuildStaticColliders(
	const SceneDocument& document,
	const std::vector<SceneRuntimeObjectBinding>& bindings
) {
	staticColliders_.clear();
	for (const SceneRuntimeObjectBinding& binding : bindings) {
		if (
			!binding.entity ||
			!binding.collider ||
			binding.body ||
			SceneEntityQuery::HasComponent(
				*binding.entity,
				"PlayerBehavior"
			) ||
			!SceneEntityQuery::IsEntityActiveInHierarchy(
				document,
				*binding.entity
			)
		) {
			continue;
		}
		staticColliders_.push_back(binding.collider);
	}
}
