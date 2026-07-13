// 役割: DirectXTexを使ったテクスチャ読み込みとGPUリソース生成を実装する。
#include "TextureManager.h"
#include "../base/DirectXCommon.h"
#include "../3d/SrvManager.h"
#include "../utility/StringUtility.h"
#include "../utility/Logger.h"
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

bool TextureManager::LoadTexture(const std::string& filePath) {

	// 読み込み済みテクスチャを検索
	if (textureDatas.contains(filePath)) {
		return true;
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

	if (FAILED(hr)) {
		Logger::Log("Failed to load texture: " + filePath + "\n");
		return false;
	}

	return RegisterTexture(filePath, loadedImage, metadata);
}

bool TextureManager::ReloadTexture(const std::string& filePath) {
	textureDatas.erase(filePath);
	return LoadTexture(filePath);
}

bool TextureManager::LoadTextureFromMemory(
	const std::string& textureKey,
	const uint8_t* data,
	size_t dataSize,
	bool isDDS
) {
	if (textureDatas.contains(textureKey)) {
		return true;
	}
	if (data == nullptr || dataSize == 0) {
		Logger::Log("Embedded texture data is empty: " + textureKey + "\n");
		return false;
	}

	DirectX::ScratchImage loadedImage{};
	DirectX::TexMetadata metadata{};
	HRESULT hr = S_OK;
	if (isDDS) {
		hr = DirectX::LoadFromDDSMemory(
			data,
			dataSize,
			DirectX::DDS_FLAGS_NONE,
			&metadata,
			loadedImage
		);
	}
	else {
		hr = DirectX::LoadFromWICMemory(
			data,
			dataSize,
			DirectX::WIC_FLAGS_FORCE_SRGB,
			&metadata,
			loadedImage
		);
	}

	if (FAILED(hr)) {
		Logger::Log("Failed to load embedded texture: " + textureKey + "\n");
		return false;
	}

	return RegisterTexture(textureKey, loadedImage, metadata);
}

bool TextureManager::RegisterTexture(
	const std::string& textureKey,
	const DirectX::ScratchImage& loadedImage,
	const DirectX::TexMetadata& metadata
) {
	if (loadedImage.GetImageCount() == 0 || loadedImage.GetImages() == nullptr) {
		Logger::Log("Texture contains no images: " + textureKey + "\n");
		return false;
	}

	assert(srvManager->CanAllocate());

	DirectX::ScratchImage mipImages{};
	const DirectX::ScratchImage* uploadImage = &loadedImage;
	HRESULT hr = S_OK;

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
		if (SUCCEEDED(hr)) {
			uploadImage = &mipImages;
		}
	}

	TextureData& textureData = textureDatas[textureKey];

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
	if (!intermediateResource) {
		textureDatas.erase(textureKey);
		Logger::Log("Failed to upload texture: " + textureKey + "\n");
		return false;
	}

	dxCommon->ExecuteCommandListAndWait();
	return true;
}

bool TextureManager::HasTexture(const std::string& textureKey) const {
	return textureDatas.contains(textureKey);
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
