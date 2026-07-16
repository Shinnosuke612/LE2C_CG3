// 役割: 稲妻の分岐形状と描画用GPUリソース生成を実装する。
#include "LightningRenderer.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstring>

#include <d3dcompiler.h>

#include "../3d/Camera.h"
#include "../base/DirectXCommon.h"
#include "../base/RenderFormats.h"
#include "../math/Math.h"
#include "../math/Matrix4x4.h"
#include "../utility/Logger.h"

void LightningRenderer::Initialize(DirectXCommon* dxCommon, uint32_t maxVertexCount) {
	dxCommon_ = dxCommon;
	maxVertexCount_ = maxVertexCount;
	assert(dxCommon_);
	assert(maxVertexCount_ > 0);

	CreateRootSignature();
	CreateGraphicsPipeline();
	CreateResources(maxVertexCount_);
}

void LightningRenderer::Finalize() {
	mappedVertices_ = nullptr;
	cameraData_ = nullptr;
	vertexResource_.Reset();
	cameraResource_.Reset();
	pipelineState_.Reset();
	rootSignature_.Reset();
	vertices_.clear();
	maxVertexCount_ = 0;
	dxCommon_ = nullptr;
}

void LightningRenderer::Update(float deltaTime) {
	time_ += deltaTime;
	if (activeTime_ > 0.0f) {
		activeTime_ = (std::max)(0.0f, activeTime_ - deltaTime);
	}
}

void LightningRenderer::Draw(const Camera* camera) {
	if (
		!dxCommon_ ||
		!camera ||
		!settings_.enabled ||
		settings_.segmentCount == 0 ||
		(activeTime_ <= 0.0f && !settings_.previewContinuous)
	) {
		return;
	}

	BuildVertices(camera);
	if (vertices_.empty()) {
		return;
	}

	const uint32_t vertexCount = (std::min)(
		static_cast<uint32_t>(vertices_.size()),
		maxVertexCount_
	);
	std::memcpy(mappedVertices_, vertices_.data(), sizeof(Vertex) * vertexCount);
	cameraData_->viewProjection = camera->GetViewProjectionMatrix();

	ID3D12GraphicsCommandList* commandList = dxCommon_->GetCommandList();
	commandList->SetGraphicsRootSignature(rootSignature_.Get());
	commandList->SetPipelineState(pipelineState_.Get());
	commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	commandList->IASetVertexBuffers(0, 1, &vertexBufferView_);
	commandList->SetGraphicsRootConstantBufferView(
		0,
		cameraResource_->GetGPUVirtualAddress()
	);
	commandList->DrawInstanced(vertexCount, 1, 0, 0);
}

void LightningRenderer::Trigger(const Settings& settings) {
	settings_ = settings;
	settings_.enabled = true;
	settings_.previewContinuous = false;
	activeTime_ = (std::max)(settings_.duration, 0.01f);
}

void LightningRenderer::CreateRootSignature() {
	D3D12_ROOT_PARAMETER rootParameter{};
	rootParameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
	rootParameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
	rootParameter.Descriptor.ShaderRegister = 0;

	D3D12_ROOT_SIGNATURE_DESC rootSignatureDesc{};
	rootSignatureDesc.Flags =
		D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
	rootSignatureDesc.pParameters = &rootParameter;
	rootSignatureDesc.NumParameters = 1;

	Microsoft::WRL::ComPtr<ID3DBlob> signatureBlob;
	Microsoft::WRL::ComPtr<ID3DBlob> errorBlob;
	const HRESULT serializeResult = D3D12SerializeRootSignature(
		&rootSignatureDesc,
		D3D_ROOT_SIGNATURE_VERSION_1,
		&signatureBlob,
		&errorBlob
	);
	if (FAILED(serializeResult)) {
		if (errorBlob) {
			Logger::Log(static_cast<const char*>(errorBlob->GetBufferPointer()));
		}
		assert(false);
	}

	const HRESULT createResult = dxCommon_->GetDevice()->CreateRootSignature(
		0,
		signatureBlob->GetBufferPointer(),
		signatureBlob->GetBufferSize(),
		IID_PPV_ARGS(&rootSignature_)
	);
	assert(SUCCEEDED(createResult));
}

void LightningRenderer::CreateGraphicsPipeline() {
	D3D12_INPUT_ELEMENT_DESC inputElements[2]{};
	inputElements[0].SemanticName = "POSITION";
	inputElements[0].Format = DXGI_FORMAT_R32G32B32_FLOAT;
	inputElements[0].AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;
	inputElements[1].SemanticName = "COLOR";
	inputElements[1].Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
	inputElements[1].AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;

	const D3D12_INPUT_LAYOUT_DESC inputLayout{
		inputElements,
		_countof(inputElements)
	};

	const auto vertexShader = dxCommon_->CompileShader(
		L"resources/shaders/Lightning.VS.hlsl",
		L"vs_6_0"
	);
	const auto pixelShader = dxCommon_->CompileShader(
		L"resources/shaders/Lightning.PS.hlsl",
		L"ps_6_0"
	);
	assert(vertexShader);
	assert(pixelShader);

	D3D12_BLEND_DESC blendDesc{};
	blendDesc.RenderTarget[0].RenderTargetWriteMask =
		D3D12_COLOR_WRITE_ENABLE_ALL;
	blendDesc.RenderTarget[0].BlendEnable = TRUE;
	blendDesc.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
	blendDesc.RenderTarget[0].DestBlend = D3D12_BLEND_ONE;
	blendDesc.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
	blendDesc.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
	blendDesc.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ZERO;
	blendDesc.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;

	D3D12_RASTERIZER_DESC rasterizerDesc{};
	rasterizerDesc.FillMode = D3D12_FILL_MODE_SOLID;
	rasterizerDesc.CullMode = D3D12_CULL_MODE_NONE;
	rasterizerDesc.DepthClipEnable = TRUE;

	D3D12_DEPTH_STENCIL_DESC depthStencilDesc{};
	depthStencilDesc.DepthEnable = TRUE;
	depthStencilDesc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
	depthStencilDesc.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;

	D3D12_GRAPHICS_PIPELINE_STATE_DESC pipelineDesc{};
	pipelineDesc.pRootSignature = rootSignature_.Get();
	pipelineDesc.InputLayout = inputLayout;
	pipelineDesc.VS = {
		vertexShader->GetBufferPointer(),
		vertexShader->GetBufferSize()
	};
	pipelineDesc.PS = {
		pixelShader->GetBufferPointer(),
		pixelShader->GetBufferSize()
	};
	pipelineDesc.BlendState = blendDesc;
	pipelineDesc.RasterizerState = rasterizerDesc;
	pipelineDesc.DepthStencilState = depthStencilDesc;
	pipelineDesc.SampleMask = D3D12_DEFAULT_SAMPLE_MASK;
	pipelineDesc.PrimitiveTopologyType =
		D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	pipelineDesc.NumRenderTargets = 1;
	pipelineDesc.RTVFormats[0] = RenderFormats::kSceneHdrFormat;
	pipelineDesc.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
	pipelineDesc.SampleDesc.Count = 1;

	const HRESULT result =
		dxCommon_->GetDevice()->CreateGraphicsPipelineState(
			&pipelineDesc,
			IID_PPV_ARGS(&pipelineState_)
		);
	assert(SUCCEEDED(result));
}

void LightningRenderer::CreateResources(uint32_t maxVertexCount) {
	vertexResource_ = dxCommon_->CreateBufferResource(sizeof(Vertex) * maxVertexCount);
	vertexResource_->Map(
		0,
		nullptr,
		reinterpret_cast<void**>(&mappedVertices_)
	);

	vertexBufferView_.BufferLocation =
		vertexResource_->GetGPUVirtualAddress();
	vertexBufferView_.SizeInBytes =
		static_cast<UINT>(sizeof(Vertex) * maxVertexCount);
	vertexBufferView_.StrideInBytes = sizeof(Vertex);

	cameraResource_ = dxCommon_->CreateBufferResource(sizeof(CameraData));
	cameraResource_->Map(
		0,
		nullptr,
		reinterpret_cast<void**>(&cameraData_)
	);
	cameraData_->viewProjection = MakeIdentity4x4();
}

void LightningRenderer::BuildVertices(const Camera* camera) {
	vertices_.clear();

	uint32_t state =
		settings_.seed ^
		(static_cast<uint32_t>(time_ * 24.0f) * 747796405u);

	const uint32_t segmentCount = (std::max)(settings_.segmentCount, 1u);
	const Vector3 direction = Math::Subtract(settings_.end, settings_.start);
	const float length = Math::Length(direction);
	const Vector3 forward =
		length > 0.0001f ? Math::Normalize(direction) : Vector3{ 0.0f, 1.0f, 0.0f };

	const float fade = activeTime_ > 0.0f
		? (std::min)(activeTime_ / (std::max)(settings_.duration, 0.01f), 1.0f)
		: 1.0f;
	const Vector4 coreColor = MultiplyAlpha(settings_.coreColor, fade);
	const Vector4 branchColor = MultiplyAlpha(settings_.branchColor, fade);

	Vector3 previous = settings_.start;
	for (uint32_t index = 1; index <= segmentCount; ++index) {
		const float t = static_cast<float>(index) / static_cast<float>(segmentCount);
		Vector3 point = Math::Add(settings_.start, Math::Multiply(direction, t));

		if (index < segmentCount) {
			Vector3 side = RandomUnitVector(state);
			side = Math::Subtract(
				side,
				Math::Multiply(forward, Math::Dot(side, forward))
			);
			const float sideLength = Math::Length(side);
			if (sideLength > 0.0001f) {
				side = Math::Normalize(side);
				const float width =
					settings_.jitter *
					(0.25f + Random01(state)) *
					std::sin(t * 3.14159265f);
				point = Math::Add(point, Math::Multiply(side, width));
			}
		}

		AddLightningSegment(
			previous,
			point,
			coreColor,
			settings_.thickness,
			camera,
			state
		);

		if (
			index < segmentCount &&
			Random01(state) < settings_.branchProbability
		) {
			Vector3 branchDirection = RandomUnitVector(state);
			branchDirection = Math::Normalize(
				Math::Add(
					branchDirection,
					Math::Multiply(forward, 0.45f)
				)
			);
			const float branchLength =
				settings_.branchLength * (0.4f + Random01(state));
			AddLightningSegment(
				point,
				Math::Add(point, Math::Multiply(branchDirection, branchLength)),
				branchColor,
				settings_.thickness * 0.55f,
				camera,
				state
			);
		}

		previous = point;
	}
}

void LightningRenderer::AddLightningSegment(
	const Vector3& start,
	const Vector3& end,
	const Vector4& color,
	float thickness,
	const Camera* camera,
	uint32_t& state
) {
	(void)state;
	const float baseHalfWidth = (std::max)(thickness, 0.001f);
	AddRibbon(start, end, MultiplyAlpha(color, 0.22f), baseHalfWidth * 4.0f, camera);
	AddRibbon(start, end, color, baseHalfWidth, camera);
}

void LightningRenderer::AddRibbon(
	const Vector3& start,
	const Vector3& end,
	const Vector4& color,
	float halfWidth,
	const Camera* camera
) {
	if (vertices_.size() + 6 > maxVertexCount_) {
		return;
	}

	const Vector3 segment = Math::Subtract(end, start);
	const float segmentLength = Math::Length(segment);
	if (segmentLength <= 0.0001f || halfWidth <= 0.0f) {
		return;
	}

	const Vector3 forward = Math::Normalize(segment);
	const Vector3 center = Math::Multiply(Math::Add(start, end), 0.5f);
	Vector3 toCamera = Math::Subtract(camera->GetTranslate(), center);
	if (Math::Length(toCamera) <= 0.0001f) {
		toCamera = { 0.0f, 0.0f, -1.0f };
	}
	toCamera = Math::Normalize(toCamera);

	Vector3 side = Math::Cross(forward, toCamera);
	if (Math::Length(side) <= 0.0001f) {
		side = Math::Cross(forward, { 0.0f, 1.0f, 0.0f });
	}
	if (Math::Length(side) <= 0.0001f) {
		side = { 1.0f, 0.0f, 0.0f };
	}
	side = Math::Multiply(Math::Normalize(side), halfWidth);

	const Vector3 start0 = Math::Add(start, side);
	const Vector3 start1 = Math::Subtract(start, side);
	const Vector3 end0 = Math::Add(end, side);
	const Vector3 end1 = Math::Subtract(end, side);

	vertices_.push_back({ start0, color });
	vertices_.push_back({ end0, color });
	vertices_.push_back({ start1, color });
	vertices_.push_back({ start1, color });
	vertices_.push_back({ end0, color });
	vertices_.push_back({ end1, color });
}

float LightningRenderer::Random01(uint32_t& state) {
	state ^= state << 13;
	state ^= state >> 17;
	state ^= state << 5;
	return static_cast<float>(state & 0x00ffffffu) / static_cast<float>(0x01000000u);
}

Vector3 LightningRenderer::RandomUnitVector(uint32_t& state) {
	Vector3 v{
		Random01(state) * 2.0f - 1.0f,
		Random01(state) * 2.0f - 1.0f,
		Random01(state) * 2.0f - 1.0f
	};
	const float length = Math::Length(v);
	if (length <= 0.0001f) {
		return { 1.0f, 0.0f, 0.0f };
	}
	return Math::Multiply(v, 1.0f / length);
}

Vector4 LightningRenderer::MultiplyAlpha(const Vector4& color, float alphaScale) {
	return {
		color.x,
		color.y,
		color.z,
		color.w * alphaScale
	};
}
