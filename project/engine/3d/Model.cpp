// 役割: モデルのメッシュ描画、材質設定、Skinning描画を実装する。
#include "Model.h"
#include "ModelCommon.h"
#include "ModelFormat.h"
#include "PmxLoader.h"
#include "AssimpUnicodeIO.h"
#include "../base/DirectXCommon.h"
#include "../2d/TextureManager.h"
#include "../utility/Logger.h"
#include "../utility/StringUtility.h"
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <filesystem>
#include <cstdlib>
#include <algorithm>
#include <cctype>
#include <functional>
#include <limits>

namespace {

constexpr const char* kDefaultModelTexture = "resources/human/white.png";

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

bool Model::Initialize(
	ModelCommon* modelCommon,
	const std::filesystem::path& modelFilePath
) {
	this->modelCommon = modelCommon;
	if (!LoadModelFile(modelFilePath, modelData)) {
		return false;
	}
	const bool isBasicPmx = ModelFormat::NormalizeExtension(
		modelFilePath.extension().string()
	) == ".pmx";
	animations_ = isBasicPmx
		? std::vector<Animation>{}
		: LoadAnimationFiles(modelFilePath);
	CreateVertexResource();
	CreateIndexResource();
	CreateMaterialResource();

	TextureManager* textureManager = TextureManager::GetInstance();
	for (MaterialSlot& material : modelData.materials) {
		if (
			!material.textureFilePath.empty() &&
			textureManager->LoadTexture(material.textureFilePath)
		) {
			continue;
		}

		// テクスチャ未指定は材質色をそのまま使える白画像へ、読込失敗はUVチェッカーへ退避する。
		const bool hasSpecifiedTexture = !material.textureFilePath.empty();
		material.textureFilePath = hasSpecifiedTexture
			? "resources/uvChecker.png"
			: kDefaultModelTexture;
		textureManager->LoadTexture(material.textureFilePath);
	}
	return true;
}
void Model::Draw(const D3D12_VERTEX_BUFFER_VIEW* influenceBufferView) {
	DrawWithMaterial(nullptr, influenceBufferView);
}

bool Model::GetLocalBounds(Vector3& boundsMin, Vector3& boundsMax) const {
	if (modelData.vertices.empty()) {
		boundsMin = {};
		boundsMax = {};
		return false;
	}

	const float maximum = (std::numeric_limits<float>::max)();
	boundsMin = { maximum, maximum, maximum };
	boundsMax = { -maximum, -maximum, -maximum };
	for (const VertexData& vertex : modelData.vertices) {
		boundsMin.x = (std::min)(boundsMin.x, vertex.position.x);
		boundsMin.y = (std::min)(boundsMin.y, vertex.position.y);
		boundsMin.z = (std::min)(boundsMin.z, vertex.position.z);
		boundsMax.x = (std::max)(boundsMax.x, vertex.position.x);
		boundsMax.y = (std::max)(boundsMax.y, vertex.position.y);
		boundsMax.z = (std::max)(boundsMax.z, vertex.position.z);
	}
	return true;
}

void Model::DrawWithMaterial(
	ID3D12Resource* materialOverride,
	const D3D12_VERTEX_BUFFER_VIEW* influenceBufferView
) {
	DrawWithMaterialAndTexture(materialOverride, {}, influenceBufferView);
}

void Model::DrawWithMaterialAndTexture(
	ID3D12Resource* materialOverride,
	D3D12_GPU_DESCRIPTOR_HANDLE textureOverride,
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

	// マテリアルCBufferの場所を設定
	ID3D12Resource* materialToBind =
		materialOverride ? materialOverride : nullptr;
	for (const SubMesh& subMesh : modelData.subMeshes) {
		const uint32_t materialIndex = (std::min)(
			subMesh.materialIndex,
			static_cast<uint32_t>(materialResources.size() - 1)
		);
		ID3D12Resource* subMeshMaterial = materialToBind
			? materialToBind
			: materialResources[materialIndex];
		commandList->SetGraphicsRootConstantBufferView(
			0,
			subMeshMaterial->GetGPUVirtualAddress()
		);
		const D3D12_GPU_DESCRIPTOR_HANDLE textureHandle = textureOverride.ptr != 0
			? textureOverride
			: TextureManager::GetInstance()->GetSrvHandleGPU(
				modelData.materials[materialIndex].textureFilePath
			);
		commandList->SetGraphicsRootDescriptorTable(2, textureHandle);
		commandList->DrawIndexedInstanced(
			subMesh.indexCount, 1, subMesh.firstIndex, 0, 0
		);
	}
}

void Model::DrawWithMaterialSlots(
	const std::vector<ID3D12Resource*>& materialOverrides,
	const std::vector<D3D12_GPU_DESCRIPTOR_HANDLE>& textureOverrides,
	const D3D12_VERTEX_BUFFER_VIEW* influenceBufferView
) {
	auto* commandList = modelCommon->GetDxCommon()->GetCommandList();
	if (influenceBufferView) {
		const D3D12_VERTEX_BUFFER_VIEW vertexBufferViews[] = {
			vertexBufferView, *influenceBufferView
		};
		commandList->IASetVertexBuffers(0, 2, vertexBufferViews);
	} else {
		commandList->IASetVertexBuffers(0, 1, &vertexBufferView);
	}
	commandList->IASetIndexBuffer(&indexBufferView);
	commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	for (const SubMesh& subMesh : modelData.subMeshes) {
		const uint32_t materialIndex = (std::min)(
			subMesh.materialIndex,
			static_cast<uint32_t>(materialResources.size() - 1)
		);
		ID3D12Resource* material = materialIndex < materialOverrides.size() &&
			materialOverrides[materialIndex]
			? materialOverrides[materialIndex]
			: materialResources[materialIndex];
		const D3D12_GPU_DESCRIPTOR_HANDLE textureOverride =
			materialIndex < textureOverrides.size()
				? textureOverrides[materialIndex]
				: D3D12_GPU_DESCRIPTOR_HANDLE{};
		const D3D12_GPU_DESCRIPTOR_HANDLE textureHandle = textureOverride.ptr != 0
			? textureOverride
			: TextureManager::GetInstance()->GetSrvHandleGPU(
				modelData.materials[materialIndex].textureFilePath
			);
		commandList->SetGraphicsRootConstantBufferView(
			0, material->GetGPUVirtualAddress()
		);
		commandList->SetGraphicsRootDescriptorTable(2, textureHandle);
		commandList->DrawIndexedInstanced(
			subMesh.indexCount, 1, subMesh.firstIndex, 0, 0
		);
	}
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
	for (const SubMesh& subMesh : modelData.subMeshes) {
		commandList->DrawIndexedInstanced(
			subMesh.indexCount, 1, subMesh.firstIndex, 0, 0
		);
	}
}

void Model::DrawWithVertexBufferAndMaterial(
	const D3D12_VERTEX_BUFFER_VIEW& customVertexBufferView,
	ID3D12Resource* materialOverride,
	D3D12_GPU_DESCRIPTOR_HANDLE textureOverride
) {
	auto* commandList = modelCommon->GetDxCommon()->GetCommandList();
	commandList->IASetVertexBuffers(0, 1, &customVertexBufferView);
	commandList->IASetIndexBuffer(&indexBufferView);
	commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	for (const SubMesh& subMesh : modelData.subMeshes) {
		const uint32_t materialIndex = (std::min)(
			subMesh.materialIndex,
			static_cast<uint32_t>(materialResources.size() - 1)
		);
		ID3D12Resource* materialToBind = materialOverride
			? materialOverride
			: materialResources[materialIndex];
		const D3D12_GPU_DESCRIPTOR_HANDLE textureHandle = textureOverride.ptr != 0
			? textureOverride
			: TextureManager::GetInstance()->GetSrvHandleGPU(
				modelData.materials[materialIndex].textureFilePath
			);
		commandList->SetGraphicsRootConstantBufferView(
			0, materialToBind->GetGPUVirtualAddress()
		);
		commandList->SetGraphicsRootDescriptorTable(2, textureHandle);
		commandList->DrawIndexedInstanced(
			subMesh.indexCount, 1, subMesh.firstIndex, 0, 0
		);
	}
}

void Model::DrawWithVertexBufferAndMaterialSlots(
	const D3D12_VERTEX_BUFFER_VIEW& customVertexBufferView,
	const std::vector<ID3D12Resource*>& materialOverrides,
	const std::vector<D3D12_GPU_DESCRIPTOR_HANDLE>& textureOverrides
) {
	auto* commandList = modelCommon->GetDxCommon()->GetCommandList();
	commandList->IASetVertexBuffers(0, 1, &customVertexBufferView);
	commandList->IASetIndexBuffer(&indexBufferView);
	commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	for (const SubMesh& subMesh : modelData.subMeshes) {
		const uint32_t materialIndex = (std::min)(
			subMesh.materialIndex,
			static_cast<uint32_t>(materialResources.size() - 1)
		);
		ID3D12Resource* material = materialIndex < materialOverrides.size() &&
			materialOverrides[materialIndex]
			? materialOverrides[materialIndex]
			: materialResources[materialIndex];
		const D3D12_GPU_DESCRIPTOR_HANDLE textureOverride =
			materialIndex < textureOverrides.size()
				? textureOverrides[materialIndex]
				: D3D12_GPU_DESCRIPTOR_HANDLE{};
		const D3D12_GPU_DESCRIPTOR_HANDLE textureHandle = textureOverride.ptr != 0
			? textureOverride
			: TextureManager::GetInstance()->GetSrvHandleGPU(
				modelData.materials[materialIndex].textureFilePath
			);
		commandList->SetGraphicsRootConstantBufferView(
			0, material->GetGPUVirtualAddress()
		);
		commandList->SetGraphicsRootDescriptorTable(2, textureHandle);
		commandList->DrawIndexedInstanced(
			subMesh.indexCount, 1, subMesh.firstIndex, 0, 0
		);
	}
}

void Model::DrawForShadowWithVertexBuffer(const D3D12_VERTEX_BUFFER_VIEW& customVertexBufferView) {
	auto* commandList = modelCommon->GetDxCommon()->GetCommandList();
	commandList->IASetVertexBuffers(0, 1, &customVertexBufferView);
	commandList->IASetIndexBuffer(&indexBufferView);
	commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	for (const SubMesh& subMesh : modelData.subMeshes) {
		commandList->DrawIndexedInstanced(
			subMesh.indexCount, 1, subMesh.firstIndex, 0, 0
		);
	}
}

bool Model::LoadModelFile(
	const std::filesystem::path& requestedModelFilePath,
	ModelData& result
) {
	const std::filesystem::path modelFilePath =
		requestedModelFilePath.lexically_normal();
	const std::string filePath = StringUtility::ToUtf8(modelFilePath);
	const std::string extension = ModelFormat::NormalizeExtension(
		modelFilePath.extension().string()
	);
	if (ModelFormat::FindByExtension(extension) == nullptr) {
		Logger::Log("Unsupported model format: " + filePath + "\n");
		return false;
	}
	const std::filesystem::path modelDirectory = modelFilePath.parent_path();
	if (extension == ".pmx") {
		PmxLoader::ModelData pmxData;
		std::string errorMessage;
		if (!PmxLoader::LoadBasic(modelFilePath, pmxData, errorMessage)) {
			Logger::Log(
				"Failed to load PMX model: " + filePath +
				"\nPMX: " + errorMessage + "\n"
			);
			return false;
		}

		ModelData modelData;
		modelData.rootNode.name = pmxData.name.empty()
			? StringUtility::ToUtf8(modelFilePath.stem())
			: pmxData.name;
		modelData.rootNode.localMatrix = MakeIdentity4x4();

		// PMXの絶対座標ボーンを、既存Skeletonが扱う親相対Nodeへ変換する。
		std::vector<std::vector<size_t>> boneChildren(pmxData.bones.size());
		std::vector<size_t> rootBones;
		for (size_t boneIndex = 0; boneIndex < pmxData.bones.size(); ++boneIndex) {
			const int32_t parentIndex = pmxData.bones[boneIndex].parentIndex;
			if (parentIndex >= 0) {
				boneChildren[static_cast<size_t>(parentIndex)].push_back(boneIndex);
			}
			else {
				rootBones.push_back(boneIndex);
			}
		}

		std::function<Node(size_t)> buildBoneNode = [&](size_t boneIndex) {
			const PmxLoader::Bone& sourceBone = pmxData.bones[boneIndex];
			Node node{};
			node.name = sourceBone.name;
			node.transform.translate = sourceBone.position;
			if (sourceBone.parentIndex >= 0) {
				const Vector3& parentPosition =
					pmxData.bones[static_cast<size_t>(sourceBone.parentIndex)].position;
				node.transform.translate = {
					sourceBone.position.x - parentPosition.x,
					sourceBone.position.y - parentPosition.y,
					sourceBone.position.z - parentPosition.z
				};
			}
			node.localMatrix = MakeAffineMatrix(
				node.transform.scale,
				node.transform.rotate,
				node.transform.translate
			);
			node.children.reserve(boneChildren[boneIndex].size());
			for (const size_t childIndex : boneChildren[boneIndex]) {
				node.children.push_back(buildBoneNode(childIndex));
			}
			return node;
		};

		modelData.rootNode.children.reserve(rootBones.size());
		for (const size_t rootBoneIndex : rootBones) {
			modelData.rootNode.children.push_back(buildBoneNode(rootBoneIndex));
		}

		modelData.vertices.reserve(pmxData.vertices.size());
		for (const PmxLoader::Vertex& sourceVertex : pmxData.vertices) {
			modelData.vertices.push_back({
				{
					sourceVertex.position.x,
					sourceVertex.position.y,
					sourceVertex.position.z,
					1.0f
				},
				sourceVertex.texcoord,
				sourceVertex.normal
			});
		}
		modelData.indices = std::move(pmxData.indices);
		modelData.materials.reserve(pmxData.materials.size());
		modelData.subMeshes.reserve(pmxData.materials.size());
		uint32_t firstIndex = 0;
		for (size_t materialIndex = 0;
			materialIndex < pmxData.materials.size();
			++materialIndex) {
			const PmxLoader::Material& sourceMaterial =
				pmxData.materials[materialIndex];
			MaterialSlot material{};
			material.name = sourceMaterial.name;
			material.baseColor = sourceMaterial.baseColor;
			if (!sourceMaterial.texturePath.empty()) {
				const std::filesystem::path texturePath =
					StringUtility::ToPath(sourceMaterial.texturePath);
				material.textureFilePath = StringUtility::ToUtf8(
					(texturePath.is_absolute()
						? texturePath
						: modelDirectory / texturePath).lexically_normal()
				);
			}
			modelData.materials.push_back(std::move(material));

			SubMesh subMesh{};
			subMesh.name = sourceMaterial.name;
			subMesh.firstIndex = firstIndex;
			subMesh.indexCount = sourceMaterial.indexCount;
			subMesh.materialIndex = static_cast<uint32_t>(materialIndex);
			modelData.subMeshes.push_back(std::move(subMesh));
			firstIndex += sourceMaterial.indexCount;
		}
		result = std::move(modelData);
		return true;
	}

	Assimp::Importer importer;
	if (!importer.IsExtensionSupported(extension.c_str())) {
		Logger::Log(
			"Assimp does not support this model format in the current build: " +
			filePath + "\n"
		);
		return false;
	}
	importer.SetIOHandler(new AssimpUnicodeIOSystem(modelDirectory));
	const aiScene* scene = importer.ReadFile(
		filePath.c_str(),
		aiProcess_FlipWindingOrder |
		aiProcess_FlipUVs |
		aiProcess_Triangulate |
		aiProcess_GenSmoothNormals
	);
	if (!scene) {
		Logger::Log(
			"Failed to load model: " + filePath +
			"\nAssimp: " + importer.GetErrorString() + "\n"
		);
		return false;
	}
	if (!scene->HasMeshes() || !scene->mRootNode) {
		Logger::Log("Model contains no mesh or root node: " + filePath + "\n");
		return false;
	}

	ModelData modelData; //構築するModelData
	modelData.rootNode = ReadNode(scene->mRootNode);
	modelData.materials.reserve(scene->mNumMaterials);
	for (uint32_t materialIndex = 0; materialIndex < scene->mNumMaterials; ++materialIndex) {
		aiMaterial* sourceMaterial = scene->mMaterials[materialIndex];
		MaterialSlot material{};
		aiString materialName;
		if (sourceMaterial->Get(AI_MATKEY_NAME, materialName) == AI_SUCCESS) {
			material.name = materialName.C_Str();
		}
		if (material.name.empty()) {
			material.name = "Material " + std::to_string(materialIndex + 1);
		}
		aiColor4D diffuseColor{};
		if (sourceMaterial->Get(AI_MATKEY_COLOR_DIFFUSE, diffuseColor) == AI_SUCCESS) {
			material.baseColor = {
				diffuseColor.r, diffuseColor.g, diffuseColor.b, diffuseColor.a
			};
		}

		const aiTextureType textureTypes[] = {
			aiTextureType_BASE_COLOR,
			aiTextureType_DIFFUSE
		};
		for (aiTextureType textureType : textureTypes) {
			if (sourceMaterial->GetTextureCount(textureType) == 0) {
				continue;
			}
			aiString textureFilePath;
			sourceMaterial->GetTexture(textureType, 0, &textureFilePath);
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
							formatHint.begin(), formatHint.end(), formatHint.begin(),
							[](unsigned char c) { return static_cast<char>(std::tolower(c)); }
						);
						loaded = TextureManager::GetInstance()->LoadTextureFromMemory(
							textureKey,
							reinterpret_cast<const uint8_t*>(embeddedTexture->pcData),
							static_cast<size_t>(embeddedTexture->mWidth),
							formatHint
						);
					}
					if (loaded) {
						material.textureFilePath = textureKey;
					}
				}
			} else {
				material.textureFilePath = StringUtility::ToUtf8(
					(modelDirectory / StringUtility::ToPath(texturePath))
						.lexically_normal()
				);
			}
			break;
		}
		modelData.materials.push_back(std::move(material));
	}
	if (modelData.materials.empty()) {
		modelData.materials.push_back({});
	}

	for (uint32_t meshIndex = 0; meshIndex < scene->mNumMeshes; ++meshIndex) {
		aiMesh* mesh = scene->mMeshes[meshIndex];
		const aiVector3D* normals = mesh->HasNormals()
			? mesh->mNormals
			: nullptr;
		const aiVector3D* textureCoordinates = mesh->HasTextureCoords(0)
			? mesh->mTextureCoords[0]
			: nullptr;
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
			const aiVector3D normal = normals
				? normals[vertexIndex]
				: aiVector3D{ 0.0f, 1.0f, 0.0f };
			// glTFではUVが任意なので、未設定メッシュは原点UVとして扱う。
			const aiVector3D texcoord = textureCoordinates
				? textureCoordinates[vertexIndex]
				: aiVector3D{};
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

		SubMesh subMesh{};
		subMesh.name = mesh->mName.C_Str();
		subMesh.firstIndex = static_cast<uint32_t>(modelData.indices.size());
		subMesh.materialIndex = mesh->mMaterialIndex < modelData.materials.size()
			? mesh->mMaterialIndex
			: 0;
		for (uint32_t faceIndex = 0; faceIndex < mesh->mNumFaces; ++faceIndex) {
			aiFace& face = mesh->mFaces[faceIndex];
			if (face.mNumIndices != 3) {
				Logger::Log(
					"Skipped a non-triangulated face while loading model: " +
					filePath + "\n"
				);
				continue;
			}
			for (uint32_t element = 0; element < face.mNumIndices; ++element) {
				modelData.indices.push_back(
					vertexOffset + face.mIndices[element]
				);
			}
		}
		subMesh.indexCount = static_cast<uint32_t>(modelData.indices.size()) -
			subMesh.firstIndex;
		modelData.subMeshes.push_back(std::move(subMesh));

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

	if (modelData.vertices.empty() || modelData.indices.empty()) {
		Logger::Log("Model contains no drawable geometry: " + filePath + "\n");
		return false;
	}
	result = std::move(modelData);
	return true;
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
	materialResources.resize(modelData.materials.size());
	materialDataList.resize(modelData.materials.size());
	for (size_t materialIndex = 0; materialIndex < modelData.materials.size(); ++materialIndex) {
		materialResources[materialIndex] = *&modelCommon->GetDxCommon()
			->CreateBufferResource(sizeof(Material));
		materialResources[materialIndex]->Map(
			0, nullptr, reinterpret_cast<void**>(&materialDataList[materialIndex])
		);
		Material* material = materialDataList[materialIndex];
		material->color = modelData.materials[materialIndex].baseColor;
		material->enableLighting = true;
		material->emissiveIntensity = 0.0f;
		material->uvTransform = MakeIdentity4x4();
		material->emissiveColor = Vector4(1.0f, 1.0f, 1.0f, 1.0f);
		material->shininess = 40.0f;
		material->dissolveAmount = 0.0f;
		material->dissolveEdgeWidth = 0.08f;
		material->dissolveNoiseScale = 6.0f;
	}
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
