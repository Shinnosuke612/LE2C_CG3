#include "Player.h"

#include "../../engine/3d/Object3d.h"
#include "../../engine/3d/Object3dCommon.h"
#include "../../engine/3d/Camera.h"
#include "../../engine/io/Input.h"
#include "../../engine/math/Math.h"
#include "../../engine/math/Matrix4x4.h"

#include <algorithm>
#include <cmath>

namespace {
	constexpr float kPi = 3.14159265358979323846f;
	constexpr float kTwoPi = kPi * 2.0f;

	float NormalizeAngle(float angle) {
		while (angle > kPi) {
			angle -= kTwoPi;
		}
		while (angle < -kPi) {
			angle += kTwoPi;
		}
		return angle;
	}

	Vector3 FlattenAndNormalize(Vector3 value) {
		value.y = 0.0f;
		if (Math::Length(value) < 0.000001f) {
			return { 0.0f, 0.0f, 0.0f };
		}
		return Math::Normalize(value);
	}
}

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

	physicsBody_.type = PhysicsBodyType::Dynamic;
	physicsBody_.transform = &object_->GetTransform();
	physicsBody_.obbCollider = &collider_;
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
	collider_.SetWorldTransform(&object_->GetTransform());
	collider_.SetHalfSize({ 1.0f, 1.0f, 1.0f });
	collider_.SetOffset({ 0.0f, 0.0f, 0.0f });

	physicsBody_.type = PhysicsBodyType::Dynamic;
	physicsBody_.transform = &object_->GetTransform();
	physicsBody_.obbCollider = &collider_;
	physicsBody_.useGravity = true;
	physicsBody_.gravityScale = 8.0f;
	physicsBody_.maxFallSpeed = 60.0f;
	physicsBody_.friction = 0.0f;
}

void Player::Update(
	const Camera* camera
) {
	if (!object_) {
		return;
	}
	Vector3 inputMove{};
	Input* input = Input::GetInstance();

	if (input->PushKey(DIK_W)) {
		inputMove.z += 1.0f;
	}
	if (input->PushKey(DIK_S)) {
		inputMove.z -= 1.0f;
	}
	if (input->PushKey(DIK_A)) {
		inputMove.x -= 1.0f;
	}
	if (input->PushKey(DIK_D)) {
		inputMove.x += 1.0f;
	}

	Vector3 desiredVelocity = physicsBody_.velocity;
	desiredVelocity.x = 0.0f;
	desiredVelocity.z = 0.0f;

	if (Math::Length(inputMove) > 0.000001f) {
		Vector3 forward = { 0.0f, 0.0f, 1.0f };
		Vector3 right = { 1.0f, 0.0f, 0.0f };
		if (cameraRelativeMove_ && camera) {
			const Matrix4x4& cameraWorld = camera->GetWorldMatrix();
			right = FlattenAndNormalize({
				cameraWorld.m[0][0],
				cameraWorld.m[0][1],
				cameraWorld.m[0][2]
			});
			forward = FlattenAndNormalize({
				cameraWorld.m[2][0],
				cameraWorld.m[2][1],
				cameraWorld.m[2][2]
			});
			if (Math::Length(right) < 0.000001f) {
				right = { 1.0f, 0.0f, 0.0f };
			}
			if (Math::Length(forward) < 0.000001f) {
				forward = { 0.0f, 0.0f, 1.0f };
			}
		}

		Vector3 move = Math::Add(
			Math::Multiply(right, inputMove.x),
			Math::Multiply(forward, inputMove.z)
		);
		const float activeMoveSpeed =
			moveSpeed_ * (inWater_ ? waterMoveSpeedMultiplier_ : 1.0f);
		move = Math::Multiply(Math::Normalize(move), activeMoveSpeed);
		desiredVelocity.x = move.x;
		desiredVelocity.z = move.z;

		// S入力を含む後退移動中は、向きを変えない
		const bool isMovingBackward = inputMove.z < 0.0f;

		if (!isMovingBackward && (move.x != 0.0f || move.z != 0.0f)) {
			const float targetYaw = std::atan2(move.x, move.z);
			const float currentYaw = object_->GetRotate().y;
			const float yawDelta = NormalizeAngle(targetYaw - currentYaw);
			const float nextYaw = currentYaw + yawDelta *
				std::clamp(turnResponsiveness_, 0.0f, 1.0f);
			object_->SetRotate({ 0.0f, nextYaw, 0.0f });
		}
	}

	if (inWater_) {
		const bool swimUp = input->PushKey(DIK_SPACE);
		const bool swimDown = input->PushKey(DIK_LCONTROL);
		if (swimUp) {
			desiredVelocity.y = waterSwimUpSpeed_;
			physicsBody_.isGrounded = false;
		} else if (swimDown) {
			desiredVelocity.y = -waterSwimUpSpeed_ * 0.6f;
			physicsBody_.isGrounded = false;
		} else {
			desiredVelocity.y = std::clamp(
				desiredVelocity.y,
				-waterSwimUpSpeed_ * 0.35f,
				waterSwimUpSpeed_ * 0.35f
			);
		}
	} else if (allowJump_ && physicsBody_.isGrounded && input->TriggerKey(DIK_SPACE)) {
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

void Player::SetBehaviorSettings(
	float moveSpeed,
	float jumpVelocity,
	float turnResponsiveness,
	bool cameraRelativeMove,
	bool allowJump
) {
	moveSpeed_ = (std::max)(moveSpeed, 0.0f);
	jumpVelocity_ = (std::max)(jumpVelocity, 0.0f);
	turnResponsiveness_ = std::clamp(turnResponsiveness, 0.0f, 1.0f);
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
