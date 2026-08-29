// 役割: Editorで選択可能なComponentの表示情報、Context、明示依存を提供する。
// Component生成、既定値、Dirty、Reloadは所有しない。
#pragma once

#include <cstdint>
#include <string_view>
#include <vector>

#include "EditorLocalization.h"

enum class EditorComponentContext : uint8_t {
	Scene = 1,
	Prefab = 2
};

enum class EditorComponentCategory : uint8_t {
	Rendering,
	World,
	Camera,
	Physics,
	Gameplay,
	Animation,
	EventAndFlow
};

enum class EditorComponentTag : uint16_t {
	TwoD = 1u << 0,
	ThreeD = 1u << 1,
	UI = 1u << 2,
	Camera = 1u << 3,
	Physics = 1u << 4,
	Collision = 1u << 5,
	Combat = 1u << 6,
	Player = 1u << 7,
	Enemy = 1u << 8,
	Animation = 1u << 9,
	Event = 1u << 10,
	Spawn = 1u << 11,
	Reference = 1u << 12,
	PostEffect = 1u << 13,
	Prefab = 1u << 14
};

constexpr uint16_t EditorComponentTagBit(EditorComponentTag tag) {
	return static_cast<uint16_t>(tag);
}

struct EditorComponentDefinition {
	const char* type = "";
	const char* japaneseName = "";
	const char* englishName = "";
	const char* japaneseDescription = "";
	const char* englishDescription = "";
	EditorComponentCategory category = EditorComponentCategory::Gameplay;
	uint8_t contextMask = 0;
	int sceneOrder = -1;
	int prefabOrder = -1;
	const char* requiredType = "";
	uint16_t tagMask = 0;
};

const std::vector<const EditorComponentDefinition*>&
GetEditorComponentDefinitions(EditorComponentContext context);

const EditorComponentDefinition* FindEditorComponentDefinition(
	std::string_view type
);

bool SupportsEditorComponentContext(
	const EditorComponentDefinition& definition,
	EditorComponentContext context
);

const char* GetEditorComponentDisplayName(
	const EditorComponentDefinition& definition,
	EditorLanguage language
);

const char* GetEditorComponentDescription(
	const EditorComponentDefinition& definition,
	EditorLanguage language
);

const char* GetEditorComponentCategoryDisplayName(
	EditorComponentCategory category,
	EditorLanguage language
);

const std::vector<EditorComponentTag>& GetEditorComponentTags();

bool HasEditorComponentTag(
	const EditorComponentDefinition& definition,
	EditorComponentTag tag
);

const char* GetEditorComponentTagId(EditorComponentTag tag);

const char* GetEditorComponentTagDisplayName(
	EditorComponentTag tag,
	EditorLanguage language
);
