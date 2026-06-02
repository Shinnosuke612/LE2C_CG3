// engine/base/ImGuiManager.h
#pragma once
class WinApp;
class DirectXCommon;
class SrvManager;

class ImGuiManager{
public:
	// 初期化
	void Initialize(WinApp* winApp, DirectXCommon* dxCommon, SrvManager* srvManager);

	// フレーム開始
	void BeginFrame();

	// フレーム終了
	void EndFrame();

	// 終了処理
	void Finalize();

private:
	// ドッキング用の土台を作成
	void CreateDockSpace();

	WinApp* winApp_ = nullptr;
	DirectXCommon* dxCommon_ = nullptr;
	SrvManager* srvManager_ = nullptr;
};
