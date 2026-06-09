#include "Collider.h"

#include "OBBCollider.h"
#include "SphereCollider.h"
#include "../math/Math.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace {
	constexpr float kEpsilon = 0.00001f;

	float AbsDot(const Vector3& a, const Vector3& b) {
		return std::fabs(Math::Dot(a, b));
	}

	float ProjectOBB(const OBBCollider::OBB& obb, const Vector3& axis) {
		return
			obb.halfSize.x * AbsDot(obb.axis[0], axis) +
			obb.halfSize.y * AbsDot(obb.axis[1], axis) +
			obb.halfSize.z * AbsDot(obb.axis[2], axis);
	}

	bool OverlapOnAxis(const OBBCollider::OBB& a, const OBBCollider::OBB& b, const Vector3& axis) {
		if (Math::Length(axis) < kEpsilon) {
			return true;
		}

		Vector3 normalizedAxis = Math::Normalize(axis);
		Vector3 centerDelta = Math::Subtract(b.center, a.center);
		float distance = std::fabs(Math::Dot(centerDelta, normalizedAxis));
		float radiusA = ProjectOBB(a, normalizedAxis);
		float radiusB = ProjectOBB(b, normalizedAxis);

		return distance <= radiusA + radiusB;
	}
}

bool CheckCollision(const SphereCollider& a, const SphereCollider& b) {
	Vector3 delta = Math::Subtract(a.GetWorldCenter(), b.GetWorldCenter());
	float radius = a.GetRadius() + b.GetRadius();
	return Math::Dot(delta, delta) <= radius * radius;
}

bool CheckCollision(const SphereCollider& sphere, const OBBCollider& obbCollider) {
	OBBCollider::OBB obb = obbCollider.GetOBB();
	Vector3 sphereCenter = sphere.GetWorldCenter();
	Vector3 localDelta = Math::Subtract(sphereCenter, obb.center);
	Vector3 closestPoint = obb.center;

	const std::array<float, 3> halfSizes = {
		obb.halfSize.x,
		obb.halfSize.y,
		obb.halfSize.z
	};

	for (uint32_t i = 0; i < 3; ++i) {
		float distance = Math::Dot(localDelta, obb.axis[i]);
		float clampedDistance = std::clamp(distance, -halfSizes[i], halfSizes[i]);
		closestPoint = Math::Add(
			closestPoint,
			Math::Multiply(obb.axis[i], clampedDistance)
		);
	}

	Vector3 closestDelta = Math::Subtract(sphereCenter, closestPoint);
	return Math::Dot(closestDelta, closestDelta) <= sphere.GetRadius() * sphere.GetRadius();
}

bool CheckCollision(const OBBCollider& aCollider, const OBBCollider& bCollider) {
	OBBCollider::OBB a = aCollider.GetOBB();
	OBBCollider::OBB b = bCollider.GetOBB();

	for (uint32_t i = 0; i < 3; ++i) {
		if (!OverlapOnAxis(a, b, a.axis[i])) {
			return false;
		}
		if (!OverlapOnAxis(a, b, b.axis[i])) {
			return false;
		}
	}

	for (uint32_t aAxis = 0; aAxis < 3; ++aAxis) {
		for (uint32_t bAxis = 0; bAxis < 3; ++bAxis) {
			Vector3 crossAxis = Math::Cross(a.axis[aAxis], b.axis[bAxis]);
			if (!OverlapOnAxis(a, b, crossAxis)) {
				return false;
			}
		}
	}

	return true;
}
