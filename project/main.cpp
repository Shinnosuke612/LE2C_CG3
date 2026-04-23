#include "Game.h"

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
	Game game;

	// ゲームの初期化
	game.Initialize();

	while (true) // ゲームループ
	{
		// 毎フレーム更新
		game.Update();

		// ×ボタンで終了
		if (game.IsEndRequest()) {
			break;
		}

		// 描画
		game.Draw();
	}

	// ゲームの終了
	game.Finalize();

	return 0;
}