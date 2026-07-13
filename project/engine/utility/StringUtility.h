// 役割: 文字列とワイド文字列の変換を提供する。
#pragma once
#include <string>
//文字コード
namespace StringUtility
{
	std::wstring ConvertString(const std::string& str);

	std::string ConvertString(const std::wstring& str);
}
