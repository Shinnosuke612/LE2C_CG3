// engine/base/ImGuiManager.cpp
#include "ImGuiManager.h"

#include <cassert>

#include "WinApp.h"
#include "DirectXCommon.h"
#include "../3d/SrvManager.h"

#include "../../externals/imgui/imgui.h"
#include "../../externals/imgui/imgui_impl_win32.h"
#include "../../externals/imgui/imgui_impl_dx12.h"

void ImGuiManager::Initialize(WinApp* winApp, DirectXCommon* dxCommon, SrvManager* srvManager){
	assert(winApp);
	assert(dxCommon);
	assert(srvManager);

	winApp_ = winApp;
	dxCommon_ = dxCommon;
	srvManager_ = srvManager;

	IMGUI_CHECKVERSION();
	ImGui::CreateContext();

	ImGuiIO& io = ImGui::GetIO();
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
	io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

	ImGui::StyleColorsDark();

	ImGui_ImplWin32_Init(winApp_->GetHwnd());

	ImGui_ImplDX12_InitInfo initInfo{};
	initInfo.Device = dxCommon_->GetDevice();
	initInfo.CommandQueue = dxCommon_->GetCommandQueue();
	initInfo.NumFramesInFlight = dxCommon_->GetSwapChainResourcesNum();
	initInfo.RTVFormat = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
	initInfo.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
	initInfo.SrvDescriptorHeap = srvManager_->GetDescriptorHeap();
	initInfo.UserData = srvManager_;

	initInfo.SrvDescriptorAllocFn =
		[](ImGui_ImplDX12_InitInfo* info,
		   D3D12_CPU_DESCRIPTOR_HANDLE* out_cpu_handle,
		   D3D12_GPU_DESCRIPTOR_HANDLE* out_gpu_handle){
			   SrvManager* srvManager = static_cast<SrvManager*>(info->UserData);
			   assert(srvManager);
			   assert(srvManager->CanAllocate());

			   uint32_t index = srvManager->Allocate();
			   *out_cpu_handle = srvManager->GetCPUDescriptorHandle(index);
			   *out_gpu_handle = srvManager->GetGPUDescriptorHandle(index);
		};

	initInfo.SrvDescriptorFreeFn =
		[](ImGui_ImplDX12_InitInfo*,
		   D3D12_CPU_DESCRIPTOR_HANDLE,
		   D3D12_GPU_DESCRIPTOR_HANDLE){
			   // 今のSrvManagerには解放機能が無いので何もしない
		};

	ImGui_ImplDX12_Init(&initInfo);
}

void ImGuiManager::BeginFrame(){
	ImGui_ImplDX12_NewFrame();
	ImGui_ImplWin32_NewFrame();
	ImGui::NewFrame();

	CreateDockSpace();
}

void ImGuiManager::EndFrame(){
	ImGui::Render();
	ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), dxCommon_->GetCommandList());
}

void ImGuiManager::Finalize(){
	ImGui_ImplDX12_Shutdown();
	ImGui_ImplWin32_Shutdown();
	ImGui::DestroyContext();
}

void ImGuiManager::CreateDockSpace(){
	ImGuiDockNodeFlags dockSpaceFlags = ImGuiDockNodeFlags_PassthruCentralNode;
	ImGui::DockSpaceOverViewport(
		ImGui::GetID("MainDockSpace"),
		ImGui::GetMainViewport(),
		dockSpaceFlags
	);
}
