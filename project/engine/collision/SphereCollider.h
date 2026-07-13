// 役割: 球Colliderの半径とワールドスケールを定義する。
#pragma once

#include "Collider.h"

class SphereCollider : public Collider {
public:
	Type GetType() const override { return Type::Sphere; }
	bool Intersects(const Collider& other) const override;

	void SetRadius(float radius) { radius_ = radius; }
	float GetRadius() const;

private:
	float radius_ = 1.0f;
};
