// 役割: Windowsアプリケーションを起動し、Gameのライフサイクルを開始する。
#include <Windows.h>
#include "application/Game.h"
#include "engine/base/Framework.h"

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int)
{
	Framework* game = new Game();

	game->Run();

	delete game;

	return 0;
}
