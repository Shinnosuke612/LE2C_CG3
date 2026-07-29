// 役割: TextRenderer ComponentのRuntime texture cacheと2D描画空間の振り分けを所有する。
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>

#include "../../../engine/2d/TextSprite.h"
#include "../../../engine/math/Vector2.h"

class DirectXCommon;
class SceneDocument;

class SceneTextRenderSystem {
public:
	~SceneTextRenderSystem();
	void Initialize(DirectXCommon* dxCommon, std::string runtimeKey);
	void SetTextOverride(uint64_t entityId, std::string text);
	void ClearTextOverrides();
	void Sync(SceneDocument* document);
	void DrawScene2D(const SceneDocument& document, uint32_t width, uint32_t height) const;
	void DrawScreenOverlay(const SceneDocument& document, uint32_t width, uint32_t height) const;
	bool HasScreenOverlay(const SceneDocument& document) const;
	void Finalize();

private:
	struct RuntimeText {
		std::unique_ptr<TextSprite> sprite;
		std::string textureKey;
		std::string contentSignature;
		Vector2 bitmapSize{};
		bool spriteInitialized = false;
	};

	DirectXCommon* dxCommon_ = nullptr;
	std::string runtimeKey_;
	std::unordered_map<uint64_t, RuntimeText> texts_;
	std::unordered_map<uint64_t, std::string> textOverrides_;
};
