// 役割: デバッグ形状の頂点生成と描画コマンド設定を実装する。
#include "DebugRenderer.h"
#include "../base/RenderFormats.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstring>

#include "../3d/Camera.h"
#include "../base/DirectXCommon.h"
#include "../math/Math.h"
#include "../utility/Logger.h"

namespace {

constexpr float kPi = 3.14159265358979323846f;

Vector3 GetTranslation(const Matrix4x4& matrix) {
	return {
		matrix.m[3][0],
		matrix.m[3][1],
		matrix.m[3][2]
	};
}

Vector3 GetNormalizedAxis(const Matrix4x4& matrix, uint32_t row) {
	return Math::Normalize({
		matrix.m[row][0],
		matrix.m[row][1],
		matrix.m[row][2]
	});
}

} // namespace

DebugRenderer* DebugRenderer::GetInstance() {
	static DebugRenderer instance;
	return &instance;
}

void DebugRenderer::Initialize(
	DirectXCommon* dxCommon,
	uint32_t maxVertexCount
) {
	if (isInitialized_) {
		return;
	}

	assert(dxCommon);
	assert(maxVertexCount >= 2);

	dxCommon_ = dxCommon;
	maxVertexCount_ = maxVertexCount;
	vertices_.reserve(maxVertexCount_);
	depthTestedVertices_.reserve(maxVertexCount_);
	solidVertices_.reserve(maxVertexCount_);

	CreateRootSignature();
	CreateGraphicsPipeline();
	CreateResources();
	isInitialized_ = true;
}

void DebugRenderer::Finalize() {
	vertices_.clear();
	depthTestedVertices_.clear();
	solidVertices_.clear();
	vertices_.shrink_to_fit();
	depthTestedVertices_.shrink_to_fit();
	solidVertices_.shrink_to_fit();
	mappedVertices_ = nullptr;
	mappedDepthTestedVertices_ = nullptr;
	mappedSolidVertices_ = nullptr;
	cameraData_ = nullptr;
	vertexResource_.Reset();
	depthTestedVertexResource_.Reset();
	solidVertexResource_.Reset();
	cameraResource_.Reset();
	pipelineState_.Reset();
	depthTestedPipelineState_.Reset();
	solidPipelineState_.Reset();
	rootSignature_.Reset();
	dxCommon_ = nullptr;
	maxVertexCount_ = 0;
	isInitialized_ = false;
}

void DebugRenderer::Clear() {
	vertices_.clear();
	depthTestedVertices_.clear();
	solidVertices_.clear();
}

void DebugRenderer::AddLine(
	const Vector3& start,
	const Vector3& end,
	const Vector4& color
) {
	if (!isInitialized_ || vertices_.size() + 2 > maxVertexCount_) {
		return;
	}

	vertices_.push_back({ start, color });
	vertices_.push_back({ end, color });
}

void DebugRenderer::AddDepthTestedLine(
	const Vector3& start,
	const Vector3& end,
	const Vector4& color
) {
	if (
		!isInitialized_ ||
		depthTestedVertices_.size() + 2 > maxVertexCount_
	) {
		return;
	}

	depthTestedVertices_.push_back({ start, color });
	depthTestedVertices_.push_back({ end, color });
}

void DebugRenderer::AddSphere(
	const Vector3& center,
	float radius,
	const Vector4& color,
	uint32_t segments
) {
	if (radius <= 0.0f) {
		return;
	}

	segments = (std::max)(segments, 4u);
	const float angleStep = 2.0f * kPi / static_cast<float>(segments);

	for (uint32_t plane = 0; plane < 3; ++plane) {
		for (uint32_t segment = 0; segment < segments; ++segment) {
			const float angle0 = angleStep * static_cast<float>(segment);
			const float angle1 =
				angleStep * static_cast<float>(segment + 1);
			const float cosine0 = std::cos(angle0) * radius;
			const float sine0 = std::sin(angle0) * radius;
			const float cosine1 = std::cos(angle1) * radius;
			const float sine1 = std::sin(angle1) * radius;

			Vector3 start = center;
			Vector3 end = center;
			if (plane == 0) {
				start.x += cosine0;
				start.y += sine0;
				end.x += cosine1;
				end.y += sine1;
			}
			else if (plane == 1) {
				start.x += cosine0;
				start.z += sine0;
				end.x += cosine1;
				end.z += sine1;
			}
			else {
				start.y += cosine0;
				start.z += sine0;
				end.y += cosine1;
				end.z += sine1;
			}
			AddLine(start, end, color);
		}
	}
}

void DebugRenderer::AddOBB(
	const Vector3& center,
	const std::array<Vector3, 3>& axis,
	const Vector3& halfSize,
	const Vector4& color
) {
	if (
		halfSize.x <= 0.0f ||
		halfSize.y <= 0.0f ||
		halfSize.z <= 0.0f
	) {
		return;
	}

	const Vector3 extentX = Math::Multiply(axis[0], halfSize.x);
	const Vector3 extentY = Math::Multiply(axis[1], halfSize.y);
	const Vector3 extentZ = Math::Multiply(axis[2], halfSize.z);
	const auto makeCorner = [&](float x, float y, float z) {
		return Math::Add(
			Math::Add(center, Math::Multiply(extentX, x)),
			Math::Add(Math::Multiply(extentY, y), Math::Multiply(extentZ, z))
		);
	};

	const std::array<Vector3, 8> corners = {
		makeCorner(-1.0f, -1.0f, -1.0f),
		makeCorner(1.0f, -1.0f, -1.0f),
		makeCorner(1.0f, 1.0f, -1.0f),
		makeCorner(-1.0f, 1.0f, -1.0f),
		makeCorner(-1.0f, -1.0f, 1.0f),
		makeCorner(1.0f, -1.0f, 1.0f),
		makeCorner(1.0f, 1.0f, 1.0f),
		makeCorner(-1.0f, 1.0f, 1.0f)
	};
	constexpr std::array<std::array<uint32_t, 2>, 12> edges = {{
		{{0, 1}}, {{1, 2}}, {{2, 3}}, {{3, 0}},
		{{4, 5}}, {{5, 6}}, {{6, 7}}, {{7, 4}},
		{{0, 4}}, {{1, 5}}, {{2, 6}}, {{3, 7}}
	}};
	for (const auto& edge : edges) {
		AddLine(corners[edge[0]], corners[edge[1]], color);
	}
}

void DebugRenderer::AddTriangle(
	const Vector3& a,
	const Vector3& b,
	const Vector3& c,
	const Vector4& color
) {
	if (!isInitialized_ || solidVertices_.size() + 3 > maxVertexCount_) {
		return;
	}
	solidVertices_.push_back({ a, color });
	solidVertices_.push_back({ b, color });
	solidVertices_.push_back({ c, color });
}

void DebugRenderer::AddSolidSphere(
	const Vector3& center,
	float radius,
	const Vector4& color,
	uint32_t segments
) {
	if (radius <= 0.0f) {
		return;
	}
	segments = (std::max)(segments, 4u);
	const uint32_t rings = (std::max)(segments / 2u, 3u);
	for (uint32_t ring = 0; ring < rings; ++ring) {
		const float latitude0 = -kPi * 0.5f +
			kPi * static_cast<float>(ring) / static_cast<float>(rings);
		const float latitude1 = -kPi * 0.5f +
			kPi * static_cast<float>(ring + 1) / static_cast<float>(rings);
		for (uint32_t segment = 0; segment < segments; ++segment) {
			const float longitude0 = 2.0f * kPi *
				static_cast<float>(segment) / static_cast<float>(segments);
			const float longitude1 = 2.0f * kPi *
				static_cast<float>(segment + 1) / static_cast<float>(segments);
			auto point = [&](float latitude, float longitude) {
				return Vector3{
					center.x + radius * std::cos(latitude) * std::cos(longitude),
					center.y + radius * std::sin(latitude),
					center.z + radius * std::cos(latitude) * std::sin(longitude)
				};
			};
			const Vector3 a = point(latitude0, longitude0);
			const Vector3 b = point(latitude0, longitude1);
			const Vector3 c = point(latitude1, longitude1);
			const Vector3 d = point(latitude1, longitude0);
			AddTriangle(a, b, c, color);
			AddTriangle(a, c, d, color);
		}
	}
}

void DebugRenderer::AddSolidOBB(
	const Vector3& center,
	const std::array<Vector3, 3>& axis,
	const Vector3& halfSize,
	const Vector4& color
) {
	if (
		halfSize.x <= 0.0f ||
		halfSize.y <= 0.0f ||
		halfSize.z <= 0.0f
	) {
		return;
	}
	const Vector3 extentX = Math::Multiply(axis[0], halfSize.x);
	const Vector3 extentY = Math::Multiply(axis[1], halfSize.y);
	const Vector3 extentZ = Math::Multiply(axis[2], halfSize.z);
	const auto makeCorner = [&](float x, float y, float z) {
		return Math::Add(
			Math::Add(center, Math::Multiply(extentX, x)),
			Math::Add(Math::Multiply(extentY, y), Math::Multiply(extentZ, z))
		);
	};
	const std::array<Vector3, 8> corners = {
		makeCorner(-1.0f, -1.0f, -1.0f), makeCorner(1.0f, -1.0f, -1.0f),
		makeCorner(1.0f, 1.0f, -1.0f), makeCorner(-1.0f, 1.0f, -1.0f),
		makeCorner(-1.0f, -1.0f, 1.0f), makeCorner(1.0f, -1.0f, 1.0f),
		makeCorner(1.0f, 1.0f, 1.0f), makeCorner(-1.0f, 1.0f, 1.0f)
	};
	constexpr std::array<std::array<uint32_t, 3>, 12> triangles = {{
		{{0, 2, 1}}, {{0, 3, 2}}, {{4, 5, 6}}, {{4, 6, 7}},
		{{0, 1, 5}}, {{0, 5, 4}}, {{1, 2, 6}}, {{1, 6, 5}},
		{{2, 3, 7}}, {{2, 7, 6}}, {{3, 0, 4}}, {{3, 4, 7}}
	}};
	for (const auto& triangle : triangles) {
		AddTriangle(
			corners[triangle[0]], corners[triangle[1]], corners[triangle[2]], color
		);
	}
}

void DebugRenderer::AddAxis(
	const Matrix4x4& worldMatrix,
	float length
) {
	if (length <= 0.0f) {
		return;
	}

	const Vector3 origin = GetTranslation(worldMatrix);
	const Vector3 xAxis = GetNormalizedAxis(worldMatrix, 0);
	const Vector3 yAxis = GetNormalizedAxis(worldMatrix, 1);
	const Vector3 zAxis = GetNormalizedAxis(worldMatrix, 2);

	AddLine(
		origin,
		Math::Add(origin, Math::Multiply(xAxis, length)),
		{ 1.0f, 0.15f, 0.15f, 1.0f }
	);
	AddLine(
		origin,
		Math::Add(origin, Math::Multiply(yAxis, length)),
		{ 0.15f, 1.0f, 0.2f, 1.0f }
	);
	AddLine(
		origin,
		Math::Add(origin, Math::Multiply(zAxis, length)),
		{ 0.2f, 0.45f, 1.0f, 1.0f }
	);
}

void DebugRenderer::Draw(const Camera* camera) {
	if (
		!isInitialized_ ||
		!camera ||
		vertices_.empty() &&
		depthTestedVertices_.empty() &&
		solidVertices_.empty()
	) {
		return;
	}

	const uint32_t vertexCount = static_cast<uint32_t>(
		(std::min)(
			vertices_.size(),
			static_cast<size_t>(maxVertexCount_)
		)
	);
	if (vertexCount > 0) {
		std::memcpy(mappedVertices_, vertices_.data(), sizeof(Vertex) * vertexCount);
	}
	const uint32_t depthTestedVertexCount = static_cast<uint32_t>(
		(std::min)(
			depthTestedVertices_.size(),
			static_cast<size_t>(maxVertexCount_)
		)
	);
	if (depthTestedVertexCount > 0) {
		std::memcpy(
			mappedDepthTestedVertices_,
			depthTestedVertices_.data(),
			sizeof(Vertex) * depthTestedVertexCount
		);
	}
	const uint32_t solidVertexCount = static_cast<uint32_t>(
		(std::min)(solidVertices_.size(), static_cast<size_t>(maxVertexCount_))
	);
	if (solidVertexCount > 0) {
		std::memcpy(
			mappedSolidVertices_, solidVertices_.data(), sizeof(Vertex) * solidVertexCount
		);
	}
	cameraData_->viewProjection = camera->GetViewProjectionMatrix();

	ID3D12GraphicsCommandList* commandList = dxCommon_->GetCommandList();
	commandList->SetGraphicsRootSignature(rootSignature_.Get());
	commandList->SetGraphicsRootConstantBufferView(
		0,
		cameraResource_->GetGPUVirtualAddress()
	);
	if (depthTestedVertexCount > 0) {
		commandList->SetPipelineState(depthTestedPipelineState_.Get());
		commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_LINELIST);
		commandList->IASetVertexBuffers(
			0,
			1,
			&depthTestedVertexBufferView_
		);
		commandList->DrawInstanced(depthTestedVertexCount, 1, 0, 0);
	}
	if (solidVertexCount > 0) {
		commandList->SetPipelineState(solidPipelineState_.Get());
		commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		commandList->IASetVertexBuffers(0, 1, &solidVertexBufferView_);
		commandList->DrawInstanced(solidVertexCount, 1, 0, 0);
	}
	if (vertexCount > 0) {
		commandList->SetPipelineState(pipelineState_.Get());
		commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_LINELIST);
		commandList->IASetVertexBuffers(0, 1, &vertexBufferView_);
		commandList->DrawInstanced(vertexCount, 1, 0, 0);
	}
}

void DebugRenderer::CreateRootSignature() {
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
			Logger::Log(
				static_cast<const char*>(errorBlob->GetBufferPointer())
			);
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

void DebugRenderer::CreateGraphicsPipeline() {
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
		L"resources/shaders/DebugLine.VS.hlsl",
		L"vs_6_0"
	);
	const auto pixelShader = dxCommon_->CompileShader(
		L"resources/shaders/DebugLine.PS.hlsl",
		L"ps_6_0"
	);
	assert(vertexShader);
	assert(pixelShader);

	D3D12_BLEND_DESC blendDesc{};
	blendDesc.RenderTarget[0].RenderTargetWriteMask =
		D3D12_COLOR_WRITE_ENABLE_ALL;
	blendDesc.RenderTarget[0].BlendEnable = TRUE;
	blendDesc.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
	blendDesc.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
	blendDesc.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
	blendDesc.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
	blendDesc.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ZERO;
	blendDesc.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;

	D3D12_RASTERIZER_DESC rasterizerDesc{};
	rasterizerDesc.FillMode = D3D12_FILL_MODE_SOLID;
	rasterizerDesc.CullMode = D3D12_CULL_MODE_NONE;
	rasterizerDesc.DepthClipEnable = TRUE;

	D3D12_DEPTH_STENCIL_DESC depthStencilDesc{};
	depthStencilDesc.DepthEnable = FALSE;
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
		D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE;
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

	pipelineDesc.PrimitiveTopologyType =
		D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	const HRESULT solidResult =
		dxCommon_->GetDevice()->CreateGraphicsPipelineState(
			&pipelineDesc,
			IID_PPV_ARGS(&solidPipelineState_)
		);
	assert(SUCCEEDED(solidResult));

	// GridなどWorld内に存在する補助線だけがDepth Testを使用する。
	// 既存のCollider／Skeleton用Pipelineは常時前面表示のまま維持する。
	depthStencilDesc.DepthEnable = TRUE;
	pipelineDesc.DepthStencilState = depthStencilDesc;
	pipelineDesc.PrimitiveTopologyType =
		D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE;
	const HRESULT depthTestedResult =
		dxCommon_->GetDevice()->CreateGraphicsPipelineState(
			&pipelineDesc,
			IID_PPV_ARGS(&depthTestedPipelineState_)
		);
	assert(SUCCEEDED(depthTestedResult));
}

void DebugRenderer::CreateResources() {
	vertexResource_ = dxCommon_->CreateBufferResource(
		sizeof(Vertex) * maxVertexCount_
	);
	vertexResource_->Map(
		0,
		nullptr,
		reinterpret_cast<void**>(&mappedVertices_)
	);

	vertexBufferView_.BufferLocation =
		vertexResource_->GetGPUVirtualAddress();
	vertexBufferView_.SizeInBytes =
		static_cast<UINT>(sizeof(Vertex) * maxVertexCount_);
	vertexBufferView_.StrideInBytes = sizeof(Vertex);

	depthTestedVertexResource_ = dxCommon_->CreateBufferResource(
		sizeof(Vertex) * maxVertexCount_
	);
	depthTestedVertexResource_->Map(
		0,
		nullptr,
		reinterpret_cast<void**>(&mappedDepthTestedVertices_)
	);
	depthTestedVertexBufferView_.BufferLocation =
		depthTestedVertexResource_->GetGPUVirtualAddress();
	depthTestedVertexBufferView_.SizeInBytes =
		static_cast<UINT>(sizeof(Vertex) * maxVertexCount_);
	depthTestedVertexBufferView_.StrideInBytes = sizeof(Vertex);

	solidVertexResource_ = dxCommon_->CreateBufferResource(
		sizeof(Vertex) * maxVertexCount_
	);
	solidVertexResource_->Map(
		0,
		nullptr,
		reinterpret_cast<void**>(&mappedSolidVertices_)
	);
	solidVertexBufferView_.BufferLocation =
		solidVertexResource_->GetGPUVirtualAddress();
	solidVertexBufferView_.SizeInBytes =
		static_cast<UINT>(sizeof(Vertex) * maxVertexCount_);
	solidVertexBufferView_.StrideInBytes = sizeof(Vertex);

	cameraResource_ = dxCommon_->CreateBufferResource(sizeof(CameraData));
	cameraResource_->Map(
		0,
		nullptr,
		reinterpret_cast<void**>(&cameraData_)
	);
	cameraData_->viewProjection = MakeIdentity4x4();
}
