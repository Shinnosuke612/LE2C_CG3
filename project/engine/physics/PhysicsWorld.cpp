#include "PhysicsWorld.h"

#include "../collision/Collider.h"
#include "../collision/OBBCollider.h"
#include "../math/Math.h"

#include <algorithm>
#include <cfloat>
#include <cmath>

namespace {
	constexpr float kBounceVelocityThreshold = 0.01f;
	constexpr float kGroundProbeDistance = 0.05f;
	constexpr uint32_t kAxisSweepIterations = 10;
	constexpr uint32_t kPenetrationResolveIterations = 4;
	constexpr float kPenetrationEpsilon = 0.00001f;
	constexpr float kPenetrationSlop = 0.001f;
	constexpr float kPenetrationSkin = 0.002f;

	float AbsDot(const Vector3& a, const Vector3& b) {
		return std::fabs(Math::Dot(a, b));
	}

	float ProjectOBB(const OBBCollider::OBB& obb, const Vector3& axis) {
		return
			obb.halfSize.x * AbsDot(obb.axis[0], axis) +
			obb.halfSize.y * AbsDot(obb.axis[1], axis) +
			obb.halfSize.z * AbsDot(obb.axis[2], axis);
	}

	bool TestPenetrationAxis(
		const OBBCollider::OBB& moving,
		const OBBCollider::OBB& obstacle,
		Vector3 axis,
		float& bestOverlap,
		Vector3& bestPushAxis
	) {
		if (Math::Length(axis) < kPenetrationEpsilon) {
			return true;
		}

		axis = Math::Normalize(axis);
		const Vector3 centerDelta =
			Math::Subtract(obstacle.center, moving.center);
		const float signedDistance = Math::Dot(centerDelta, axis);
		const float distance = std::fabs(signedDistance);
		const float overlap =
			ProjectOBB(moving, axis) + ProjectOBB(obstacle, axis) - distance;
		if (overlap <= 0.0f) {
			return false;
		}

		if (overlap < bestOverlap) {
			bestOverlap = overlap;
			bestPushAxis = signedDistance < 0.0f
				? axis
				: Vector3{ -axis.x, -axis.y, -axis.z };
		}
		return true;
	}

	bool TryComputePushOut(
		const OBBCollider::OBB& moving,
		const OBBCollider::OBB& obstacle,
		Vector3& pushOut
	) {
		float bestOverlap = FLT_MAX;
		Vector3 bestPushAxis{};

		for (uint32_t i = 0; i < 3; ++i) {
			if (!TestPenetrationAxis(
				moving,
				obstacle,
				moving.axis[i],
				bestOverlap,
				bestPushAxis
			)) {
				return false;
			}
			if (!TestPenetrationAxis(
				moving,
				obstacle,
				obstacle.axis[i],
				bestOverlap,
				bestPushAxis
			)) {
				return false;
			}
		}

		for (uint32_t movingAxis = 0; movingAxis < 3; ++movingAxis) {
			for (uint32_t obstacleAxis = 0; obstacleAxis < 3; ++obstacleAxis) {
				if (!TestPenetrationAxis(
					moving,
					obstacle,
					Math::Cross(
						moving.axis[movingAxis],
						obstacle.axis[obstacleAxis]
					),
					bestOverlap,
					bestPushAxis
				)) {
					return false;
				}
			}
		}

		if (
			bestOverlap <= kPenetrationSlop ||
			Math::Length(bestPushAxis) < kPenetrationEpsilon
		) {
			return false;
		}

		pushOut = Math::Multiply(
			bestPushAxis,
			bestOverlap + kPenetrationSkin
		);
		return true;
	}

	void ApplyFreezeAxes(const PhysicsBody& body, Vector3& pushOut) {
		if (body.freezePositionX) {
			pushOut.x = 0.0f;
		}
		if (body.freezePositionY) {
			pushOut.y = 0.0f;
		}
		if (body.freezePositionZ) {
			pushOut.z = 0.0f;
		}
	}
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

void PhysicsWorld::AddStaticCollider(Collider* collider) {
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
		ResolveStaticPenetration(*body);

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
		if (
			!body->isGrounded &&
			body->velocity.y <= 0.0f &&
			SnapToGround(*body, kGroundProbeDistance)
		) {
			body->isGrounded = true;
			body->velocity.y = 0.0f;
		}
		ResolveStaticPenetration(*body);
	}
}

bool PhysicsWorld::CollidesWithStatic(const PhysicsBody& body) const {
	if (!body.collider) {
		return false;
	}

	for (const PhysicsBody* other : bodies_) {
		if (
			!other ||
			other == &body ||
			!other->collider ||
			other->type == PhysicsBodyType::Dynamic
		) {
			continue;
		}
		if (body.collider->Intersects(*other->collider)) {
			return true;
		}
	}

	for (const Collider* collider : staticColliders_) {
		if (
			collider &&
			collider != body.collider &&
			body.collider->Intersects(*collider)
		) {
			return true;
		}
	}

	return false;
}

bool PhysicsWorld::SnapToGround(
	PhysicsBody& body,
	float probeDistance
) const {
	if (
		!body.transform ||
		!body.collider ||
		probeDistance <= 0.0f
	) {
		return false;
	}

	const float startY = body.transform->translate.y;
	const float delta = -probeDistance;
	body.transform->translate.y = startY + delta;
	if (!CollidesWithStatic(body)) {
		body.transform->translate.y = startY;
		return false;
	}

	float safeRate = 0.0f;
	float hitRate = 1.0f;
	for (uint32_t i = 0; i < kAxisSweepIterations; ++i) {
		const float testRate = (safeRate + hitRate) * 0.5f;
		body.transform->translate.y = startY + delta * testRate;
		if (CollidesWithStatic(body)) {
			hitRate = testRate;
		} else {
			safeRate = testRate;
		}
	}
	body.transform->translate.y = startY + delta * safeRate;
	return true;
}

bool PhysicsWorld::ResolveStaticPenetration(PhysicsBody& body) const {
	if (
		!body.transform ||
		!body.collider ||
		body.collider->GetType() != Collider::Type::OBB
	) {
		return false;
	}

	bool resolvedAny = false;
	std::vector<const OBBCollider*> testedColliders;
	const auto* bodyCollider =
		static_cast<const OBBCollider*>(body.collider);

	auto resolveAgainst = [&](const Collider* candidate) {
		if (!candidate || candidate->GetType() != Collider::Type::OBB) {
			return;
		}
		const auto* collider = static_cast<const OBBCollider*>(candidate);
		if (
			!collider ||
			collider == bodyCollider ||
			!bodyCollider->CanCollideWith(*collider) ||
			std::find(
				testedColliders.begin(),
				testedColliders.end(),
				collider
			) != testedColliders.end()
		) {
			return;
		}
		testedColliders.push_back(collider);

		for (uint32_t iteration = 0;
			iteration < kPenetrationResolveIterations;
			++iteration) {
			Vector3 pushOut{};
			if (!TryComputePushOut(
				bodyCollider->GetOBB(),
				collider->GetOBB(),
				pushOut
			)) {
				return;
			}

			ApplyFreezeAxes(body, pushOut);
			if (Math::Length(pushOut) < kPenetrationEpsilon) {
				return;
			}

			body.transform->translate = Math::Add(
				body.transform->translate,
				pushOut
			);
			if (std::abs(pushOut.x) > kPenetrationEpsilon) {
				body.velocity.x = 0.0f;
			}
			if (std::abs(pushOut.y) > kPenetrationEpsilon) {
				body.velocity.y = 0.0f;
				if (pushOut.y > 0.0f) {
					body.isGrounded = true;
				}
			}
			if (std::abs(pushOut.z) > kPenetrationEpsilon) {
				body.velocity.z = 0.0f;
			}
			resolvedAny = true;
		}
	};

	for (const PhysicsBody* other : bodies_) {
		if (
			!other ||
			other == &body ||
			!other->collider ||
			other->type == PhysicsBodyType::Dynamic
		) {
			continue;
		}
		resolveAgainst(other->collider);
	}

	for (const Collider* collider : staticColliders_) {
		resolveAgainst(collider);
	}

	return resolvedAny;
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

	const float startPosition = *components[axis];
	*components[axis] += delta;
	if (CollidesWithStatic(body)) {
		*components[axis] = startPosition;
		float safeRate = 0.0f;
		float hitRate = 1.0f;
		for (uint32_t i = 0; i < kAxisSweepIterations; ++i) {
			const float testRate = (safeRate + hitRate) * 0.5f;
			*components[axis] = startPosition + delta * testRate;
			if (CollidesWithStatic(body)) {
				hitRate = testRate;
			} else {
				safeRate = testRate;
			}
		}
		*components[axis] = startPosition + delta * safeRate;
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
