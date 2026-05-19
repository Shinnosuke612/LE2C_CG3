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
}

void Input::Update(){

	memcpy(keyPre, key, sizeof(key));
	//キーボード情報の取得開始
	keyboard->Acquire();
	//全キーの入力情報を取得する
	keyboard->GetDeviceState(sizeof(key), key);

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
