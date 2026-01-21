#pragma once
#include "Vector/Vector2.h"
#include "Vector/Vector3.h"
#include "Vector/Vector4.h"
#include "Transform.h"
#include "Matrix4x4.h"
#include <stdint.h>
#include <d3d12.h> 
#include <dxgi1_6.h>
class SpriteCommon;

class Sprite{
private://インナークラス
	struct VertexData{
		Vector4 position;
		Vector2 texcoord;
		Vector3 normal;
	};

	struct Material{
		Vector4 color;
		int32_t enableLighting;
		float padding[3];
		Matrix4x4 uvTransform;
	};

	struct TransformationMatrix{
		Matrix4x4 WVP;
		Matrix4x4 World;
	};
public://公開メンバ関数
	//初期化
	void Initialize(SpriteCommon* spriteCommon,std::string textureFilePath);
	//更新処理
	void Update();
	//描画処理
	void Draw();

	//getter
	const Vector2& GetPosition() const{return position;}//座標
	float GetRotation() const{return rotation;}//回転
	const Vector4& GetColor() const{return materialData->color;}
	const Vector2& GetSize() const{return size;}
	//setter
	void SetPosition(const Vector2& position){this->position = position;}//座標
	void SetRotation(float rotation){this->rotation = rotation;}//回転
	void SetColor(const Vector4& color){this->materialData->color = color;}//回転
	void SetSize(const Vector2& size){this->size = size;}//回転


private://非公開メンバ関数
	void MakeVertexData();

	void MakeMaterialData();

	void MakeTransformationMatrixData();

private://メンバ変数

	SpriteCommon* spriteCommon_ = nullptr;
	//バッファリソース
	ID3D12Resource* vertexResource;
	ID3D12Resource* indexResource;
	ID3D12Resource* materialResource;
	ID3D12Resource* transformationMatrixResource;
	//バッファリソース内のデータを指すポインタ
	VertexData* vertexData = nullptr;
	uint32_t* indexData = nullptr;
	Material* materialData = nullptr;
	TransformationMatrix* transformationMatrixData = nullptr;
	//バッファリソースの使い道wp補足するバッファビュー
	D3D12_VERTEX_BUFFER_VIEW vertexBufferView;
	D3D12_INDEX_BUFFER_VIEW indexBufferView;
	//テクスチャ番号
	uint32_t textureIndex = 0;

	Transform transform;

	//座標
	Vector2 position = { 0.0f };
	//回転
	float rotation = 0.0f;
	//サイズ
	Vector2 size = { 640.0f,360.0f };
};

