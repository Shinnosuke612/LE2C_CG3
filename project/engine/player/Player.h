#pragma once

#include <vector>

#include "../collision/OBBCollider.h"
#include "../math/Vector3.h"
#include "../math/Transform.h"

class Object3d;
class Object3dCommon;
struct Matrix4x4;

class Player {
public:
	void Initialize(Object3dCommon* object3dCommon, const char* modelName);
	void Update(const std::vector<OBBCollider*>& staticColliders);
	void Draw();
	void DrawShadow(const Matrix4x4& lightViewProjection);
	void Finalize();

	const Vector3& GetPosition() const { return position_; }
	OBBCollider& GetCollider() { return collider_; }
	Object3d* GetObject() const { return object_; }
	void SetTransform(const Transform& transform);

private:
	bool IsColliding(const std::vector<OBBCollider*>& staticColliders) const;
	void ApplyPosition();

private:
	Object3d* object_ = nullptr;
	OBBCollider collider_;
	Vector3 position_ = { 0.0f, 1.0f, -4.0f };
	float moveSpeed_ = 0.18f;
};
