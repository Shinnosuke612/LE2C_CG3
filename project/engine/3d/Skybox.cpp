#include "Skybox.h"
#include "Object3dCommon.h"
#include "Camera.h"
#include "../base/DirectXCommon.h"
#include "../2d/TextureManager.h"
#include <cassert>

void Skybox::Initialize(Object3dCommon* object3dCommon, const std::string& textureFilePath) {
	assert(object3dCommon);

	object3dCommon_ = object3dCommon;
	camera_ = object3dCommon_->GetDefaultCamera();
	textureFilePath_ = textureFilePath;

	TextureManager::GetInstance()->LoadTexture(textureFilePath_);

	CreateTransformationMatrixResource();
	CreateMaterialResource();
}

void Skybox::Update() {
	if (camera_) {
		transformationMatrixData_->inverseViewProjection =
			Inverse(camera_->GetViewProjectionMatrix());
		const Vector3& cameraPosition = camera_->GetTranslate();
		transformationMatrixData_->cameraPosition = {
			cameraPosition.x,
			cameraPosition.y,
			cameraPosition.z,
			1.0f
		};
	} else {
		transformationMatrixData_->inverseViewProjection = MakeIdentity4x4();
		transformationMatrixData_->cameraPosition = { 0.0f, 0.0f, 0.0f, 1.0f };
	}
}

void Skybox::Draw() {
	auto* commandList = object3dCommon_->GetDxCommon()->GetCommandList();

	object3dCommon_->SetSkyboxRenderState();

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

	commandList->DrawInstanced(3, 1, 0, 0);
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

	transformationMatrixData_->inverseViewProjection = MakeIdentity4x4();
	transformationMatrixData_->cameraPosition = { 0.0f, 0.0f, 0.0f, 1.0f };
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
