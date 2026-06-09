#include "Collider.h"

#include "../math/Math.h"
#include "../math/Transform.h"

bool Collider::CanCollideWith(const Collider& other) const {
	return isActive_ &&
		other.isActive_ &&
		(collisionMask_ & other.collisionAttribute_) != 0 &&
		(other.collisionMask_ & collisionAttribute_) != 0;
}

Vector3 Collider::GetWorldCenter() const {
	if (!worldTransform_) {
		return offset_;
	}

	return Math::Add(worldTransform_->translate, offset_);
}
