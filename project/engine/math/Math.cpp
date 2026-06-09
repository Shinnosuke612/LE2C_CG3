#include "Math.h"
#define _USE_MATH_DEFINES
#include <cmath>
namespace Math{
	float Math::Length(const Vector3& v){
		return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
	}

	Vector3 Math::Normalize(const Vector3& v){
		float len = Length(v);
		if(len < 1e-6f){
			return { 0.0f, 0.0f, 0.0f };
		}
		float inv = 1.0f / len;
		return { v.x * inv, v.y * inv, v.z * inv };
	}

	Vector3 Math::Add(const Vector3& v1, const Vector3& v2) {
		return {
			v1.x + v2.x,
			v1.y + v2.y,
			v1.z + v2.z
		};
	}

	Vector3 Math::Subtract(const Vector3& v1, const Vector3& v2) {
		return {
			v1.x - v2.x,
			v1.y - v2.y,
			v1.z - v2.z
		};
	}

	Vector3 Math::Multiply(const Vector3& v, float scalar) {
		return {
			v.x * scalar,
			v.y * scalar,
			v.z * scalar
		};
	}

	float Math::Dot(const Vector3& v1, const Vector3& v2) {
		return v1.x * v2.x + v1.y * v2.y + v1.z * v2.z;
	}

	Vector3 Math::Cross(const Vector3& v1, const Vector3& v2) {
		return {
			v1.y * v2.z - v1.z * v2.y,
			v1.z * v2.x - v1.x * v2.z,
			v1.x * v2.y - v1.y * v2.x
		};
	}
}
