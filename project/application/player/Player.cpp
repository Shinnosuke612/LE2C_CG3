// 役割: プレイヤーの入力移動と物理状態の同期を実装する。
#include "Player.h"

#include "../../engine/3d/Object3d.h"
#include "../../engine/3d/Object3dCommon.h"
#include "../../engine/io/Input.h"
#include "../../engine/math/Math.h"

#include <algorithm>
#include <cmath>

void Player::Initialize(Object3dCommon* object3dCommon, const char* modelName) {
	object_ = new Object3d();
	ownsObject_ = true;
	object_->Initialize(object3dCommon);
	object_->SetModel(modelName);
	object_->SetScale({ 1.0f, 1.0f, 1.0f });
	ApplyPosition();
	object_->Update();

	physicsBody_.type = PhysicsBodyType::Dynamic;
	physicsBody_.transform = &object_->GetTransform();
	physicsBody_.collider = collider_;
	physicsBody_.useGravity = true;
	physicsBody_.gravityScale = 8.0f;
	physicsBody_.maxFallSpeed = 60.0f;
	physicsBody_.friction = 0.0f;
}

void Player::Initialize(Object3d* object) {
	object_ = object;
	ownsObject_ = false;
	if (!object_) {
		return;
	}
	position_ = object_->GetTransform().translate;
	physicsBody_.type = PhysicsBodyType::Dynamic;
	physicsBody_.transform = &object_->GetTransform();
	physicsBody_.collider = collider_;
	physicsBody_.useGravity = true;
	physicsBody_.gravityScale = 8.0f;
	physicsBody_.maxFallSpeed = 60.0f;
	physicsBody_.friction = 0.0f;
}

void Player::SetCollider(Collider* collider) {
	collider_ = collider;
	physicsBody_.collider = collider_;
}

void Player::Update(
	const Camera*,
	bool acceptGameplayInput
) {
	if (!object_) {
		return;
	}
	Vector3 inputMove{};
	Input* input = acceptGameplayInput ? Input::GetInstance() : nullptr;

	if (input && input->PushKey(DIK_W)) {
		inputMove.x += 1.0f;
	}
	if (input && input->PushKey(DIK_S)) {
		inputMove.x -= 1.0f;
	}
	if (input && input->PushKey(DIK_A)) {
		inputMove.z += 1.0f;
	}
	if (input && input->PushKey(DIK_D)) {
		inputMove.z -= 1.0f;
	}

	Vector3 desiredVelocity = physicsBody_.velocity;
	desiredVelocity.x = 0.0f;
	desiredVelocity.z = 0.0f;

	const Vector3 movementRight = { 1.0f, 0.0f, 0.0f };
	const Vector3 movementForward = { 0.0f, 0.0f, 1.0f };

	const bool dash =
		input && (input->PushKey(DIK_LSHIFT) || input->PushKey(DIK_RSHIFT));
	const float speedMultiplier = dash ? dashMultiplier_ : 1.0f;

	if (Math::Length(inputMove) > 0.000001f) {
		Vector3 move = Math::Add(
			Math::Multiply(movementRight, inputMove.x),
			Math::Multiply(movementForward, inputMove.z)
		);
		const float activeMoveSpeed =
			moveSpeed_ *
			speedMultiplier *
			(inWater_ ? waterMoveSpeedMultiplier_ : 1.0f);
		move = Math::Multiply(Math::Normalize(move), activeMoveSpeed);
		desiredVelocity.x = move.x;
		desiredVelocity.z = move.z;
		object_->SetRotate({ 0.0f, std::atan2(move.x, move.z), 0.0f });
		if (inWater_) {
			desiredVelocity.y = move.y;
		}
	}

	if (acceptGameplayInput && inWater_) {
		const bool swimUp = input && input->PushKey(DIK_SPACE);
		const bool swimDown = input && input->PushKey(DIK_LCONTROL);
		if (swimUp) {
			desiredVelocity.y = waterSwimUpSpeed_ * speedMultiplier;
			physicsBody_.isGrounded = false;
		} else if (swimDown) {
			desiredVelocity.y = -waterSwimUpSpeed_ * 10.6f * speedMultiplier;
			physicsBody_.isGrounded = false;
		} else {
			desiredVelocity.y = std::clamp(
				desiredVelocity.y,
				-waterSwimUpSpeed_ * 0.35f * speedMultiplier,
				waterSwimUpSpeed_ * 0.35f * speedMultiplier
			);
		}
	} else if (input && allowJump_ && physicsBody_.isGrounded && input->TriggerKey(DIK_SPACE)) {
		desiredVelocity.y = jumpVelocity_;
		physicsBody_.isGrounded = false;
	}

	physicsBody_.velocity = desiredVelocity;
	object_->Update();
}

void Player::PostPhysicsUpdate() {
	if (!object_) {
		return;
	}
	position_ = object_->GetTransform().translate;
	object_->Update();
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
	physicsBody_.velocity = {};
	physicsBody_.isGrounded = false;
	object_->GetTransform() = transform;
	ApplyPosition();
	object_->Update();
}

bool Player::ClampToWaterBounds(
	const Vector3& center,
	float yaw,
	float halfSizeX,
	float halfSizeZ
) {
	if (!object_ || !std::isfinite(yaw) ||
		!std::isfinite(halfSizeX) || !std::isfinite(halfSizeZ)) {
		return false;
	}
	const float cosine = std::cos(yaw);
	const float sine = std::sin(yaw);
	const Vector3 worldDelta = {
		position_.x - center.x,
		0.0f,
		position_.z - center.z
	};
	const float localX = worldDelta.x * cosine - worldDelta.z * sine;
	const float localZ = worldDelta.x * sine + worldDelta.z * cosine;
	const float clampedX = std::clamp(localX, -halfSizeX, halfSizeX);
	const float clampedZ = std::clamp(localZ, -halfSizeZ, halfSizeZ);
	if (clampedX == localX && clampedZ == localZ) {
		return false;
	}
	position_.x = center.x + clampedX * cosine + clampedZ * sine;
	position_.z = center.z - clampedX * sine + clampedZ * cosine;
	physicsBody_.velocity.x = 0.0f;
	physicsBody_.velocity.z = 0.0f;
	ApplyPosition();
	object_->Update();
	return true;
}

void Player::SetBehaviorSettings(
	float moveSpeed,
	float jumpVelocity,
	float turnResponsiveness,
	float dashMultiplier,
	bool cameraRelativeMove,
	bool allowJump
) {
	moveSpeed_ = (std::max)(moveSpeed, 0.0f);
	jumpVelocity_ = (std::max)(jumpVelocity, 0.0f);
	turnResponsiveness_ = std::clamp(turnResponsiveness, 0.0f, 1.0f);
	dashMultiplier_ = (std::max)(dashMultiplier, 1.0f);
	cameraRelativeMove_ = cameraRelativeMove;
	allowJump_ = allowJump;
}

void Player::SetWaterState(
	bool inWater,
	float moveSpeedMultiplier,
	float swimUpSpeed
) {
	inWater_ = inWater;
	waterMoveSpeedMultiplier_ = std::clamp(moveSpeedMultiplier, 0.0f, 1.0f);
	waterSwimUpSpeed_ = (std::max)(swimUpSpeed, 0.0f);
}

void Player::ApplyPosition() {
	if (object_) {
		object_->SetTranslate(position_);
	}
}
