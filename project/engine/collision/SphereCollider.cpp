#include "SphereCollider.h"

#include "OBBCollider.h"

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
