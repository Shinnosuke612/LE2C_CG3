#include "Matrix4x4.h"

Matrix4x4 Multiply(const Matrix4x4& m1, const Matrix4x4& m2) {
	Matrix4x4 result = {};

	for (int i = 0; i < 4; i++) {
		for (int j = 0; j < 4; j++) {
			result.m[i][j] = m1.m[i][0] * m2.m[0][j] + m1.m[i][1] * m2.m[1][j] + m1.m[i][2] * m2.m[2][j] + m1.m[i][3] * m2.m[3][j];
		}
	}

	return result;
}

Matrix4x4 MakeIdentity4x4() {
	Matrix4x4 result = {};

	for (int i = 0; i < 4; i++) {
		for (int j = 0; j < 4; j++) {
			if (i == j) {
				result.m[i][j] = 1.0f;
			} else {
				result.m[i][j] = 0.0f;
			}
		}
	}

	return result;
}

Matrix4x4 MakeRotateXMatrix(float radian) {
	Matrix4x4 result;

	for (int i = 0; i < 4; i++) {
		for (int j = 0; j < 4; j++) {
			if (i == j) {
				result.m[i][j] = 1.0f;
			} else {
				result.m[i][j] = 0.0f;
			}
		}
	}

	result.m[1][1] = std::cos(radian);
	result.m[1][2] = std::sin(radian);
	result.m[2][1] = -std::sin(radian);
	result.m[2][2] = std::cos(radian);

	return result;

}

Matrix4x4 MakeRotateYMatrix(float radian) {
	Matrix4x4 result;

	for (int i = 0; i < 4; i++) {
		for (int j = 0; j < 4; j++) {
			if (i == j) {
				result.m[i][j] = 1.0f;
			} else {
				result.m[i][j] = 0.0f;
			}
		}
	}

	result.m[0][0] = std::cos(radian);
	result.m[2][0] = std::sin(radian);
	result.m[0][2] = -std::sin(radian);
	result.m[2][2] = std::cos(radian);

	return result;

}

Matrix4x4 MakeRotateZMatrix(float radian) {
	Matrix4x4 result;

	for (int i = 0; i < 4; i++) {
		for (int j = 0; j < 4; j++) {
			if (i == j) {
				result.m[i][j] = 1.0f;
			} else {
				result.m[i][j] = 0.0f;
			}
		}
	}

	result.m[0][0] = std::cos(radian);
	result.m[0][1] = std::sin(radian);
	result.m[1][0] = -std::sin(radian);
	result.m[1][1] = std::cos(radian);

	return result;

}

Matrix4x4 MakeScaleMatrix(const Vector3& scale) {
	Matrix4x4 result = {};
	result.m[0][0] = scale.x;
	result.m[1][1] = scale.y;
	result.m[2][2] = scale.z;
	result.m[3][3] = 1.0f;
	return result;
}

Matrix4x4 MakeTranslateMatrix(const Vector3& translate) {
	Matrix4x4 result = MakeIdentity4x4();
	result.m[3][0] = translate.x;
	result.m[3][1] = translate.y;
	result.m[3][2] = translate.z;
	return result;
}


Matrix4x4 MakeAffineMatrix(const Vector3& scale, const Vector3& rotate, const Vector3& translate) {
	Matrix4x4 scaleMatrix = MakeScaleMatrix(scale);

	Matrix4x4 rotateXMatrix = MakeRotateXMatrix(rotate.x);
	Matrix4x4 rotateYMatrix = MakeRotateYMatrix(rotate.y);
	Matrix4x4 rotateZMatrix = MakeRotateZMatrix(rotate.z);
	Matrix4x4 rotateMatrix = Multiply(Multiply(rotateZMatrix, rotateYMatrix), rotateXMatrix);

	Matrix4x4 translateMatrix = MakeTranslateMatrix(translate);

	// 最終行列：S → R → T の順に掛ける
	return Multiply(Multiply(scaleMatrix, rotateMatrix), translateMatrix);
}

Matrix4x4 MakePerspectiveFovMatrix(float fovY, float aspectRatio, float nearClip, float farClip) {
	Matrix4x4 result;

	for (int i = 0; i < 4; i++) {
		for (int j = 0; j < 4; j++) {
			result.m[i][j] = 0;
		}
	}

	result.m[0][0] = 1 / aspectRatio * (1 / tanf(fovY / 2));
	result.m[1][1] = (1 / tanf(fovY / 2));
	result.m[2][2] = farClip / (farClip - nearClip);
	result.m[2][3] = 1;
	result.m[3][2] = -nearClip * farClip / (farClip - nearClip);

	return result;
}

Matrix4x4 MakeOrthographicMatrix(float left, float top, float right, float bottom, float nearClip, float farClip) {
	Matrix4x4 result;

	for (int i = 0; i < 4; i++) {
		for (int j = 0; j < 4; j++) {
			result.m[i][j] = 0;
		}
	}

	result.m[0][0] = 2 / (right - left);
	result.m[1][1] = 2 / (top - bottom);
	result.m[2][2] = 2 / (farClip - nearClip);
	result.m[3][0] = (left + right) / (left - right);
	result.m[3][1] = (top + bottom) / (bottom - top);
	result.m[3][2] = nearClip / (nearClip - farClip);
	result.m[3][3] = 1;

	return result;
}

Matrix4x4 MakeViewportMatrix(float left, float top, float width, float height, float minDepth, float maxDepth) {
	Matrix4x4 result;

	for (int i = 0; i < 4; i++) {
		for (int j = 0; j < 4; j++) {
			result.m[i][j] = 0;
		}
	}

	result.m[0][0] = width / 2;
	result.m[1][1] = -height / 2;
	result.m[2][2] = maxDepth - minDepth;
	result.m[3][0] = left + width / 2;
	result.m[3][1] = top + height / 2;
	result.m[3][2] = minDepth;
	result.m[3][3] = 1;

	return result;
}

// 3x3の行列式を求める補助関数
float Minor(const Matrix4x4& mat, int row0, int row1, int row2, int col0, int col1, int col2) {
	return
		mat.m[row0][col0] * (mat.m[row1][col1] * mat.m[row2][col2] - mat.m[row1][col2] * mat.m[row2][col1]) -
		mat.m[row0][col1] * (mat.m[row1][col0] * mat.m[row2][col2] - mat.m[row1][col2] * mat.m[row2][col0]) +
		mat.m[row0][col2] * (mat.m[row1][col0] * mat.m[row2][col1] - mat.m[row1][col1] * mat.m[row2][col0]);
}

// 余因子を求める（i行j列のcofactor）
float Cofactor(const Matrix4x4& m, int row, int col) {
	int r[3], c[3];
	for (int i = 0, ri = 0; i < 4; ++i)
		if (i != row) r[ri++] = i;
	for (int j = 0, ci = 0; j < 4; ++j)
		if (j != col) c[ci++] = j;

	float minor = Minor(m, r[0], r[1], r[2], c[0], c[1], c[2]);
	return ((row + col) % 2 == 0 ? 1.0f : -1.0f) * minor;
}

// 4x4行列の行列式（1行目で余因子展開）
float Determinant(const Matrix4x4& m) {
	float det = 0.0f;
	for (int j = 0; j < 4; ++j) {
		det += m.m[0][j] * Cofactor(m, 0, j);
	}
	return det;
}

// 逆行列を求める関数（存在しない場合はゼロ行列を返す）
Matrix4x4 Inverse(const Matrix4x4& m) {
	Matrix4x4 result = {};

	float det = Determinant(m);
	if (std::fabs(det) < 1e-6f) {
		// 行列式が0 → 逆行列なし
		return result;
	}

	float invDet = 1.0f / det;

	// 逆行列 = 転置(Cofactor行列) × 1/det
	for (int i = 0; i < 4; ++i)
		for (int j = 0; j < 4; ++j)
			result.m[i][j] = Cofactor(m, j, i) * invDet;

	return result;
}
