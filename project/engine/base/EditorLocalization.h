// 役割: Editor固有の表示言語と設定値の変換を定義する。
// SceneやRuntimeの文字列は所有しない。
#pragma once

#include <string_view>

enum class EditorLanguage {
	Japanese,
	English
};

EditorLanguage ParseEditorLanguage(std::string_view value);
const char* ToEditorLanguageSettingValue(EditorLanguage language);
const char* SelectEditorText(
	EditorLanguage language,
	const char* japanese,
	const char* english
);
