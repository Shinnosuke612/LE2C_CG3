// 役割: 物理シミュレーションに必要な速度、質量、拘束、同期処理を保持する。
#pragma once

#include <functional>

#include "../math/Transform.h"
#include "../math/Vector3.h"

class Collider;

enum class PhysicsBodyType {
	Static,
	Dynamic,
	Kinematic
};

struct PhysicsBody {
	PhysicsBodyType type = PhysicsBodyType::Static;
	Transform* transform = nullptr;
	Collider* collider = nullptr;

	Vector3 velocity{};
	float mass = 1.0f;
	bool useGravity = true;
	float gravityScale = 1.0f;
	float drag = 0.0f;
	float restitution = 0.0f;
	float friction = 0.0f;
	float maxFallSpeed = 100.0f;

	bool freezePositionX = false;
	bool freezePositionY = false;
	bool freezePositionZ = false;

	bool isGrounded = false;
	std::function<void()> syncTransform;

	void SynchronizeTransform() const {
		if (syncTransform) {
			syncTransform();
		}
	}
};
