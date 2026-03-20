#include "Model.h"
#include "ModelCommon.h"
#include "DirectXCommon.h"
#include "TextureManager.h"
void Model::Initialize(ModelCommon* modelCommon, const std::string& directoryPath, const std::string& filename){
	this->modelCommon = modelCommon;
		modelData = LoadObjFile(directoryPath, filename);
	CreateVertexResource();
	CreateMaterialResource();

	// .objの参照していrテクスチャファイル読み込み
	TextureManager::GetInstance()->LoadTexture(modelData.material.textureFilePath);
}
void Model::Draw(){
	auto* commandList = modelCommon->GetDxCommon()->GetCommandList();
	// VertexBufferViewを設定
	commandList->IASetVertexBuffers(0, 1, &vertexBufferView);
	commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	// マテリアルCBufferの場所を設定
	commandList->SetGraphicsRootConstantBufferView(0, materialResource->GetGPUVirtualAddress());

	// SRVのDescriptorTableの先頭を設定（TextureManagerから取得）
	commandList->SetGraphicsRootDescriptorTable(
		2, TextureManager::GetInstance()->GetSrvHandleGPU(modelData.material.textureFilePath)
	);

	// 描画！
	commandList->DrawInstanced(UINT(modelData.vertices.size()), 1, 0, 0);
}
Model::MaterialData Model::LoadMaterialTemplateFile(const std::string& directoryPath, const std::string& filename){
	MaterialData materialData;      // 構築するMaterialData
	std::string line;               // 1行分
	std::ifstream file(directoryPath + "/" + filename); // ファイルを開く
	assert(file.is_open());         // 開けなかったら止める（資料準拠）

	while(std::getline(file, line)){
		std::string identifier;
		std::istringstream s(line);
		s >> identifier;

		// 必要なのはTextureなので map_Kd だけ処理する
		if(identifier == "map_Kd"){
			std::string textureFilename;
			s >> textureFilename;

			// 連結してフルパスにする
			materialData.textureFilePath = directoryPath + "/" + textureFilename;
		}
	}

	return materialData;
}

Model::ModelData Model::LoadObjFile(const std::string& directoryPath, const std::string& filename){
	ModelData modelData; //構築するModelData
	std::vector<Vector4> positions;//位置
	std::vector<Vector3> normals;//法線
	std::vector<Vector2> texcoords;//テクスチャ座標
	std::string line;//ファイルから読んだ１行を格納するもの

	std::ifstream file(directoryPath + "/" + filename);
	assert(file.is_open());

	while(std::getline(file, line)){
		std::string identifier;
		std::istringstream s(line);
		s >> identifier;

		if(identifier == "v"){
			Vector4 position;
			s >> position.x >> position.y >> position.z;
			position.x *= 1;
			position.w = 1.0f;
			positions.push_back(position);
		} else if(identifier == "vt"){
			Vector2 texcoord;
			s >> texcoord.x >> texcoord.y;
			texcoord.y = 1.0f - texcoord.y;
			texcoords.push_back(texcoord);
		} else if(identifier == "vn"){
			Vector3 normal;
			s >> normal.x >> normal.y >> normal.z;
			normal.x *= -1;
			normals.push_back(normal);
		} else if(identifier == "f"){
			VertexData triangle[3];
			//面は三角限定。その他は未対応
			for(int32_t faceVertex = 0; faceVertex < 3; ++faceVertex){
				std::string vertexDefinition;
				s >> vertexDefinition;
				//頂点の要素へのIndexは「位置/UV/法線」で格納されているので、分解してIndexを取得する
				std::istringstream v(vertexDefinition);
				uint32_t elementIndices[3];
				for(int32_t element = 0; element < 3; ++element){
					std::string index;
					std::getline(v, index, '/');
					elementIndices[element] = std::stoi(index);
				}
				//要素へのIndexから、実際の要素の値を取得して頂点を構築する
				Vector4 position = positions[elementIndices[0] - 1];
				Vector2 texcoord = texcoords[elementIndices[1] - 1];
				Vector3 normal = normals[elementIndices[2] - 1];
				VertexData vertex = { position, texcoord,normal };
				modelData.vertices.push_back(vertex);
				triangle[faceVertex] = { position,texcoord,normal };
			}
		} else if(identifier == "mtllib"){
			std::string materialFilename;
			s >> materialFilename;
			modelData.material = LoadMaterialTemplateFile(directoryPath, materialFilename);
		}

	}
	return modelData;
}

void Model::CreateVertexResource(){
	vertexResource = *&modelCommon->GetDxCommon()->CreateBufferResource(sizeof(VertexData) * modelData.vertices.size());

	vertexBufferView.BufferLocation = vertexResource->GetGPUVirtualAddress();
	vertexBufferView.SizeInBytes = UINT(sizeof(VertexData) * modelData.vertices.size());
	vertexBufferView.StrideInBytes = sizeof(VertexData);

	vertexResource->Map(0, nullptr, reinterpret_cast<void**>(&vertexData));
	std::memcpy(vertexData, modelData.vertices.data(), sizeof(VertexData) * modelData.vertices.size());
}

void Model::CreateMaterialResource(){
	//マテリアル用のリソースを作る。今回はカラー1つ分のサイズを用意する
	materialResource = *&modelCommon->GetDxCommon()->CreateBufferResource(sizeof(Material));
	//書き込むためのアドレスを取得
	materialResource->Map(0, nullptr, reinterpret_cast<void**>(&materialData));
	//今回は赤を書き込んでる
	materialData->color = Vector4(1.0f, 1.0f, 1.0f, 1.0f);
	//ライティングの有無
	materialData->enableLighting = true;
	//uvTransform行列を単位行列で初期化
	materialData->uvTransform = MakeIdentity4x4();
}
