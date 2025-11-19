#pragma once
#include <cassert>
#include <Windows.h>
#include "WinApp.h"
//入力
#define DIRECTINPUT_VERSION 0x0800
#include <dinput.h>
class Input{
public:
	//初期化
	void Initialize(WinApp* winApp);
	//更新
	void Update();

	bool PushKey(BYTE keyNumber);
	bool TriggerKey(BYTE keyNumber);
private:
	//キーボードデバイスの生成
	IDirectInputDevice8* keyboard = nullptr;
	BYTE key[256] = {};
	BYTE keyPre[256] = {};

	IDirectInput8* directInput = nullptr;
	//WindowAPI
	WinApp* winApp_ = nullptr;
};