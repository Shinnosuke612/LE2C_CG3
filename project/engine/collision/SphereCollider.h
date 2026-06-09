#pragma once

#include "Collider.h"

class SphereCollider : public Collider {
public:
	Type GetType() const override { return Type::Sphere; }
	bool Intersects(const Collider& other) const override;

	void SetRadius(float radius) { radius_ = radius; }
	float GetRadius() const { return radius_; }

private:
	float radius_ = 1.0f;
};
