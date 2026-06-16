#include "Model.h"
#include "ModelCommon.h"
#include "../base/DirectXCommon.h"
#include "../2d/TextureManager.h"
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <filesystem>
#include <cstdlib>

namespace {

Matrix4x4 ConvertAssimpMatrix(const aiMatrix4x4& matrix) {
	// Keep the inverse bind pose exact while converting RH/column-vector data.
	const float source[4][4] = {
		{ matrix.a1, matrix.a2, matrix.a3, matrix.a4 },
		{ matrix.b1, matrix.b2, matrix.b3, matrix.b4 },
		{ matrix.c1, matrix.c2, matrix.c3, matrix.c4 },
		{ matrix.d1, matrix.d2, matrix.d3, matrix.d4 }
	};
	const float handedness[4] = { -1.0f, 1.0f, 1.0f, 1.0f };

	Matrix4x4 result{};
	for (uint32_t row = 0; row < 4; ++row) {
		for (uint32_t column = 0; column < 4; ++column) {
			result.m[row][column] =
				handedness[row] *
				source[column][row] *
				handedness[column];
		}
	}
	return result;
}

} // namespace

void Model::Initialize(ModelCommon* modelCommon, const std::string& directoryPath, const std::string& filename) {
	this->modelCommon = modelCommon;
	modelData = LoadModelFile(directoryPath, filename);
	animation_ = LoadAnimationFile(directoryPath, filename);
	CreateVertexResource();
	CreateIndexResource();
	CreateMaterialResource();

	TextureManager* textureManager = TextureManager::GetInstance();
	if (
		modelData.material.textureFilePath.empty() ||
		!textureManager->LoadTexture(modelData.material.textureFilePath)
	) {
		modelData.material.textureFilePath = "resources/uvChecker.png";
		textureManager->LoadTexture(modelData.material.textureFilePath);
	}
}
void Model::Draw(const D3D12_VERTEX_BUFFER_VIEW* influenceBufferView) {
	auto* commandList = modelCommon->GetDxCommon()->GetCommandList();
	if (influenceBufferView) {
		const D3D12_VERTEX_BUFFER_VIEW vertexBufferViews[] = {
			vertexBufferView,
			*influenceBufferView
		};
		commandList->IASetVertexBuffers(0, 2, vertexBufferViews);
	}
	else {
		commandList->IASetVertexBuffers(0, 1, &vertexBufferView);
	}
	commandList->IASetIndexBuffer(&indexBufferView);
	commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	// マテリアルCBufferの場所を設定
	commandList->SetGraphicsRootConstantBufferView(0, materialResource->GetGPUVirtualAddress());

	// SRVのDescriptorTableの先頭を設定（TextureManagerから取得）
	commandList->SetGraphicsRootDescriptorTable(
		2, TextureManager::GetInstance()->GetSrvHandleGPU(modelData.material.textureFilePath)
	);

	// 描画！
	commandList->DrawIndexedInstanced(
		static_cast<UINT>(modelData.indices.size()),
		1,
		0,
		0,
		0
	);
}

void Model::DrawForShadow(
	const D3D12_VERTEX_BUFFER_VIEW* influenceBufferView
) {
	auto* commandList = modelCommon->GetDxCommon()->GetCommandList();
	if (influenceBufferView) {
		const D3D12_VERTEX_BUFFER_VIEW vertexBufferViews[] = {
			vertexBufferView,
			*influenceBufferView
		};
		commandList->IASetVertexBuffers(0, 2, vertexBufferViews);
	}
	else {
		commandList->IASetVertexBuffers(0, 1, &vertexBufferView);
	}
	commandList->IASetIndexBuffer(&indexBufferView);
	commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	commandList->DrawIndexedInstanced(
		static_cast<UINT>(modelData.indices.size()),
		1,
		0,
		0,
		0
	);
}

void Model::DrawWithVertexBuffer(const D3D12_VERTEX_BUFFER_VIEW& customVertexBufferView) {
	auto* commandList = modelCommon->GetDxCommon()->GetCommandList();
	commandList->IASetVertexBuffers(0, 1, &customVertexBufferView);
	commandList->IASetIndexBuffer(&indexBufferView);
	commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	commandList->SetGraphicsRootConstantBufferView(0, materialResource->GetGPUVirtualAddress());
	commandList->SetGraphicsRootDescriptorTable(
		2,
		TextureManager::GetInstance()->GetSrvHandleGPU(modelData.material.textureFilePath)
	);
	commandList->DrawIndexedInstanced(
		static_cast<UINT>(modelData.indices.size()),
		1,
		0,
		0,
		0
	);
}

void Model::DrawForShadowWithVertexBuffer(const D3D12_VERTEX_BUFFER_VIEW& customVertexBufferView) {
	auto* commandList = modelCommon->GetDxCommon()->GetCommandList();
	commandList->IASetVertexBuffers(0, 1, &customVertexBufferView);
	commandList->IASetIndexBuffer(&indexBufferView);
	commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	commandList->DrawIndexedInstanced(
		static_cast<UINT>(modelData.indices.size()),
		1,
		0,
		0,
		0
	);
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
	const std::filesystem::path modelDirectory =
		std::filesystem::path(filePath).parent_path();
	const aiScene* scene = importer.ReadFile(filePath.c_str(), aiProcess_FlipWindingOrder | aiProcess_FlipUVs);
	assert(scene->HasMeshes());

	ModelData modelData; //構築するModelData
	modelData.rootNode = ReadNode(scene->mRootNode);

	for (uint32_t meshIndex = 0; meshIndex < scene->mNumMeshes; ++meshIndex) {
		aiMesh* mesh = scene->mMeshes[meshIndex];
		assert(mesh->HasNormals());
		assert(mesh->HasTextureCoords(0));
		modelData.hasSkinning =
			modelData.hasSkinning || mesh->HasBones();

		const uint32_t vertexOffset =
			static_cast<uint32_t>(modelData.vertices.size());
		modelData.vertices.resize(
			modelData.vertices.size() + mesh->mNumVertices
		);
		for (uint32_t vertexIndex = 0;
			vertexIndex < mesh->mNumVertices;
			++vertexIndex) {
			const aiVector3D& position = mesh->mVertices[vertexIndex];
			const aiVector3D& normal = mesh->mNormals[vertexIndex];
			const aiVector3D& texcoord =
				mesh->mTextureCoords[0][vertexIndex];
			VertexData& vertex =
				modelData.vertices[vertexOffset + vertexIndex];
			vertex.position = {
				-position.x,
				position.y,
				position.z,
				1.0f
			};
			vertex.normal = {
				-normal.x,
				normal.y,
				normal.z
			};
			vertex.texcoord = { texcoord.x, texcoord.y };
		}

		for (uint32_t faceIndex = 0; faceIndex < mesh->mNumFaces; ++faceIndex) {
			aiFace& face = mesh->mFaces[faceIndex];
			assert(face.mNumIndices == 3); // 三角形以外は非対応
			for (uint32_t element = 0; element < face.mNumIndices; ++element) {
				modelData.indices.push_back(
					vertexOffset + face.mIndices[element]
				);
			}
		}

		for (uint32_t boneIndex = 0;
			boneIndex < mesh->mNumBones;
			++boneIndex) {
			const aiBone* bone = mesh->mBones[boneIndex];
			const std::string jointName = bone->mName.C_Str();
			JointWeightData& jointWeightData =
				modelData.skinClusterData[jointName];

			jointWeightData.inverseBindPoseMatrix =
				ConvertAssimpMatrix(bone->mOffsetMatrix);

			for (uint32_t weightIndex = 0;
				weightIndex < bone->mNumWeights;
				++weightIndex) {
				const aiVertexWeight& weight =
					bone->mWeights[weightIndex];
				jointWeightData.vertexWeights.push_back({
					weight.mWeight,
					vertexOffset + weight.mVertexId
				});
			}
		}
	}

	for (uint32_t materialIndex = 0; materialIndex < scene->mNumMaterials; ++materialIndex) {
		aiMaterial* material = scene->mMaterials[materialIndex];
		if (material->GetTextureCount(aiTextureType_DIFFUSE) != 0) {
			aiString textureFilePath;
			material->GetTexture(aiTextureType_DIFFUSE, 0, &textureFilePath);
			const std::string texturePath = textureFilePath.C_Str();
			if (!texturePath.empty() && texturePath.front() == '*') {
				const uint32_t textureIndex = static_cast<uint32_t>(
					std::strtoul(texturePath.c_str() + 1, nullptr, 10)
				);
				if (textureIndex < scene->mNumTextures) {
					const aiTexture* embeddedTexture = scene->mTextures[textureIndex];
					const std::string textureKey =
						filePath + "#embedded" + std::to_string(textureIndex);
					bool loaded = false;
					if (embeddedTexture->mHeight == 0) {
						std::string formatHint = embeddedTexture->achFormatHint;
						std::transform(
							formatHint.begin(),
							formatHint.end(),
							formatHint.begin(),
							[](unsigned char c) {
								return static_cast<char>(std::tolower(c));
							}
						);
						loaded = TextureManager::GetInstance()->LoadTextureFromMemory(
							textureKey,
							reinterpret_cast<const uint8_t*>(embeddedTexture->pcData),
							static_cast<size_t>(embeddedTexture->mWidth),
							formatHint == "dds"
						);
					}
					if (loaded) {
						modelData.material.textureFilePath = textureKey;
					}
				}
			}
			else {
				modelData.material.textureFilePath =
					(modelDirectory / texturePath)
						.lexically_normal()
						.generic_string();
			}
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

void Model::CreateIndexResource() {
	indexResource = *&modelCommon->GetDxCommon()->CreateBufferResource(
		sizeof(uint32_t) * modelData.indices.size()
	);
	indexBufferView.BufferLocation =
		indexResource->GetGPUVirtualAddress();
	indexBufferView.SizeInBytes = static_cast<UINT>(
		sizeof(uint32_t) * modelData.indices.size()
	);
	indexBufferView.Format = DXGI_FORMAT_R32_UINT;

	indexResource->Map(
		0,
		nullptr,
		reinterpret_cast<void**>(&indexData)
	);
	std::memcpy(
		indexData,
		modelData.indices.data(),
		sizeof(uint32_t) * modelData.indices.size()
	);
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
	aiVector3D scale;
	aiQuaternion rotate;
	aiVector3D translate;
	aiNode->mTransformation.Decompose(scale, rotate, translate);

	result.transform.scale = { scale.x, scale.y, scale.z };
	result.transform.rotate = Normalize({
		rotate.x,
		-rotate.y,
		-rotate.z,
		rotate.w
	});
	result.transform.translate = {
		-translate.x,
		translate.y,
		translate.z
	};
	result.localMatrix = MakeAffineMatrix(
		result.transform.scale,
		result.transform.rotate,
		result.transform.translate
	);
	result.name = aiNode->mName.C_Str();
	result.children.resize(aiNode->mNumChildren);
	for (uint32_t childIndex = 0; childIndex < aiNode->mNumChildren; ++childIndex) {
		result.children[childIndex] = ReadNode(aiNode->mChildren[childIndex]);
	}
	return result;
}
