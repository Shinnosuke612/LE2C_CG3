#include "Object3dCommon.h"
#include "../base/DirectXCommon.h"
#include "../utility/Logger.h"
#include <cassert>

Object3dCommon* Object3dCommon::GetInstance() {
	static Object3dCommon instance;
	return &instance;
}

void Object3dCommon::Initialize(DirectXCommon* dxCommon) {
	assert(dxCommon);

	// 同じ DirectXCommon で初期化済みなら何もしない
	if (dxCommon_ == dxCommon && graphicsPipelineState_) {
		return;
	}

	dxCommon_ = dxCommon;

	CreateLightingResource();

	GenerateGraphicsPipeline();
}

void Object3dCommon::SetCommonRenderState() {
	dxCommon_->GetCommandList()->SetGraphicsRootSignature(rootSignature_.Get());
	dxCommon_->GetCommandList()->SetPipelineState(graphicsPipelineState_.Get());
	dxCommon_->GetCommandList()->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	// Object3D用PixelShaderの LightingCB : register(b1)
	dxCommon_->GetCommandList()->SetGraphicsRootConstantBufferView(
		3,
		lightingResource_->GetGPUVirtualAddress()
	);
}

void Object3dCommon::SetLightColor(const Vector4& color) {
	lightingData_->directionalLight.color = color;
}

void Object3dCommon::SetLightDirection(const Vector3& direction) {
	lightingData_->directionalLight.direction = direction;
}

void Object3dCommon::SetLightIntensity(float intensity) {
	lightingData_->directionalLight.intensity = intensity;
}

void Object3dCommon::SetDirectionalLight(const DirectionalLight& light) {
	lightingData_->directionalLight = light;
}

void Object3dCommon::SetPointLight(const PointLight& light) {
	lightingData_->pointLight = light;
}

void Object3dCommon::SetSpotLight(const SpotLight& light) {
	lightingData_->spotLight = light;
}

Object3dCommon::DirectionalLight* Object3dCommon::GetDirectionalLight() {
	return &lightingData_->directionalLight;
}

Object3dCommon::PointLight* Object3dCommon::GetPointLight() {
	return &lightingData_->pointLight;
}

Object3dCommon::SpotLight* Object3dCommon::GetSpotLight() {
	return &lightingData_->spotLight;
}

void Object3dCommon::MakeRootSignature(){
	HRESULT hr;
	D3D12_DESCRIPTOR_RANGE descriptorRange[1] = {};
	descriptorRange[0].BaseShaderRegister = 0;
	descriptorRange[0].NumDescriptors = 1;
	descriptorRange[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	descriptorRange[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

	// RootSignature作成
	D3D12_ROOT_SIGNATURE_DESC descriptionRootSignature{};
	descriptionRootSignature.Flags =
		D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;


	//RootParameter作成、複数指定できるので配列。
	D3D12_ROOT_PARAMETER rootParameters[5] = {};
	rootParameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
	rootParameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	rootParameters[0].Descriptor.ShaderRegister = 0;
	rootParameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
	rootParameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
	rootParameters[1].Descriptor.ShaderRegister = 0;
	rootParameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	rootParameters[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	rootParameters[2].DescriptorTable.pDescriptorRanges = descriptorRange;
	rootParameters[2].DescriptorTable.NumDescriptorRanges = _countof(descriptorRange);
	rootParameters[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
	rootParameters[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	rootParameters[3].Descriptor.ShaderRegister = 1;
	rootParameters[4].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
	rootParameters[4].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	rootParameters[4].Descriptor.ShaderRegister = 2;

	descriptionRootSignature.pParameters = rootParameters;
	descriptionRootSignature.NumParameters = _countof(rootParameters);
	D3D12_STATIC_SAMPLER_DESC staticSampler = MakeStaticSamplerS0();
	descriptionRootSignature.pStaticSamplers = &staticSampler;   // ★追加
	descriptionRootSignature.NumStaticSamplers = 1;                // ★追加

	// シリアライズしてバイナリにする
	ID3DBlob* signatureBlob = nullptr;
	ID3DBlob* errorBlob = nullptr;
	hr = D3D12SerializeRootSignature(&descriptionRootSignature,
									 D3D_ROOT_SIGNATURE_VERSION_1, &signatureBlob, &errorBlob);
	if(FAILED(hr)){
		Logger::Log(reinterpret_cast<char*>(errorBlob->GetBufferPointer()));
		assert(false);
	}

	// バイナリを元に生成

	hr = dxCommon_->GetDevice()->CreateRootSignature(0,
													 signatureBlob->GetBufferPointer(), signatureBlob->GetBufferSize(),
													 IID_PPV_ARGS(&rootSignature_));
	assert(SUCCEEDED(hr));
}

void Object3dCommon::GenerateGraphicsPipeline(){
	MakeRootSignature();
	HRESULT hr;
	//DepthStencilStateの設定
	D3D12_DEPTH_STENCIL_DESC depthStencilDesc{};
	//Depth機能を有効化する
	depthStencilDesc.DepthEnable = true;
	//書き込みします
	depthStencilDesc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
	//比較関数はLessEqual。つまり、近ければ描画される
	depthStencilDesc.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;

	//InputLayout
	D3D12_INPUT_ELEMENT_DESC inputElementDescs[3] = {};
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
	D3D12_INPUT_LAYOUT_DESC inputLayoutDesc = {};
	inputLayoutDesc.pInputElementDescs = inputElementDescs;
	inputLayoutDesc.NumElements = _countof(inputElementDescs);

	BlendMode currentBlendMode = BlendMode::kBlendModeNormal;

	// BlendStateの設定
	D3D12_BLEND_DESC blendDesc{};
	// すべての色要素を書き込む
	blendDesc.RenderTarget[0].RenderTargetWriteMask =
		D3D12_COLOR_WRITE_ENABLE_ALL;
	blendDesc.RenderTarget[0].BlendEnable = TRUE;
	if(currentBlendMode == BlendMode::kBlendModeNormal){
		blendDesc.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
		blendDesc.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
		blendDesc.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
	} else if(currentBlendMode == BlendMode::kBlendModeAdd){
		blendDesc.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
		blendDesc.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
		blendDesc.RenderTarget[0].DestBlend = D3D12_BLEND_ONE;
	} else if(currentBlendMode == BlendMode::kBlendModeSubtract){
		blendDesc.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
		blendDesc.RenderTarget[0].BlendOp = D3D12_BLEND_OP_REV_SUBTRACT;
		blendDesc.RenderTarget[0].DestBlend = D3D12_BLEND_ONE;
	} else if(currentBlendMode == BlendMode::kBlendModeMultiply){
		blendDesc.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
		blendDesc.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
		blendDesc.RenderTarget[0].DestBlend = D3D12_BLEND_SRC_COLOR;
	}

	else if(currentBlendMode == BlendMode::kBlendModeScreen){
		blendDesc.RenderTarget[0].SrcBlend = D3D12_BLEND_INV_DEST_COLOR;
		blendDesc.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
		blendDesc.RenderTarget[0].DestBlend = D3D12_BLEND_ONE;
	}
	blendDesc.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
	blendDesc.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
	blendDesc.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ZERO;

	// RasterizerStateの設定
	D3D12_RASTERIZER_DESC rasterizerDesc{};
	// 裏面（時計回り）を表示しない
	rasterizerDesc.CullMode = D3D12_CULL_MODE_BACK;
	// 三角形の中を塗りつぶす
	rasterizerDesc.FillMode = D3D12_FILL_MODE_SOLID;

	rasterizerDesc.DepthClipEnable = TRUE;

	// Shaderをコンパイルする
	Microsoft::WRL::ComPtr <IDxcBlob> vertexShaderBlob = dxCommon_->CompileShader(L"resources/shaders/Object3D.VS.hlsl",
																				  L"vs_6_0");
	assert(vertexShaderBlob != nullptr);

	Microsoft::WRL::ComPtr <IDxcBlob> pixelShaderBlob = dxCommon_->CompileShader(L"resources/shaders/Object3D.PS.hlsl",
																				 L"ps_6_0");
	assert(pixelShaderBlob != nullptr);

	D3D12_GRAPHICS_PIPELINE_STATE_DESC graphicsPipelineStateDesc{};
	graphicsPipelineStateDesc.pRootSignature = rootSignature_.Get(); // RootSignature
	graphicsPipelineStateDesc.InputLayout = inputLayoutDesc;  // InputLayout
	graphicsPipelineStateDesc.VS = { vertexShaderBlob->GetBufferPointer(),
									 vertexShaderBlob->GetBufferSize() }; // VertexShader
	graphicsPipelineStateDesc.PS = { pixelShaderBlob->GetBufferPointer(),
									 pixelShaderBlob->GetBufferSize() };  // PixelShader
	graphicsPipelineStateDesc.BlendState = blendDesc;         // BlendState
	graphicsPipelineStateDesc.RasterizerState = rasterizerDesc; // RasterizerState

	// 書き込むRTVの情報
	graphicsPipelineStateDesc.NumRenderTargets = 1;
	graphicsPipelineStateDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;

	// 利用するトポロジ（形状）のタイプ。三角形
	graphicsPipelineStateDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;

	//どのように画面に色を打ち込むかの設定（気にしなくていい）
	graphicsPipelineStateDesc.SampleDesc.Count = 1;
	graphicsPipelineStateDesc.SampleMask = D3D12_DEFAULT_SAMPLE_MASK;

	//DepthStencilの設定
	graphicsPipelineStateDesc.DepthStencilState = depthStencilDesc;
	graphicsPipelineStateDesc.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;

	//実際に生成

	hr = dxCommon_->GetDevice()->CreateGraphicsPipelineState(&graphicsPipelineStateDesc,
															 IID_PPV_ARGS(&graphicsPipelineState_));
	assert(SUCCEEDED(hr));

}

inline D3D12_STATIC_SAMPLER_DESC Object3dCommon::MakeStaticSamplerS0(){
	D3D12_STATIC_SAMPLER_DESC s{};
	s.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
	s.AddressU = s.AddressV = s.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	s.ShaderRegister = 0; // s0
	s.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	return s;
}

void Object3dCommon::CreateLightingResource() {
	lightingResource_ = *&dxCommon_->CreateBufferResource(sizeof(LightingForGPU));

	lightingResource_->Map(
		0,
		nullptr,
		reinterpret_cast<void**>(&lightingData_)
	);

	// DirectionalLight
	lightingData_->directionalLight.color = { 1.0f, 1.0f, 1.0f, 1.0f };
	lightingData_->directionalLight.direction = { 0.0f, -1.0f, 0.0f };
	lightingData_->directionalLight.intensity = 1.0f;
	lightingData_->directionalLight.enable = true;
	lightingData_->directionalLight.padding[0] = 0.0f;
	lightingData_->directionalLight.padding[1] = 0.0f;
	lightingData_->directionalLight.padding[2] = 0.0f;

	// PointLight
	lightingData_->pointLight.color = { 1.0f, 1.0f, 1.0f, 1.0f };
	lightingData_->pointLight.position = { 0.0f, 2.0f, -3.0f };
	lightingData_->pointLight.intensity = 1.0f;
	lightingData_->pointLight.radius = 5.0f;
	lightingData_->pointLight.decay = 1.0f;
	lightingData_->pointLight.enable = false;
	lightingData_->pointLight.padding = 0.0f;

	// SpotLight
	lightingData_->spotLight.color = { 1.0f, 1.0f, 1.0f, 1.0f };
	lightingData_->spotLight.position = { 0.0f, 4.0f, -5.0f };
	lightingData_->spotLight.intensity = 1.0f;
	lightingData_->spotLight.direction = { 0.0f, -1.0f, 1.0f };
	lightingData_->spotLight.distance = 10.0f;
	lightingData_->spotLight.decay = 1.0f;

	// 外側の角度。cos(45度)相当
	lightingData_->spotLight.cosAngle = 0.70710678f;

	// 内側の角度。cos(30度)相当
	lightingData_->spotLight.cosFalloffStart = 0.86602540f;

	lightingData_->spotLight.enable = false;
}
