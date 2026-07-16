// 役割: 回転を表すQuaternionと補間操作を定義する。
#pragma once

#include "Matrix4x4.h"

struct Quaternion {
	float x;
	float y;
	float z;
	float w;
};

Quaternion Normalize(const Quaternion& quaternion);
Quaternion Slerp(
	const Quaternion& start,
	const Quaternion& end,
	float t
);
Quaternion MakeIdentityQuaternion();
Quaternion MakeQuaternionFromRotationMatrix(const Matrix4x4& matrix);
Quaternion MakeQuaternionFromEuler(const Vector3& rotate);
Vector3 MakeEulerFromQuaternion(const Quaternion& quaternion);
Quaternion MakeLookRotationQuaternion(
	const Vector3& forward,
	const Vector3& up
);
Matrix4x4 MakeRotateMatrix(const Quaternion& quaternion);
