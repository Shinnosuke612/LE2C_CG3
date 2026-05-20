#include "Object3d.h"
#include "Object3dCommon.h"
#include "../base/DirectXCommon.h"
#include "../2d/TextureManager.h"
#include "ModelManager.h"
#include "Model.h"
#include "Camera.h"

void Object3d::Initialize(Object3dCommon* object3dCommon){
	this->object3dCommon = object3dCommon;

	CreateTransformationMatrixResource();
	CreateCameraResource();

	//Transform変数を作る
	transform = { {1.0f,1.0f,1.0f},{0.0f,0.0f,0.0f},{0.0f,0.0f,0.0f} };
	this->camera = object3dCommon->GetDefaultCamera();
}

void Object3d::Update(){
	Matrix4x4 worldMatrix = MakeAffineMatrix(transform.scale, transform.rotate, transform.translate);

	Matrix4x4 worldViewProjectionMatrix;
	if(camera){
		const Matrix4x4& viewProjectionMatrix = camera->GetViewProjectionMatrix();
		worldViewProjectionMatrix = Multiply(worldMatrix, viewProjectionMatrix);
	} else{
		worldViewProjectionMatrix = worldMatrix;
	}
	

	transformationMatrixData->WVP = worldViewProjectionMatrix;
	transformationMatrixData->World = worldMatrix;

	if (camera) {
		cameraData->worldPosition = camera->GetTranslate();
	}
}

void Object3d::Draw(){

	auto* commandList = object3dCommon->GetDxCommon()->GetCommandList();

	// 座標変換行列CBufferの場所を設定
	commandList->SetGraphicsRootConstantBufferView(1, transformationMatrixResource->GetGPUVirtualAddress());

	commandList->SetGraphicsRootConstantBufferView(4, cameraResource->GetGPUVirtualAddress());

	if(model){
		model->Draw();
	}
}

void Object3d::SetModel(const std::string& filePath){
//モデルを検索してセットする
	model = ModelManager::GetInstance()->FindModel(filePath);
}



void Object3d::CreateTransformationMatrixResource(){
	//WVP用リソースのリソースを作る。Matrix4x4 1つ分のサイズを用意する
	transformationMatrixResource = *&object3dCommon->GetDxCommon()->CreateBufferResource(sizeof(TransformationMatrix));
	//書き込むためのアドレス取得
	transformationMatrixResource->Map(0, nullptr, reinterpret_cast<void**>(&transformationMatrixData));
	//単位行列を書き込んでおく
	transformationMatrixData->WVP = MakeIdentity4x4();
	transformationMatrixData->World = MakeIdentity4x4();
}

void Object3d::CreateCameraResource() {
	cameraResource = *&object3dCommon->GetDxCommon()->CreateBufferResource(sizeof(CameraForGPU));
	cameraResource->Map(0, nullptr, reinterpret_cast<void**>(&cameraData));
	cameraData->worldPosition = { 0.0f, 0.0f, -10.0f };
	cameraData->padding = 0.0f;
}
