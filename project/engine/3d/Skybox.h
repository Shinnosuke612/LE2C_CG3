#pragma once
#include <string>
#include <d3d12.h>
#include "../math/Vector3.h"
#include "../math/Vector4.h"
#include "../math/Matrix4x4.h"
#include "../math/Transform.h"
#include <wrl/client.h>

class Object3dCommon;
class Camera;

class Skybox {
private:
	struct VertexData {
		Vector4 position;
	};

	struct TransformationMatrix {
		Matrix4x4 WVP;
		Matrix4x4 World;
	};

	struct Material {
		Vector4 color;
	};

public:
	void Initialize(Object3dCommon* object3dCommon, const std::string& textureFilePath);
	void Update();
	void Draw();

	void SetScale(const Vector3& scale) { transform_.scale = scale; }
	void SetColor(const Vector4& color) { materialData_->color = color; }

private:
	void CreateVertexResource();
	void CreateTransformationMatrixResource();
	void CreateMaterialResource();

private:
	Object3dCommon* object3dCommon_ = nullptr;
	Camera* camera_ = nullptr;

	Transform transform_{};

	std::string textureFilePath_;

	Microsoft::WRL::ComPtr<ID3D12Resource> vertexResource_ = nullptr;
	VertexData* vertexData_ = nullptr;
	D3D12_VERTEX_BUFFER_VIEW vertexBufferView_{};
	uint32_t vertexCount_ = 0;

	Microsoft::WRL::ComPtr<ID3D12Resource> transformationMatrixResource_ = nullptr;
	TransformationMatrix* transformationMatrixData_ = nullptr;

	Microsoft::WRL::ComPtr<ID3D12Resource> materialResource_ = nullptr;
	Material* materialData_ = nullptr;
};