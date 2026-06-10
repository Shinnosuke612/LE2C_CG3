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
Matrix4x4 MakeRotateMatrix(const Quaternion& quaternion);

