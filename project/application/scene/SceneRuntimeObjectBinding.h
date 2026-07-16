// 役割: SceneEntityとRuntime側の描画・衝突・物理オブジェクトを接続する。
#pragma once

class Collider;
class Object3d;
struct PhysicsBody;
struct SceneEntity;

struct SceneRuntimeObjectBinding {
	SceneEntity* entity = nullptr;
	Object3d* object = nullptr;
	Collider* collider = nullptr;
	PhysicsBody* body = nullptr;
};
