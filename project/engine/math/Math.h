#pragma once
#include "Vector3.h"
//計算関数
namespace Math{
	float Length(const Vector3& v);

	Vector3 Normalize(const Vector3& v);

	Vector3 Add(const Vector3& v1, const Vector3& v2);
	Vector3 Subtract(const Vector3& v1, const Vector3& v2);
	Vector3 Multiply(const Vector3& v, float scalar);
	Vector3 Cross(const Vector3& v1, const Vector3& v2);

}