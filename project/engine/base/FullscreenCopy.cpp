#include "FullscreenCopy.h"

#include <cassert>

#include "DirectXCommon.h"
#include "../utility/Logger.h"

void FullscreenCopy::Initialize(DirectXCommon* dxCommon) {
	assert(dxCommon);
	dxCommon_ = dxCommon;
	CreateRootSignature();
	CreatePipelineState();

	for (uint32_t index = 0; index < kMaxDrawsPerFrame; ++index) {
		parameterResources_[index] =
			dxCommon_->CreateBufferResource(sizeof(Parameters));
		parameterResources_[index]->Map(
			0,
			nullptr,
			reinterpret_cast<void**>(&parameterData_[index])
		);
		*parameterData_[index] = {};
	}
}

void FullscreenCopy::BeginFrame() {
	drawIndex_ = 0;
	pendingParameters_ = {};
}

void FullscreenCopy::SetParameters(
	const Parameters& parameters
) {
	pendingParameters_ = parameters;
}

void FullscreenCopy::Draw(
	D3D12_GPU_DESCRIPTOR_HANDLE textureHandle,
	Effect effect
) {
	assert(dxCommon_);
	assert(drawIndex_ < kMaxDrawsPerFrame);

	const uint32_t parameterIndex = drawIndex_++;
	*parameterData_[parameterIndex] = pendingParameters_;

	ID3D12GraphicsCommandList* commandList = dxCommon_->GetCommandList();
	commandList->SetGraphicsRootSignature(rootSignature_.Get());
	ID3D12PipelineState* pipelineState = copyPipelineState_.Get();
	if (effect == Effect::kGrayscale) {
		pipelineState = grayscalePipelineState_.Get();
	}
	else if (effect == Effect::kVignette) {
		pipelineState = vignettePipelineState_.Get();
	}
	else if (effect == Effect::kBoxBlur) {
		pipelineState = boxBlurPipelineState_.Get();
	}
	else if (effect == Effect::kGaussianBlur) {
		pipelineState = gaussianBlurPipelineState_.Get();
	}
	commandList->SetPipelineState(pipelineState);
	commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	commandList->SetGraphicsRootDescriptorTable(0, textureHandle);
	commandList->SetGraphicsRootConstantBufferView(
		1,
		parameterResources_[parameterIndex]->GetGPUVirtualAddress()
	);
	commandList->DrawInstanced(3, 1, 0, 0);
}

void FullscreenCopy::CreateRootSignature() {
	D3D12_DESCRIPTOR_RANGE descriptorRange{};
	descriptorRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	descriptorRange.NumDescriptors = 1;
	descriptorRange.BaseShaderRegister = 0;
	descriptorRange.OffsetInDescriptorsFromTableStart =
		D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

	D3D12_ROOT_PARAMETER rootParameters[2]{};
	rootParameters[0].ParameterType =
		D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	rootParameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	rootParameters[0].DescriptorTable.pDescriptorRanges = &descriptorRange;
	rootParameters[0].DescriptorTable.NumDescriptorRanges = 1;

	rootParameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
	rootParameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	rootParameters[1].Descriptor.ShaderRegister = 0;

	D3D12_STATIC_SAMPLER_DESC sampler{};
	sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
	sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
	sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
	sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
	sampler.ShaderRegister = 0;
	sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	sampler.MaxLOD = D3D12_FLOAT32_MAX;

	D3D12_ROOT_SIGNATURE_DESC rootSignatureDesc{};
	rootSignatureDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;
	rootSignatureDesc.pParameters = rootParameters;
	rootSignatureDesc.NumParameters = _countof(rootParameters);
	rootSignatureDesc.pStaticSamplers = &sampler;
	rootSignatureDesc.NumStaticSamplers = 1;

	Microsoft::WRL::ComPtr<ID3DBlob> signatureBlob;
	Microsoft::WRL::ComPtr<ID3DBlob> errorBlob;
	HRESULT result = D3D12SerializeRootSignature(
		&rootSignatureDesc,
		D3D_ROOT_SIGNATURE_VERSION_1,
		&signatureBlob,
		&errorBlob
	);
	if (FAILED(result)) {
		if (errorBlob) {
			Logger::Log(
				static_cast<const char*>(errorBlob->GetBufferPointer())
			);
		}
		assert(false);
	}

	result = dxCommon_->GetDevice()->CreateRootSignature(
		0,
		signatureBlob->GetBufferPointer(),
		signatureBlob->GetBufferSize(),
		IID_PPV_ARGS(&rootSignature_)
	);
	assert(SUCCEEDED(result));
}

void FullscreenCopy::CreatePipelineState() {
	const auto vertexShader = dxCommon_->CompileShader(
		L"resources/shaders/Fullscreen.VS.hlsl",
		L"vs_6_0"
	);
	const auto copyPixelShader = dxCommon_->CompileShader(
		L"resources/shaders/CopyImage.PS.hlsl",
		L"ps_6_0"
	);
	const auto grayscalePixelShader = dxCommon_->CompileShader(
		L"resources/shaders/Grayscale.PS.hlsl",
		L"ps_6_0"
	);
	const auto vignettePixelShader = dxCommon_->CompileShader(
		L"resources/shaders/Vignette.PS.hlsl",
		L"ps_6_0"
	);
	const auto boxBlurPixelShader = dxCommon_->CompileShader(
		L"resources/shaders/BoxBlur.PS.hlsl",
		L"ps_6_0"
	);
	const auto gaussianBlurPixelShader = dxCommon_->CompileShader(
		L"resources/shaders/GaussianBlur.PS.hlsl",
		L"ps_6_0"
	);
	assert(vertexShader);
	assert(copyPixelShader);
	assert(grayscalePixelShader);
	assert(vignettePixelShader);
	assert(boxBlurPixelShader);
	assert(gaussianBlurPixelShader);

	D3D12_RASTERIZER_DESC rasterizerDesc{};
	rasterizerDesc.FillMode = D3D12_FILL_MODE_SOLID;
	rasterizerDesc.CullMode = D3D12_CULL_MODE_NONE;
	rasterizerDesc.DepthClipEnable = TRUE;

	D3D12_BLEND_DESC blendDesc{};
	blendDesc.RenderTarget[0].RenderTargetWriteMask =
		D3D12_COLOR_WRITE_ENABLE_ALL;

	D3D12_DEPTH_STENCIL_DESC depthStencilDesc{};
	depthStencilDesc.DepthEnable = FALSE;
	depthStencilDesc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
	depthStencilDesc.StencilEnable = FALSE;

	D3D12_GRAPHICS_PIPELINE_STATE_DESC pipelineDesc{};
	pipelineDesc.pRootSignature = rootSignature_.Get();
	pipelineDesc.InputLayout = { nullptr, 0 };
	pipelineDesc.VS = {
		vertexShader->GetBufferPointer(),
		vertexShader->GetBufferSize()
	};
	pipelineDesc.BlendState = blendDesc;
	pipelineDesc.RasterizerState = rasterizerDesc;
	pipelineDesc.DepthStencilState = depthStencilDesc;
	pipelineDesc.SampleMask = D3D12_DEFAULT_SAMPLE_MASK;
	pipelineDesc.PrimitiveTopologyType =
		D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	pipelineDesc.NumRenderTargets = 1;
	pipelineDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
	pipelineDesc.SampleDesc.Count = 1;

	pipelineDesc.PS = {
		copyPixelShader->GetBufferPointer(),
		copyPixelShader->GetBufferSize()
	};
	HRESULT result = dxCommon_->GetDevice()->CreateGraphicsPipelineState(
		&pipelineDesc,
		IID_PPV_ARGS(&copyPipelineState_)
	);
	assert(SUCCEEDED(result));

	pipelineDesc.PS = {
		grayscalePixelShader->GetBufferPointer(),
		grayscalePixelShader->GetBufferSize()
	};
	result = dxCommon_->GetDevice()->CreateGraphicsPipelineState(
		&pipelineDesc,
		IID_PPV_ARGS(&grayscalePipelineState_)
	);
	assert(SUCCEEDED(result));

	pipelineDesc.PS = {
		vignettePixelShader->GetBufferPointer(),
		vignettePixelShader->GetBufferSize()
	};
	result = dxCommon_->GetDevice()->CreateGraphicsPipelineState(
		&pipelineDesc,
		IID_PPV_ARGS(&vignettePipelineState_)
	);
	assert(SUCCEEDED(result));

	pipelineDesc.PS = {
		boxBlurPixelShader->GetBufferPointer(),
		boxBlurPixelShader->GetBufferSize()
	};
	result = dxCommon_->GetDevice()->CreateGraphicsPipelineState(
		&pipelineDesc,
		IID_PPV_ARGS(&boxBlurPipelineState_)
	);
	assert(SUCCEEDED(result));

	pipelineDesc.PS = {
		gaussianBlurPixelShader->GetBufferPointer(),
		gaussianBlurPixelShader->GetBufferSize()
	};
	result = dxCommon_->GetDevice()->CreateGraphicsPipelineState(
		&pipelineDesc,
		IID_PPV_ARGS(&gaussianBlurPipelineState_)
	);
	assert(SUCCEEDED(result));
}
