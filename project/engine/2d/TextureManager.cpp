#include "TextureManager.h"
#include "../base/DirectXCommon.h"
#include "../3d/SrvManager.h"
#include "../utility/StringUtility.h"
#include <cassert>
#include <algorithm>
#include <filesystem>

namespace {

	bool IsDDSFile(const std::string& filePath) {
		std::string extension = std::filesystem::path(filePath).extension().string();

		std::transform(
			extension.begin(),
			extension.end(),
			extension.begin(),
			[](unsigned char c) { return static_cast<char>(std::tolower(c)); }
		);

		return extension == ".dds";
	}

}

TextureManager* TextureManager::instance = nullptr;

uint32_t TextureManager::kSRVIndexTop = 1;

void TextureManager::LoadTexture(const std::string& filePath) {

	// 読み込み済みテクスチャを検索
	if (textureDatas.contains(filePath)) {
		return;
	}

	assert(srvManager->CanAllocate());

	DirectX::ScratchImage loadedImage{};
	DirectX::TexMetadata metadata{};
	std::wstring filePathW = StringUtility::ConvertString(filePath);

	HRESULT hr = S_OK;

	if (IsDDSFile(filePath)) {
		hr = DirectX::LoadFromDDSFile(
			filePathW.c_str(),
			DirectX::DDS_FLAGS_NONE,
			&metadata,
			loadedImage
		);
	}
	else {
		hr = DirectX::LoadFromWICFile(
			filePathW.c_str(),
			DirectX::WIC_FLAGS_FORCE_SRGB,
			&metadata,
			loadedImage
		);
	}

	assert(SUCCEEDED(hr));

	DirectX::ScratchImage mipImages{};
	const DirectX::ScratchImage* uploadImage = &loadedImage;

	// 圧縮フォーマットは DirectXTex の GenerateMipMaps が直接扱えないことがある
	if (!DirectX::IsCompressed(metadata.format) && metadata.mipLevels <= 1) {
		hr = DirectX::GenerateMipMaps(
			loadedImage.GetImages(),
			loadedImage.GetImageCount(),
			loadedImage.GetMetadata(),
			DirectX::TEX_FILTER_SRGB,
			0,
			mipImages
		);
		assert(SUCCEEDED(hr));

		uploadImage = &mipImages;
	}

	TextureData& textureData = textureDatas[filePath];

	textureData.metadata = uploadImage->GetMetadata();
	textureData.resource = dxCommon->CreateTextureResource(textureData.metadata);

	// SRV確保
	textureData.srvIndex = srvManager->Allocate();
	textureData.srvHandleCPU = srvManager->GetCPUDescriptorHandle(textureData.srvIndex);
	textureData.srvHandleGPU = srvManager->GetGPUDescriptorHandle(textureData.srvIndex);

	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
	srvDesc.Format = textureData.metadata.format;
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;

	if (textureData.metadata.IsCubemap()) {
		srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
		srvDesc.TextureCube.MostDetailedMip = 0;
		srvDesc.TextureCube.MipLevels = UINT(textureData.metadata.mipLevels);
		srvDesc.TextureCube.ResourceMinLODClamp = 0.0f;
	}
	else {
		srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		srvDesc.Texture2D.MipLevels = UINT(textureData.metadata.mipLevels);
	}

	dxCommon->GetDevice()->CreateShaderResourceView(
		textureData.resource.Get(),
		&srvDesc,
		textureData.srvHandleCPU
	);

	Microsoft::WRL::ComPtr<ID3D12Resource> intermediateResource;
	intermediateResource.Attach(
		dxCommon->UploadTextureData(textureData.resource, *uploadImage)
	);

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
