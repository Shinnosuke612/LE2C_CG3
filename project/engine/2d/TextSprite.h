// 役割: Runtime生成Text textureを2D quadとして描画する。
#pragma once

#include <string>
#include <utility>

#include <d3d12.h>
#include <wrl.h>

#include "../math/Matrix4x4.h"
#include "../math/Vector2.h"
#include "../math/Vector3.h"
#include "../math/Vector4.h"

class DirectXCommon;

class TextSprite {
public:
	enum class OutputTarget { Display, SceneHdr };

	void Initialize(DirectXCommon* dxCommon, std::string textureKey);
	void SetTextureKey(std::string textureKey) { textureKey_ = std::move(textureKey); }
	void Update(
		const Vector2& position,
		float rotation,
		const Vector2& size,
		const Vector2& pivot,
		const Vector4& color,
		uint32_t canvasWidth,
		uint32_t canvasHeight
	);
	void Draw(OutputTarget target) const;

private:
	struct VertexData {
		Vector4 position;
		Vector2 texcoord;
		Vector3 normal;
	};
	struct Material {
		Vector4 color;
		int32_t enableLighting;
		float padding[3];
		Matrix4x4 uvTransform;
	};
	struct TransformationMatrix {
		Matrix4x4 WVP;
		Matrix4x4 World;
	};

	void CreateResources();

	DirectXCommon* dxCommon_ = nullptr;
	std::string textureKey_;
	Microsoft::WRL::ComPtr<ID3D12Resource> vertexResource_;
	Microsoft::WRL::ComPtr<ID3D12Resource> indexResource_;
	Microsoft::WRL::ComPtr<ID3D12Resource> materialResource_;
	Microsoft::WRL::ComPtr<ID3D12Resource> transformationResource_;
	VertexData* vertexData_ = nullptr;
	uint32_t* indexData_ = nullptr;
	Material* materialData_ = nullptr;
	TransformationMatrix* transformationData_ = nullptr;
	D3D12_VERTEX_BUFFER_VIEW vertexBufferView_{};
	D3D12_INDEX_BUFFER_VIEW indexBufferView_{};
};
