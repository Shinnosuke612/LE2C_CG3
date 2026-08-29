// 役割: Editor表示言語の設定値変換と英語Fallbackを実装する。
#include "EditorLocalization.h"

EditorLanguage ParseEditorLanguage(std::string_view value) {
	return value == "en"
		? EditorLanguage::English
		: EditorLanguage::Japanese;
}

const char* ToEditorLanguageSettingValue(EditorLanguage language) {
	return language == EditorLanguage::English ? "en" : "ja";
}

const char* SelectEditorText(
	EditorLanguage language,
	const char* japanese,
	const char* english
) {
	if (language == EditorLanguage::Japanese) {
		return japanese && japanese[0] != '\0' ? japanese : english;
	}
	return english && english[0] != '\0' ? english : japanese;
}
