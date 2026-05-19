#include <Windows.h>
#include "engine/scene/Game.h"
#include "engine/base/Framework.h"

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int)
{
	Framework* game = new Game();

	game->Run();

	delete game;

	return 0;
}