#include <string>
#include <Windows.h>
//ログ出力
namespace Logger
{
	void Log(const std::string& message);

	void Log(const std::wstring& message);
}

