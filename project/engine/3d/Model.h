#pragma once
#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <cassert>
#include <map>
#include "../math/Vector2.h"
#include "../math/Vector3.h"
#include "../math/Vector4.h"
#include "../math/Matrix4x4.h"
#include "../math/Transform.h"
#include "Animation.h"
#include <d3d12.h> 
class ModelCommon;
class aiNode;
class Model{
public:
	struct Node {
		QuaternionTransform transform;
		Matrix4x4 localMatrix;
		std::string name;
		std::vector<Node> children;
	};

	struct VertexWeightData {
		float weight = 0.0f;
		uint32_t vertexIndex = 0;
	};

	struct JointWeightData {
		Matrix4x4 inverseBindPoseMatrix = MakeIdentity4x4();
		std::vector<VertexWeightData> vertexWeights;
	};

	using SkinClusterData = std::map<std::string, JointWeightData>;

	struct VertexData{
		Vector4 position;
		Vector2 texcoord;
		Vector3 normal;
	};

private://インナークラス
	struct MaterialData{
		std::string textureFilePath;
	};
	struct ModelData{
		std::vector<VertexData> vertices;
		std::vector<uint32_t> indices;
		MaterialData material;
		Node rootNode;
		SkinClusterData skinClusterData;
		bool hasSkinning = false;
	};
	//マテリアルデータ
	struct Material {
		Vector4 color;
		int32_t enableLighting;
		float emissiveIntensity;
		float padding[2];
		Matrix4x4 uvTransform;
		Vector4 emissiveColor;
		float shininess;
		float padding2[3];
	};
private://非公開メンバ関数
	// .mtlファイルの読み取り
	static MaterialData LoadMaterialTemplateFile(const std::string& directoryPath, const std::string& filename);
	// .objファイルの読み取り
	ModelData LoadModelFile(const std::string& directoryPath, const std::string& filename);

	//頂点リソース作成関数
	void CreateVertexResource();
	void CreateIndexResource();
	//マテリアルリソース作成関数
	void CreateMaterialResource();

	Node ReadNode(aiNode* aiNode);
public://公開メンバ関数
	//初期化
	void Initialize(ModelCommon* modelCommon,const std::string& directoryPath, const std::string& filename);
	//描画
	void Draw(const D3D12_VERTEX_BUFFER_VIEW* influenceBufferView = nullptr);
	void DrawForShadow(const D3D12_VERTEX_BUFFER_VIEW* influenceBufferView = nullptr);
	void DrawWithVertexBuffer(const D3D12_VERTEX_BUFFER_VIEW& vertexBufferView);
	void DrawWithMaterial(ID3D12Resource* materialOverride, const D3D12_VERTEX_BUFFER_VIEW* influenceBufferView = nullptr);
	void DrawWithVertexBufferAndMaterial(const D3D12_VERTEX_BUFFER_VIEW& vertexBufferView, ID3D12Resource* materialOverride);
	void DrawForShadowWithVertexBuffer(const D3D12_VERTEX_BUFFER_VIEW& vertexBufferView);

	// getter
	const Matrix4x4& GetRootNodeLocalMatrix() const { return modelData.rootNode.localMatrix; }
	const Node& GetRootNode() const { return modelData.rootNode; }
	const Animation& GetAnimation() const { return animation_; }
	bool HasAnimation() const { return animation_.IsValid(); }
	bool HasSkinning() const { return modelData.hasSkinning; }
	uint32_t GetVertexCount() const {
		return static_cast<uint32_t>(modelData.vertices.size());
	}
	const SkinClusterData& GetSkinClusterData() const {
		return modelData.skinClusterData;
	}
	ID3D12Resource* GetVertexResource() const { return vertexResource; }
	const D3D12_VERTEX_BUFFER_VIEW& GetVertexBufferView() const {
		return vertexBufferView;
	}
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
	ID3D12Resource* indexResource = nullptr;
	uint32_t* indexData = nullptr;
	D3D12_INDEX_BUFFER_VIEW indexBufferView{};

	//バッファリソース
	ID3D12Resource* materialResource;
	//バッファリソース内のデータおw指すポインタ
	Material* materialData = nullptr;
	Animation animation_{};
};

