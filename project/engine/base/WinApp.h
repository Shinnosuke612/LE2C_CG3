#pragma once
#include <Windows.h>
#include <cstdint>
class WinApp{
public:

	//静的メンバ関数
	static LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);

	//初期化
	void Initialize();
	//終了
	void Finalize();
	//メッセージの処理
	bool ProcessMessage();

	//クライアント領域のサイズ
	static const int32_t kClientWidth = 1280;
	static const int32_t kClientHeight = 720;

	//getter
	HWND GetHwnd() const{
		return hwnd;
	}
	HINSTANCE GetHInstance() const{
		return wc.hInstance;
	}

	// ボーダーレスフルスクリーン切り替え
	void ToggleFullscreen();
	void SetFullscreen(bool fullscreen);

	bool IsFullscreen() const { return isFullscreen_; }

	uint32_t GetClientWidth() const;
	uint32_t GetClientHeight() const;

private:
	//ウィンドウハンドル
	HWND hwnd = nullptr;
	//ウィンドウクラスの設定
	WNDCLASS wc{};

	bool isFullscreen_ = false;
	RECT windowedRect_{};
	LONG_PTR windowedStyle_ = WS_OVERLAPPEDWINDOW;
};

