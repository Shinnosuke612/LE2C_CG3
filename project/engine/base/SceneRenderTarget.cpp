#include "SceneRenderTarget.h"

#include <algorithm>
#include <cassert>

#include "DirectXCommon.h"
#include "../3d/SrvManager.h"

void SceneRenderTarget::Initialize(
	DirectXCommon* dxCommon,
	SrvManager* srvManager,
	uint32_t width,
	uint32_t height
) {
	assert(dxCommon);
	assert(srvManager);

	dxCommon_ = dxCommon;
	srvManager_ = srvManager;
	assert(srvManager_->CanAllocate());
	srvIndex_ = srvManager_->Allocate();

	rtvHeap_ = dxCommon_->CreateDescriptorHeap(
		D3D12_DESCRIPTOR_HEAP_TYPE_RTV,
		1,
		false
	);
	dsvHeap_ = dxCommon_->CreateDescriptorHeap(
		D3D12_DESCRIPTOR_HEAP_TYPE_DSV,
		1,
		false
	);

	width_ = (std::max)(width, 1u);
	height_ = (std::max)(height, 1u);
	CreateResources();
	initialized_ = true;
}

void SceneRenderTarget::Resize(uint32_t width, uint32_t height) {
	width = std::clamp(width, 1u, 4096u);
	height = std::clamp(height, 1u, 4096u);
	if (!initialized_ || (width_ == width && height_ == height)) {
		return;
	}

	width_ = width;
	height_ = height;
	colorResource_.Reset();
	depthResource_.Reset();
	CreateResources();
}

void SceneRenderTarget::Begin() {
	if (!initialized_) {
		return;
	}

	D3D12_RESOURCE_BARRIER barrier{};
	barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barrier.Transition.pResource = colorResource_.Get();
	barrier.Transition.Subresource =
		D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	barrier.Transition.StateBefore =
		D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
	barrier.Transition.StateAfter =
		D3D12_RESOURCE_STATE_RENDER_TARGET;
	dxCommon_->GetCommandList()->ResourceBarrier(1, &barrier);

	const D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle =
		rtvHeap_->GetCPUDescriptorHandleForHeapStart();
	const D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle =
		dsvHeap_->GetCPUDescriptorHandleForHeapStart();
	dxCommon_->GetCommandList()->OMSetRenderTargets(
		1,
		&rtvHandle,
		false,
		&dsvHandle
	);

	const float clearColor[] = { 1.0f, 0.0f, 0.0f, 1.0f };
	dxCommon_->GetCommandList()->ClearRenderTargetView(
		rtvHandle,
		clearColor,
		0,
		nullptr
	);
	dxCommon_->GetCommandList()->ClearDepthStencilView(
		dsvHandle,
		D3D12_CLEAR_FLAG_DEPTH,
		1.0f,
		0,
		0,
		nullptr
	);

	D3D12_VIEWPORT viewport{};
	viewport.Width = static_cast<float>(width_);
	viewport.Height = static_cast<float>(height_);
	viewport.MinDepth = 0.0f;
	viewport.MaxDepth = 1.0f;
	dxCommon_->GetCommandList()->RSSetViewports(1, &viewport);

	D3D12_RECT scissorRect{};
	scissorRect.right = static_cast<LONG>(width_);
	scissorRect.bottom = static_cast<LONG>(height_);
	dxCommon_->GetCommandList()->RSSetScissorRects(1, &scissorRect);
}

void SceneRenderTarget::End() {
	if (!initialized_) {
		return;
	}

	D3D12_RESOURCE_BARRIER barrier{};
	barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barrier.Transition.pResource = colorResource_.Get();
	barrier.Transition.Subresource =
		D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	barrier.Transition.StateBefore =
		D3D12_RESOURCE_STATE_RENDER_TARGET;
	barrier.Transition.StateAfter =
		D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
	dxCommon_->GetCommandList()->ResourceBarrier(1, &barrier);
}

D3D12_GPU_DESCRIPTOR_HANDLE SceneRenderTarget::GetSrvGpuHandle() const {
	if (!initialized_) {
		return {};
	}
	return srvManager_->GetGPUDescriptorHandle(srvIndex_);
}

void SceneRenderTarget::CreateResources() {
	D3D12_HEAP_PROPERTIES heapProperties{};
	heapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;

	D3D12_RESOURCE_DESC colorDesc{};
	colorDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	colorDesc.Width = width_;
	colorDesc.Height = height_;
	colorDesc.DepthOrArraySize = 1;
	colorDesc.MipLevels = 1;
	colorDesc.Format = DXGI_FORMAT_R8G8B8A8_TYPELESS;
	colorDesc.SampleDesc.Count = 1;
	colorDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

	D3D12_CLEAR_VALUE colorClearValue{};
	colorClearValue.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
	colorClearValue.Color[0] = 1.0f;
	colorClearValue.Color[1] = 0.0f;
	colorClearValue.Color[2] = 0.0f;
	colorClearValue.Color[3] = 1.0f;

	HRESULT result = dxCommon_->GetDevice()->CreateCommittedResource(
		&heapProperties,
		D3D12_HEAP_FLAG_NONE,
		&colorDesc,
		D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
		&colorClearValue,
		IID_PPV_ARGS(&colorResource_)
	);
	assert(SUCCEEDED(result));

	D3D12_RENDER_TARGET_VIEW_DESC rtvDesc{};
	rtvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
	rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
	dxCommon_->GetDevice()->CreateRenderTargetView(
		colorResource_.Get(),
		&rtvDesc,
		rtvHeap_->GetCPUDescriptorHandleForHeapStart()
	);

	D3D12_RESOURCE_DESC depthDesc{};
	depthDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	depthDesc.Width = width_;
	depthDesc.Height = height_;
	depthDesc.DepthOrArraySize = 1;
	depthDesc.MipLevels = 1;
	depthDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
	depthDesc.SampleDesc.Count = 1;
	depthDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

	D3D12_CLEAR_VALUE depthClearValue{};
	depthClearValue.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
	depthClearValue.DepthStencil.Depth = 1.0f;

	result = dxCommon_->GetDevice()->CreateCommittedResource(
		&heapProperties,
		D3D12_HEAP_FLAG_NONE,
		&depthDesc,
		D3D12_RESOURCE_STATE_DEPTH_WRITE,
		&depthClearValue,
		IID_PPV_ARGS(&depthResource_)
	);
	assert(SUCCEEDED(result));

	D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc{};
	dsvDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
	dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
	dxCommon_->GetDevice()->CreateDepthStencilView(
		depthResource_.Get(),
		&dsvDesc,
		dsvHeap_->GetCPUDescriptorHandleForHeapStart()
	);

	srvManager_->CreateSRVforTexture2D(
		srvIndex_,
		colorResource_.Get(),
		DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,
		1
	);
}
