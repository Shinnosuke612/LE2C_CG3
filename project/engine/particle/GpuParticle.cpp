#include "GpuParticle.h"

#include "ParticleCommon.h"
#include "../2d/TextureManager.h"
#include "../3d/Camera.h"
#include "../3d/SrvManager.h"
#include "../base/DirectXCommon.h"
#include "../utility/Logger.h"

#include <cassert>
#include <cstring>

namespace {

Microsoft::WRL::ComPtr<ID3D12Resource> CreateUavBufferResource(
	ID3D12Device* device,
	size_t sizeInBytes
) {
	assert(device);

	D3D12_HEAP_PROPERTIES heapProperties{};
	heapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;

	D3D12_RESOURCE_DESC resourceDesc{};
	resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	resourceDesc.Width = sizeInBytes;
	resourceDesc.Height = 1;
	resourceDesc.DepthOrArraySize = 1;
	resourceDesc.MipLevels = 1;
	resourceDesc.SampleDesc.Count = 1;
	resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
	resourceDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

	Microsoft::WRL::ComPtr<ID3D12Resource> resource;
	const HRESULT hr = device->CreateCommittedResource(
		&heapProperties,
		D3D12_HEAP_FLAG_NONE,
		&resourceDesc,
		D3D12_RESOURCE_STATE_COMMON,
		nullptr,
		IID_PPV_ARGS(&resource)
	);
	assert(SUCCEEDED(hr));
	return resource;
}

D3D12_STATIC_SAMPLER_DESC MakeLinearSampler() {
	D3D12_STATIC_SAMPLER_DESC sampler{};
	sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
	sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
	sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
	sampler.ShaderRegister = 0;
	sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	return sampler;
}

} // namespace

void GpuParticle::Initialize(
	ParticleCommon* particleCommon,
	SrvManager* srvManager,
	const std::string& textureFilePath
) {
	assert(particleCommon);
	assert(srvManager);

	particleCommon_ = particleCommon;
	srvManager_ = srvManager;
	dxCommon_ = particleCommon_->GetDxCommon();
	textureFilePath_ = textureFilePath;

	const bool loaded = TextureManager::GetInstance()->LoadTexture(textureFilePath_);
	assert(loaded);
	textureSrvIndex_ = TextureManager::GetInstance()->GetSrvIndex(textureFilePath_);

	CreateParticleResource();
	CreateConstantBuffers();
	CreateRootSignatures();
	CreatePipelineStates();

	needsInitialize_ = true;
}

void GpuParticle::Reset() {
	particleResource_.Reset();
	counterResource_.Reset();
	materialResource_.Reset();
	directionalLightResource_.Reset();
	perViewResource_.Reset();
	emitterResource_.Reset();
	perFrameResource_.Reset();
	graphicsRootSignature_.Reset();
	graphicsPipelineState_.Reset();
	initializeRootSignature_.Reset();
	initializePipelineState_.Reset();
	emitRootSignature_.Reset();
	emitPipelineState_.Reset();
	updatePipelineState_.Reset();

	materialData_ = nullptr;
	directionalLightData_ = nullptr;
	perViewData_ = nullptr;
	emitterData_ = nullptr;
	perFrameData_ = nullptr;
	particleCommon_ = nullptr;
	srvManager_ = nullptr;
	dxCommon_ = nullptr;
	textureFilePath_.clear();
	textureSrvIndex_ = 0;
	particleSrvIndex_ = 0;
	particleUavIndex_ = 0;
	counterUavIndex_ = 0;
	particleResourceState_ = D3D12_RESOURCE_STATE_COMMON;
	counterResourceState_ = D3D12_RESOURCE_STATE_COMMON;
	needsInitialize_ = true;
	elapsedTime_ = 0.0f;
}

void GpuParticle::CreateParticleResource() {
	assert(dxCommon_);
	assert(srvManager_);
	assert(srvManager_->CanAllocate());

	particleResource_ = CreateUavBufferResource(
		dxCommon_->GetDevice(),
		sizeof(ParticleData) * kMaxParticles
	);
	particleResourceState_ = D3D12_RESOURCE_STATE_COMMON;

	particleSrvIndex_ = srvManager_->Allocate();
	srvManager_->CreateSRVforStructuredBuffer(
		particleSrvIndex_,
		particleResource_.Get(),
		kMaxParticles,
		sizeof(ParticleData)
	);

	assert(srvManager_->CanAllocate());
	particleUavIndex_ = srvManager_->Allocate();
	srvManager_->CreateUAVforStructuredBuffer(
		particleUavIndex_,
		particleResource_.Get(),
		kMaxParticles,
		sizeof(ParticleData)
	);

	assert(srvManager_->CanAllocate());
	counterResource_ =
		CreateUavBufferResource(dxCommon_->GetDevice(), sizeof(int32_t));
	counterResourceState_ = D3D12_RESOURCE_STATE_COMMON;

	counterUavIndex_ = srvManager_->Allocate();
	srvManager_->CreateUAVforStructuredBuffer(
		counterUavIndex_,
		counterResource_.Get(),
		1,
		sizeof(int32_t)
	);
}

void GpuParticle::CreateConstantBuffers() {
	materialResource_ = dxCommon_->CreateBufferResource(sizeof(Material));
	materialResource_->Map(0, nullptr, reinterpret_cast<void**>(&materialData_));
	materialData_->color = { 1.0f, 1.0f, 1.0f, 1.0f };
	materialData_->enableLighting = false;
	materialData_->alphaCutoff = 0.0f;
	materialData_->flipU = false;
	materialData_->flipV = false;
	materialData_->uvTransform = MakeIdentity4x4();

	directionalLightResource_ =
		dxCommon_->CreateBufferResource(sizeof(DirectionalLight));
	directionalLightResource_->Map(
		0,
		nullptr,
		reinterpret_cast<void**>(&directionalLightData_)
	);
	directionalLightData_->color = { 1.0f, 1.0f, 1.0f, 1.0f };
	directionalLightData_->direction = { 0.0f, -1.0f, 0.0f };
	directionalLightData_->intensity = 1.0f;

	perViewResource_ = dxCommon_->CreateBufferResource(sizeof(PerView));
	perViewResource_->Map(0, nullptr, reinterpret_cast<void**>(&perViewData_));
	std::memset(perViewData_, 0, sizeof(PerView));

	emitterResource_ = dxCommon_->CreateBufferResource(sizeof(EmitterSphere));
	emitterResource_->Map(0, nullptr, reinterpret_cast<void**>(&emitterData_));
	emitterData_->translate = { 0.0f, 1.7f, 0.0f };
	emitterData_->radius = 0.35f;
	emitterData_->count = 10;
	emitterData_->frequency = 0.5f;
	emitterData_->frequencyTime = 0.0f;
	emitterData_->emit = 0;

	perFrameResource_ = dxCommon_->CreateBufferResource(sizeof(PerFrame));
	perFrameResource_->Map(0, nullptr, reinterpret_cast<void**>(&perFrameData_));
	perFrameData_->time = 0.0f;
	perFrameData_->deltaTime = deltaTime_;
	perFrameData_->padding[0] = 0.0f;
	perFrameData_->padding[1] = 0.0f;
}

void GpuParticle::CreateRootSignatures() {
	HRESULT hr = S_OK;

	{
		D3D12_DESCRIPTOR_RANGE ranges[2]{};
		ranges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
		ranges[0].NumDescriptors = 1;
		ranges[0].BaseShaderRegister = 0;
		ranges[0].OffsetInDescriptorsFromTableStart =
			D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

		ranges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
		ranges[1].NumDescriptors = 1;
		ranges[1].BaseShaderRegister = 0;
		ranges[1].OffsetInDescriptorsFromTableStart =
			D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

		D3D12_ROOT_PARAMETER rootParameters[5]{};
		rootParameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
		rootParameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
		rootParameters[0].Descriptor.ShaderRegister = 0;

		rootParameters[1].ParameterType =
			D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
		rootParameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
		rootParameters[1].DescriptorTable.pDescriptorRanges = &ranges[0];
		rootParameters[1].DescriptorTable.NumDescriptorRanges = 1;

		rootParameters[2].ParameterType =
			D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
		rootParameters[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
		rootParameters[2].DescriptorTable.pDescriptorRanges = &ranges[1];
		rootParameters[2].DescriptorTable.NumDescriptorRanges = 1;

		rootParameters[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
		rootParameters[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
		rootParameters[3].Descriptor.ShaderRegister = 1;

		rootParameters[4].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
		rootParameters[4].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
		rootParameters[4].Descriptor.ShaderRegister = 0;

		D3D12_ROOT_SIGNATURE_DESC rootSignatureDesc{};
		rootSignatureDesc.Flags =
			D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
		rootSignatureDesc.pParameters = rootParameters;
		rootSignatureDesc.NumParameters = _countof(rootParameters);

		const D3D12_STATIC_SAMPLER_DESC sampler = MakeLinearSampler();
		rootSignatureDesc.pStaticSamplers = &sampler;
		rootSignatureDesc.NumStaticSamplers = 1;

		ID3DBlob* signatureBlob = nullptr;
		ID3DBlob* errorBlob = nullptr;
		hr = D3D12SerializeRootSignature(
			&rootSignatureDesc,
			D3D_ROOT_SIGNATURE_VERSION_1,
			&signatureBlob,
			&errorBlob
		);
		if (FAILED(hr)) {
			if (errorBlob) {
				Logger::Log(reinterpret_cast<char*>(errorBlob->GetBufferPointer()));
			}
			assert(false);
		}

		hr = dxCommon_->GetDevice()->CreateRootSignature(
			0,
			signatureBlob->GetBufferPointer(),
			signatureBlob->GetBufferSize(),
			IID_PPV_ARGS(&graphicsRootSignature_)
		);
		assert(SUCCEEDED(hr));
	}

	{
		D3D12_DESCRIPTOR_RANGE uavRanges[2]{};
		for (uint32_t index = 0; index < _countof(uavRanges); ++index) {
			uavRanges[index].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
			uavRanges[index].NumDescriptors = 1;
			uavRanges[index].BaseShaderRegister = index;
			uavRanges[index].OffsetInDescriptorsFromTableStart =
				D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
		}

		D3D12_ROOT_PARAMETER rootParameters[2]{};
		for (uint32_t index = 0; index < _countof(rootParameters); ++index) {
			rootParameters[index].ParameterType =
				D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
			rootParameters[index].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
			rootParameters[index].DescriptorTable.pDescriptorRanges =
				&uavRanges[index];
			rootParameters[index].DescriptorTable.NumDescriptorRanges = 1;
		}

		D3D12_ROOT_SIGNATURE_DESC rootSignatureDesc{};
		rootSignatureDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;
		rootSignatureDesc.pParameters = rootParameters;
		rootSignatureDesc.NumParameters = _countof(rootParameters);

		ID3DBlob* signatureBlob = nullptr;
		ID3DBlob* errorBlob = nullptr;
		hr = D3D12SerializeRootSignature(
			&rootSignatureDesc,
			D3D_ROOT_SIGNATURE_VERSION_1,
			&signatureBlob,
			&errorBlob
		);
		if (FAILED(hr)) {
			if (errorBlob) {
				Logger::Log(reinterpret_cast<char*>(errorBlob->GetBufferPointer()));
			}
			assert(false);
		}

		hr = dxCommon_->GetDevice()->CreateRootSignature(
			0,
			signatureBlob->GetBufferPointer(),
			signatureBlob->GetBufferSize(),
			IID_PPV_ARGS(&initializeRootSignature_)
		);
		assert(SUCCEEDED(hr));
	}

	{
		D3D12_DESCRIPTOR_RANGE uavRanges[2]{};
		for (uint32_t index = 0; index < _countof(uavRanges); ++index) {
			uavRanges[index].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
			uavRanges[index].NumDescriptors = 1;
			uavRanges[index].BaseShaderRegister = index;
			uavRanges[index].OffsetInDescriptorsFromTableStart =
				D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
		}

		D3D12_ROOT_PARAMETER rootParameters[4]{};
		rootParameters[0].ParameterType =
			D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
		rootParameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
		rootParameters[0].DescriptorTable.pDescriptorRanges = &uavRanges[0];
		rootParameters[0].DescriptorTable.NumDescriptorRanges = 1;

		rootParameters[1].ParameterType =
			D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
		rootParameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
		rootParameters[1].DescriptorTable.pDescriptorRanges = &uavRanges[1];
		rootParameters[1].DescriptorTable.NumDescriptorRanges = 1;

		rootParameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
		rootParameters[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
		rootParameters[2].Descriptor.ShaderRegister = 0;

		rootParameters[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
		rootParameters[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
		rootParameters[3].Descriptor.ShaderRegister = 1;

		D3D12_ROOT_SIGNATURE_DESC rootSignatureDesc{};
		rootSignatureDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;
		rootSignatureDesc.pParameters = rootParameters;
		rootSignatureDesc.NumParameters = _countof(rootParameters);

		ID3DBlob* signatureBlob = nullptr;
		ID3DBlob* errorBlob = nullptr;
		hr = D3D12SerializeRootSignature(
			&rootSignatureDesc,
			D3D_ROOT_SIGNATURE_VERSION_1,
			&signatureBlob,
			&errorBlob
		);
		if (FAILED(hr)) {
			if (errorBlob) {
				Logger::Log(reinterpret_cast<char*>(errorBlob->GetBufferPointer()));
			}
			assert(false);
		}

		hr = dxCommon_->GetDevice()->CreateRootSignature(
			0,
			signatureBlob->GetBufferPointer(),
			signatureBlob->GetBufferSize(),
			IID_PPV_ARGS(&emitRootSignature_)
		);
		assert(SUCCEEDED(hr));
	}
}

void GpuParticle::CreatePipelineStates() {
	HRESULT hr = S_OK;

	const auto vertexShaderBlob = dxCommon_->CompileShader(
		L"resources/shaders/GpuParticle.VS.hlsl",
		L"vs_6_0"
	);
	assert(vertexShaderBlob);
	const auto pixelShaderBlob = dxCommon_->CompileShader(
		L"resources/shaders/Particle.PS.hlsl",
		L"ps_6_0"
	);
	assert(pixelShaderBlob);

	D3D12_INPUT_ELEMENT_DESC inputElementDescs[4]{};
	inputElementDescs[0].SemanticName = "POSITION";
	inputElementDescs[0].Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
	inputElementDescs[0].AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;
	inputElementDescs[1].SemanticName = "TEXCOORD";
	inputElementDescs[1].Format = DXGI_FORMAT_R32G32_FLOAT;
	inputElementDescs[1].AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;
	inputElementDescs[2].SemanticName = "NORMAL";
	inputElementDescs[2].Format = DXGI_FORMAT_R32G32B32_FLOAT;
	inputElementDescs[2].AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;
	inputElementDescs[3].SemanticName = "COLOR";
	inputElementDescs[3].Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
	inputElementDescs[3].AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;

	D3D12_INPUT_LAYOUT_DESC inputLayoutDesc{};
	inputLayoutDesc.pInputElementDescs = inputElementDescs;
	inputLayoutDesc.NumElements = _countof(inputElementDescs);

	D3D12_BLEND_DESC blendDesc{};
	blendDesc.RenderTarget[0].BlendEnable = TRUE;
	blendDesc.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
	blendDesc.RenderTarget[0].DestBlend = D3D12_BLEND_ONE;
	blendDesc.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
	blendDesc.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ZERO;
	blendDesc.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ONE;
	blendDesc.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
	blendDesc.RenderTarget[0].RenderTargetWriteMask =
		D3D12_COLOR_WRITE_ENABLE_ALL;

	D3D12_RASTERIZER_DESC rasterizerDesc{};
	rasterizerDesc.CullMode = D3D12_CULL_MODE_NONE;
	rasterizerDesc.FillMode = D3D12_FILL_MODE_SOLID;
	rasterizerDesc.DepthClipEnable = TRUE;

	D3D12_DEPTH_STENCIL_DESC depthStencilDesc{};
	depthStencilDesc.DepthEnable = TRUE;
	depthStencilDesc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
	depthStencilDesc.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;

	D3D12_GRAPHICS_PIPELINE_STATE_DESC graphicsPipelineDesc{};
	graphicsPipelineDesc.pRootSignature = graphicsRootSignature_.Get();
	graphicsPipelineDesc.InputLayout = inputLayoutDesc;
	graphicsPipelineDesc.VS = {
		vertexShaderBlob->GetBufferPointer(),
		vertexShaderBlob->GetBufferSize()
	};
	graphicsPipelineDesc.PS = {
		pixelShaderBlob->GetBufferPointer(),
		pixelShaderBlob->GetBufferSize()
	};
	graphicsPipelineDesc.BlendState = blendDesc;
	graphicsPipelineDesc.RasterizerState = rasterizerDesc;
	graphicsPipelineDesc.DepthStencilState = depthStencilDesc;
	graphicsPipelineDesc.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
	graphicsPipelineDesc.NumRenderTargets = 1;
	graphicsPipelineDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
	graphicsPipelineDesc.PrimitiveTopologyType =
		D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	graphicsPipelineDesc.SampleDesc.Count = 1;
	graphicsPipelineDesc.SampleMask = D3D12_DEFAULT_SAMPLE_MASK;

	hr = dxCommon_->GetDevice()->CreateGraphicsPipelineState(
		&graphicsPipelineDesc,
		IID_PPV_ARGS(&graphicsPipelineState_)
	);
	assert(SUCCEEDED(hr));

	const auto initializeShaderBlob = dxCommon_->CompileShader(
		L"resources/shaders/InitializeGpuParticle.CS.hlsl",
		L"cs_6_0"
	);
	assert(initializeShaderBlob);

	D3D12_COMPUTE_PIPELINE_STATE_DESC computePipelineDesc{};
	computePipelineDesc.pRootSignature = initializeRootSignature_.Get();
	computePipelineDesc.CS = {
		initializeShaderBlob->GetBufferPointer(),
		initializeShaderBlob->GetBufferSize()
	};

	hr = dxCommon_->GetDevice()->CreateComputePipelineState(
		&computePipelineDesc,
		IID_PPV_ARGS(&initializePipelineState_)
	);
	assert(SUCCEEDED(hr));

	const auto emitShaderBlob = dxCommon_->CompileShader(
		L"resources/shaders/EmitGpuParticle.CS.hlsl",
		L"cs_6_0"
	);
	assert(emitShaderBlob);

	D3D12_COMPUTE_PIPELINE_STATE_DESC emitPipelineDesc{};
	emitPipelineDesc.pRootSignature = emitRootSignature_.Get();
	emitPipelineDesc.CS = {
		emitShaderBlob->GetBufferPointer(),
		emitShaderBlob->GetBufferSize()
	};

	hr = dxCommon_->GetDevice()->CreateComputePipelineState(
		&emitPipelineDesc,
		IID_PPV_ARGS(&emitPipelineState_)
	);
	assert(SUCCEEDED(hr));

	const auto updateShaderBlob = dxCommon_->CompileShader(
		L"resources/shaders/UpdateGpuParticle.CS.hlsl",
		L"cs_6_0"
	);
	assert(updateShaderBlob);

	D3D12_COMPUTE_PIPELINE_STATE_DESC updatePipelineDesc{};
	updatePipelineDesc.pRootSignature = emitRootSignature_.Get();
	updatePipelineDesc.CS = {
		updateShaderBlob->GetBufferPointer(),
		updateShaderBlob->GetBufferSize()
	};

	hr = dxCommon_->GetDevice()->CreateComputePipelineState(
		&updatePipelineDesc,
		IID_PPV_ARGS(&updatePipelineState_)
	);
	assert(SUCCEEDED(hr));
}

void GpuParticle::TransitionParticleResource(D3D12_RESOURCE_STATES stateAfter) {
	if (!particleResource_ || particleResourceState_ == stateAfter) {
		return;
	}

	D3D12_RESOURCE_BARRIER barrier{};
	barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
	barrier.Transition.pResource = particleResource_.Get();
	barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	barrier.Transition.StateBefore = particleResourceState_;
	barrier.Transition.StateAfter = stateAfter;
	dxCommon_->GetCommandList()->ResourceBarrier(1, &barrier);
	particleResourceState_ = stateAfter;
}

void GpuParticle::TransitionCounterResource(D3D12_RESOURCE_STATES stateAfter) {
	if (!counterResource_ || counterResourceState_ == stateAfter) {
		return;
	}

	D3D12_RESOURCE_BARRIER barrier{};
	barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
	barrier.Transition.pResource = counterResource_.Get();
	barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	barrier.Transition.StateBefore = counterResourceState_;
	barrier.Transition.StateAfter = stateAfter;
	dxCommon_->GetCommandList()->ResourceBarrier(1, &barrier);
	counterResourceState_ = stateAfter;
}

void GpuParticle::InitializeParticlesOnGPU() {
	if (!needsInitialize_) {
		return;
	}

	auto* commandList = dxCommon_->GetCommandList();
	TransitionParticleResource(D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	TransitionCounterResource(D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

	commandList->SetComputeRootSignature(initializeRootSignature_.Get());
	commandList->SetPipelineState(initializePipelineState_.Get());
	srvManager_->SetComputeRootDescriptorTable(0, particleUavIndex_);
	srvManager_->SetComputeRootDescriptorTable(1, counterUavIndex_);
	commandList->Dispatch(1, 1, 1);

	D3D12_RESOURCE_BARRIER uavBarriers[2]{};
	uavBarriers[0].Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
	uavBarriers[0].UAV.pResource = particleResource_.Get();
	uavBarriers[1].Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
	uavBarriers[1].UAV.pResource = counterResource_.Get();
	commandList->ResourceBarrier(_countof(uavBarriers), uavBarriers);

	TransitionParticleResource(D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	needsInitialize_ = false;
}

void GpuParticle::EmitParticlesOnGPU() {
	if (!emitterData_ || emitterData_->emit == 0) {
		return;
	}

	auto* commandList = dxCommon_->GetCommandList();
	TransitionParticleResource(D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	TransitionCounterResource(D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

	commandList->SetComputeRootSignature(emitRootSignature_.Get());
	commandList->SetPipelineState(emitPipelineState_.Get());
	srvManager_->SetComputeRootDescriptorTable(0, particleUavIndex_);
	srvManager_->SetComputeRootDescriptorTable(1, counterUavIndex_);
	commandList->SetComputeRootConstantBufferView(
		2,
		emitterResource_->GetGPUVirtualAddress()
	);
	commandList->SetComputeRootConstantBufferView(
		3,
		perFrameResource_->GetGPUVirtualAddress()
	);
	commandList->Dispatch(1, 1, 1);

	D3D12_RESOURCE_BARRIER uavBarriers[2]{};
	uavBarriers[0].Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
	uavBarriers[0].UAV.pResource = particleResource_.Get();
	uavBarriers[1].Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
	uavBarriers[1].UAV.pResource = counterResource_.Get();
	commandList->ResourceBarrier(_countof(uavBarriers), uavBarriers);
}

void GpuParticle::UpdateParticlesOnGPU() {
	auto* commandList = dxCommon_->GetCommandList();
	TransitionParticleResource(D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

	commandList->SetComputeRootSignature(emitRootSignature_.Get());
	commandList->SetPipelineState(updatePipelineState_.Get());
	srvManager_->SetComputeRootDescriptorTable(0, particleUavIndex_);
	srvManager_->SetComputeRootDescriptorTable(1, counterUavIndex_);
	commandList->SetComputeRootConstantBufferView(
		2,
		emitterResource_->GetGPUVirtualAddress()
	);
	commandList->SetComputeRootConstantBufferView(
		3,
		perFrameResource_->GetGPUVirtualAddress()
	);
	commandList->Dispatch(1, 1, 1);

	D3D12_RESOURCE_BARRIER uavBarrier{};
	uavBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
	uavBarrier.UAV.pResource = particleResource_.Get();
	commandList->ResourceBarrier(1, &uavBarrier);

	TransitionParticleResource(D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
}

void GpuParticle::Update() {
	if (!emitterData_ || !perFrameData_) {
		return;
	}

	elapsedTime_ += deltaTime_;
	perFrameData_->time = elapsedTime_;
	perFrameData_->deltaTime = deltaTime_;

	emitterData_->frequencyTime += deltaTime_;
	if (emitterData_->frequency <= emitterData_->frequencyTime) {
		emitterData_->frequencyTime -= emitterData_->frequency;
		emitterData_->emit = 1;
	}
	else {
		emitterData_->emit = 0;
	}
}

void GpuParticle::Draw(Camera* camera) {
	if (!camera || !particleResource_) {
		return;
	}

	InitializeParticlesOnGPU();
	EmitParticlesOnGPU();
	UpdateParticlesOnGPU();
	TransitionParticleResource(D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

	perViewData_->viewProjection = camera->GetViewProjectionMatrix();
	perViewData_->billboardMatrix = camera->GetWorldMatrix();
	perViewData_->billboardMatrix.m[3][0] = 0.0f;
	perViewData_->billboardMatrix.m[3][1] = 0.0f;
	perViewData_->billboardMatrix.m[3][2] = 0.0f;

	auto* commandList = dxCommon_->GetCommandList();
	commandList->SetGraphicsRootSignature(graphicsRootSignature_.Get());
	commandList->SetPipelineState(graphicsPipelineState_.Get());
	commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	const D3D12_VERTEX_BUFFER_VIEW& vbv =
		particleCommon_->GetVertexBufferView();
	commandList->IASetVertexBuffers(0, 1, &vbv);

	commandList->SetGraphicsRootConstantBufferView(
		0,
		materialResource_->GetGPUVirtualAddress()
	);
	srvManager_->SetGraphicsRootDescriptorTable(1, particleSrvIndex_);
	srvManager_->SetGraphicsRootDescriptorTable(2, textureSrvIndex_);
	commandList->SetGraphicsRootConstantBufferView(
		3,
		directionalLightResource_->GetGPUVirtualAddress()
	);
	commandList->SetGraphicsRootConstantBufferView(
		4,
		perViewResource_->GetGPUVirtualAddress()
	);

	commandList->DrawInstanced(6, kMaxParticles, 0, 0);
}
