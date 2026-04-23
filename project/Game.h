#pragma once
#include <vector>
#include "engine/audio/Audio.h"

class D3DResourceLeadChecker;
class WinApp;
class DirectXCommon;
class Input;
class SpriteCommon;
class Sprite;
class Camera;
class Object3dCommon;
class Object3d;
class SrvManager;
class ImGuiManager;
class ParticleCommon;
class ParticleManager;
class ParticleEmitter;

class Game {
public:
	// 初期化
	void Initialize();

	// 終了
	void Finalize();

	// 毎フレーム更新
	void Update();

	// 描画
	void Draw();

	// 終了要求
	bool IsEndRequest() const {
		return isEndRequest_;
	}

private:
	D3DResourceLeadChecker* checker_ = nullptr;

	WinApp* winApp_ = nullptr;
	DirectXCommon* dxCommon_ = nullptr;
	Input* input_ = nullptr;

	SpriteCommon* spriteCommon_ = nullptr;
	Camera* camera_ = nullptr;
	Object3dCommon* object3dCommon_ = nullptr;
	SrvManager* srvManager_ = nullptr;
	ImGuiManager* imguiManager_ = nullptr;
	Audio* audio_ = nullptr;

	ParticleCommon* particleCommon_ = nullptr;
	ParticleManager* particleManager_ = nullptr;

	bool isEndRequest_ = false;
};