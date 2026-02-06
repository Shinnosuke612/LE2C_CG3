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

class Object3d{
private://インナークラス
	//頂点データ
	struct VertexData{
		Vector4 position;
		Vector2 texcoord;
		Vector3 normal;
	};
	struct MaterialData{
		std::string textureFilePath;
		uint32_t textureIndex = 0;
	};
	struct ModelData{
		std::vector<VertexData> vertices;
		MaterialData material;
	};
	//マテリアルデータ
	struct Material{
		Vector4 color;
		int32_t enableLighting;
		float padding[3];
		Matrix4x4 uvTransform;
	};
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

private: //非公開メンバ関数
	// .mtlファイルの読み取り
	static MaterialData LoadMaterialTemplateFile(const std::string& directoryPath, const std::string& filename);
	// .objファイルの読み取り
	static ModelData LoadObjFile(const std::string& directoryPath, const std::string& filename);

	//頂点リソース作成関数
	void CreateVertexResource();
	//マテリアルリソース作成関数
	void CreateMaterialResource();
	//座標変換行列用リソース作成関数
	void CreateTransformationMatrixResource();
	//並行光源用リソース作成関数
	void CreateDirectionalLightResource();

private://メンバ変数
	Transform transform;
	Transform cameraTransform;

	Object3dCommon* object3dCommon = nullptr;
	//objファイルのデータ
	ModelData modelData;
	//バッファリソース
	ID3D12Resource* vertexResource;
	//バッファリソース内のデータを指すポインタ
	VertexData* vertexData = nullptr;
	//バッファリソースの使い道を補足するバッファビュー
	D3D12_VERTEX_BUFFER_VIEW vertexBufferView;

	//バッファリソース
	ID3D12Resource* materialResource;
	//バッファリソース内のデータおw指すポインタ
	Material* materialData = nullptr;

	//バッファリソース
	ID3D12Resource* transformationMatrixResource;
	//バッファリソース内のデータおw指すポインタ
	TransformationMatrix* transformationMatrixData = nullptr;
	
	//バッファリソース
	ID3D12Resource* directionalLightResource;
	//バッファリソース内のデータおw指すポインタ
	DirectionalLight* directionalLightData = nullptr;
};

