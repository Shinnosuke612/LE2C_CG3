#pragma once
#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <cassert>
#include "Vector/Vector2.h"
#include "Vector/Vector3.h"
#include "Vector/Vector4.h"
#include "Matrix4x4.h"
#include <d3d12.h> 
class ModelCommon;
class Model{
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
private://非公開メンバ関数
	// .mtlファイルの読み取り
	static MaterialData LoadMaterialTemplateFile(const std::string& directoryPath, const std::string& filename);
	// .objファイルの読み取り
	static ModelData LoadObjFile(const std::string& directoryPath, const std::string& filename);

	//頂点リソース作成関数
	void CreateVertexResource();
	//マテリアルリソース作成関数
	void CreateMaterialResource();
public://公開メンバ関数
	//初期化
	void Initialize(ModelCommon* modelCommon,const std::string& directoryPath, const std::string& filename);
	//描画
	void Draw();
private:
	//ModelCommonのポインタ
	ModelCommon* modelCommon;
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
};

