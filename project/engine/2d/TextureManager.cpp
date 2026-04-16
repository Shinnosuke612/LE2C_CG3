#include "TextureManager.h"
#include "../base/DirectXCommon.h"
#include "../3d/SrvManager.h"
TextureManager* TextureManager::instance = nullptr;

uint32_t TextureManager::kSRVIndexTop = 1;

void TextureManager::LoadTexture(const std::string& filePath){

	//読み込み済みテクスチャを検索
	if(textureDatas.contains(filePath)){
			//読み込み済みなら早期return
			return;
	}

	assert(srvManager->CanAllocate());

	//テクスチャを読み込んでプログラムで扱えるようにする
	DirectX::ScratchImage image{};
	std::wstring filePathW = StringUtility::ConvertString(filePath);
	HRESULT hr = DirectX::LoadFromWICFile(filePathW.c_str(), DirectX::WIC_FLAGS_FORCE_SRGB, nullptr, image);
	assert(SUCCEEDED(hr));

	//ミニマップの作成
	DirectX::ScratchImage mipImages{};
	hr = DirectX::GenerateMipMaps(image.GetImages(), image.GetImageCount(), image.GetMetadata(), DirectX::TEX_FILTER_SRGB, 0, mipImages);
	assert(SUCCEEDED(hr));

	//追加したテクスチャデータの参照を取得する
	TextureData& textureData = textureDatas[filePath];

	textureData.metadata = mipImages.GetMetadata();
	textureData.resource = dxCommon->CreateTextureResource(textureData.metadata);

	
	//SRV確保
	textureData.srvIndex = srvManager->Allocate();
	textureData.srvHandleCPU = srvManager->GetCPUDescriptorHandle(textureData.srvIndex);
	textureData.srvHandleGPU = srvManager->GetGPUDescriptorHandle(textureData.srvIndex);

	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
	srvDesc.Format = textureData.metadata.format;
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MipLevels = UINT(textureData.metadata.mipLevels);

	//SRVを作成するDescriptorHeapの設定
	D3D12_CPU_DESCRIPTOR_HANDLE textureSrvHandleCPU = srvManager->GetCPUDescriptorHandle(0);
	D3D12_GPU_DESCRIPTOR_HANDLE textureSrvHandleGPU = srvManager->GetGPUDescriptorHandle(0);
	//先頭はImGuiが使っているのでその次を使う
	textureSrvHandleCPU.ptr += dxCommon->GetDevice()->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	textureSrvHandleGPU.ptr += dxCommon->GetDevice()->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	//データの転送
	dxCommon->GetDevice()->CreateShaderResourceView(textureData.resource.Get(), &srvDesc, textureData.srvHandleCPU);
	// ここは textureData.resource を渡す（TextureData* じゃない）
	Microsoft::WRL::ComPtr<ID3D12Resource> intermediateResource;
	intermediateResource.Attach(
		dxCommon->UploadTextureData(textureData.resource, mipImages)
	);

	//簡単に実装するためLoadTexture内で完結する(要改善)
	dxCommon->ExecuteCommandListAndWait();

}

const DirectX::TexMetadata& TextureManager::GetMetaData(const std::string& filePath){
	auto it = textureDatas.find(filePath);
	assert(it != textureDatas.end());
	return it->second.metadata;
}

uint32_t TextureManager::GetSrvIndex(const std::string& filePath){
	auto it = textureDatas.find(filePath);
	assert(it != textureDatas.end());
	return it->second.srvIndex;
}

D3D12_GPU_DESCRIPTOR_HANDLE TextureManager::GetSrvHandleGPU(const std::string& filePath){
	auto it = textureDatas.find(filePath);
	assert(it != textureDatas.end());
	return it->second.srvHandleGPU;
}

void TextureManager::Initialize(DirectXCommon* directXCommon, SrvManager* srvManager){
	this->srvManager = srvManager;
	this->dxCommon = directXCommon;

}

TextureManager* TextureManager::GetInstance(){
	if(instance == nullptr){
		instance = new TextureManager;
	}
	return instance;
}

void TextureManager::Finalize(){
	delete instance;
	instance = nullptr;
}
