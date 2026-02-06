#include "Object3d.h"
#include "Object3dCommon.h"
#include "DirectXCommon.h"
#include "TextureManager.h"

void Object3d::Initialize(Object3dCommon* object3dCommon){
	this->object3dCommon = object3dCommon;
	modelData = LoadObjFile("resources", "plane.obj");
	CreateVertexResource();
	CreateMaterialResource();
	CreateTransformationMatrixResource();
	CreateDirectionalLightResource();

	// .objの参照していrテクスチャファイル読み込み
	TextureManager::GetInstance()->LoadTexture(modelData.material.textureFilePath);
	//読み込んだテクスチャ番号を取得
	modelData.material.textureIndex =
		TextureManager::GetInstance()->GetTextureIndexByFilePath(modelData.material.textureFilePath);

	//Transform変数を作る
	transform = { {1.0f,1.0f,1.0f},{0.0f,0.0f,0.0f},{0.0f,0.0f,0.0f} };
	cameraTransform = { {1.0f,1.0f,1.0f},{0.3f,0.0f,0.0f},{0.0f,4.0f,-10.0f} };
}

void Object3d::Update(){
	Matrix4x4 worldMatrix = MakeAffineMatrix(transform.scale, transform.rotate, transform.translate);
	Matrix4x4 cameraMatrix = MakeAffineMatrix(cameraTransform.scale, cameraTransform.rotate, cameraTransform.translate);
	Matrix4x4 viewMatrix = Inverse(cameraMatrix);
	Matrix4x4 projectionMatrix = MakePerspectiveFovMatrix(0.45f, float(WinApp::kClientWidth) / float(WinApp::kClientHeight), 0.1f, 100.0f);
	Matrix4x4 viewProjectionMatirx = Multiply(viewMatrix, projectionMatrix);
	Matrix4x4 worldViewProjectionMatrix = Multiply(worldMatrix, viewProjectionMatirx);

	transformationMatrixData->WVP = worldViewProjectionMatrix;
	transformationMatrixData->World = worldMatrix;
}

void Object3d::Draw(){

	auto* commandList = object3dCommon->GetDxCommon()->GetCommandList();

	// VertexBufferViewを設定
	commandList->IASetVertexBuffers(0, 1, &vertexBufferView);
	commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	// マテリアルCBufferの場所を設定
	commandList->SetGraphicsRootConstantBufferView(0, materialResource->GetGPUVirtualAddress());

	// 座標変換行列CBufferの場所を設定
	commandList->SetGraphicsRootConstantBufferView(1, transformationMatrixResource->GetGPUVirtualAddress());

	// SRVのDescriptorTableの先頭を設定（TextureManagerから取得）
	commandList->SetGraphicsRootDescriptorTable(
		2, TextureManager::GetInstance()->GetSrvHandleGPU(modelData.material.textureIndex)
	);

	// 平行光源CBufferの場所を設定
	commandList->SetGraphicsRootConstantBufferView(3, directionalLightResource->GetGPUVirtualAddress());

	// 描画！
	commandList->DrawInstanced(UINT(modelData.vertices.size()), 1, 0, 0);
}

Object3d::MaterialData Object3d::LoadMaterialTemplateFile(const std::string& directoryPath, const std::string& filename){
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

Object3d::ModelData Object3d::LoadObjFile(const std::string& directoryPath, const std::string& filename){
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
		}else if(identifier == "mtllib"){
			std::string materialFilename;
			s >> materialFilename;
			modelData.material = LoadMaterialTemplateFile(directoryPath, materialFilename);
		}

	}
	return modelData;
}

void Object3d::CreateVertexResource(){
	vertexResource = *&object3dCommon->GetDxCommon()->CreateBufferResource(sizeof(VertexData) * modelData.vertices.size());

	vertexBufferView.BufferLocation = vertexResource->GetGPUVirtualAddress();
	vertexBufferView.SizeInBytes = UINT(sizeof(VertexData) * modelData.vertices.size());
	vertexBufferView.StrideInBytes = sizeof(VertexData);

	vertexResource->Map(0, nullptr, reinterpret_cast<void**>(&vertexData));
	std::memcpy(vertexData, modelData.vertices.data(), sizeof(VertexData) * modelData.vertices.size());
}

void Object3d::CreateMaterialResource(){
	//マテリアル用のリソースを作る。今回はカラー1つ分のサイズを用意する
	materialResource = *&object3dCommon->GetDxCommon()->CreateBufferResource(sizeof(Material));
	//書き込むためのアドレスを取得
	materialResource->Map(0, nullptr, reinterpret_cast<void**>(&materialData));
	//今回は赤を書き込んでる
	materialData->color = Vector4(1.0f, 1.0f, 1.0f, 1.0f);
	//ライティングの有無
	materialData->enableLighting = true;
	//uvTransform行列を単位行列で初期化
	materialData->uvTransform = MakeIdentity4x4();
}

void Object3d::CreateTransformationMatrixResource(){
	//WVP用リソースのリソースを作る。Matrix4x4 1つ分のサイズを用意する
	transformationMatrixResource = *&object3dCommon->GetDxCommon()->CreateBufferResource(sizeof(TransformationMatrix));
	//書き込むためのアドレス取得
	transformationMatrixResource->Map(0, nullptr, reinterpret_cast<void**>(&transformationMatrixData));
	//単位行列を書き込んでおく
	transformationMatrixData->WVP = MakeIdentity4x4();
	transformationMatrixData->World = MakeIdentity4x4();
}

void Object3d::CreateDirectionalLightResource(){
	//平行光源のリソースを作る
	directionalLightResource = *&object3dCommon->GetDxCommon()->CreateBufferResource(sizeof(DirectionalLight));
	//書き込むためのアドレス取得
	directionalLightResource->Map(0, nullptr, reinterpret_cast<void**>(&directionalLightData));
	//平行光源の設定
	directionalLightData->color = { 1.0f,1.0f,1.0f,1.0f };
	directionalLightData->direction = { 0.0f,-1.0f,0.0f };
	directionalLightData->intensity = 1.0f;
}
