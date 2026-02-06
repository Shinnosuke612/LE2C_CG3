#pragma once
#include <string>
#include <DirectXTex.h>
#include <wrl.h>  
#include "d3dx12.h"

class DirectXCommon;

class TextureManager{
private:
	//テクスチャ一枚分のデータ
	struct  TextureData{
		std::string filePath;
		DirectX::TexMetadata metadata;
		Microsoft::WRL::ComPtr<ID3D12Resource> resource;
		D3D12_CPU_DESCRIPTOR_HANDLE srvHandleCPU;
		D3D12_GPU_DESCRIPTOR_HANDLE srvHandleGPU;
	};
private:
	static TextureManager* instance;

	TextureManager() = default;
	~TextureManager() = default;
	TextureManager(TextureManager&) = delete;
	TextureManager& operator=(TextureManager&) = delete;

public:
	//初期化
	void Initialize(DirectXCommon* directXCommon);
	//シングルトンインスタンスの取得
	static TextureManager* GetInstance();
	//終了
	void Finalize();

	//SRVインデックスの開始番号
	uint32_t GetTextureIndexByFilePath(const std::string& filePath);

	//テクスチャ番号からGPUハンドルを取得
	D3D12_GPU_DESCRIPTOR_HANDLE GetSrvHandleGPU(uint32_t textureIndex);

	/// <summary>
/// テクスチャファイルの読み込み
/// </summary>
	void LoadTexture(const std::string& filePath);
private:
	//テクスチャデータ
	std::vector<TextureData> textureDatas;
	//DirectXCommonを参照
	DirectXCommon* dxCommon;
	//SRVインデックス開始番号
	static uint32_t kSRVIndexTop;
};

