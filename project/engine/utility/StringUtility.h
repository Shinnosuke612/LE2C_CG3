// 役割: 文字列とワイド文字列の変換を提供する。
#pragma once
#include <filesystem>
#include <string>
//文字コード
namespace StringUtility
{
	std::wstring ConvertString(const std::string& str);

	std::string ConvertString(const std::wstring& str);

	// UTF-8文字列とfilesystem::pathを相互変換する。
	std::filesystem::path ToPath(const std::string& utf8Path);

	std::string ToUtf8(const std::filesystem::path& path);
}
