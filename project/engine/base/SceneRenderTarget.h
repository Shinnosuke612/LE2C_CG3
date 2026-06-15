#pragma once

#include <cstdint>

#include <d3d12.h>
#include <wrl.h>

class DirectXCommon;
class SrvManager;

class SceneRenderTarget {
public:
	void Initialize(
		DirectXCommon* dxCommon,
		SrvManager* srvManager,
		uint32_t width,
		uint32_t height
	);
	void Resize(uint32_t width, uint32_t height);
	void Begin();
	void End();

	uint32_t GetWidth() const { return width_; }
	uint32_t GetHeight() const { return height_; }
	D3D12_GPU_DESCRIPTOR_HANDLE GetSrvGpuHandle() const;
	D3D12_GPU_DESCRIPTOR_HANDLE GetDepthSrvGpuHandle() const;

private:
	void CreateResources();

	DirectXCommon* dxCommon_ = nullptr;
	SrvManager* srvManager_ = nullptr;

	Microsoft::WRL::ComPtr<ID3D12Resource> colorResource_;
	Microsoft::WRL::ComPtr<ID3D12Resource> depthResource_;
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> rtvHeap_;
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> dsvHeap_;

	uint32_t srvIndex_ = 0;
	uint32_t depthSrvIndex_ = 0;
	uint32_t width_ = 1;
	uint32_t height_ = 1;
	bool initialized_ = false;
	bool depthReadable_ = false;
};
