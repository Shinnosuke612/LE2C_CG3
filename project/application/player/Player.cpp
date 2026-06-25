#include "Player.h"

#include "../../engine/3d/Object3d.h"
#include "../../engine/3d/Object3dCommon.h"
#include "../../engine/io/Input.h"
#include "../../engine/math/Math.h"
#include "../../engine/math/Matrix4x4.h"

#include <cmath>

void Player::Initialize(Object3dCommon* object3dCommon, const char* modelName) {
	object_ = new Object3d();
	ownsObject_ = true;
	object_->Initialize(object3dCommon);
	object_->SetModel(modelName);
	object_->SetScale({ 1.0f, 1.0f, 1.0f });
	ApplyPosition();
	object_->Update();

	collider_.SetWorldTransform(&object_->GetTransform());
	collider_.SetHalfSize({ 1.0f, 1.0f, 1.0f });
	collider_.SetOffset({ 0.0f, 0.0f, 0.0f });
}

void Player::Initialize(Object3d* object) {
	object_ = object;
	ownsObject_ = false;
	if (!object_) {
		return;
	}
	position_ = object_->GetTransform().translate;
	collider_.SetWorldTransform(&object_->GetTransform());
	collider_.SetHalfSize({ 1.0f, 1.0f, 1.0f });
	collider_.SetOffset({ 0.0f, 0.0f, 0.0f });
}

void Player::Update(const std::vector<OBBCollider*>& staticColliders) {
	if (!object_) {
		return;
	}
	Vector3 move{};
	Input* input = Input::GetInstance();

	if (input->PushKey(DIK_W)) {
		move.z += 1.0f;
	}
	if (input->PushKey(DIK_S)) {
		move.z -= 1.0f;
	}
	if (input->PushKey(DIK_A)) {
		move.x -= 1.0f;
	}
	if (input->PushKey(DIK_D)) {
		move.x += 1.0f;
	}

	if (Math::Length(move) > 0.000001f) {
		move = Math::Multiply(Math::Normalize(move), moveSpeed_);
		Vector3 previous = position_;
		position_ = Math::Add(position_, move);

		if (move.x != 0.0f || move.z != 0.0f) {
			object_->SetRotate({ 0.0f, std::atan2(move.x, move.z), 0.0f });
		}

		ApplyPosition();
		object_->Update();

		if (IsColliding(staticColliders)) {
			position_ = previous;
			ApplyPosition();
			object_->Update();
		}
	}
	else {
		object_->Update();
	}
}

void Player::Draw() {
	if (object_) {
		object_->Draw();
	}
}

void Player::DrawShadow(const Matrix4x4& lightViewProjection) {
	if (object_) {
		object_->DrawShadow(lightViewProjection);
	}
}

void Player::Finalize() {
	if (ownsObject_) {
		delete object_;
	}
	object_ = nullptr;
	ownsObject_ = false;
}

void Player::SetTransform(const Transform& transform) {
	if (!object_) {
		return;
	}
	position_ = transform.translate;
	object_->GetTransform() = transform;
	ApplyPosition();
	object_->Update();
}

bool Player::IsColliding(const std::vector<OBBCollider*>& staticColliders) const {
	for (const OBBCollider* staticCollider : staticColliders) {
		if (staticCollider && collider_.Intersects(*staticCollider)) {
			return true;
		}
	}

	return false;
}

void Player::ApplyPosition() {
	if (object_) {
		object_->SetTranslate(position_);
	}
}
