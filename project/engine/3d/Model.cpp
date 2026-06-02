#include "Model.h"
#include "ModelCommon.h"
#include "../base/DirectXCommon.h"
#include "../2d/TextureManager.h"
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
void Model::Initialize(ModelCommon* modelCommon, const std::string& directoryPath, const std::string& filename) {
	this->modelCommon = modelCommon;
	modelData = LoadModelFile(directoryPath, filename);
	CreateVertexResource();
	CreateMaterialResource();

	// .objの参照していrテクスチャファイル読み込み
	TextureManager::GetInstance()->LoadTexture(modelData.material.textureFilePath);
}
void Model::Draw() {
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

void Model::DrawForShadow() {
	auto* commandList = modelCommon->GetDxCommon()->GetCommandList();
	commandList->IASetVertexBuffers(0, 1, &vertexBufferView);
	commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	commandList->DrawInstanced(UINT(modelData.vertices.size()), 1, 0, 0);
}

Model::MaterialData Model::LoadMaterialTemplateFile(const std::string& directoryPath, const std::string& filename) {
	MaterialData materialData;      // 構築するMaterialData
	std::string line;               // 1行分
	std::ifstream file(directoryPath + "/" + filename); // ファイルを開く
	assert(file.is_open());         // 開けなかったら止める（資料準拠）

	while (std::getline(file, line)) {
		std::string identifier;
		std::istringstream s(line);
		s >> identifier;

		// 必要なのはTextureなので map_Kd だけ処理する
		if (identifier == "map_Kd") {
			std::string textureFilename;
			s >> textureFilename;

			// 連結してフルパスにする
			materialData.textureFilePath = directoryPath + "/" + textureFilename;
		}
	}

	return materialData;
}

Model::ModelData Model::LoadModelFile(const std::string& directoryPath, const std::string& filename) {

	Assimp::Importer importer;
	std::string filePath = directoryPath + "/" + filename;
	const aiScene* scene = importer.ReadFile(filePath.c_str(), aiProcess_FlipWindingOrder | aiProcess_FlipUVs);
	assert(scene->HasMeshes());

	ModelData modelData; //構築するModelData
	std::vector<Vector4> positions;//位置
	std::vector<Vector3> normals;//法線
	std::vector<Vector2> texcoords;//テクスチャ座標
	std::string line;//ファイルから読んだ１行を格納するもの

	modelData.rootNode = ReadNode(scene->mRootNode);

	for (uint32_t meshIndex = 0; meshIndex < scene->mNumMeshes; ++meshIndex) {
		aiMesh* mesh = scene->mMeshes[meshIndex];
		assert(mesh->HasNormals());
		assert(mesh->HasTextureCoords(0));

		for (uint32_t faceIndex = 0; faceIndex < mesh->mNumFaces; ++faceIndex) {
			aiFace& face = mesh->mFaces[faceIndex];
			assert(face.mNumIndices == 3); // 三角形以外は非対応
			for (uint32_t element = 0; element < face.mNumIndices; ++element) {
				uint32_t vertexIndex = face.mIndices[element];
				aiVector3D position = mesh->mVertices[vertexIndex];
				aiVector3D normal = mesh->mNormals[vertexIndex];
				aiVector3D texcoord = mesh->mTextureCoords[0][vertexIndex];
				VertexData vertex;
				vertex.position = Vector4(position.x, position.y, position.z, 1.0f);
				vertex.normal = Vector3(normal.x, normal.y, normal.z);
				vertex.texcoord = Vector2(texcoord.x, texcoord.y);

				vertex.position = Vector4(-position.x, position.y, position.z, 1.0f);
				vertex.normal = Vector3(-normal.x, normal.y, normal.z);
				vertex.texcoord = Vector2(texcoord.x, texcoord.y);
				modelData.vertices.push_back(vertex);
			}
		}
	}

	for (uint32_t materialIndex = 0; materialIndex < scene->mNumMaterials; ++materialIndex) {
		aiMaterial* material = scene->mMaterials[materialIndex];
		if (material->GetTextureCount(aiTextureType_DIFFUSE) != 0) {
			aiString textureFilePath;
			material->GetTexture(aiTextureType_DIFFUSE, 0, &textureFilePath);
			modelData.material.textureFilePath = directoryPath + "/" + textureFilePath.C_Str();
		}
	}
	return modelData;
}

void Model::CreateVertexResource() {
	vertexResource = *&modelCommon->GetDxCommon()->CreateBufferResource(sizeof(VertexData) * modelData.vertices.size());

	vertexBufferView.BufferLocation = vertexResource->GetGPUVirtualAddress();
	vertexBufferView.SizeInBytes = UINT(sizeof(VertexData) * modelData.vertices.size());
	vertexBufferView.StrideInBytes = sizeof(VertexData);

	vertexResource->Map(0, nullptr, reinterpret_cast<void**>(&vertexData));
	std::memcpy(vertexData, modelData.vertices.data(), sizeof(VertexData) * modelData.vertices.size());
}

void Model::CreateMaterialResource() {
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

	materialData->shininess = 40.0f;
}

Model::Node Model::ReadNode(aiNode* aiNode)
{
	Node result;
	aiMatrix4x4 aiLocalMatrix = aiNode->mTransformation;
	aiLocalMatrix.Transpose();
	for (int row = 0; row < 4; ++row) {
		for (int column = 0; column < 4; ++column) {
			result.localMatrix.m[row][column] = aiLocalMatrix[row][column];
		}
	}
	result.name = aiNode->mName.C_Str();
	result.children.resize(aiNode->mNumChildren);
	for (uint32_t childIndex = 0; childIndex < aiNode->mNumChildren; ++childIndex) {
		result.children[childIndex] = ReadNode(aiNode->mChildren[childIndex]);
	}
	return result;
}
