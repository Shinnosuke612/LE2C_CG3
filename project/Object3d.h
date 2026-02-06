#pragma once

class Object3dCommon;

class Object3d{
private://インナークラス

	struct MaterialData{
		std::string textureFilePath;
	};
	struct ModelData{
		std::vector<VertexData> vertices;
		MaterialData material;
	};

public: //メンバ関数
	//初期化
	void Initialize(Object3dCommon* object3dCommon);

private://メンバ変数
	Object3dCommon* object3dCommon = nullptr;
	//objファイルのデータ
	ModelData modelData;
};

