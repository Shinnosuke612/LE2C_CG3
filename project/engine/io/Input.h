#pragma once
#include <cassert>
#include <Windows.h>
#include <cstdint>
#include "../base/WinApp.h"
#include "../math/Vector2.h"
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
	enum class MouseButton : uint8_t {
		Left = 0,
		Right = 1,
		Middle = 2,
	};

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

	bool PushMouse(MouseButton button) const;
	bool TriggerMouse(MouseButton button) const;
	bool ReleaseMouse(MouseButton button) const;

	const Vector2& GetMousePosition() const { return mousePosition_; }
	Vector2 GetMouseMove() const {
		return {
			static_cast<float>(mouseState_.lX),
			static_cast<float>(mouseState_.lY)
		};
	}
	float GetMouseWheel() const {
		return static_cast<float>(mouseState_.lZ) / static_cast<float>(WHEEL_DELTA);
	}

private:
	//キーボードデバイスの生成
	IDirectInputDevice8* keyboard = nullptr;
	IDirectInputDevice8* mouse_ = nullptr;
	BYTE key[256] = {};
	BYTE keyPre[256] = {};
	DIMOUSESTATE2 mouseState_ = {};
	DIMOUSESTATE2 previousMouseState_ = {};
	Vector2 mousePosition_ = {};

	IDirectInput8* directInput = nullptr;
	//WindowAPI
	WinApp* winApp_ = nullptr;
};
