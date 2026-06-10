#include "Quaternion.h"

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

