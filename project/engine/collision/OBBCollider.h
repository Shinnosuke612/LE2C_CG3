// 役割: 回転可能なボックスColliderの形状と交差判定用データを定義する。
#pragma once

#include <array>

#include "Collider.h"

class OBBCollider : public Collider {
public:
	struct OBB {
		Vector3 center;
		std::array<Vector3, 3> axis;
		Vector3 halfSize;
	};

public:
	Type GetType() const override { return Type::OBB; }
	bool Intersects(const Collider& other) const override;

	void SetHalfSize(const Vector3& halfSize) { halfSize_ = halfSize; }
	const Vector3& GetHalfSize() const { return halfSize_; }

	OBB GetOBB() const;

private:
	Vector3 halfSize_ = { 0.5f, 0.5f, 0.5f };
};
