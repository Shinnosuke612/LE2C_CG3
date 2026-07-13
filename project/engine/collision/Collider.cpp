#include "Collider.h"

#include "OBBCollider.h"
#include "SphereCollider.h"
#include "../debug/DebugRenderer.h"
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

void Collider::DrawDebug(const Vector4& color) const {
	DebugRenderer* debugRenderer = DebugRenderer::GetInstance();
	switch (GetType()) {
	case Type::Sphere: {
		const auto& sphere = static_cast<const SphereCollider&>(*this);
		debugRenderer->AddSphere(GetWorldCenter(), sphere.GetRadius(), color);
		break;
	}
	case Type::OBB: {
		const auto& obbCollider = static_cast<const OBBCollider&>(*this);
		const OBBCollider::OBB obb = obbCollider.GetOBB();
		debugRenderer->AddOBB(obb.center, obb.axis, obb.halfSize, color);
		break;
	}
	}
}
