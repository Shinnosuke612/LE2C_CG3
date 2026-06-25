#include "PhysicsWorld.h"

#include "../collision/OBBCollider.h"
#include "../math/Math.h"

#include <algorithm>
#include <cmath>

namespace {
	constexpr float kBounceVelocityThreshold = 0.01f;
}

void PhysicsWorld::Clear() {
	bodies_.clear();
	staticColliders_.clear();
}

void PhysicsWorld::AddBody(PhysicsBody* body) {
	if (!body) {
		return;
	}
	bodies_.push_back(body);
}

void PhysicsWorld::AddStaticCollider(OBBCollider* collider) {
	if (!collider) {
		return;
	}
	staticColliders_.push_back(collider);
}

void PhysicsWorld::Step(float deltaTime) {
	if (deltaTime <= 0.0f) {
		return;
	}
	deltaTime = (std::min)(deltaTime, 1.0f / 15.0f);

	for (PhysicsBody* body : bodies_) {
		if (
			!body ||
			body->type != PhysicsBodyType::Dynamic ||
			!body->transform
		) {
			continue;
		}

		if (body->useGravity) {
			body->velocity = Math::Add(
				body->velocity,
				Math::Multiply(gravity_, body->gravityScale * deltaTime)
			);
		}
		body->isGrounded = false;

		const float dragFactor = std::clamp(
			1.0f - body->drag * deltaTime,
			0.0f,
			1.0f
		);
		body->velocity = Math::Multiply(body->velocity, dragFactor);
		if (body->maxFallSpeed > 0.0f) {
			body->velocity.y = (std::max)(
				body->velocity.y,
				-body->maxFallSpeed
			);
		}

		Vector3 delta = Math::Multiply(body->velocity, deltaTime);
		if (body->freezePositionX) {
			delta.x = 0.0f;
			body->velocity.x = 0.0f;
		}
		if (body->freezePositionY) {
			delta.y = 0.0f;
			body->velocity.y = 0.0f;
		}
		if (body->freezePositionZ) {
			delta.z = 0.0f;
			body->velocity.z = 0.0f;
		}

		IntegrateAxis(*body, delta.x, 0);
		const bool collidedY = IntegrateAxis(*body, delta.y, 1);
		if (collidedY && delta.y < 0.0f) {
			body->isGrounded = true;
			const float frictionFactor = std::clamp(
				1.0f - body->friction,
				0.0f,
				1.0f
			);
			body->velocity.x *= frictionFactor;
			body->velocity.z *= frictionFactor;
		}
		IntegrateAxis(*body, delta.z, 2);
	}
}

bool PhysicsWorld::CollidesWithStatic(const PhysicsBody& body) const {
	if (!body.obbCollider) {
		return false;
	}

	for (const PhysicsBody* other : bodies_) {
		if (
			!other ||
			other == &body ||
			!other->obbCollider ||
			other->type == PhysicsBodyType::Dynamic
		) {
			continue;
		}
		if (body.obbCollider->Intersects(*other->obbCollider)) {
			return true;
		}
	}

	for (const OBBCollider* collider : staticColliders_) {
		if (collider && body.obbCollider->Intersects(*collider)) {
			return true;
		}
	}

	return false;
}

bool PhysicsWorld::IntegrateAxis(
	PhysicsBody& body,
	float delta,
	uint32_t axis
) const {
	if (std::abs(delta) < 0.000001f || !body.transform) {
		return false;
	}

	float* components[] = {
		&body.transform->translate.x,
		&body.transform->translate.y,
		&body.transform->translate.z
	};
	float* velocityComponents[] = {
		&body.velocity.x,
		&body.velocity.y,
		&body.velocity.z
	};

	*components[axis] += delta;
	if (CollidesWithStatic(body)) {
		*components[axis] -= delta;
		const float velocity = *velocityComponents[axis];
		if (
			body.restitution > 0.0f &&
			std::abs(velocity) > kBounceVelocityThreshold
		) {
			*velocityComponents[axis] =
				-velocity * std::clamp(body.restitution, 0.0f, 1.0f);
		} else {
			*velocityComponents[axis] = 0.0f;
		}
		return true;
	}
	return false;
}
