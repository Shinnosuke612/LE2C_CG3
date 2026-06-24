// engine/base/ImGuiManager.cpp
#include "ImGuiManager.h"

#include <cassert>
#include <algorithm>
#include <cstring>
#include <functional>

#include "WinApp.h"
#include "DirectXCommon.h"
#include "../3d/SrvManager.h"
#include "../2d/TextureManager.h"
#include "../3d/ModelManager.h"
#include "../particle/ParticleEffectResource.h"
#include "../scene/EditorSession.h"
#include "../scene/SceneDocument.h"

#include "../../externals/imgui/imgui.h"
#include "../../externals/imgui/imgui_internal.h"
#include "../../externals/imgui/imgui_impl_win32.h"
#include "../../externals/imgui/imgui_impl_dx12.h"

namespace {
	std::string GetPathRelativeToResources(const std::string& fullPath) {
		const std::string prefix = "resources/";
		if (fullPath.rfind(prefix, 0) == 0) {
			return fullPath.substr(prefix.length());
		}
		return fullPath;
	}
}

ImGuiManager* ImGuiManager::instance = nullptr;

ImGuiManager* ImGuiManager::GetInstance() {
	return instance;
}

bool ImGuiManager::sceneViewInputActive_ = false;

void ImGuiManager::Initialize(WinApp* winApp, DirectXCommon* dxCommon, SrvManager* srvManager){
	instance = this;
	assert(winApp);
	assert(dxCommon);
	assert(srvManager);

	winApp_ = winApp;
	dxCommon_ = dxCommon;
	srvManager_ = srvManager;
	sceneViewWidth_ = dxCommon_->GetClientWidth();
	sceneViewHeight_ = dxCommon_->GetClientHeight();

	IMGUI_CHECKVERSION();
	ImGui::CreateContext();

	ImGuiIO& io = ImGui::GetIO();
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
	io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

	ImGui::StyleColorsDark();
	ImGuiStyle& style = ImGui::GetStyle();
	const float dpiScale =
		static_cast<float>(GetDpiForWindow(winApp_->GetHwnd())) / 96.0f;
	ImFontConfig fontConfig{};
	fontConfig.SizePixels = 13.0f * dpiScale;
	io.Fonts->AddFontDefault(&fontConfig);
	style.ScaleAllSizes(dpiScale);
	style.WindowRounding = 0.0f;
	style.ChildRounding = 0.0f;
	style.FrameRounding = 2.0f;
	style.PopupRounding = 2.0f;
	style.TabRounding = 2.0f;
	style.WindowBorderSize = 1.0f;
	style.FrameBorderSize = 0.0f;
	style.Colors[ImGuiCol_WindowBg] = ImVec4(0.105f, 0.11f, 0.12f, 1.0f);
	style.Colors[ImGuiCol_TitleBg] = ImVec4(0.075f, 0.08f, 0.09f, 1.0f);
	style.Colors[ImGuiCol_TitleBgActive] = ImVec4(0.12f, 0.13f, 0.15f, 1.0f);
	style.Colors[ImGuiCol_Tab] = ImVec4(0.09f, 0.095f, 0.105f, 1.0f);
	style.Colors[ImGuiCol_TabHovered] = ImVec4(0.19f, 0.34f, 0.48f, 1.0f);
	style.Colors[ImGuiCol_TabSelected] = ImVec4(0.14f, 0.24f, 0.33f, 1.0f);
	style.Colors[ImGuiCol_Header] = ImVec4(0.16f, 0.25f, 0.32f, 1.0f);
	style.Colors[ImGuiCol_HeaderHovered] = ImVec4(0.21f, 0.36f, 0.48f, 1.0f);
	style.Colors[ImGuiCol_DockingPreview] = ImVec4(0.24f, 0.54f, 0.82f, 0.7f);

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

void ImGuiManager::DrawEditorWorkspace(
	D3D12_GPU_DESCRIPTOR_HANDLE sceneTexture,
	uint32_t textureWidth,
	uint32_t textureHeight,
	const char* sceneName
) {
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
	ImGui::Begin(
		"Scene",
		nullptr,
		ImGuiWindowFlags_NoScrollbar |
		ImGuiWindowFlags_NoScrollWithMouse
	);

	const ImVec2 availableSize = ImGui::GetContentRegionAvail();
	sceneViewWidth_ = static_cast<uint32_t>(
		(std::max)(availableSize.x, 1.0f)
	);
	sceneViewHeight_ = static_cast<uint32_t>(
		(std::max)(availableSize.y, 1.0f)
	);

	const ImTextureID textureId =
		static_cast<ImTextureID>(sceneTexture.ptr);
	ImGui::Image(
		ImTextureRef(textureId),
		availableSize,
		ImVec2(0.0f, 0.0f),
		ImVec2(1.0f, 1.0f)
	);
	ImVec2 sceneMin = ImGui::GetItemRectMin();

	ImGui::GetWindowDrawList()->AddText(
		ImVec2(sceneMin.x + 16.0f, sceneMin.y + 16.0f),
		IM_COL32(255, 255, 255, 255),
		"Space to change particle assets"
	);

	sceneViewInputActive_ =
		ImGui::IsItemHovered() || ImGui::IsWindowFocused();

	ImGui::End();
	ImGui::PopStyleVar();

	if (showHierarchy_) {
		DrawHierarchyWindow(sceneName);
	}
	if (showInspector_) {
		DrawInspectorWindow();
	}
	if (showProject_) {
		DrawProjectWindow();
	}
	if (showConsole_) {
		DrawConsoleWindow();
	}

	(void)textureWidth;
	(void)textureHeight;
}

void ImGuiManager::Finalize(){
	if (previewSoundData_.pBuffer && Audio::GetInstance()) {
		Audio::GetInstance()->SoundUnload(&previewSoundData_);
	}
	ImGui_ImplDX12_Shutdown();
	ImGui_ImplWin32_Shutdown();
	ImGui::DestroyContext();
	instance = nullptr;
}

void ImGuiManager::CreateDockSpace(){
	const ImGuiViewport* viewport = ImGui::GetMainViewport();
	ImGui::SetNextWindowPos(viewport->WorkPos);
	ImGui::SetNextWindowSize(viewport->WorkSize);
	ImGui::SetNextWindowViewport(viewport->ID);

	const ImGuiWindowFlags windowFlags =
		ImGuiWindowFlags_MenuBar |
		ImGuiWindowFlags_NoDocking |
		ImGuiWindowFlags_NoTitleBar |
		ImGuiWindowFlags_NoCollapse |
		ImGuiWindowFlags_NoResize |
		ImGuiWindowFlags_NoMove |
		ImGuiWindowFlags_NoBringToFrontOnFocus |
		ImGuiWindowFlags_NoNavFocus;

	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
	ImGui::Begin("EditorDockSpace", nullptr, windowFlags);
	ImGui::PopStyleVar(3);

	if (ImGui::BeginMenuBar()) {
		if (ImGui::BeginMenu("Window")) {
			ImGui::MenuItem("Hierarchy", nullptr, &showHierarchy_);
			ImGui::MenuItem("Inspector", nullptr, &showInspector_);
			ImGui::MenuItem("Project", nullptr, &showProject_);
			ImGui::MenuItem("Console", nullptr, &showConsole_);
			ImGui::Separator();
			if (ImGui::MenuItem("Reset Layout")) {
				resetLayout_ = true;
			}
			ImGui::EndMenu();
		}
		DrawPlaybackControls();
		ImGui::EndMenuBar();
	}

	const ImGuiID dockSpaceId = ImGui::GetID("UnityEditorDockSpaceV2");
	if (
		resetLayout_ ||
		ImGui::DockBuilderGetNode(dockSpaceId) == nullptr
	) {
		BuildDefaultLayout();
		resetLayout_ = false;
	}

	ImGui::DockSpace(dockSpaceId, ImVec2(0.0f, 0.0f));
	ImGui::End();
}

void ImGuiManager::DrawPlaybackControls() {
	if (!editorSession_) {
		return;
	}

	ImGui::Separator();
	const EditorPlayState state = editorSession_->GetState();
	if (state == EditorPlayState::Edit) {
		if (ImGui::SmallButton("Play")) {
			editorSession_->Play();
		}
	}
	else {
		if (ImGui::SmallButton("Stop")) {
			editorSession_->Stop();
		}
		ImGui::SameLine();
		if (state == EditorPlayState::Paused) {
			if (ImGui::SmallButton("Resume")) {
				editorSession_->Resume();
			}
		}
		else if (ImGui::SmallButton("Pause")) {
			editorSession_->Pause();
		}
	}

	ImGui::SameLine();
	ImGui::BeginDisabled(!editorSession_->IsEditing());
	const bool saveRequested = ImGui::SmallButton("Save Scene");
	ImGui::EndDisabled();
	const ImGuiIO& io = ImGui::GetIO();
	if (
		editorSession_->IsEditing() &&
		(saveRequested || (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S, false)))
	) {
		editorSession_->Save();
	}

	ImGui::SameLine();
	if (state == EditorPlayState::Playing) {
		ImGui::TextColored(ImVec4(0.35f, 0.85f, 0.4f, 1.0f), "Playing");
	}
	else if (state == EditorPlayState::Paused) {
		ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.25f, 1.0f), "Paused");
	}
	else if (editorSession_->GetEditDocument().IsDirty()) {
		ImGui::TextColored(ImVec4(0.95f, 0.65f, 0.25f, 1.0f), "Unsaved");
	}
	else {
		ImGui::TextDisabled("Edit");
	}
}

void ImGuiManager::BuildDefaultLayout() {
	const ImGuiID dockSpaceId = ImGui::GetID("UnityEditorDockSpaceV2");
	const ImGuiViewport* viewport = ImGui::GetMainViewport();

	ImGui::DockBuilderRemoveNode(dockSpaceId);
	ImGui::DockBuilderAddNode(
		dockSpaceId,
		ImGuiDockNodeFlags_DockSpace
	);
	ImGui::DockBuilderSetNodeSize(dockSpaceId, viewport->WorkSize);

	ImGuiID centerId = dockSpaceId;
	const ImGuiID leftId = ImGui::DockBuilderSplitNode(
		centerId,
		ImGuiDir_Left,
		0.18f,
		nullptr,
		&centerId
	);
	const ImGuiID rightId = ImGui::DockBuilderSplitNode(
		centerId,
		ImGuiDir_Right,
		0.22f,
		nullptr,
		&centerId
	);
	const ImGuiID bottomId = ImGui::DockBuilderSplitNode(
		centerId,
		ImGuiDir_Down,
		0.26f,
		nullptr,
		&centerId
	);

	ImGui::DockBuilderDockWindow("Scene", centerId);
	ImGui::DockBuilderDockWindow("Hierarchy", leftId);
	ImGui::DockBuilderDockWindow("Inspector", rightId);
	ImGui::DockBuilderDockWindow("Scene Controls", rightId);
	ImGui::DockBuilderDockWindow("Title Scene", rightId);
	ImGui::DockBuilderDockWindow("Light Manager", rightId);
	ImGui::DockBuilderDockWindow("Particle Effect Editor", rightId);
	ImGui::DockBuilderDockWindow("Environment", rightId);
	ImGui::DockBuilderDockWindow("Post Process Stack", rightId);
	ImGui::DockBuilderDockWindow("Lightning", rightId);
	ImGui::DockBuilderDockWindow("Scene Particles", rightId);
	ImGui::DockBuilderDockWindow("Project", bottomId);
	ImGui::DockBuilderDockWindow("Console", bottomId);
	ImGui::DockBuilderFinish(dockSpaceId);
}

void ImGuiManager::DrawHierarchyWindow(const char* sceneName) {
	ImGui::Begin("Hierarchy", &showHierarchy_);
	if (editorSession_) {
		SceneDocument& document = editorSession_->GetActiveDocument();
		ImGui::BeginDisabled(!editorSession_->IsEditing());
		bool createRequested = false;
		uint64_t createParentId = 0;
		if (ImGui::SmallButton("+")) {
			createRequested = true;
		}
		if (ImGui::IsItemHovered()) {
			ImGui::SetTooltip("Create Empty Entity");
		}
		ImGui::EndDisabled();
		ImGui::SameLine();
		ImGui::TextDisabled("%zu entities", document.GetEntities().size());
		ImGui::Separator();

		uint64_t removeId = 0;
		uint64_t duplicateId = 0;
		uint64_t reparentId = 0;
		uint64_t reparentTargetId = 0;
		uint64_t moveId = 0;
		int moveDirection = 0;
		if (
			editorSession_->IsEditing() &&
			selectedEntityId_ != 0 &&
			ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
			!ImGui::GetIO().WantTextInput
		) {
			if (ImGui::IsKeyPressed(ImGuiKey_Delete, false)) {
				removeId = selectedEntityId_;
			}
			if (
				ImGui::GetIO().KeyCtrl &&
				ImGui::IsKeyPressed(ImGuiKey_D, false)
			) {
				duplicateId = selectedEntityId_;
			}
		}

		auto acceptEntityDrop = [&](uint64_t parentId) {
			if (ImGui::BeginDragDropTarget()) {
				if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(
					"SCENE_ENTITY_ID"
				)) {
					if (payload->DataSize == sizeof(uint64_t)) {
						std::memcpy(
							&reparentId,
							payload->Data,
							sizeof(uint64_t)
						);
						reparentTargetId = parentId;
					}
				}
				ImGui::EndDragDropTarget();
			}
		};

		std::function<void(uint64_t)> drawEntity;
		drawEntity = [&](uint64_t entityId) {
			const SceneEntity* entity = document.FindEntity(entityId);
			if (!entity) {
				return;
			}
			const bool hasChildren = std::any_of(
				document.GetEntities().begin(),
				document.GetEntities().end(),
				[entityId](const SceneEntity& candidate) {
					return candidate.parentId == entityId;
				}
			);

			ImGui::PushID(static_cast<int>(entity->id));
			ImGuiTreeNodeFlags flags =
				ImGuiTreeNodeFlags_OpenOnArrow |
				ImGuiTreeNodeFlags_SpanAvailWidth;
			if (!hasChildren) {
				flags |= ImGuiTreeNodeFlags_Leaf |
					ImGuiTreeNodeFlags_NoTreePushOnOpen;
			}
			if (selectedEntityId_ == entity->id) {
				flags |= ImGuiTreeNodeFlags_Selected;
			}
			const std::string label = entity->active
				? entity->name
				: entity->name + " (inactive)";
			const bool open = ImGui::TreeNodeEx(
				"##Entity",
				flags,
				"%s",
				label.c_str()
			);
			if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) {
				selectedEntityId_ = entity->id;
				selectedProjectFile_.clear();
			}

			if (editorSession_->IsEditing() && ImGui::BeginDragDropSource()) {
				const uint64_t draggedId = entity->id;
				ImGui::SetDragDropPayload(
					"SCENE_ENTITY_ID",
					&draggedId,
					sizeof(draggedId)
				);
				ImGui::TextUnformatted(entity->name.c_str());
				ImGui::EndDragDropSource();
			}
			if (editorSession_->IsEditing()) {
				acceptEntityDrop(entity->id);
			}

			if (
				editorSession_->IsEditing() &&
				ImGui::BeginPopupContextItem("EntityContext")
			) {
				if (ImGui::MenuItem("Create Child")) {
					createRequested = true;
					createParentId = entity->id;
				}
				if (ImGui::MenuItem("Duplicate")) {
					duplicateId = entity->id;
				}
				ImGui::Separator();
				if (ImGui::MenuItem("Move Up")) {
					moveId = entity->id;
					moveDirection = -1;
				}
				if (ImGui::MenuItem("Move Down")) {
					moveId = entity->id;
					moveDirection = 1;
				}
				ImGui::Separator();
				if (ImGui::MenuItem("Delete")) {
					removeId = entity->id;
				}
				ImGui::EndPopup();
			}

			if (hasChildren && open) {
				for (const SceneEntity& child : document.GetEntities()) {
					if (child.parentId == entity->id) {
						drawEntity(child.id);
					}
				}
				ImGui::TreePop();
			}
			ImGui::PopID();
		};

		const char* rootName = document.GetSceneName().empty()
			? (sceneName && sceneName[0] != '\0' ? sceneName : "Scene")
			: document.GetSceneName().c_str();
		ImGui::SetNextItemOpen(true, ImGuiCond_Once);
		if (ImGui::TreeNodeEx(
			"##SceneRoot",
			ImGuiTreeNodeFlags_DefaultOpen |
				ImGuiTreeNodeFlags_OpenOnArrow |
				ImGuiTreeNodeFlags_SpanAvailWidth,
			"%s",
			rootName
		)) {
			if (editorSession_->IsEditing()) {
				acceptEntityDrop(0);
			}
			for (const SceneEntity& entity : document.GetEntities()) {
				if (entity.parentId == 0) {
					drawEntity(entity.id);
				}
			}
			ImGui::TreePop();
		}

		if (reparentId != 0) {
			document.SetParent(reparentId, reparentTargetId);
		}
		if (moveId != 0) {
			document.MoveEntity(moveId, moveDirection);
		}
		if (removeId != 0) {
			if (
				selectedEntityId_ == removeId ||
				document.IsDescendantOf(selectedEntityId_, removeId)
			) {
				selectedEntityId_ = 0;
			}
			document.RemoveEntity(removeId);
		}
		if (duplicateId != 0) {
			selectedEntityId_ = document.DuplicateEntity(duplicateId);
		}
		if (createRequested) {
			SceneEntity& entity = document.CreateEntity("Entity", createParentId);
			selectedEntityId_ = entity.id;
			selectedProjectFile_.clear();
		}
		ImGui::End();
		return;
	}

	const char* rootName = sceneName && sceneName[0] != '\0'
		? sceneName
		: "Scene";

	ImGui::SetNextItemOpen(true, ImGuiCond_Once);
	if (ImGui::TreeNode(rootName)) {
		const char* items[] = {
			"Main Camera",
			"Environment",
			"Scene Objects",
			"Lights",
			"Effects"
		};
		for (int index = 0; index < IM_ARRAYSIZE(items); ++index) {
			if (ImGui::Selectable(
				items[index],
				selectedHierarchyItem_ == index
			)) {
				selectedHierarchyItem_ = index;
			}
		}
		ImGui::TreePop();
	}
	ImGui::End();
}

void ImGuiManager::DrawInspectorWindow() {
	ImGui::Begin("Inspector", &showInspector_);

	if (!selectedProjectFile_.empty()) {
		if (ImGui::Button("Back to Hierarchy Selection")) {
			selectedProjectFile_.clear();
			ImGui::End();
			return;
		}
		ImGui::Separator();

		std::filesystem::path path(selectedProjectFile_);
		std::string fileName = path.filename().string();
		std::string ext = path.extension().string();
		std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

		ImGui::Text("Asset Name: %s", fileName.c_str());
		ImGui::Text("Path: %s", selectedProjectFile_.c_str());

		std::error_code ec;
		auto fileSize = std::filesystem::file_size(path, ec);
		if (!ec) {
			if (fileSize < 1024) {
				ImGui::Text("Size: %llu Bytes", fileSize);
			} else if (fileSize < 1024 * 1024) {
				ImGui::Text("Size: %.2f KB", fileSize / 1024.0f);
			} else {
				ImGui::Text("Size: %.2f MB", fileSize / (1024.0f * 1024.0f));
			}
		}
		ImGui::Separator();

		// Check for specific file types
		if (ext == ".png" || ext == ".dds") {
			// Texture asset inspector
			bool isLoaded = TextureManager::GetInstance() && TextureManager::GetInstance()->HasTexture(selectedProjectFile_);
			if (isLoaded) {
				ImGui::Text("Status: Loaded in memory");
				
				const auto& metadata = TextureManager::GetInstance()->GetMetaData(selectedProjectFile_);
				ImGui::Text("Width: %zu px", metadata.width);
				ImGui::Text("Height: %zu px", metadata.height);
				ImGui::Text("Mip Levels: %zu", metadata.mipLevels);
				
				// Render thumbnail
				D3D12_GPU_DESCRIPTOR_HANDLE handle = TextureManager::GetInstance()->GetSrvHandleGPU(selectedProjectFile_);
				const ImTextureID textureId = static_cast<ImTextureID>(handle.ptr);
				
				float aspect = static_cast<float>(metadata.width) / static_cast<float>(metadata.height);
				float drawWidth = 150.0f;
				float drawHeight = drawWidth / (aspect > 0.0f ? aspect : 1.0f);
				if (drawHeight > 150.0f) {
					drawHeight = 150.0f;
					drawWidth = drawHeight * aspect;
				}
				ImGui::TextUnformatted("Preview:");
				ImGui::Image(ImTextureRef(textureId), ImVec2(drawWidth, drawHeight));
			} else {
				ImGui::Text("Status: Not loaded");
				if (ImGui::Button("Load Texture")) {
					if (TextureManager::GetInstance()) {
						TextureManager::GetInstance()->LoadTexture(selectedProjectFile_);
					}
				}
			}
		} 
		else if (ext == ".obj" || ext == ".gltf") {
			// Model asset inspector
			std::string relativePath = GetPathRelativeToResources(selectedProjectFile_);
			bool isLoaded = ModelManager::GetInstance() && (ModelManager::GetInstance()->FindModel(relativePath) != nullptr);
			
			if (isLoaded) {
				ImGui::Text("Status: Loaded in ModelManager");
				ImGui::Text("Key: %s", relativePath.c_str());
			} else {
				ImGui::Text("Status: Not loaded");
				if (ImGui::Button("Load Model")) {
					if (ModelManager::GetInstance()) {
						ModelManager::GetInstance()->LoadModel(relativePath);
					}
				}
			}

			ImGui::Separator();
			ImGui::BeginDisabled(
				!editorSession_ || !editorSession_->IsEditing()
			);
			if (ImGui::Button("Add Model to Scene")) {
				SceneDocument& document = editorSession_->GetEditDocument();
				std::string entityName = path.stem().string();
				if (entityName.empty()) {
					entityName = "Model";
				}
				const std::string baseName = entityName;
				uint32_t suffix = 2;
				while (document.FindEntityByName(entityName)) {
					entityName = baseName + " " + std::to_string(suffix++);
				}
				SceneEntity& entity = document.CreateEntity(entityName);
				entity.modelPath = relativePath;
				entity.components = { "MeshRenderer" };
				selectedEntityId_ = entity.id;
				selectedProjectFile_.clear();
			}
			ImGui::EndDisabled();
			if (!editorSession_) {
				ImGui::TextDisabled("Scene editing is unavailable");
			} else if (!editorSession_->IsEditing()) {
				ImGui::TextDisabled("Stop Play Mode to edit the scene");
			}
		}
		else if (ext == ".wav") {
			// Audio asset inspector
			ImGui::Text("Audio format: WAVE");
			if (previewSoundData_.pBuffer == nullptr) {
				if (ImGui::Button("Load & Play Sound")) {
					if (Audio::GetInstance()) {
						previewSoundData_ = Audio::GetInstance()->SoundLoadWave(selectedProjectFile_.c_str());
						Audio::GetInstance()->SoundPlayWave(previewSoundData_);
					}
				}
			} else {
				ImGui::TextColored(ImVec4(0.3f, 0.8f, 0.3f, 1.0f), "Playing / Loaded");
				if (ImGui::Button("Stop & Unload Sound")) {
					if (Audio::GetInstance()) {
						Audio::GetInstance()->SoundUnload(&previewSoundData_);
					}
				}
			}
		}
		else if (ext == ".json") {
			// Particle JSON inspector
			ParticleEffectDesc desc;
			if (ParticleEffectResource::Load(selectedProjectFile_, desc)) {
				ImGui::Text("Type: Particle Effect Description");
				ImGui::Text("Effect Name: %s", desc.name.c_str());
				ImGui::Text("Texture: %s", desc.textureFilePath.c_str());
				ImGui::Text("Particle Count: %u", desc.emitter.count);
				ImGui::Text("Spawn Size: (%.2f, %.2f, %.2f)", desc.emitter.spawnSize.x, desc.emitter.spawnSize.y, desc.emitter.spawnSize.z);
				
				ImGui::Separator();
				if (ImGui::Button("Load into Particle Editor")) {
					particleToLoad_ = selectedProjectFile_;
					requestLoadParticle_ = true;
				}
			} else {
				ImGui::Text("Type: JSON File");
				ImGui::Text("Unable to parse as ParticleEffectDesc");
			}
		}
		else {
			ImGui::Text("Type: Unknown Asset / Plain File");
		}
	}
	else if (editorSession_ && selectedEntityId_ != 0) {
		SceneDocument& document = editorSession_->GetActiveDocument();
		SceneEntity* entity = document.FindEntity(selectedEntityId_);
		if (!entity) {
			selectedEntityId_ = 0;
			ImGui::TextDisabled("Entity no longer exists");
			ImGui::End();
			return;
		}

		char nameBuffer[128]{};
		strncpy_s(nameBuffer, entity->name.c_str(), _TRUNCATE);
		if (ImGui::InputText("Name", nameBuffer, sizeof(nameBuffer))) {
			entity->name = nameBuffer;
			document.MarkDirty();
		}
		if (ImGui::Checkbox("Active", &entity->active)) {
			document.MarkDirty();
		}

		const SceneEntity* parent = document.FindEntity(entity->parentId);
		const char* parentName = parent ? parent->name.c_str() : "None (Root)";
		if (ImGui::BeginCombo("Parent", parentName)) {
			if (ImGui::Selectable("None (Root)", entity->parentId == 0)) {
				document.SetParent(entity->id, 0);
			}
			for (const SceneEntity& candidate : document.GetEntities()) {
				if (
					candidate.id == entity->id ||
					document.IsDescendantOf(candidate.id, entity->id)
				) {
					continue;
				}
				if (ImGui::Selectable(
					candidate.name.c_str(),
					entity->parentId == candidate.id
				)) {
					document.SetParent(entity->id, candidate.id);
				}
			}
			ImGui::EndCombo();
		}

		ImGui::SeparatorText("Transform");
		bool transformChanged = false;
		transformChanged |= ImGui::DragFloat3(
			"Position",
			&entity->transform.translate.x,
			0.05f
		);
		transformChanged |= ImGui::DragFloat3(
			"Rotation",
			&entity->transform.rotate.x,
			0.01f
		);
		transformChanged |= ImGui::DragFloat3(
			"Scale",
			&entity->transform.scale.x,
			0.01f,
			0.001f,
			1000.0f
		);
		if (transformChanged) {
			document.MarkDirty();
		}

		for (const std::string& component : entity->components) {
			ImGui::SeparatorText(component.c_str());
			if (component == "MeshRenderer") {
				ImGui::Text("Model: %s", entity->modelPath.c_str());
			}
		}
		if (editorSession_->IsPlaying() || editorSession_->IsPaused()) {
			ImGui::TextDisabled("Play mode changes are temporary");
		}
	}
	else {
		// Default scene hierarchy inspector
		static const char* itemNames[] = {
			"Main Camera",
			"Environment",
			"Scene Objects",
			"Lights",
			"Effects"
		};
		ImGui::TextUnformatted(itemNames[selectedHierarchyItem_]);
		ImGui::Separator();
	}

	ImGui::End();
}

void ImGuiManager::DrawProjectWindow() {
	ImGui::Begin("Project", &showProject_);
	ImGui::Columns(2, "ProjectColumns", true);
	
	// Left column: directory tree
	static bool setColWidth = false;
	if (!setColWidth) {
		ImGui::SetColumnWidth(0, 180.0f);
		setColWidth = true;
	}

	ImGui::TextUnformatted("Folders");
	ImGui::Separator();
	
	// Draw recursive tree starting from "resources"
	DrawDirectoryTreeNode("resources");
	
	ImGui::NextColumn();

	// Right column: files inside selected folder
	ImGui::Text("Contents of: %s", selectedProjectFolder_.c_str());
	ImGui::Separator();

	std::error_code ec;
	if (std::filesystem::exists(selectedProjectFolder_, ec)) {
		for (const auto& entry : std::filesystem::directory_iterator(selectedProjectFolder_, ec)) {
			if (entry.is_regular_file()) {
				std::string fileName = entry.path().filename().string();
				std::string filePath = entry.path().generic_string();

				bool isSelected = (selectedProjectFile_ == filePath);
				// Add file type prefix
				std::string prefix = "[File] ";
				std::string ext = entry.path().extension().string();
				std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
				if (ext == ".png" || ext == ".dds") {
					prefix = "[Tex]  ";
				} else if (ext == ".obj" || ext == ".gltf") {
					prefix = "[Model]";
				} else if (ext == ".json") {
					prefix = "[JSON] ";
				} else if (ext == ".wav") {
					prefix = "[Audio]";
				} else if (ext == ".hlsl" || ext == ".hlsli") {
					prefix = "[Shdr] ";
				}

				std::string displayName = prefix + "  " + fileName;
				if (ImGui::Selectable(displayName.c_str(), isSelected)) {
					selectedProjectFile_ = filePath;
					selectedEntityId_ = 0;
					
					// Stop and unload preview sound if we change file
					if (previewSoundData_.pBuffer) {
						if (Audio::GetInstance()) {
							Audio::GetInstance()->SoundUnload(&previewSoundData_);
						}
					}
				}
			}
		}
	}
	ImGui::Columns(1);
	ImGui::End();
}

void ImGuiManager::DrawDirectoryTreeNode(const std::filesystem::path& path) {
	std::string folderName = path.filename().string();
	if (folderName.empty()) {
		folderName = path.generic_string();
	}

	ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick;
	if (selectedProjectFolder_ == path.generic_string()) {
		flags |= ImGuiTreeNodeFlags_Selected;
	}

	// Check if this directory has subdirectories
	bool hasSubdirs = false;
	std::error_code ec;
	if (std::filesystem::exists(path, ec)) {
		for (const auto& entry : std::filesystem::directory_iterator(path, ec)) {
			if (entry.is_directory()) {
				hasSubdirs = true;
				break;
			}
		}
	}
	if (!hasSubdirs) {
		flags |= ImGuiTreeNodeFlags_Leaf;
	}

	bool open = ImGui::TreeNodeEx(folderName.c_str(), flags);
	if (ImGui::IsItemClicked()) {
		selectedProjectFolder_ = path.generic_string();
	}

	if (open) {
		if (hasSubdirs && std::filesystem::exists(path, ec)) {
			for (const auto& entry : std::filesystem::directory_iterator(path, ec)) {
				if (entry.is_directory()) {
					DrawDirectoryTreeNode(entry.path());
				}
			}
		}
		ImGui::TreePop();
	}
}

void ImGuiManager::DrawConsoleWindow() {
	ImGui::Begin("Console", &showConsole_);
	ImGui::TextColored(
		ImVec4(0.45f, 0.8f, 0.55f, 1.0f),
		"Ready"
	);
	ImGui::SameLine();
	ImGui::TextDisabled(
		"%.1f FPS",
		ImGui::GetIO().Framerate
	);
	ImGui::End();
}

bool ImGuiManager::IsSceneViewInputActive() {
	return sceneViewInputActive_;
}
