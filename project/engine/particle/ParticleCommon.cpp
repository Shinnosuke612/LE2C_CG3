#include "ParticleCommon.h"
#include "../base/DirectXCommon.h"
#include "../utility/Logger.h"
#include <cassert>
#include <array>
#include <cstring>

ParticleCommon* ParticleCommon::instance_ = nullptr;

ParticleCommon* ParticleCommon::GetInstance() {
	if (instance_ == nullptr) {
		instance_ = new ParticleCommon();
	}
	return instance_;
}

void ParticleCommon::DeleteInstance() {
	delete instance_;
	instance_ = nullptr;
}

namespace {
	void ApplyBlendMode(D3D12_BLEND_DESC& blendDesc, ParticleCommon::BlendMode blendMode) {
		blendDesc = {};
		blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

		if (blendMode == ParticleCommon::BlendMode::kBlendModeNone) {
			blendDesc.RenderTarget[0].BlendEnable = FALSE;
			return;
		}

		blendDesc.RenderTarget[0].BlendEnable = TRUE;

		if (blendMode == ParticleCommon::BlendMode::kBlendModeNormal) {
			blendDesc.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
			blendDesc.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
			blendDesc.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
		}
		else if (blendMode == ParticleCommon::BlendMode::kBlendModeAdd) {
			blendDesc.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
			blendDesc.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
			blendDesc.RenderTarget[0].DestBlend = D3D12_BLEND_ONE;
		}
		else if (blendMode == ParticleCommon::BlendMode::kBlendModeSubtract) {
			blendDesc.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
			blendDesc.RenderTarget[0].BlendOp = D3D12_BLEND_OP_REV_SUBTRACT;
			blendDesc.RenderTarget[0].DestBlend = D3D12_BLEND_ONE;
		}
		else if (blendMode == ParticleCommon::BlendMode::kBlendModeMultiply) {
			blendDesc.RenderTarget[0].SrcBlend = D3D12_BLEND_ZERO;
			blendDesc.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
			blendDesc.RenderTarget[0].DestBlend = D3D12_BLEND_SRC_COLOR;
		}
		else if (blendMode == ParticleCommon::BlendMode::kBlendModeScreen) {
			blendDesc.RenderTarget[0].SrcBlend = D3D12_BLEND_INV_DEST_COLOR;
			blendDesc.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
			blendDesc.RenderTarget[0].DestBlend = D3D12_BLEND_ONE;
		}

		// SceneをImGui上へ表示するときに描画先のAlphaも合成に使われるため、
		// パーティクルのAlphaでScene全体の不透明度を上書きしない。
		blendDesc.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ZERO;
		blendDesc.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
		blendDesc.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ONE;
	}

	D3D12_CULL_MODE ToD3D12CullMode(ParticleCommon::CullMode cullMode) {
		switch (cullMode) {
		case ParticleCommon::CullMode::kBack:
			return D3D12_CULL_MODE_BACK;
		case ParticleCommon::CullMode::kFront:
			return D3D12_CULL_MODE_FRONT;
		default:
			return D3D12_CULL_MODE_NONE;
		}
	}
}

void ParticleCommon::Initialize(DirectXCommon* dxCommon) {
	if (isInitialized_) {
		return;
	}

	dxCommon_ = dxCommon;
	GenerateGraphicsPipeline();
	CreateVertexResource();

	currentBlendMode_ = BlendMode::kBlendModeNormal;
	currentCullMode_ = CullMode::kNone;
	currentDepthTest_ = true;
	currentDepthWrite_ = false;
	defaultCamera_ = nullptr;
	isInitialized_ = true;
}

void ParticleCommon::ResetState() {
	defaultCamera_ = nullptr;
	currentBlendMode_ = BlendMode::kBlendModeNormal;
	currentCullMode_ = CullMode::kNone;
	currentDepthTest_ = true;
	currentDepthWrite_ = false;
}

void ParticleCommon::SetRenderState(
	BlendMode blendMode,
	CullMode cullMode,
	bool depthTest,
	bool depthWrite
) {
	currentBlendMode_ = blendMode;
	currentCullMode_ = cullMode;
	currentDepthTest_ = depthTest;
	currentDepthWrite_ = depthWrite;
}

void ParticleCommon::SetCommonRenderState() {
	assert(dxCommon_ != nullptr);

	auto* commandList = dxCommon_->GetCommandList();
	commandList->SetGraphicsRootSignature(rootSignature_.Get());
	commandList->SetPipelineState(
		graphicsPipelineStates_
			[static_cast<uint32_t>(currentBlendMode_)]
			[static_cast<uint32_t>(currentCullMode_)]
			[currentDepthTest_ ? 1 : 0]
			[currentDepthWrite_ ? 1 : 0]
			.Get()
	);
	commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	commandList->IASetVertexBuffers(0, 1, &vertexBufferView_);
}

void ParticleCommon::MakeRootSignature(){
	HRESULT hr;

	D3D12_DESCRIPTOR_RANGE descriptorRanges[2] = {};

	// VS: t0 StructuredBuffer<TransformationMatrix>
	descriptorRanges[0].BaseShaderRegister = 0;
	descriptorRanges[0].NumDescriptors = 1;
	descriptorRanges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	descriptorRanges[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

	// PS: t0 Texture2D
	descriptorRanges[1].BaseShaderRegister = 0;
	descriptorRanges[1].NumDescriptors = 1;
	descriptorRanges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	descriptorRanges[1].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

	D3D12_ROOT_PARAMETER rootParameters[4] = {};

	// b0 : Material (PS)
	rootParameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
	rootParameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	rootParameters[0].Descriptor.ShaderRegister = 0;

	// t0 : TransformationMatrix structured buffer (VS)
	rootParameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	rootParameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
	rootParameters[1].DescriptorTable.pDescriptorRanges = &descriptorRanges[0];
	rootParameters[1].DescriptorTable.NumDescriptorRanges = 1;

	// t0 : Texture (PS)
	rootParameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	rootParameters[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	rootParameters[2].DescriptorTable.pDescriptorRanges = &descriptorRanges[1];
	rootParameters[2].DescriptorTable.NumDescriptorRanges = 1;

	// b1 : DirectionalLight (PS)
	rootParameters[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
	rootParameters[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	rootParameters[3].Descriptor.ShaderRegister = 1;

	D3D12_ROOT_SIGNATURE_DESC rootSignatureDesc{};
	rootSignatureDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
	rootSignatureDesc.pParameters = rootParameters;
	rootSignatureDesc.NumParameters = _countof(rootParameters);

	D3D12_STATIC_SAMPLER_DESC staticSampler = MakeStaticSamplerS0();
	rootSignatureDesc.pStaticSamplers = &staticSampler;
	rootSignatureDesc.NumStaticSamplers = 1;

	ID3DBlob* signatureBlob = nullptr;
	ID3DBlob* errorBlob = nullptr;
	hr = D3D12SerializeRootSignature(
		&rootSignatureDesc,
		D3D_ROOT_SIGNATURE_VERSION_1,
		&signatureBlob,
		&errorBlob
	);
	if(FAILED(hr)){
		if(errorBlob){
			Logger::Log(reinterpret_cast<char*>(errorBlob->GetBufferPointer()));
		}
		assert(false);
	}

	hr = dxCommon_->GetDevice()->CreateRootSignature(
		0,
		signatureBlob->GetBufferPointer(),
		signatureBlob->GetBufferSize(),
		IID_PPV_ARGS(&rootSignature_)
	);
	assert(SUCCEEDED(hr));
}

void ParticleCommon::GenerateGraphicsPipeline() {
	MakeRootSignature();

	// =========================
	// InputLayout
	// =========================
	D3D12_INPUT_ELEMENT_DESC inputElementDescs[4] = {};
	inputElementDescs[0].SemanticName = "POSITION";
	inputElementDescs[0].SemanticIndex = 0;
	inputElementDescs[0].Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
	inputElementDescs[0].AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;

	inputElementDescs[1].SemanticName = "TEXCOORD";
	inputElementDescs[1].SemanticIndex = 0;
	inputElementDescs[1].Format = DXGI_FORMAT_R32G32_FLOAT;
	inputElementDescs[1].AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;

	inputElementDescs[2].SemanticName = "NORMAL";
	inputElementDescs[2].SemanticIndex = 0;
	inputElementDescs[2].Format = DXGI_FORMAT_R32G32B32_FLOAT;
	inputElementDescs[2].AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;
	inputElementDescs[3].SemanticName = "COLOR";
	inputElementDescs[3].SemanticIndex = 0;
	inputElementDescs[3].Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
	inputElementDescs[3].AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;

	D3D12_INPUT_LAYOUT_DESC inputLayoutDesc{};
	inputLayoutDesc.pInputElementDescs = inputElementDescs;
	inputLayoutDesc.NumElements = _countof(inputElementDescs);

	// =========================
	// Shader
	// =========================
	auto vertexShaderBlob = dxCommon_->CompileShader(L"resources/shaders/Particle.VS.hlsl", L"vs_6_0");
	assert(vertexShaderBlob != nullptr);

	auto pixelShaderBlob = dxCommon_->CompileShader(L"resources/shaders/Particle.PS.hlsl", L"ps_6_0");
	assert(pixelShaderBlob != nullptr);

	// =========================================================
	// まず全体共通の PSO 設定を作る
	// BlendState だけあとで Normal / Add に差し替える
	// =========================================================
	D3D12_GRAPHICS_PIPELINE_STATE_DESC desc{};
	desc.pRootSignature = rootSignature_.Get();
	desc.InputLayout = inputLayoutDesc;
	desc.VS = { vertexShaderBlob->GetBufferPointer(), vertexShaderBlob->GetBufferSize() };
	desc.PS = { pixelShaderBlob->GetBufferPointer(), pixelShaderBlob->GetBufferSize() };
	desc.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
	desc.NumRenderTargets = 1;
	desc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
	desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	desc.SampleDesc.Count = 1;
	desc.SampleMask = D3D12_DEFAULT_SAMPLE_MASK;

	HRESULT hr;

	for (uint32_t blendIndex = 0;
		blendIndex < static_cast<uint32_t>(BlendMode::kCountOfBlendMode);
		++blendIndex) {
		D3D12_BLEND_DESC blendDesc{};
		ApplyBlendMode(blendDesc, static_cast<BlendMode>(blendIndex));
		desc.BlendState = blendDesc;

		for (uint32_t cullIndex = 0;
			cullIndex < static_cast<uint32_t>(CullMode::kCount);
			++cullIndex) {
			D3D12_RASTERIZER_DESC rasterizerDesc{};
			rasterizerDesc.CullMode =
				ToD3D12CullMode(static_cast<CullMode>(cullIndex));
			rasterizerDesc.FillMode = D3D12_FILL_MODE_SOLID;
			rasterizerDesc.DepthClipEnable = TRUE;
			desc.RasterizerState = rasterizerDesc;

			for (uint32_t depthTest = 0; depthTest < 2; ++depthTest) {
				for (uint32_t depthWrite = 0; depthWrite < 2; ++depthWrite) {
					D3D12_DEPTH_STENCIL_DESC depthStencilDesc{};
					depthStencilDesc.DepthEnable = depthTest != 0;
					depthStencilDesc.DepthWriteMask = depthWrite != 0
						? D3D12_DEPTH_WRITE_MASK_ALL
						: D3D12_DEPTH_WRITE_MASK_ZERO;
					depthStencilDesc.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
					desc.DepthStencilState = depthStencilDesc;

					hr = dxCommon_->GetDevice()->CreateGraphicsPipelineState(
						&desc,
						IID_PPV_ARGS(
							&graphicsPipelineStates_
								[blendIndex]
								[cullIndex]
								[depthTest]
								[depthWrite]
						)
					);
					assert(SUCCEEDED(hr));
				}
			}
		}
	}
}

void ParticleCommon::CreateVertexResource(){
	// 板ポリ 2枚三角形
	std::array<VertexData, 6> vertices = {
		VertexData{ {-0.5f, -0.5f, 0.0f, 1.0f}, {0.0f, 1.0f}, {0.0f, 0.0f, -1.0f}, {1.0f, 1.0f, 1.0f, 1.0f} },
		VertexData{ {-0.5f,  0.5f, 0.0f, 1.0f}, {0.0f, 0.0f}, {0.0f, 0.0f, -1.0f}, {1.0f, 1.0f, 1.0f, 1.0f} },
		VertexData{ { 0.5f, -0.5f, 0.0f, 1.0f}, {1.0f, 1.0f}, {0.0f, 0.0f, -1.0f}, {1.0f, 1.0f, 1.0f, 1.0f} },

		VertexData{ { 0.5f, -0.5f, 0.0f, 1.0f}, {1.0f, 1.0f}, {0.0f, 0.0f, -1.0f}, {1.0f, 1.0f, 1.0f, 1.0f} },
		VertexData{ {-0.5f,  0.5f, 0.0f, 1.0f}, {0.0f, 0.0f}, {0.0f, 0.0f, -1.0f}, {1.0f, 1.0f, 1.0f, 1.0f} },
		VertexData{ { 0.5f,  0.5f, 0.0f, 1.0f}, {1.0f, 0.0f}, {0.0f, 0.0f, -1.0f}, {1.0f, 1.0f, 1.0f, 1.0f} },
	};

	vertexResource_ = dxCommon_->CreateBufferResource(sizeof(VertexData) * vertices.size());
	vertexBufferView_.BufferLocation = vertexResource_->GetGPUVirtualAddress();
	vertexBufferView_.SizeInBytes = UINT(sizeof(VertexData) * vertices.size());
	vertexBufferView_.StrideInBytes = sizeof(VertexData);

	vertexResource_->Map(0, nullptr, reinterpret_cast<void**>(&vertexData_));
	std::memcpy(vertexData_, vertices.data(), sizeof(VertexData) * vertices.size());
}

inline D3D12_STATIC_SAMPLER_DESC ParticleCommon::MakeStaticSamplerS0(){
	D3D12_STATIC_SAMPLER_DESC s{};
	s.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
	s.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	s.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
	s.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
	s.ShaderRegister = 0;
	s.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	return s;
}
