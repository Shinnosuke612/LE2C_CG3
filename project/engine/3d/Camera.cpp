#define NOMINMAX
#include "Camera.h"

#include "../externals/imgui/imgui.h"
#include "../io/Input.h"
#include "../math/Math.h"

#include <algorithm>
#include <cmath>

void Camera::Update() {

	if (isOrbitMode_) {
		UpdateOrbitMouseControl();
		UpdateOrbitTransform();
	}
	else {
		worldMatrix = MakeAffineMatrix(transform.scale, transform.rotate, transform.translate);
		viewMatrix = Inverse(worldMatrix);
	}

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

	// orbitTarget_ を中心にしたカメラ位置
	transform.translate = {
		orbitTarget_.x + sinYaw * cosPitch * orbitDistance_,
		orbitTarget_.y + sinPitch * orbitDistance_,
		orbitTarget_.z - cosYaw * cosPitch * orbitDistance_
	};

	// 回転角から向きを作らず、必ず target を見る方向を作る
	Vector3 forward = Math::Normalize(Math::Subtract(orbitTarget_, transform.translate));
	if (Math::Length(forward) < 0.000001f) {
		forward = { 0.0f, 0.0f, 1.0f };
	}

	const Vector3 worldUp = { 0.0f, 1.0f, 0.0f };

	Vector3 right = Math::Normalize(Math::Cross(worldUp, forward));
	if (Math::Length(right) < 0.000001f) {
		right = { 1.0f, 0.0f, 0.0f };
	}

	Vector3 up = Math::Cross(forward, right);

	worldMatrix = MakeIdentity4x4();

	worldMatrix.m[0][0] = right.x;
	worldMatrix.m[0][1] = right.y;
	worldMatrix.m[0][2] = right.z;

	worldMatrix.m[1][0] = up.x;
	worldMatrix.m[1][1] = up.y;
	worldMatrix.m[1][2] = up.z;

	worldMatrix.m[2][0] = forward.x;
	worldMatrix.m[2][1] = forward.y;
	worldMatrix.m[2][2] = forward.z;

	worldMatrix.m[3][0] = transform.translate.x;
	worldMatrix.m[3][1] = transform.translate.y;
	worldMatrix.m[3][2] = transform.translate.z;
	worldMatrix.m[3][3] = 1.0f;

	viewMatrix = Inverse(worldMatrix);

	// ImGui表示用。実際のOrbit向きは worldMatrix で作っている
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
    , farClip_(1000.0f)
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

void Camera::UpdateOrbitMouseControl() {

#ifdef _DEBUG
	if (ImGui::GetCurrentContext() == nullptr) {
		// ImGui未初期化時もゲーム側のマウス入力は使用する
	}
	else if (ImGui::GetIO().WantCaptureMouse) {
		return;
	}
#endif

	Input* input = Input::GetInstance();
	const Vector2 mouseMove = input->GetMouseMove();
	const float mouseWheel = input->GetMouseWheel();

	const float rotateSpeed = 0.005f;
	const float zoomSpeed = 0.15f;
	const float panSpeed = orbitDistance_ * 0.0015f;

	// 右ドラッグ：中心点を保ったまま回転
	if (input->PushMouse(Input::MouseButton::Right)) {
		orbitYaw_ += mouseMove.x * rotateSpeed;
		orbitPitch_ += mouseMove.y * rotateSpeed;
	}

	// ホイール：ズーム
	if (mouseWheel != 0.0f) {
		orbitDistance_ *= (1.0f - mouseWheel * zoomSpeed);
		orbitDistance_ = std::clamp(orbitDistance_, orbitMinDistance_, 1000.0f);
	}

	// 中ドラッグ：中心点を平行移動
	if (input->PushMouse(Input::MouseButton::Middle)) {

		const float cosPitch = std::cos(orbitPitch_);
		const float sinPitch = std::sin(orbitPitch_);
		const float cosYaw = std::cos(orbitYaw_);
		const float sinYaw = std::sin(orbitYaw_);

		Vector3 eye = {
			orbitTarget_.x + sinYaw * cosPitch * orbitDistance_,
			orbitTarget_.y + sinPitch * orbitDistance_,
			orbitTarget_.z - cosYaw * cosPitch * orbitDistance_
		};

		Vector3 forward = Math::Normalize(Math::Subtract(orbitTarget_, eye));
		if (Math::Length(forward) < 0.000001f) {
			forward = { 0.0f, 0.0f, 1.0f };
		}

		const Vector3 worldUp = { 0.0f, 1.0f, 0.0f };

		Vector3 right = Math::Normalize(Math::Cross(worldUp, forward));
		if (Math::Length(right) < 0.000001f) {
			right = { 1.0f, 0.0f, 0.0f };
		}

		Vector3 up = Math::Cross(forward, right);

		Vector3 moveRight = Math::Multiply(right, -mouseMove.x * panSpeed);
		Vector3 moveUp = Math::Multiply(up, mouseMove.y * panSpeed);
		Vector3 move = Math::Add(moveRight, moveUp);

		orbitTarget_ = Math::Add(orbitTarget_, move);
	}
}
