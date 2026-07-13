// 役割: ワールド行列からOBBの軸と半サイズを計算する。
#include "OBBCollider.h"

#include "SphereCollider.h"
#include "../math/Matrix4x4.h"
#include "../math/Math.h"
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
	obb.halfSize = {
		halfSize_.x * GetWorldAxisScale(0),
		halfSize_.y * GetWorldAxisScale(1),
		halfSize_.z * GetWorldAxisScale(2)
	};

	const Matrix4x4 fallbackMatrix = MakeIdentity4x4();
	const Matrix4x4& worldMatrix =
		GetWorldMatrix() ? *GetWorldMatrix() : fallbackMatrix;
	obb.axis[0] = GetMatrixAxis(worldMatrix, 0);
	obb.axis[1] = GetMatrixAxis(worldMatrix, 1);
	obb.axis[2] = GetMatrixAxis(worldMatrix, 2);

	return obb;
}
