// 役割: アプリケーションのメッセージをログファイルへ出力する。
#pragma once
#include <string>
#include <Windows.h>

// ログ出力
namespace Logger
{
	// ログファイルの初期化
	void Initialize();

	// ログ出力
	void Log(const std::string& message);
	void Log(const std::wstring& message);

	// ログファイルの終了処理
	void Finalize();
}
