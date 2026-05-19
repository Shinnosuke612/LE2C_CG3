#pragma once

class D3DResourceLeadChecker;
class WinApp;
class DirectXCommon;
class Input;
class SrvManager;
class ImGuiManager;
class Audio;
class ParticleCommon;
class Object3dCommon;
class SpriteCommon;
class Camera;

class Framework {
public:
	// 仮想デストラクタ
	virtual ~Framework() = default;

public:
	// 実行
	void Run();

	// 初期化
	virtual void Initialize();

	// 終了
	virtual void Finalize();

	// 毎フレーム更新
	virtual void Update();

	// 描画
	virtual void Draw() = 0;

	// 終了チェック
	virtual bool IsEndRequest() {
		return endRequest_;
	}

protected:
	bool endRequest_ = false;

	D3DResourceLeadChecker* checker_ = nullptr;
	WinApp* winApp_ = nullptr;
	DirectXCommon* dxCommon_ = nullptr;
	Input* input_ = nullptr;
	SrvManager* srvManager_ = nullptr;
	ImGuiManager* imguiManager_ = nullptr;
	Audio* audio_ = nullptr;
	ParticleCommon* particleCommon_ = nullptr;
};