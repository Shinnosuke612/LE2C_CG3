#pragma once
#include <string>
#include <fstream>
#include <sstream>
#include <cassert>
#include <vector>
#include "Vector/Vector2.h"
#include "Vector/Vector3.h"
#include "Vector/Vector4.h"
#include "Matrix4x4.h"
#include "Transform.h"
#include <d3d12.h> 

class Object3dCommon;
class Model;

class Object3d{
private://インナークラス

	//座標変換行列データ
	struct TransformationMatrix{
		Matrix4x4 WVP;
		Matrix4x4 World;
	};
	//並行光源
	struct DirectionalLight{
		Vector4 color; //!< ライトの色
		Vector3 direction; //!< ライトの向き
		float intensity; //!< 輝度
	};

public: //公開メンバ関数
	//初期化
	void Initialize(Object3dCommon* object3dCommon);
	//更新
	void Update();
	//描画
	void Draw();
	//setter
	void SetModel(Model* model){
		this->model = model;
	}
	// setter
	void SetScale(const Vector3& scale){ transform.scale = scale; }
	void SetRotate(const Vector3& rotate){ transform.rotate = rotate; }
	void SetTranslate(const Vector3& translate){transform.translate = translate;}

	// getter（参照返しが軽くて安全）
	const Vector3& GetScale() const{return transform.scale;}
	const Vector3& GetRotate() const{return transform.rotate;}
	const Vector3& GetTranslate() const{return transform.translate;}
private: //非公開メンバ関数

	//座標変換行列用リソース作成関数
	void CreateTransformationMatrixResource();
	//並行光源用リソース作成関数
	void CreateDirectionalLightResource();

private://メンバ変数
	Transform transform;
	Transform cameraTransform;

	Object3dCommon* object3dCommon = nullptr;


	//バッファリソース
	ID3D12Resource* transformationMatrixResource;
	//バッファリソース内のデータおw指すポインタ
	TransformationMatrix* transformationMatrixData = nullptr;

	//バッファリソース
	ID3D12Resource* directionalLightResource;
	//バッファリソース内のデータおw指すポインタ
	DirectionalLight* directionalLightData = nullptr;

	//見た目用のモデル
	Model* model = nullptr;
};

