#include "OBBCollider.h"

#include "SphereCollider.h"
#include "../math/Matrix4x4.h"
#include "../math/Math.h"
#include "../math/Transform.h"

#include <algorithm>
#include <cmath>

namespace {
	Vector3 GetMatrixAxis(const Matrix4x4& matrix, uint32_t index) {
		Vector3 axis = {
			matrix.m[index][0],
			matrix.m[index][1],
			matrix.m[index][2]
		};

		axis = Math::Normalize(axis);
		if (Math::Length(axis) < 0.000001f) {
			if (index == 0) {
				return { 1.0f, 0.0f, 0.0f };
			}
			if (index == 1) {
				return { 0.0f, 1.0f, 0.0f };
			}
			return { 0.0f, 0.0f, 1.0f };
		}

		return axis;
	}
}

bool OBBCollider::Intersects(const Collider& other) const {
	if (!CanCollideWith(other)) {
		return false;
	}

	switch (other.GetType()) {
	case Type::Sphere:
		return CheckCollision(static_cast<const SphereCollider&>(other), *this);
	case Type::OBB:
		return CheckCollision(*this, static_cast<const OBBCollider&>(other));
	default:
		return false;
	}
}

OBBCollider::OBB OBBCollider::GetOBB() const {
	OBB obb{};
	obb.center = GetWorldCenter();
	obb.halfSize = halfSize_;

	Matrix4x4 rotateMatrix = MakeIdentity4x4();
	if (worldTransform_) {
		Matrix4x4 rotateX = MakeRotateXMatrix(worldTransform_->rotate.x);
		Matrix4x4 rotateY = MakeRotateYMatrix(worldTransform_->rotate.y);
		Matrix4x4 rotateZ = MakeRotateZMatrix(worldTransform_->rotate.z);
		rotateMatrix = Multiply(Multiply(rotateZ, rotateY), rotateX);
	}

	obb.axis[0] = GetMatrixAxis(rotateMatrix, 0);
	obb.axis[1] = GetMatrixAxis(rotateMatrix, 1);
	obb.axis[2] = GetMatrixAxis(rotateMatrix, 2);

	return obb;
}
