#pragma once
#include <cassert>
#include <Windows.h>
#include "../base/WinApp.h"
//入力
#define DIRECTINPUT_VERSION 0x0800
#include <dinput.h>
class Input {
private:
	static Input* instance_;

	Input() = default;
	~Input() = default;

	Input(const Input&) = delete;
	Input& operator=(const Input&) = delete;

public:
	// シングルトンインスタンスの取得
	static Input* GetInstance();

	// 終了
	void Finalize();

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