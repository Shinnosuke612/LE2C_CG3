#include "TextureManager.h"
#include "DirectXCommon.h"
TextureManager* TextureManager::instance = nullptr;

uint32_t TextureManager::kSRVIndexTop = 1;

void TextureManager::LoadTexture(const std::string& filePath){

	//読み込み済みテクスチャを検索
	auto it = std::find_if(
		textureDatas.begin(),
		textureDatas.end(),
		[&](TextureData& textureData){ return textureData.filePath == filePath; }
	);
	if(it != textureDatas.end()){
		//読み込み済みなら早期return
		return;
	}

	assert(textureDatas.size() + kSRVIndexTop < DirectXCommon::kMaxSRVCount);

	//テクスチャを読み込んでプログラムで扱えるようにする
	DirectX::ScratchImage image{};
	std::wstring filePathW = StringUtility::ConvertString(filePath);
	HRESULT hr = DirectX::LoadFromWICFile(filePathW.c_str(), DirectX::WIC_FLAGS_FORCE_SRGB, nullptr, image);
	assert(SUCCEEDED(hr));

	//ミニマップの作成
	DirectX::ScratchImage mipImages{};
	hr = DirectX::GenerateMipMaps(image.GetImages(), image.GetImageCount(), image.GetMetadata(), DirectX::TEX_FILTER_SRGB, 0, mipImages);
	assert(SUCCEEDED(hr));

	//テクスチャデータを追加
	textureDatas.reserve(textureDatas.size() + 1);
	//追加したテクスチャデータの参照を取得する
	TextureData& textureData = textureDatas.back();

	textureData.filePath = filePath;
	textureData.metadata = mipImages.GetMetadata();
	textureData.resource = dxCommon->CreateTextureResource(textureData.metadata);

	//テクスチャデータの要素番号をSRVのインデックスとする
	uint32_t srvIndex = static_cast<uint32_t>(textureDatas.size() - 1) + kSRVIndexTop;

	textureData.srvHandleCPU = dxCommon->GetSRVCPUDescriptorHandle(srvIndex);
	textureData.srvHandleGPU = dxCommon->GetSRVGPUDescriptorHandle(srvIndex);

	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};

	//SRVを作成するDescriptorHeapの設定
	D3D12_CPU_DESCRIPTOR_HANDLE textureSrvHandleCPU = dxCommon->GetSRVCPUDescriptorHandle(0);
	D3D12_GPU_DESCRIPTOR_HANDLE textureSrvHandleGPU = dxCommon->GetSRVGPUDescriptorHandle(0);
	//先頭はImGuiが使っているのでその次を使う
	textureSrvHandleCPU.ptr += dxCommon->GetDevice()->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	textureSrvHandleGPU.ptr += dxCommon->GetDevice()->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	//SRVの作成
	dxCommon->GetDevice()->CreateShaderResourceView(textureData.resource.Get(), &srvDesc, textureData.srvHandleCPU);
	Microsoft::WRL::ComPtr<ID3D12Resource> intermediateResource = dxCommon->UploadTextureData(&textureData, mipImages);

	//簡単に実装するためLoadTexture内で完結する(要改善)
	dxCommon->ExecuteCommandListAndWait();

}

uint32_t TextureManager::GetTextureIndexByFilePath(const std::string& filePath){
	auto it = std::find_if(
		textureDatas.begin(),
		textureDatas.end(),
		[&](TextureData& textureData){ return textureData.filePath == filePath; }
	);
	if(it != textureDatas.end()){
		//読み込み済みなら要素番号を返す
		uint32_t textureIndex = static_cast<uint32_t>(std::distance(textureDatas.begin(), it));
		return textureIndex;
	}
	assert(0);
	return 0;
}

D3D12_GPU_DESCRIPTOR_HANDLE TextureManager::GerSrvHandleGPU(uint32_t textureIndex){
	//範囲外指定違反チェック
	assert(textureIndex < kSRVIndexTop);

	TextureData& textureData = textureDatas[textureIndex];
	return textureData.srvHandleGPU;
}

void TextureManager::Initialize(DirectXCommon* directXCommon){
	textureDatas.reserve(DirectXCommon::kMaxSRVCount);
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
