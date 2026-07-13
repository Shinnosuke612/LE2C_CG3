// 役割: Agentの進行方向、補間、Jitter、回転を計算する純粋な操作を提供する。
#pragma once

#include "../math/Vector3.h"

#include <cstdint>
#include <string>

struct SceneComponent;

namespace AgentSteering {
	// 同じIDとsaltの組み合わせから、常に同じ0.0から1.0の値を返す。
	float Hash01(uint64_t id, uint32_t salt);

	uint64_t BuildFlockRuntimeSeed(
		uint64_t stableId,
		int configuredSeed,
		bool randomizeOnPlay
	);

	Vector3 AddScaled(Vector3 base, const Vector3& value, float scale);
	Vector3 LerpVector(const Vector3& a, const Vector3& b, float t);
	Vector3 SafeNormalize(const Vector3& value, const Vector3& fallback);

	Vector3 BuildFlockWanderDirection(
		const Vector3& currentHeading,
		uint64_t seedId,
		uint32_t step,
		float directionRange,
		float verticalRange
	);

	Vector3 BuildFlockMemberJitterTarget(
		uint64_t entityId,
		uint64_t teamSeed,
		uint32_t step,
		float strength
	);

	float FollowAmount(float followSpeed, float deltaTime);

	Vector3 BlendDirections(
		const Vector3& from,
		const Vector3& to,
		float amount,
		const Vector3& fallback
	);

	Vector3 ClampVectorLength(const Vector3& value, float maximumLength);
	Vector3 MoveVectorToward(
		const Vector3& current,
		const Vector3& target,
		float maximumDelta
	);

	Vector3 RotateDirectionToward(
		const Vector3& current,
		const Vector3& target,
		float maximumRadians,
		const Vector3& fallback
	);

	Vector3 RotateDirection(
		const Vector3& direction,
		const Vector3& rotate
	);

	Vector3 ForwardDirectionFromRotation(
		const Vector3& rotate,
		const std::string& forwardAxis,
		const Vector3& fallback
	);

	Vector3 BuildAgentVelocityRotation(
		const SceneComponent& behavior,
		const Vector3& currentRotate,
		const Vector3& velocity,
		const Vector3& desiredDirection,
		float deltaTime,
		float rotationFollowSpeed
	);

	Vector3 BuildAgentVelocityRotation(
		const SceneComponent& behavior,
		const Vector3& currentRotate,
		const Vector3& velocity,
		const Vector3& desiredDirection,
		float deltaTime
	);
}
