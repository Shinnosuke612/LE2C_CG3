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
