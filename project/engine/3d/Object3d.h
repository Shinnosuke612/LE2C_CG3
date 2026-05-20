#pragma once
#include <string>
#include <fstream>
#include <sstream>
#include <cassert>
#include <vector>
#include "../math/Vector2.h"
#include "../math/Vector3.h"
#include "../math/Vector4.h"
#include "../math/Matrix4x4.h"
#include "../math/Transform.h"
#include <d3d12.h> 

class Object3dCommon;
class Model;
class Camera;

class Object3d{
private://インナークラス

	//座標変換行列データ
	struct TransformationMatrix{
		Matrix4x4 WVP;
		Matrix4x4 World;
	};

	struct CameraForGPU {
		Vector3 worldPosition;
		float padding;
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
	void SetModel(const std::string& filePath);
	void SetCamera(Camera* camera){ this->camera = camera; }

	// getter（参照返しが軽くて安全）
	const Vector3& GetScale() const{return transform.scale;}
	const Vector3& GetRotate() const{return transform.rotate;}
	const Vector3& GetTranslate() const{return transform.translate;}
private: //非公開メンバ関数

	//座標変換行列用リソース作成関数
	void CreateTransformationMatrixResource();

	void CreateCameraResource();
private://メンバ変数
	Transform transform;

	Object3dCommon* object3dCommon = nullptr;


	//バッファリソース
	ID3D12Resource* transformationMatrixResource;
	//バッファリソース内のデータおw指すポインタ
	TransformationMatrix* transformationMatrixData = nullptr;

	//見た目用のモデル
	Model* model = nullptr;
	//カメラ
	Camera* camera = nullptr;

	ID3D12Resource* cameraResource;
	CameraForGPU* cameraData = nullptr;
};

