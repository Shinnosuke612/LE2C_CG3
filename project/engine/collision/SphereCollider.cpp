// 役割: ワールドスケールを考慮した球Collider半径を実装する。
#include "SphereCollider.h"

#include "OBBCollider.h"

float SphereCollider::GetRadius() const {
	return radius_ * GetMaxWorldScale();
}

bool SphereCollider::Intersects(const Collider& other) const {
	if (!CanCollideWith(other)) {
		return false;
	}

	switch (other.GetType()) {
	case Type::Sphere:
		return CheckCollision(*this, static_cast<const SphereCollider&>(other));
	case Type::OBB:
		return CheckCollision(*this, static_cast<const OBBCollider&>(other));
	default:
		return false;
	}
}
