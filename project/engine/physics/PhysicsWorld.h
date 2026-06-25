#pragma once

#include <vector>

#include "PhysicsBody.h"
#include "../math/Vector3.h"

class OBBCollider;

class PhysicsWorld {
public:
	void Clear();
	void AddBody(PhysicsBody* body);
	void AddStaticCollider(OBBCollider* collider);
	void Step(float deltaTime);

	void SetGravity(const Vector3& gravity) { gravity_ = gravity; }
	const Vector3& GetGravity() const { return gravity_; }

private:
	bool CollidesWithStatic(const PhysicsBody& body) const;
	bool IntegrateAxis(
		PhysicsBody& body,
		float delta,
		uint32_t axis
	) const;

	Vector3 gravity_ = { 0.0f, -9.8f, 0.0f };
	std::vector<PhysicsBody*> bodies_;
	std::vector<OBBCollider*> staticColliders_;
};
