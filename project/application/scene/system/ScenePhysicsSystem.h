// 役割: Sceneの物理Componentを実行状態へ反映し、物理ワールドを更新する。
#pragma once

#include <vector>

#include "../SceneRuntimeObjectBinding.h"
#include "../../../engine/physics/PhysicsWorld.h"

class Collider;
class Object3d;
class Player;
class SceneDocument;
struct SceneComponent;

// PhysicsWorldを所有するが、Collider・PhysicsBody・Playerは外部から借用する。
// Object同期後にSyncSceneSettingsを呼び、Object破棄前にClearする必要がある。
class ScenePhysicsSystem {
public:
	void ApplyBodyComponent(
		PhysicsBody& body,
		const SceneComponent& component,
		Object3d* object,
		Collider* collider,
		bool resetVelocity
	) const;
	void SyncSceneSettings(
		const SceneDocument& document,
		Player* player,
		const std::vector<SceneRuntimeObjectBinding>& bindings,
		bool editing
	);
	void Step(
		Player* player,
		const std::vector<SceneRuntimeObjectBinding>& bindings,
		float deltaTime,
		bool playing
	);
	void Clear();

private:
	void ApplyPlayerBehavior(
		const SceneDocument& document,
		Player* player
	) const;
	void ApplyPlayerPhysics(
		const SceneDocument& document,
		Player* player,
		const std::vector<SceneRuntimeObjectBinding>& bindings,
		bool editing
	) const;
	void ApplyWaterVolumes(
		const SceneDocument& document,
		Player* player
	) const;
	void RebuildStaticColliders(
		const SceneDocument& document,
		const std::vector<SceneRuntimeObjectBinding>& bindings
	);

	PhysicsWorld physicsWorld_;
	std::vector<Collider*> staticColliders_;
};
