// 役割: Mathで公開するベクトル演算と角度計算を実装する。
#include "Math.h"
#define _USE_MATH_DEFINES
#include <algorithm>
#include <cmath>
namespace Math{
	namespace {
		constexpr float kPi = 3.14159265358979323846f;
		constexpr float kTwoPi = kPi * 2.0f;
	}

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

	float Math::Clamp01(float value) {
		return std::clamp(value, 0.0f, 1.0f);
	}

	float Math::Lerp(float a, float b, float t) {
		const float clampedT = Clamp01(t);
		return a + (b - a) * clampedT;
	}

	Vector3 Math::Lerp(const Vector3& a, const Vector3& b, float t) {
		const float clampedT = Clamp01(t);
		return {
			Lerp(a.x, b.x, clampedT),
			Lerp(a.y, b.y, clampedT),
			Lerp(a.z, b.z, clampedT)
		};
	}

	float Math::NormalizeAngle(float angle) {
		while (angle > kPi) {
			angle -= kTwoPi;
		}
		while (angle < -kPi) {
			angle += kTwoPi;
		}
		return angle;
	}

	float Math::LerpAngle(float a, float b, float t) {
		return a + NormalizeAngle(b - a) * Clamp01(t);
	}

	float Math::SmoothStep(float t) {
		const float clampedT = Clamp01(t);
		return clampedT * clampedT * (3.0f - 2.0f * clampedT);
	}

	float Math::EaseOutCubic(float t) {
		const float inverse = 1.0f - Clamp01(t);
		return 1.0f - inverse * inverse * inverse;
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
