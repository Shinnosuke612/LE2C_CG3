// 役割: ベクトル、角度、補間に共通して使う数学関数を定義する。
#pragma once
#include "Vector3.h"
//計算関数
namespace Math{
	float Length(const Vector3& v);

	Vector3 Normalize(const Vector3& v);

	Vector3 Add(const Vector3& v1, const Vector3& v2);
	Vector3 Subtract(const Vector3& v1, const Vector3& v2);
	Vector3 Multiply(const Vector3& v, float scalar);
	float Clamp01(float value);
	float Lerp(float a, float b, float t);
	Vector3 Lerp(const Vector3& a, const Vector3& b, float t);
	float NormalizeAngle(float angle);
	float LerpAngle(float a, float b, float t);
	float SmoothStep(float t);
	float EaseOutCubic(float t);
	float Dot(const Vector3& v1, const Vector3& v2);
	Vector3 Cross(const Vector3& v1, const Vector3& v2);

}
