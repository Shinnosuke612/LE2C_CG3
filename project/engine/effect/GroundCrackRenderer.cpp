// 役割: 接地法線の平面上に分岐リボンを生成し、暗い割れ目と発光縁を重ねて描画する。
#include "GroundCrackRenderer.h"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstring>
#include <d3dcompiler.h>
#include "../3d/Camera.h"
#include "../base/DirectXCommon.h"
#include "../base/RenderFormats.h"
#include "../math/Math.h"

float GroundCrackRenderer::Random01(uint32_t& state) {
	state ^= state << 13; state ^= state >> 17; state ^= state << 5;
	return static_cast<float>(state & 0x00ffffffu) / 16777215.0f;
}
void GroundCrackRenderer::Initialize(DirectXCommon* dxCommon, uint32_t maxCracks, uint32_t maxVertices) {
	Finalize();
	if (!dxCommon || maxCracks == 0 || maxVertices == 0) { return; }
	dxCommon_ = dxCommon;
	maxCracks_ = maxCracks;
	maxVertices_ = maxVertices;
	vertices_.reserve(maxVertices_);
	if (!CreatePipeline() || !CreateResources(maxVertices_)) {
		Finalize();
		return;
	}
	isInitialized_ = true;
}
void GroundCrackRenderer::Finalize() {
	mappedVertices_ = nullptr; mappedCameraData_ = nullptr; cracks_.clear(); vertices_.clear();
	vertexResource_.Reset(); cameraResource_.Reset(); basePipelineState_.Reset(); glowPipelineState_.Reset(); rootSignature_.Reset(); dxCommon_ = nullptr;
	maxCracks_ = 0; maxVertices_ = 0; baseVertexCount_ = 0; drawSlotIndex_ = 0; isInitialized_ = false;
}
void GroundCrackRenderer::Update(float deltaTime) {
	drawSlotIndex_ = 0;
	const float safeDelta = (std::max)(deltaTime, 0.0f);
	for (Crack& crack : cracks_) { crack.age += safeDelta; }
	cracks_.erase(std::remove_if(cracks_.begin(), cracks_.end(), [](const Crack& crack) { return crack.age >= crack.lifetime; }), cracks_.end());
}
void GroundCrackRenderer::Spawn(const SpawnRequest& request) {
	if (!isInitialized_ || request.radius <= 0.0f || request.width <= 0.0f || request.lifetime <= 0.0f) return;
	if (cracks_.size() >= maxCracks_) cracks_.erase(cracks_.begin());
	Crack crack{}; crack.lifetime = request.lifetime; crack.width = request.width;
	crack.normal = Math::Length(request.normal) > 0.0001f ? Math::Normalize(request.normal) : Vector3{ 0.0f, 1.0f, 0.0f };
	const Vector3 reference = std::abs(crack.normal.y) < 0.95f ? Vector3{ 0.0f, 1.0f, 0.0f } : Vector3{ 1.0f, 0.0f, 0.0f };
	const Vector3 tangent = Math::Normalize(Math::Cross(reference, crack.normal));
	const Vector3 bitangent = Math::Normalize(Math::Cross(crack.normal, tangent));
	const Vector3 origin = Math::Add(request.position, Math::Multiply(crack.normal, request.surfaceOffset));
	uint32_t state = request.seed ? request.seed : 1u;
	const uint32_t branches = (std::min)((std::max)(request.primaryBranchCount, 1u), 24u);
	const uint32_t segments = (std::min)((std::max)(request.segmentsPerBranch, 1u), 12u);
	for (uint32_t branch = 0; branch < branches; ++branch) {
		float angle = 6.2831853f * (static_cast<float>(branch) / branches + (Random01(state) - 0.5f) * 0.12f);
		Vector3 previous = origin; const float length = request.radius * (0.58f + Random01(state) * 0.42f);
		for (uint32_t index = 1; index <= segments; ++index) {
			angle += (Random01(state) - 0.5f) * 0.62f;
			const float step = length / segments * (0.72f + Random01(state) * 0.48f);
			const Vector3 direction = Math::Add(Math::Multiply(tangent, std::cos(angle)), Math::Multiply(bitangent, std::sin(angle)));
			const Vector3 next = Math::Add(previous, Math::Multiply(direction, step));
			crack.segments.push_back({ previous, next, static_cast<float>(index) / segments });
			if (index > 1 && Random01(state) < request.branchProbability * 0.35f) {
				const float sideAngle = angle + (Random01(state) < 0.5f ? -1.0f : 1.0f) * (0.55f + Random01(state) * 0.55f);
				const Vector3 side = Math::Add(Math::Multiply(tangent, std::cos(sideAngle)), Math::Multiply(bitangent, std::sin(sideAngle)));
				crack.segments.push_back({ next, Math::Add(next, Math::Multiply(side, step * (0.7f + Random01(state) * 0.5f))), static_cast<float>(index) / segments });
			}
			previous = next;
		}
	}
	cracks_.push_back(std::move(crack));
}
void GroundCrackRenderer::AddRibbon(const Segment& segment, const Vector3& normal, float width, const Vector4& color) {
	Vector3 direction = Math::Subtract(segment.end, segment.start); if (Math::Length(direction) <= 0.0001f) return;
	direction = Math::Normalize(direction); const Vector3 side = Math::Normalize(Math::Cross(normal, direction)); const Vector3 offset = Math::Multiply(side, width * 0.5f);
	vertices_.insert(vertices_.end(), {{ Math::Add(segment.start, offset), color }, { Math::Subtract(segment.start, offset), color }, { Math::Add(segment.end, offset), color }, { Math::Add(segment.end, offset), color }, { Math::Subtract(segment.start, offset), color }, { Math::Subtract(segment.end, offset), color }});
}
void GroundCrackRenderer::BuildVertices() {
	vertices_.clear(); baseVertexCount_ = 0;
	for (const Crack& crack : cracks_) {
		const float fade = crack.age > crack.lifetime * 0.65f ? (std::max)(0.0f, 1.0f - (crack.age - crack.lifetime * 0.65f) / (crack.lifetime * 0.35f)) : 1.0f;
		const float reveal = (std::min)(1.0f, 0.12f + crack.age / 0.08f);
		for (const Segment& segment : crack.segments) if (segment.reveal <= reveal) AddRibbon(segment, crack.normal, crack.width * 1.7f, { 0.04f, 0.006f, 0.002f, 0.9f * fade });
	}
	baseVertexCount_ = static_cast<uint32_t>(vertices_.size());
	for (const Crack& crack : cracks_) {
		const float fade = crack.age > crack.lifetime * 0.65f ? (std::max)(0.0f, 1.0f - (crack.age - crack.lifetime * 0.65f) / (crack.lifetime * 0.35f)) : 1.0f;
		const float reveal = (std::min)(1.0f, 0.12f + crack.age / 0.08f);
		for (const Segment& segment : crack.segments) if (segment.reveal <= reveal) AddRibbon(segment, crack.normal, crack.width, { 2.4f, 0.22f, 0.025f, 0.8f * fade });
	}
}
void GroundCrackRenderer::Draw(const Camera* camera) {
	if (!isInitialized_ || !camera || cracks_.empty() || drawSlotIndex_ >= kDrawSlotCount) return;
	BuildVertices(); if (vertices_.empty()) return;
	const uint32_t drawSlot = drawSlotIndex_++;
	const size_t vertexSlotOffset = static_cast<size_t>(drawSlot) * maxVertices_;
	const UINT64 vertexSlotByteOffset = static_cast<UINT64>(vertexSlotOffset * sizeof(Vertex));
	const uint32_t count = (std::min)(static_cast<uint32_t>(vertices_.size()), maxVertices_);
	std::memcpy(mappedVertices_ + vertexSlotOffset, vertices_.data(), sizeof(Vertex) * count);
	CameraData* cameraData = reinterpret_cast<CameraData*>(
		mappedCameraData_ + static_cast<size_t>(drawSlot) * kCameraSlotSize
	);
	cameraData->viewProjection = camera->GetViewProjectionMatrix();
	D3D12_VERTEX_BUFFER_VIEW vertexBufferView = vertexBufferView_;
	vertexBufferView.BufferLocation += vertexSlotByteOffset;
	ID3D12GraphicsCommandList* list = dxCommon_->GetCommandList();
	if (!list) { return; }
	list->SetGraphicsRootSignature(rootSignature_.Get());
	list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	list->IASetVertexBuffers(0, 1, &vertexBufferView);
	list->SetGraphicsRootConstantBufferView(
		0,
		cameraResource_->GetGPUVirtualAddress() +
			static_cast<UINT64>(drawSlot) * kCameraSlotSize
	);
	if (baseVertexCount_ > 0) { list->SetPipelineState(basePipelineState_.Get()); list->DrawInstanced((std::min)(baseVertexCount_, count), 1, 0, 0); }
	if (count > baseVertexCount_) { list->SetPipelineState(glowPipelineState_.Get()); list->DrawInstanced(count - baseVertexCount_, 1, baseVertexCount_, 0); }
}
bool GroundCrackRenderer::CreatePipeline() {
	D3D12_ROOT_PARAMETER parameter{}; parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV; parameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX; parameter.Descriptor.ShaderRegister = 0;
	D3D12_ROOT_SIGNATURE_DESC root{}; root.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT; root.pParameters = &parameter; root.NumParameters = 1;
	Microsoft::WRL::ComPtr<ID3DBlob> signature, error;
	const HRESULT serializeResult = D3D12SerializeRootSignature(
		&root, D3D_ROOT_SIGNATURE_VERSION_1, &signature, &error
	);
	if (FAILED(serializeResult) || !signature) { return false; }
	const HRESULT rootResult = dxCommon_->GetDevice()->CreateRootSignature(
		0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&rootSignature_)
	);
	if (FAILED(rootResult)) { return false; }
	D3D12_INPUT_ELEMENT_DESC input[] = {{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }, { "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }};
	// Ground CrackのVertex形式はLightningと同じPOSITION/COLORなので、
	// PSO診断中は既存実績のあるShader Bytecodeをそのまま用いる。
	auto vs = dxCommon_->CompileShader(L"resources/shaders/Lightning.VS.hlsl", L"vs_6_0");
	auto ps = dxCommon_->CompileShader(L"resources/shaders/Lightning.PS.hlsl", L"ps_6_0");
	if (!vs || !ps) { return false; }
	// LightningRendererと同じ検証済みStateを先に使い、PSO作成失敗を切り分ける。
	// Alpha外側レイヤーとDepth Biasは描画成功の確認後に段階的に戻す。
	D3D12_BLEND_DESC blend{}; blend.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL; blend.RenderTarget[0].BlendEnable = TRUE; blend.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA; blend.RenderTarget[0].DestBlend = D3D12_BLEND_ONE; blend.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD; blend.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE; blend.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ZERO; blend.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
	D3D12_RASTERIZER_DESC raster{}; raster.FillMode = D3D12_FILL_MODE_SOLID; raster.CullMode = D3D12_CULL_MODE_NONE; raster.DepthClipEnable = TRUE;
	D3D12_DEPTH_STENCIL_DESC depth{}; depth.DepthEnable = TRUE; depth.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO; depth.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
	D3D12_GRAPHICS_PIPELINE_STATE_DESC desc{}; desc.pRootSignature = rootSignature_.Get(); desc.InputLayout = { input, _countof(input) }; desc.VS = { vs->GetBufferPointer(), vs->GetBufferSize() }; desc.PS = { ps->GetBufferPointer(), ps->GetBufferSize() }; desc.BlendState = blend; desc.RasterizerState = raster; desc.DepthStencilState = depth; desc.SampleMask = D3D12_DEFAULT_SAMPLE_MASK; desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE; desc.NumRenderTargets = 1; desc.RTVFormats[0] = RenderFormats::kSceneHdrFormat; desc.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT; desc.SampleDesc.Count = 1;
	const HRESULT basePipelineResult = dxCommon_->GetDevice()->CreateGraphicsPipelineState(
		&desc, IID_PPV_ARGS(&basePipelineState_)
	);
	if (FAILED(basePipelineResult)) { return false; }
	// Base／Glowは現在同じAdd Blendを使う。PSOを共有し、未検証Stateを増やさない。
	glowPipelineState_ = basePipelineState_;
	return true;
}
bool GroundCrackRenderer::CreateResources(uint32_t maxVertices) {
	vertexResource_ = dxCommon_->CreateBufferResource(
		sizeof(Vertex) * static_cast<size_t>(maxVertices) * kDrawSlotCount
	);
	if (!vertexResource_) { return false; }
	const HRESULT vertexMapResult = vertexResource_->Map(
		0, nullptr, reinterpret_cast<void**>(&mappedVertices_)
	);
	if (FAILED(vertexMapResult) || !mappedVertices_) { return false; }
	vertexBufferView_.BufferLocation = vertexResource_->GetGPUVirtualAddress();
	vertexBufferView_.SizeInBytes = static_cast<UINT>(sizeof(Vertex) * maxVertices);
	vertexBufferView_.StrideInBytes = sizeof(Vertex);
	cameraResource_ = dxCommon_->CreateBufferResource(
		static_cast<size_t>(kCameraSlotSize) * kDrawSlotCount
	);
	if (!cameraResource_) { return false; }
	const HRESULT cameraMapResult = cameraResource_->Map(
		0, nullptr, reinterpret_cast<void**>(&mappedCameraData_)
	);
	if (FAILED(cameraMapResult) || !mappedCameraData_) { return false; }
	for (uint32_t drawSlot = 0; drawSlot < kDrawSlotCount; ++drawSlot) {
		CameraData* cameraData = reinterpret_cast<CameraData*>(
			mappedCameraData_ + static_cast<size_t>(drawSlot) * kCameraSlotSize
		);
		cameraData->viewProjection = MakeIdentity4x4();
	}
	return vertexBufferView_.BufferLocation != 0;
}
