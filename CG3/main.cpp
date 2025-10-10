#include <Windows.h>
#define _USE_MATH_DEFINES
#include <cmath>
#include <iostream>
#include <cstdint>
#include<filesystem>
//ファイルを読み書きするためのライブラリ
#include <fstream>
//時間を扱うライブラリ
#include <chrono>
//文字列を扱うライブラリ
#include <string>
//
#include <format>
//
#include <d3d12.h>
#include <dxgi1_6.h>
#include <cassert>
#pragma comment(lib,"d3d12.lib")
#pragma comment(lib,"dxgi.lib")
//デバッグ用のあれこれを使えるようにする
#include <dbghelp.h>
#pragma comment(lib,"Dbghelp.lib")
#include <strsafe.h>

#include <dxgidebug.h>
#pragma comment(lib, "dxguid.lib")

#include <dxcapi.h>
#pragma comment(lib,"dxcompiler.lib")

#include <string>
//imguiを使うためのinclude
#include "externals/imgui/imgui.h"
#include "externals/imgui/imgui_impl_dx12.h"
#include "externals/imgui/imgui_impl_win32.h"
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
#include <DirectXTex.h>
#include "externals/DirectXTex/d3dx12.h"
#include <vector>

#include "../CG3/Vector/Vector2.h"
#include "../CG3/Vector/Vector3.h"
#include "../CG3/Vector/Vector4.h"
#include "../CG3/Matrix/Matrix4x4.h"
#include "../CG3/Matrix/Matrix3x3.h"

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

struct Transform {
	Vector3 scale;
	Vector3 rotate;
	Vector3 translate;
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

float Length(const Vector3& v) {
	return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
}

Vector3 Normalize(const Vector3& v) {
	float len = Length(v);
	if (len < 1e-6f) {
		return { 0.0f, 0.0f, 0.0f };
	}
	float inv = 1.0f / len;
	return { v.x * inv, v.y * inv, v.z * inv };
}

std::wstring ConvertString(const std::string& str) {
	if (str.empty()) {
		return std::wstring();
	}

	auto sizeNeeded = MultiByteToWideChar(CP_UTF8, 0, reinterpret_cast<const char*>(&str[0]), static_cast<int>(str.size()), NULL, 0);
	if (sizeNeeded == 0) {
		return std::wstring();
	}
	std::wstring result(sizeNeeded, 0);
	MultiByteToWideChar(CP_UTF8, 0, reinterpret_cast<const char*>(&str[0]), static_cast<int>(str.size()), &result[0], sizeNeeded);
	return result;
}

std::string ConvertString(const std::wstring& str) {
	if (str.empty()) {
		return std::string();
	}

	auto sizeNeeded = WideCharToMultiByte(CP_UTF8, 0, str.data(), static_cast<int>(str.size()), NULL, 0, NULL, NULL);
	if (sizeNeeded == 0) {
		return std::string();
	}
	std::string result(sizeNeeded, 0);
	WideCharToMultiByte(CP_UTF8, 0, str.data(), static_cast<int>(str.size()), result.data(), sizeNeeded, NULL, NULL);
	return result;
}

void Log(const std::string& message) {
	OutputDebugStringA(message.c_str());
}

void Log(const std::wstring& message) {
	OutputDebugStringW(message.c_str());
}

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

IDxcBlob* CompileShader(
	// CompilerするShaderファイルへのパス
	const std::wstring& filePath,
	// Compilerに使用するProfile
	const wchar_t* profile,
	// 初期化で生成したものを3つ
	IDxcUtils* dxcUtils,
	IDxcCompiler3* dxcCompiler,
	IDxcIncludeHandler* includeHandler)
{
	// 1. hlslファイルを読む
	// // これからシェーダーをコンパイルする旨をログに出す
	Log(ConvertString(std::format(L"Begin CompileShader, path:{}, profile:{}\n", filePath, profile)));

	// hlslファイルを読む
	IDxcBlobEncoding* shaderSource = nullptr;
	HRESULT hr = dxcUtils->LoadFile(filePath.c_str(), nullptr, &shaderSource);
	// 読めなかったら止める
	assert(SUCCEEDED(hr));

	// 読み込んだファイルの内容を設定する
	DxcBuffer shaderSourceBuffer;
	shaderSourceBuffer.Ptr = shaderSource->GetBufferPointer();
	shaderSourceBuffer.Size = shaderSource->GetBufferSize();
	shaderSourceBuffer.Encoding = DXC_CP_UTF8; // UTF8の文字コードであることを通知

	// 2. Compileする
	LPCWSTR arguments[] = {
	filePath.c_str(),     // コンパイル対象のhlslファイル名
	L"-E", L"main",       // エントリーポイントの指定。基本的にmain以外にはしない
	L"-T", profile,       // ShaderProfileの設定
	L"-Zi", L"-Qembed_debug", // デバッグ用の情報を埋め込む
	L"-Od",               // 最適化を外しておく
	L"-Zpr",              // メモリレイアウトは行優先
	};

	// 実際にShaderをコンパイルする
	IDxcResult* shaderResult = nullptr;
	hr = dxcCompiler->Compile(
		&shaderSourceBuffer,     // 読み込んだファイル
		arguments,               // コンパイルオプション
		_countof(arguments),     // コンパイルオプションの数
		includeHandler,          // includeが含まれた場合
		IID_PPV_ARGS(&shaderResult) // コンパイル結果
	);
	// コンパイルエラーではなくdxcが起動できないなど致命的な状況
	assert(SUCCEEDED(hr));

	// 3. 警告・エラーがでていないか確認する
	// 警告・エラーが出てたらログに出して止める
	IDxcBlobUtf8* shaderError = nullptr;
	shaderResult->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(&shaderError), nullptr);
	if (shaderError != nullptr && shaderError->GetStringLength() != 0) {
		Log(shaderError->GetStringPointer());
		// 警告・エラー ダメ ゼッタイ
		assert(false);
	}

	// 4. Compile結果を受け取って返す
	// コンパイル結果から実行用のバイナリ部分を取得
	IDxcBlob* shaderBlob = nullptr;
	hr = shaderResult->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(&shaderBlob), nullptr);
	assert(SUCCEEDED(hr));

	// 成功したログを出す
	Log(ConvertString(std::format(L"Compile Succeeded, path:{}, profile:{}\n", filePath, profile)));

	// もう使わないリソースを解放
	shaderSource->Release();
	shaderResult->Release();

	// 実行用のバイナリを返却
	return shaderBlob;

}

ID3D12Resource* CreateBufferResource(ID3D12Device* device, size_t sizeInBytes) {
	// 頂点リソース用のヒープの設定
	D3D12_HEAP_PROPERTIES uploadHeapProperties{};
	uploadHeapProperties.Type = D3D12_HEAP_TYPE_UPLOAD; // UploadHeapを使う

	// 頂点リソースの設定
	D3D12_RESOURCE_DESC vertexResourceDesc{};
	// バッファリソース。テクスチャの場合はまた別の設定をする
	vertexResourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	vertexResourceDesc.Width = sizeInBytes; // リソースのサイズ。今回はVector4を3頂点分
	// バッファの高さは1にする決まり
	vertexResourceDesc.Height = 1;
	vertexResourceDesc.DepthOrArraySize = 1;
	vertexResourceDesc.MipLevels = 1;
	vertexResourceDesc.SampleDesc.Count = 1;
	// バッファの場合はこれにされる決まり
	vertexResourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

	HRESULT hr;

	ID3D12Resource* vertexResource = nullptr;

	hr = device->CreateCommittedResource(&uploadHeapProperties, D3D12_HEAP_FLAG_NONE,
		&vertexResourceDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
		IID_PPV_ARGS(&vertexResource));
	assert(SUCCEEDED(hr));

	return vertexResource;
}

ID3D12DescriptorHeap* CreateDescriptorHeap(
	ID3D12Device* device, D3D12_DESCRIPTOR_HEAP_TYPE heapType, UINT numDescriptors, bool shaderVisible) {

	ID3D12DescriptorHeap* descriptorHeap = nullptr;
	D3D12_DESCRIPTOR_HEAP_DESC descriptorHeapDesc{};
	descriptorHeapDesc.Type = heapType;  // レンダーターゲットビュー用
	descriptorHeapDesc.NumDescriptors = numDescriptors;  // ダブルバッファ用に2つ。多くても別に構わない
	descriptorHeapDesc.Flags = shaderVisible ? D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE : D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
	HRESULT hr = device->CreateDescriptorHeap(&descriptorHeapDesc, IID_PPV_ARGS(&descriptorHeap));
	// ディスクリプタヒープが作れなかったので起動できない
	assert(SUCCEEDED(hr));

	return descriptorHeap;
}

static LONG WINAPI ExportDump(EXCEPTION_POINTERS* exception) {
	// 時刻を取得して、時刻を名前に入れたファイルを作成し、Dumpsディレクトリ以下に出力
	SYSTEMTIME time;
	GetLocalTime(&time);
	wchar_t filePath[MAX_PATH] = { 0 };
	CreateDirectory(L"./Dumps", nullptr);
	StringCchPrintfW(filePath, MAX_PATH, L"./Dumps/%04d-%02d-%02d-%02d%02d.dmp", time.wYear, time.wMonth, time.wDay, time.wHour, time.wMinute);
	HANDLE dumpFileHandle = CreateFile(filePath, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_WRITE | FILE_SHARE_READ, 0, CREATE_ALWAYS, 0, 0);

	// processId（CのexeのID）とスレッド（例外）の発生したthreadIdを取得
	DWORD processId = GetCurrentProcessId();
	DWORD threadId = GetCurrentThreadId();

	// 設定情報を入力
	MINIDUMP_EXCEPTION_INFORMATION minidumpInformation{ };
	minidumpInformation.ThreadId = threadId;
	minidumpInformation.ExceptionPointers = exception;
	minidumpInformation.ClientPointers = TRUE;

	// Dmpを出力。MiniDumpNormalは最低限の情報を書き出すフラグ
	MiniDumpWriteDump(GetCurrentProcess(), processId, dumpFileHandle, MiniDumpNormal, &minidumpInformation, nullptr, nullptr);

	// 例外に紐づけられているSEH例外ハンドラに任せる。通常はエクスポートされてる
	return EXCEPTION_EXECUTE_HANDLER;

}

// ウィンドウプロシージャ
LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg,
	WPARAM wparam, LPARAM lparam) {
	if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wparam, lparam)) {
		return 0;
	}
	// メッセージに応じてゲーム固有の処理を行う
	switch (msg) {
		// ウィンドウが破棄された
	case WM_DESTROY:
		// OSに対して、アプリの終了を伝える
		PostQuitMessage(0);
		return 0;
	}

	// 標準のメッセージ処理を行う
	return DefWindowProc(hwnd, msg, wparam, lparam);
}

DirectX::ScratchImage LoadTexture(const std::string& filePath) {
	//テクスチャを読み込んでプログラムで扱えるようにする
	DirectX::ScratchImage image{};
	std::wstring filePathW = ConvertString(filePath);
	HRESULT hr = DirectX::LoadFromWICFile(filePathW.c_str(), DirectX::WIC_FLAGS_FORCE_SRGB, nullptr, image);
	assert(SUCCEEDED(hr));

	//ミニマップの作成
	DirectX::ScratchImage mipImages{};
	hr = DirectX::GenerateMipMaps(image.GetImages(), image.GetImageCount(), image.GetMetadata(), DirectX::TEX_FILTER_SRGB, 0, mipImages);
	assert(SUCCEEDED(hr));

	//ミニマップ付きのデータを返す
	return mipImages;

}

ID3D12Resource* CreateTextureResource(ID3D12Device* device, const DirectX::TexMetadata& metadate) {
	//1. metadataを基にResourceの設定
	D3D12_RESOURCE_DESC resourceDesc{};
	resourceDesc.Width = UINT(metadate.width);//Textureの幅
	resourceDesc.Height = UINT(metadate.height);//Textureの高さ
	resourceDesc.MipLevels = UINT(metadate.mipLevels);//mipmapの数
	resourceDesc.DepthOrArraySize = UINT(metadate.arraySize);//奥行 or 配列Textureの配列数
	resourceDesc.Format = metadate.format;//TextureのFormat
	resourceDesc.SampleDesc.Count = 1;//サンプリングカウント。1固定。
	resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION(metadate.dimension);//Textureの次元数。普段使っているのは二次元
	//2.利用するHeapの設定
	D3D12_HEAP_PROPERTIES heapPropaties{};
	heapPropaties.Type = D3D12_HEAP_TYPE_DEFAULT;//細かい設定を行う
	//3.Resourを生成する
	ID3D12Resource* resource = nullptr;
	HRESULT hr = device->CreateCommittedResource(
		&heapPropaties,//Heapの設定
		D3D12_HEAP_FLAG_NONE,//Heapの特殊な設定、特になし。
		&resourceDesc,
		D3D12_RESOURCE_STATE_COPY_DEST,
		nullptr,
		IID_PPV_ARGS(&resource));
	assert(SUCCEEDED(hr));
	return resource;
}

[[nodiscard]]
ID3D12Resource* UploadTextureData(ID3D12Resource* texture, const DirectX::ScratchImage& mipImages, ID3D12Device* device,
	ID3D12GraphicsCommandList* commandList) {

	std::vector<D3D12_SUBRESOURCE_DATA> subresource;
	DirectX::PrepareUpload(device, mipImages.GetImages(), mipImages.GetImageCount(), mipImages.GetMetadata(), subresource);
	uint64_t intermediatesize = GetRequiredIntermediateSize(texture, 0, UINT(subresource.size()));
	ID3D12Resource* intermediaResouece = CreateBufferResource(device, intermediatesize);
	UpdateSubresources(commandList, texture, intermediaResouece, 0, 0, UINT(subresource.size()), subresource.data());

	D3D12_RESOURCE_BARRIER barrier{};
	barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
	barrier.Transition.pResource = texture;
	barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
	barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_GENERIC_READ;
	commandList->ResourceBarrier(1, &barrier);
	return intermediaResouece;

}

ID3D12Resource* CreateDepthStencilTextureResource(ID3D12Device* device, int32_t width, int32_t height) {
	//生成するリソースの設定
	D3D12_RESOURCE_DESC resourceDesc{};
	resourceDesc.Width = width;
	resourceDesc.Height = height;
	resourceDesc.MipLevels = 1;
	resourceDesc.DepthOrArraySize = 1;
	resourceDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
	resourceDesc.SampleDesc.Count = 1;
	resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	resourceDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

	D3D12_HEAP_PROPERTIES heapProperties{};
	heapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;

	//深度値のクリア設定
	D3D12_CLEAR_VALUE depthClearValue{};
	depthClearValue.DepthStencil.Depth = 1.0f;
	depthClearValue.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;

	//Resourceの生成
	ID3D12Resource* resource = nullptr;
	HRESULT hr = device->CreateCommittedResource(
		&heapProperties,
		D3D12_HEAP_FLAG_NONE,
		&resourceDesc,
		D3D12_RESOURCE_STATE_DEPTH_WRITE,
		&depthClearValue,
		IID_PPV_ARGS(&resource));
	assert(SUCCEEDED(hr));

	return resource;
}

D3D12_CPU_DESCRIPTOR_HANDLE  GetCPUDescriptorHandle(ID3D12DescriptorHeap* descriptorHeap, uint32_t descriptorSize, uint32_t index) {

	D3D12_CPU_DESCRIPTOR_HANDLE handleCPU = descriptorHeap->GetCPUDescriptorHandleForHeapStart();
	handleCPU.ptr += (descriptorSize * index);
	return handleCPU;

}

D3D12_GPU_DESCRIPTOR_HANDLE  GetGPUDescriptorHandle(ID3D12DescriptorHeap* descriptorHeap, uint32_t descriptorSize, uint32_t index) {

	D3D12_GPU_DESCRIPTOR_HANDLE handleGPU = descriptorHeap->GetGPUDescriptorHandleForHeapStart();
	handleGPU.ptr += (descriptorSize * index);
	return handleGPU;

}

//Windowsアプリでのエントリーポイント(main開放)
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {

	CoInitializeEx(0, COINIT_MULTITHREADED);

	SetUnhandledExceptionFilter(ExportDump);

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



	WNDCLASS wc{};
	// ウィンドウプロシージャ
	wc.lpfnWndProc = WindowProc;
	// ウィンドウクラス名（なんでも良い）
	wc.lpszClassName = L"CG3WindowClass";
	// インスタンスハンドル
	wc.hInstance = GetModuleHandle(nullptr);
	// カーソル
	wc.hCursor = LoadCursor(nullptr, IDC_ARROW);

	// ウィンドウクラスを登録する
	RegisterClass(&wc);

	//クライアント領域のサイズ
	const int32_t kClientWidth = 1280;
	const int32_t kClientHeight = 720;


	//ウィンドウサイズを表す構造体にクライアント領域を入れる
	RECT wrc = { 0,0,kClientWidth,kClientHeight };

	//クライアント領域を元に実際のサイズにwrcを変更してもらう
	AdjustWindowRect(&wrc, WS_OVERLAPPEDWINDOW, false);

	// ウィンドウの生成
	HWND hwnd = CreateWindow(
		wc.lpszClassName,      // 利用するクラス名
		L"CG2",                // タイトルバーの文字（何でも良い）
		WS_OVERLAPPEDWINDOW,   // よく見るウィンドウスタイル
		CW_USEDEFAULT,         // 表示X座標（Windowsに任せる）
		CW_USEDEFAULT,         // 表示Y座標（WindowsOSに任せる）
		wrc.right - wrc.left,  // ウィンドウ横幅
		wrc.bottom - wrc.top,  // ウィンドウ縦幅
		nullptr,               // 親ウィンドウハンドル
		nullptr,               // メニューハンドル
		wc.hInstance,          // インスタンスハンドル
		nullptr);              // オプション

#ifdef _DEBUG
	ID3D12Debug1* debugController = nullptr;
	if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController)))) {
		// デバッグレイヤーを有効化する
		debugController->EnableDebugLayer();
		// さらにGPU側でもチェックを行うようにする
		debugController->SetEnableGPUBasedValidation(TRUE);
	}
#endif


	// DXGIファクトリーの生成
	IDXGIFactory7* dxgiFactory = nullptr;
	// HRESULTはWindows系のエラーコードであり、
	// 関数が成功したかどうかをSUCCEEDEDマクロで判定できる
	HRESULT hr = CreateDXGIFactory(IID_PPV_ARGS(&dxgiFactory));
	// 初期化の根本的な部分でエラーが出た場合はプログラムが間違っているか、どうにもできない場合が多いのでassertにしておく
	assert(SUCCEEDED(hr));

	//使用するアダプタ用の変数。最初にnullptrを入れておく
	IDXGIAdapter4* useAdapter = nullptr;
	// 良い順にアダプタを頼む
	for (UINT i = 0; dxgiFactory->EnumAdapterByGpuPreference(i,
		DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&useAdapter)) !=
		DXGI_ERROR_NOT_FOUND; ++i) {

		// アダプターの情報を取得する
		DXGI_ADAPTER_DESC3 adapterDesc{};
		hr = useAdapter->GetDesc3(&adapterDesc);
		assert(SUCCEEDED(hr)); // 取得できないのは一大事

		// ソフトウェアアダプタでなければ採用
		if (!(adapterDesc.Flags & DXGI_ADAPTER_FLAG3_SOFTWARE)) {
			//採用したアダプタ情報をログに出力。wstringのほうなので注意
			Log(std::format(L"Use Adapter:{}\n", adapterDesc.Description));
			break;
		}

		useAdapter = nullptr; // ソフトウェアアダプタの場合は見なかったことにする
	}
	// 適切なアダプタが見つからなかったので起動できない
	assert(useAdapter != nullptr);

	ID3D12Device* device = nullptr;
	// 機能レベルとログ出力用の文字列
	D3D_FEATURE_LEVEL featureLevels[] = {
		D3D_FEATURE_LEVEL_12_2, D3D_FEATURE_LEVEL_12_1, D3D_FEATURE_LEVEL_12_0
	};
	const char* featureLevelStrings[] = { "12.2", "12.1", "12.0" };

	// 高い順に生成できるか試していく
	for (size_t i = 0; i < _countof(featureLevels); ++i) {
		// 採用したアダプターでデバイスを生成
		hr = D3D12CreateDevice(useAdapter, featureLevels[i], IID_PPV_ARGS(&device));
		// 指定した機能レベルでデバイスが生成できたかを確認
		if (SUCCEEDED(hr)) {
			// 生成できたのでログ出力を行ってループを抜ける
			Log(std::format("FeatureLevel : {}\n", featureLevelStrings[i]));
			break;
		}
	}

	// デバイスの生成がうまくいかなかったので起動できない
	assert(device != nullptr);
	Log("Complete create D3D12Device!!!\n"); // 初期化完了のログをだす

#ifdef _DEBUG
	ID3D12InfoQueue* infoQueue = nullptr;
	if (SUCCEEDED(device->QueryInterface(IID_PPV_ARGS(&infoQueue)))) {
		// ヤバイエラー時に止まる
		infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, true);
		// エラー時に止まる
		infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, true);

		//リソースのログを知りたいのでコメントアウト
		//// 警告時に止まる
		//infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_WARNING, true);


		// 解放
		infoQueue->Release();

		// 抑制するメッセージのID
		D3D12_MESSAGE_ID denyIds[] = {
			// Windows11でのDXGIデバッグレイヤーとDX12デバッグレイヤーの相互作用バグによるエラーメッセージ
			// https://stackoverflow.com/questions/69805245/directx-12-application-is-crashing-in-windows-11
			D3D12_MESSAGE_ID_RESOURCE_BARRIER_MISMATCHING_COMMAND_LIST_TYPE
		};

		// 抑制するレベル
		D3D12_MESSAGE_SEVERITY severities[] = { D3D12_MESSAGE_SEVERITY_INFO };
		D3D12_INFO_QUEUE_FILTER filter{};
		filter.DenyList.NumIDs = _countof(denyIds);
		filter.DenyList.pIDList = denyIds;
		filter.DenyList.NumSeverities = _countof(severities);
		filter.DenyList.pSeverityList = severities;

		// 指定したメッセージの表示を抑制する
		infoQueue->PushStorageFilter(&filter);
	}
#endif

	//DepthStencilTextureをウィンドウのサイズで生成
	ID3D12Resource* depthStencilResource = CreateDepthStencilTextureResource(device, kClientWidth, kClientHeight);


	// ウィンドウを表示する
	ShowWindow(hwnd, SW_SHOW);

	// コマンドキューを生成する
	ID3D12CommandQueue* commandQueue = nullptr;
	D3D12_COMMAND_QUEUE_DESC commandQueueDesc{};
	hr = device->CreateCommandQueue(&commandQueueDesc, IID_PPV_ARGS(&commandQueue));

	// コマンドキューの生成がうまくいかなかったので起動できない
	assert(SUCCEEDED(hr));

	// コマンドアロケータを生成する
	ID3D12CommandAllocator* commandAllocator = nullptr;
	hr = device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&commandAllocator));
	// コマンドアロケータの生成がうまくいかなかったので起動できない
	assert(SUCCEEDED(hr));

	// コマンドリストを生成する
	ID3D12GraphicsCommandList* commandList = nullptr;
	hr = device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, commandAllocator, nullptr,
		IID_PPV_ARGS(&commandList));
	// コマンドリストの生成がうまくいかなかったので起動できない
	assert(SUCCEEDED(hr));

	// スワップチェーンを生成する
	IDXGISwapChain4* swapChain = nullptr;
	DXGI_SWAP_CHAIN_DESC1 swapChainDesc{};
	swapChainDesc.Width = kClientWidth;  // 画面の幅。ウィンドウのクライアント領域を同じものにしておく
	swapChainDesc.Height = kClientHeight;  // 画面の高さ。ウィンドウのクライアント領域を同じものにしておく
	swapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;  // 色の形式
	swapChainDesc.SampleDesc.Count = 1;  // マルチサンプルしない
	swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;  // 描画のターゲットとして利用する
	swapChainDesc.BufferCount = 2;  // ダブルバッファ
	swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;  // モニタにうつしたら、中身を破棄

	// コマンドキュー、ウィンドウハンドル、設定を渡して生成する
	hr = dxgiFactory->CreateSwapChainForHwnd(commandQueue, hwnd, &swapChainDesc, nullptr, nullptr, reinterpret_cast<IDXGISwapChain1**>(&swapChain));
	assert(SUCCEEDED(hr));

	// ディスクリプタヒープの生成
	ID3D12DescriptorHeap* rtvDescriptorHeap = CreateDescriptorHeap(device, D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 2, false);

	//SRV用のヒープでディスクリプタの数は128。SRVはshader内で触るものなので、shederVisibleはture
	ID3D12DescriptorHeap* srvDescriptorHeap = CreateDescriptorHeap(device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 128, true);

	//DSV用のヒープでディスクリプタの数は1。DSVはShader内で触るものではないので、ShaderVisibleはfalse
	ID3D12DescriptorHeap* dsvDescriptorHeap = CreateDescriptorHeap(device, D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 1, false);

	//DescriptorSizeを取得しておく
	const uint32_t descriptorSizeSRV = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	const uint32_t descriptorSizeRTV = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
	const uint32_t descriptorSizeDSV = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);

	//DSVの設定
	D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc{};
	dsvDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
	dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
	//DSVHeapの先頭にDSVを作る
	device->CreateDepthStencilView(depthStencilResource, &dsvDesc, GetCPUDescriptorHandle(dsvDescriptorHeap,descriptorSizeDSV,0));

	//DepthStencilStateの設定
	D3D12_DEPTH_STENCIL_DESC depthStencilDesc{};
	//Depth機能を有効化する
	depthStencilDesc.DepthEnable = true;
	//書き込みします
	depthStencilDesc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
	//比較関数はLessEqual。つまり、近ければ描画される
	depthStencilDesc.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;

	// SwapChainからResourceを引っ張ってくる
	ID3D12Resource* swapChainResources[2] = { nullptr };
	hr = swapChain->GetBuffer(0, IID_PPV_ARGS(&swapChainResources[0]));
	// うまく取得できなければ起動できない
	assert(SUCCEEDED(hr));
	hr = swapChain->GetBuffer(1, IID_PPV_ARGS(&swapChainResources[1]));
	assert(SUCCEEDED(hr));

	// RTVの設定
	D3D12_RENDER_TARGET_VIEW_DESC rtvDesc{};
	rtvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;  // 出力結果をSRGBに変換して書き込む
	rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;  // 2dテクスチャとして書き込む

	// ディスクリプタの先頭を取得する
	D3D12_CPU_DESCRIPTOR_HANDLE rtvStartHandle = GetCPUDescriptorHandle(rtvDescriptorHeap,descriptorSizeRTV,0);

	// RTVを2つ作るのでディスクリプタを2つ用意
	D3D12_CPU_DESCRIPTOR_HANDLE rtvHandles[2];

	// まず1つ目を作る。1つ目は最初のところに作る。格納場所をこちらで指定してあげる必要がある
	rtvHandles[0] = rtvStartHandle;
	device->CreateRenderTargetView(swapChainResources[0], &rtvDesc, rtvHandles[0]);

	// 2つ目のディスクリプタハンドルを得る（自力で）
	rtvHandles[1].ptr = rtvHandles[0].ptr + device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

	// 2つ目を作る
	device->CreateRenderTargetView(swapChainResources[1], &rtvDesc, rtvHandles[1]);



	// 初期値0でFenceを作る
	ID3D12Fence* fence = nullptr;
	uint64_t fenceValue = 0;
	hr = device->CreateFence(fenceValue, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence));
	assert(SUCCEEDED(hr));

	// FenceのSignalを待つためのイベントを作成する
	HANDLE fenceEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
	assert(fenceEvent != nullptr);

	// dxcCompilerを初期化
	IDxcUtils* dxcUtils = nullptr;
	IDxcCompiler3* dxcCompiler = nullptr;
	hr = DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(&dxcUtils));
	assert(SUCCEEDED(hr));
	hr = DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&dxcCompiler));
	assert(SUCCEEDED(hr));

	// 現時点でincludeはしないが、includeに対応するための設定を行っておく
	IDxcIncludeHandler* includeHandler = nullptr;
	hr = dxcUtils->CreateDefaultIncludeHandler(&includeHandler);
	assert(SUCCEEDED(hr));

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

	D3D12_STATIC_SAMPLER_DESC staticSamplers[1] = {};
	staticSamplers[0].Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
	staticSamplers[0].AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	staticSamplers[0].AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	staticSamplers[0].AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	staticSamplers[0].ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
	staticSamplers[0].MaxLOD = D3D12_FLOAT32_MAX;
	staticSamplers[0].ShaderRegister = 0;
	staticSamplers[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	descriptionRootSignature.pStaticSamplers = staticSamplers;
	descriptionRootSignature.NumStaticSamplers = _countof(staticSamplers);

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
	hr = device->CreateRootSignature(0,
		signatureBlob->GetBufferPointer(), signatureBlob->GetBufferSize(),
		IID_PPV_ARGS(&rootSignature));
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
	IDxcBlob* vertexShaderBlob = CompileShader(L"resources/shaders/Object3D.VS.hlsl",
		L"vs_6_0", dxcUtils, dxcCompiler, includeHandler);
	assert(vertexShaderBlob != nullptr);

	IDxcBlob* pixelShaderBlob = CompileShader(L"resources/shaders/Object3D.PS.hlsl",
		L"ps_6_0", dxcUtils, dxcCompiler, includeHandler);
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
	hr = device->CreateGraphicsPipelineState(&graphicsPipelineStateDesc,
		IID_PPV_ARGS(&graphicsPipelineState));
	assert(SUCCEEDED(hr));

	//Textureを読んで転送する
	DirectX::ScratchImage mipImages = LoadTexture("resources/uvChecker.png");
	const DirectX::TexMetadata& metadata = mipImages.GetMetadata();
	ID3D12Resource* textureResource = CreateTextureResource(device, metadata);
	ID3D12Resource* intermediaResource = UploadTextureData(textureResource, mipImages, device, commandList);

	//二枚目のTextureを読んで転送する
	DirectX::ScratchImage mipImages2 = LoadTexture("resources/monsterBall.png");
	const DirectX::TexMetadata& metadata2 = mipImages2.GetMetadata();
	ID3D12Resource* textureResource2 = CreateTextureResource(device, metadata2);
	ID3D12Resource* intermediaResource2 = UploadTextureData(textureResource2, mipImages2, device, commandList);

	//マテリアル用のリソースを作る。今回はカラー1つ分のサイズを用意する
	ID3D12Resource* materialResource = CreateBufferResource(device, sizeof(Material));
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
	ID3D12Resource* materialResourceSprite = CreateBufferResource(device, sizeof(Material));
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
	ID3D12Resource* directionalLightResource = CreateBufferResource(device, sizeof(DirectionalLight));
	//データを書き込む
	DirectionalLight* directionalLightData = nullptr;
	//書き込むためのアドレス取得
	directionalLightResource->Map(0, nullptr, reinterpret_cast<void**>(&directionalLightData));
	//平行光源の設定
	directionalLightData->color = { 1.0f,1.0f,1.0f,1.0f };
	directionalLightData->direction = { 0.0f,-1.0f,0.0f };
	directionalLightData->intensity = 1.0f;

	ID3D12Resource* indexResourceSprite = CreateBufferResource(device, sizeof(uint32_t) * 6);

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
	ID3D12Resource* wvpResource = CreateBufferResource(device, sizeof(TransformationMatrix));
	//データを書き込む
	TransformationMatrix* wvpData = nullptr;
	//書き込むためのアドレス取得
	wvpResource->Map(0, nullptr, reinterpret_cast<void**>(&wvpData));
	//単位行列を書き込んでおく
	wvpData->World = MakeIdentity4x4();

	//WVP用リソースのリソースを作る。Matrix4x4 1つ分のサイズを用意する
	ID3D12Resource* transformationMatrixResourceSprite = CreateBufferResource(device, sizeof(TransformationMatrix));
	//データを書き込む
	TransformationMatrix* transformationMatrixDataSprite = nullptr;
	//書き込むためのアドレス取得
	transformationMatrixResourceSprite->Map(0, nullptr, reinterpret_cast<void**>(&transformationMatrixDataSprite));
	//単位行列を書き込んでおく
	transformationMatrixDataSprite->WVP =  MakeIdentity4x4();

	//分割数
	const int kSubdivision = 16;
	const float kLonEvery = float(M_PI) * 2.0f / float(kSubdivision); // 経度の１分割あたりの角度
	const float kLatEvery = float(M_PI) / float(kSubdivision); // 緯度の１分割あたりの角度

	// 実際に頂点リソースを作る
	const int vertexCount = kSubdivision * kSubdivision * 6;
	ID3D12Resource* vertexResource = CreateBufferResource(device, sizeof(VertexData) * vertexCount);
	// 頂点バッファビューを作成する
	D3D12_VERTEX_BUFFER_VIEW vertexBufferView{};
	// リソースの先頭のアドレスから使う
	vertexBufferView.BufferLocation = vertexResource->GetGPUVirtualAddress();
	// 使用するリソースのサイズは頂点3つ分のサイズ
	vertexBufferView.SizeInBytes = sizeof(VertexData) * vertexCount;
	// 1頂点あたりのサイズ
	vertexBufferView.StrideInBytes = sizeof(VertexData);

	//Sprite用の頂点リソースを作る
	ID3D12Resource* vertexResourceSprite = CreateBufferResource(device, sizeof(VertexData) * 6);
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

	int index = 0;
	for (int latIndex = 0; latIndex < kSubdivision; ++latIndex) {
		// lat0: 現在の緯度、lat1: 次の緯度
		float lat0 = -float(M_PI) / 2.0f + kLatEvery * float(latIndex);
		float lat1 = -float(M_PI) / 2.0f + kLatEvery * float(latIndex + 1);

		for (int lonIndex = 0; lonIndex < kSubdivision; ++lonIndex) {
			// 経度方向も同じく分割
			float lon0 = kLonEvery * float(lonIndex);
			float lon1 = kLonEvery * float(lonIndex + 1);

			// 「start」を (latIndex, lonIndex) によって計算
			// 1 つのクワッドにつき頂点 6 つ分のオフセットを使う
			uint32_t start = (latIndex * kSubdivision + lonIndex) * 6;

			// ── クワッドを構成する 4 つの頂点をワールド座標上で計算 ──
			//  頂点 p0: (lat0, lon0)
			Vector3 p0;
			p0.x = std::cosf(lat0) * std::cosf(lon0);
			p0.y = std::sinf(lat0);
			p0.z = std::cosf(lat0) * std::sinf(lon0);

			//  頂点 p1: (lat1, lon0)
			Vector3 p1;
			p1.x = std::cosf(lat1) * std::cosf(lon0);
			p1.y = std::sinf(lat1);
			p1.z = std::cosf(lat1) * std::sinf(lon0);

			//  頂点 p2: (lat0, lon1)
			Vector3 p2;
			p2.x = std::cosf(lat0) * std::cosf(lon1);
			p2.y = std::sinf(lat0);
			p2.z = std::cosf(lat0) * std::sinf(lon1);

			//  頂点 p3: (lat1, lon1)
			Vector3 p3;
			p3.x = std::cosf(lat1) * std::cosf(lon1);
			p3.y = std::sinf(lat1);
			p3.z = std::cosf(lat1) * std::sinf(lon1);

			// ── テクスチャ座標 (u,v) を [0,1] にマッピング ──
			//  経度方向は lonIndex／kSubdivision、緯度方向は latIndex／kSubdivision を用いる
			float u0 = float(lonIndex) / float(kSubdivision);
			float u1 = float(lonIndex + 1) / float(kSubdivision);
			// v は「南極(-π/2)→北極(+π/2)」を [0,1] に対応させるので、1.0-（latIndex/分割数）とする
			float v0 = 1.0f - float(latIndex) / float(kSubdivision);
			float v1 = 1.0f - float(latIndex + 1) / float(kSubdivision);

			// ────────────────────────────────────────────────────────────────────
			// 三角形１： (p0, p1, p2)
			// ────────────────────────────────────────────────────────────────────
			// 頂点 0: p0
			vertexData[start + 0].position = { p0.x, p0.y, p0.z, 1.0f };
			vertexData[start + 0].texcoord = { u0, v0 };
			// 頂点 1: p1
			vertexData[start + 1].position = { p1.x, p1.y, p1.z, 1.0f };
			vertexData[start + 1].texcoord = { u0, v1 };
			// 頂点 2: p2
			vertexData[start + 2].position = { p2.x, p2.y, p2.z, 1.0f };
			vertexData[start + 2].texcoord = { u1, v0 };

			// ────────────────────────────────────────────────────────────────────
			// 三角形２： (p2, p1, p3)
			// ────────────────────────────────────────────────────────────────────
			// 頂点 3: p2（再利用）
			vertexData[start + 3].position = { p2.x, p2.y, p2.z, 1.0f };
			vertexData[start + 3].texcoord = { u1, v0 };
			// 頂点 4: p1（再利用）
			vertexData[start + 4].position = { p1.x, p1.y, p1.z, 1.0f };
			vertexData[start + 4].texcoord = { u0, v1 };
			// 頂点 5: p3
			vertexData[start + 5].position = { p3.x, p3.y, p3.z, 1.0f };
			vertexData[start + 5].texcoord = { u1, v1 };

			// ※ index を使ったインクリメントは不要。start で直接書き込んでいるので、
			//    もし index 変数を使う場合は「index += 6;」するか、
			//    コメントアウトした以下のようなチェックを行ってもよいです。
			// index += 6;

			for (int index = 0; index < 6; index++) {
				vertexData[start + index].normal.x = vertexData[start + index].position.x;
				vertexData[start + index].normal.y = vertexData[start + index].position.y;
				vertexData[start + index].normal.z = vertexData[start + index].position.z;
			}
		}
	}

	// 必要ならチェック
	// assert(index == vertexCount);

	vertexResource->Unmap(0, nullptr);


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

	// ビューポート
	D3D12_VIEWPORT viewport{};
	// クライアント領域のサイズと一緒にして画面全体に表示
	viewport.Width = kClientWidth;
	viewport.Height = kClientHeight;
	viewport.TopLeftX = 0;
	viewport.TopLeftY = 0;
	viewport.MinDepth = 0.0f;
	viewport.MaxDepth = 1.0f;

	// シザー矩形
	D3D12_RECT scissorRect{};
	// 基本的にビューポートと同じ矩形が構成されるようにする
	scissorRect.left = 0;
	scissorRect.right = kClientWidth;
	scissorRect.top = 0;
	scissorRect.bottom = kClientHeight;

	//Transform変数を作る
	Transform transform = { {1.0f,1.0f,1.0f},{0.0f,0.0f,0.0f},{0.0f,0.0f,0.0f} };
	Transform cameraTransform = { {1.0f,1.0f,1.0f},{0.0f,0.0f,0.0f},{0.0f,0.0f,-5.0f} };
	Transform transformSprite = { {1.0f,1.0f,1.0f},{0.0f,0.0f,0.0f},{0.0f,0.0f,0.0f} };

	Transform uvTransformSprite{
		{1.0f,1.0f,1.0f},
		{0.0f,0.0f,0.0f},
		{0.0f,0.0f,0.0f},
	};

	bool isDrawSprite = true;
	bool useMonsterBall = true;

	//ImGuiの初期化
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGui::StyleColorsDark();
	ImGui_ImplWin32_Init(hwnd);
	ImGui_ImplDX12_Init(device,
		swapChainDesc.BufferCount,
		rtvDesc.Format,
		srvDescriptorHeap,
		srvDescriptorHeap->GetCPUDescriptorHandleForHeapStart(),
		srvDescriptorHeap->GetGPUDescriptorHandleForHeapStart());

	//metaDataを基にSRVの設定
	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
	srvDesc.Format = metadata.format;
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MipLevels = UINT(metadata.mipLevels);

	//SRVを作成するDescriptorHeapの設定
	D3D12_CPU_DESCRIPTOR_HANDLE textureSrvHandleCPU = GetCPUDescriptorHandle(srvDescriptorHeap,descriptorSizeSRV,0);
	D3D12_GPU_DESCRIPTOR_HANDLE textureSrvHandleGPU = GetGPUDescriptorHandle(srvDescriptorHeap, descriptorSizeSRV, 0);
	//先頭はImGuiが使っているのでその次を使う
	textureSrvHandleCPU.ptr += device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	textureSrvHandleGPU.ptr += device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	//SRVの作成
	device->CreateShaderResourceView(textureResource, &srvDesc, textureSrvHandleCPU);

	//metaDataを基に二個目のSRVの設定
	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc2{};
	srvDesc2.Format = metadata2.format;
	srvDesc2.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc2.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc2.Texture2D.MipLevels = UINT(metadata2.mipLevels);

	//SRVを作成するDescriptorHeapの設定
	D3D12_CPU_DESCRIPTOR_HANDLE textureSrvHandleCPU2 = GetCPUDescriptorHandle(srvDescriptorHeap, descriptorSizeSRV, 2);
	D3D12_GPU_DESCRIPTOR_HANDLE textureSrvHandleGPU2 = GetGPUDescriptorHandle(srvDescriptorHeap, descriptorSizeSRV, 2);

	device->CreateShaderResourceView(textureResource2, &srvDesc2, textureSrvHandleCPU2);

	MSG msg{};
	// ウィンドウの×ボタンが押されるまでループ
	while (msg.message != WM_QUIT) {

		ImGui_ImplDX12_NewFrame();
		ImGui_ImplWin32_NewFrame();
		ImGui::NewFrame();

		// Windowにメッセージが来てたら最優先で処理させる
		while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
			TranslateMessage(&msg);
			DispatchMessage(&msg);
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

		directionalLightData->direction = Normalize(directionalLightData->direction);

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


		//ゲーム処理
		/*transform.rotate.y += 0.03f;*/
		Matrix4x4 worldMatrix = MakeAffineMatrix(transform.scale, transform.rotate, transform.translate);
		Matrix4x4 cameraMatrix = MakeAffineMatrix(cameraTransform.scale, cameraTransform.rotate, cameraTransform.translate);
		Matrix4x4 viewMatrix = Inverse(cameraMatrix);
		Matrix4x4 projectionMatrix = MakePerspectiveFovMatrix(0.45f, float(kClientWidth) / float(kClientHeight), 0.1f, 100.0f);
		Matrix4x4 worldViewProjectionMatrix = Multiply(worldMatrix, Multiply(viewMatrix, projectionMatrix));

		wvpData->WVP = worldViewProjectionMatrix;
		wvpData->World = worldMatrix;

		Matrix4x4 worldMatrixSprite = MakeAffineMatrix(transformSprite.scale, transformSprite.rotate, transformSprite.translate);
		Matrix4x4 viewMatrixSprite = MakeIdentity4x4();
		Matrix4x4 projectionMatrixSprite = MakeOrthographicMatrix(0.0f, 0.0f, float(kClientWidth), float(kClientHeight), 0.0f, 100.0f);
		Matrix4x4 worldViewProjectionMatrixSprite = Multiply(worldMatrixSprite, Multiply(viewMatrixSprite, projectionMatrixSprite));
		
		transformationMatrixDataSprite->WVP =  worldViewProjectionMatrixSprite;
		transformationMatrixDataSprite->World = MakeIdentity4x4();

		Matrix4x4 uvTransformMatrix = MakeScaleMatrix(uvTransformSprite.scale);
		uvTransformMatrix = Multiply(uvTransformMatrix, MakeRotateZMatrix(uvTransformSprite.rotate.z));
		uvTransformMatrix = Multiply(uvTransformMatrix, MakeTranslateMatrix(uvTransformSprite.translate));
		materialDataSprite->uvTransform = uvTransformMatrix;

		ImGui::Render();

		// これから書き込むバックバッファのインデックスを取得
		UINT backBufferIndex = swapChain->GetCurrentBackBufferIndex();

		// TransitionBarrierの設定
		D3D12_RESOURCE_BARRIER barrier{};
		// 今回のバリアはTransition
		barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		// Noneにしておく
		barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
		// バリアを張る対象のリソース。現在のバックバッファに対して行う
		barrier.Transition.pResource = swapChainResources[backBufferIndex];
		// 遷移前（現在）のResourceState
		barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
		// 遷移後のResourceState
		barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
		// TransitionBarrierを張る
		commandList->ResourceBarrier(1, &barrier);

		// 描画先のRTVとDSVを設定する
		D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = GetCPUDescriptorHandle(dsvDescriptorHeap,descriptorSizeDSV,0);
		commandList->OMSetRenderTargets(1, &rtvHandles[backBufferIndex], false, &dsvHandle);
		/*commandList->OMSetRenderTargets(1, &rtvHandles[backBufferIndex], false, nullptr);*/

		commandList->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

		// 指定した色で画面全体をクリアする
		float clearColor[] = { 0.1f, 0.25f, 0.5f, 1.0f };  // 青っぽい色。RGBAの順
		commandList->ClearRenderTargetView(rtvHandles[backBufferIndex], clearColor, 0, nullptr);

		//ImGui描画用のDescriptorHeapの設定
		ID3D12DescriptorHeap* descriptorHeaps[] = { srvDescriptorHeap };
		commandList->SetDescriptorHeaps(1, descriptorHeaps);

		commandList->RSSetViewports(1, &viewport);           // Viewportを設定
		commandList->RSSetScissorRects(1, &scissorRect);     // Scissorを設定

		// RootSignatureを設定。PSOに設定しているけど別途設定も必要
		commandList->SetGraphicsRootSignature(rootSignature);
		commandList->SetPipelineState(graphicsPipelineState); // PSOを設定

		commandList->IASetVertexBuffers(0, 1, &vertexBufferView); // VBVを設定

		commandList->SetGraphicsRootConstantBufferView(0, materialResource->GetGPUVirtualAddress());

		commandList->SetGraphicsRootConstantBufferView(1, wvpResource->GetGPUVirtualAddress());

		commandList->SetGraphicsRootConstantBufferView(3, directionalLightResource->GetGPUVirtualAddress());

		commandList->SetGraphicsRootDescriptorTable(2, useMonsterBall ? textureSrvHandleGPU2 : textureSrvHandleGPU);

		// 形状を設定。PSOに設定しているものとは本来別。同じものを設定することを考えておけば良い
		commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

		// 描画！（DrawCall／ドローコール）。3頂点で1つのインスタンス。インスタンスについては今後
		commandList->DrawInstanced(vertexCount, 1, 0, 0);

		//Spriteの描画。変更が必要なものだけ変更する
		commandList->IASetVertexBuffers(0, 1, &vertexBufferViewSprite); // VBVを設定

		commandList->IASetIndexBuffer(&indexBufferViewSprite); // VBVを設定

		//TransformationMatrixCBufferの場所を限定
		commandList->SetGraphicsRootConstantBufferView(1, transformationMatrixResourceSprite->GetGPUVirtualAddress());

		commandList->SetGraphicsRootDescriptorTable(2, textureSrvHandleGPU);

		commandList->SetGraphicsRootConstantBufferView(0, materialResourceSprite->GetGPUVirtualAddress());

		if (isDrawSprite) {
			//描画
			commandList->DrawIndexedInstanced(6, 1, 0, 0, 0);
		}

		ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), commandList);

		// 画面に描く処理はすべて終わり、画面に映すので、状態を遷移
		// 今回はRenderTargetからPresentにする
		barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
		barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
		// TransitionBarrierを張る
		commandList->ResourceBarrier(1, &barrier);

		// コマンドリストの内容を確定させる。すべてのコマンドを積んでからCloseすること
		hr = commandList->Close();
		assert(SUCCEEDED(hr));

		// GPUにコマンドリストの実行を行わせる
		ID3D12CommandList* commandLists[] = { commandList };
		commandQueue->ExecuteCommandLists(1, commandLists);

		// GPUとOSに画面の交換を行うよう通知する
		swapChain->Present(1, 0);

		// Fenceの値を更新
		fenceValue++;
		// GPUがここまでたどり着いたときに、Fenceの値を指定した値に代入するようにSignalを送る
		commandQueue->Signal(fence, fenceValue);

		// Fenceの値が指定したSignal値にたどり着いているか確認する
		 // GetCompletedValueの初期値はFence作成時に渡した初期値
		if (fence->GetCompletedValue() < fenceValue)
		{
			// 指定したSignalにたどりついていないので、たどり着くまで待つようにイベントを設定する
			fence->SetEventOnCompletion(fenceValue, fenceEvent);
			// イベント待つ
			WaitForSingleObject(fenceEvent, INFINITE);
		}

		// 次のフレーム用のコマンドリストを準備
		hr = commandAllocator->Reset();
		assert(SUCCEEDED(hr));

		hr = commandList->Reset(commandAllocator, nullptr);
		assert(SUCCEEDED(hr));


	}

	//ImGuiの終了
	ImGui_ImplDX12_Shutdown();
	ImGui_ImplWin32_Shutdown();
	ImGui::DestroyContext();

	CoUninitialize();

	//出力ウィンドウへの文字出力
	OutputDebugStringA("Hello,DirectX!\n");

	CloseHandle(fenceEvent);
	fence->Release();
	rtvDescriptorHeap->Release();
	swapChainResources[0]->Release();
	swapChainResources[1]->Release();
	swapChain->Release();
	commandList->Release();
	commandAllocator->Release();
	commandQueue->Release();
	device->Release();
	useAdapter->Release();
	dxgiFactory->Release();
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
	intermediaResource->Release();

#ifdef _DEBUG
	debugController->Release();
#endif
	CloseWindow(hwnd);


	// リソースリークチェック
	IDXGIDebug1* debug;
	if (SUCCEEDED(DXGIGetDebugInterface1(0, IID_PPV_ARGS(&debug)))) {
		debug->ReportLiveObjects(DXGI_DEBUG_ALL, DXGI_DEBUG_RLO_ALL);
		debug->ReportLiveObjects(DXGI_DEBUG_APP, DXGI_DEBUG_RLO_ALL);
		debug->ReportLiveObjects(DXGI_DEBUG_D3D12, DXGI_DEBUG_RLO_ALL);
		debug->Release();
	}


	return 0;
}