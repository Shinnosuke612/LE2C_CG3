#include "Object3d.h"
#include "Object3dCommon.h"
#include "DirectXCommon.h"
#include "TextureManager.h"
#include "ModelManager.h"
#include "Model.h"

void Object3d::Initialize(Object3dCommon* object3dCommon){
	this->object3dCommon = object3dCommon;

	CreateTransformationMatrixResource();
	CreateDirectionalLightResource();

	

	//Transform変数を作る
	transform = { {1.0f,1.0f,1.0f},{0.0f,0.0f,0.0f},{0.0f,0.0f,0.0f} };
	cameraTransform = { {1.0f,1.0f,1.0f},{0.3f,0.0f,0.0f},{0.0f,4.0f,-10.0f} };
}

void Object3d::Update(){
	Matrix4x4 worldMatrix = MakeAffineMatrix(transform.scale, transform.rotate, transform.translate);
	Matrix4x4 cameraMatrix = MakeAffineMatrix(cameraTransform.scale, cameraTransform.rotate, cameraTransform.translate);
	Matrix4x4 viewMatrix = Inverse(cameraMatrix);
	Matrix4x4 projectionMatrix = MakePerspectiveFovMatrix(0.45f, float(WinApp::kClientWidth) / float(WinApp::kClientHeight), 0.1f, 100.0f);
	Matrix4x4 viewProjectionMatirx = Multiply(viewMatrix, projectionMatrix);
	Matrix4x4 worldViewProjectionMatrix = Multiply(worldMatrix, viewProjectionMatirx);

	transformationMatrixData->WVP = worldViewProjectionMatrix;
	transformationMatrixData->World = worldMatrix;
}

void Object3d::Draw(){

	auto* commandList = object3dCommon->GetDxCommon()->GetCommandList();

	// 座標変換行列CBufferの場所を設定
	commandList->SetGraphicsRootConstantBufferView(1, transformationMatrixResource->GetGPUVirtualAddress());

	// 平行光源CBufferの場所を設定
	commandList->SetGraphicsRootConstantBufferView(3, directionalLightResource->GetGPUVirtualAddress());

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

void Object3d::CreateDirectionalLightResource(){
	//平行光源のリソースを作る
	directionalLightResource = *&object3dCommon->GetDxCommon()->CreateBufferResource(sizeof(DirectionalLight));
	//書き込むためのアドレス取得
	directionalLightResource->Map(0, nullptr, reinterpret_cast<void**>(&directionalLightData));
	//平行光源の設定
	directionalLightData->color = { 1.0f,1.0f,1.0f,1.0f };
	directionalLightData->direction = { 0.0f,-1.0f,0.0f };
	directionalLightData->intensity = 1.0f;
}
