#pragma once

#include <cstdint>

#include "../math/Vector3.h"

struct Transform;
class SphereCollider;
class OBBCollider;

class Collider {
public:
	enum class Type {
		Sphere,
		OBB
	};

	virtual ~Collider() = default;

	virtual Type GetType() const = 0;
	virtual bool Intersects(const Collider& other) const = 0;

	void SetWorldTransform(const Transform* worldTransform) { worldTransform_ = worldTransform; }
	const Transform* GetWorldTransform() const { return worldTransform_; }

	void SetOffset(const Vector3& offset) { offset_ = offset; }
	const Vector3& GetOffset() const { return offset_; }

	void SetActive(bool active) { isActive_ = active; }
	bool IsActive() const { return isActive_; }

	void SetCollisionAttribute(uint32_t attribute) { collisionAttribute_ = attribute; }
	uint32_t GetCollisionAttribute() const { return collisionAttribute_; }

	void SetCollisionMask(uint32_t mask) { collisionMask_ = mask; }
	uint32_t GetCollisionMask() const { return collisionMask_; }

	bool CanCollideWith(const Collider& other) const;
	Vector3 GetWorldCenter() const;

protected:
	const Transform* worldTransform_ = nullptr;
	Vector3 offset_ = { 0.0f, 0.0f, 0.0f };
	bool isActive_ = true;
	uint32_t collisionAttribute_ = 0xffffffffu;
	uint32_t collisionMask_ = 0xffffffffu;
};

bool CheckCollision(const SphereCollider& a, const SphereCollider& b);
bool CheckCollision(const SphereCollider& sphere, const OBBCollider& obb);
bool CheckCollision(const OBBCollider& a, const OBBCollider& b);
