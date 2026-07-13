// 役割: 3次元座標と3次元ベクトルの値型を定義する。
#pragma once
struct Vector3 {
	float x, y, z;

	Vector3& operator*=(float scalar) {
		x *= scalar;
		y *= scalar;
		z *= scalar;
		return *this;
	}
};
