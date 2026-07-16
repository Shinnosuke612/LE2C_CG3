// 役割: Quaternionの行列変換、積、補間を実装する。
#include "Quaternion.h"

#include "Math.h"

#include <algorithm>
#include <cmath>

Quaternion Normalize(const Quaternion& quaternion) {
	const float length = std::sqrt(
		quaternion.x * quaternion.x +
		quaternion.y * quaternion.y +
		quaternion.z * quaternion.z +
		quaternion.w * quaternion.w
	);
	if (length <= 0.000001f) {
		return { 0.0f, 0.0f, 0.0f, 1.0f };
	}

	return {
		quaternion.x / length,
		quaternion.y / length,
		quaternion.z / length,
		quaternion.w / length
	};
}

Quaternion Slerp(
	const Quaternion& start,
	const Quaternion& end,
	float t
) {
	t = std::clamp(t, 0.0f, 1.0f);
	Quaternion normalizedStart = Normalize(start);
	Quaternion normalizedEnd = Normalize(end);

	float dot =
		normalizedStart.x * normalizedEnd.x +
		normalizedStart.y * normalizedEnd.y +
		normalizedStart.z * normalizedEnd.z +
		normalizedStart.w * normalizedEnd.w;

	if (dot < 0.0f) {
		normalizedEnd.x = -normalizedEnd.x;
		normalizedEnd.y = -normalizedEnd.y;
		normalizedEnd.z = -normalizedEnd.z;
		normalizedEnd.w = -normalizedEnd.w;
		dot = -dot;
	}

	if (dot > 0.9995f) {
		return Normalize({
			normalizedStart.x +
				(normalizedEnd.x - normalizedStart.x) * t,
			normalizedStart.y +
				(normalizedEnd.y - normalizedStart.y) * t,
			normalizedStart.z +
				(normalizedEnd.z - normalizedStart.z) * t,
			normalizedStart.w +
				(normalizedEnd.w - normalizedStart.w) * t
		});
	}

	const float theta = std::acos(std::clamp(dot, -1.0f, 1.0f));
	const float sinTheta = std::sin(theta);
	const float startWeight = std::sin((1.0f - t) * theta) / sinTheta;
	const float endWeight = std::sin(t * theta) / sinTheta;

	return {
		normalizedStart.x * startWeight + normalizedEnd.x * endWeight,
		normalizedStart.y * startWeight + normalizedEnd.y * endWeight,
		normalizedStart.z * startWeight + normalizedEnd.z * endWeight,
		normalizedStart.w * startWeight + normalizedEnd.w * endWeight
	};
}

Quaternion MakeIdentityQuaternion() {
	return { 0.0f, 0.0f, 0.0f, 1.0f };
}

Quaternion MakeQuaternionFromRotationMatrix(const Matrix4x4& matrix) {
	const float trace =
		matrix.m[0][0] + matrix.m[1][1] + matrix.m[2][2];
	Quaternion result{};
	if (trace > 0.0f) {
		const float s = std::sqrt(trace + 1.0f) * 2.0f;
		result.w = 0.25f * s;
		result.x = (matrix.m[1][2] - matrix.m[2][1]) / s;
		result.y = (matrix.m[2][0] - matrix.m[0][2]) / s;
		result.z = (matrix.m[0][1] - matrix.m[1][0]) / s;
	} else if (
		matrix.m[0][0] > matrix.m[1][1] &&
		matrix.m[0][0] > matrix.m[2][2]
	) {
		const float s = std::sqrt(
			1.0f + matrix.m[0][0] - matrix.m[1][1] -
			matrix.m[2][2]
		) * 2.0f;
		result.w = (matrix.m[1][2] - matrix.m[2][1]) / s;
		result.x = 0.25f * s;
		result.y = (matrix.m[0][1] + matrix.m[1][0]) / s;
		result.z = (matrix.m[2][0] + matrix.m[0][2]) / s;
	} else if (matrix.m[1][1] > matrix.m[2][2]) {
		const float s = std::sqrt(
			1.0f + matrix.m[1][1] - matrix.m[0][0] -
			matrix.m[2][2]
		) * 2.0f;
		result.w = (matrix.m[2][0] - matrix.m[0][2]) / s;
		result.x = (matrix.m[0][1] + matrix.m[1][0]) / s;
		result.y = 0.25f * s;
		result.z = (matrix.m[1][2] + matrix.m[2][1]) / s;
	} else {
		const float s = std::sqrt(
			1.0f + matrix.m[2][2] - matrix.m[0][0] -
			matrix.m[1][1]
		) * 2.0f;
		result.w = (matrix.m[0][1] - matrix.m[1][0]) / s;
		result.x = (matrix.m[2][0] + matrix.m[0][2]) / s;
		result.y = (matrix.m[1][2] + matrix.m[2][1]) / s;
		result.z = 0.25f * s;
	}
	return Normalize(result);
}

Quaternion MakeQuaternionFromEuler(const Vector3& rotate) {
	const Matrix4x4 matrix = MakeAffineMatrix(
		{ 1.0f, 1.0f, 1.0f },
		rotate,
		{ 0.0f, 0.0f, 0.0f }
	);
	return MakeQuaternionFromRotationMatrix(matrix);
}

Vector3 MakeEulerFromQuaternion(const Quaternion& quaternion) {
	const Matrix4x4 matrix = MakeRotateMatrix(quaternion);
	Vector3 scale{};
	Vector3 rotate{};
	Vector3 translate{};
	if (DecomposeAffineMatrix(matrix, scale, rotate, translate)) {
		return rotate;
	}
	return {};
}

Quaternion MakeLookRotationQuaternion(
	const Vector3& forward,
	const Vector3& up
) {
	Vector3 normalizedForward = Math::Normalize(forward);
	if (Math::Length(normalizedForward) < 0.000001f) {
		return MakeIdentityQuaternion();
	}

	Vector3 projectedUp = up;
	if (Math::Length(projectedUp) < 0.000001f) {
		projectedUp = { 0.0f, 1.0f, 0.0f };
	}
	projectedUp = Math::Subtract(
		projectedUp,
		Math::Multiply(
			normalizedForward,
			Math::Dot(projectedUp, normalizedForward)
		)
	);
	if (Math::Length(projectedUp) < 0.000001f) {
		projectedUp = std::abs(normalizedForward.y) < 0.95f
			? Vector3{ 0.0f, 1.0f, 0.0f }
			: Vector3{ 1.0f, 0.0f, 0.0f };
		projectedUp = Math::Subtract(
			projectedUp,
			Math::Multiply(
				normalizedForward,
				Math::Dot(projectedUp, normalizedForward)
			)
		);
	}
	projectedUp = Math::Normalize(projectedUp);

	Vector3 right = Math::Normalize(
		Math::Cross(projectedUp, normalizedForward)
	);
	if (Math::Length(right) < 0.000001f) {
		right = { 1.0f, 0.0f, 0.0f };
	}
	const Vector3 correctedUp = Math::Normalize(
		Math::Cross(normalizedForward, right)
	);

	Matrix4x4 matrix = MakeIdentity4x4();
	matrix.m[0][0] = right.x;
	matrix.m[0][1] = right.y;
	matrix.m[0][2] = right.z;
	matrix.m[1][0] = correctedUp.x;
	matrix.m[1][1] = correctedUp.y;
	matrix.m[1][2] = correctedUp.z;
	matrix.m[2][0] = normalizedForward.x;
	matrix.m[2][1] = normalizedForward.y;
	matrix.m[2][2] = normalizedForward.z;
	return MakeQuaternionFromRotationMatrix(matrix);
}

Matrix4x4 MakeRotateMatrix(const Quaternion& quaternion) {
	const Quaternion q = Normalize(quaternion);
	Matrix4x4 result = MakeIdentity4x4();

	result.m[0][0] = 1.0f - 2.0f * (q.y * q.y + q.z * q.z);
	result.m[0][1] = 2.0f * (q.x * q.y + q.z * q.w);
	result.m[0][2] = 2.0f * (q.x * q.z - q.y * q.w);
	result.m[1][0] = 2.0f * (q.x * q.y - q.z * q.w);
	result.m[1][1] = 1.0f - 2.0f * (q.x * q.x + q.z * q.z);
	result.m[1][2] = 2.0f * (q.y * q.z + q.x * q.w);
	result.m[2][0] = 2.0f * (q.x * q.z + q.y * q.w);
	result.m[2][1] = 2.0f * (q.y * q.z - q.x * q.w);
	result.m[2][2] = 1.0f - 2.0f * (q.x * q.x + q.y * q.y);

	return result;
}

