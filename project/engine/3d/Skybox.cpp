#include "Skybox.h"
#include "Object3dCommon.h"
#include "Camera.h"
#include "../base/DirectXCommon.h"
#include "../2d/TextureManager.h"
#include <array>
#include <cassert>
#include <cstring>

void Skybox::Initialize(Object3dCommon* object3dCommon, const std::string& textureFilePath) {
	assert(object3dCommon);

	object3dCommon_ = object3dCommon;
	camera_ = object3dCommon_->GetDefaultCamera();
	textureFilePath_ = textureFilePath;

	transform_.scale = { 50.0f, 50.0f, 50.0f };
	transform_.rotate = { 0.0f, 0.0f, 0.0f };
	transform_.translate = { 0.0f, 0.0f, 0.0f };

	TextureManager::GetInstance()->LoadTexture(textureFilePath_);

	CreateVertexResource();
	CreateTransformationMatrixResource();
	CreateMaterialResource();
}

void Skybox::Update() {
	if (camera_) {
		transform_.translate = camera_->GetTranslate();
	}

	Matrix4x4 worldMatrix = MakeAffineMatrix(
		transform_.scale,
		transform_.rotate,
		transform_.translate
	);

	Matrix4x4 worldViewProjectionMatrix = worldMatrix;

	if (camera_) {
		worldViewProjectionMatrix = Multiply(
			worldMatrix,
			camera_->GetViewProjectionMatrix()
		);
	}

	transformationMatrixData_->WVP = worldViewProjectionMatrix;
	transformationMatrixData_->World = worldMatrix;
}

void Skybox::Draw() {
	auto* commandList = object3dCommon_->GetDxCommon()->GetCommandList();

	object3dCommon_->SetSkyboxRenderState();

	commandList->IASetVertexBuffers(0, 1, &vertexBufferView_);

	commandList->SetGraphicsRootConstantBufferView(
		0,
		materialResource_->GetGPUVirtualAddress()
	);

	commandList->SetGraphicsRootConstantBufferView(
		1,
		transformationMatrixResource_->GetGPUVirtualAddress()
	);

	commandList->SetGraphicsRootDescriptorTable(
		2,
		TextureManager::GetInstance()->GetSrvHandleGPU(textureFilePath_)
	);
	commandList->SetGraphicsRootDescriptorTable(
		7,
		TextureManager::GetInstance()->GetSrvHandleGPU(textureFilePath_)
	);

	commandList->DrawInstanced(vertexCount_, 1, 0, 0);
}

void Skybox::CreateVertexResource() {
	constexpr float kSize = 1.0f;

	std::array<VertexData, 36> vertices = {
		// +X
		VertexData{ Vector4(kSize,  kSize, -kSize, 1.0f) },
		VertexData{ Vector4(kSize,  kSize,  kSize, 1.0f) },
		VertexData{ Vector4(kSize, -kSize, -kSize, 1.0f) },
		VertexData{ Vector4(kSize, -kSize, -kSize, 1.0f) },
		VertexData{ Vector4(kSize,  kSize,  kSize, 1.0f) },
		VertexData{ Vector4(kSize, -kSize,  kSize, 1.0f) },

		// -X
		VertexData{ Vector4(-kSize,  kSize,  kSize, 1.0f) },
		VertexData{ Vector4(-kSize,  kSize, -kSize, 1.0f) },
		VertexData{ Vector4(-kSize, -kSize,  kSize, 1.0f) },
		VertexData{ Vector4(-kSize, -kSize,  kSize, 1.0f) },
		VertexData{ Vector4(-kSize,  kSize, -kSize, 1.0f) },
		VertexData{ Vector4(-kSize, -kSize, -kSize, 1.0f) },

		// +Z
		VertexData{ Vector4(kSize,  kSize,  kSize, 1.0f) },
		VertexData{ Vector4(-kSize,  kSize,  kSize, 1.0f) },
		VertexData{ Vector4(kSize, -kSize,  kSize, 1.0f) },
		VertexData{ Vector4(kSize, -kSize,  kSize, 1.0f) },
		VertexData{ Vector4(-kSize,  kSize,  kSize, 1.0f) },
		VertexData{ Vector4(-kSize, -kSize,  kSize, 1.0f) },

		// -Z
		VertexData{ Vector4(-kSize,  kSize, -kSize, 1.0f) },
		VertexData{ Vector4(kSize,  kSize, -kSize, 1.0f) },
		VertexData{ Vector4(-kSize, -kSize, -kSize, 1.0f) },
		VertexData{ Vector4(-kSize, -kSize, -kSize, 1.0f) },
		VertexData{ Vector4(kSize,  kSize, -kSize, 1.0f) },
		VertexData{ Vector4(kSize, -kSize, -kSize, 1.0f) },

		// +Y
		VertexData{ Vector4(-kSize,  kSize,  kSize, 1.0f) },
		VertexData{ Vector4(kSize,  kSize,  kSize, 1.0f) },
		VertexData{ Vector4(-kSize,  kSize, -kSize, 1.0f) },
		VertexData{ Vector4(-kSize,  kSize, -kSize, 1.0f) },
		VertexData{ Vector4(kSize,  kSize,  kSize, 1.0f) },
		VertexData{ Vector4(kSize,  kSize, -kSize, 1.0f) },

		// -Y
		VertexData{ Vector4(-kSize, -kSize, -kSize, 1.0f) },
		VertexData{ Vector4(kSize, -kSize, -kSize, 1.0f) },
		VertexData{ Vector4(-kSize, -kSize,  kSize, 1.0f) },
		VertexData{ Vector4(-kSize, -kSize,  kSize, 1.0f) },
		VertexData{ Vector4(kSize, -kSize, -kSize, 1.0f) },
		VertexData{ Vector4(kSize, -kSize,  kSize, 1.0f) },
	};

	vertexCount_ = static_cast<uint32_t>(vertices.size());

	vertexResource_ = object3dCommon_->GetDxCommon()->CreateBufferResource(
		sizeof(VertexData) * vertices.size()
	);

	vertexBufferView_.BufferLocation = vertexResource_->GetGPUVirtualAddress();
	vertexBufferView_.SizeInBytes = UINT(sizeof(VertexData) * vertices.size());
	vertexBufferView_.StrideInBytes = sizeof(VertexData);

	vertexResource_->Map(0, nullptr, reinterpret_cast<void**>(&vertexData_));
	std::memcpy(vertexData_, vertices.data(), sizeof(VertexData) * vertices.size());
}

void Skybox::CreateTransformationMatrixResource() {
	transformationMatrixResource_ = object3dCommon_->GetDxCommon()->CreateBufferResource(
		sizeof(TransformationMatrix)
	);

	transformationMatrixResource_->Map(
		0,
		nullptr,
		reinterpret_cast<void**>(&transformationMatrixData_)
	);

	transformationMatrixData_->WVP = MakeIdentity4x4();
	transformationMatrixData_->World = MakeIdentity4x4();
}

void Skybox::CreateMaterialResource() {
	materialResource_ = object3dCommon_->GetDxCommon()->CreateBufferResource(
		sizeof(Material)
	);

	materialResource_->Map(
		0,
		nullptr,
		reinterpret_cast<void**>(&materialData_)
	);

	materialData_->color = { 1.0f, 1.0f, 1.0f, 1.0f };
}
