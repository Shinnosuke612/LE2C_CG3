#include "Camera.h"

void Camera::Update(){
	Matrix4x4 worldMatrix = MakeAffineMatrix(transform.scale, transform.rotate, transform.translate);
	Matrix4x4 viewMatrix = Inverse(worldMatrix);
	projectionMatrix = MakePerspectiveFovMatrix(fovY_, aspectRatio_, nearClip_, farClip_);
	viewProjectionMatrix = Multiply(viewMatrix, projectionMatrix);

}

Camera::Camera()
    : transform{ {1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f} }
    , fovY_(0.45f)
    , aspectRatio_(float(WinApp::kClientWidth) / float(WinApp::kClientHeight))
    , nearClip_(0.1f)
    , farClip_(100.0f)
    , worldMatrix(MakeAffineMatrix(transform.scale, transform.rotate, transform.translate))
    , viewMatrix(Inverse(worldMatrix))
    , projectionMatrix(MakePerspectiveFovMatrix(fovY_, aspectRatio_, nearClip_, farClip_))
    , viewProjectionMatrix(Multiply(viewMatrix, projectionMatrix)){}
