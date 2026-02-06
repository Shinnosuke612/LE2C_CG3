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
}