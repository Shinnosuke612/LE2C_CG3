#define NOMINMAX
#include "Camera.h"

#include "../externals/imgui/imgui.h"

#include <algorithm>
#include <cmath>

void Camera::Update() {

    if (isOrbitMode_) {
        UpdateOrbitTransform();
    }

	worldMatrix = MakeAffineMatrix(transform.scale, transform.rotate, transform.translate);
	viewMatrix = Inverse(worldMatrix);
	projectionMatrix = MakePerspectiveFovMatrix(fovY_, aspectRatio_, nearClip_, farClip_);
	viewProjectionMatrix = Multiply(viewMatrix, projectionMatrix);
}

void Camera::UpdateOrbitTransform() {

	orbitDistance_ = std::max(orbitDistance_, orbitMinDistance_);
	orbitPitch_ = std::clamp(orbitPitch_, -orbitMaxPitch_, orbitMaxPitch_);

	const float cosPitch = std::cos(orbitPitch_);
	const float sinPitch = std::sin(orbitPitch_);
	const float cosYaw = std::cos(orbitYaw_);
	const float sinYaw = std::sin(orbitYaw_);

	transform.translate = {
		orbitTarget_.x + sinYaw * cosPitch * orbitDistance_,
		orbitTarget_.y + sinPitch * orbitDistance_,
		orbitTarget_.z - cosYaw * cosPitch * orbitDistance_
	};

	// このエンジンではカメラが +Z 方向を見る前提としている想定
	transform.rotate = {
		orbitPitch_,
		-orbitYaw_,
		0.0f
	};
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

void Camera::DrawImGui(const char* label) {

	constexpr float kRadToDeg = 57.2957795f;
	constexpr float kDegToRad = 0.0174532925f;

	ImGui::PushID(this);

	if (ImGui::TreeNode(label)) {

		ImGui::Checkbox("Orbit Mode", &isOrbitMode_);

		if (isOrbitMode_) {

			ImGui::DragFloat3("Target", &orbitTarget_.x, 0.1f);

			ImGui::DragFloat(
				"Distance",
				&orbitDistance_,
				0.1f,
				orbitMinDistance_,
				1000.0f
			);

			float yawDeg = orbitYaw_ * kRadToDeg;
			float pitchDeg = orbitPitch_ * kRadToDeg;

			if (ImGui::DragFloat("Yaw(deg)", &yawDeg, 0.5f)) {
				orbitYaw_ = yawDeg * kDegToRad;
			}

			if (ImGui::DragFloat("Pitch(deg)", &pitchDeg, 0.5f, -89.0f, 89.0f)) {
				orbitPitch_ = pitchDeg * kDegToRad;
			}

			if (ImGui::Button("Reset Orbit")) {
				orbitTarget_ = { 0.0f, 0.0f, 0.0f };
				orbitDistance_ = 10.0f;
				orbitYaw_ = 0.0f;
				orbitPitch_ = 0.0f;
			}
		}
		else {

			ImGui::DragFloat3("Translate", &transform.translate.x, 0.1f);

			Vector3 rotateDeg = {
				transform.rotate.x * kRadToDeg,
				transform.rotate.y * kRadToDeg,
				transform.rotate.z * kRadToDeg
			};

			if (ImGui::DragFloat3("Rotate(deg)", &rotateDeg.x, 0.5f)) {
				transform.rotate = {
					rotateDeg.x * kDegToRad,
					rotateDeg.y * kDegToRad,
					rotateDeg.z * kDegToRad
				};
			}
		}

		float fovYDeg = fovY_ * kRadToDeg;
		if (ImGui::DragFloat("FovY(deg)", &fovYDeg, 0.5f, 1.0f, 179.0f)) {
			fovY_ = fovYDeg * kDegToRad;
		}

		ImGui::DragFloat("NearClip", &nearClip_, 0.01f, 0.001f, 100.0f);
		ImGui::DragFloat("FarClip", &farClip_, 1.0f, 1.0f, 10000.0f);

		ImGui::Separator();
		ImGui::Text(
			"Position: %.2f, %.2f, %.2f",
			transform.translate.x,
			transform.translate.y,
			transform.translate.z
		);

		ImGui::TreePop();
	}

	ImGui::PopID();
}