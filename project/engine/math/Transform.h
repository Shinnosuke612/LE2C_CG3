// 役割: 位置、回転、拡縮をまとめたローカルTransformを定義する。
#pragma once
#include "Vector3.h"
#include "Quaternion.h"

struct EulerTransform {
	Vector3 scale = { 1.0f, 1.0f, 1.0f };
	Vector3 rotate = { 0.0f, 0.0f, 0.0f };
	Vector3 translate = { 0.0f, 0.0f, 0.0f };
	bool useQuaternionRotation = false;
	Quaternion quaternionRotate = { 0.0f, 0.0f, 0.0f, 1.0f };
};

struct QuaternionTransform {
	Vector3 scale = { 1.0f, 1.0f, 1.0f };
	Quaternion rotate = { 0.0f, 0.0f, 0.0f, 1.0f };
	Vector3 translate = { 0.0f, 0.0f, 0.0f };
};

// Existing gameplay code still uses Transform as an Euler transform.
using Transform = EulerTransform;
