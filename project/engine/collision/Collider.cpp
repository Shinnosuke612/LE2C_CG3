// 役割: Colliderのワールド中心とワールドスケールの共通計算を実装する。
#include "Collider.h"

#include "../math/Math.h"

#include <algorithm>

bool Collider::CanCollideWith(const Collider& other) const {
	return isActive_ &&
		other.isActive_ &&
		(collisionMask_ & other.collisionAttribute_) != 0 &&
		(other.collisionMask_ & collisionAttribute_) != 0;
}

Vector3 Collider::GetWorldCenter() const {
	if (!worldMatrix_) {
		return offset_;
	}

	return {
		offset_.x * worldMatrix_->m[0][0] +
		offset_.y * worldMatrix_->m[1][0] +
		offset_.z * worldMatrix_->m[2][0] +
		worldMatrix_->m[3][0],
		offset_.x * worldMatrix_->m[0][1] +
		offset_.y * worldMatrix_->m[1][1] +
		offset_.z * worldMatrix_->m[2][1] +
		worldMatrix_->m[3][1],
		offset_.x * worldMatrix_->m[0][2] +
		offset_.y * worldMatrix_->m[1][2] +
		offset_.z * worldMatrix_->m[2][2] +
		worldMatrix_->m[3][2]
	};
}

float Collider::GetWorldAxisScale(uint32_t axis) const {
	if (!worldMatrix_ || axis >= 3) {
		return 1.0f;
	}

	return Math::Length({
		worldMatrix_->m[axis][0],
		worldMatrix_->m[axis][1],
		worldMatrix_->m[axis][2]
	});
}

float Collider::GetMaxWorldScale() const {
	return (std::max)(
		GetWorldAxisScale(0),
		(std::max)(GetWorldAxisScale(1), GetWorldAxisScale(2))
	);
}
