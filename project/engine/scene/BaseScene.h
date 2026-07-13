// 役割: すべてのシーンに共通する更新と描画のライフサイクルを定義する。
#pragma once

class SceneManager;

// シーン基底クラス
class BaseScene
{
public:
	// 仮想デストラクタ
	virtual ~BaseScene() = default;

public:
	// 初期化
	virtual void Initialize() = 0;

	// 終了
	virtual void Finalize() = 0;

	// 更新
	virtual void Update(float deltaTime) = 0;
	virtual void UpdatePaused() {}

	// 描画
	virtual void Draw() = 0;
	virtual void DrawForegroundEffects() {}
	virtual void DrawShadow() {}
	virtual void DrawOffscreenViews() {}
	virtual void SetDeferForegroundEffects(bool defer) { (void)defer; }

	virtual void SetSceneManager(SceneManager* sceneManager) { sceneManager_ = sceneManager; }


protected:
	SceneManager* sceneManager_ = nullptr;
};
