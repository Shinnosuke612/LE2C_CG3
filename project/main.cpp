
#define _USE_MATH_DEFINES
#include <cmath>
#include <iostream>
#include<filesystem>
//ファイルを読み書きするためのライブラリ
#include <fstream>
#include <sstream>
//時間を扱うライブラリ
#include <chrono>
//文字列を扱うライブラリ
#include <string>
//
#include <format>
#include <cassert>

#include <vector>
//入力
#define DIRECTINPUT_VERSION 0x0800
#include <dinput.h>

#pragma comment(lib,"dinput8.lib")
#pragma comment(lib,"dxguid.lib")

#include "Vector2.h"
#include "Vector3.h"
#include "Vector4.h"
#include "Matrix4x4.h"
#include "Matrix3x3.h"
#include "Transform.h"
#include "Math.h"
#include "Input.h"
#include "WinApp.h"
#include "DirectXCommon.h"
#include "D3DResourceLeadChecker.h"
#include "SpriteCommon.h"
#include "Sprite.h"
#include "TextureManager.h"
#include "Object3dCommon.h"
#include "Object3d.h"

//デバッグ用のあれこれを使えるようにする
#include <dbghelp.h>
#pragma comment(lib,"Dbghelp.lib")
#include <strsafe.h>

#include <dxgidebug.h>
#pragma comment(lib, "dxguid.lib")
using namespace Logger;

enum class BlendMode{
	//!< ブレンドなし
	kBlendModeNone,
	//!< 通常アルファブレンド
	kBlendModeNormal,
	//!< 加算
	kBlendModeAdd,
	//!< 減算
	kBlendModeSubtract,
	//!< 乗算
	kBlendModeMultiply,
	//!< スクリーン
	kBlendModeScreen,
	//利用してはいけない
	kCountOfBlendMode,
};

struct VertexData {
	Vector4 position;
	Vector2 texcoord;
	Vector3 normal;
};

struct Material {
	Vector4 color;
	int32_t enableLighting;
	float padding[3];
	Matrix4x4 uvTransform;
};

struct TransformationMatrix {
	Matrix4x4 WVP;
	Matrix4x4 World;
};

struct DirectionalLight {
	Vector4 color; //!< ライトの色
	Vector3 direction; //!< ライトの向き
	float intensity; //!< 輝度
};

struct ModelData{
	std::vector<VertexData> vertices;
};

struct MatrialData{
	std::string textureFilePath;
};

void DrawImGui(BlendMode &currentBlendMode){
	const char* blendModeNames[] = {
		"None", "Normal", "Add", "Subtract", "Multiply", "Screen"
	};

	int current = static_cast<int>(currentBlendMode);
	if(ImGui::Combo("Blend Mode", &current, blendModeNames, IM_ARRAYSIZE(blendModeNames))){
		// 選択変更されたとき
		currentBlendMode = static_cast<BlendMode>(current);
	}
}

ModelData LoadObjFile(const std::string& directoryPath, const std::string& filename){
	ModelData modelData; //構築するModelData
	std::vector<Vector4> positions;//位置
	std::vector<Vector3> normals;//法線
	std::vector<Vector2> texcoords;//テクスチャ座標
	std::string line;//ファイルから読んだ１行を格納するもの

	std::ifstream file(directoryPath + "/" + filename);
	assert(file.is_open());

	while(std::getline(file, line)){
		std::string identifier;
		std::istringstream s(line);
		s >> identifier;

		if(identifier == "v"){
			Vector4 position;
			s >> position.x >> position.y >> position.z;
			position.x *= -1;
			position.w = 1.0f;
			positions.push_back(position);
		} else if(identifier == "vt"){
			Vector2 texcoord;
			s >> texcoord.x >> texcoord.y;
			texcoord.y = 1.0f - texcoord.y;
			texcoords.push_back(texcoord);
		} else if(identifier == "vn"){
			Vector3 normal;
			s >> normal.x >> normal.y >> normal.z;
			normal.x *= -1;
			normals.push_back(normal);
		} else if(identifier == "f"){
			VertexData triangle[3];
			//面は三角限定。その他は未対応
			for(int32_t faceVertex = 0; faceVertex < 3; ++faceVertex){
				std::string vertexDefinition;
				s >> vertexDefinition;
				//頂点の要素へのIndexは「位置/UV/法線」で格納されているので、分解してIndexを取得する
				std::istringstream v(vertexDefinition);
				uint32_t elementIndices[3];
				for(int32_t element = 0; element < 3; ++element){
					std::string index;
					std::getline(v, index, '/');
					elementIndices[element] = std::stoi(index);
				}
				//要素へのIndexから、実際の要素の値を取得して頂点を構築する
				Vector4 position = positions[elementIndices[0] - 1];
				Vector2 texcoord = texcoords[elementIndices[1] - 1];
				Vector3 normal = normals[elementIndices[2] - 1];
				VertexData vertex = { position, texcoord,normal };
				modelData.vertices.push_back(vertex);
				triangle[faceVertex] = { position,texcoord,normal };
			}
			modelData.vertices.push_back(triangle[2]);
			modelData.vertices.push_back(triangle[1]);
			modelData.vertices.push_back(triangle[0]);
		}

	}
	return modelData;
}

// どこか共通ヘルパに
inline D3D12_STATIC_SAMPLER_DESC MakeStaticSamplerS0(){
	D3D12_STATIC_SAMPLER_DESC s{};
	s.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
	s.AddressU = s.AddressV = s.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	s.ShaderRegister = 0; // s0
	s.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	return s;
}

//static LONG WINAPI ExportDump(EXCEPTION_POINTERS* exception) {
//	// 時刻を取得して、時刻を名前に入れたファイルを作成し、Dumpsディレクトリ以下に出力
//	SYSTEMTIME time;
//	GetLocalTime(&time);
//	wchar_t filePath[MAX_PATH] = { 0 };
//	CreateDirectory(L"./Dumps", nullptr);
//	StringCchPrintfW(filePath, MAX_PATH, L"./Dumps/%04d-%02d-%02d-%02d%02d.dmp", time.wYear, time.wMonth, time.wDay, time.wHour, time.wMinute);
//	HANDLE dumpFileHandle = CreateFile(filePath, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_WRITE | FILE_SHARE_READ, 0, CREATE_ALWAYS, 0, 0);
//
//	// processId（CのexeのID）とスレッド（例外）の発生したthreadIdを取得
//	DWORD processId = GetCurrentProcessId();
//	DWORD threadId = GetCurrentThreadId();
//
//	// 設定情報を入力
//	MINIDUMP_EXCEPTION_INFORMATION minidumpInformation{ };
//	minidumpInformation.ThreadId = threadId;
//	minidumpInformation.ExceptionPointers = exception;
//	minidumpInformation.ClientPointers = TRUE;
//
//	// Dmpを出力。MiniDumpNormalは最低限の情報を書き出すフラグ
//	MiniDumpWriteDump(GetCurrentProcess(), processId, dumpFileHandle, MiniDumpNormal, &minidumpInformation, nullptr, nullptr);
//
//	// 例外に紐づけられているSEH例外ハンドラに任せる。通常はエクスポートされてる
//	return EXCEPTION_EXECUTE_HANDLER;
//
//}

//Windowsアプリでのエントリーポイント(main開放)
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
	D3DResourceLeadChecker* checker = new D3DResourceLeadChecker();
	WinApp* winApp = nullptr;
	winApp = new WinApp();
	winApp->Initialize();

	//SetUnhandledExceptionFilter(ExportDump);

#pragma region Log
	std::filesystem::create_directory("Logs");

	// 現在時刻を取得（UTC時刻）
	std::chrono::system_clock::time_point now = std::chrono::system_clock::now();
	// ログファイルの名前にコンマ何秒はいらないので、削って秒にする
	std::chrono::time_point<std::chrono::system_clock, std::chrono::seconds>
		nowSeconds = std::chrono::time_point_cast<std::chrono::seconds>(now);

	// 日本時間（PCの設定時間）に変換
	std::chrono::zoned_time localTime{ std::chrono::current_zone(), nowSeconds };

	// formatを使って年月日_時分秒の文字列に変換
	std::string dateString = std::format("{:%Y%m%d_%H%M%S}", localTime);

	// 文字列を使ってファイル名を決定
	std::string logFilePath = std::string("logs/") + dateString + ".log";

	// ファイルを作って書き込み準備
	std::ofstream logStream(logFilePath);

#pragma endregion

	DirectXCommon* dxCommon = nullptr;
	dxCommon = new DirectXCommon();
	dxCommon->Initialize(winApp);

	TextureManager::GetInstance()->Initialize(dxCommon);
	TextureManager::GetInstance()->LoadTexture("resources/monsterBall.png");
	TextureManager::GetInstance()->LoadTexture("resources/uvChecker.png");
	HRESULT hr;

	// ウィンドウを表示する
	ShowWindow(winApp->GetHwnd(), SW_SHOW);

	//DepthStencilStateの設定
	D3D12_DEPTH_STENCIL_DESC depthStencilDesc{};
	//Depth機能を有効化する
	depthStencilDesc.DepthEnable = true;
	//書き込みします
	depthStencilDesc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
	//比較関数はLessEqual。つまり、近ければ描画される
	depthStencilDesc.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;



	Input* input = nullptr;
	input = new Input();
	input->Initialize(winApp);

	

	SpriteCommon* spriteCommon = nullptr;
	//スプライト共通部の初期化
	spriteCommon = new SpriteCommon;
	spriteCommon->Initialize(dxCommon);

	Object3dCommon* object3dCommon = nullptr;
	//3Dオブジェクト共通部の初期化
	object3dCommon = new Object3dCommon;
	object3dCommon->Initialize(dxCommon);

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
	D3D12_ROOT_PARAMETER rootParameters[4] = {};
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
	if (FAILED(hr)) {
		Log(reinterpret_cast<char*>(errorBlob->GetBufferPointer()));
		assert(false);
	}

	// バイナリを元に生成
	ID3D12RootSignature* rootSignature = nullptr;
	hr = dxCommon->GetDevice()->CreateRootSignature(0,
		signatureBlob->GetBufferPointer(), signatureBlob->GetBufferSize(),
		IID_PPV_ARGS(&rootSignature));
	assert(SUCCEEDED(hr));

	//ParticleShader用の設定
	D3D12_DESCRIPTOR_RANGE descriptorRangeForInstancing[1] = {};
	descriptorRangeForInstancing[0].BaseShaderRegister = 0;//0から始まる
	descriptorRangeForInstancing[0].NumDescriptors = 1;//数は1つ
	descriptorRangeForInstancing[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;//SRVを使う
	descriptorRangeForInstancing[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

	// RootSignature作成
	D3D12_ROOT_SIGNATURE_DESC descriptionRootSignatureForInstancing{};
	descriptionRootSignatureForInstancing.Flags =
		D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

	//RootParameter作成、複数指定できるので配列。
	D3D12_ROOT_PARAMETER rootParametersForInstancing[4] = {};
	rootParametersForInstancing[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
	rootParametersForInstancing[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	rootParametersForInstancing[0].Descriptor.ShaderRegister = 0;
	rootParametersForInstancing[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;//DescriptorTableを使う
	rootParametersForInstancing[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;//VertexShaderで使う
	rootParametersForInstancing[1].DescriptorTable.pDescriptorRanges = descriptorRangeForInstancing; //Tableの中身の配列を指定
	rootParametersForInstancing[1].DescriptorTable.NumDescriptorRanges = _countof(descriptorRangeForInstancing); //Tableで利用する数
	rootParametersForInstancing[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	rootParametersForInstancing[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	rootParametersForInstancing[2].DescriptorTable.pDescriptorRanges = descriptorRangeForInstancing;
	rootParametersForInstancing[2].DescriptorTable.NumDescriptorRanges = _countof(descriptorRangeForInstancing);
	rootParametersForInstancing[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
	rootParametersForInstancing[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	rootParametersForInstancing[3].Descriptor.ShaderRegister = 1;

	descriptionRootSignatureForInstancing.pParameters = rootParametersForInstancing;
	descriptionRootSignatureForInstancing.NumParameters = _countof(rootParametersForInstancing);
	D3D12_STATIC_SAMPLER_DESC staticSamplerP = MakeStaticSamplerS0();
	descriptionRootSignatureForInstancing.pStaticSamplers = &staticSamplerP; // ★追加
	descriptionRootSignatureForInstancing.NumStaticSamplers = 1;               // ★追加

	// シリアライズしてバイナリにする
	ID3DBlob* signatureBlobForInstancing = nullptr;
	ID3DBlob* errorBlobForInstancing = nullptr;
	hr = D3D12SerializeRootSignature(&descriptionRootSignatureForInstancing,
									 D3D_ROOT_SIGNATURE_VERSION_1, &signatureBlobForInstancing, &errorBlobForInstancing);
	if(FAILED(hr)){
		Log(reinterpret_cast<char*>(errorBlobForInstancing->GetBufferPointer()));
		assert(false);
	}

	// バイナリを元に生成
	ID3D12RootSignature* rootSignatureForInstancing = nullptr;
	hr = dxCommon->GetDevice()->CreateRootSignature(0,
									 signatureBlobForInstancing->GetBufferPointer(), signatureBlobForInstancing->GetBufferSize(),
									 IID_PPV_ARGS(&rootSignatureForInstancing));
	assert(SUCCEEDED(hr));


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
	}
	else if(currentBlendMode == BlendMode::kBlendModeAdd){
		blendDesc.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
		blendDesc.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
		blendDesc.RenderTarget[0].DestBlend = D3D12_BLEND_ONE;
	}
	else if(currentBlendMode == BlendMode::kBlendModeSubtract){
		blendDesc.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
		blendDesc.RenderTarget[0].BlendOp = D3D12_BLEND_OP_REV_SUBTRACT;
		blendDesc.RenderTarget[0].DestBlend = D3D12_BLEND_ONE;
	} 
	else if(currentBlendMode == BlendMode::kBlendModeMultiply){
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
	rasterizerDesc.CullMode = D3D12_CULL_MODE_NONE;
	// 三角形の中を塗りつぶす
	rasterizerDesc.FillMode = D3D12_FILL_MODE_SOLID;

	// Shaderをコンパイルする
	Microsoft::WRL::ComPtr <IDxcBlob> vertexShaderBlob = dxCommon->CompileShader(L"resources/shaders/Object3D.VS.hlsl",
		L"vs_6_0");
	assert(vertexShaderBlob != nullptr);

	Microsoft::WRL::ComPtr <IDxcBlob> pixelShaderBlob = dxCommon->CompileShader(L"resources/shaders/Object3D.PS.hlsl",
		L"ps_6_0");
	assert(pixelShaderBlob != nullptr);

	D3D12_GRAPHICS_PIPELINE_STATE_DESC graphicsPipelineStateDesc{};
	graphicsPipelineStateDesc.pRootSignature = rootSignature; // RootSignature
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
	ID3D12PipelineState* graphicsPipelineState = nullptr;
	hr = dxCommon->GetDevice()->CreateGraphicsPipelineState(&graphicsPipelineStateDesc,
		IID_PPV_ARGS(&graphicsPipelineState));
	assert(SUCCEEDED(hr));

	//パーティクル用PSO
		// Shaderをコンパイルする
	Microsoft::WRL::ComPtr <IDxcBlob> vertexShaderBlobForInstancing = dxCommon->CompileShader(L"resources/shaders/Particle.VS.hlsl",
											   L"vs_6_0");
	assert(vertexShaderBlobForInstancing != nullptr);

	Microsoft::WRL::ComPtr <IDxcBlob> pixelShaderBlobForInstancing = dxCommon->CompileShader(L"resources/shaders/Particle.PS.hlsl",
											  L"ps_6_0");
	assert(pixelShaderBlobForInstancing != nullptr);

	D3D12_GRAPHICS_PIPELINE_STATE_DESC graphicsPipelineStateDescForInstancing{};
	graphicsPipelineStateDescForInstancing.pRootSignature = rootSignatureForInstancing; // RootSignature
	graphicsPipelineStateDescForInstancing.InputLayout = inputLayoutDesc;  // InputLayout
	graphicsPipelineStateDescForInstancing.VS = { vertexShaderBlobForInstancing->GetBufferPointer(),
									 vertexShaderBlobForInstancing->GetBufferSize() }; // VertexShader
	graphicsPipelineStateDescForInstancing.PS = { pixelShaderBlobForInstancing->GetBufferPointer(),
									 pixelShaderBlobForInstancing->GetBufferSize() };  // PixelShader
	graphicsPipelineStateDescForInstancing.BlendState = blendDesc;         // BlendState
	graphicsPipelineStateDescForInstancing.RasterizerState = rasterizerDesc; // RasterizerState

	// 書き込むRTVの情報
	graphicsPipelineStateDescForInstancing.NumRenderTargets = 1;
	graphicsPipelineStateDescForInstancing.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;

	// 利用するトポロジ（形状）のタイプ。三角形
	graphicsPipelineStateDescForInstancing.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;

	//どのように画面に色を打ち込むかの設定（気にしなくていい）
	graphicsPipelineStateDescForInstancing.SampleDesc.Count = 1;
	graphicsPipelineStateDescForInstancing.SampleMask = D3D12_DEFAULT_SAMPLE_MASK;

	//DepthStencilの設定
	graphicsPipelineStateDescForInstancing.DepthStencilState = depthStencilDesc;
	graphicsPipelineStateDescForInstancing.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;

	//実際に生成
	ID3D12PipelineState* graphicsPipelineStateForInstancing = nullptr;
	hr = dxCommon->GetDevice()->CreateGraphicsPipelineState(&graphicsPipelineStateDescForInstancing,
											 IID_PPV_ARGS(&graphicsPipelineStateForInstancing));
	assert(SUCCEEDED(hr));

	////Textureを読んで転送する
	//DirectX::ScratchImage mipImages = dxCommon->LoadTexture("resources/uvChecker.png");
	//const DirectX::TexMetadata& metadata = mipImages.GetMetadata();
	//ID3D12Resource* textureResource = *&dxCommon->CreateTextureResource(metadata);
	//ID3D12Resource* intermediaResource = dxCommon->UploadTextureData(textureResource, mipImages);

	////二枚目のTextureを読んで転送する
	//DirectX::ScratchImage mipImages2 = dxCommon->LoadTexture("resources/fence.png");
	//const DirectX::TexMetadata& metadata2 = mipImages2.GetMetadata();
	//ID3D12Resource* textureResource2 = *&dxCommon->CreateTextureResource(metadata2);
	//ID3D12Resource* intermediaResource2 = dxCommon->UploadTextureData(textureResource2, mipImages2);

	//マテリアル用のリソースを作る。今回はカラー1つ分のサイズを用意する
	ID3D12Resource* materialResource = *&dxCommon->CreateBufferResource(sizeof(Material));
	//マテリアルデータを書き込む
	Material* materialData = nullptr;
	//書き込むためのアドレスを取得
	materialResource->Map(0, nullptr, reinterpret_cast<void**>(&materialData));
	//今回は赤を書き込んでる
	materialData->color = Vector4(1.0f, 1.0f, 1.0f, 1.0f);
	//ライティングの有無
	materialData->enableLighting = true;
	//uvTransform行列を単位行列で初期化
	materialData->uvTransform = MakeIdentity4x4();


	//Sprite用のマテリアルリソースを作る
	ID3D12Resource* materialResourceSprite = *&dxCommon->CreateBufferResource(sizeof(Material));
	//マテリアルデータを書き込む
	Material* materialDataSprite = nullptr;
	//書き込むためのアドレスを取得
	materialResourceSprite->Map(0, nullptr, reinterpret_cast<void**>(&materialDataSprite));
	//今回は赤を書き込んでる
	materialDataSprite->color = Vector4(1.0f, 1.0f, 1.0f, 1.0f);
	//ライティングの有無
	materialDataSprite->enableLighting = false;
	//uvTransform行列を単位行列で初期化
	materialDataSprite->uvTransform = MakeIdentity4x4();

	//平行光源のリソースを作る
	ID3D12Resource* directionalLightResource = *&dxCommon->CreateBufferResource(sizeof(DirectionalLight));
	//データを書き込む
	DirectionalLight* directionalLightData = nullptr;
	//書き込むためのアドレス取得
	directionalLightResource->Map(0, nullptr, reinterpret_cast<void**>(&directionalLightData));
	//平行光源の設定
	directionalLightData->color = { 1.0f,1.0f,1.0f,1.0f };
	directionalLightData->direction = { 0.0f,-1.0f,0.0f };
	directionalLightData->intensity = 1.0f;

	ID3D12Resource* indexResourceSprite = *&dxCommon->CreateBufferResource(sizeof(uint32_t) * 6);

	D3D12_INDEX_BUFFER_VIEW indexBufferViewSprite{};

	//リソースの先頭アドレスから使う
	indexBufferViewSprite.BufferLocation = indexResourceSprite->GetGPUVirtualAddress();
	//使用するリソースのインデックスは6つ分のサイズ
	indexBufferViewSprite.SizeInBytes = sizeof(uint32_t) * 6;
	//インデックスはuint32_tとする
	indexBufferViewSprite.Format = DXGI_FORMAT_R32_UINT;

	uint32_t* indexDataSprite = nullptr;
	indexResourceSprite->Map(0, nullptr, reinterpret_cast<void**>(&indexDataSprite));
	indexDataSprite[0] = 0; indexDataSprite[1] = 1; indexDataSprite[2] = 2;
	indexDataSprite[3] = 1; indexDataSprite[4] = 4; indexDataSprite[5] = 2;

	//WVP用リソースのリソースを作る。Matrix4x4 1つ分のサイズを用意する
	ID3D12Resource* wvpResource = *&dxCommon->CreateBufferResource(sizeof(TransformationMatrix));
	//データを書き込む
	TransformationMatrix* wvpData = nullptr;
	//書き込むためのアドレス取得
	wvpResource->Map(0, nullptr, reinterpret_cast<void**>(&wvpData));
	//単位行列を書き込んでおく
	wvpData->World = MakeIdentity4x4();
	//WVP用リソースのリソースを作る。Matrix4x4 1つ分のサイズを用意する
	ID3D12Resource* wvpResource2 = *&dxCommon->CreateBufferResource(sizeof(TransformationMatrix));
	//データを書き込む
	TransformationMatrix* wvpData2 = nullptr;
	//書き込むためのアドレス取得
	wvpResource2->Map(0, nullptr, reinterpret_cast<void**>(&wvpData2));
	//単位行列を書き込んでおく
	wvpData2->World = MakeIdentity4x4();

	//WVP用リソースのリソースを作る。Matrix4x4 1つ分のサイズを用意する
	ID3D12Resource* transformationMatrixResourceSprite = *&dxCommon->CreateBufferResource(sizeof(TransformationMatrix));
	//データを書き込む
	TransformationMatrix* transformationMatrixDataSprite = nullptr;
	//書き込むためのアドレス取得
	transformationMatrixResourceSprite->Map(0, nullptr, reinterpret_cast<void**>(&transformationMatrixDataSprite));
	//単位行列を書き込んでおく
	transformationMatrixDataSprite->WVP =  MakeIdentity4x4();

	//=============================
	//球の頂点情報の作成
	// ============================
	////分割数
	//const int kSubdivision = 16;
	//const float kLonEvery = float(M_PI) * 2.0f / float(kSubdivision); // 経度の１分割あたりの角度
	//const float kLatEvery = float(M_PI) / float(kSubdivision); // 緯度の１分割あたりの角度

	//// 実際に頂点リソースを作る
	//const int vertexCount = kSubdivision * kSubdivision * 6;
	//ID3D12Resource* vertexResource = CreateBufferResource(device, sizeof(VertexData) * vertexCount);
	//// 頂点バッファビューを作成する
	//D3D12_VERTEX_BUFFER_VIEW vertexBufferView{};
	//// リソースの先頭のアドレスから使う
	//vertexBufferView.BufferLocation = vertexResource->GetGPUVirtualAddress();
	//// 使用するリソースのサイズは頂点3つ分のサイズ
	//vertexBufferView.SizeInBytes = sizeof(VertexData) * vertexCount;
	//// 1頂点あたりのサイズ
	//vertexBufferView.StrideInBytes = sizeof(VertexData);

	//=============================
	//Objモデルの頂点情報の作成
	// ============================
	ModelData modelData2 = LoadObjFile("resources", "fence.obj");
	//ID3D12Resource* vertexResource = CreateBufferResource(device, sizeof(VertexData) * modelData.vertices.size());
	////頂点バッファビューを作成する
	//D3D12_VERTEX_BUFFER_VIEW vertexBufferView{};
	//vertexBufferView.BufferLocation = vertexResource->GetGPUVirtualAddress();
	//vertexBufferView.SizeInBytes = UINT(sizeof(VertexData) * modelData.vertices.size());
	//vertexBufferView.StrideInBytes = sizeof(VertexData);
	ID3D12Resource* vertexResource2 = *&dxCommon->CreateBufferResource(sizeof(VertexData) * modelData2.vertices.size());
	//頂点バッファビューを作成する
	D3D12_VERTEX_BUFFER_VIEW vertexBufferView2{};
	vertexBufferView2.BufferLocation = vertexResource2->GetGPUVirtualAddress();
	vertexBufferView2.SizeInBytes = UINT(sizeof(VertexData) * modelData2.vertices.size());
	vertexBufferView2.StrideInBytes = sizeof(VertexData);

	ModelData modelData;
	modelData.vertices.push_back({ .position = {  1.0f,  1.0f, 0.0f, 1.0f }, .texcoord = { 0.0f, 0.0f }, .normal = { 0.0f, 0.0f, 1.0f } });
	modelData.vertices.push_back({ .position = { -1.0f,  1.0f, 0.0f, 1.0f }, .texcoord = { 1.0f, 0.0f }, .normal = { 0.0f, 0.0f, 1.0f } });
	modelData.vertices.push_back({ .position = {  1.0f, -1.0f, 0.0f, 1.0f }, .texcoord = { 0.0f, 1.0f }, .normal = { 0.0f, 0.0f, 1.0f } });
	modelData.vertices.push_back({ .position = {  1.0f, -1.0f, 0.0f, 1.0f }, .texcoord = { 0.0f, 1.0f }, .normal = { 0.0f, 0.0f, 1.0f } });
	modelData.vertices.push_back({ .position = { -1.0f,  1.0f, 0.0f, 1.0f }, .texcoord = { 1.0f, 0.0f }, .normal = { 0.0f, 0.0f, 1.0f } });
	modelData.vertices.push_back({ .position = { -1.0f, -1.0f, 0.0f, 1.0f }, .texcoord = { 1.0f, 1.0f }, .normal = { 0.0f, 0.0f, 1.0f } });

	ID3D12Resource* vertexResource = *&dxCommon->CreateBufferResource(sizeof(VertexData) * modelData.vertices.size());
	//頂点バッファビューを作成する
	D3D12_VERTEX_BUFFER_VIEW vertexBufferView{};
	vertexBufferView.BufferLocation = vertexResource->GetGPUVirtualAddress();
	vertexBufferView.SizeInBytes = UINT(sizeof(VertexData) * modelData.vertices.size());
	vertexBufferView.StrideInBytes = sizeof(VertexData);


	//Sprite用の頂点リソースを作る
	ID3D12Resource* vertexResourceSprite = *&dxCommon->CreateBufferResource(sizeof(VertexData) * 6);
	// 頂点バッファビューを作成する
	D3D12_VERTEX_BUFFER_VIEW vertexBufferViewSprite{};
	// リソースの先頭のアドレスから使う
	vertexBufferViewSprite.BufferLocation = vertexResourceSprite->GetGPUVirtualAddress();
	// 使用するリソースのサイズは頂点3つ分のサイズ
	vertexBufferViewSprite.SizeInBytes = sizeof(VertexData) * 6;
	// 1頂点あたりのサイズ
	vertexBufferViewSprite.StrideInBytes = sizeof(VertexData);

	// 頂点リソースにデータを書き込む
	VertexData* vertexData = nullptr;
	// 書き込むためのアドレスを取得
	vertexResource->Map(0, nullptr, reinterpret_cast<void**>(&vertexData));

	std::memcpy(vertexData, modelData.vertices.data(), sizeof(VertexData)* modelData.vertices.size());
	// 頂点リソースにデータを書き込む
	VertexData* vertexData2 = nullptr;
	// 書き込むためのアドレスを取得
	vertexResource2->Map(0, nullptr, reinterpret_cast<void**>(&vertexData2));

	std::memcpy(vertexData2, modelData.vertices.data(), sizeof(VertexData)* modelData.vertices.size());

	//複数Instance用のTransformationMatrixResource
	const uint32_t kNumInstance = 10;//インスタンス数
	Microsoft::WRL::ComPtr<ID3D12Resource> instancingResource =
		*&dxCommon->CreateBufferResource(sizeof(TransformationMatrix) * kNumInstance);
	//書き込むためのアドレスを取得
	TransformationMatrix* instancingData = nullptr;
	instancingResource->Map(0, nullptr, reinterpret_cast<void**>(&instancingData));
	//単位行列を書き込んでおく
	for(uint32_t index = 0; index < kNumInstance; ++index){
		instancingData[index].WVP = MakeIdentity4x4();
		instancingData[index].World = MakeIdentity4x4();
	}

	//int index = 0;
	//for (int latIndex = 0; latIndex < kSubdivision; ++latIndex) {
	//	// lat0: 現在の緯度、lat1: 次の緯度
	//	float lat0 = -float(M_PI) / 2.0f + kLatEvery * float(latIndex);
	//	float lat1 = -float(M_PI) / 2.0f + kLatEvery * float(latIndex + 1);

	//	for (int lonIndex = 0; lonIndex < kSubdivision; ++lonIndex) {
	//		// 経度方向も同じく分割
	//		float lon0 = kLonEvery * float(lonIndex);
	//		float lon1 = kLonEvery * float(lonIndex + 1);

	//		// 「start」を (latIndex, lonIndex) によって計算
	//		// 1 つのクワッドにつき頂点 6 つ分のオフセットを使う
	//		uint32_t start = (latIndex * kSubdivision + lonIndex) * 6;

	//		// ── クワッドを構成する 4 つの頂点をワールド座標上で計算 ──
	//		//  頂点 p0: (lat0, lon0)
	//		Vector3 p0;
	//		p0.x = std::cosf(lat0) * std::cosf(lon0);
	//		p0.y = std::sinf(lat0);
	//		p0.z = std::cosf(lat0) * std::sinf(lon0);

	//		//  頂点 p1: (lat1, lon0)
	//		Vector3 p1;
	//		p1.x = std::cosf(lat1) * std::cosf(lon0);
	//		p1.y = std::sinf(lat1);
	//		p1.z = std::cosf(lat1) * std::sinf(lon0);

	//		//  頂点 p2: (lat0, lon1)
	//		Vector3 p2;
	//		p2.x = std::cosf(lat0) * std::cosf(lon1);
	//		p2.y = std::sinf(lat0);
	//		p2.z = std::cosf(lat0) * std::sinf(lon1);

	//		//  頂点 p3: (lat1, lon1)
	//		Vector3 p3;
	//		p3.x = std::cosf(lat1) * std::cosf(lon1);
	//		p3.y = std::sinf(lat1);
	//		p3.z = std::cosf(lat1) * std::sinf(lon1);

	//		// ── テクスチャ座標 (u,v) を [0,1] にマッピング ──
	//		//  経度方向は lonIndex／kSubdivision、緯度方向は latIndex／kSubdivision を用いる
	//		float u0 = float(lonIndex) / float(kSubdivision);
	//		float u1 = float(lonIndex + 1) / float(kSubdivision);
	//		// v は「南極(-π/2)→北極(+π/2)」を [0,1] に対応させるので、1.0-（latIndex/分割数）とする
	//		float v0 = 1.0f - float(latIndex) / float(kSubdivision);
	//		float v1 = 1.0f - float(latIndex + 1) / float(kSubdivision);

	//		// ────────────────────────────────────────────────────────────────────
	//		// 三角形１： (p0, p1, p2)
	//		// ────────────────────────────────────────────────────────────────────
	//		// 頂点 0: p0
	//		vertexData[start + 0].position = { p0.x, p0.y, p0.z, 1.0f };
	//		vertexData[start + 0].texcoord = { u0, v0 };
	//		// 頂点 1: p1
	//		vertexData[start + 1].position = { p1.x, p1.y, p1.z, 1.0f };
	//		vertexData[start + 1].texcoord = { u0, v1 };
	//		// 頂点 2: p2
	//		vertexData[start + 2].position = { p2.x, p2.y, p2.z, 1.0f };
	//		vertexData[start + 2].texcoord = { u1, v0 };

	//		// ────────────────────────────────────────────────────────────────────
	//		// 三角形２： (p2, p1, p3)
	//		// ────────────────────────────────────────────────────────────────────
	//		// 頂点 3: p2（再利用）
	//		vertexData[start + 3].position = { p2.x, p2.y, p2.z, 1.0f };
	//		vertexData[start + 3].texcoord = { u1, v0 };
	//		// 頂点 4: p1（再利用）
	//		vertexData[start + 4].position = { p1.x, p1.y, p1.z, 1.0f };
	//		vertexData[start + 4].texcoord = { u0, v1 };
	//		// 頂点 5: p3
	//		vertexData[start + 5].position = { p3.x, p3.y, p3.z, 1.0f };
	//		vertexData[start + 5].texcoord = { u1, v1 };

	//		// ※ index を使ったインクリメントは不要。start で直接書き込んでいるので、
	//		//    もし index 変数を使う場合は「index += 6;」するか、
	//		//    コメントアウトした以下のようなチェックを行ってもよいです。
	//		// index += 6;

	//		for (int index = 0; index < 6; index++) {
	//			vertexData[start + index].normal.x = vertexData[start + index].position.x;
	//			vertexData[start + index].normal.y = vertexData[start + index].position.y;
	//			vertexData[start + index].normal.z = vertexData[start + index].position.z;
	//		}
	//	}
	//}

	// 必要ならチェック
	// assert(index == vertexCount);

	vertexResource->Unmap(0, nullptr);
	vertexResource2->Unmap(0, nullptr);


	//// 左下
	//vertexData[0].position = { -0.5f, -0.5f, 0.0f, 1.0f };
	//vertexData[0].texcoord = { 0.0f,1.0f };
	//// 上
	//vertexData[1] = { 0.0f,  0.5f, 0.0f, 1.0f };
	//vertexData[1].texcoord = { 0.5f,0.0f };
	//// 右下
	//vertexData[2] = { 0.5f, -0.5f, 0.0f, 1.0f };
	//vertexData[2].texcoord = { 1.0f,1.0f };

	////二枚目
	//// 左下
	//vertexData[3].position = { -0.5f, -0.5f, 0.5f, 1.0f };
	//vertexData[3].texcoord = { 0.0f,1.0f };
	//// 上
	//vertexData[4] = { 0.0f,  0.0f, 0.0f, 1.0f };
	//vertexData[4].texcoord = { 0.5f,0.0f };
	//// 右下
	//vertexData[5] = { 0.5f, -0.5f, -0.5f, 1.0f };
	//vertexData[5].texcoord = { 1.0f,1.0f };

	//Spriteの頂点データ
	VertexData* vertexDataSprite = nullptr;
	vertexResourceSprite->Map(0, nullptr, reinterpret_cast<void**>(&vertexDataSprite));
	// 左下
	vertexDataSprite[0].position = { 0.0f, 360.0f, 0.0f, 1.0f };
	vertexDataSprite[0].texcoord = { 0.0f,1.0f };
	// 上
	vertexDataSprite[1] = { 0.0f,  0.0f, 0.0f, 1.0f };
	vertexDataSprite[1].texcoord = { 0.0f,0.0f };
	// 右下
	vertexDataSprite[2] = { 640.0f, 360.0f, 0.0f, 1.0f };
	vertexDataSprite[2].texcoord = { 1.0f,1.0f };

	//二枚目
	// 左下
	vertexDataSprite[3].position = { 0.0f, 0.0f, 0.0f, 1.0f };
	vertexDataSprite[3].texcoord = { 0.0f,0.0f };
	// 上
	vertexDataSprite[4] = { 640.0f,  0.0f, 0.0f, 1.0f };
	vertexDataSprite[4].texcoord = { 1.0f,0.0f };
	// 右下
	vertexDataSprite[5] = { 640.0f, 360.0f, 0.0f, 1.0f };
	vertexDataSprite[5].texcoord = { 1.0f,1.0f };

	vertexDataSprite[0].normal = { 0.0f,0.0f,-1.0f };





	//Transform変数を作る
	Transform transform = { {1.0f,1.0f,1.0f},{0.0f,0.0f,0.0f},{0.0f,0.0f,0.0f} };
	Transform transform2 = { {1.0f,1.0f,1.0f},{0.0f,3.14f,0.0f},{0.0f,0.0f,0.0f} };
	Transform cameraTransform = { {1.0f,1.0f,1.0f},{0.0f,0.0f,0.0f},{0.0f,0.0f,-5.0f} };
	Transform transformSprite = { {1.0f,1.0f,1.0f},{0.0f,0.0f,0.0f},{0.0f,0.0f,0.0f} };

	Transform transforms[kNumInstance];
	for(uint32_t index = 0; index < kNumInstance; ++index){
		transforms[index].scale = { 1.0f,1.0f,1.0f };
		transforms[index].rotate = { 0.0f,0.0f,0.0f };
		transforms[index].translate = { index * 0.1f,index * 0.1f,index * 0.1f };
	}

	Transform uvTransformSprite{
		{1.0f,1.0f,1.0f},
		{0.0f,0.0f,0.0f},
		{0.0f,0.0f,0.0f},
	};

	bool isDrawSprite = true;
	bool useMonsterBall = true;

	////metaDataを基にSRVの設定
	//D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
	//srvDesc.Format = metadata.format;
	//srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	//srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	//srvDesc.Texture2D.MipLevels = UINT(metadata.mipLevels);

	////SRVを作成するDescriptorHeapの設定
	//D3D12_CPU_DESCRIPTOR_HANDLE textureSrvHandleCPU = dxCommon->GetSRVCPUDescriptorHandle(0);
	//D3D12_GPU_DESCRIPTOR_HANDLE textureSrvHandleGPU = dxCommon->GetSRVGPUDescriptorHandle(0);
	////先頭はImGuiが使っているのでその次を使う
	//textureSrvHandleCPU.ptr += dxCommon->GetDevice()->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	//textureSrvHandleGPU.ptr += dxCommon->GetDevice()->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	////SRVの作成
	//dxCommon->GetDevice()->CreateShaderResourceView(textureResource, &srvDesc, textureSrvHandleCPU);

	////metaDataを基に二個目のSRVの設定
	//D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc2{};
	//srvDesc2.Format = metadata2.format;
	//srvDesc2.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	//srvDesc2.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	//srvDesc2.Texture2D.MipLevels = UINT(metadata2.mipLevels);

	////SRVを作成するDescriptorHeapの設定
	//D3D12_CPU_DESCRIPTOR_HANDLE textureSrvHandleCPU2 = dxCommon->GetSRVCPUDescriptorHandle(2);
	//D3D12_GPU_DESCRIPTOR_HANDLE textureSrvHandleGPU2 = dxCommon->GetSRVGPUDescriptorHandle(2);

	//dxCommon->GetDevice()->CreateShaderResourceView(textureResource2, &srvDesc2, textureSrvHandleCPU2);

	//StructuredBuffer用のSRVの設定
	D3D12_SHADER_RESOURCE_VIEW_DESC instancingSrvDesc{};
	instancingSrvDesc.Format = DXGI_FORMAT_UNKNOWN;
	instancingSrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	instancingSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
	instancingSrvDesc.Buffer.FirstElement = 0;
	instancingSrvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
	instancingSrvDesc.Buffer.NumElements = kNumInstance;
	instancingSrvDesc.Buffer.StructureByteStride = sizeof(TransformationMatrix);
	D3D12_CPU_DESCRIPTOR_HANDLE instancingSrvHandleCPU = dxCommon->GetSRVCPUDescriptorHandle(3);
	D3D12_GPU_DESCRIPTOR_HANDLE instancingSrvHandleGPU = dxCommon->GetSRVGPUDescriptorHandle(3);
	dxCommon->GetDevice()->CreateShaderResourceView(instancingResource.Get(), &instancingSrvDesc, instancingSrvHandleCPU);


	std::vector<Sprite*> sprites;
	for(uint32_t i = 0; i < 1; ++i){
		Sprite* sprite = new Sprite();
		if(i % 2 == 1){
			sprite->Initialize(spriteCommon, "resources/monsterBall.png");
		} else{
			sprite->Initialize(spriteCommon, "resources/uvChecker.png");
		}
		sprite->SetPosition({ float(i * 200),0.0f });
		sprites.push_back(sprite);
	}

	Object3d* object3d = new Object3d();
	object3d->Initialize(object3dCommon);

	// ウィンドウの×ボタンが押されるまでループ
	while (true) {

		ImGui_ImplDX12_NewFrame();
		ImGui_ImplWin32_NewFrame();
		ImGui::NewFrame();

		// Windowsのメッセージ処理
		if(winApp->ProcessMessage()) {
			//ゲームループを抜ける
			break;
		}


		//================================
		//入力処理
		//================================

		input->Update();
		if(input->PushKey(DIK_0)){
			OutputDebugStringA("Hit 0 \n");
		}
		if(input->TriggerKey(DIK_0)){
			OutputDebugStringA("Trigger 0 \n");
		}

		//================================
		// ループの用意
		//================================

		// ImGui処理の中に追加（ImGui::ShowDemoWindow(); の前でも後でもOK）
		ImGui::Begin("Scene Controls");

		// === modelTransform操作 ===
		if (ImGui::CollapsingHeader("modelTransform", ImGuiTreeNodeFlags_DefaultOpen)) {
			ImGui::DragFloat3("modelRotate", &transform.rotate.x, 0.01f);
		}
		// === uvColor操作 ===
		if(ImGui::CollapsingHeader("modelColor", ImGuiTreeNodeFlags_DefaultOpen)){
			ImGui::DragFloat4("modelColor", &materialData->color.x, 0.01f, 0.0f, 1.0f);
		}

		if (ImGui::CollapsingHeader("directionalLight", ImGuiTreeNodeFlags_DefaultOpen)) {

			ImGui::DragFloat4("Color", &directionalLightData->color.x, 0.01f, 0.0f, 1.0f);
			ImGui::DragFloat3("direction", &directionalLightData->direction.x, 0.01f);
			ImGui::DragFloat("intensity", &directionalLightData->intensity, 0.1f,0.0f,10.0f);
		}

		directionalLightData->direction = Math::Normalize(directionalLightData->direction);

		// === カメラ操作 ===
		if (ImGui::CollapsingHeader("Camera", ImGuiTreeNodeFlags_DefaultOpen)) {
			ImGui::Text("Position");
			ImGui::DragFloat3("Camera Pos", &cameraTransform.translate.x, 0.01f);

			ImGui::Text("Rotation (Radians)");
			ImGui::DragFloat3("Camera Rot", &cameraTransform.rotate.x, 0.01f);
		}

		// === uvTransform操作 ===
		if (ImGui::CollapsingHeader("UVTransform", ImGuiTreeNodeFlags_DefaultOpen)) {
			ImGui::DragFloat2("UVTranslate", &uvTransformSprite.translate.x, 0.01f, -10.0f, 10.0f);
			ImGui::DragFloat2("UVScale", &uvTransformSprite.scale.x, 0.01f, -10.0f, 10.0f);

			ImGui::SliderAngle("UVRotate", &uvTransformSprite.rotate.z);
		}
		// === spriteColor操作 ===
		if (ImGui::CollapsingHeader("spriteColor", ImGuiTreeNodeFlags_DefaultOpen)) {
			ImGui::DragFloat4("spriteColor", &materialDataSprite->color.x, 0.01f,0.0f,1.0f);
		}

		// === トグル機能 ===
		if (ImGui::CollapsingHeader("Options", ImGuiTreeNodeFlags_DefaultOpen)) {
			ImGui::Checkbox("Enable Feature", &isDrawSprite);
			ImGui::Checkbox("useMonsterBall", &useMonsterBall);
		}

		ImGui::End();

		for(Sprite* sprite : sprites){
			sprite->Update();
		}
		object3d->Update();
		//ゲーム処理
		/*transform.rotate.y += 0.03f;*/
		Matrix4x4 worldMatrix = MakeAffineMatrix(transform.scale, transform.rotate, transform.translate);
		Matrix4x4 cameraMatrix = MakeAffineMatrix(cameraTransform.scale, cameraTransform.rotate, cameraTransform.translate);
		Matrix4x4 viewMatrix = Inverse(cameraMatrix);
		Matrix4x4 projectionMatrix = MakePerspectiveFovMatrix(0.45f, float(WinApp::kClientWidth) / float(WinApp::kClientHeight), 0.1f, 100.0f);
		Matrix4x4 viewProjectionMatirx = Multiply(viewMatrix, projectionMatrix);
		Matrix4x4 worldViewProjectionMatrix = Multiply(worldMatrix, viewProjectionMatirx);

		wvpData->WVP = worldViewProjectionMatrix;
		wvpData->World = worldMatrix;

		Matrix4x4 worldMatrix2 = MakeAffineMatrix(transform2.scale, transform2.rotate, transform2.translate);
		Matrix4x4 worldViewProjectionMatrix2 = Multiply(worldMatrix2, viewProjectionMatirx);
		wvpData2->WVP = worldViewProjectionMatrix2;
		wvpData2->World = worldMatrix2;

		for(uint32_t index = 0; index < kNumInstance; ++index){
			Matrix4x4 worldMatrix =
				MakeAffineMatrix(transforms[index].scale, transforms[index].rotate, transforms[index].translate);
			Matrix4x4 worldViewProjectionMatirx = Multiply(worldMatrix, viewProjectionMatirx);
			instancingData[index].WVP = worldViewProjectionMatirx;
			instancingData[index].World = worldMatrix;
		}

		Matrix4x4 worldMatrixSprite = MakeAffineMatrix(transformSprite.scale, transformSprite.rotate, transformSprite.translate);
		Matrix4x4 viewMatrixSprite = MakeIdentity4x4();
		Matrix4x4 projectionMatrixSprite = MakeOrthographicMatrix(0.0f, 0.0f, float(WinApp::kClientWidth), float(WinApp::kClientHeight), 0.0f, 100.0f);
		Matrix4x4 worldViewProjectionMatrixSprite = Multiply(worldMatrixSprite, Multiply(viewMatrixSprite, projectionMatrixSprite));
		
		transformationMatrixDataSprite->WVP =  worldViewProjectionMatrixSprite;
		transformationMatrixDataSprite->World = MakeIdentity4x4();

		Matrix4x4 uvTransformMatrix = MakeScaleMatrix(uvTransformSprite.scale);
		uvTransformMatrix = Multiply(uvTransformMatrix, MakeRotateZMatrix(uvTransformSprite.rotate.z));
		uvTransformMatrix = Multiply(uvTransformMatrix, MakeTranslateMatrix(uvTransformSprite.translate));
		materialDataSprite->uvTransform = uvTransformMatrix;

		ImGui::Render();

		//DirectXの描画準備。全ての描画に共通のグラフィックスコマンドを積む
		dxCommon->PreDraw();


//		// RootSignatureを設定。PSOに設定しているけど別途設定も必要
//		dxCommon->GetCommandList()->SetGraphicsRootSignature(rootSignatureForInstancing);
//		dxCommon->GetCommandList()->SetPipelineState(graphicsPipelineStateForInstancing); // PSOを設定
//
//		dxCommon->GetCommandList()->IASetVertexBuffers(0, 1, &vertexBufferView); // VBVを設定
//
//		//commandList->SetGraphicsRootConstantBufferView(0, materialResource->GetGPUVirtualAddress());
//
//		//commandList->SetGraphicsRootConstantBufferView(1, wvpResource->GetGPUVirtualAddress());
//
//		//commandList->SetGraphicsRootConstantBufferView(3, directionalLightResource->GetGPUVirtualAddress());
//
//		/*commandList->SetGraphicsRootDescriptorTable(2, useMonsterBall ? textureSrvHandleGPU2 : textureSrvHandleGPU);*/
//
////		dxCommon->GetCommandList()->SetGraphicsRootDescriptorTable(1,instancingSrvHandleGPU);
////
////		dxCommon->GetCommandList()->SetGraphicsRootConstantBufferView(0, materialResource->GetGPUVirtualAddress()); // PS b0
////		dxCommon->GetCommandList()->SetGraphicsRootDescriptorTable(1, instancingSrvHandleGPU);                   // VS t0
/////*		dxCommon->GetCommandList()->SetGraphicsRootDescriptorTable(2, textureSrvHandleGPU);        */              // PS t0
////		dxCommon->GetCommandList()->SetGraphicsRootConstantBufferView(3, directionalLightResource->GetGPUVirtualAddress()); // PS b1
////
////		// 形状を設定。PSOに設定しているものとは本来別。同じものを設定することを考えておけば良い
////		dxCommon->GetCommandList()->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
////
////		// 描画！（DrawCall／ドローコール）。3頂点で1つのインスタンス。インスタンスについては今後
////		dxCommon->GetCommandList()->DrawInstanced(UINT(modelData.vertices.size()), kNumInstance, 0, 0);
//
//		//二つ目のモデルの描画
//		//commandList->SetGraphicsRootConstantBufferView(1, wvpResource2->GetGPUVirtualAddress());
//		//commandList->IASetVertexBuffers(0, 1, &vertexBufferView2); // VBVを設定
//		//commandList->DrawInstanced(UINT(modelData.vertices.size()), 1, 0, 0);
//
//		//Spriteの描画。変更が必要なものだけ変更する
//				// RootSignatureを設定。PSOに設定しているけど別途設定も必要
//		dxCommon->GetCommandList()->SetGraphicsRootSignature(rootSignature);
//		dxCommon->GetCommandList()->SetPipelineState(graphicsPipelineState); // PSOを設定
//		dxCommon->GetCommandList()->IASetVertexBuffers(0, 1, &vertexBufferViewSprite); // VBVを設定
//
//		dxCommon->GetCommandList()->IASetIndexBuffer(&indexBufferViewSprite); // VBVを設定
//
//		//TransformationMatrixCBufferの場所を限定
//		dxCommon->GetCommandList()->SetGraphicsRootConstantBufferView(1, transformationMatrixResourceSprite->GetGPUVirtualAddress());
//
//		//dxCommon->GetCommandList()->SetGraphicsRootDescriptorTable(2, textureSrvHandleGPU);
//
//		dxCommon->GetCommandList()->SetGraphicsRootConstantBufferView(0, materialResourceSprite->GetGPUVirtualAddress());
//
//		dxCommon->GetCommandList()->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

		object3dCommon->SetCommonRenderState();
		object3d->Draw();

		//if (isDrawSprite) {
		//	//描画
		//	dxCommon->GetCommandList()->DrawIndexedInstanced(6, 1, 0, 0, 0);
		//}
		//Spriteの描画準備。Spriteの描画に共通のグラフィックスコマンドを積む

		spriteCommon->SetCommonRenderState();
		for(Sprite* sprite : sprites){
			sprite->Draw();
		}

		ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), dxCommon->GetCommandList());

		dxCommon->PostDraw();

	}

	//ImGuiの終了
	ImGui_ImplDX12_Shutdown();
	ImGui_ImplWin32_Shutdown();
	ImGui::DestroyContext();

	//出力ウィンドウへの文字出力
	OutputDebugStringA("Hello,DirectX!\n");

	//CloseHandle(fenceEvent);
	vertexResource->Release();
	graphicsPipelineState->Release();
	signatureBlob->Release();
	if (errorBlob) {
		errorBlob->Release();
	}
	rootSignature->Release();
	pixelShaderBlob->Release();
	vertexShaderBlob->Release();
	materialResource->Release();
	//intermediaResource->Release();

#ifdef _DEBUG

#endif
	winApp->Finalize();
	TextureManager::GetInstance()->Finalize();

	for(Sprite* sprite : sprites){
	delete sprite;
	sprite = nullptr;
	}
	delete spriteCommon;
	spriteCommon = nullptr;
	delete input;
	input = nullptr;
	delete winApp;
	winApp = nullptr;
	delete dxCommon;
	dxCommon = nullptr;

	delete checker;


	return 0;
}