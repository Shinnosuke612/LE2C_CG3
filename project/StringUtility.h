#pragma once
#include <string>
//文字コード
namespace StringUtility
{
	std::wstring ConvertString(const std::string& str);

	std::string ConvertString(const std::wstring& str);
}