// 役割: AgentSteeringで公開する乱数、方向、回転、速度追従の計算を実装する。
#include "AgentSteering.h"

#include "../math/Math.h"
#include "../math/Matrix4x4.h"
#include "../scene/SceneDocument.h"

#include <algorithm>
#include <chrono>
#include <cmath>

namespace {
	void ApplyForwardAxisOffset(Vector3& rotate, const std::string& axis) {
		constexpr float halfPi = 1.57079632679f;
		constexpr float pi = 3.14159265359f;
		if (axis == "-Z") {
			rotate.y += pi;
		} else if (axis == "+X") {
			rotate.y -= halfPi;
		} else if (axis == "-X") {
			rotate.y += halfPi;
		} else if (axis == "+Y") {
			rotate.x += halfPi;
		} else if (axis == "-Y") {
			rotate.x -= halfPi;
		}
	}

	Vector3 ResolveForwardAxisVector(const std::string& axis) {
		if (axis == "-Z") {
			return { 0.0f, 0.0f, -1.0f };
		}
		if (axis == "+X") {
			return { 1.0f, 0.0f, 0.0f };
		}
		if (axis == "-X") {
			return { -1.0f, 0.0f, 0.0f };
		}
		if (axis == "+Y") {
			return { 0.0f, 1.0f, 0.0f };
		}
		if (axis == "-Y") {
			return { 0.0f, -1.0f, 0.0f };
		}
		return { 0.0f, 0.0f, 1.0f };
	}
}

namespace AgentSteering {
	float Hash01(uint64_t id, uint32_t salt) {
		uint64_t value = id + 0x9E3779B97F4A7C15ull + salt;
		value = (value ^ (value >> 30)) * 0xBF58476D1CE4E5B9ull;
		value = (value ^ (value >> 27)) * 0x94D049BB133111EBull;
		value = value ^ (value >> 31);
		return static_cast<float>(value & 0x00FFFFFFu) /
			static_cast<float>(0x00FFFFFFu);
	}

	uint64_t BuildFlockRuntimeSeed(
		uint64_t stableId,
		int configuredSeed,
		bool randomizeOnPlay
	) {
		uint64_t seed = stableId ^
			(static_cast<uint64_t>((std::max)(configuredSeed, 0)) << 32);
		if (!randomizeOnPlay) {
			return seed;
		}
		static uint64_t sequence = 0;
		const uint64_t ticks = static_cast<uint64_t>(
			std::chrono::high_resolution_clock::now().time_since_epoch().count()
		);
		return seed ^ ticks ^ (++sequence * 0x9E3779B97F4A7C15ull);
	}

	Vector3 AddScaled(Vector3 base, const Vector3& value, float scale) {
		base.x += value.x * scale;
		base.y += value.y * scale;
		base.z += value.z * scale;
		return base;
	}

	Vector3 LerpVector(const Vector3& a, const Vector3& b, float t) {
		return {
			a.x + (b.x - a.x) * t,
			a.y + (b.y - a.y) * t,
			a.z + (b.z - a.z) * t
		};
	}

	Vector3 SafeNormalize(const Vector3& value, const Vector3& fallback) {
		return Math::Length(value) > 0.000001f
			? Math::Normalize(value)
			: fallback;
	}

	Vector3 BuildFlockWanderDirection(
		const Vector3& currentHeading,
		uint64_t seedId,
		uint32_t step,
		float directionRange,
		float verticalRange
	) {
		const Vector3 heading = SafeNormalize(
			currentHeading,
			{ 0.0f, 0.0f, 1.0f }
		);
		const float yaw = std::atan2(heading.x, heading.z);
		const float yawOffset =
			(Hash01(seedId, 307u + step * 3u) * 2.0f - 1.0f) *
			directionRange;
		const float vertical =
			(Hash01(seedId, 311u + step * 3u) * 2.0f - 1.0f) *
			verticalRange;
		return SafeNormalize(
			{
				std::sin(yaw + yawOffset),
				vertical,
				std::cos(yaw + yawOffset)
			},
			heading
		);
	}

	Vector3 BuildFlockMemberJitterTarget(
		uint64_t entityId,
		uint64_t teamSeed,
		uint32_t step,
		float strength
	) {
		const uint64_t seed = entityId ^ teamSeed;
		const uint32_t salt = 601u + step * 5u;
		return {
			(Hash01(seed, salt) * 2.0f - 1.0f) * strength,
			(Hash01(seed, salt + 1u) * 2.0f - 1.0f) * strength * 0.45f,
			(Hash01(seed, salt + 2u) * 2.0f - 1.0f) * strength
		};
	}

	float FollowAmount(float followSpeed, float deltaTime) {
		if (followSpeed <= 0.0f || deltaTime <= 0.0f) {
			return 0.0f;
		}
		return std::clamp(
			1.0f - std::exp(-followSpeed * deltaTime),
			0.0f,
			1.0f
		);
	}

	Vector3 BlendDirections(
		const Vector3& from,
		const Vector3& to,
		float amount,
		const Vector3& fallback
	) {
		const float blend = std::clamp(amount, 0.0f, 1.0f);
		const Vector3 fromDirection = SafeNormalize(from, fallback);
		const Vector3 toDirection = SafeNormalize(to, fromDirection);
		return SafeNormalize(
			LerpVector(fromDirection, toDirection, blend),
			blend < 0.5f ? fromDirection : toDirection
		);
	}

	Vector3 ClampVectorLength(const Vector3& value, float maximumLength) {
		const float length = Math::Length(value);
		if (maximumLength <= 0.0f || length <= maximumLength) {
			return maximumLength <= 0.0f ? Vector3{} : value;
		}
		return Math::Multiply(value, maximumLength / length);
	}

	Vector3 MoveVectorToward(
		const Vector3& current,
		const Vector3& target,
		float maximumDelta
	) {
		if (maximumDelta <= 0.0f) {
			return current;
		}
		const Vector3 delta = Math::Subtract(target, current);
		return Math::Add(current, ClampVectorLength(delta, maximumDelta));
	}

	Vector3 RotateDirectionToward(
		const Vector3& current,
		const Vector3& target,
		float maximumRadians,
		const Vector3& fallback
	) {
		const Vector3 currentDirection = SafeNormalize(current, fallback);
		const Vector3 targetDirection = SafeNormalize(target, currentDirection);
		if (maximumRadians <= 0.0f) {
			return currentDirection;
		}
		const float dot = std::clamp(
			currentDirection.x * targetDirection.x +
				currentDirection.y * targetDirection.y +
				currentDirection.z * targetDirection.z,
			-1.0f,
			1.0f
		);
		const float angle = std::acos(dot);
		if (angle <= maximumRadians || angle <= 0.0001f) {
			return targetDirection;
		}
		return BlendDirections(
			currentDirection,
			targetDirection,
			maximumRadians / angle,
			currentDirection
		);
	}

	Vector3 RotateDirection(
		const Vector3& direction,
		const Vector3& rotate
	) {
		const Matrix4x4 matrix = MakeAffineMatrix(
			{ 1.0f, 1.0f, 1.0f },
			rotate,
			{ 0.0f, 0.0f, 0.0f }
		);
		return {
			direction.x * matrix.m[0][0] +
				direction.y * matrix.m[1][0] +
				direction.z * matrix.m[2][0],
			direction.x * matrix.m[0][1] +
				direction.y * matrix.m[1][1] +
				direction.z * matrix.m[2][1],
			direction.x * matrix.m[0][2] +
				direction.y * matrix.m[1][2] +
				direction.z * matrix.m[2][2]
		};
	}

	Vector3 ForwardDirectionFromRotation(
		const Vector3& rotate,
		const std::string& forwardAxis,
		const Vector3& fallback
	) {
		return SafeNormalize(
			RotateDirection(ResolveForwardAxisVector(forwardAxis), rotate),
			fallback
		);
	}

	Vector3 BuildAgentVelocityRotation(
		const SceneComponent& behavior,
		const Vector3& currentRotate,
		const Vector3& velocity,
		const Vector3& desiredDirection,
		float deltaTime,
		float rotationFollowSpeed
	) {
		Vector3 target = currentRotate;
		if (!behavior.agentAlignForwardToVelocity) {
			return target;
		}
		if (Math::Length(velocity) <= 0.0001f) {
			return target;
		}

		const float horizontalLength = std::sqrt(
			velocity.x * velocity.x + velocity.z * velocity.z
		);
		Vector3 velocityRotate = currentRotate;
		if (horizontalLength > 0.0001f) {
			velocityRotate.y = std::atan2(velocity.x, velocity.z);
		}
		velocityRotate.x = std::clamp(
			-std::atan2(velocity.y, horizontalLength) *
				behavior.agentPitchFromVerticalVelocity,
			-1.45f,
			1.45f
		);
		const Vector3 velocityDirection =
			SafeNormalize(velocity, { 0.0f, 0.0f, 1.0f });
		const float turnSign =
			velocityDirection.x * desiredDirection.z -
			velocityDirection.z * desiredDirection.x;
		velocityRotate.z = std::clamp(
			-turnSign * behavior.agentBankingStrength,
			-1.35f,
			1.35f
		);
		ApplyForwardAxisOffset(velocityRotate, behavior.agentForwardAxis);

		const float rotationLerp = std::clamp(
			FollowAmount(rotationFollowSpeed, deltaTime),
			0.0f,
			1.0f
		);
		if (behavior.agentRotateAxisX) {
			target.x = Math::NormalizeAngle(
				Math::LerpAngle(currentRotate.x, velocityRotate.x, rotationLerp)
			);
		}
		if (behavior.agentRotateAxisY) {
			target.y = Math::NormalizeAngle(
				Math::LerpAngle(currentRotate.y, velocityRotate.y, rotationLerp)
			);
		}
		if (behavior.agentRotateAxisZ) {
			target.z = Math::NormalizeAngle(
				Math::LerpAngle(currentRotate.z, velocityRotate.z, rotationLerp)
			);
		}
		return target;
	}

	Vector3 BuildAgentVelocityRotation(
		const SceneComponent& behavior,
		const Vector3& currentRotate,
		const Vector3& velocity,
		const Vector3& desiredDirection,
		float deltaTime
	) {
		return BuildAgentVelocityRotation(
			behavior,
			currentRotate,
			velocity,
			desiredDirection,
			deltaTime,
			behavior.agentRotationFollowSpeed
		);
	}
}
