// 役割: DirectInputとXInputから入力状態を更新する。
#include "Input.h"

#include <cmath>

#pragma comment(lib,"dinput8.lib")
#pragma comment(lib,"dxguid.lib")

Input* Input::instance_ = nullptr;

Input* Input::GetInstance() {
	if (instance_ == nullptr) {
		instance_ = new Input();
	}
	return instance_;
}

void Input::Finalize() {
	if (mouse_) {
		mouse_->Unacquire();
		mouse_->Release();
		mouse_ = nullptr;
	}

	if (keyboard) {
		keyboard->Unacquire();
		keyboard->Release();
		keyboard = nullptr;
	}

	if (directInput) {
		directInput->Release();
		directInput = nullptr;
	}

	winApp_ = nullptr;
	SetCursorCapture(false);

	delete instance_;
	instance_ = nullptr;
}

void Input::Initialize(WinApp* winApp){

	winApp_ = winApp;
	HRESULT hr;

	//DirectInputの初期化
	hr = DirectInput8Create(winApp_->GetHInstance(), DIRECTINPUT_VERSION, IID_IDirectInput8, (void**)&directInput, nullptr);
	assert(SUCCEEDED(hr));
	hr = directInput->CreateDevice(GUID_SysKeyboard, &keyboard, NULL);
	assert(SUCCEEDED(hr));
	//入力データの形式のセット
	hr = keyboard->SetDataFormat(&c_dfDIKeyboard);
	assert(SUCCEEDED(hr));
	hr = keyboard->SetCooperativeLevel(
		winApp_->GetHwnd(), DISCL_FOREGROUND | DISCL_NONEXCLUSIVE | DISCL_NOWINKEY);
	assert(SUCCEEDED(hr));

	hr = directInput->CreateDevice(GUID_SysMouse, &mouse_, nullptr);
	assert(SUCCEEDED(hr));
	hr = mouse_->SetDataFormat(&c_dfDIMouse2);
	assert(SUCCEEDED(hr));
	hr = mouse_->SetCooperativeLevel(
		winApp_->GetHwnd(), DISCL_FOREGROUND | DISCL_NONEXCLUSIVE);
	assert(SUCCEEDED(hr));
}

void Input::Update(){

	memcpy(keyPre, key, sizeof(key));
	memset(key, 0, sizeof(key));
	previousMouseState_ = mouseState_;
	mouseState_ = {};

	//キーボード情報の取得開始
	HRESULT hr = keyboard->Acquire();
	//全キーの入力情報を取得する
	if (SUCCEEDED(hr)) {
		hr = keyboard->GetDeviceState(sizeof(key), key);
		if (FAILED(hr)) {
			memset(key, 0, sizeof(key));
		}
	}

	hr = mouse_->Acquire();
	if (SUCCEEDED(hr)) {
		hr = mouse_->GetDeviceState(sizeof(mouseState_), &mouseState_);
		if (FAILED(hr)) {
			mouseState_ = {};
		}
	}

	POINT mousePoint{};
	if (GetCursorPos(&mousePoint)) {
		ScreenToClient(winApp_->GetHwnd(), &mousePoint);
		mousePosition_ = {
			static_cast<float>(mousePoint.x),
			static_cast<float>(mousePoint.y)
		};
	}
	ApplyCursorCapture();
}

bool Input::PushKey(BYTE keyNumber){
	if(key[keyNumber]){
		return true;
	}
	return false;
}

bool Input::TriggerKey(BYTE keyNumber) {
	if (PushKey(keyNumber) && !keyPre[keyNumber]) {
		return true;
	}
	return false;
}

bool Input::PushMouse(MouseButton button) const {
	const uint8_t index = static_cast<uint8_t>(button);
	return (mouseState_.rgbButtons[index] & 0x80) != 0;
}

bool Input::TriggerMouse(MouseButton button) const {
	const uint8_t index = static_cast<uint8_t>(button);
	return
		(mouseState_.rgbButtons[index] & 0x80) != 0 &&
		(previousMouseState_.rgbButtons[index] & 0x80) == 0;
}

void Input::SetCursorCapture(bool enabled) {
	if (cursorCaptured_ == enabled) {
		return;
	}

	cursorCaptured_ = enabled;
	if (!cursorCaptured_) {
		ClipCursor(nullptr);
		hasCursorCaptureRect_ = false;
		hasAppliedCursorCaptureRect_ = false;
		if (cursorHidden_) {
			while (ShowCursor(TRUE) < 0) {
			}
			cursorHidden_ = false;
		}
		return;
	}

	if (!cursorHidden_) {
		while (ShowCursor(FALSE) >= 0) {
		}
		cursorHidden_ = true;
	}
	ApplyCursorCapture();
}

void Input::SetCursorCaptureRect(
	float minX,
	float minY,
	float maxX,
	float maxY
) {
	constexpr LONG kCaptureInset = 2;
	const LONG rawLeft = static_cast<LONG>(std::ceil(minX));
	const LONG rawTop = static_cast<LONG>(std::ceil(minY));
	const LONG rawRight = static_cast<LONG>(std::floor(maxX));
	const LONG rawBottom = static_cast<LONG>(std::floor(maxY));
	const LONG horizontalInset =
		rawRight - rawLeft > kCaptureInset * 2 ? kCaptureInset : 0;
	const LONG verticalInset =
		rawBottom - rawTop > kCaptureInset * 2 ? kCaptureInset : 0;
	const RECT newRect = {
		rawLeft + horizontalInset,
		rawTop + verticalInset,
		rawRight - horizontalInset,
		rawBottom - verticalInset
	};
	const bool sameRect =
		hasCursorCaptureRect_ &&
		cursorCaptureRect_.left == newRect.left &&
		cursorCaptureRect_.top == newRect.top &&
		cursorCaptureRect_.right == newRect.right &&
		cursorCaptureRect_.bottom == newRect.bottom;
	cursorCaptureRect_ = newRect;
	hasCursorCaptureRect_ = true;
	if (cursorCaptured_ && !sameRect) {
		ApplyCursorCapture();
	}
}

void Input::ApplyCursorCapture() {
	if (!cursorCaptured_ || !winApp_ || !winApp_->GetHwnd()) {
		return;
	}

	RECT clientRect = cursorCaptureRect_;
	if (!hasCursorCaptureRect_) {
		GetClientRect(winApp_->GetHwnd(), &clientRect);
	}
	POINT topLeft{ clientRect.left, clientRect.top };
	POINT bottomRight{ clientRect.right, clientRect.bottom };
	ClientToScreen(winApp_->GetHwnd(), &topLeft);
	ClientToScreen(winApp_->GetHwnd(), &bottomRight);
	RECT screenRect{
		topLeft.x,
		topLeft.y,
		bottomRight.x,
		bottomRight.y
	};
	if (
		hasAppliedCursorCaptureRect_ &&
		appliedCursorCaptureRect_.left == screenRect.left &&
		appliedCursorCaptureRect_.top == screenRect.top &&
		appliedCursorCaptureRect_.right == screenRect.right &&
		appliedCursorCaptureRect_.bottom == screenRect.bottom
	) {
		return;
	}
	ClipCursor(&screenRect);
	appliedCursorCaptureRect_ = screenRect;
	hasAppliedCursorCaptureRect_ = true;
}
