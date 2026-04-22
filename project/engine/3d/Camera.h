#pragma once
#include "../math/Vector2.h"
#include "../math/Vector3.h"
#include "../math/Vector4.h"
#include "../math/Matrix4x4.h"
#include "../math/Transform.h"
#include "../base/WinApp.h"
class Camera{

public://メンバ関数
	//更新
	void Update();

	//デフォルトコンストラクタ
	Camera();

	// ---- setter ----
	void SetRotate(const Vector3& rotate){ transform.rotate = rotate; }
	void SetTranslate(const Vector3& translate){ transform.translate = translate; }

	void SetFovY(float fovY){ fovY_ = fovY; }
	void SetAspectRatio(float aspectRatio){ aspectRatio_ = aspectRatio; }
	void SetNearClip(float nearClip){ nearClip_ = nearClip; }
	void SetFarClip(float farClip){ farClip_ = farClip; }

	// ---- getter ----
	const Matrix4x4& GetWorldMatrix() const{ return worldMatrix; }
	const Matrix4x4& GetViewMatrix() const{ return viewMatrix; }
	const Matrix4x4& GetProjectionMatrix() const{ return projectionMatrix; }
	const Matrix4x4& GetViewProjectionMatrix() const{ return viewProjectionMatrix; }

	const Vector3& GetRotate() const{ return transform.rotate; }
	const Vector3& GetTranslate() const{ return transform.translate; }

	float GetFovY() const{ return fovY_; }
	float GetAspectRatio() const{ return aspectRatio_; }
	float GetNearClip() const{ return nearClip_; }
	float GetFarClip() const{ return farClip_; }
private:
	Transform transform;
	Matrix4x4 worldMatrix;
	Matrix4x4 viewMatrix;
	Matrix4x4 projectionMatrix;
	float fovY_ = 0.45f;                                  // 水平方向視野角…ではなく通常これは「縦FOV」(rad)
	float aspectRatio_ = float(WinApp::kClientWidth) / float(WinApp::kClientHeight);
	float nearClip_ = 0.1f;
	float farClip_ = 100.0f;
	Matrix4x4 viewProjectionMatrix;
};

