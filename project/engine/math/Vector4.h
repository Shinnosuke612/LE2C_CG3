// 役割: 色や4次元ベクトルに使う値型を定義する。
#pragma once
struct Vector4 {
	float x, y, z, w;

	Vector4& operator*=(float scalar) {
		x *= scalar;
		y *= scalar;
		z *= scalar;
		w *= scalar;
		return *this;
	}
};
