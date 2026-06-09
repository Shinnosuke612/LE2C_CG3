#include "Input.h"

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
	return (mouseState_.rgbButtons[index] & 0x80) != 0 &&
		(previousMouseState_.rgbButtons[index] & 0x80) == 0;
}

bool Input::ReleaseMouse(MouseButton button) const {
	const uint8_t index = static_cast<uint8_t>(button);
	return (mouseState_.rgbButtons[index] & 0x80) == 0 &&
		(previousMouseState_.rgbButtons[index] & 0x80) != 0;
}
