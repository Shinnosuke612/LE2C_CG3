// 役割: ImGuiエディタ各ウィンドウの描画、入力、シーン編集操作を実装する。
#include "ImGuiManager.h"

#include <cassert>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <functional>
#include <iterator>
#include <limits>
#include <utility>

#include "WinApp.h"
#include "DirectXCommon.h"
#include "../3d/SrvManager.h"
#include "../2d/TextureManager.h"
#include "../2d/TextureFormat.h"
#include "../3d/ModelManager.h"
#include "../3d/Model.h"
#include "../3d/ModelFormat.h"
#include "../3d/Object3dCommon.h"
#include "../3d/Camera.h"
#include "../io/Input.h"
#include "../math/Matrix4x4.h"
#include "../math/Math.h"
#include "../particle/ParticleEffectResource.h"
#include "../particle/ParticleManager.h"
#include "../scene/EditorSession.h"
#include "../scene/SceneCatalog.h"
#include "../scene/SceneDocument.h"
#include "../scene/SceneEntityQuery.h"
#include "../scene/SceneManager.h"
#include "../scene/SceneTemplateRegistry.h"
#include "../scene/SceneTransformResolver.h"
#include "../utility/EditableResourcePath.h"
#include "../utility/StringUtility.h"
#include "../utility/SystemPerformanceMonitor.h"

#include "../../externals/imgui/imgui.h"
#include "../../externals/imgui/imgui_internal.h"
#include "../../externals/imgui/imgui_impl_win32.h"
#include "../../externals/imgui/imgui_impl_dx12.h"
#include "../../externals/ImGuizmo/ImGuizmo.h"
#include "../../externals/nlohmann/json.hpp"

namespace {
	using json = nlohmann::json;
	using SceneEntityQuery::FindComponent;
	using SceneEntityQuery::FindEnabledComponent;
	using SceneEntityQuery::HasComponent;
	using SceneTransformResolver::ResolveSceneWorldMatrix;

	bool IsSameRotation(const Quaternion& left, const Quaternion& right) {
		const Quaternion normalizedLeft = Normalize(left);
		const Quaternion normalizedRight = Normalize(right);
		const float dot =
			normalizedLeft.x * normalizedRight.x +
			normalizedLeft.y * normalizedRight.y +
			normalizedLeft.z * normalizedRight.z +
			normalizedLeft.w * normalizedRight.w;
		return std::abs(dot) >= 0.999999f;
	}

	bool HasNonUniformScale(const Matrix4x4& matrix) {
		Vector3 scale{};
		Quaternion rotate = MakeIdentityQuaternion();
		Vector3 translate{};
		if (!DecomposeAffineMatrix(matrix, scale, rotate, translate)) {
			return true;
		}
		const Vector3 absoluteScale{
			std::abs(scale.x), std::abs(scale.y), std::abs(scale.z)
		};
		constexpr float epsilon = 0.0001f;
		return
			std::abs(absoluteScale.x - absoluteScale.y) > epsilon ||
			std::abs(absoluteScale.y - absoluteScale.z) > epsilon;
	}

	std::string PathToUtf8(const std::filesystem::path& path) {
		return StringUtility::ToUtf8(path);
	}

	std::filesystem::path PathFromUtf8(const std::string& path) {
		return StringUtility::ToPath(path);
	}

	void CopyTextBuffer(
		char* destination,
		size_t destinationSize,
		const std::string& source
	) {
		strncpy_s(destination, destinationSize, source.c_str(), _TRUNCATE);
	}

	std::string SceneAssetFileStem(const SceneDescriptor& descriptor) {
		const std::string path = descriptor.assetPath.empty()
			? descriptor.filePath
			: descriptor.assetPath;
		std::string fileName = StringUtility::ToUtf8(
			StringUtility::ToPath(path).filename()
		);
		constexpr const char* suffix = ".scene.json";
		if (fileName.ends_with(suffix)) {
			fileName.erase(fileName.size() - std::strlen(suffix));
		}
		return fileName;
	}

	std::string BuildSceneAssetPath(const char* fileStem) {
		std::string fileName = fileStem;
		if (!fileName.ends_with(".scene.json")) {
			fileName += ".scene.json";
		}
		return "resources/scenes/" + fileName;
	}

	std::filesystem::path GetProjectResourceRoot();

	bool ReadBinaryFile(
		const std::filesystem::path& path,
		std::vector<uint8_t>& output
	) {
		std::ifstream input(path, std::ios::binary | std::ios::ate);
		if (!input.is_open()) {
			return false;
		}
		const std::streampos size = input.tellg();
		if (size <= 0) {
			return false;
		}
		output.resize(static_cast<size_t>(size));
		input.seekg(0, std::ios::beg);
		input.read(
			reinterpret_cast<char*>(output.data()),
			static_cast<std::streamsize>(output.size())
		);
		return input.gcount() == static_cast<std::streamsize>(output.size());
	}

	bool LoadFirstAvailableFont(
		const std::vector<std::filesystem::path>& candidates,
		std::vector<uint8_t>& output
	) {
		for (const std::filesystem::path& candidate : candidates) {
			std::error_code error;
			if (!std::filesystem::exists(candidate, error)) {
				continue;
			}
			if (ReadBinaryFile(candidate, output)) {
				return true;
			}
		}
		return false;
	}

	void AddProjectAssetGlyphs(ImFontGlyphRangesBuilder& builder) {
		std::error_code error;
		std::filesystem::recursive_directory_iterator iterator(
			GetProjectResourceRoot(),
			std::filesystem::directory_options::skip_permission_denied,
			error
		);
		const std::filesystem::recursive_directory_iterator end;
		while (!error && iterator != end) {
			builder.AddText(PathToUtf8(iterator->path().filename()).c_str());
			iterator.increment(error);
		}
	}

	bool IsModelAssetPath(const std::filesystem::path& path) {
		return ModelFormat::IsBasicModelPath(path);
	}

	bool IsTextureAssetPath(const std::filesystem::path& path) {
		return TextureFormat::IsSupportedTexturePath(path);
	}

	std::filesystem::path GetProjectResourceRoot() {
		return EditableResourcePath::Resolve("resources");
	}

	constexpr char kEditorSettingsPath[] = "editor_settings.json";

	std::string GetProjectResourcePath(const std::string& path) {
		return PathToUtf8(EditableResourcePath::ToProjectRelative(
			EditableResourcePath::ResolveResource(PathFromUtf8(path))
		));
	}

	std::string GetModelPathRelativeToResources(const std::string& fullPath) {
		const std::string projectPath = GetProjectResourcePath(fullPath);
		const std::string prefix = "resources/";
		if (projectPath.rfind(prefix, 0) == 0) {
			return projectPath.substr(prefix.length());
		}
		return projectPath;
	}

	std::vector<std::string> CollectModelAssetPaths() {
		std::vector<std::string> paths;
		std::error_code error;
		std::filesystem::recursive_directory_iterator iterator(
			GetProjectResourceRoot(),
			std::filesystem::directory_options::skip_permission_denied,
			error
		);
		const std::filesystem::recursive_directory_iterator end;
		while (!error && iterator != end) {
			if (iterator->is_regular_file(error) && IsModelAssetPath(iterator->path())) {
				paths.push_back(GetModelPathRelativeToResources(
					PathToUtf8(iterator->path())
				));
			}
			iterator.increment(error);
		}
		std::sort(paths.begin(), paths.end());
		return paths;
	}

	std::vector<std::string> CollectTextureAssetPaths() {
		std::vector<std::string> paths;
		std::error_code error;
		std::filesystem::recursive_directory_iterator iterator(
			GetProjectResourceRoot(),
			std::filesystem::directory_options::skip_permission_denied,
			error
		);
		const std::filesystem::recursive_directory_iterator end;
		while (!error && iterator != end) {
			if (iterator->is_regular_file(error) && IsTextureAssetPath(iterator->path())) {
				paths.push_back(GetProjectResourcePath(
					PathToUtf8(iterator->path())
				));
			}
			iterator.increment(error);
		}
		std::sort(paths.begin(), paths.end());
		return paths;
	}

	Vector3 TransformCoord(const Vector3& value, const Matrix4x4& matrix) {
		const float x =
			value.x * matrix.m[0][0] +
			value.y * matrix.m[1][0] +
			value.z * matrix.m[2][0] +
			matrix.m[3][0];
		const float y =
			value.x * matrix.m[0][1] +
			value.y * matrix.m[1][1] +
			value.z * matrix.m[2][1] +
			matrix.m[3][1];
		const float z =
			value.x * matrix.m[0][2] +
			value.y * matrix.m[1][2] +
			value.z * matrix.m[2][2] +
			matrix.m[3][2];
		const float w =
			value.x * matrix.m[0][3] +
			value.y * matrix.m[1][3] +
			value.z * matrix.m[2][3] +
			matrix.m[3][3];
		const float inverseW = std::abs(w) > 0.000001f ? 1.0f / w : 1.0f;
		return { x * inverseW, y * inverseW, z * inverseW };
	}

	bool IntersectRayAabb(
		const Vector3& rayOrigin,
		const Vector3& rayDirection,
		const Vector3& boundsMin,
		const Vector3& boundsMax,
		float& outDistance
	) {
		float tMin = 0.0f;
		float tMax = (std::numeric_limits<float>::max)();
		const float origin[3] = {
			rayOrigin.x,
			rayOrigin.y,
			rayOrigin.z
		};
		const float direction[3] = {
			rayDirection.x,
			rayDirection.y,
			rayDirection.z
		};
		const float minimum[3] = {
			boundsMin.x,
			boundsMin.y,
			boundsMin.z
		};
		const float maximum[3] = {
			boundsMax.x,
			boundsMax.y,
			boundsMax.z
		};
		for (uint32_t axis = 0; axis < 3; ++axis) {
			if (std::abs(direction[axis]) < 0.000001f) {
				if (origin[axis] < minimum[axis] || origin[axis] > maximum[axis]) {
					return false;
				}
				continue;
			}
			float t1 = (minimum[axis] - origin[axis]) / direction[axis];
			float t2 = (maximum[axis] - origin[axis]) / direction[axis];
			if (t1 > t2) {
				std::swap(t1, t2);
			}
			tMin = (std::max)(tMin, t1);
			tMax = (std::min)(tMax, t2);
			if (tMin > tMax) {
				return false;
			}
		}
		outDistance = tMin;
		return true;
	}

	bool IsLowPriorityPickTarget(
		const SceneEntity& entity,
		const SceneComponent& meshRenderer,
		const Vector3& localMin,
		const Vector3& localMax
	) {
		std::string name = entity.name;
		std::string modelPath = meshRenderer.modelPath;
		std::transform(name.begin(), name.end(), name.begin(), ::tolower);
		std::transform(
			modelPath.begin(),
			modelPath.end(),
			modelPath.begin(),
			::tolower
		);
		if (
			name.find("terrain") != std::string::npos ||
			modelPath.find("terrain") != std::string::npos
		) {
			return true;
		}

		const Vector3 extent = {
			std::abs((localMax.x - localMin.x) * entity.transform.scale.x),
			std::abs((localMax.y - localMin.y) * entity.transform.scale.y),
			std::abs((localMax.z - localMin.z) * entity.transform.scale.z)
		};
		const float footprint = extent.x * extent.z;
		return footprint > 2500.0f || (extent.x > 80.0f && extent.z > 80.0f);
	}

}

ImGuiManager* ImGuiManager::instance = nullptr;

ImGuiManager* ImGuiManager::GetInstance() {
	return instance;
}

bool ImGuiManager::ConsumeOpenSceneRequest(
	std::string& sceneId,
	bool& discardUnsavedChanges
) {
	if (requestedSceneId_.empty()) {
		return false;
	}
	sceneId = std::move(requestedSceneId_);
	requestedSceneId_.clear();
	discardUnsavedChanges = requestedSceneDiscardUnsavedChanges_;
	requestedSceneDiscardUnsavedChanges_ = false;
	return true;
}

bool ImGuiManager::ConsumeSceneAssetRequest(SceneAssetRequest& request) {
	if (!sceneAssetRequestPending_) {
		return false;
	}
	request = std::move(requestedSceneAsset_);
	requestedSceneAsset_ = {};
	sceneAssetRequestPending_ = false;
	return true;
}

bool ImGuiManager::ConsumeSceneInstanceRequest(SceneInstanceRequest& request) {
	if (!sceneInstanceRequestPending_) {
		return false;
	}
	request = std::move(requestedSceneInstance_);
	requestedSceneInstance_ = {};
	sceneInstanceRequestPending_ = false;
	return true;
}

bool ImGuiManager::ConsumeStartSceneRequest(std::string& sceneId) {
	if (!startSceneRequestPending_) {
		return false;
	}
	sceneId = std::move(requestedStartSceneId_);
	requestedStartSceneId_.clear();
	startSceneRequestPending_ = false;
	return true;
}

bool ImGuiManager::ConsumeStartupModeRequest(
	SceneBuildConfiguration& configuration,
	SceneStartupMode& mode
) {
	if (!startupModeRequestPending_) {
		return false;
	}
	configuration = requestedStartupConfiguration_;
	mode = requestedStartupMode_;
	startupModeRequestPending_ = false;
	return true;
}

void ImGuiManager::NotifySceneAssetOperationResult(
	bool success,
	const std::string& message
) {
	if (success) {
		InvalidateProjectCache();
		return;
	}
	sceneAssetErrorMessage_ = message.empty()
		? "Scene asset operation failed."
		: message;
	sceneAssetErrorPopupRequested_ = true;
}

void ImGuiManager::NotifySceneInstanceOperationResult(
	bool success,
	const std::string& message
) {
	sceneInstanceOperationSucceeded_ = success;
	sceneInstanceStatusMessage_ = message;
}

void ImGuiManager::NotifyProjectSettingsResult(
	bool success,
	const std::string& message
) {
	if (success) {
		return;
	}
	projectSettingsErrorMessage_ = message.empty()
		? "Project Settings could not be saved."
		: message;
	projectSettingsErrorPopupRequested_ = true;
}

void ImGuiManager::NotifyEditSceneOpened() {
	selectedEntityId_ = 0;
	selectedEntityIds_.clear();
	hierarchySelectionAnchorId_ = 0;
	hierarchyObservedEntityId_ = 0;
	hierarchyRenameEntityId_ = 0;
	hierarchyRevealRequested_ = false;
	revealInspectorRequested_ = false;
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
	LoadEditorSettings();

	ImGui::StyleColorsDark();
	ImGuiStyle& style = ImGui::GetStyle();
	const float dpiScale =
		static_cast<float>(GetDpiForWindow(winApp_->GetHwnd())) / 96.0f;
	ConfigureEditorFont(io, dpiScale);
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

void ImGuiManager::ConfigureEditorFont(ImGuiIO& io, float dpiScale) {
	const std::filesystem::path fontDirectory =
		EditableResourcePath::Resolve("resources/fonts");
	const std::vector<std::filesystem::path> cascadiaFontCandidates = {
		fontDirectory / "CascadiaMono.ttf",
		fontDirectory / "CascadiaCode.ttf",
		L"C:\\Windows\\Fonts\\CascadiaMono.ttf",
		L"C:\\Windows\\Fonts\\CascadiaCode.ttf"
	};
	const std::vector<std::filesystem::path> japaneseFontCandidates = {
		fontDirectory / "NotoSansCJKjp-Regular.otf",
		fontDirectory / "NotoSansJP-Regular.ttf",
		fontDirectory / "NotoSansJP-VF.ttf",
		L"C:\\Windows\\Fonts\\NotoSansJP-VF.ttf",
		L"C:\\Windows\\Fonts\\YuGothM.ttc",
		L"C:\\Windows\\Fonts\\meiryo.ttc"
	};
	const std::vector<std::filesystem::path> chineseFontCandidates = {
		fontDirectory / "NotoSansCJKsc-Regular.otf",
		fontDirectory / "NotoSansSC-Regular.ttf",
		L"C:\\Windows\\Fonts\\msyh.ttc",
		L"C:\\Windows\\Fonts\\msjh.ttc"
	};

	editorBaseFontData_.clear();
	editorJapaneseFontData_.clear();
	editorChineseFontData_.clear();
	editorGlyphRanges_.clear();

	ImFontGlyphRangesBuilder glyphBuilder;
	glyphBuilder.AddRanges(io.Fonts->GetGlyphRangesJapanese());
	glyphBuilder.AddRanges(io.Fonts->GetGlyphRangesChineseSimplifiedCommon());
	AddProjectAssetGlyphs(glyphBuilder);
	glyphBuilder.BuildRanges(&editorGlyphRanges_);

	ImFontConfig fontConfig{};
	fontConfig.SizePixels = editorFontSize_ * dpiScale;
	fontConfig.OversampleH = 1;
	fontConfig.OversampleV = 1;
	fontConfig.FontDataOwnedByAtlas = false;

	ImFont* editorFont = nullptr;
	bool baseContainsJapanese = false;
	bool baseContainsChinese = false;
	if (editorFontPreset_ == EditorFontPreset::UnifiedCjk) {
		if (LoadFirstAvailableFont(japaneseFontCandidates, editorBaseFontData_)) {
			baseContainsJapanese = true;
			editorFont = io.Fonts->AddFontFromMemoryTTF(
				editorBaseFontData_.data(),
				static_cast<int>(editorBaseFontData_.size()),
				fontConfig.SizePixels,
				&fontConfig,
				editorGlyphRanges_.Data
			);
		} else if (LoadFirstAvailableFont(chineseFontCandidates, editorBaseFontData_)) {
			baseContainsChinese = true;
			editorFont = io.Fonts->AddFontFromMemoryTTF(
				editorBaseFontData_.data(),
				static_cast<int>(editorBaseFontData_.size()),
				fontConfig.SizePixels,
				&fontConfig,
				editorGlyphRanges_.Data
			);
		}
	} else if (editorFontPreset_ == EditorFontPreset::CascadiaMonoWithCjk &&
		LoadFirstAvailableFont(cascadiaFontCandidates, editorBaseFontData_)) {
		editorFont = io.Fonts->AddFontFromMemoryTTF(
			editorBaseFontData_.data(),
			static_cast<int>(editorBaseFontData_.size()),
			fontConfig.SizePixels,
			&fontConfig,
			editorGlyphRanges_.Data
		);
	}

	if (!editorFont) {
		editorFont = io.Fonts->AddFontDefault(&fontConfig);
	}

	if (editorFont && !baseContainsJapanese &&
		LoadFirstAvailableFont(japaneseFontCandidates, editorJapaneseFontData_)) {
		ImFontConfig mergeConfig = fontConfig;
		mergeConfig.MergeMode = true;
		mergeConfig.DstFont = editorFont;
		io.Fonts->AddFontFromMemoryTTF(
			editorJapaneseFontData_.data(),
			static_cast<int>(editorJapaneseFontData_.size()),
			fontConfig.SizePixels,
			&mergeConfig,
			editorGlyphRanges_.Data
		);
	}

	if (editorFont && !baseContainsChinese &&
		LoadFirstAvailableFont(chineseFontCandidates, editorChineseFontData_)) {
		ImFontConfig mergeConfig = fontConfig;
		mergeConfig.MergeMode = true;
		mergeConfig.DstFont = editorFont;
		io.Fonts->AddFontFromMemoryTTF(
			editorChineseFontData_.data(),
			static_cast<int>(editorChineseFontData_.size()),
			fontConfig.SizePixels,
			&mergeConfig,
			editorGlyphRanges_.Data
		);
	}

	if (editorFont) {
		io.FontDefault = editorFont;
	}
}

void ImGuiManager::BeginFrame(){
	ImGuiIO& io = ImGui::GetIO();
	Input* input = Input::GetInstance();
	const bool altHeld = input &&
		(input->PushKey(DIK_LMENU) || input->PushKey(DIK_RMENU));
	const bool blockEditorMouse =
		editorSession_ && editorSession_->IsPlaying() && !altHeld;
	if (blockEditorMouse) {
		io.ConfigFlags |= ImGuiConfigFlags_NoMouse;
		io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
	} else {
		io.ConfigFlags &= ~ImGuiConfigFlags_NoMouse;
		io.ConfigFlags &= ~ImGuiConfigFlags_NoMouseCursorChange;
	}
	ApplyPendingEditorFont();
	ImGui_ImplDX12_NewFrame();
	ImGui_ImplWin32_NewFrame();
	ImGui::NewFrame();
	ImGuizmo::BeginFrame();

	CreateDockSpace();
}

void ImGuiManager::LoadEditorSettings() {
	std::string text;
	if (!EditableResourcePath::ReadText(kEditorSettingsPath, text)) {
		return;
	}

	try {
		const json settings = json::parse(text);
		const std::string preset = settings.value(
			"fontPreset",
			"originalWithCjk"
		);
		if (preset == "unifiedCjk") {
			editorFontPreset_ = EditorFontPreset::UnifiedCjk;
		} else if (preset == "cascadiaMonoWithCjk") {
			editorFontPreset_ = EditorFontPreset::CascadiaMonoWithCjk;
		} else {
			editorFontPreset_ = EditorFontPreset::OriginalWithCjk;
		}
		if (settings.contains("fontSize") && settings["fontSize"].is_number()) {
			editorFontSize_ = std::clamp(
				settings["fontSize"].get<float>(),
				10.0f,
				22.0f
			);
		}
	} catch (const json::exception&) {
		// 壊れたエディタ設定は無視し、既定値で起動する。
	}
}

void ImGuiManager::SaveEditorSettings() const {
	const char* preset = "originalWithCjk";
	if (editorFontPreset_ == EditorFontPreset::UnifiedCjk) {
		preset = "unifiedCjk";
	} else if (editorFontPreset_ == EditorFontPreset::CascadiaMonoWithCjk) {
		preset = "cascadiaMonoWithCjk";
	}

	const json settings = {
		{ "fontPreset", preset },
		{ "fontSize", editorFontSize_ }
	};
	EditableResourcePath::WriteTextAtomically(
		kEditorSettingsPath,
		settings.dump(2)
	);
}

void ImGuiManager::RequestEditorFontRebuild() {
	editorFontRebuildRequested_ = true;
}

void ImGuiManager::ApplyPendingEditorFont() {
	if (!editorFontRebuildRequested_) {
		return;
	}

	ImGuiIO& io = ImGui::GetIO();
	io.FontDefault = nullptr;
	io.Fonts->Clear();
	const float dpiScale =
		static_cast<float>(GetDpiForWindow(winApp_->GetHwnd())) / 96.0f;
	ConfigureEditorFont(io, dpiScale);
	editorFontRebuildRequested_ = false;
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
	if (editorSession_) {
		const ImGuiIO& io = ImGui::GetIO();
		const bool mayEditThisFrame =
			ImGui::IsAnyItemActive() ||
			io.WantTextInput ||
			io.MouseDown[ImGuiMouseButton_Left] ||
			io.MouseDown[ImGuiMouseButton_Right] ||
			io.MouseDown[ImGuiMouseButton_Middle] ||
			io.MouseClicked[ImGuiMouseButton_Left] ||
			io.MouseClicked[ImGuiMouseButton_Right] ||
			io.MouseClicked[ImGuiMouseButton_Middle];
		if (mayEditThisFrame) {
			editorSession_->BeginEditFrame();
		}
	}

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
	const bool sceneImageHovered = ImGui::IsItemHovered();
	const ImVec2 sceneMin = ImGui::GetItemRectMin();
	const ImVec2 sceneMax = ImGui::GetItemRectMax();
	sceneViewMinX_ = sceneMin.x;
	sceneViewMinY_ = sceneMin.y;
	sceneViewMaxX_ = sceneMax.x;
	sceneViewMaxY_ = sceneMax.y;
	if (
		editorSession_ &&
		editorSession_->IsEditing() &&
		ImGui::BeginDragDropTarget()
	) {
		if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(
			"PROJECT_TEXTURE_PATH"
		)) {
			const char* droppedPath = static_cast<const char*>(payload->Data);
			if (droppedPath && droppedPath[0] != '\0') {
				SceneDocument& document = editorSession_->GetEditDocument();
				const std::filesystem::path texturePath = PathFromUtf8(droppedPath);
				std::string entityName = PathToUtf8(texturePath.stem());
				if (entityName.empty()) {
					entityName = "Sprite";
				}
				const std::string baseName = entityName;
				uint32_t suffix = 2;
				while (document.FindEntityByName(entityName)) {
					entityName = baseName + " " + std::to_string(suffix++);
				}
				SceneEntity& entity = document.CreateEntity(entityName);
				entity.spriteTexturePath = GetProjectResourcePath(
					PathToUtf8(texturePath)
				);
				entity.components = { "SpriteRenderer" };
				entity.components.front().texturePath = entity.spriteTexturePath;
				const ImVec2 mouse = ImGui::GetMousePos();
				const float normalizedX = (mouse.x - sceneMin.x) /
					(std::max)(sceneMax.x - sceneMin.x, 1.0f);
				const float normalizedY = (mouse.y - sceneMin.y) /
					(std::max)(sceneMax.y - sceneMin.y, 1.0f);
				entity.transform.translate = {
					normalizedX * static_cast<float>((std::max)(textureWidth, uint32_t{ 1 })),
					normalizedY * static_cast<float>((std::max)(textureHeight, uint32_t{ 1 })),
					0.0f
				};
				if (TextureManager::GetInstance()) {
					TextureManager::GetInstance()->LoadTexture(entity.spriteTexturePath);
					const auto& metadata = TextureManager::GetInstance()->GetMetaData(
						entity.spriteTexturePath
					);
					entity.spriteSize = {
						static_cast<float>(metadata.width),
						static_cast<float>(metadata.height)
					};
					entity.components.front().spriteSize = entity.spriteSize;
				}
				selectedEntityId_ = entity.id;
				selectedProjectFile_.clear();
			}
		}
		if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(
			"PROJECT_MODEL_PATH"
		)) {
			const char* droppedPath = static_cast<const char*>(payload->Data);
			if (droppedPath && droppedPath[0] != '\0') {
				SceneDocument& document = editorSession_->GetEditDocument();
				const std::filesystem::path modelPath = PathFromUtf8(droppedPath);
				std::string entityName = PathToUtf8(modelPath.stem());
				if (entityName.empty()) {
					entityName = "Model";
				}
				const std::string baseName = entityName;
				uint32_t suffix = 2;
				while (document.FindEntityByName(entityName)) {
					entityName = baseName + " " + std::to_string(suffix++);
				}
				SceneEntity& entity = document.CreateEntity(entityName);
				entity.modelPath = GetModelPathRelativeToResources(
					PathToUtf8(modelPath)
				);
				entity.components = { "MeshRenderer" };
				entity.components.front().modelPath = entity.modelPath;
				Object3dCommon* object3dCommon = Object3dCommon::GetInstance();
				if (Camera* camera = object3dCommon
					? object3dCommon->GetDefaultCamera()
					: nullptr) {
					const Matrix4x4& cameraWorld = camera->GetWorldMatrix();
					entity.transform.translate = {
						cameraWorld.m[3][0] + cameraWorld.m[2][0] * 5.0f,
						cameraWorld.m[3][1] + cameraWorld.m[2][1] * 5.0f,
						cameraWorld.m[3][2] + cameraWorld.m[2][2] * 5.0f
					};
				}
				if (ModelManager::GetInstance()) {
					ModelManager::GetInstance()->LoadModel(entity.modelPath);
				}
				selectedEntityId_ = entity.id;
				selectedProjectFile_.clear();
			}
		}
		ImGui::EndDragDropTarget();
	}

	DrawSceneGizmo(
		sceneMin.x,
		sceneMin.y,
		sceneMax.x - sceneMin.x,
		sceneMax.y - sceneMin.y,
		textureWidth,
		textureHeight
	);

	if (
		sceneImageHovered &&
		editorSession_ &&
		editorSession_->IsEditing() &&
		ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
		!ImGuizmo::IsUsing() &&
		!ImGuizmo::IsOver()
	) {
		const ImVec2 mouse = ImGui::GetMousePos();
		const bool overSceneToolbar =
			mouse.x >= sceneMin.x &&
			mouse.x <= sceneMin.x + 280.0f &&
			mouse.y >= sceneMin.y &&
			mouse.y <= sceneMin.y + 40.0f;
		if (!overSceneToolbar) {
			PickSceneEntity(
				sceneMin.x,
				sceneMin.y,
				sceneMax.x - sceneMin.x,
				sceneMax.y - sceneMin.y
			);
		}
	}
	if (
		editorSession_ &&
		editorSession_->IsEditing() &&
		ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
		!ImGui::GetIO().WantTextInput &&
		ImGui::IsKeyPressed(ImGuiKey_F, false)
	) {
		FocusSceneCameraOnSelection();
	}

	ImGui::GetWindowDrawList()->AddText(
		ImVec2(sceneMin.x + 16.0f, sceneMin.y + 44.0f),
		IM_COL32(255, 255, 255, 255),
		"Space to change particle assets"
	);

	sceneViewInputActive_ =
		(sceneImageHovered || ImGui::IsWindowFocused()) &&
		!ImGuizmo::IsUsing() &&
		!ImGuizmo::IsOver();

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
	if (showLoadedScenes_) {
		DrawLoadedScenesWindow();
	}

	if (editorSession_) {
		const bool editingInteractionActive =
			ImGui::IsAnyItemActive() ||
			ImGui::IsMouseDown(ImGuiMouseButton_Left) ||
			ImGui::IsMouseDown(ImGuiMouseButton_Right) ||
			ImGui::IsMouseDown(ImGuiMouseButton_Middle) ||
			ImGuizmo::IsUsing();
		editorSession_->EndEditFrame(!editingInteractionActive);
	}

	(void)textureWidth;
	(void)textureHeight;
}

void ImGuiManager::SetModelPreviewTexture(
	const std::string& modelPath,
	D3D12_GPU_DESCRIPTOR_HANDLE texture,
	uint32_t width,
	uint32_t height
) {
	modelPreviewRenderedPath_ = modelPath;
	modelPreviewTexture_ = texture;
	modelPreviewWidth_ = (std::max)(width, 1u);
	modelPreviewHeight_ = (std::max)(height, 1u);
}

const std::vector<std::string>& ImGuiManager::GetCachedModelAssetPaths() {
	if (assetPathCacheDirty_) {
		RefreshAssetPathCache();
	}
	return cachedModelAssetPaths_;
}

const std::vector<std::string>& ImGuiManager::GetCachedTextureAssetPaths() {
	if (assetPathCacheDirty_) {
		RefreshAssetPathCache();
	}
	return cachedTextureAssetPaths_;
}

void ImGuiManager::RefreshAssetPathCache() {
	cachedModelAssetPaths_ = CollectModelAssetPaths();
	cachedTextureAssetPaths_ = CollectTextureAssetPaths();
	assetPathCacheDirty_ = false;
}

void ImGuiManager::InvalidateProjectCache() {
	assetPathCacheDirty_ = true;
	projectDirectoryCacheDirty_ = true;
	projectTreeCacheDirty_ = true;
	cachedProjectFolder_.clear();
}

const std::vector<ImGuiManager::ProjectDirectoryEntry>&
ImGuiManager::GetCachedProjectDirectoryEntries() {
	if (
		projectDirectoryCacheDirty_ ||
		cachedProjectFolder_ != selectedProjectFolder_
	) {
		RefreshProjectDirectoryCache();
	}
	return cachedProjectEntries_;
}

void ImGuiManager::RefreshProjectDirectoryCache() {
	cachedProjectEntries_.clear();
	cachedProjectFolder_ = selectedProjectFolder_;
	projectDirectoryCacheDirty_ = false;

	std::error_code ec;
	const std::filesystem::path selectedFolderPath =
		PathFromUtf8(selectedProjectFolder_);
	if (!std::filesystem::exists(selectedFolderPath, ec)) {
		return;
	}

	for (const auto& entry : std::filesystem::directory_iterator(selectedFolderPath, ec)) {
		const bool isDirectory = entry.is_directory(ec);
		const bool isRegularFile = entry.is_regular_file(ec);
		if (!isDirectory && !isRegularFile) {
			continue;
		}

		ProjectDirectoryEntry cachedEntry{};
		cachedEntry.fileName = PathToUtf8(entry.path().filename());
		cachedEntry.filePath = PathToUtf8(entry.path());
		cachedEntry.extension = entry.path().extension().string();
		std::transform(
			cachedEntry.extension.begin(),
			cachedEntry.extension.end(),
			cachedEntry.extension.begin(),
			::tolower
		);
		cachedEntry.isDirectory = isDirectory;
		cachedEntry.isTexture = !isDirectory && IsTextureAssetPath(entry.path());
		cachedEntry.isModel = !isDirectory && IsModelAssetPath(entry.path());
		cachedEntry.isScene = !isDirectory && sceneCatalog_ &&
			sceneCatalog_->FindByFilePath(cachedEntry.filePath);
		cachedProjectEntries_.push_back(cachedEntry);
	}

	std::sort(
		cachedProjectEntries_.begin(),
		cachedProjectEntries_.end(),
		[](const ProjectDirectoryEntry& left, const ProjectDirectoryEntry& right) {
			if (left.isDirectory != right.isDirectory) {
				return left.isDirectory;
			}
			return left.fileName < right.fileName;
		}
	);
}

ImGuiManager::ProjectDirectoryNode ImGuiManager::BuildProjectDirectoryNode(
	const std::filesystem::path& path
) {
	ProjectDirectoryNode node{};
	node.folderName = PathToUtf8(path.filename());
	if (node.folderName.empty()) {
		node.folderName = PathToUtf8(path);
	}
	node.folderPath = PathToUtf8(path);

	std::error_code ec;
	if (!std::filesystem::exists(path, ec)) {
		return node;
	}

	for (const auto& entry : std::filesystem::directory_iterator(path, ec)) {
		if (entry.is_directory(ec)) {
			node.children.push_back(BuildProjectDirectoryNode(entry.path()));
		}
	}
	std::sort(
		node.children.begin(),
		node.children.end(),
		[](const ProjectDirectoryNode& left, const ProjectDirectoryNode& right) {
			return left.folderName < right.folderName;
		}
	);
	return node;
}

void ImGuiManager::RefreshProjectTreeCache() {
	cachedProjectTreeRoot_ = BuildProjectDirectoryNode(
		GetProjectResourceRoot()
	);
	projectTreeCacheDirty_ = false;
}

bool ImGuiManager::GetModelPreviewRequest(
	std::string& modelPath,
	float& yaw,
	float& pitch,
	float& zoom
) const {
	if (!selectedProjectFile_.empty()) {
		const std::filesystem::path selectedPath =
			PathFromUtf8(selectedProjectFile_);
		if (IsModelAssetPath(selectedPath)) {
			modelPath = GetModelPathRelativeToResources(
				PathToUtf8(selectedPath)
			);
			yaw = modelPreviewYaw_;
			pitch = modelPreviewPitch_;
			zoom = modelPreviewZoom_;
			return true;
		}
	}

	if (editorSession_ && selectedEntityId_ != 0) {
		const SceneDocument& document = editorSession_->GetActiveDocument();
		if (const SceneEntity* entity = document.FindEntity(selectedEntityId_)) {
			if (const SceneComponent* meshRenderer =
				FindEnabledComponent(*entity, "MeshRenderer")) {
				if (meshRenderer->modelPath.empty()) {
					return false;
				}
				modelPath = meshRenderer->modelPath;
				yaw = modelPreviewYaw_;
				pitch = modelPreviewPitch_;
				zoom = modelPreviewZoom_;
				return true;
			}
		}
	}

	return false;
}

bool ImGuiManager::PickSceneEntity(
	float x,
	float y,
	float width,
	float height
) {
	if (
		!editorSession_ ||
		!editorSession_->IsEditing() ||
		width <= 1.0f ||
		height <= 1.0f
	) {
		return false;
	}

	Camera* camera = Object3dCommon::GetInstance()
		? Object3dCommon::GetInstance()->GetDefaultCamera()
		: nullptr;
	if (!camera) {
		return false;
	}

	const ImVec2 mouse = ImGui::GetMousePos();
	const float normalizedX = (mouse.x - x) / width;
	const float normalizedY = (mouse.y - y) / height;
	if (
		normalizedX < 0.0f ||
		normalizedX > 1.0f ||
		normalizedY < 0.0f ||
		normalizedY > 1.0f
	) {
		return false;
	}

	const float ndcX = normalizedX * 2.0f - 1.0f;
	const float ndcY = 1.0f - normalizedY * 2.0f;
	const Matrix4x4 inverseViewProjection = Inverse(
		Multiply(camera->GetViewMatrix(), camera->GetProjectionMatrix())
	);
	const Vector3 nearPoint =
		TransformCoord({ ndcX, ndcY, 0.0f }, inverseViewProjection);
	const Vector3 farPoint =
		TransformCoord({ ndcX, ndcY, 1.0f }, inverseViewProjection);
	const Vector3 rayDirection =
		Math::Normalize(Math::Subtract(farPoint, nearPoint));

	SceneDocument& document = editorSession_->GetEditDocument();
	uint64_t bestEntityId = 0;
	float bestDistance = (std::numeric_limits<float>::max)();
	uint64_t bestLowPriorityEntityId = 0;
	float bestLowPriorityDistance = (std::numeric_limits<float>::max)();
	for (const SceneEntity& entity : document.GetEntities()) {
		if (entity.locked || !entity.active) {
			continue;
		}
		const SceneComponent* meshRenderer =
			FindEnabledComponent(entity, "MeshRenderer");
		if (!meshRenderer || meshRenderer->modelPath.empty()) {
			continue;
		}
		ModelManager::GetInstance()->LoadModel(meshRenderer->modelPath);
		Model* model = ModelManager::GetInstance()->FindModel(
			meshRenderer->modelPath
		);
		if (!model) {
			continue;
		}
		Vector3 localMin{};
		Vector3 localMax{};
		if (!model->GetLocalBounds(localMin, localMax)) {
			continue;
		}
		const Matrix4x4 inverseWorld =
			Inverse(ResolveSceneWorldMatrix(document, entity));
		const Vector3 localRayOrigin =
			TransformCoord(nearPoint, inverseWorld);
		const Vector3 localRayFar =
			TransformCoord(farPoint, inverseWorld);
		const Vector3 localRayDirection =
			Math::Normalize(Math::Subtract(localRayFar, localRayOrigin));
		float distance = 0.0f;
		if (
			IntersectRayAabb(
				localRayOrigin,
				localRayDirection,
				localMin,
				localMax,
				distance
			)
		) {
			const Vector3 localHit = Math::Add(
				localRayOrigin,
				Math::Multiply(localRayDirection, distance)
			);
			const Vector3 worldHit = TransformCoord(
				localHit,
				ResolveSceneWorldMatrix(document, entity)
			);
			const float worldDistance =
				Math::Length(Math::Subtract(worldHit, nearPoint));
			if (worldDistance >= bestDistance) {
				const bool lowPriority = IsLowPriorityPickTarget(
					entity,
					*meshRenderer,
					localMin,
					localMax
				);
				if (
					lowPriority &&
					worldDistance < bestLowPriorityDistance
				) {
					bestLowPriorityDistance = worldDistance;
					bestLowPriorityEntityId = entity.id;
				}
				continue;
			}
			const bool lowPriority = IsLowPriorityPickTarget(
				entity,
				*meshRenderer,
				localMin,
				localMax
			);
			if (lowPriority) {
				if (worldDistance < bestLowPriorityDistance) {
					bestLowPriorityDistance = worldDistance;
					bestLowPriorityEntityId = entity.id;
				}
			} else {
				bestDistance = worldDistance;
				bestEntityId = entity.id;
			}
		}
	}

	if (bestEntityId == 0) {
		bestEntityId = bestLowPriorityEntityId;
	}

	if (bestEntityId == 0) {
		return false;
	}

	selectedEntityId_ = bestEntityId;
	selectedProjectFile_.clear();
	showInspector_ = true;
	revealInspectorRequested_ = true;
	return true;
}

void ImGuiManager::FocusSceneCameraOnSelection() {
	if (!editorSession_ || !editorSession_->IsEditing()) {
		return;
	}
	Camera* camera = Object3dCommon::GetInstance()
		? Object3dCommon::GetInstance()->GetDefaultCamera()
		: nullptr;
	if (!camera) {
		return;
	}

	SceneDocument& document = editorSession_->GetEditDocument();
	std::vector<uint64_t> targetIds;
	for (uint64_t entityId : selectedEntityIds_) {
		if (document.FindEntity(entityId)) {
			targetIds.push_back(entityId);
		}
	}
	if (targetIds.empty() && selectedEntityId_ != 0) {
		if (document.FindEntity(selectedEntityId_)) {
			targetIds.push_back(selectedEntityId_);
		}
	}
	if (targetIds.empty()) {
		return;
	}

	Vector3 center{};
	std::vector<Vector3> positions;
	positions.reserve(targetIds.size());
	for (uint64_t entityId : targetIds) {
		const SceneEntity* entity = document.FindEntity(entityId);
		if (!entity) {
			continue;
		}
		const Matrix4x4 worldMatrix = ResolveSceneWorldMatrix(document, *entity);
		const Vector3 position = {
			worldMatrix.m[3][0],
			worldMatrix.m[3][1],
			worldMatrix.m[3][2]
		};
		positions.push_back(position);
		center = Math::Add(center, position);
	}
	if (positions.empty()) {
		return;
	}
	center = Math::Multiply(center, 1.0f / static_cast<float>(positions.size()));

	float radius = 0.0f;
	for (const Vector3& position : positions) {
		radius = (std::max)(
			radius,
			Math::Length(Math::Subtract(position, center))
		);
	}
	const Vector3 currentOffset = Math::Subtract(camera->GetTranslate(), center);
	Vector3 viewDirection = Math::Normalize(currentOffset);
	if (Math::Length(viewDirection) < 0.0001f) {
		viewDirection = { 0.0f, 0.35f, -1.0f };
		viewDirection = Math::Normalize(viewDirection);
	}
	const float distance = (std::max)(5.0f, radius * 2.5f + 2.0f);
	if (camera->IsOrbitMode()) {
		camera->SetOrbitTarget(center);
		camera->SetOrbitDistance(distance);
	} else {
		camera->SetLookAt(
			Math::Add(center, Math::Multiply(viewDirection, distance)),
			center
		);
	}
}

void ImGuiManager::DrawSceneGizmo(
	float x,
	float y,
	float width,
	float height,
	uint32_t textureWidth,
	uint32_t textureHeight
) {
	if (
		!editorSession_ ||
		!editorSession_->IsEditing() ||
		width <= 1.0f ||
		height <= 1.0f
	) {
		return;
	}

	if (
		ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
		!ImGui::GetIO().WantTextInput &&
		!ImGuizmo::IsUsing()
	) {
		if (ImGui::IsKeyPressed(ImGuiKey_W, false)) {
			gizmoOperation_ = 0;
		}
		if (ImGui::IsKeyPressed(ImGuiKey_E, false)) {
			gizmoOperation_ = 1;
		}
		if (ImGui::IsKeyPressed(ImGuiKey_R, false)) {
			gizmoOperation_ = 2;
		}
	}

	ImGui::SetCursorScreenPos(ImVec2(x + 8.0f, y + 8.0f));
	ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 2.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(3.0f, 0.0f));
	ImGui::BeginGroup();
	const char* operationLabels[] = { "W", "E", "R" };
	const char* operationTooltips[] = { "Move (W)", "Rotate (E)", "Scale (R)" };
	for (int operation = 0; operation < 3; ++operation) {
		if (operation > 0) {
			ImGui::SameLine();
		}
		if (gizmoOperation_ == operation) {
			ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
		}
		if (ImGui::Button(operationLabels[operation], ImVec2(28.0f, 24.0f))) {
			gizmoOperation_ = operation;
		}
		if (gizmoOperation_ == operation) {
			ImGui::PopStyleColor();
		}
		if (ImGui::IsItemHovered()) {
			ImGui::SetTooltip("%s", operationTooltips[operation]);
		}
	}
	ImGui::SameLine();
	if (ImGui::Button(gizmoLocalMode_ ? "Local" : "World", ImVec2(52.0f, 24.0f))) {
		gizmoLocalMode_ = !gizmoLocalMode_;
	}
	if (ImGui::IsItemHovered()) {
		ImGui::SetTooltip("Toggle Local / World space");
	}
	ImGui::SameLine();
	ImGui::Checkbox("Snap", &gizmoSnapEnabled_);
	if (gizmoSnapEnabled_) {
		float* snapValue = gizmoOperation_ == 0
			? &gizmoTranslationSnap_
			: gizmoOperation_ == 1
				? &gizmoRotationSnapDegrees_
				: &gizmoScaleSnap_;
		ImGui::SameLine();
		ImGui::SetNextItemWidth(64.0f);
		ImGui::DragFloat(
			"##GizmoSnapValue",
			snapValue,
			gizmoOperation_ == 1 ? 1.0f : 0.05f,
			0.01f,
			0.0f,
			"%.2f"
		);
	}
	ImGui::EndGroup();
	ImGui::PopStyleVar(2);

	if (selectedEntityId_ == 0) {
		return;
	}
	SceneDocument& document = editorSession_->GetEditDocument();
	SceneEntity* entity = document.FindEntity(selectedEntityId_);
	Camera* camera = Object3dCommon::GetInstance()
		? Object3dCommon::GetInstance()->GetDefaultCamera()
		: nullptr;
	if (!entity || !camera) {
		return;
	}
	if (entity->locked) {
		return;
	}

	const SceneComponent* spriteRenderer =
		FindEnabledComponent(*entity, "SpriteRenderer");
	const bool isSprite = spriteRenderer != nullptr;
	const SceneEntity* parentEntity = document.FindEntity(entity->parentId);
	const Matrix4x4 parentWorld = parentEntity
		? ResolveSceneWorldMatrix(document, *parentEntity)
		: MakeIdentity4x4();
	Matrix4x4 worldMatrix{};
	Matrix4x4 viewMatrix{};
	Matrix4x4 projectionMatrix{};
	if (isSprite) {
		const Vector3 spriteRotate =
			MakeEulerFromQuaternion(entity->transform.rotate);
		const Vector3 spriteScale = {
			spriteRenderer->spriteSize.x * entity->transform.scale.x,
			spriteRenderer->spriteSize.y * entity->transform.scale.y,
			1.0f
		};
		worldMatrix = MakeAffineMatrix(
			spriteScale,
			Vector3{ 0.0f, 0.0f, spriteRotate.z },
			Vector3{
				entity->transform.translate.x,
				entity->transform.translate.y,
				0.0f
			}
		);
		if (parentEntity) {
			worldMatrix = Multiply(
				worldMatrix,
				parentWorld
			);
		}
		viewMatrix = MakeIdentity4x4();
		projectionMatrix = MakeOrthographicMatrix(
			0.0f,
			0.0f,
			static_cast<float>((std::max)(textureWidth, uint32_t{ 1 })),
			static_cast<float>((std::max)(textureHeight, uint32_t{ 1 })),
			0.0f,
			100.0f
		);
	} else {
		worldMatrix = ResolveSceneWorldMatrix(document, *entity);
		viewMatrix = camera->GetViewMatrix();
		projectionMatrix = camera->GetProjectionMatrix();
	}

	const ImGuizmo::OPERATION operation = gizmoOperation_ == 0
		? ImGuizmo::TRANSLATE
		: gizmoOperation_ == 1
			? ImGuizmo::ROTATE
			: ImGuizmo::SCALE;
	const ImGuizmo::MODE mode = gizmoLocalMode_
		? ImGuizmo::LOCAL
		: ImGuizmo::WORLD;
	if (
		!gizmoLocalMode_ &&
		gizmoOperation_ != 0 &&
		parentEntity &&
		HasNonUniformScale(parentWorld)
	) {
		ImGui::GetWindowDrawList()->AddText(
			ImVec2(x + 8.0f, y + 38.0f),
			IM_COL32(255, 190, 80, 255),
			"World Rotate/Scale requires a uniformly scaled parent."
		);
		return;
	}
	const float snapValue = gizmoOperation_ == 0
		? gizmoTranslationSnap_
		: gizmoOperation_ == 1
			? gizmoRotationSnapDegrees_
			: gizmoScaleSnap_;
	const float snap[3] = { snapValue, snapValue, snapValue };

	ImGuizmo::SetOrthographic(isSprite);
	ImGuizmo::SetDrawlist(ImGui::GetWindowDrawList());
	ImGuizmo::SetRect(x, y, width, height);
	const bool changed = ImGuizmo::Manipulate(
		&viewMatrix.m[0][0],
		&projectionMatrix.m[0][0],
		operation,
		mode,
		&worldMatrix.m[0][0],
		nullptr,
		gizmoSnapEnabled_ ? snap : nullptr
	);
	if (!changed || !ImGuizmo::IsUsing()) {
		return;
	}

	Matrix4x4 localMatrix = worldMatrix;
	if (parentEntity) {
		localMatrix = Multiply(worldMatrix, Inverse(parentWorld));
	}

	Vector3 localScale{};
	Quaternion localRotate = MakeIdentityQuaternion();
	Vector3 localTranslate{};
	if (!DecomposeAffineMatrix(
		localMatrix,
		localScale,
		localRotate,
		localTranslate
	)) {
		return;
	}
	if (isSprite) {
		if (gizmoOperation_ == 0) {
			entity->transform.translate.x = localTranslate.x;
			entity->transform.translate.y = localTranslate.y;
		} else if (gizmoOperation_ == 1) {
			const Vector3 localEuler = MakeEulerFromQuaternion(localRotate);
			entity->transform.rotate = MakeQuaternionFromEuler({
				0.0f, 0.0f, localEuler.z
			});
		} else {
			entity->transform.scale.x = localScale.x /
				(std::max)(spriteRenderer->spriteSize.x, 0.001f);
			entity->transform.scale.y = localScale.y /
				(std::max)(spriteRenderer->spriteSize.y, 0.001f);
		}
	} else {
		if (gizmoOperation_ == 0) {
			entity->transform.translate = localTranslate;
		} else if (gizmoOperation_ == 1) {
			entity->transform.rotate = localRotate;
		} else {
			entity->transform.scale = localScale;
		}
	}
	document.MarkDirty();
}

void ImGuiManager::Finalize(){
	if (previewSoundData_.pBuffer && Audio::GetInstance()) {
		Audio::GetInstance()->SoundUnload(&previewSoundData_);
	}
	ImGui_ImplDX12_Shutdown();
	ImGui_ImplWin32_Shutdown();
	ImGui::DestroyContext();
	editorBaseFontData_.clear();
	editorJapaneseFontData_.clear();
	editorChineseFontData_.clear();
	editorGlyphRanges_.clear();
	instance = nullptr;
}

void ImGuiManager::RequestOpenScene(const std::string& sceneId) {
	if (!editorSession_ || !editorSession_->IsEditing() || sceneId.empty() ||
		sceneId == editorSession_->GetEditSceneId()) {
		return;
	}

	sceneSaveFailed_ = false;
	if (editorSession_->GetEditDocument().IsDirty()) {
		pendingSceneId_ = sceneId;
		sceneSwitchPopupRequested_ = true;
		return;
	}

	requestedSceneId_ = sceneId;
	requestedSceneDiscardUnsavedChanges_ = false;
}

void ImGuiManager::QueueSceneAssetRequest(
	const SceneAssetRequest& request
) {
	if (sceneAssetRequestPending_) {
		return;
	}
	requestedSceneAsset_ = request;
	sceneAssetRequestPending_ = true;
}

void ImGuiManager::QueueSceneInstanceRequest(
	const SceneInstanceRequest& request
) {
	if (sceneInstanceRequestPending_) {
		return;
	}
	requestedSceneInstance_ = request;
	sceneInstanceRequestPending_ = true;
}

void ImGuiManager::DrawSceneMenu() {
	if (!ImGui::BeginMenu("Scene")) {
		return;
	}

	const bool canOpenScene =
		editorSession_ && editorSession_->IsEditing() && sceneCatalog_;
	const bool currentSceneClean = canOpenScene &&
		!editorSession_->GetEditDocument().IsDirty();
	const SceneDescriptor* currentScene = canOpenScene
		? sceneCatalog_->Find(editorSession_->GetEditSceneId())
		: nullptr;
	const bool canManageScene = currentSceneClean && currentScene &&
		!sceneAssetRequestPending_;

	if (ImGui::MenuItem("New Scene...", nullptr, false, canManageScene)) {
		CopyTextBuffer(
			sceneAssetNameBuffer_,
			sizeof(sceneAssetNameBuffer_),
			"New Scene"
		);
		CopyTextBuffer(
			sceneAssetIdBuffer_,
			sizeof(sceneAssetIdBuffer_),
			"new_scene"
		);
		CopyTextBuffer(
			sceneAssetFileBuffer_,
			sizeof(sceneAssetFileBuffer_),
			"new_scene"
		);
		sceneTemplateIndex_ = 0;
		createScenePopupRequested_ = true;
	}
	if (ImGui::MenuItem(
		"Duplicate Active Scene...",
		nullptr,
		false,
		canManageScene
	)) {
		const std::string duplicateId = currentScene->id + "_copy";
		CopyTextBuffer(
			sceneAssetNameBuffer_,
			sizeof(sceneAssetNameBuffer_),
			currentScene->displayName + " Copy"
		);
		CopyTextBuffer(
			sceneAssetIdBuffer_,
			sizeof(sceneAssetIdBuffer_),
			duplicateId
		);
		CopyTextBuffer(
			sceneAssetFileBuffer_,
			sizeof(sceneAssetFileBuffer_),
			duplicateId
		);
		sceneAssetTargetId_ = currentScene->id;
		duplicateScenePopupRequested_ = true;
	}
	if (ImGui::MenuItem(
		"Rename Active Scene...",
		nullptr,
		false,
		canManageScene
	)) {
		CopyTextBuffer(
			sceneAssetNameBuffer_,
			sizeof(sceneAssetNameBuffer_),
			currentScene->displayName
		);
		CopyTextBuffer(
			sceneAssetFileBuffer_,
			sizeof(sceneAssetFileBuffer_),
			SceneAssetFileStem(*currentScene)
		);
		sceneAssetTargetId_ = currentScene->id;
		renameScenePopupRequested_ = true;
	}

	if (ImGui::BeginMenu("Delete Scene", canManageScene)) {
		bool hasDeleteCandidate = false;
		for (const SceneDescriptor& scene : sceneCatalog_->GetScenes()) {
			if (scene.id == currentScene->id ||
				scene.id == sceneCatalog_->GetStartSceneId()) {
				continue;
			}
			hasDeleteCandidate = true;
			const std::string label = scene.displayName + "##delete_" + scene.id;
			if (ImGui::MenuItem(label.c_str())) {
				sceneAssetTargetId_ = scene.id;
				deleteScenePopupRequested_ = true;
			}
		}
		if (!hasDeleteCandidate) {
			ImGui::TextDisabled("No deletable Scene");
		}
		ImGui::EndMenu();
	}
	if (canOpenScene && !currentSceneClean) {
		ImGui::TextDisabled("Save the active Scene to manage Scene assets.");
	}

	ImGui::Separator();
	ImGui::MenuItem("Loaded Scenes", nullptr, &showLoadedScenes_);

	ImGui::Separator();
	ImGui::TextDisabled("Open Scene");
	ImGui::BeginDisabled(!canOpenScene);
	if (sceneCatalog_ && editorSession_) {
		for (const SceneDescriptor& scene : sceneCatalog_->GetScenes()) {
			const std::string label = scene.displayName + "##" + scene.id;
			if (ImGui::MenuItem(
				label.c_str(),
				nullptr,
				editorSession_->GetEditSceneId() == scene.id
			)) {
				RequestOpenScene(scene.id);
			}
		}
	}
	ImGui::EndDisabled();
	ImGui::EndMenu();
}

void ImGuiManager::DrawSceneSwitchConfirmation() {
	if (sceneSwitchPopupRequested_) {
		ImGui::OpenPopup("Unsaved Scene");
		sceneSwitchPopupRequested_ = false;
	}
	if (!ImGui::BeginPopupModal(
		"Unsaved Scene",
		nullptr,
		ImGuiWindowFlags_AlwaysAutoResize
	)) {
		return;
	}

	const SceneDescriptor* pendingScene = sceneCatalog_
		? sceneCatalog_->Find(pendingSceneId_)
		: nullptr;
	ImGui::TextUnformatted("The current Scene has unsaved changes.");
	if (pendingScene) {
		ImGui::Text("Open: %s", pendingScene->displayName.c_str());
	}
	if (sceneSaveFailed_) {
		ImGui::TextColored(
			ImVec4(0.95f, 0.35f, 0.25f, 1.0f),
			"The current Scene could not be saved."
		);
	}
	ImGui::Separator();

	if (ImGui::Button("Save and Open")) {
		if (editorSession_ && editorSession_->Save()) {
			requestedSceneId_ = pendingSceneId_;
			requestedSceneDiscardUnsavedChanges_ = false;
			pendingSceneId_.clear();
			sceneSaveFailed_ = false;
			ImGui::CloseCurrentPopup();
		} else {
			sceneSaveFailed_ = true;
		}
	}
	ImGui::SameLine();
	if (ImGui::Button("Discard")) {
		requestedSceneId_ = pendingSceneId_;
		requestedSceneDiscardUnsavedChanges_ = true;
		pendingSceneId_.clear();
		sceneSaveFailed_ = false;
		ImGui::CloseCurrentPopup();
	}
	ImGui::SameLine();
	if (ImGui::Button("Cancel")) {
		pendingSceneId_.clear();
		sceneSaveFailed_ = false;
		ImGui::CloseCurrentPopup();
	}

	ImGui::EndPopup();
}

void ImGuiManager::DrawSceneAssetDialogs() {
	if (createScenePopupRequested_) {
		ImGui::OpenPopup("Create Scene");
		createScenePopupRequested_ = false;
	}
	if (ImGui::BeginPopupModal(
		"Create Scene",
		nullptr,
		ImGuiWindowFlags_AlwaysAutoResize
	)) {
		ImGui::InputText(
			"Name",
			sceneAssetNameBuffer_,
			sizeof(sceneAssetNameBuffer_)
		);
		ImGui::InputText(
			"Scene ID",
			sceneAssetIdBuffer_,
			sizeof(sceneAssetIdBuffer_)
		);
		ImGui::InputText(
			"File Name",
			sceneAssetFileBuffer_,
			sizeof(sceneAssetFileBuffer_)
		);
		ImGui::TextDisabled("Saved under resources/scenes as .scene.json");

		const std::vector<SceneTemplateDescriptor>* templates =
			sceneTemplateRegistry_
				? &sceneTemplateRegistry_->GetTemplates()
				: nullptr;
		if (templates && !templates->empty()) {
			sceneTemplateIndex_ = std::clamp(
				sceneTemplateIndex_,
				0,
				static_cast<int>(templates->size()) - 1
			);
			const char* preview =
				(*templates)[sceneTemplateIndex_].displayName.c_str();
			if (ImGui::BeginCombo("Template", preview)) {
				for (int index = 0; index < static_cast<int>(templates->size()); ++index) {
					if (ImGui::Selectable(
						(*templates)[index].displayName.c_str(),
						sceneTemplateIndex_ == index
					)) {
						sceneTemplateIndex_ = index;
					}
				}
				ImGui::EndCombo();
			}
		} else {
			ImGui::TextDisabled("No Scene templates are available.");
		}

		const bool canSubmit = templates && !templates->empty() &&
			sceneAssetNameBuffer_[0] != '\0' &&
			sceneAssetIdBuffer_[0] != '\0' &&
			sceneAssetFileBuffer_[0] != '\0';
		ImGui::BeginDisabled(!canSubmit);
		if (ImGui::Button("Create")) {
			SceneAssetRequest request{};
			request.operation = SceneAssetOperation::Create;
			request.sceneId = sceneAssetIdBuffer_;
			request.displayName = sceneAssetNameBuffer_;
			request.assetPath = BuildSceneAssetPath(sceneAssetFileBuffer_);
			request.templateId = (*templates)[sceneTemplateIndex_].id;
			QueueSceneAssetRequest(request);
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndDisabled();
		ImGui::SameLine();
		if (ImGui::Button("Cancel")) {
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndPopup();
	}

	if (duplicateScenePopupRequested_) {
		ImGui::OpenPopup("Duplicate Scene");
		duplicateScenePopupRequested_ = false;
	}
	if (ImGui::BeginPopupModal(
		"Duplicate Scene",
		nullptr,
		ImGuiWindowFlags_AlwaysAutoResize
	)) {
		const SceneDescriptor* source = sceneCatalog_
			? sceneCatalog_->Find(sceneAssetTargetId_)
			: nullptr;
		if (source) {
			ImGui::Text("Source: %s", source->displayName.c_str());
		}
		ImGui::InputText(
			"Name",
			sceneAssetNameBuffer_,
			sizeof(sceneAssetNameBuffer_)
		);
		ImGui::InputText(
			"Scene ID",
			sceneAssetIdBuffer_,
			sizeof(sceneAssetIdBuffer_)
		);
		ImGui::InputText(
			"File Name",
			sceneAssetFileBuffer_,
			sizeof(sceneAssetFileBuffer_)
		);
		const bool canSubmit = source && sceneAssetNameBuffer_[0] != '\0' &&
			sceneAssetIdBuffer_[0] != '\0' &&
			sceneAssetFileBuffer_[0] != '\0';
		ImGui::BeginDisabled(!canSubmit);
		if (ImGui::Button("Duplicate")) {
			SceneAssetRequest request{};
			request.operation = SceneAssetOperation::Duplicate;
			request.sourceSceneId = sceneAssetTargetId_;
			request.sceneId = sceneAssetIdBuffer_;
			request.displayName = sceneAssetNameBuffer_;
			request.assetPath = BuildSceneAssetPath(sceneAssetFileBuffer_);
			QueueSceneAssetRequest(request);
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndDisabled();
		ImGui::SameLine();
		if (ImGui::Button("Cancel")) {
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndPopup();
	}

	if (renameScenePopupRequested_) {
		ImGui::OpenPopup("Rename Scene");
		renameScenePopupRequested_ = false;
	}
	if (ImGui::BeginPopupModal(
		"Rename Scene",
		nullptr,
		ImGuiWindowFlags_AlwaysAutoResize
	)) {
		ImGui::Text("Scene ID: %s", sceneAssetTargetId_.c_str());
		ImGui::TextDisabled("Scene ID remains stable so references do not change.");
		ImGui::InputText(
			"Name",
			sceneAssetNameBuffer_,
			sizeof(sceneAssetNameBuffer_)
		);
		ImGui::InputText(
			"File Name",
			sceneAssetFileBuffer_,
			sizeof(sceneAssetFileBuffer_)
		);
		const bool canSubmit = !sceneAssetTargetId_.empty() &&
			sceneAssetNameBuffer_[0] != '\0' &&
			sceneAssetFileBuffer_[0] != '\0';
		ImGui::BeginDisabled(!canSubmit);
		if (ImGui::Button("Rename")) {
			SceneAssetRequest request{};
			request.operation = SceneAssetOperation::Rename;
			request.sceneId = sceneAssetTargetId_;
			request.displayName = sceneAssetNameBuffer_;
			request.assetPath = BuildSceneAssetPath(sceneAssetFileBuffer_);
			QueueSceneAssetRequest(request);
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndDisabled();
		ImGui::SameLine();
		if (ImGui::Button("Cancel")) {
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndPopup();
	}

	if (deleteScenePopupRequested_) {
		ImGui::OpenPopup("Delete Scene");
		deleteScenePopupRequested_ = false;
	}
	if (ImGui::BeginPopupModal(
		"Delete Scene",
		nullptr,
		ImGuiWindowFlags_AlwaysAutoResize
	)) {
		const SceneDescriptor* target = sceneCatalog_
			? sceneCatalog_->Find(sceneAssetTargetId_)
			: nullptr;
		ImGui::TextUnformatted("Delete the Scene asset and remove it from the Catalog?");
		if (target) {
			ImGui::Text("Scene: %s", target->displayName.c_str());
			ImGui::Text("Path: %s", target->assetPath.c_str());
		}
		ImGui::TextDisabled("Deletion is rejected when another Scene references it.");
		ImGui::BeginDisabled(!target);
		if (ImGui::Button("Delete")) {
			SceneAssetRequest request{};
			request.operation = SceneAssetOperation::Delete;
			request.sceneId = sceneAssetTargetId_;
			QueueSceneAssetRequest(request);
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndDisabled();
		ImGui::SameLine();
		if (ImGui::Button("Cancel")) {
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndPopup();
	}

	if (sceneAssetErrorPopupRequested_) {
		ImGui::OpenPopup("Scene Asset Error");
		sceneAssetErrorPopupRequested_ = false;
	}
	if (ImGui::BeginPopupModal(
		"Scene Asset Error",
		nullptr,
		ImGuiWindowFlags_AlwaysAutoResize
	)) {
		ImGui::TextWrapped("%s", sceneAssetErrorMessage_.c_str());
		if (ImGui::Button("OK")) {
			sceneAssetErrorMessage_.clear();
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndPopup();
	}
}

void ImGuiManager::DrawSettingsMenu() {
	if (!ImGui::BeginMenu("Settings")) {
		return;
	}
	if (ImGui::BeginMenu("Project", sceneCatalog_ != nullptr)) {
		if (ImGui::BeginMenu("Startup Scene")) {
			for (const SceneDescriptor& scene : sceneCatalog_->GetScenes()) {
				const bool selected =
					scene.id == sceneCatalog_->GetStartSceneId();
				const std::string label = scene.displayName + "##startup_" + scene.id;
				if (ImGui::MenuItem(
					label.c_str(),
					nullptr,
					selected,
					!startSceneRequestPending_
				) && !selected) {
					requestedStartSceneId_ = scene.id;
					startSceneRequestPending_ = true;
				}
			}
			ImGui::Separator();
			ImGui::TextDisabled("Saved in resources/scenes/scenes.json");
			ImGui::EndMenu();
		}
		if (ImGui::BeginMenu("Startup Mode")) {
			struct StartupModeEntry {
				const char* label;
				SceneBuildConfiguration configuration;
			};
			const StartupModeEntry entries[] = {
				{ "Debug", SceneBuildConfiguration::Debug },
				{ "Development", SceneBuildConfiguration::Development },
				{ "Release", SceneBuildConfiguration::Release }
			};
			for (const StartupModeEntry& entry : entries) {
				if (!ImGui::BeginMenu(entry.label)) {
					continue;
				}
				const SceneStartupMode currentMode =
					sceneCatalog_->GetStartupMode(entry.configuration);
				const bool editorAllowed =
					entry.configuration != SceneBuildConfiguration::Release;
				if (ImGui::MenuItem(
					"Editor",
					nullptr,
					currentMode == SceneStartupMode::Editor,
					editorAllowed && !startupModeRequestPending_
				)) {
					requestedStartupConfiguration_ = entry.configuration;
					requestedStartupMode_ = SceneStartupMode::Editor;
					startupModeRequestPending_ = true;
				}
				if (ImGui::MenuItem(
					"Runtime",
					nullptr,
					currentMode == SceneStartupMode::Runtime,
					!startupModeRequestPending_
				)) {
					requestedStartupConfiguration_ = entry.configuration;
					requestedStartupMode_ = SceneStartupMode::Runtime;
					startupModeRequestPending_ = true;
				}
				ImGui::EndMenu();
			}
			ImGui::Separator();
			ImGui::TextDisabled("Release supports Runtime startup only.");
			ImGui::EndMenu();
		}
		ImGui::EndMenu();
	}

	ImGui::Separator();

	if (ImGui::BeginMenu("Windows")) {
		ImGui::MenuItem("Hierarchy", nullptr, &showHierarchy_);
		ImGui::MenuItem("Inspector", nullptr, &showInspector_);
		ImGui::MenuItem("Project", nullptr, &showProject_);
		ImGui::MenuItem("Console", nullptr, &showConsole_);
		ImGui::MenuItem("Loaded Scenes", nullptr, &showLoadedScenes_);
		ImGui::Separator();
		if (ImGui::MenuItem("Reset Layout")) {
			resetLayout_ = true;
		}
		ImGui::EndMenu();
	}

	if (ImGui::BeginMenu("Appearance")) {
		if (ImGui::BeginMenu("Font")) {
			if (ImGui::MenuItem(
				"Original + CJK",
				nullptr,
				editorFontPreset_ == EditorFontPreset::OriginalWithCjk
			)) {
				editorFontPreset_ = EditorFontPreset::OriginalWithCjk;
				RequestEditorFontRebuild();
				SaveEditorSettings();
			}
			if (ImGui::MenuItem(
				"Unified CJK",
				nullptr,
				editorFontPreset_ == EditorFontPreset::UnifiedCjk
			)) {
				editorFontPreset_ = EditorFontPreset::UnifiedCjk;
				RequestEditorFontRebuild();
				SaveEditorSettings();
			}
			if (ImGui::MenuItem(
				"Cascadia Mono + CJK",
				nullptr,
				editorFontPreset_ == EditorFontPreset::CascadiaMonoWithCjk
			)) {
				editorFontPreset_ = EditorFontPreset::CascadiaMonoWithCjk;
				RequestEditorFontRebuild();
				SaveEditorSettings();
			}
			ImGui::EndMenu();
		}

		if (ImGui::DragFloat(
			"Font Size",
			&editorFontSize_,
			0.25f,
			10.0f,
			22.0f,
			"%.1f px"
		)) {
			RequestEditorFontRebuild();
		}
		if (ImGui::IsItemDeactivatedAfterEdit()) {
			SaveEditorSettings();
		}
		ImGui::EndMenu();
	}

	ImGui::EndMenu();
}

void ImGuiManager::DrawProjectSettingsDialogs() {
	if (projectSettingsErrorPopupRequested_) {
		ImGui::OpenPopup("Project Settings Error");
		projectSettingsErrorPopupRequested_ = false;
	}
	if (ImGui::BeginPopupModal(
		"Project Settings Error",
		nullptr,
		ImGuiWindowFlags_AlwaysAutoResize
	)) {
		ImGui::TextWrapped("%s", projectSettingsErrorMessage_.c_str());
		if (ImGui::Button("OK")) {
			projectSettingsErrorMessage_.clear();
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndPopup();
	}
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
		DrawSceneMenu();
		DrawSettingsMenu();
		DrawPlaybackControls();
		ImGui::EndMenuBar();
	}
	DrawSceneSwitchConfirmation();
	DrawSceneAssetDialogs();
	DrawProjectSettingsDialogs();

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
	ImGui::TextDisabled("F1 Play/Stop / F2 Pause");

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
	ImGui::BeginDisabled(
		!editorSession_->IsEditing() ||
		!editorSession_->CanUndo()
	);
	const bool undoRequested = ImGui::SmallButton("Undo");
	ImGui::EndDisabled();
	ImGui::SameLine();
	ImGui::BeginDisabled(
		!editorSession_->IsEditing() ||
		!editorSession_->CanRedo()
	);
	const bool redoRequested = ImGui::SmallButton("Redo");
	ImGui::EndDisabled();
	if (
		editorSession_->IsEditing() &&
		(undoRequested || (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Z, false)))
	) {
		editorSession_->Undo();
	}
	if (
		editorSession_->IsEditing() &&
		(redoRequested || (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Y, false)))
	) {
		editorSession_->Redo();
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
		0.22f,
		nullptr,
		&centerId
	);
	const ImGuiID rightId = ImGui::DockBuilderSplitNode(
		centerId,
		ImGuiDir_Right,
		0.51f,
		nullptr,
		&centerId
	);
	const ImGuiID bottomId = ImGui::DockBuilderSplitNode(
		centerId,
		ImGuiDir_Down,
		0.42f,
		nullptr,
		&centerId
	);

	ImGui::DockBuilderDockWindow("Scene", centerId);
	ImGui::DockBuilderDockWindow("Hierarchy", leftId);
	ImGui::DockBuilderDockWindow("Inspector", rightId);
	ImGui::DockBuilderDockWindow("Scene Controls", rightId);
	ImGui::DockBuilderDockWindow("Title Scene", rightId);
	ImGui::DockBuilderDockWindow("Particle Effect Editor", rightId);
	ImGui::DockBuilderDockWindow("Environment", rightId);
	ImGui::DockBuilderDockWindow("Post Process Stack", rightId);
	ImGui::DockBuilderDockWindow("Scene Particles", rightId);
	ImGui::DockBuilderDockWindow("Monitor Debug", bottomId);
	ImGui::DockBuilderDockWindow("Project", bottomId);
	ImGui::DockBuilderDockWindow("Console", bottomId);
	ImGui::DockBuilderDockWindow("Loaded Scenes", bottomId);
	ImGui::DockBuilderFinish(dockSpaceId);
}

void ImGuiManager::DrawLoadedScenesWindow() {
	ImGui::Begin("Loaded Scenes", &showLoadedScenes_);

	const bool runtimeMode = editorSession_ && !editorSession_->IsEditing();
	if (!sceneManager_ || !sceneCatalog_) {
		ImGui::TextDisabled("Scene runtime is not available.");
		ImGui::End();
		return;
	}
	if (!runtimeMode) {
		ImGui::TextDisabled("Additive Scene operations are available in Play Mode.");
	}

	const std::vector<const SceneInstance*> loadedScenes =
		sceneManager_->GetLoadedSceneInstances();
	const SceneInstanceId activeInstanceId =
		sceneManager_->GetActiveSceneInstanceId();

	if (ImGui::BeginTable(
		"LoadedSceneInstances",
		5,
		ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
		ImGuiTableFlags_SizingStretchProp
	)) {
		ImGui::TableSetupColumn("Scene");
		ImGui::TableSetupColumn("Instance Key");
		ImGui::TableSetupColumn("Active");
		ImGui::TableSetupColumn("Persistent");
		ImGui::TableSetupColumn("Actions");
		ImGui::TableHeadersRow();

		for (const SceneInstance* instance : loadedScenes) {
			if (!instance) {
				continue;
			}
			const SceneInstanceId instanceId = instance->GetId();
			ImGui::PushID(static_cast<int>(instanceId));
			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			ImGui::TextUnformatted(instance->GetSceneId().c_str());
			ImGui::TextDisabled(
				"ID: %llu",
				static_cast<unsigned long long>(instanceId)
			);

			ImGui::TableSetColumnIndex(1);
			ImGui::TextUnformatted(instance->GetInstanceKey().c_str());

			ImGui::TableSetColumnIndex(2);
			const bool isActive = instanceId == activeInstanceId;
			if (isActive) {
				ImGui::TextUnformatted("Active");
			} else {
				ImGui::BeginDisabled(!runtimeMode || sceneInstanceRequestPending_);
				if (ImGui::SmallButton("Set Active")) {
					SceneInstanceRequest request{};
					request.operation = SceneInstanceOperation::SetActive;
					request.instanceId = instanceId;
					QueueSceneInstanceRequest(request);
				}
				ImGui::EndDisabled();
			}

			ImGui::TableSetColumnIndex(3);
			bool persistent = instance->IsPersistent();
			ImGui::BeginDisabled(!runtimeMode || sceneInstanceRequestPending_);
			if (ImGui::Checkbox("##Persistent", &persistent)) {
				SceneInstanceRequest request{};
				request.operation = SceneInstanceOperation::SetPersistent;
				request.instanceId = instanceId;
				request.persistent = persistent;
				QueueSceneInstanceRequest(request);
			}
			ImGui::EndDisabled();

			ImGui::TableSetColumnIndex(4);
			const bool canUnload = runtimeMode && loadedScenes.size() > 1 &&
				!sceneInstanceRequestPending_;
			ImGui::BeginDisabled(!canUnload);
			if (ImGui::SmallButton("Unload")) {
				SceneInstanceRequest request{};
				request.operation = SceneInstanceOperation::Unload;
				request.instanceId = instanceId;
				QueueSceneInstanceRequest(request);
			}
			ImGui::EndDisabled();
			ImGui::PopID();
		}
		ImGui::EndTable();
	}

	ImGui::SeparatorText("Additive Load");
	const std::vector<SceneDescriptor>& catalogScenes = sceneCatalog_->GetScenes();
	static int selectedSceneIndex = 0;
	if (selectedSceneIndex >= static_cast<int>(catalogScenes.size())) {
		selectedSceneIndex = 0;
	}
	const char* selectedLabel = catalogScenes.empty()
		? "No registered Scenes"
		: catalogScenes[selectedSceneIndex].displayName.c_str();
	ImGui::BeginDisabled(!runtimeMode || catalogScenes.empty() ||
		sceneInstanceRequestPending_);
	if (ImGui::BeginCombo("Scene", selectedLabel)) {
		for (int index = 0; index < static_cast<int>(catalogScenes.size()); ++index) {
			const bool selected = index == selectedSceneIndex;
			if (ImGui::Selectable(
				catalogScenes[index].displayName.c_str(),
				selected
			)) {
				selectedSceneIndex = index;
			}
			if (selected) {
				ImGui::SetItemDefaultFocus();
			}
		}
		ImGui::EndCombo();
	}
	ImGui::InputText(
		"Instance Key (optional)",
		additiveInstanceKeyBuffer_,
		sizeof(additiveInstanceKeyBuffer_)
	);
	if (ImGui::Button("Load Additive") && !catalogScenes.empty()) {
		SceneInstanceRequest request{};
		request.operation = SceneInstanceOperation::LoadAdditive;
		request.sceneId = catalogScenes[selectedSceneIndex].id;
		request.instanceKey = additiveInstanceKeyBuffer_;
		QueueSceneInstanceRequest(request);
	}
	ImGui::EndDisabled();

	if (!sceneInstanceStatusMessage_.empty()) {
		const ImVec4 color = sceneInstanceOperationSucceeded_
			? ImVec4(0.45f, 0.85f, 0.50f, 1.0f)
			: ImVec4(0.95f, 0.40f, 0.35f, 1.0f);
		ImGui::TextColored(color, "%s", sceneInstanceStatusMessage_.c_str());
	}

	ImGui::End();
}

void ImGuiManager::DrawHierarchyWindow(const char* sceneName) {
	ImGui::Begin("Hierarchy", &showHierarchy_);
	if (editorSession_) {
		SceneDocument& document = editorSession_->GetActiveDocument();
		for (
			auto it = selectedEntityIds_.begin();
			it != selectedEntityIds_.end();
		) {
			if (!document.FindEntity(*it)) {
				it = selectedEntityIds_.erase(it);
			} else {
				++it;
			}
		}
		if (selectedEntityId_ == 0) {
			selectedEntityIds_.clear();
			hierarchySelectionAnchorId_ = 0;
		} else if (selectedEntityId_ != hierarchyObservedEntityId_) {
			// Scene ViewなどHierarchy外からの選択は単体選択として受け取り、表示位置を追従する。
			selectedEntityIds_.clear();
			selectedEntityIds_.insert(selectedEntityId_);
			hierarchySelectionAnchorId_ = selectedEntityId_;
			hierarchyRevealRequested_ = true;
		}
		ImGui::BeginDisabled(!editorSession_->IsEditing());
		bool createRequested = false;
		bool createFolderRequested = false;
		bool createCameraPathRequested = false;
		uint64_t createParentId = 0;
		if (ImGui::SmallButton("+")) {
			createRequested = true;
		}
		if (ImGui::IsItemHovered()) {
			ImGui::SetTooltip("Create Empty Entity");
		}
		ImGui::SameLine();
		if (ImGui::SmallButton("+ Folder")) {
			createFolderRequested = true;
		}
		if (ImGui::IsItemHovered()) {
			ImGui::SetTooltip("Create Folder");
		}
		ImGui::SameLine();
		if (ImGui::SmallButton("+ Path")) {
			createCameraPathRequested = true;
		}
		if (ImGui::IsItemHovered()) {
			ImGui::SetTooltip("Create CameraPath with two child points");
		}
		ImGui::EndDisabled();
		ImGui::SameLine();
		ImGui::TextDisabled("%zu entities", document.GetEntities().size());
		ImGui::SetNextItemWidth(-1.0f);
		ImGui::InputTextWithHint(
			"##HierarchySearch",
			"Search... team:Fish type:Camera is:inactive",
			hierarchySearchBuffer_,
			sizeof(hierarchySearchBuffer_)
		);
		ImGui::Separator();

		uint64_t removeId = 0;
		uint64_t duplicateId = 0;
		uint64_t reorderId = 0;
		uint64_t reorderTargetId = 0;
		bool reorderAfter = false;
		uint64_t reparentId = 0;
		uint64_t reparentTargetId = 0;
		std::vector<uint64_t> hierarchyDroppedIds;
		uint64_t moveId = 0;
		int moveDirection = 0;
		if (
			editorSession_->IsEditing() &&
			selectedEntityId_ != 0 &&
			ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
			!ImGui::GetIO().WantTextInput
		) {
			const SceneEntity* selectedEntity = document.FindEntity(selectedEntityId_);
			if (ImGui::IsKeyPressed(ImGuiKey_F, false)) {
				FocusSceneCameraOnSelection();
			}
			if (selectedEntity && !selectedEntity->locked) {
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
		}

		auto drawDropLine = [](const ImVec2& start, const ImVec2& end) {
			ImGui::GetWindowDrawList()->AddLine(
				start,
				end,
				IM_COL32(90, 180, 255, 255),
				2.0f
			);
		};
		auto drawDropRect = [](const ImVec2& itemMin, const ImVec2& itemMax) {
			ImGui::GetWindowDrawList()->AddRect(
				itemMin,
				itemMax,
				IM_COL32(90, 180, 255, 255),
				3.0f,
				0,
				2.0f
			);
		};
		auto toLower = [](std::string value) {
			std::transform(
				value.begin(),
				value.end(),
				value.begin(),
				[](unsigned char ch) {
					return static_cast<char>(std::tolower(ch));
				}
			);
			return value;
		};
		const std::string hierarchySearch = toLower(hierarchySearchBuffer_);
		std::vector<std::string> hierarchyFilterTerms;
		for (size_t begin = 0; begin < hierarchySearch.size();) {
			while (
				begin < hierarchySearch.size() &&
				std::isspace(static_cast<unsigned char>(hierarchySearch[begin]))
			) {
				++begin;
			}
			const size_t end = hierarchySearch.find_first_of(" \t\r\n", begin);
			if (begin < hierarchySearch.size()) {
				hierarchyFilterTerms.push_back(
					hierarchySearch.substr(begin, end - begin)
				);
			}
			begin = end == std::string::npos ? hierarchySearch.size() : end + 1;
		}
		const bool searchActive = !hierarchyFilterTerms.empty();
		uint64_t hierarchyDropTargetId = 0;
		bool hierarchyDropAfter = false;
		bool hierarchyDropIntoFolder = false;
		bool hierarchyDropToRoot = false;
		if (!editorSession_->IsEditing() || searchActive) {
			hierarchyDragSourceId_ = 0;
			hierarchyDragActive_ = false;
		}
		auto entityNameMatches = [&](const SceneEntity& entity) {
			for (const std::string& term : hierarchyFilterTerms) {
				const size_t separator = term.find(':');
				if (separator == std::string::npos) {
					if (toLower(entity.name).find(term) == std::string::npos) {
						return false;
					}
					continue;
				}

				const std::string key = term.substr(0, separator);
				const std::string value = term.substr(separator + 1);
				if (value.empty()) {
					continue;
				}
				if (key == "team") {
					const SceneTeamSettings* team = document.ResolveEntityTeam(entity);
					if (!team || toLower(team->name).find(value) == std::string::npos) {
						return false;
					}
				} else if (key == "type") {
					bool typeMatches = entity.folder && value == "folder";
					for (const SceneComponent& component : entity.components) {
						if (toLower(component.type) == value) {
							typeMatches = true;
							break;
						}
					}
					if (!typeMatches) {
						return false;
					}
				} else if (key == "is") {
					const bool matches =
						(value == "active" && entity.active) ||
						(value == "inactive" && !entity.active) ||
						(value == "locked" && entity.locked) ||
						(value == "unlocked" && !entity.locked) ||
						(value == "folder" && entity.folder);
					if (!matches) {
						return false;
					}
				} else {
					// 未対応の条件は名前検索として扱い、検索結果が空になる事故を避ける。
					if (toLower(entity.name).find(term) == std::string::npos) {
						return false;
					}
				}
			}
			return true;
		};
		std::function<bool(uint64_t)> entityVisibleInFilter;
		entityVisibleInFilter = [&](uint64_t entityId) {
			const SceneEntity* entity = document.FindEntity(entityId);
			if (!entity) {
				return false;
			}
			if (entityNameMatches(*entity)) {
				return true;
			}
			for (const SceneEntity& child : document.GetEntities()) {
				if (
					child.parentId == entityId &&
					entityVisibleInFilter(child.id)
				) {
					return true;
				}
			}
			return false;
		};
		std::function<bool(uint64_t)> entitySubtreeHasLocked;
		entitySubtreeHasLocked = [&](uint64_t entityId) {
			const SceneEntity* entity = document.FindEntity(entityId);
			if (!entity) {
				return false;
			}
			if (entity->locked) {
				return true;
			}
			for (const SceneEntity& child : document.GetEntities()) {
				if (
					child.parentId == entityId &&
					entitySubtreeHasLocked(child.id)
				) {
					return true;
				}
			}
			return false;
		};
		std::vector<uint64_t> hierarchyFilterOrder;
		std::function<void(uint64_t)> collectFilterOrder;
		collectFilterOrder = [&](uint64_t entityId) {
			if (!entityVisibleInFilter(entityId)) {
				return;
			}
			hierarchyFilterOrder.push_back(entityId);
			for (const SceneEntity& child : document.GetEntities()) {
				if (child.parentId == entityId) {
					collectFilterOrder(child.id);
				}
			}
		};
		for (const SceneEntity& entity : document.GetEntities()) {
			if (entity.parentId == 0) {
				collectFilterOrder(entity.id);
			}
		}
		auto isEntitySelected = [&](uint64_t entityId) {
			return selectedEntityIds_.find(entityId) != selectedEntityIds_.end();
		};
		auto getSelectedRoots = [&]() {
			std::vector<uint64_t> roots;
			for (const SceneEntity& candidate : document.GetEntities()) {
				if (!isEntitySelected(candidate.id)) {
					continue;
				}
				bool selectedAncestor = false;
				for (
					const SceneEntity* parent = document.FindEntity(candidate.parentId);
					parent;
					parent = document.FindEntity(parent->parentId)
				) {
					if (isEntitySelected(parent->id)) {
						selectedAncestor = true;
						break;
					}
				}
				if (!selectedAncestor) {
					roots.push_back(candidate.id);
				}
			}
			return roots;
		};
		auto getDraggedRoots = [&]() {
			if (hierarchyDragSourceId_ == 0) {
				return std::vector<uint64_t>{};
			}
			if (isEntitySelected(hierarchyDragSourceId_)) {
				return getSelectedRoots();
			}
			return std::vector<uint64_t>{ hierarchyDragSourceId_ };
		};
		auto rootsCanBeEdited = [&](const std::vector<uint64_t>& roots) {
			return !roots.empty() && std::all_of(
				roots.begin(),
				roots.end(),
				[&](uint64_t entityId) {
					const SceneEntity* entity = document.FindEntity(entityId);
					return entity && !entity->locked && !entitySubtreeHasLocked(entityId);
				}
			);
		};
			auto selectEntity = [&](uint64_t entityId, bool extend, bool range) {
			if (range && hierarchySelectionAnchorId_ != 0) {
				const auto anchor = std::find(
					hierarchyFilterOrder.begin(),
					hierarchyFilterOrder.end(),
					hierarchySelectionAnchorId_
				);
				const auto target = std::find(
					hierarchyFilterOrder.begin(),
					hierarchyFilterOrder.end(),
					entityId
				);
				if (anchor != hierarchyFilterOrder.end() && target != hierarchyFilterOrder.end()) {
					selectedEntityIds_.clear();
					const auto first = (std::min)(anchor, target);
					const auto last = (std::max)(anchor, target);
					for (auto it = first; it != std::next(last); ++it) {
						selectedEntityIds_.insert(*it);
					}
				} else {
					selectedEntityIds_.clear();
					selectedEntityIds_.insert(entityId);
					hierarchySelectionAnchorId_ = entityId;
				}
			} else if (extend) {
				if (isEntitySelected(entityId)) {
					selectedEntityIds_.erase(entityId);
				} else {
					selectedEntityIds_.insert(entityId);
					hierarchySelectionAnchorId_ = entityId;
				}
			} else {
				selectedEntityIds_.clear();
				selectedEntityIds_.insert(entityId);
				hierarchySelectionAnchorId_ = entityId;
			}
			selectedEntityId_ = selectedEntityIds_.empty()
				? 0
				: entityId;
			selectedProjectFile_.clear();
			showInspector_ = true;
			revealInspectorRequested_ = true;
		};
		auto setSelectedActive = [&](bool active, uint64_t clickedEntityId) {
			const bool applyToSelection = isEntitySelected(clickedEntityId);
			bool changed = false;
			for (SceneEntity& candidate : document.GetEntities()) {
				if ((applyToSelection && isEntitySelected(candidate.id)) ||
					(!applyToSelection && candidate.id == clickedEntityId)) {
					if (candidate.active != active) {
						candidate.active = active;
						changed = true;
					}
				}
			}
			if (changed) {
				document.MarkDirty();
			}
		};
		auto setSelectedLocked = [&](bool locked, uint64_t clickedEntityId) {
			const bool applyToSelection = isEntitySelected(clickedEntityId);
			bool changed = false;
			for (SceneEntity& candidate : document.GetEntities()) {
				if ((applyToSelection && isEntitySelected(candidate.id)) ||
					(!applyToSelection && candidate.id == clickedEntityId)) {
					if (candidate.locked != locked) {
						candidate.locked = locked;
						changed = true;
					}
				}
			}
			if (changed) {
				document.MarkDirty();
			}
		};

		std::function<void(uint64_t)> drawEntity;
		drawEntity = [&](uint64_t entityId) {
			const SceneEntity* entity = document.FindEntity(entityId);
			if (!entity) {
				return;
			}
			if (!entityVisibleInFilter(entityId)) {
				return;
			}
			const size_t childCount = static_cast<size_t>(std::count_if(
				document.GetEntities().begin(),
				document.GetEntities().end(),
				[&](const SceneEntity& candidate) {
					return candidate.parentId == entityId &&
						entityVisibleInFilter(candidate.id);
				}
			));
			const bool hasChildren = childCount > 0;
			const bool editable = editorSession_->IsEditing() && !entity->locked;

			ImGui::PushID(static_cast<int>(entity->id));
			ImGui::BeginDisabled(!editorSession_->IsEditing());
			bool active = entity->active;
			if (ImGui::SmallButton(active ? "V" : "-")) {
				setSelectedActive(!active, entity->id);
			}
			if (ImGui::IsItemHovered()) {
				ImGui::SetTooltip(active ? "Hide Entity" : "Show Entity");
			}
			ImGui::SameLine();
			bool locked = entity->locked;
			if (ImGui::SmallButton(locked ? "L" : "U")) {
				setSelectedLocked(!locked, entity->id);
			}
			if (ImGui::IsItemHovered()) {
				ImGui::SetTooltip(locked ? "Unlock Editing" : "Lock Editing");
			}
			ImGui::EndDisabled();
			ImGui::SameLine();
			ImGuiTreeNodeFlags flags =
				ImGuiTreeNodeFlags_OpenOnArrow |
				ImGuiTreeNodeFlags_SpanAvailWidth;
			if (!hasChildren) {
				flags |= ImGuiTreeNodeFlags_Leaf |
					ImGuiTreeNodeFlags_NoTreePushOnOpen;
			}
			if (isEntitySelected(entity->id)) {
				flags |= ImGuiTreeNodeFlags_Selected;
			}
			const std::string folderPrefix = entity->folder
				? "[Folder " + std::to_string(childCount) + "]  "
				: "";
			const std::string label = entity->locked
				? folderPrefix + entity->name + " [locked]"
				: entity->active
					? folderPrefix + entity->name
					: folderPrefix + entity->name + " (inactive)";
			const SceneTeamSettings* effectiveTeam =
				document.ResolveEntityTeam(*entity);
			const std::string labelWithTeam = effectiveTeam
				? label + " {" + effectiveTeam->name + "}"
				: label;
			const bool revealAncestor =
				hierarchyRevealRequested_ &&
				selectedEntityId_ != 0 &&
				document.IsDescendantOf(selectedEntityId_, entity->id);
			if (
				searchActive ||
				revealAncestor ||
				(
					hierarchyAutoOpenFolderId_ == entity->id &&
					ImGui::GetTime() - hierarchyAutoOpenStartTime_ >= 0.55
				)
			) {
				ImGui::SetNextItemOpen(true, ImGuiCond_Always);
			}
			if (entity->folder) {
				ImGui::PushStyleVar(
					ImGuiStyleVar_FramePadding,
					ImVec2(ImGui::GetStyle().FramePadding.x, 5.0f)
				);
			}
			const bool renaming = hierarchyRenameEntityId_ == entity->id;
			const bool open = ImGui::TreeNodeEx(
				"##Entity",
				flags,
				"%s",
				renaming ? " " : labelWithTeam.c_str()
			);
			if (entity->folder) {
				ImGui::PopStyleVar();
			}
			const ImVec2 itemMin = ImGui::GetItemRectMin();
			const ImVec2 itemMax = ImGui::GetItemRectMax();
			const ImVec2 nextItemCursor = ImGui::GetCursorScreenPos();
			const bool treeItemClicked =
				ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen();
			const bool treeItemDoubleClicked =
				ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) &&
				ImGui::IsMouseHoveringRect(itemMin, itemMax);
			if (treeItemClicked && !renaming) {
				selectEntity(
					entity->id,
					ImGui::GetIO().KeyCtrl,
					ImGui::GetIO().KeyShift
				);
			}
			if (treeItemDoubleClicked && editable && !renaming) {
				hierarchyRenameEntityId_ = entity->id;
				strncpy_s(hierarchyRenameBuffer_, entity->name.c_str(), _TRUNCATE);
				hierarchyRenameFocusRequested_ = true;
			}
			if (renaming) {
				const float nameStart = itemMin.x + ImGui::GetTreeNodeToLabelSpacing();
				ImGui::SetCursorScreenPos(ImVec2(nameStart, itemMin.y));
				ImGui::SetNextItemWidth((std::max)(itemMax.x - nameStart - 4.0f, 80.0f));
				if (hierarchyRenameFocusRequested_) {
					ImGui::SetKeyboardFocusHere();
					hierarchyRenameFocusRequested_ = false;
				}
				const bool renameCommitted = ImGui::InputText(
					"##RenameEntity",
					hierarchyRenameBuffer_,
					sizeof(hierarchyRenameBuffer_),
					ImGuiInputTextFlags_AutoSelectAll |
						ImGuiInputTextFlags_EnterReturnsTrue
				);
				const bool renameCanceled = ImGui::IsKeyPressed(ImGuiKey_Escape, false);
				if (renameCommitted && hierarchyRenameBuffer_[0] != '\0') {
					if (SceneEntity* mutableEntity = document.FindEntity(entity->id)) {
						mutableEntity->name = hierarchyRenameBuffer_;
						document.MarkDirty();
					}
					hierarchyRenameEntityId_ = 0;
				} else if (renameCanceled || ImGui::IsItemDeactivated()) {
					hierarchyRenameEntityId_ = 0;
				}
				ImGui::SetCursorScreenPos(nextItemCursor);
			}
			if (hierarchyRevealRequested_ && selectedEntityId_ == entity->id) {
				ImGui::SetScrollHereY(0.5f);
				hierarchyRevealRequested_ = false;
			}

			const bool rowHovered =
				ImGui::IsMouseHoveringRect(itemMin, itemMax);
			if (
				editable &&
				!searchActive &&
				rootsCanBeEdited(
					isEntitySelected(entity->id)
						? getSelectedRoots()
						: std::vector<uint64_t>{ entity->id }
				) &&
				rowHovered &&
				ImGui::IsMouseClicked(ImGuiMouseButton_Left)
			) {
				hierarchyDragSourceId_ = entity->id;
				hierarchyDragActive_ = false;
			}
			if (
				hierarchyDragSourceId_ == entity->id &&
				ImGui::IsMouseDragging(ImGuiMouseButton_Left, 4.0f)
			) {
				hierarchyDragActive_ = true;
			}
			if (hierarchyDragActive_ && hierarchyDragSourceId_ != 0) {
				const std::vector<uint64_t> draggedRoots = getDraggedRoots();
				const bool targetIsDragged = std::find(
					draggedRoots.begin(),
					draggedRoots.end(),
					entity->id
				) != draggedRoots.end();
				if (
					rootsCanBeEdited(draggedRoots) &&
					!targetIsDragged &&
					!entity->locked &&
					rowHovered
				) {
					const float itemHeight =
						(std::max)(itemMax.y - itemMin.y, 1.0f);
					const float localY = ImGui::GetMousePos().y - itemMin.y;
					const bool canDropIntoFolder = entity->folder && std::all_of(
						draggedRoots.begin(),
						draggedRoots.end(),
						[&](uint64_t draggedId) {
							return !document.IsDescendantOf(entity->id, draggedId);
						}
					);
					const bool canDropAsSibling = std::all_of(
						draggedRoots.begin(),
						draggedRoots.end(),
						[&](uint64_t draggedId) {
							return entity->parentId == 0 ||
								!document.IsDescendantOf(entity->parentId, draggedId);
						}
					);
					if (
						canDropIntoFolder &&
						localY >= itemHeight * 0.25f &&
						localY <= itemHeight * 0.75f
					) {
						if (hierarchyAutoOpenFolderId_ != entity->id) {
							hierarchyAutoOpenFolderId_ = entity->id;
							hierarchyAutoOpenStartTime_ = ImGui::GetTime();
						} else if (ImGui::GetTime() - hierarchyAutoOpenStartTime_ >= 0.55) {
							hierarchyAutoOpenFolderId_ = entity->id;
						}
						hierarchyDropTargetId = entity->id;
						hierarchyDropAfter = false;
						hierarchyDropIntoFolder = true;
					} else if (canDropAsSibling) {
						hierarchyDropTargetId = entity->id;
						hierarchyDropAfter =
							ImGui::GetMousePos().y >= (itemMin.y + itemMax.y) * 0.5f;
						hierarchyDropIntoFolder = false;
					}
				}
			}
			if (
				hierarchyDragActive_ &&
				hierarchyDropTargetId == entity->id
			) {
				if (hierarchyDropIntoFolder) {
					drawDropRect(itemMin, itemMax);
				} else if (hierarchyDropAfter) {
					drawDropLine(ImVec2(itemMin.x, itemMax.y), itemMax);
				} else {
					drawDropLine(itemMin, ImVec2(itemMax.x, itemMin.y));
				}
			}

			if (
				editable &&
				ImGui::BeginPopupContextItem("EntityContext")
			) {
				if (ImGui::MenuItem("Create Child")) {
					createRequested = true;
					createParentId = entity->id;
				}
				if (ImGui::MenuItem("Create Child Folder")) {
					createFolderRequested = true;
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
					if (
						child.parentId == entity->id &&
						entityVisibleInFilter(child.id)
					) {
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
		const bool rootOpen = ImGui::TreeNodeEx(
			"##SceneRoot",
			ImGuiTreeNodeFlags_DefaultOpen |
				ImGuiTreeNodeFlags_OpenOnArrow |
				ImGuiTreeNodeFlags_SpanAvailWidth,
			"%s",
			rootName
		);
		const ImVec2 rootItemMin = ImGui::GetItemRectMin();
		const ImVec2 rootItemMax = ImGui::GetItemRectMax();
		const std::vector<uint64_t> draggedRoots = getDraggedRoots();
		if (
			hierarchyDragActive_ &&
			rootsCanBeEdited(draggedRoots) &&
			std::any_of(
				draggedRoots.begin(),
				draggedRoots.end(),
				[&](uint64_t entityId) {
					const SceneEntity* entity = document.FindEntity(entityId);
					return entity && entity->parentId != 0;
				}
			) &&
			ImGui::IsMouseHoveringRect(rootItemMin, rootItemMax)
		) {
			hierarchyDropToRoot = true;
			drawDropRect(rootItemMin, rootItemMax);
		}
		if (rootOpen) {
			for (const SceneEntity& entity : document.GetEntities()) {
				if (entity.parentId == 0) {
					drawEntity(entity.id);
				}
			}
			ImGui::TreePop();
		}
		if (hierarchyDragActive_) {
			const ImVec2 mouse = ImGui::GetMousePos();
			const ImVec2 windowPos = ImGui::GetWindowPos();
			const ImVec2 windowSize = ImGui::GetWindowSize();
			const float edgeSize = 28.0f;
			const float scrollStep = 420.0f * ImGui::GetIO().DeltaTime;
			if (mouse.y < windowPos.y + edgeSize) {
				ImGui::SetScrollY((std::max)(0.0f, ImGui::GetScrollY() - scrollStep));
			} else if (mouse.y > windowPos.y + windowSize.y - edgeSize) {
				ImGui::SetScrollY((std::min)(
					ImGui::GetScrollMaxY(),
					ImGui::GetScrollY() + scrollStep
				));
			}
		}

		if (
			hierarchyDragSourceId_ != 0 &&
			!ImGui::IsMouseDown(ImGuiMouseButton_Left)
		) {
			if (
				hierarchyDragActive_ &&
				(hierarchyDropTargetId != 0 || hierarchyDropToRoot)
			) {
				hierarchyDroppedIds = getDraggedRoots();
				if (hierarchyDropToRoot) {
					reparentId = hierarchyDragSourceId_;
					reparentTargetId = 0;
				} else if (hierarchyDropIntoFolder) {
					reparentId = hierarchyDragSourceId_;
					reparentTargetId = hierarchyDropTargetId;
				} else {
					reorderId = hierarchyDragSourceId_;
					reorderTargetId = hierarchyDropTargetId;
					reorderAfter = hierarchyDropAfter;
				}
			}
			hierarchyDragSourceId_ = 0;
			hierarchyDragActive_ = false;
			hierarchyAutoOpenFolderId_ = 0;
			hierarchyAutoOpenStartTime_ = 0.0;
		}

		if (reorderId != 0) {
			const SceneEntity* targetEntity = document.FindEntity(reorderTargetId);
			if (
				targetEntity &&
				!targetEntity->locked &&
				rootsCanBeEdited(hierarchyDroppedIds)
			) {
				if (reorderAfter) {
					uint64_t insertAfterId = reorderTargetId;
					for (uint64_t entityId : hierarchyDroppedIds) {
						if (document.MoveEntityToSibling(entityId, insertAfterId, true)) {
							insertAfterId = entityId;
						}
					}
				} else {
					uint64_t insertBeforeId = reorderTargetId;
					for (auto it = hierarchyDroppedIds.rbegin();
						it != hierarchyDroppedIds.rend(); ++it) {
						if (document.MoveEntityToSibling(*it, insertBeforeId, false)) {
							insertBeforeId = *it;
						}
					}
				}
			}
		}
		if (reparentId != 0) {
			const SceneEntity* targetEntity = document.FindEntity(reparentTargetId);
			if (
				(
					reparentTargetId == 0 ||
					(targetEntity && targetEntity->folder && !targetEntity->locked)
				) &&
				rootsCanBeEdited(hierarchyDroppedIds)
			) {
				for (uint64_t entityId : hierarchyDroppedIds) {
					document.MoveEntityToParent(entityId, reparentTargetId);
				}
			}
		}
		if (moveId != 0) {
			if (const SceneEntity* entity = document.FindEntity(moveId)) {
				if (!entity->locked) {
					document.MoveEntity(moveId, moveDirection);
				}
			}
		}
		if (removeId != 0) {
			const std::vector<uint64_t> removeRoots =
				isEntitySelected(removeId)
					? getSelectedRoots()
					: std::vector<uint64_t>{ removeId };
			bool removedAny = false;
			for (uint64_t entityId : removeRoots) {
				if (document.FindEntity(entityId) && !entitySubtreeHasLocked(entityId)) {
					document.RemoveEntity(entityId);
					removedAny = true;
				}
			}
			if (removedAny) {
				selectedEntityIds_.clear();
				selectedEntityId_ = 0;
				hierarchySelectionAnchorId_ = 0;
			}
		}
		if (duplicateId != 0) {
			const std::vector<uint64_t> duplicateRoots =
				isEntitySelected(duplicateId)
					? getSelectedRoots()
					: std::vector<uint64_t>{ duplicateId };
			std::vector<uint64_t> duplicates;
			for (uint64_t entityId : duplicateRoots) {
				if (const SceneEntity* entity = document.FindEntity(entityId)) {
					if (!entity->locked) {
						const uint64_t duplicateId = document.DuplicateEntity(entityId);
						if (duplicateId != 0) {
							duplicates.push_back(duplicateId);
						}
					}
				}
			}
			if (!duplicates.empty()) {
				selectedEntityIds_.clear();
				selectedEntityIds_.insert(duplicates.begin(), duplicates.end());
				selectedEntityId_ = duplicates.back();
				hierarchySelectionAnchorId_ = selectedEntityId_;
				hierarchyRevealRequested_ = true;
			}
		}
		if (createRequested) {
			SceneEntity& entity = document.CreateEntity("Entity", createParentId);
			selectedEntityId_ = entity.id;
			selectedEntityIds_ = { entity.id };
			hierarchySelectionAnchorId_ = entity.id;
			hierarchyRevealRequested_ = true;
			selectedProjectFile_.clear();
		}
		if (createFolderRequested) {
			SceneEntity& folder = document.CreateEntity("Folder", createParentId);
			folder.folder = true;
			selectedEntityId_ = folder.id;
			selectedEntityIds_ = { folder.id };
			hierarchySelectionAnchorId_ = folder.id;
			hierarchyRevealRequested_ = true;
			selectedProjectFile_.clear();
		}
		if (createCameraPathRequested) {
			std::string pathName = "CameraPath";
			const std::string baseName = pathName;
			uint32_t suffix = 2;
			while (document.FindEntityByName(pathName)) {
				pathName = baseName + " " + std::to_string(suffix++);
			}
			SceneEntity& entity = document.CreateEntity(pathName, createParentId);
			const uint64_t pathEntityId = entity.id;
			document.AddComponent(pathEntityId, "CameraPath");
			selectedEntityId_ = pathEntityId;
			selectedEntityIds_ = { pathEntityId };
			hierarchySelectionAnchorId_ = pathEntityId;
			hierarchyRevealRequested_ = true;
			selectedProjectFile_.clear();
		}
		hierarchyObservedEntityId_ = selectedEntityId_;
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
	if (revealInspectorRequested_) {
		if (ImGuiWindow* inspectorWindow = ImGui::FindWindowByName("Inspector")) {
			if (ImGuiDockNode* dockNode = inspectorWindow->DockNode) {
				dockNode->SelectedTabId = inspectorWindow->TabId;
				if (dockNode->TabBar) {
					dockNode->TabBar->SelectedTabId = inspectorWindow->TabId;
				}
			}
		}
		revealInspectorRequested_ = false;
	}
	// コンテンツ量の境界でスクロールバーが出入りすると、幅依存のPreviewが再配置を繰り返す。
	ImGui::Begin(
		"Inspector",
		&showInspector_,
		ImGuiWindowFlags_AlwaysVerticalScrollbar
	);

	if (!selectedProjectFile_.empty()) {
		if (ImGui::Button("Back to Hierarchy Selection")) {
			selectedProjectFile_.clear();
			ImGui::End();
			return;
		}
		ImGui::Separator();

		const std::filesystem::path path = PathFromUtf8(selectedProjectFile_);
		std::string fileName = PathToUtf8(path.filename());
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
		if (TextureFormat::IsSupportedTexturePath(path)) {
			// Texture asset inspector
			const std::string texturePath = GetProjectResourcePath(
				selectedProjectFile_
			);
			bool isLoaded = TextureManager::GetInstance() &&
				TextureManager::GetInstance()->HasTexture(texturePath);
			if (isLoaded) {
				ImGui::Text("Status: Loaded in memory");
				
				const auto& metadata = TextureManager::GetInstance()->GetMetaData(
					texturePath
				);
				ImGui::Text("Width: %zu px", metadata.width);
				ImGui::Text("Height: %zu px", metadata.height);
				ImGui::Text("Mip Levels: %zu", metadata.mipLevels);
				
				// Render thumbnail
				D3D12_GPU_DESCRIPTOR_HANDLE handle =
					TextureManager::GetInstance()->GetSrvHandleGPU(texturePath);
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
						TextureManager::GetInstance()->LoadTexture(texturePath);
					}
				}
			}
			ImGui::Separator();
			ImGui::BeginDisabled(!editorSession_ || !editorSession_->IsEditing());
			if (ImGui::Button("Add Sprite to Scene")) {
				SceneDocument& document = editorSession_->GetEditDocument();
				std::string entityName = PathToUtf8(path.stem());
				if (entityName.empty()) {
					entityName = "Sprite";
				}
				const std::string baseName = entityName;
				uint32_t suffix = 2;
				while (document.FindEntityByName(entityName)) {
					entityName = baseName + " " + std::to_string(suffix++);
				}
				SceneEntity& entity = document.CreateEntity(entityName);
				entity.spriteTexturePath = GetProjectResourcePath(
					PathToUtf8(path)
				);
				entity.components = { "SpriteRenderer" };
				entity.components.front().texturePath = entity.spriteTexturePath;
				entity.transform.translate = {
					static_cast<float>(dxCommon_->GetClientWidth()) * 0.5f,
					static_cast<float>(dxCommon_->GetClientHeight()) * 0.5f,
					0.0f
				};
				if (TextureManager::GetInstance()) {
					TextureManager::GetInstance()->LoadTexture(entity.spriteTexturePath);
					const auto& metadata = TextureManager::GetInstance()->GetMetaData(
						entity.spriteTexturePath
					);
					entity.spriteSize = {
						static_cast<float>(metadata.width),
						static_cast<float>(metadata.height)
					};
					entity.components.front().spriteSize = entity.spriteSize;
				}
				selectedEntityId_ = entity.id;
				selectedProjectFile_.clear();
			}
			ImGui::EndDisabled();
		} 
		else if (ModelFormat::IsBasicModelPath(path)) {
			// Model asset inspector
			std::string relativePath = GetModelPathRelativeToResources(
				selectedProjectFile_
			);
			if (const ModelFormat::Descriptor* format =
				ModelFormat::FindByPath(path)) {
				ImGui::Text("Format: %s", format->displayName.data());
			}
			Model* loadedModel = ModelManager::GetInstance()
				? ModelManager::GetInstance()->FindModel(relativePath)
				: nullptr;
			bool isLoaded = loadedModel != nullptr;
			
			if (isLoaded) {
				ImGui::Text("Status: Loaded in ModelManager");
				ImGui::Text("Key: %s", relativePath.c_str());
				ImGui::Text("Vertices: %u", loadedModel->GetVertexCount());
			} else {
				ImGui::Text("Status: Not loaded");
				if (ImGui::Button("Load Model")) {
					if (ModelManager::GetInstance()) {
						ModelManager::GetInstance()->LoadModel(relativePath);
					}
				}
			}

			ImGui::SeparatorText("Preview");
			if (
				modelPreviewRenderedPath_ == relativePath &&
				modelPreviewTexture_.ptr != 0
			) {
				const float availableWidth = ImGui::GetContentRegionAvail().x;
				const float previewSize = std::clamp(
					availableWidth,
					160.0f,
					360.0f
				);
				const ImVec2 imageSize(previewSize, previewSize);
				ImGui::Image(
					ImTextureRef(static_cast<ImTextureID>(modelPreviewTexture_.ptr)),
					imageSize
				);
				if (ImGui::IsItemHovered()) {
					const ImGuiIO& io = ImGui::GetIO();
					if (ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
						modelPreviewYaw_ += io.MouseDelta.x * 0.01f;
						modelPreviewPitch_ = std::clamp(
							modelPreviewPitch_ + io.MouseDelta.y * 0.01f,
							-1.45f,
							1.45f
						);
					}
					if (io.MouseWheel != 0.0f) {
						modelPreviewZoom_ = std::clamp(
							modelPreviewZoom_ * (1.0f - io.MouseWheel * 0.12f),
							0.25f,
							4.0f
						);
					}
					ImGui::SetTooltip("Drag to orbit | Wheel to zoom");
				}
			} else {
				ImGui::TextDisabled("Preparing preview...");
			}
			if (ImGui::SmallButton("Reset View")) {
				modelPreviewYaw_ = 0.65f;
				modelPreviewPitch_ = 0.25f;
				modelPreviewZoom_ = 1.0f;
			}

			ImGui::Separator();
			ImGui::BeginDisabled(
				!editorSession_ || !editorSession_->IsEditing()
			);
			if (ImGui::Button("Add Model to Scene")) {
				SceneDocument& document = editorSession_->GetEditDocument();
				std::string entityName = PathToUtf8(path.stem());
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
				entity.components.front().modelPath = entity.modelPath;
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
	else if (editorSession_ && selectedEntityIds_.size() > 1) {
		SceneDocument& document = editorSession_->GetActiveDocument();
		std::vector<SceneEntity*> selectedEntities;
		for (uint64_t entityId : selectedEntityIds_) {
			if (SceneEntity* entity = document.FindEntity(entityId)) {
				selectedEntities.push_back(entity);
			}
		}
		if (selectedEntities.size() <= 1) {
			ImGui::TextDisabled("Selection changed");
			ImGui::End();
			return;
		}

		ImGui::Text("%zu Entities Selected", selectedEntities.size());
		ImGui::Separator();
		bool allActive = std::all_of(
			selectedEntities.begin(),
			selectedEntities.end(),
			[](const SceneEntity* entity) { return entity->active; }
		);
		bool allLocked = std::all_of(
			selectedEntities.begin(),
			selectedEntities.end(),
			[](const SceneEntity* entity) { return entity->locked; }
		);
		const bool activeMixed = std::any_of(
			selectedEntities.begin(),
			selectedEntities.end(),
			[allActive](const SceneEntity* entity) { return entity->active != allActive; }
		);
		const bool lockedMixed = std::any_of(
			selectedEntities.begin(),
			selectedEntities.end(),
			[allLocked](const SceneEntity* entity) { return entity->locked != allLocked; }
		);
		if (activeMixed) {
			ImGui::PushItemFlag(ImGuiItemFlags_MixedValue, true);
		}
		if (ImGui::Checkbox("Active", &allActive)) {
			for (SceneEntity* entity : selectedEntities) {
				entity->active = allActive;
			}
			document.MarkDirty();
		}
		if (activeMixed) {
			ImGui::PopItemFlag();
		}
		if (lockedMixed) {
			ImGui::PushItemFlag(ImGuiItemFlags_MixedValue, true);
		}
		if (ImGui::Checkbox("Locked", &allLocked)) {
			for (SceneEntity* entity : selectedEntities) {
				entity->locked = allLocked;
			}
			document.MarkDirty();
		}
		if (lockedMixed) {
			ImGui::PopItemFlag();
		}
		ImGui::TextDisabled("Transform and components are edited on the active Entity.");
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
		const bool entityLocked = entity->locked;

		char nameBuffer[128]{};
		strncpy_s(nameBuffer, entity->name.c_str(), _TRUNCATE);
		ImGui::BeginDisabled(entityLocked);
		if (ImGui::InputText("Name", nameBuffer, sizeof(nameBuffer))) {
			entity->name = nameBuffer;
			document.MarkDirty();
		}
		ImGui::EndDisabled();
		if (ImGui::Checkbox("Active", &entity->active)) {
			document.MarkDirty();
		}
		if (ImGui::Checkbox("Locked", &entity->locked)) {
			document.MarkDirty();
		}
		ImGui::BeginDisabled(entityLocked || !entity->components.empty());
		bool folder = entity->folder;
		if (ImGui::Checkbox("Folder", &folder)) {
			entity->folder = folder;
			if (entity->folder) {
				entity->modelPath.clear();
				entity->spriteTexturePath.clear();
			}
			document.MarkDirty();
			editorSession_->RequestSceneReload();
		}
		ImGui::EndDisabled();
		if (!entity->components.empty()) {
			ImGui::SameLine();
			ImGui::TextDisabled("Folder requires no components");
		}

		const SceneEntity* parent = document.FindEntity(entity->parentId);
		const char* parentName = parent ? parent->name.c_str() : "None (Root)";
		ImGui::BeginDisabled(entityLocked);
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
		ImGui::EndDisabled();

		auto resolveEffectiveTeamName = [&]() {
			if (!entity->teamName.empty()) {
				return entity->teamName;
			}
			return document.ResolveInheritedFolderTeamName(entity->id);
		};
		const std::string inheritedFolderTeamName =
			entity->teamName.empty()
				? document.ResolveInheritedFolderTeamName(entity->id)
				: std::string{};
		const bool teamInheritedFromFolder =
			!inheritedFolderTeamName.empty();

		ImGui::SeparatorText("Team");
		const std::string currentTeamLabel = entity->teamName.empty()
			? (
				teamInheritedFromFolder
					? "Inherit: " + inheritedFolderTeamName
					: std::string("None")
			)
			: entity->teamName;
		ImGui::BeginDisabled(entityLocked);
		if (ImGui::BeginCombo("Team", currentTeamLabel.c_str())) {
			if (ImGui::Selectable("None", entity->teamName.empty())) {
				entity->teamName.clear();
				entity->folderTeamEnabled = false;
				document.MarkDirty();
				editorSession_->RequestSceneReload();
			}
			for (const SceneTeamSettings& team : document.GetTeams()) {
				if (ImGui::Selectable(
					team.name.c_str(),
					entity->teamName == team.name
				)) {
					entity->teamName = team.name;
					document.MarkDirty();
					editorSession_->RequestSceneReload();
				}
			}
			ImGui::EndCombo();
		}
		static char newTeamNameBuffer[64] = "Team";
		ImGui::SetNextItemWidth(160.0f);
		ImGui::InputText(
			"New Team",
			newTeamNameBuffer,
			sizeof(newTeamNameBuffer)
		);
		ImGui::SameLine();
		if (ImGui::SmallButton("Create")) {
			SceneTeamSettings& team = document.CreateTeam(newTeamNameBuffer);
			entity->teamName = team.name;
			if (entity->folder) {
				entity->folderTeamEnabled = true;
			}
			document.MarkDirty();
			editorSession_->RequestSceneReload();
		}
		ImGui::EndDisabled();

		if (teamInheritedFromFolder) {
			ImGui::Text(
				"Inherited from folder: %s",
				inheritedFolderTeamName.c_str()
			);
		}

		SceneTeamSettings* selectedTeam =
			document.FindTeam(resolveEffectiveTeamName());
		if (selectedTeam) {
			int memberCount = 0;
			for (const SceneEntity& candidate : document.GetEntities()) {
				if (candidate.folder) {
					continue;
				}
				const SceneTeamSettings* candidateTeam =
					document.ResolveEntityTeam(candidate);
				if (candidateTeam && candidateTeam->name == selectedTeam->name) {
					++memberCount;
				}
			}
			ImGui::Text("Members: %d", memberCount);
			if (ImGui::TreeNodeEx(
				"Team Settings",
				ImGuiTreeNodeFlags_DefaultOpen
			)) {
				ImGui::BeginDisabled(entityLocked);
				std::string previousTeamName = selectedTeam->name;
				char teamNameBuffer[64]{};
				strncpy_s(
					teamNameBuffer,
					selectedTeam->name.c_str(),
					_TRUNCATE
				);
				if (ImGui::InputText(
					"Team Name",
					teamNameBuffer,
					sizeof(teamNameBuffer)
				)) {
					document.RenameTeam(previousTeamName, teamNameBuffer);
					selectedTeam = document.FindTeam(resolveEffectiveTeamName());
					editorSession_->RequestSceneReload();
				}
				if (selectedTeam) {
					bool teamChanged = false;
					ImGui::SeparatorText("Agent Common");
					teamChanged |= ImGui::Checkbox(
						"Team Agent Settings",
						&selectedTeam->agentBehaviorOverride
					);
					ImGui::BeginDisabled(
						!selectedTeam->agentBehaviorOverride
					);
					char teamGroupBuffer[64]{};
					strncpy_s(
						teamGroupBuffer,
						selectedTeam->agentGroupName.c_str(),
						_TRUNCATE
					);
					if (ImGui::InputText(
						"Team Agent Group",
						teamGroupBuffer,
						sizeof(teamGroupBuffer)
					)) {
						selectedTeam->agentGroupName = teamGroupBuffer;
						teamChanged = true;
					}
					teamChanged |= ImGui::DragFloat(
						"Team Min Speed",
						&selectedTeam->agentMinSpeed,
						0.05f,
						0.0f,
						100.0f
					);
					teamChanged |= ImGui::DragFloat(
						"Team Max Speed",
						&selectedTeam->agentMaxSpeed,
						0.05f,
						0.0f,
						100.0f
					);
					teamChanged |= ImGui::DragFloat(
						"Team Turn Speed",
						&selectedTeam->agentTurnSpeed,
						0.05f,
						0.0f,
						20.0f
					);
					teamChanged |= ImGui::DragFloat(
						"Team Wander Strength",
						&selectedTeam->agentWanderStrength,
						0.05f,
						0.0f,
						20.0f
					);
					teamChanged |= ImGui::DragFloat(
						"Team Wander Change Interval",
						&selectedTeam->agentWanderChangeInterval,
						0.05f,
						0.0f,
						60.0f
					);
					teamChanged |= ImGui::SliderFloat(
						"Team Wander Direction Range",
						&selectedTeam->agentWanderDirectionRange,
						0.0f,
						3.141592f
					);
					teamChanged |= ImGui::SliderFloat(
						"Team Wander Vertical Range",
						&selectedTeam->agentWanderVerticalRange,
						0.0f,
						1.0f
					);
					teamChanged |= ImGui::Checkbox(
						"Randomize Seed On Play",
						&selectedTeam->agentRandomizeSeedOnPlay
					);
					ImGui::BeginDisabled(selectedTeam->agentRandomizeSeedOnPlay);
					teamChanged |= ImGui::InputInt(
						"Random Seed",
						&selectedTeam->agentRandomSeed
					);
					ImGui::EndDisabled();
					teamChanged |= ImGui::Checkbox(
						"Use Leader Start Position",
						&selectedTeam->agentUseLeaderStartPosition
					);
					ImGui::BeginDisabled(!selectedTeam->agentUseLeaderStartPosition);
					teamChanged |= ImGui::DragFloat3(
						"Leader Start Position",
						&selectedTeam->agentLeaderStartPosition.x,
						0.05f
					);
					ImGui::EndDisabled();
					teamChanged |= ImGui::DragFloat(
						"Team Decision Interval",
						&selectedTeam->agentFlockDecisionInterval,
						0.01f,
						0.0f,
						5.0f
					);
					teamChanged |= ImGui::DragFloat(
						"Team Acceleration",
						&selectedTeam->agentFlockAcceleration,
						0.05f,
						0.0f,
						100.0f
					);
					teamChanged |= ImGui::DragFloat(
						"Team Max Turn Rate",
						&selectedTeam->agentFlockTurnRate,
						0.01f,
						0.0f,
						6.283185f
					);
					ImGui::SeparatorText("Member Follow");
					teamChanged |= ImGui::DragFloat(
						"Member Return Strength",
						&selectedTeam->agentMemberCenterFollow,
						0.05f,
						0.0f,
						20.0f
					);
					teamChanged |= ImGui::DragFloat(
						"Member Jitter Strength",
						&selectedTeam->agentMemberJitterStrength,
						0.01f,
						0.0f,
						10.0f
					);
					teamChanged |= ImGui::DragFloat(
						"Member Jitter Frequency",
						&selectedTeam->agentMemberJitterFrequency,
						0.01f,
						0.0f,
						10.0f
					);
					teamChanged |= ImGui::DragFloat(
						"Member Jitter Update Interval",
						&selectedTeam->agentMemberJitterUpdateInterval,
						0.01f,
						0.0f,
						10.0f
					);
					teamChanged |= ImGui::DragFloat(
						"Member Jitter Follow Speed",
						&selectedTeam->agentMemberJitterFollowSpeed,
						0.01f,
						0.0f,
						20.0f
					);
					teamChanged |= ImGui::DragFloat(
						"Member Max Distance",
						&selectedTeam->agentMemberLeashDistance,
						0.05f,
						0.0f,
						100.0f
					);
					teamChanged |= ImGui::DragFloat(
						"Member Leash Strength",
						&selectedTeam->agentMemberLeashStrength,
						0.05f,
						0.0f,
						20.0f
					);
					teamChanged |= ImGui::DragFloat(
						"Member Catchup Speed",
						&selectedTeam->agentMemberCatchupSpeed,
						0.05f,
						0.0f,
						100.0f
					);

					ImGui::SeparatorText("Team Heading");
					teamChanged |= ImGui::Checkbox(
						"Team Use Heading",
						&selectedTeam->agentUseTeamHeading
					);
					teamChanged |= ImGui::DragFloat3(
						"Team Heading Direction",
						&selectedTeam->agentTeamHeadingDirection.x,
						0.01f,
						-1.0f,
						1.0f
					);
					teamChanged |= ImGui::DragFloat(
						"Team Heading Weight",
						&selectedTeam->agentTeamHeadingWeight,
						0.05f,
						0.0f,
						20.0f
					);
					teamChanged |= ImGui::DragFloat(
						"Team Heading Follow Speed",
						&selectedTeam->agentTeamHeadingFollowSpeed,
						0.05f,
						0.0f,
						20.0f
					);

					ImGui::SeparatorText("Agent Rotation");
					teamChanged |= ImGui::Checkbox(
						"Team Align Forward To Velocity",
						&selectedTeam->agentAlignForwardToVelocity
					);
					const char* forwardAxes[] = {
						"+Z",
						"-Z",
						"+X",
						"-X",
						"+Y",
						"-Y"
					};
					const char* currentForwardAxis =
						selectedTeam->agentForwardAxis.c_str();
					if (ImGui::BeginCombo(
						"Team Forward Axis",
						currentForwardAxis
					)) {
						for (const char* axis : forwardAxes) {
							if (ImGui::Selectable(
								axis,
								selectedTeam->agentForwardAxis == axis
							)) {
								selectedTeam->agentForwardAxis = axis;
								teamChanged = true;
							}
						}
						ImGui::EndCombo();
					}
					teamChanged |= ImGui::Checkbox(
						"Team Rotate X",
						&selectedTeam->agentRotateAxisX
					);
					ImGui::SameLine();
					teamChanged |= ImGui::Checkbox(
						"Team Rotate Y",
						&selectedTeam->agentRotateAxisY
					);
					ImGui::SameLine();
					teamChanged |= ImGui::Checkbox(
						"Team Rotate Z",
						&selectedTeam->agentRotateAxisZ
					);
					teamChanged |= ImGui::DragFloat(
						"Team Rotation Follow Speed",
						&selectedTeam->agentRotationFollowSpeed,
						0.05f,
						0.0f,
						60.0f
					);
					teamChanged |= ImGui::DragFloat(
						"Team Pitch From Vertical Velocity",
						&selectedTeam->agentPitchFromVerticalVelocity,
						0.05f,
						0.0f,
						4.0f
					);
					teamChanged |= ImGui::DragFloat(
						"Team Banking Strength",
						&selectedTeam->agentBankingStrength,
						0.05f,
						0.0f,
						4.0f
					);

					teamChanged |= ImGui::Checkbox(
						"Team Schooling",
						&selectedTeam->agentSchooling
					);
					teamChanged |= ImGui::DragFloat(
						"Team Schooling Update Interval",
						&selectedTeam->agentSchoolingUpdateInterval,
						0.01f,
						0.0f,
						5.0f
					);
					teamChanged |= ImGui::DragFloat(
						"Team Schooling Update Jitter",
						&selectedTeam->agentSchoolingUpdateJitter,
						0.01f,
						0.0f,
						1.0f
					);
					teamChanged |= ImGui::InputInt(
						"Team Neighbor Limit",
						&selectedTeam->agentNeighborLimit
					);
					teamChanged |= ImGui::SliderFloat(
						"Team Schooling Blend",
						&selectedTeam->agentSchoolingBlend,
						0.0f,
						1.0f
					);
					teamChanged |= ImGui::DragFloat(
						"Team Separation Radius",
						&selectedTeam->agentSeparationRadius,
						0.05f,
						0.0f,
						100.0f
					);
					teamChanged |= ImGui::DragFloat(
						"Team Alignment Radius",
						&selectedTeam->agentAlignmentRadius,
						0.05f,
						0.0f,
						100.0f
					);
					teamChanged |= ImGui::DragFloat(
						"Team Cohesion Radius",
						&selectedTeam->agentCohesionRadius,
						0.05f,
						0.0f,
						100.0f
					);
					teamChanged |= ImGui::DragFloat(
						"Team Separation Weight",
						&selectedTeam->agentSeparationWeight,
						0.05f,
						0.0f,
						50.0f
					);
					teamChanged |= ImGui::DragFloat(
						"Team Alignment Weight",
						&selectedTeam->agentAlignmentWeight,
						0.05f,
						0.0f,
						50.0f
					);
					teamChanged |= ImGui::DragFloat(
						"Team Cohesion Weight",
						&selectedTeam->agentCohesionWeight,
						0.05f,
						0.0f,
						50.0f
					);
					teamChanged |= ImGui::ColorEdit4(
						"Team Agent Color",
						&selectedTeam->agentVisualColor.x,
						ImGuiColorEditFlags_Float
					);
					teamChanged |= ImGui::Checkbox(
						"Team Agent Lighting",
						&selectedTeam->agentEnableLighting
					);
					ImGui::EndDisabled();

					const float previousMinSpeed = selectedTeam->agentMinSpeed;
					const float previousMaxSpeed = selectedTeam->agentMaxSpeed;
					const float previousTurnSpeed = selectedTeam->agentTurnSpeed;
					const float previousWanderStrength =
						selectedTeam->agentWanderStrength;
					const Vector3 previousTeamHeadingDirection =
						selectedTeam->agentTeamHeadingDirection;
					const float previousTeamHeadingWeight =
						selectedTeam->agentTeamHeadingWeight;
					const float previousTeamHeadingFollowSpeed =
						selectedTeam->agentTeamHeadingFollowSpeed;
					const float previousTeamRotationWeight =
						selectedTeam->agentTeamRotationWeight;
					const float previousTeamRotationFollowSpeed =
						selectedTeam->agentTeamRotationFollowSpeed;
					const float previousRotationFollowSpeed =
						selectedTeam->agentRotationFollowSpeed;
					const float previousPitchFromVerticalVelocity =
						selectedTeam->agentPitchFromVerticalVelocity;
					const float previousBankingStrength =
						selectedTeam->agentBankingStrength;
					const float previousSchoolingUpdateInterval =
						selectedTeam->agentSchoolingUpdateInterval;
					const float previousSchoolingUpdateJitter =
						selectedTeam->agentSchoolingUpdateJitter;
					const int previousNeighborLimit =
						selectedTeam->agentNeighborLimit;
					const float previousSchoolingBlend =
						selectedTeam->agentSchoolingBlend;
					const float previousSeparationRadius =
						selectedTeam->agentSeparationRadius;
					const float previousAlignmentRadius =
						selectedTeam->agentAlignmentRadius;
					const float previousCohesionRadius =
						selectedTeam->agentCohesionRadius;
					const float previousSeparationWeight =
						selectedTeam->agentSeparationWeight;
					const float previousAlignmentWeight =
						selectedTeam->agentAlignmentWeight;
					const float previousCohesionWeight =
						selectedTeam->agentCohesionWeight;
					selectedTeam->agentMinSpeed =
						(std::max)(selectedTeam->agentMinSpeed, 0.0f);
					selectedTeam->agentMaxSpeed =
						(std::max)(
							selectedTeam->agentMaxSpeed,
							selectedTeam->agentMinSpeed
						);
					selectedTeam->agentTurnSpeed =
						(std::max)(selectedTeam->agentTurnSpeed, 0.0f);
					selectedTeam->agentWanderStrength =
						(std::max)(selectedTeam->agentWanderStrength, 0.0f);
					if (
						Math::Length(selectedTeam->agentTeamHeadingDirection) <=
						0.000001f
					) {
						selectedTeam->agentTeamHeadingDirection = {
							0.0f,
							0.0f,
							1.0f
						};
					} else {
						selectedTeam->agentTeamHeadingDirection =
							Math::Normalize(
								selectedTeam->agentTeamHeadingDirection
							);
					}
					selectedTeam->agentTeamHeadingWeight =
						(std::max)(selectedTeam->agentTeamHeadingWeight, 0.0f);
					selectedTeam->agentTeamHeadingFollowSpeed =
						(std::max)(
							selectedTeam->agentTeamHeadingFollowSpeed,
							0.0f
						);
					selectedTeam->agentTeamRotationWeight = std::clamp(
						selectedTeam->agentTeamRotationWeight,
						0.0f,
						1.0f
					);
					selectedTeam->agentTeamRotationFollowSpeed =
						(std::max)(
							selectedTeam->agentTeamRotationFollowSpeed,
							0.0f
						);
					selectedTeam->agentRotationFollowSpeed =
						(std::max)(
							selectedTeam->agentRotationFollowSpeed,
							0.0f
						);
					selectedTeam->agentPitchFromVerticalVelocity =
						(std::max)(
							selectedTeam->agentPitchFromVerticalVelocity,
							0.0f
						);
					selectedTeam->agentBankingStrength =
						(std::max)(selectedTeam->agentBankingStrength, 0.0f);
					selectedTeam->agentSchoolingUpdateInterval =
						(std::max)(
							selectedTeam->agentSchoolingUpdateInterval,
							0.0f
						);
					selectedTeam->agentSchoolingUpdateJitter =
						(std::max)(
							selectedTeam->agentSchoolingUpdateJitter,
							0.0f
						);
					selectedTeam->agentNeighborLimit =
						(std::max)(selectedTeam->agentNeighborLimit, 0);
					selectedTeam->agentSchoolingBlend = std::clamp(
						selectedTeam->agentSchoolingBlend,
						0.0f,
						1.0f
					);
					selectedTeam->agentSeparationRadius =
						(std::max)(selectedTeam->agentSeparationRadius, 0.0f);
					selectedTeam->agentAlignmentRadius =
						(std::max)(selectedTeam->agentAlignmentRadius, 0.0f);
					selectedTeam->agentCohesionRadius =
						(std::max)(selectedTeam->agentCohesionRadius, 0.0f);
					selectedTeam->agentSeparationWeight =
						(std::max)(selectedTeam->agentSeparationWeight, 0.0f);
					selectedTeam->agentAlignmentWeight =
						(std::max)(selectedTeam->agentAlignmentWeight, 0.0f);
					selectedTeam->agentCohesionWeight =
						(std::max)(selectedTeam->agentCohesionWeight, 0.0f);
					teamChanged |=
						previousMinSpeed != selectedTeam->agentMinSpeed ||
						previousMaxSpeed != selectedTeam->agentMaxSpeed ||
						previousTurnSpeed != selectedTeam->agentTurnSpeed ||
						previousWanderStrength !=
							selectedTeam->agentWanderStrength ||
						previousTeamHeadingDirection.x !=
							selectedTeam->agentTeamHeadingDirection.x ||
						previousTeamHeadingDirection.y !=
							selectedTeam->agentTeamHeadingDirection.y ||
						previousTeamHeadingDirection.z !=
							selectedTeam->agentTeamHeadingDirection.z ||
						previousTeamHeadingWeight !=
							selectedTeam->agentTeamHeadingWeight ||
						previousTeamHeadingFollowSpeed !=
							selectedTeam->agentTeamHeadingFollowSpeed ||
						previousTeamRotationWeight !=
							selectedTeam->agentTeamRotationWeight ||
						previousTeamRotationFollowSpeed !=
							selectedTeam->agentTeamRotationFollowSpeed ||
						previousRotationFollowSpeed !=
							selectedTeam->agentRotationFollowSpeed ||
						previousPitchFromVerticalVelocity !=
							selectedTeam->agentPitchFromVerticalVelocity ||
						previousBankingStrength !=
							selectedTeam->agentBankingStrength ||
						previousSchoolingUpdateInterval !=
							selectedTeam->agentSchoolingUpdateInterval ||
						previousSchoolingUpdateJitter !=
							selectedTeam->agentSchoolingUpdateJitter ||
						previousNeighborLimit !=
							selectedTeam->agentNeighborLimit ||
						previousSchoolingBlend !=
							selectedTeam->agentSchoolingBlend ||
						previousSeparationRadius !=
							selectedTeam->agentSeparationRadius ||
						previousAlignmentRadius !=
							selectedTeam->agentAlignmentRadius ||
						previousCohesionRadius !=
							selectedTeam->agentCohesionRadius ||
						previousSeparationWeight !=
							selectedTeam->agentSeparationWeight ||
						previousAlignmentWeight !=
							selectedTeam->agentAlignmentWeight ||
						previousCohesionWeight !=
							selectedTeam->agentCohesionWeight;
					if (teamChanged) {
						document.MarkDirty();
					}
					if (ImGui::SmallButton("Remove Team")) {
						const std::string removeTeamName = selectedTeam->name;
						document.RemoveTeam(removeTeamName);
						selectedTeam = nullptr;
						editorSession_->RequestSceneReload();
					}
				}
				ImGui::EndDisabled();
				ImGui::TreePop();
			}
		}

		if (entity->folder) {
			ImGui::SeparatorText("Folder");
			ImGui::TextDisabled("Folders organize children in the hierarchy.");
			ImGui::BeginDisabled(entityLocked || entity->teamName.empty());
			bool folderTeamEnabled = entity->folderTeamEnabled;
			if (ImGui::Checkbox("Use Folder As Team", &folderTeamEnabled)) {
				entity->folderTeamEnabled = folderTeamEnabled;
				document.MarkDirty();
				editorSession_->RequestSceneReload();
			}
			ImGui::EndDisabled();
			if (entity->teamName.empty()) {
				ImGui::TextDisabled("Assign a Team above to enable folder team.");
			}
			if (editorSession_->IsPlaying() || editorSession_->IsPaused()) {
				ImGui::TextDisabled("Play mode changes are temporary");
			}
			ImGui::End();
			return;
		}

		ImGui::SeparatorText("Transform");
		bool transformChanged = false;
		ImGui::BeginDisabled(entityLocked);
		if (HasComponent(*entity, "SpriteRenderer")) {
			transformChanged |= ImGui::DragFloat2(
				"Position",
				&entity->transform.translate.x,
				0.5f
			);
			Vector3 spriteEuler =
				MakeEulerFromQuaternion(entity->transform.rotate);
			if (ImGui::DragFloat(
				"Rotation",
				&spriteEuler.z,
				0.01f
			)) {
				entity->transform.rotate = MakeQuaternionFromEuler({
					0.0f, 0.0f, spriteEuler.z
				});
				transformChanged = true;
			}
			transformChanged |= ImGui::DragFloat2(
				"Scale",
				&entity->transform.scale.x,
				0.01f,
				0.001f,
				1000.0f
			);
		} else {
			transformChanged |= ImGui::DragFloat3(
				"Position",
				&entity->transform.translate.x,
				0.05f
			);
			if (
				inspectorRotationEntityId_ != entity->id ||
				!IsSameRotation(
					inspectorRotationSource_,
					entity->transform.rotate
				)
			) {
				inspectorRotationEntityId_ = entity->id;
				inspectorRotationEuler_ =
					MakeEulerFromQuaternion(entity->transform.rotate);
				inspectorRotationSource_ = entity->transform.rotate;
			}
			if (ImGui::DragFloat3(
				"Rotation",
				&inspectorRotationEuler_.x,
				0.01f
			)) {
				entity->transform.rotate =
					MakeQuaternionFromEuler(inspectorRotationEuler_);
				inspectorRotationSource_ = entity->transform.rotate;
				transformChanged = true;
			}
			transformChanged |= ImGui::DragFloat3(
				"Scale",
				&entity->transform.scale.x,
				0.01f,
				0.001f,
				1000.0f
			);
		}
		ImGui::EndDisabled();
		if (transformChanged) {
			document.MarkDirty();
		}

		std::string removeComponentType;
		for (SceneComponent& component : entity->components) {
			ImGui::PushID(component.type.c_str());
			ImGui::SeparatorText(
				component.type == "OBBCollider" ? "Collider" : component.type.c_str()
			);
			ImGui::BeginDisabled(entityLocked || !editorSession_->IsEditing());
			if (ImGui::Checkbox("Enabled", &component.enabled)) {
				document.MarkDirty();
				editorSession_->RequestSceneReload();
			}
			ImGui::SameLine();
			if (ImGui::SmallButton("Remove")) {
				removeComponentType = component.type;
			}
			ImGui::EndDisabled();

			if (component.type == "MeshRenderer") {
				const bool modelEditingDisabled =
					!editorSession_->IsEditing() || entityLocked;
				auto assignModel = [&](const std::string& modelPath) {
					if (component.modelPath == modelPath) {
						return;
					}
					component.modelPath = modelPath;
					entity->modelPath = component.modelPath;
					document.MarkDirty();
					editorSession_->RequestSceneReload();
				};

				if (!component.modelPath.empty()) {
					const bool previewReady =
						modelPreviewRenderedPath_ == component.modelPath &&
						modelPreviewTexture_.ptr != 0;
					const float previewSize = std::clamp(
						ImGui::GetContentRegionAvail().x,
						180.0f,
						320.0f
					);
					if (previewReady) {
						ImGui::Image(
							ImTextureRef(static_cast<ImTextureID>(modelPreviewTexture_.ptr)),
							ImVec2(previewSize, previewSize)
						);
						if (ImGui::IsItemHovered()) {
							const ImGuiIO& io = ImGui::GetIO();
							if (ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
								modelPreviewYaw_ += io.MouseDelta.x * 0.01f;
								modelPreviewPitch_ = std::clamp(
									modelPreviewPitch_ + io.MouseDelta.y * 0.01f,
									-1.45f,
									1.45f
								);
							}
							if (io.MouseWheel != 0.0f) {
								modelPreviewZoom_ = std::clamp(
									modelPreviewZoom_ * (1.0f - io.MouseWheel * 0.12f),
									0.25f,
									4.0f
								);
							}
							ImGui::SetTooltip(
								"Drag to orbit | Wheel to zoom | Drop model to replace"
							);
						}
					} else {
						ImGui::Button(
							"Preparing model preview...",
							ImVec2(previewSize, previewSize)
						);
					}
					if (!modelEditingDisabled && ImGui::BeginDragDropTarget()) {
						if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(
							"PROJECT_MODEL_PATH"
						)) {
							const char* droppedPath =
								static_cast<const char*>(payload->Data);
							if (droppedPath && droppedPath[0] != '\0') {
								assignModel(GetModelPathRelativeToResources(droppedPath));
							}
						}
						ImGui::EndDragDropTarget();
					}
					if (ImGui::SmallButton("Reset Preview")) {
						modelPreviewYaw_ = 0.65f;
						modelPreviewPitch_ = 0.25f;
						modelPreviewZoom_ = 1.0f;
					}
					ImGui::SameLine();
					ImGui::BeginDisabled(modelEditingDisabled);
					if (ImGui::SmallButton("Clear Model")) {
						assignModel({});
					}
					ImGui::EndDisabled();
				} else {
					ImGui::BeginDisabled(modelEditingDisabled);
					ImGui::Button("Drop Model Here", ImVec2(-1.0f, 48.0f));
					ImGui::EndDisabled();
					if (!modelEditingDisabled && ImGui::BeginDragDropTarget()) {
						if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(
							"PROJECT_MODEL_PATH"
						)) {
							const char* droppedPath =
								static_cast<const char*>(payload->Data);
							if (droppedPath && droppedPath[0] != '\0') {
								assignModel(GetModelPathRelativeToResources(droppedPath));
							}
						}
						ImGui::EndDragDropTarget();
					}
					ImGui::TextDisabled("No model assigned. Select or drop a model.");
				}

				const char* currentModel = component.modelPath.empty()
					? "None"
					: component.modelPath.c_str();
				ImGui::BeginDisabled(modelEditingDisabled);
				if (ImGui::BeginCombo("Model", currentModel)) {
					if (ImGui::Selectable("None", component.modelPath.empty())) {
						assignModel({});
					}
					for (const std::string& modelPath : GetCachedModelAssetPaths()) {
						if (ImGui::Selectable(
							modelPath.c_str(),
							component.modelPath == modelPath
						)) {
							assignModel(modelPath);
						}
					}
					ImGui::EndCombo();
				}
				if (!modelEditingDisabled && ImGui::BeginDragDropTarget()) {
					if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(
						"PROJECT_MODEL_PATH"
					)) {
						const char* droppedPath = static_cast<const char*>(payload->Data);
						if (droppedPath && droppedPath[0] != '\0') {
							assignModel(GetModelPathRelativeToResources(droppedPath));
						}
					}
					ImGui::EndDragDropTarget();
				}
				if (!component.modelPath.empty()) {
					ModelManager::GetInstance()->LoadModel(component.modelPath);
					Model* model = ModelManager::GetInstance()->FindModel(
						component.modelPath
					);
					if (model) {
						const std::vector<Model::MaterialSlot>& materialSlots =
							model->GetMaterialSlots();
						ImGui::SeparatorText("Materials");
						ImGui::TextDisabled(
							"%zu meshes / %zu materials",
							model->GetSubMeshes().size(),
							materialSlots.size()
						);
						for (size_t materialIndex = 0;
							materialIndex < materialSlots.size();
							++materialIndex) {
							const Model::MaterialSlot& materialSlot =
								materialSlots[materialIndex];
							const std::string materialLabel = materialSlot.name.empty()
								? "Material " + std::to_string(materialIndex + 1)
								: materialSlot.name;
							ImGui::PushID(static_cast<int>(materialIndex));
							if (ImGui::TreeNode(materialLabel.c_str())) {
								auto overrideIt = std::find_if(
									component.meshMaterialOverrides.begin(),
									component.meshMaterialOverrides.end(),
									[&](const SceneMeshMaterialOverride& override) {
										return override.materialName == materialSlot.name;
									}
								);
								bool overrideEnabled = overrideIt !=
									component.meshMaterialOverrides.end() && overrideIt->enabled;
								if (ImGui::Checkbox("Override", &overrideEnabled)) {
									if (overrideIt == component.meshMaterialOverrides.end()) {
										component.meshMaterialOverrides.push_back({
											materialSlot.name,
											true
										});
										overrideIt = std::prev(
											component.meshMaterialOverrides.end()
										);
									} else {
										overrideIt->enabled = overrideEnabled;
									}
									document.MarkDirty();
								}

								SceneMeshMaterialOverride* override = overrideIt ==
									component.meshMaterialOverrides.end()
									? nullptr
									: &*overrideIt;
								ImGui::BeginDisabled(!overrideEnabled);
								bool materialChanged = false;
								if (override) {
									materialChanged |= ImGui::Checkbox(
										"Override Color",
										&override->colorOverrideEnabled
									);
									ImGui::BeginDisabled(!override->colorOverrideEnabled);
									materialChanged |= ImGui::ColorEdit4(
										"Color", &override->color.x
									);
									ImGui::EndDisabled();
									const char* texturePath = override->texturePath.empty()
										? "Using model texture"
										: override->texturePath.c_str();
									ImGui::TextWrapped("Texture: %s", texturePath);
									if (ImGui::SmallButton("Clear Texture")) {
										override->texturePath.clear();
										materialChanged = true;
									}
									ImGui::Button("Drop Texture Here", ImVec2(-1.0f, 28.0f));
									if (ImGui::BeginDragDropTarget()) {
										if (const ImGuiPayload* payload =
											ImGui::AcceptDragDropPayload("PROJECT_TEXTURE_PATH")) {
											const char* droppedPath =
												static_cast<const char*>(payload->Data);
											if (droppedPath && droppedPath[0] != '\0') {
											override->texturePath =
													GetProjectResourcePath(droppedPath);
												materialChanged = true;
											}
										}
										ImGui::EndDragDropTarget();
									}
								}
								ImGui::EndDisabled();
								if (materialChanged) {
									document.MarkDirty();
								}
								ImGui::TreePop();
							}
							ImGui::PopID();
						}
					}
				}
				const char* currentCullMode = component.meshCullMode.empty()
					? "Back"
					: component.meshCullMode.c_str();
				if (ImGui::BeginCombo("Cull Mode", currentCullMode)) {
					const char* cullModes[] = { "Back", "Front", "None" };
					for (const char* cullMode : cullModes) {
						if (ImGui::Selectable(
							cullMode,
							component.meshCullMode == cullMode ||
								(component.meshCullMode.empty() &&
									std::strcmp(cullMode, "Back") == 0)
						)) {
							component.meshCullMode = cullMode;
							document.MarkDirty();
						}
					}
					ImGui::EndCombo();
				}
				bool reflectionChanged = false;
				if (!component.meshEnvironmentReflectionOverride) {
					ImGui::TextDisabled("Using Environment Reflection");
				}
				reflectionChanged |= ImGui::Checkbox(
					"Override Environment Reflection",
					&component.meshEnvironmentReflectionOverride
				);
				ImGui::BeginDisabled(
					!component.meshEnvironmentReflectionOverride
				);
				reflectionChanged |= ImGui::DragFloat(
					"Reflection Intensity",
					&component.meshEnvironmentReflectionIntensity,
					0.01f,
					0.0f,
					1.0f
				);
				ImGui::EndDisabled();
				if (component.meshEnvironmentReflectionIntensity < 0.0f) {
					component.meshEnvironmentReflectionIntensity = 0.0f;
					reflectionChanged = true;
				}
				if (component.meshEnvironmentReflectionIntensity > 1.0f) {
					component.meshEnvironmentReflectionIntensity = 1.0f;
					reflectionChanged = true;
				}
				if (reflectionChanged) {
					document.MarkDirty();
				}
				ImGui::EndDisabled();
			} else if (component.type == "Environment") {
				ImGui::BeginDisabled(!editorSession_->IsEditing() || entityLocked);
				bool environmentChanged = false;
				environmentChanged |= ImGui::Checkbox(
					"Skybox Enabled",
					&component.environmentSkyboxEnabled
				);

				auto assignSkybox = [&](const std::string& texturePath) {
					if (component.environmentSkyboxPath == texturePath) {
						return;
					}
					component.environmentSkyboxPath = texturePath;
					TextureManager::GetInstance()->LoadTexture(texturePath);
					environmentChanged = true;
					editorSession_->RequestSceneReload();
				};

				const char* currentSkybox =
					component.environmentSkyboxPath.empty()
					? "None"
					: component.environmentSkyboxPath.c_str();
				if (ImGui::BeginCombo("Skybox DDS", currentSkybox)) {
					if (ImGui::Selectable(
						"None",
						component.environmentSkyboxPath.empty()
					)) {
						assignSkybox({});
					}
					for (const std::string& texturePath : GetCachedTextureAssetPaths()) {
						const std::filesystem::path path =
							PathFromUtf8(texturePath);
						std::string extension = path.extension().string();
						std::transform(
							extension.begin(),
							extension.end(),
							extension.begin(),
							::tolower
						);
						if (extension != ".dds") {
							continue;
						}
						if (ImGui::Selectable(
							texturePath.c_str(),
							component.environmentSkyboxPath == texturePath
						)) {
							assignSkybox(texturePath);
						}
					}
					ImGui::EndCombo();
				}
				ImGui::Button("Drop DDS Skybox Here", ImVec2(-1.0f, 38.0f));
				if (ImGui::BeginDragDropTarget()) {
					if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(
						"PROJECT_TEXTURE_PATH"
					)) {
						const char* droppedPath =
							static_cast<const char*>(payload->Data);
						if (droppedPath && droppedPath[0] != '\0') {
							const std::filesystem::path path =
								PathFromUtf8(droppedPath);
							std::string extension = path.extension().string();
							std::transform(
								extension.begin(),
								extension.end(),
								extension.begin(),
								::tolower
							);
							if (extension == ".dds") {
								assignSkybox(GetProjectResourcePath(droppedPath));
							}
						}
					}
					ImGui::EndDragDropTarget();
				}
				environmentChanged |= ImGui::DragFloat(
					"Skybox Intensity",
					&component.environmentSkyboxIntensity,
					0.01f,
					0.0f,
					10.0f
				);
				environmentChanged |= ImGui::DragFloat(
					"Reflection Intensity",
					&component.environmentReflectionIntensity,
					0.01f,
					0.0f,
					1.0f
				);
				if (component.environmentSkyboxIntensity < 0.0f) {
					component.environmentSkyboxIntensity = 0.0f;
					environmentChanged = true;
				}
				if (component.environmentReflectionIntensity < 0.0f) {
					component.environmentReflectionIntensity = 0.0f;
					environmentChanged = true;
				}
				if (component.environmentReflectionIntensity > 1.0f) {
					component.environmentReflectionIntensity = 1.0f;
					environmentChanged = true;
				}
				if (environmentChanged) {
					document.MarkDirty();
				}
				ImGui::EndDisabled();
			} else if (component.type == "SpriteRenderer") {
				const char* currentTexture = component.texturePath.empty()
					? "None"
					: component.texturePath.c_str();
				ImGui::BeginDisabled(!editorSession_->IsEditing() || entityLocked);
				if (ImGui::BeginCombo("Texture", currentTexture)) {
					for (const std::string& texturePath : GetCachedTextureAssetPaths()) {
						if (ImGui::Selectable(
							texturePath.c_str(),
							component.texturePath == texturePath
						)) {
							component.texturePath = texturePath;
							entity->spriteTexturePath = component.texturePath;
							TextureManager::GetInstance()->LoadTexture(texturePath);
							document.MarkDirty();
						}
					}
					ImGui::EndCombo();
				}
				if (ImGui::BeginDragDropTarget()) {
					if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(
						"PROJECT_TEXTURE_PATH"
					)) {
						const char* droppedPath = static_cast<const char*>(payload->Data);
						if (droppedPath && droppedPath[0] != '\0') {
							component.texturePath = GetProjectResourcePath(droppedPath);
							entity->spriteTexturePath = component.texturePath;
							TextureManager::GetInstance()->LoadTexture(
								component.texturePath
							);
							document.MarkDirty();
						}
					}
					ImGui::EndDragDropTarget();
				}
				bool spriteChanged = false;
				spriteChanged |= ImGui::DragFloat2(
					"Size",
					&component.spriteSize.x,
					1.0f,
					1.0f,
					8192.0f
				);
				spriteChanged |= ImGui::DragFloat2(
					"Anchor",
					&component.spriteAnchor.x,
					0.01f,
					0.0f,
					1.0f
				);
				spriteChanged |= ImGui::ColorEdit4(
					"Color",
					&component.spriteColor.x
				);
				spriteChanged |= ImGui::Checkbox("Flip X", &component.spriteFlipX);
				ImGui::SameLine();
				spriteChanged |= ImGui::Checkbox("Flip Y", &component.spriteFlipY);
				if (spriteChanged) {
					entity->spriteSize = component.spriteSize;
					entity->spriteAnchor = component.spriteAnchor;
					entity->spriteColor = component.spriteColor;
					entity->spriteFlipX = component.spriteFlipX;
					entity->spriteFlipY = component.spriteFlipY;
					document.MarkDirty();
				}
				ImGui::EndDisabled();
			} else if (component.type == "Light") {
				ImGui::BeginDisabled(!editorSession_->IsEditing() || entityLocked);
				bool lightChanged = false;
				const char* lightTypes[] = {
					"Directional",
					"Point",
					"Spot"
				};
				int lightTypeIndex = component.lightType == "Directional"
					? 0
					: component.lightType == "Spot" ? 2 : 1;
				if (ImGui::Combo(
					"Light Type",
					&lightTypeIndex,
					lightTypes,
					IM_ARRAYSIZE(lightTypes)
				)) {
					component.lightType = lightTypes[lightTypeIndex];
					if (component.lightType == "Point") {
						component.lightCastsShadow = false;
					}
					lightChanged = true;
				}
				lightChanged |= ImGui::ColorEdit4(
					"Color",
					&component.lightColor.x,
					ImGuiColorEditFlags_Float
				);
				lightChanged |= ImGui::DragFloat(
					"Intensity",
					&component.lightIntensity,
					0.05f,
					0.0f,
					30.0f
				);

				if (component.lightType == "Point" || component.lightType == "Spot") {
					lightChanged |= ImGui::DragFloat(
						"Range",
						&component.lightRange,
						0.1f,
						0.1f,
						1000.0f
					);
					lightChanged |= ImGui::DragFloat(
						"Decay",
						&component.lightDecay,
						0.05f,
						0.0f,
						10.0f
					);
				}
				if (component.lightType == "Spot") {
					lightChanged |= ImGui::DragFloat(
						"Inner Angle",
						&component.lightSpotInnerAngle,
						0.25f,
						0.0f,
						component.lightSpotOuterAngle,
						"%.1f deg"
					);
					lightChanged |= ImGui::DragFloat(
						"Outer Angle",
						&component.lightSpotOuterAngle,
						0.25f,
						1.0f,
						89.0f,
						"%.1f deg"
					);
					component.lightSpotOuterAngle = std::clamp(
						component.lightSpotOuterAngle,
						1.0f,
						89.0f
					);
					component.lightSpotInnerAngle = std::clamp(
						component.lightSpotInnerAngle,
						0.0f,
						component.lightSpotOuterAngle
					);
				}

				if (component.lightType == "Directional") {
					const Matrix4x4 localRotation =
						MakeRotateMatrix(entity->transform.rotate);
					Vector3 localDirection{
						localRotation.m[2][0],
						localRotation.m[2][1],
						localRotation.m[2][2]
					};
					if (ImGui::DragFloat3(
						"Direction",
						&localDirection.x,
						0.01f,
						-1.0f,
						1.0f,
						"%.3f"
					)) {
						if (Math::Length(localDirection) <= 0.000001f) {
							localDirection = { 0.0f, 0.0f, 1.0f };
						} else {
							localDirection = Math::Normalize(localDirection);
						}
						Vector3 localUp{
							localRotation.m[1][0],
							localRotation.m[1][1],
							localRotation.m[1][2]
						};
						entity->transform.rotate =
							MakeLookRotationQuaternion(localDirection, localUp);
						lightChanged = true;
					}
					ImGui::TextDisabled(
						"Local direction; parent rotation is applied. Position is the shadow focus."
					);
				} else if (component.lightType == "Spot") {
					ImGui::TextDisabled("Transform +Z is the light direction.");
				}

				if (component.lightType != "Point") {
					ImGui::SeparatorText("Shadow");
					lightChanged |= ImGui::Checkbox(
						"Cast Shadow",
						&component.lightCastsShadow
					);
					if (component.lightCastsShadow) {
						lightChanged |= ImGui::DragFloat(
							"Shadow Bias",
							&component.lightShadowBias,
							0.0001f,
							0.0f,
							0.05f,
							"%.5f"
						);
						lightChanged |= ImGui::DragFloat(
							"Normal Bias",
							&component.lightShadowNormalBias,
							0.001f,
							0.0f,
							0.2f,
							"%.4f"
						);
						lightChanged |= ImGui::DragFloat(
							"Shadow Strength",
							&component.lightShadowStrength,
							0.01f,
							0.0f,
							1.0f
						);
						if (component.lightType == "Directional") {
							lightChanged |= ImGui::DragFloat(
								"Shadow Distance",
								&component.lightShadowDistance,
								0.5f,
								1.0f,
								1000.0f
							);
							lightChanged |= ImGui::DragFloat(
								"Orthographic Size",
								&component.lightShadowOrthographicSize,
								0.5f,
								1.0f,
								1000.0f
							);
							lightChanged |= ImGui::DragFloat(
								"Shadow Near Clip",
								&component.lightShadowNearClip,
								0.01f,
								0.001f,
								1000.0f
							);
							lightChanged |= ImGui::DragFloat(
								"Shadow Far Clip",
								&component.lightShadowFarClip,
								0.5f,
								1.0f,
								5000.0f
							);
							lightChanged |= ImGui::Checkbox(
								"Texel Snap",
								&component.lightShadowTexelSnap
							);
						}
					}
				}

				ImGui::SeparatorText("Scene Lighting");
				const char* shadowMapLabels[] = { "1024", "2048", "4096" };
				SceneLightingSettings lightingSettings =
					document.GetLightingSettings();
				int shadowMapIndex = lightingSettings.shadowMapSize <= 1024
					? 0
					: lightingSettings.shadowMapSize <= 2048 ? 1 : 2;
				if (ImGui::Combo(
					"Shadow Map Size",
					&shadowMapIndex,
					shadowMapLabels,
					IM_ARRAYSIZE(shadowMapLabels)
				)) {
					const uint32_t sizes[] = { 1024, 2048, 4096 };
					lightingSettings.shadowMapSize = sizes[shadowMapIndex];
					document.SetLightingSettings(lightingSettings);
				}
				if (lightChanged) {
					component.lightColor.x = std::clamp(
						component.lightColor.x,
						0.0f,
						1.0f
					);
					component.lightColor.y = std::clamp(
						component.lightColor.y,
						0.0f,
						1.0f
					);
					component.lightColor.z = std::clamp(
						component.lightColor.z,
						0.0f,
						1.0f
					);
					component.lightColor.w = std::clamp(
						component.lightColor.w,
						0.0f,
						1.0f
					);
					component.lightIntensity = (std::max)(
						component.lightIntensity,
						0.0f
					);
					component.lightRange = (std::max)(component.lightRange, 0.1f);
					component.lightDecay = (std::max)(component.lightDecay, 0.0f);
					component.lightShadowNearClip = (std::max)(
						component.lightShadowNearClip,
						0.001f
					);
					component.lightShadowFarClip = (std::max)(
						component.lightShadowFarClip,
						component.lightShadowNearClip + 0.001f
					);
					document.MarkDirty();
				}
				ImGui::EndDisabled();
			} else if (component.type == "Camera") {
				ImGui::BeginDisabled(!editorSession_->IsEditing() || entityLocked);
				bool cameraChanged = false;
				bool isMainCamera = component.cameraIsMain;
				if (ImGui::Checkbox("Main Camera", &isMainCamera)) {
					if (isMainCamera) {
						for (SceneEntity& candidate : document.GetEntities()) {
							if (SceneComponent* cameraComponent =
								FindComponent(candidate, "Camera")) {
								cameraComponent->cameraIsMain = false;
							}
						}
					}
					component.cameraIsMain = isMainCamera;
					cameraChanged = true;
				}

				constexpr float radiansToDegrees = 57.2957795f;
				constexpr float degreesToRadians = 0.0174532925f;
				float fovDegrees = component.cameraFovY * radiansToDegrees;
				if (ImGui::DragFloat("FOV Y", &fovDegrees, 0.5f, 1.0f, 179.0f)) {
					component.cameraFovY = fovDegrees * degreesToRadians;
					cameraChanged = true;
				}
				cameraChanged |= ImGui::DragFloat(
					"Near Clip",
					&component.cameraNearClip,
					0.01f,
					0.001f,
					100.0f
				);
				cameraChanged |= ImGui::DragFloat(
					"Far Clip",
					&component.cameraFarClip,
					1.0f,
					1.0f,
					10000.0f
				);
				if (component.cameraFarClip <= component.cameraNearClip) {
					component.cameraFarClip = component.cameraNearClip + 0.001f;
					cameraChanged = true;
				}
				if (cameraChanged) {
					document.MarkDirty();
				}
				ImGui::EndDisabled();
			} else if (component.type == "MonitorRenderer") {
				ImGui::BeginDisabled(!editorSession_->IsEditing() || entityLocked);
				bool monitorChanged = false;
				struct MonitorResolutionPreset {
					const char* label;
					uint32_t width;
					uint32_t height;
				};
				constexpr MonitorResolutionPreset monitorPresets[] = {
					{ "Square 512", 512, 512 },
					{ "HD 16:9", 1280, 720 },
					{ "Full HD 16:9", 1920, 1080 },
					{ "Portrait 9:16", 720, 1280 },
					{ "Wide 21:9", 1792, 768 },
					{ "Low 16:9", 640, 360 },
					{ "Custom", 0, 0 },
				};
				const SceneEntity* currentCameraEntity = nullptr;
				if (component.monitorCameraEntityId != 0) {
					const SceneEntity* idCameraEntity =
						document.FindEntity(component.monitorCameraEntityId);
					if (
						component.monitorCameraName.empty() ||
						(
							idCameraEntity &&
							idCameraEntity->name == component.monitorCameraName
						)
					) {
						currentCameraEntity = idCameraEntity;
					}
				}
				if (
					!currentCameraEntity &&
					!component.monitorCameraName.empty()
				) {
					currentCameraEntity =
						document.FindEntityByName(component.monitorCameraName);
				}
				char cameraNameBuffer[128]{};
				strncpy_s(
					cameraNameBuffer,
					component.monitorCameraName.c_str(),
					_TRUNCATE
				);
				if (ImGui::InputText(
					"Target Camera Name",
					cameraNameBuffer,
					sizeof(cameraNameBuffer)
				)) {
					component.monitorCameraName = cameraNameBuffer;
					component.monitorCameraEntityId = 0;
					monitorChanged = true;
				}

				const std::string currentCameraLabel = currentCameraEntity
					? currentCameraEntity->name
					: (
						component.monitorCameraName.empty()
						? std::string("Select Camera...")
						: component.monitorCameraName
					);
				if (ImGui::BeginCombo("Camera Entity", currentCameraLabel.c_str())) {
					for (const SceneEntity& candidate : document.GetEntities()) {
						const SceneComponent* cameraComponent =
							FindEnabledComponent(candidate, "Camera");
						if (!cameraComponent) {
							continue;
						}
						std::string cameraLabel = candidate.name;
						if (cameraComponent->cameraIsMain) {
							cameraLabel += " (Main)";
						}
						if (HasComponent(candidate, "PlayerBehavior")) {
							cameraLabel += " (Gameplay)";
						}
						const bool selected =
							component.monitorCameraEntityId == candidate.id ||
							(
								component.monitorCameraEntityId == 0 &&
								component.monitorCameraName == candidate.name
							);
						if (ImGui::Selectable(
							cameraLabel.c_str(),
							selected
						)) {
							component.monitorCameraEntityId = candidate.id;
							component.monitorCameraName = candidate.name;
							monitorChanged = true;
						}
					}
					ImGui::EndCombo();
				}
				if (currentCameraEntity) {
					ImGui::TextDisabled(
						"Bound Camera ID: %llu",
						static_cast<unsigned long long>(currentCameraEntity->id)
					);
				}
				if (
					component.monitorCameraEntityId != 0 ||
					!component.monitorCameraName.empty()
				) {
					const SceneEntity* selectedCamera = nullptr;
					const SceneEntity* idCamera = nullptr;
					if (component.monitorCameraEntityId != 0) {
						idCamera =
							document.FindEntity(component.monitorCameraEntityId);
						selectedCamera = idCamera;
					}
					const SceneEntity* namedCamera = nullptr;
					if (!component.monitorCameraName.empty()) {
						namedCamera =
							document.FindEntityByName(component.monitorCameraName);
					}
					if (
						idCamera &&
						namedCamera &&
						idCamera->id != namedCamera->id
					) {
						selectedCamera = namedCamera;
						ImGui::TextColored(
							ImVec4(0.95f, 0.65f, 0.25f, 1.0f),
							"Stored camera ID and name differ; name is used"
						);
						if (ImGui::Button("Repair Camera ID")) {
							component.monitorCameraEntityId = namedCamera->id;
							monitorChanged = true;
						}
					} else if (
						namedCamera &&
						component.monitorCameraEntityId != namedCamera->id
					) {
						selectedCamera = namedCamera;
						ImGui::TextDisabled(
							"Camera name resolves, but ID is not bound"
						);
						if (ImGui::Button("Repair Camera ID")) {
							component.monitorCameraEntityId = namedCamera->id;
							monitorChanged = true;
						}
					} else if (!selectedCamera) {
						selectedCamera = namedCamera;
					}
					if (
						!selectedCamera ||
						!FindEnabledComponent(*selectedCamera, "Camera")
					) {
						ImGui::TextColored(
							ImVec4(0.95f, 0.55f, 0.25f, 1.0f),
							"Target camera is missing"
						);
					} else if (!selectedCamera->active) {
						ImGui::TextColored(
							ImVec4(0.95f, 0.55f, 0.25f, 1.0f),
							"Target camera is inactive"
						);
					} else if (HasComponent(*selectedCamera, "PlayerBehavior")) {
						ImGui::TextDisabled(
							"Target is the gameplay/player camera"
						);
					}
				}

				const char* currentPreset = component.monitorResolutionPreset.empty()
					? "Custom"
					: component.monitorResolutionPreset.c_str();
				if (ImGui::BeginCombo("Resolution Preset", currentPreset)) {
					for (const MonitorResolutionPreset& preset : monitorPresets) {
						const bool selected =
							component.monitorResolutionPreset == preset.label ||
							(
								component.monitorResolutionPreset.empty() &&
								std::string(preset.label) == "Custom"
							);
						if (ImGui::Selectable(preset.label, selected)) {
							component.monitorResolutionPreset = preset.label;
							if (preset.width != 0 && preset.height != 0) {
								component.monitorWidth = preset.width;
								component.monitorHeight = preset.height;
							}
							monitorChanged = true;
						}
					}
					ImGui::EndCombo();
				}
				int monitorWidth = static_cast<int>(component.monitorWidth);
				int monitorHeight = static_cast<int>(component.monitorHeight);
				if (ImGui::DragInt("Width", &monitorWidth, 16.0f, 64, 2048)) {
					component.monitorWidth =
						static_cast<uint32_t>(std::clamp(monitorWidth, 64, 2048));
					component.monitorResolutionPreset = "Custom";
					monitorChanged = true;
				}
				if (ImGui::DragInt("Height", &monitorHeight, 16.0f, 64, 2048)) {
					component.monitorHeight =
						static_cast<uint32_t>(std::clamp(monitorHeight, 64, 2048));
					component.monitorResolutionPreset = "Custom";
					monitorChanged = true;
				}
				monitorChanged |= ImGui::Checkbox(
					"Hide Self In View",
					&component.monitorHideSelf
				);
				if (monitorChanged) {
					document.MarkDirty();
				}
				ImGui::EndDisabled();
			} else if (component.type == "ThirdPersonCamera") {
				ImGui::BeginDisabled(!editorSession_->IsEditing() || entityLocked);
				bool thirdPersonChanged = false;
				thirdPersonChanged |= ImGui::DragFloat(
					"Distance",
					&component.thirdPersonDistance,
					0.05f,
					0.01f,
					30.0f
				);
				thirdPersonChanged |= ImGui::DragFloat(
					"Aim Distance",
					&component.thirdPersonAimDistance,
					0.05f,
					0.01f,
					30.0f
				);
				thirdPersonChanged |= ImGui::DragFloat3(
					"Target Offset",
					&component.thirdPersonTargetOffset.x,
					0.01f
				);
				thirdPersonChanged |= ImGui::DragFloat3(
					"Aim Target Offset",
					&component.thirdPersonAimTargetOffset.x,
					0.01f
				);
				thirdPersonChanged |= ImGui::DragFloat(
					"Mouse Sensitivity",
					&component.thirdPersonMouseSensitivity,
					0.0001f,
					0.0f,
					0.1f,
					"%.4f"
				);
				thirdPersonChanged |= ImGui::DragFloat(
					"Min Pitch",
					&component.thirdPersonMinPitch,
					0.01f,
					-1.56f,
					1.56f
				);
				thirdPersonChanged |= ImGui::DragFloat(
					"Max Pitch",
					&component.thirdPersonMaxPitch,
					0.01f,
					-1.56f,
					1.56f
				);
				thirdPersonChanged |= ImGui::DragFloat(
					"Occlusion Margin",
					&component.thirdPersonOcclusionMargin,
					0.01f,
					0.0f,
					5.0f
				);
				thirdPersonChanged |= ImGui::Checkbox(
					"Invert Horizontal",
					&component.thirdPersonInvertYaw
				);
				thirdPersonChanged |= ImGui::Checkbox(
					"Invert Vertical",
					&component.thirdPersonInvertPitch
				);
				if (component.thirdPersonDistance < 0.01f) {
					component.thirdPersonDistance = 0.01f;
					thirdPersonChanged = true;
				}
				if (component.thirdPersonAimDistance < 0.01f) {
					component.thirdPersonAimDistance = 0.01f;
					thirdPersonChanged = true;
				}
				if (component.thirdPersonMouseSensitivity < 0.0f) {
					component.thirdPersonMouseSensitivity = 0.0f;
					thirdPersonChanged = true;
				}
				if (component.thirdPersonMaxPitch < component.thirdPersonMinPitch) {
					std::swap(
						component.thirdPersonMinPitch,
						component.thirdPersonMaxPitch
					);
					thirdPersonChanged = true;
				}
				if (component.thirdPersonOcclusionMargin < 0.0f) {
					component.thirdPersonOcclusionMargin = 0.0f;
					thirdPersonChanged = true;
				}
				if (thirdPersonChanged) {
					document.MarkDirty();
				}
				ImGui::EndDisabled();
			} else if (component.type == "CameraPath") {
				ImGui::BeginDisabled(!editorSession_->IsEditing() || entityLocked);
				bool pathChanged = false;
				const char* currentTargetCamera =
					component.cameraPathTargetCameraName.empty()
					? "Main Camera / Current"
					: component.cameraPathTargetCameraName.c_str();
				if (ImGui::BeginCombo("Target Camera Entity", currentTargetCamera)) {
					if (ImGui::Selectable(
						"Main Camera / Current",
						component.cameraPathTargetCameraName.empty()
					)) {
						component.cameraPathTargetCameraName.clear();
						pathChanged = true;
					}
					for (const SceneEntity& candidate : document.GetEntities()) {
						if (!FindEnabledComponent(candidate, "Camera")) {
							continue;
						}
						if (ImGui::Selectable(
							candidate.name.c_str(),
							component.cameraPathTargetCameraName == candidate.name
						)) {
							component.cameraPathTargetCameraName = candidate.name;
							pathChanged = true;
						}
					}
					ImGui::EndCombo();
				}
				const char* currentTrigger = component.cameraPathTriggerType.empty()
					? "Key"
					: component.cameraPathTriggerType.c_str();
				if (ImGui::BeginCombo("Trigger Type", currentTrigger)) {
					const char* triggerTypes[] = { "Manual", "Key" };
					for (const char* triggerType : triggerTypes) {
						if (ImGui::Selectable(
							triggerType,
							component.cameraPathTriggerType == triggerType
						)) {
							component.cameraPathTriggerType = triggerType;
							pathChanged = true;
						}
					}
					ImGui::EndCombo();
				}
				char triggerKeyBuffer[32]{};
				strncpy_s(
					triggerKeyBuffer,
					component.cameraPathTriggerKey.c_str(),
					_TRUNCATE
				);
				if (ImGui::InputText(
					"Trigger Key",
					triggerKeyBuffer,
					sizeof(triggerKeyBuffer)
				)) {
					component.cameraPathTriggerKey = triggerKeyBuffer;
					pathChanged = true;
				}
				pathChanged |= ImGui::DragFloat(
					"Enter Duration",
					&component.cameraPathEnterDuration,
					0.01f,
					0.0f,
					60.0f
				);
				pathChanged |= ImGui::DragFloat(
					"Exit Duration",
					&component.cameraPathExitDuration,
					0.01f,
					0.0f,
					60.0f
				);
				const char* currentInterpolation =
					component.cameraPathInterpolation.empty()
					? "Linear"
					: component.cameraPathInterpolation.c_str();
				if (ImGui::BeginCombo("Interpolation", currentInterpolation)) {
					const char* interpolations[] = { "Linear", "CatmullRom" };
					for (const char* interpolation : interpolations) {
						if (ImGui::Selectable(
							interpolation,
							component.cameraPathInterpolation == interpolation
						)) {
							component.cameraPathInterpolation = interpolation;
							pathChanged = true;
						}
					}
					ImGui::EndCombo();
				}
				const char* currentEasing =
					component.cameraPathDefaultEasing.empty()
					? "SmoothStep"
					: component.cameraPathDefaultEasing.c_str();
				if (ImGui::BeginCombo("Default Easing", currentEasing)) {
					const char* easings[] = {
						"Linear",
						"EaseIn",
						"EaseOut",
						"EaseInOut",
						"SmoothStep"
					};
					for (const char* easing : easings) {
						if (ImGui::Selectable(
							easing,
							component.cameraPathDefaultEasing == easing
						)) {
							component.cameraPathDefaultEasing = easing;
							pathChanged = true;
						}
					}
					ImGui::EndCombo();
				}
				pathChanged |= ImGui::Checkbox(
					"Return To Previous Camera",
					&component.cameraPathReturnToPreviousCamera
				);
				pathChanged |= ImGui::Checkbox(
					"Start From Current Camera",
					&component.cameraPathStartFromCurrentCamera
				);
				pathChanged |= ImGui::Checkbox(
					"Auto Collect Child Points",
					&component.cameraPathAutoCollectChildPoints
				);
				if (component.cameraPathEnterDuration < 0.0f) {
					component.cameraPathEnterDuration = 0.0f;
					pathChanged = true;
				}
				if (component.cameraPathExitDuration < 0.0f) {
					component.cameraPathExitDuration = 0.0f;
					pathChanged = true;
				}
				if (pathChanged) {
					document.MarkDirty();
				}

				ImGui::SeparatorText("Child Points");
				uint32_t pointCount = 0;
				for (const SceneEntity& candidate : document.GetEntities()) {
					if (candidate.parentId != entity->id) {
						continue;
					}
					if (!HasComponent(candidate, "CameraPathPoint")) {
						continue;
					}
					ImGui::PushID(static_cast<int>(candidate.id));
					ImGui::Text("%02u: %s", pointCount, candidate.name.c_str());
					ImGui::SameLine();
					if (ImGui::SmallButton("Select")) {
						selectedEntityId_ = candidate.id;
					}
					ImGui::PopID();
					++pointCount;
				}
				if (ImGui::SmallButton("Add Point")) {
					char pointName[32]{};
					sprintf_s(pointName, "Point_%02u", pointCount);
					SceneEntity& point = document.CreateEntity(pointName, entity->id);
					point.transform.translate = {
						0.0f,
						0.0f,
						static_cast<float>(pointCount) * 5.0f
					};
					point.components.push_back(SceneComponent{
						"CameraPathPoint",
						true
					});
					point.components.back().cameraPathPointDurationToNext = 1.0f;
					point.components.back().cameraPathPointEasingToNext =
						"SmoothStep";
					selectedEntityId_ = point.id;
					editorSession_->RequestSceneReload();
				}
				ImGui::EndDisabled();
			} else if (component.type == "CameraPathPoint") {
				ImGui::BeginDisabled(!editorSession_->IsEditing() || entityLocked);
				bool pointChanged = false;
				pointChanged |= ImGui::DragFloat(
					"Duration To Next",
					&component.cameraPathPointDurationToNext,
					0.01f,
					0.0f,
					60.0f
				);
				const char* currentEasing =
					component.cameraPathPointEasingToNext.empty()
					? "SmoothStep"
					: component.cameraPathPointEasingToNext.c_str();
				if (ImGui::BeginCombo("Easing To Next", currentEasing)) {
					const char* easings[] = {
						"Linear",
						"EaseIn",
						"EaseOut",
						"EaseInOut",
						"SmoothStep"
					};
					for (const char* easing : easings) {
						if (ImGui::Selectable(
							easing,
							component.cameraPathPointEasingToNext == easing
						)) {
							component.cameraPathPointEasingToNext = easing;
							pointChanged = true;
						}
					}
					ImGui::EndCombo();
				}
				if (component.cameraPathPointDurationToNext < 0.0f) {
					component.cameraPathPointDurationToNext = 0.0f;
					pointChanged = true;
				}
				if (pointChanged) {
					document.MarkDirty();
				}
				ImGui::EndDisabled();
			} else if (component.type == "EntityReference") {
				ImGui::BeginDisabled(!editorSession_->IsEditing() || entityLocked);
				bool referenceChanged = false;
				char referenceNameBuffer[64]{};
				CopyTextBuffer(
					referenceNameBuffer,
					sizeof(referenceNameBuffer),
					component.entityReferenceName
				);
				if (ImGui::InputText(
					"Reference Name",
					referenceNameBuffer,
					sizeof(referenceNameBuffer)
				)) {
					component.entityReferenceName = referenceNameBuffer;
					referenceChanged = true;
				}
				char targetSceneIdBuffer[128]{};
				CopyTextBuffer(
					targetSceneIdBuffer,
					sizeof(targetSceneIdBuffer),
					component.entityReferenceTarget.sceneId
				);
				if (ImGui::InputText(
					"Target Scene Id",
					targetSceneIdBuffer,
					sizeof(targetSceneIdBuffer)
				)) {
					component.entityReferenceTarget.sceneId = targetSceneIdBuffer;
					referenceChanged = true;
				}
				if (component.entityReferenceTarget.sceneId.empty()) {
					ImGui::TextDisabled("Empty Scene Id targets this Scene Instance.");
				}
				char instanceKeyBuffer[128]{};
				CopyTextBuffer(
					instanceKeyBuffer,
					sizeof(instanceKeyBuffer),
					component.entityReferenceTarget.instanceKey
				);
				if (ImGui::InputText(
					"Target Instance Key",
					instanceKeyBuffer,
					sizeof(instanceKeyBuffer)
				)) {
					component.entityReferenceTarget.instanceKey = instanceKeyBuffer;
					referenceChanged = true;
				}
				referenceChanged |= ImGui::InputScalar(
					"Target Entity Id",
					ImGuiDataType_U64,
					&component.entityReferenceTarget.entityId
				);
				if (referenceChanged) {
					document.MarkDirty();
				}
				ImGui::EndDisabled();
			} else if (component.type == "SceneTransition") {
				ImGui::BeginDisabled(!editorSession_->IsEditing() || entityLocked);
				bool transitionChanged = false;
				const SceneDescriptor* targetScene = sceneCatalog_
					? sceneCatalog_->Find(
						component.sceneTransitionTargetSceneId
					)
					: nullptr;
				const char* targetLabel = targetScene
					? targetScene->displayName.c_str()
					: "Select...";
				if (ImGui::BeginCombo("Target Scene", targetLabel)) {
					if (sceneCatalog_) {
						for (const SceneDescriptor& scene :
							sceneCatalog_->GetScenes()) {
							const std::string sceneLabel =
								scene.displayName + "##" + scene.id;
							if (ImGui::Selectable(
								sceneLabel.c_str(),
								component.sceneTransitionTargetSceneId ==
									scene.id
							)) {
								component.sceneTransitionTargetSceneId = scene.id;
								transitionChanged = true;
							}
						}
					}
					ImGui::EndCombo();
				}

				const char* triggerKeys[] = {
					"ENTER", "SPACE", "ESCAPE", "TAB",
					"A", "B", "C", "D", "E", "F", "G", "H",
					"I", "J", "K", "L", "M", "N", "O", "P",
					"Q", "R", "S", "T", "U", "V", "W", "X",
					"Y", "Z"
				};
				const char* triggerKey =
					component.sceneTransitionTriggerKey.empty()
						? "ENTER"
						: component.sceneTransitionTriggerKey.c_str();
				if (ImGui::BeginCombo("Trigger Key", triggerKey)) {
					for (const char* key : triggerKeys) {
						if (ImGui::Selectable(
							key,
							component.sceneTransitionTriggerKey == key
						)) {
							component.sceneTransitionTriggerType = "Key";
							component.sceneTransitionTriggerKey = key;
							transitionChanged = true;
						}
					}
					ImGui::EndCombo();
				}
				if (transitionChanged) {
					document.MarkDirty();
				}
				ImGui::EndDisabled();
			} else if (component.type == "Animator") {
				ImGui::BeginDisabled(!editorSession_->IsEditing() || entityLocked);
				bool animatorChanged = false;
				animatorChanged |= ImGui::Checkbox(
					"Play On Start",
					&component.animatorPlayOnStart
				);
				animatorChanged |= ImGui::Checkbox(
					"Loop",
					&component.animatorLoop
				);
				animatorChanged |= ImGui::DragFloat(
					"Speed",
					&component.animatorSpeed,
					0.01f,
					-8.0f,
					8.0f
				);
				animatorChanged |= ImGui::DragInt(
					"Default Clip Index",
					&component.animatorDefaultClip,
					1.0f,
					0,
					1024
				);
				animatorChanged |= ImGui::DragFloat(
					"Transition Duration",
					&component.animatorTransitionDuration,
					0.01f,
					0.0f,
					10.0f
				);
				const char* blendCurve =
					component.animatorBlendCurve == "Linear"
					? "Linear"
					: "SmoothStep";
				if (ImGui::BeginCombo("Blend Curve", blendCurve)) {
					for (const char* candidate : { "Linear", "SmoothStep" }) {
						if (ImGui::Selectable(
							candidate,
							component.animatorBlendCurve == candidate
						)) {
							component.animatorBlendCurve = candidate;
							animatorChanged = true;
						}
					}
					ImGui::EndCombo();
				}
				component.animatorDefaultClip = (std::max)(
					component.animatorDefaultClip,
					0
				);
				component.animatorTransitionDuration = (std::max)(
					component.animatorTransitionDuration,
					0.0f
				);
				if (animatorChanged) {
					document.MarkDirty();
				}
				ImGui::EndDisabled();
			} else if (component.type == "OBBCollider") {
				ImGui::BeginDisabled(!editorSession_->IsEditing() || entityLocked);
				bool colliderChanged = false;
				const char* shape = component.colliderShape == "Sphere"
					? "Sphere"
					: "Box";
				if (ImGui::BeginCombo("Shape", shape)) {
					for (const char* candidate : { "Box", "Sphere" }) {
						if (ImGui::Selectable(
							candidate,
							component.colliderShape == candidate
						)) {
							component.colliderShape = candidate;
							colliderChanged = true;
						}
					}
					ImGui::EndCombo();
				}
				colliderChanged |= ImGui::DragFloat3(
					"Offset",
					&component.colliderOffset.x,
					0.01f
				);
				if (component.colliderShape == "Sphere") {
					colliderChanged |= ImGui::DragFloat(
						"Radius",
						&component.colliderSphereRadius,
						0.01f,
						0.001f,
						100.0f
					);
				} else {
					colliderChanged |= ImGui::DragFloat3(
						"Size Multiplier",
						&component.colliderSizeMultiplier.x,
						0.01f,
						0.001f,
						100.0f
					);
				}
				colliderChanged |= ImGui::Checkbox(
					"Debug Visible",
					&component.colliderDebugVisible
				);
				if (ImGui::BeginCombo(
					"Draw Mode",
					component.colliderDebugDrawMode.c_str()
				)) {
					for (const char* mode : {
						"Wireframe", "Solid", "WireframeAndSolid"
					}) {
						if (ImGui::Selectable(
							mode,
							component.colliderDebugDrawMode == mode
						)) {
							component.colliderDebugDrawMode = mode;
							colliderChanged = true;
						}
					}
					ImGui::EndCombo();
				}
				if (component.colliderShape == "Sphere") {
					colliderChanged |= ImGui::SliderInt(
						"Debug Segments",
						&component.colliderDebugSegments,
						4,
						64
					);
				}
				colliderChanged |= ImGui::ColorEdit4(
					"Debug Color",
					&component.colliderDebugColor.x,
					ImGuiColorEditFlags_Float
				);
				component.colliderSizeMultiplier.x = (std::max)(
					component.colliderSizeMultiplier.x,
					0.001f
				);
				component.colliderSizeMultiplier.y = (std::max)(
					component.colliderSizeMultiplier.y,
					0.001f
				);
				component.colliderSizeMultiplier.z = (std::max)(
					component.colliderSizeMultiplier.z,
					0.001f
				);
				component.colliderSphereRadius = (std::max)(
					component.colliderSphereRadius,
					0.001f
				);
				component.colliderDebugSegments = std::clamp(
					component.colliderDebugSegments,
					4,
					64
				);
				if (colliderChanged) {
					document.MarkDirty();
				}
				ImGui::EndDisabled();
			} else if (component.type == "PhysicsBody") {
				ImGui::BeginDisabled(!editorSession_->IsEditing() || entityLocked);
				bool physicsChanged = false;
				const char* currentBodyType = component.physicsBodyType.empty()
					? "Static"
					: component.physicsBodyType.c_str();
				if (ImGui::BeginCombo("Body Type", currentBodyType)) {
					const char* bodyTypes[] = { "Static", "Dynamic", "Kinematic" };
					for (const char* bodyType : bodyTypes) {
						if (ImGui::Selectable(
							bodyType,
							component.physicsBodyType == bodyType ||
								(component.physicsBodyType.empty() &&
									std::strcmp(bodyType, "Static") == 0)
						)) {
							component.physicsBodyType = bodyType;
							physicsChanged = true;
						}
					}
					ImGui::EndCombo();
				}
				physicsChanged |= ImGui::DragFloat(
					"Mass",
					&component.physicsMass,
					0.05f,
					0.001f,
					10000.0f
				);
				physicsChanged |= ImGui::Checkbox(
					"Use Gravity",
					&component.physicsUseGravity
				);
				physicsChanged |= ImGui::DragFloat(
					"Gravity Scale",
					&component.physicsGravityScale,
					0.05f,
					-10.0f,
					10.0f
				);
				physicsChanged |= ImGui::DragFloat(
					"Drag",
					&component.physicsDrag,
					0.02f,
					0.0f,
					100.0f
				);
				physicsChanged |= ImGui::SliderFloat(
					"Restitution",
					&component.physicsRestitution,
					0.0f,
					1.0f
				);
				physicsChanged |= ImGui::SliderFloat(
					"Friction",
					&component.physicsFriction,
					0.0f,
					1.0f
				);
				physicsChanged |= ImGui::DragFloat(
					"Max Fall Speed",
					&component.physicsMaxFallSpeed,
					0.1f,
					0.0f,
					1000.0f
				);
				physicsChanged |= ImGui::DragFloat3(
					"Velocity",
					&component.physicsVelocity.x,
					0.05f
				);
				ImGui::TextDisabled("Freeze Position");
				physicsChanged |= ImGui::Checkbox(
					"X##FreezePosition",
					&component.physicsFreezePositionX
				);
				ImGui::SameLine();
				physicsChanged |= ImGui::Checkbox(
					"Y##FreezePosition",
					&component.physicsFreezePositionY
				);
				ImGui::SameLine();
				physicsChanged |= ImGui::Checkbox(
					"Z##FreezePosition",
					&component.physicsFreezePositionZ
				);
				if (component.physicsMass <= 0.0f) {
					component.physicsMass = 0.001f;
					physicsChanged = true;
				}
				component.physicsRestitution = std::clamp(
					component.physicsRestitution,
					0.0f,
					1.0f
				);
				component.physicsFriction = std::clamp(
					component.physicsFriction,
					0.0f,
					1.0f
				);
				if (component.physicsMaxFallSpeed < 0.0f) {
					component.physicsMaxFallSpeed = 0.0f;
					physicsChanged = true;
				}
				if (physicsChanged) {
					document.MarkDirty();
				}
				ImGui::EndDisabled();
			} else if (component.type == "PlayerBehavior") {
				ImGui::BeginDisabled(!editorSession_->IsEditing() || entityLocked);
				bool playerChanged = false;
				playerChanged |= ImGui::DragFloat(
					"Move Speed",
					&component.playerMoveSpeed,
					0.1f,
					0.0f,
					100.0f
				);
				playerChanged |= ImGui::DragFloat(
					"Jump Velocity",
					&component.playerJumpVelocity,
					0.1f,
					0.0f,
					200.0f
				);
				playerChanged |= ImGui::SliderFloat(
					"Turn Responsiveness",
					&component.playerTurnResponsiveness,
					0.0f,
					1.0f
				);
				playerChanged |= ImGui::DragFloat(
					"Dash Multiplier",
					&component.playerDashMultiplier,
					0.05f,
					1.0f,
					5.0f
				);
				playerChanged |= ImGui::Checkbox(
					"Camera Relative Move",
					&component.playerCameraRelativeMove
				);
				playerChanged |= ImGui::Checkbox(
					"Allow Jump",
					&component.playerAllowJump
				);
				if (component.playerMoveSpeed < 0.0f) {
					component.playerMoveSpeed = 0.0f;
					playerChanged = true;
				}
				if (component.playerJumpVelocity < 0.0f) {
					component.playerJumpVelocity = 0.0f;
					playerChanged = true;
				}
				component.playerTurnResponsiveness = std::clamp(
					component.playerTurnResponsiveness,
					0.0f,
					1.0f
				);
				if (component.playerDashMultiplier < 1.0f) {
					component.playerDashMultiplier = 1.0f;
					playerChanged = true;
				}
				if (playerChanged) {
					document.MarkDirty();
				}
				ImGui::EndDisabled();
			} else if (component.type == "AgentBehavior") {
				ImGui::BeginDisabled(!editorSession_->IsEditing() || entityLocked);
				bool agentChanged = false;
				char behaviorBuffer[64]{};
				strncpy_s(
					behaviorBuffer,
					component.agentBehaviorName.c_str(),
					_TRUNCATE
				);
				if (ImGui::InputText(
					"Behavior",
					behaviorBuffer,
					sizeof(behaviorBuffer)
				)) {
					component.agentBehaviorName = behaviorBuffer;
					agentChanged = true;
				}
				char profileBuffer[64]{};
				strncpy_s(
					profileBuffer,
					component.agentProfileName.c_str(),
					_TRUNCATE
				);
				if (ImGui::InputText(
					"Profile",
					profileBuffer,
					sizeof(profileBuffer)
				)) {
					component.agentProfileName = profileBuffer;
					agentChanged = true;
				}
				const SceneTeamSettings* agentTeam =
					document.ResolveEntityTeam(*entity);
				const bool hasTeamAgentSettings =
					agentTeam && agentTeam->agentBehaviorOverride;
				if (hasTeamAgentSettings) {
					agentChanged |= ImGui::Checkbox(
						"Override Team Agent Settings",
						&component.agentTeamSettingsOverride
					);
				}
				const bool useTeamAgentSettings =
					hasTeamAgentSettings &&
					!component.agentTeamSettingsOverride;
				if (useTeamAgentSettings) {
					ImGui::Text(
						"Using Team Agent Settings: %s",
						agentTeam->name.c_str()
					);
				}
				ImGui::BeginDisabled(useTeamAgentSettings);
				char groupBuffer[64]{};
				strncpy_s(
					groupBuffer,
					component.agentGroupName.c_str(),
					_TRUNCATE
				);
				if (ImGui::InputText(
					"Group",
					groupBuffer,
					sizeof(groupBuffer)
				)) {
					component.agentGroupName = groupBuffer;
					agentChanged = true;
				}

				ImGui::SeparatorText("Motion");
				agentChanged |= ImGui::DragFloat(
					"Min Speed",
					&component.agentMinSpeed,
					0.05f,
					0.0f,
					100.0f
				);
				agentChanged |= ImGui::DragFloat(
					"Max Speed",
					&component.agentMaxSpeed,
					0.05f,
					0.0f,
					100.0f
				);
				agentChanged |= ImGui::DragFloat(
					"Turn Speed",
					&component.agentTurnSpeed,
					0.05f,
					0.0f,
					20.0f
				);
				agentChanged |= ImGui::DragFloat(
					"Wander Strength",
					&component.agentWanderStrength,
					0.05f,
					0.0f,
					20.0f
				);
				agentChanged |= ImGui::DragFloat(
					"Wander Change Interval",
					&component.agentWanderChangeInterval,
					0.05f,
					0.0f,
					60.0f
				);
				agentChanged |= ImGui::SliderFloat(
					"Wander Direction Range",
					&component.agentWanderDirectionRange,
					0.0f,
					3.141592f
				);
				agentChanged |= ImGui::SliderFloat(
					"Wander Vertical Range",
					&component.agentWanderVerticalRange,
					0.0f,
					1.0f
				);
				agentChanged |= ImGui::Checkbox(
					"Randomize Seed On Play",
					&component.agentRandomizeSeedOnPlay
				);
				ImGui::BeginDisabled(component.agentRandomizeSeedOnPlay);
				agentChanged |= ImGui::InputInt(
					"Random Seed",
					&component.agentRandomSeed
				);
				ImGui::EndDisabled();
				agentChanged |= ImGui::DragFloat(
					"Flock Decision Interval",
					&component.agentFlockDecisionInterval,
					0.01f,
					0.0f,
					5.0f
				);
				agentChanged |= ImGui::DragFloat(
					"Flock Acceleration",
					&component.agentFlockAcceleration,
					0.05f,
					0.0f,
					100.0f
				);
				agentChanged |= ImGui::DragFloat(
					"Flock Max Turn Rate",
					&component.agentFlockTurnRate,
					0.01f,
					0.0f,
					6.283185f
				);
				ImGui::SeparatorText("Member Follow");
				agentChanged |= ImGui::DragFloat(
					"Return Strength",
					&component.agentMemberCenterFollow,
					0.05f,
					0.0f,
					20.0f
				);
				agentChanged |= ImGui::DragFloat(
					"Jitter Strength",
					&component.agentMemberJitterStrength,
					0.01f,
					0.0f,
					10.0f
				);
				agentChanged |= ImGui::DragFloat(
					"Jitter Frequency",
					&component.agentMemberJitterFrequency,
					0.01f,
					0.0f,
					10.0f
				);
				agentChanged |= ImGui::DragFloat(
					"Jitter Update Interval",
					&component.agentMemberJitterUpdateInterval,
					0.01f,
					0.0f,
					10.0f
				);
				agentChanged |= ImGui::DragFloat(
					"Jitter Follow Speed",
					&component.agentMemberJitterFollowSpeed,
					0.01f,
					0.0f,
					20.0f
				);
				agentChanged |= ImGui::DragFloat(
					"Max Distance",
					&component.agentMemberLeashDistance,
					0.05f,
					0.0f,
					100.0f
				);
				agentChanged |= ImGui::DragFloat(
					"Leash Strength",
					&component.agentMemberLeashStrength,
					0.05f,
					0.0f,
					20.0f
				);
				agentChanged |= ImGui::DragFloat(
					"Catchup Speed",
					&component.agentMemberCatchupSpeed,
					0.05f,
					0.0f,
					100.0f
				);
				agentChanged |= ImGui::DragFloat(
					"Separation Update Interval",
					&component.agentMemberSeparationUpdateInterval,
					0.01f,
					0.0f,
					5.0f
				);
				agentChanged |= ImGui::SliderFloat(
					"Separation Blend",
					&component.agentMemberSeparationBlend,
					0.0f,
					1.0f
				);

				ImGui::SeparatorText("Team Heading");
				agentChanged |= ImGui::Checkbox(
					"Use Team Heading",
					&component.agentUseTeamHeading
				);
				agentChanged |= ImGui::DragFloat3(
					"Team Heading Direction",
					&component.agentTeamHeadingDirection.x,
					0.01f,
					-1.0f,
					1.0f
				);
				agentChanged |= ImGui::DragFloat(
					"Team Heading Weight",
					&component.agentTeamHeadingWeight,
					0.05f,
					0.0f,
					20.0f
				);
				agentChanged |= ImGui::DragFloat(
					"Team Heading Follow Speed",
					&component.agentTeamHeadingFollowSpeed,
					0.05f,
					0.0f,
					20.0f
				);

				ImGui::SeparatorText("Rotation");
				agentChanged |= ImGui::Checkbox(
					"Align Forward To Velocity",
					&component.agentAlignForwardToVelocity
				);
				const char* agentForwardAxes[] = {
					"+Z",
					"-Z",
					"+X",
					"-X",
					"+Y",
					"-Y"
				};
				if (ImGui::BeginCombo(
					"Forward Axis",
					component.agentForwardAxis.c_str()
				)) {
					for (const char* axis : agentForwardAxes) {
						if (ImGui::Selectable(
							axis,
							component.agentForwardAxis == axis
						)) {
							component.agentForwardAxis = axis;
							agentChanged = true;
						}
					}
					ImGui::EndCombo();
				}
				agentChanged |= ImGui::Checkbox(
					"Rotate X",
					&component.agentRotateAxisX
				);
				ImGui::SameLine();
				agentChanged |= ImGui::Checkbox(
					"Rotate Y",
					&component.agentRotateAxisY
				);
				ImGui::SameLine();
				agentChanged |= ImGui::Checkbox(
					"Rotate Z",
					&component.agentRotateAxisZ
				);
				agentChanged |= ImGui::DragFloat(
					"Rotation Follow Speed",
					&component.agentRotationFollowSpeed,
					0.05f,
					0.0f,
					60.0f
				);
				agentChanged |= ImGui::DragFloat(
					"Pitch From Vertical Velocity",
					&component.agentPitchFromVerticalVelocity,
					0.05f,
					0.0f,
					4.0f
				);
				agentChanged |= ImGui::DragFloat(
					"Banking Strength",
					&component.agentBankingStrength,
					0.05f,
					0.0f,
					4.0f
				);
				ImGui::EndDisabled();

				ImGui::SeparatorText("Bounds");
				agentChanged |= ImGui::Checkbox(
					"Use Water Bounds",
					&component.agentUseWaterBounds
				);
				agentChanged |= ImGui::InputScalar(
					"Bounds Entity Id",
					ImGuiDataType_U64,
					&component.agentBoundsEntityId
				);
				char boundsNameBuffer[128]{};
				strncpy_s(
					boundsNameBuffer,
					component.agentBoundsName.c_str(),
					_TRUNCATE
				);
				if (ImGui::InputText(
					"Bounds Name",
					boundsNameBuffer,
					sizeof(boundsNameBuffer)
				)) {
					component.agentBoundsName = boundsNameBuffer;
					agentChanged = true;
				}
				agentChanged |= ImGui::DragFloat(
					"Bounds Weight",
					&component.agentBoundsWeight,
					0.05f,
					0.0f,
					50.0f
				);

				ImGui::SeparatorText("Attractor");
				agentChanged |= ImGui::InputScalar(
					"Attractor Entity Id",
					ImGuiDataType_U64,
					&component.agentAttractorEntityId
				);
				char attractorTagBuffer[64]{};
				strncpy_s(
					attractorTagBuffer,
					component.agentAttractorTag.c_str(),
					_TRUNCATE
				);
				if (ImGui::InputText(
					"Attractor Tag",
					attractorTagBuffer,
					sizeof(attractorTagBuffer)
				)) {
					component.agentAttractorTag = attractorTagBuffer;
					agentChanged = true;
				}
				agentChanged |= ImGui::DragFloat(
					"Attractor Weight",
					&component.agentAttractorWeight,
					0.05f,
					0.0f,
					50.0f
				);

				ImGui::BeginDisabled(useTeamAgentSettings);
				ImGui::SeparatorText("Schooling");
				agentChanged |= ImGui::Checkbox(
					"Schooling",
					&component.agentSchooling
				);
				agentChanged |= ImGui::DragFloat(
					"Schooling Update Interval",
					&component.agentSchoolingUpdateInterval,
					0.01f,
					0.0f,
					5.0f
				);
				agentChanged |= ImGui::DragFloat(
					"Schooling Update Jitter",
					&component.agentSchoolingUpdateJitter,
					0.01f,
					0.0f,
					1.0f
				);
				agentChanged |= ImGui::InputInt(
					"Neighbor Limit",
					&component.agentNeighborLimit
				);
				agentChanged |= ImGui::SliderFloat(
					"Schooling Blend",
					&component.agentSchoolingBlend,
					0.0f,
					1.0f
				);
				agentChanged |= ImGui::DragFloat(
					"Separation Radius",
					&component.agentSeparationRadius,
					0.05f,
					0.0f,
					100.0f
				);
				agentChanged |= ImGui::DragFloat(
					"Alignment Radius",
					&component.agentAlignmentRadius,
					0.05f,
					0.0f,
					100.0f
				);
				agentChanged |= ImGui::DragFloat(
					"Cohesion Radius",
					&component.agentCohesionRadius,
					0.05f,
					0.0f,
					100.0f
				);
				agentChanged |= ImGui::DragFloat(
					"Separation Weight",
					&component.agentSeparationWeight,
					0.05f,
					0.0f,
					50.0f
				);
				agentChanged |= ImGui::DragFloat(
					"Alignment Weight",
					&component.agentAlignmentWeight,
					0.05f,
					0.0f,
					50.0f
				);
				agentChanged |= ImGui::DragFloat(
					"Cohesion Weight",
					&component.agentCohesionWeight,
					0.05f,
					0.0f,
					50.0f
				);

				ImGui::SeparatorText("Visual");
				agentChanged |= ImGui::ColorEdit4(
					"Visual Color",
					&component.agentVisualColor.x,
					ImGuiColorEditFlags_Float
				);
				agentChanged |= ImGui::Checkbox(
					"Enable Lighting",
					&component.agentEnableLighting
				);
				ImGui::EndDisabled();

				if (component.agentBehaviorName.empty()) {
					component.agentBehaviorName = "Agent";
					agentChanged = true;
				}
				if (component.agentProfileName.empty()) {
					component.agentProfileName = "Default";
					agentChanged = true;
				}
				component.agentMinSpeed =
					(std::max)(component.agentMinSpeed, 0.0f);
				component.agentMaxSpeed =
					(std::max)(component.agentMaxSpeed, component.agentMinSpeed);
				component.agentTurnSpeed =
					(std::max)(component.agentTurnSpeed, 0.0f);
				component.agentWanderStrength =
					(std::max)(component.agentWanderStrength, 0.0f);
				component.agentBoundsWeight =
					(std::max)(component.agentBoundsWeight, 0.0f);
				const Vector3 normalizedTeamHeading =
					Math::Length(component.agentTeamHeadingDirection) <= 0.000001f
						? Vector3{ 0.0f, 0.0f, 1.0f }
						: Math::Normalize(component.agentTeamHeadingDirection);
				if (
					component.agentTeamHeadingDirection.x !=
						normalizedTeamHeading.x ||
					component.agentTeamHeadingDirection.y !=
						normalizedTeamHeading.y ||
					component.agentTeamHeadingDirection.z !=
						normalizedTeamHeading.z
				) {
					component.agentTeamHeadingDirection =
						normalizedTeamHeading;
					agentChanged = true;
				}
				if (component.agentTeamHeadingWeight < 0.0f) {
					component.agentTeamHeadingWeight = 0.0f;
					agentChanged = true;
				}
				if (component.agentTeamHeadingFollowSpeed < 0.0f) {
					component.agentTeamHeadingFollowSpeed = 0.0f;
					agentChanged = true;
				}
				const float teamRotationWeight = std::clamp(
					component.agentTeamRotationWeight,
					0.0f,
					1.0f
				);
				if (component.agentTeamRotationWeight != teamRotationWeight) {
					component.agentTeamRotationWeight = teamRotationWeight;
					agentChanged = true;
				}
				if (component.agentTeamRotationFollowSpeed < 0.0f) {
					component.agentTeamRotationFollowSpeed = 0.0f;
					agentChanged = true;
				}
				if (
					component.agentForwardAxis != "+Z" &&
					component.agentForwardAxis != "-Z" &&
					component.agentForwardAxis != "+X" &&
					component.agentForwardAxis != "-X" &&
					component.agentForwardAxis != "+Y" &&
					component.agentForwardAxis != "-Y"
				) {
					component.agentForwardAxis = "+Z";
					agentChanged = true;
				}
				component.agentRotationFollowSpeed =
					(std::max)(component.agentRotationFollowSpeed, 0.0f);
				component.agentPitchFromVerticalVelocity =
					(std::max)(
						component.agentPitchFromVerticalVelocity,
						0.0f
					);
				component.agentBankingStrength =
					(std::max)(component.agentBankingStrength, 0.0f);
				component.agentSchoolingUpdateInterval =
					(std::max)(
						component.agentSchoolingUpdateInterval,
						0.0f
					);
				component.agentSchoolingUpdateJitter =
					(std::max)(component.agentSchoolingUpdateJitter, 0.0f);
				component.agentNeighborLimit =
					(std::max)(component.agentNeighborLimit, 0);
				component.agentSchoolingBlend = std::clamp(
					component.agentSchoolingBlend,
					0.0f,
					1.0f
				);
				component.agentSeparationRadius =
					(std::max)(component.agentSeparationRadius, 0.0f);
				component.agentAlignmentRadius =
					(std::max)(component.agentAlignmentRadius, 0.0f);
				component.agentCohesionRadius =
					(std::max)(component.agentCohesionRadius, 0.0f);
				component.agentSeparationWeight =
					(std::max)(component.agentSeparationWeight, 0.0f);
				component.agentAlignmentWeight =
					(std::max)(component.agentAlignmentWeight, 0.0f);
				component.agentCohesionWeight =
					(std::max)(component.agentCohesionWeight, 0.0f);
				component.agentAttractorWeight =
					(std::max)(component.agentAttractorWeight, 0.0f);
				if (agentChanged) {
					document.MarkDirty();
				}
				ImGui::EndDisabled();
			} else if (component.type == "AgentAttractor") {
				ImGui::BeginDisabled(!editorSession_->IsEditing() || entityLocked);
				bool attractorChanged = false;
				char tagBuffer[64]{};
				strncpy_s(
					tagBuffer,
					component.attractorTag.c_str(),
					_TRUNCATE
				);
				if (ImGui::InputText("Tag", tagBuffer, sizeof(tagBuffer))) {
					component.attractorTag = tagBuffer;
					attractorChanged = true;
				}
				char targetBehaviorBuffer[64]{};
				strncpy_s(
					targetBehaviorBuffer,
					component.attractorTargetBehaviorName.c_str(),
					_TRUNCATE
				);
				if (ImGui::InputText(
					"Target Behavior",
					targetBehaviorBuffer,
					sizeof(targetBehaviorBuffer)
				)) {
					component.attractorTargetBehaviorName =
						targetBehaviorBuffer;
					attractorChanged = true;
				}
				char targetProfileBuffer[64]{};
				strncpy_s(
					targetProfileBuffer,
					component.attractorTargetProfileName.c_str(),
					_TRUNCATE
				);
				if (ImGui::InputText(
					"Target Profile",
					targetProfileBuffer,
					sizeof(targetProfileBuffer)
				)) {
					component.attractorTargetProfileName =
						targetProfileBuffer;
					attractorChanged = true;
				}
				attractorChanged |= ImGui::DragFloat(
					"Radius",
					&component.attractorRadius,
					0.1f,
					0.0f,
					500.0f
				);
				attractorChanged |= ImGui::DragFloat(
					"Strength",
					&component.attractorStrength,
					0.05f,
					0.0f,
					50.0f
				);
				attractorChanged |= ImGui::ColorEdit4(
					"Visual Color",
					&component.attractorVisualColor.x,
					ImGuiColorEditFlags_Float
				);
				if (component.attractorTag.empty()) {
					component.attractorTag = "Default";
					attractorChanged = true;
				}
				component.attractorRadius =
					(std::max)(component.attractorRadius, 0.0f);
				component.attractorStrength =
					(std::max)(component.attractorStrength, 0.0f);
				if (attractorChanged) {
					document.MarkDirty();
				}
				ImGui::EndDisabled();
			} else if (component.type == "WaterVolume") {
				ImGui::BeginDisabled(!editorSession_->IsEditing() || entityLocked);
				bool waterChanged = false;
				ImGui::SeparatorText("Volume");
				waterChanged |= ImGui::DragFloat3(
					"Half Size",
					&component.waterHalfSize.x,
					0.1f,
					0.1f,
					500.0f
				);
				waterChanged |= ImGui::DragFloat3(
					"Offset",
					&component.waterOffset.x,
					0.1f
				);
				ImGui::SeparatorText("Surface");
				waterChanged |= ImGui::Checkbox(
					"Surface Enabled",
					&component.waterSurfaceEnabled
				);
				waterChanged |= ImGui::ColorEdit4(
					"Base Color",
					&component.waterSurfaceBaseColor.x,
					ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR
				);
				waterChanged |= ImGui::ColorEdit4(
					"Highlight Color",
					&component.waterSurfaceHighlightColor.x,
					ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR
				);
				waterChanged |= ImGui::SliderFloat(
					"Surface Alpha",
					&component.waterSurfaceAlpha,
					0.0f,
					1.0f
				);
				waterChanged |= ImGui::SliderFloat(
					"Wave Scale",
					&component.waterSurfaceWaveScale,
					0.0f,
					3.0f
				);
				waterChanged |= ImGui::SliderFloat(
					"Normal Strength",
					&component.waterSurfaceNormalStrength,
					0.0f,
					2.0f
				);
				waterChanged |= ImGui::SliderFloat(
					"Fresnel Power",
					&component.waterSurfaceFresnelPower,
					0.2f,
					8.0f
				);
				ImGui::SeparatorText("Water Light");
				waterChanged |= ImGui::Checkbox(
					"Light Shafts",
					&component.waterLightShaftEnabled
				);
				waterChanged |= ImGui::ColorEdit4(
					"Light Color",
					&component.waterLightColor.x,
					ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR
				);
				if (ImGui::DragFloat3(
					"Light Direction",
					&component.waterLightDirection.x,
					0.01f,
					-1.0f,
					1.0f
				)) {
					waterChanged = true;
				}
				waterChanged |= ImGui::SliderFloat(
					"Light Intensity",
					&component.waterLightIntensity,
					0.0f,
					3.0f
				);
				waterChanged |= ImGui::SliderFloat(
					"Light Density",
					&component.waterLightDensity,
					0.0f,
					0.25f,
					"%.4f"
				);
				waterChanged |= ImGui::SliderFloat(
					"Caustics Intensity",
					&component.waterLightCausticsIntensity,
					0.0f,
					2.0f
				);
				waterChanged |= ImGui::SliderFloat(
					"Caustics Scale",
					&component.waterLightCausticsScale,
					0.02f,
					8.0f
				);
				waterChanged |= ImGui::SliderFloat(
					"Caustics Speed",
					&component.waterLightCausticsSpeed,
					0.0f,
					5.0f
				);
				waterChanged |= ImGui::SliderFloat(
					"Breakup Strength",
					&component.waterLightBreakupStrength,
					0.0f,
					3.0f
				);
				waterChanged |= ImGui::SliderFloat(
					"Warp Strength",
					&component.waterLightWarpStrength,
					0.0f,
					3.0f
				);
				waterChanged |= ImGui::SliderFloat(
					"Noise Scale",
					&component.waterLightNoiseScale,
					0.1f,
					4.0f
				);
				waterChanged |= ImGui::SliderInt(
					"Raymarch Samples",
					&component.waterLightSampleCount,
					4,
					32
				);
				ImGui::SeparatorText("Player Behavior");
				waterChanged |= ImGui::SliderFloat(
					"Move Speed Multiplier",
					&component.waterMoveSpeedMultiplier,
					0.0f,
					1.0f
				);
				waterChanged |= ImGui::DragFloat(
					"Gravity Scale",
					&component.waterGravityScale,
					0.02f,
					-5.0f,
					5.0f
				);
				waterChanged |= ImGui::DragFloat(
					"Drag",
					&component.waterDrag,
					0.05f,
					0.0f,
					100.0f
				);
				waterChanged |= ImGui::DragFloat(
					"Max Fall Speed",
					&component.waterMaxFallSpeed,
					0.1f,
					0.0f,
					100.0f
				);
				waterChanged |= ImGui::DragFloat(
					"Swim Up Speed",
					&component.waterSwimUpSpeed,
					0.1f,
					0.0f,
					100.0f
				);
				component.waterHalfSize.x =
					(std::max)(component.waterHalfSize.x, 0.1f);
				component.waterHalfSize.y =
					(std::max)(component.waterHalfSize.y, 0.1f);
				component.waterHalfSize.z =
					(std::max)(component.waterHalfSize.z, 0.1f);
				component.waterMoveSpeedMultiplier = std::clamp(
					component.waterMoveSpeedMultiplier,
					0.0f,
					1.0f
				);
				component.waterSurfaceAlpha = std::clamp(
					component.waterSurfaceAlpha,
					0.0f,
					1.0f
				);
				component.waterSurfaceWaveScale = std::clamp(
					component.waterSurfaceWaveScale,
					0.0f,
					3.0f
				);
				component.waterSurfaceNormalStrength = std::clamp(
					component.waterSurfaceNormalStrength,
					0.0f,
					2.0f
				);
				component.waterSurfaceFresnelPower = std::clamp(
					component.waterSurfaceFresnelPower,
					0.2f,
					8.0f
				);
				component.waterLightIntensity =
					(std::max)(component.waterLightIntensity, 0.0f);
				component.waterLightDensity =
					(std::max)(component.waterLightDensity, 0.0f);
				component.waterLightCausticsIntensity =
					(std::max)(component.waterLightCausticsIntensity, 0.0f);
				component.waterLightCausticsScale =
					(std::max)(component.waterLightCausticsScale, 0.001f);
				component.waterLightBreakupStrength = std::clamp(
					component.waterLightBreakupStrength,
					0.0f,
					3.0f
				);
				component.waterLightWarpStrength = std::clamp(
					component.waterLightWarpStrength,
					0.0f,
					3.0f
				);
				component.waterLightNoiseScale =
					(std::max)(component.waterLightNoiseScale, 0.001f);
				component.waterLightSampleCount = std::clamp(
					component.waterLightSampleCount,
					4,
					32
				);
				component.waterDrag = (std::max)(component.waterDrag, 0.0f);
				component.waterMaxFallSpeed =
					(std::max)(component.waterMaxFallSpeed, 0.0f);
				component.waterSwimUpSpeed =
					(std::max)(component.waterSwimUpSpeed, 0.0f);
				if (waterChanged) {
					document.MarkDirty();
				}
				ImGui::EndDisabled();
			}
			ImGui::PopID();
		}
		if (!removeComponentType.empty()) {
			document.RemoveComponent(entity->id, removeComponentType);
			editorSession_->RequestSceneReload();
		}

		ImGui::SeparatorText("Add Component");
		ImGui::BeginDisabled(entityLocked || !editorSession_->IsEditing());
		if (ImGui::BeginCombo("Component", "Select...")) {
			const char* availableComponents[] = {
				"MeshRenderer",
				"Environment",
				"SpriteRenderer",
				"Camera",
				"Light",
				"MonitorRenderer",
				"ThirdPersonCamera",
				"EntityReference",
				"SceneTransition",
				"CameraPath",
				"CameraPathPoint",
				"PhysicsBody",
				"PlayerBehavior",
				"AgentBehavior",
				"AgentAttractor",
				"WaterVolume",
				"Animator",
				"OBBCollider"
			};
			for (const char* componentType : availableComponents) {
				if (HasComponent(*entity, componentType)) {
					continue;
				}
				if (ImGui::Selectable(componentType)) {
					document.AddComponent(entity->id, componentType);
					editorSession_->RequestSceneReload();
				}
			}
			ImGui::EndCombo();
		}
		ImGui::EndDisabled();

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
	if (!ImGui::Begin("Project", &showProject_)) {
		ImGui::End();
		return;
	}
	const std::filesystem::path projectResourceRoot =
		GetProjectResourceRoot();
	const std::string projectResourceRootPath =
		PathToUtf8(projectResourceRoot);
	std::error_code projectRootError;
	if (
		selectedProjectFolder_.empty() ||
		!std::filesystem::exists(
			PathFromUtf8(selectedProjectFolder_),
			projectRootError
		)
	) {
		selectedProjectFolder_ = projectResourceRootPath;
		selectedProjectFile_.clear();
		projectDirectoryCacheDirty_ = true;
	}

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
	if (projectTreeCacheDirty_) {
		RefreshProjectTreeCache();
	}
	DrawDirectoryTreeNode(cachedProjectTreeRoot_);
	
	ImGui::NextColumn();

	// Right column: files inside selected folder
	const std::string displayFolder = PathToUtf8(
		EditableResourcePath::ToProjectRelative(
			PathFromUtf8(selectedProjectFolder_)
		)
	);
	ImGui::Text("Contents of: %s", displayFolder.c_str());
	ImGui::SameLine();
	ImGui::TextDisabled("View:");
	ImGui::SameLine();
	if (projectGridView_) {
		ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
	}
	if (ImGui::SmallButton("Grid")) {
		projectGridView_ = true;
	}
	if (projectGridView_) {
		ImGui::PopStyleColor();
	}
	ImGui::SameLine();
	if (!projectGridView_) {
		ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
	}
	if (ImGui::SmallButton("List")) {
		projectGridView_ = false;
	}
	if (!projectGridView_) {
		ImGui::PopStyleColor();
	}
	ImGui::SameLine();
	if (ImGui::SmallButton("Refresh")) {
		InvalidateProjectCache();
		projectPreviewLoadAttempted_.clear();
		TextureManager::GetInstance()->ClearFailedTextureCache();
		ModelManager::GetInstance()->ClearFailedModelCache();
	}
	if (projectGridView_) {
		ImGui::SameLine();
		ImGui::SetNextItemWidth(100.0f);
		ImGui::SliderFloat(
			"##ProjectThumbnailSize",
			&projectThumbnailSize_,
			56.0f,
			128.0f,
			"%.0f px"
		);
	}
	ImGui::Separator();

	std::error_code ec;
	const std::filesystem::path currentProjectFolder =
		PathFromUtf8(selectedProjectFolder_);
	if (std::filesystem::exists(currentProjectFolder, ec)) {
		if (
			currentProjectFolder.has_parent_path() &&
			currentProjectFolder != projectResourceRoot
		) {
			if (ImGui::SmallButton("..")) {
				const std::filesystem::path parent = currentProjectFolder.parent_path();
				selectedProjectFolder_ = parent.empty()
					? projectResourceRootPath
					: PathToUtf8(parent);
				selectedProjectFile_.clear();
				projectDirectoryCacheDirty_ = true;
			}
			ImGui::SameLine();
			ImGui::TextDisabled("Back to parent folder");
			ImGui::Separator();
		}

		const std::vector<ProjectDirectoryEntry>& entries =
			GetCachedProjectDirectoryEntries();

		const float cellWidth = projectThumbnailSize_ + 18.0f;
		const float panelWidth = ImGui::GetContentRegionAvail().x;
		const int columnCount = projectGridView_
			? (std::max)(1, static_cast<int>(panelWidth / cellWidth))
			: 1;
		bool tableOpen = false;
		if (projectGridView_) {
			tableOpen = ImGui::BeginTable(
				"ProjectAssetGrid",
				columnCount,
				ImGuiTableFlags_SizingFixedFit
			);
		}

		if (!projectGridView_ || tableOpen) {
		for (const auto& entry : entries) {
			const std::string& fileName = entry.fileName;
			const std::string& filePath = entry.filePath;
			const bool isDirectory = entry.isDirectory;
			const std::string& extension = entry.extension;
			const bool isTexture = entry.isTexture;
			const bool isModel = entry.isModel;
			const bool isScene = entry.isScene;
			const std::string resourcePath = isTexture
				? GetProjectResourcePath(filePath)
				: std::string{};
			const bool isSelected = selectedProjectFile_ == filePath;

			if (projectGridView_) {
				ImGui::TableNextColumn();
			}
			ImGui::PushID(filePath.c_str());
			if (isSelected || selectedProjectFolder_ == filePath) {
				ImGui::PushStyleColor(
					ImGuiCol_Button,
					ImGui::GetStyleColorVec4(ImGuiCol_Header)
				);
			}

			bool texturePreviewAvailable = false;
			float texturePreviewAspect = 1.0f;
			D3D12_GPU_DESCRIPTOR_HANDLE textureHandle{};
			if (isTexture && TextureManager::GetInstance()) {
				if (TextureManager::GetInstance()->HasTexture(resourcePath)) {
					const auto& metadata = TextureManager::GetInstance()->GetMetaData(
						resourcePath
					);
					texturePreviewAvailable = !metadata.IsCubemap();
					if (texturePreviewAvailable) {
						texturePreviewAspect = metadata.height > 0
							? static_cast<float>(metadata.width) /
								static_cast<float>(metadata.height)
							: 1.0f;
						textureHandle = TextureManager::GetInstance()->GetSrvHandleGPU(
							resourcePath
						);
					}
				}
			}

			bool clicked = false;
			bool openSceneRequested = false;
			auto loadHoveredTexturePreview = [&]() {
				if (
					isTexture &&
					TextureManager::GetInstance() &&
					!TextureManager::GetInstance()->HasTexture(resourcePath) &&
					projectPreviewLoadAttempted_.size() < 96 &&
					projectPreviewLoadAttempted_.insert(resourcePath).second
				) {
					TextureManager::GetInstance()->LoadTexture(resourcePath);
				}
			};
			auto drawDragSource = [&]() {
				if (!(isModel || isTexture) || !ImGui::BeginDragDropSource()) {
					return;
				}
				const std::string dragPath = isModel
					? GetModelPathRelativeToResources(filePath)
					: resourcePath;
				ImGui::SetDragDropPayload(
					isModel ? "PROJECT_MODEL_PATH" : "PROJECT_TEXTURE_PATH",
					dragPath.c_str(),
					dragPath.size() + 1
				);
				if (texturePreviewAvailable) {
					ImGui::Image(
						ImTextureRef(static_cast<ImTextureID>(textureHandle.ptr)),
						ImVec2(48.0f, 48.0f)
					);
				}
				ImGui::TextUnformatted(fileName.c_str());
				ImGui::EndDragDropSource();
			};
			if (projectGridView_) {
				if (texturePreviewAvailable) {
					const ImVec2 previewMin = ImGui::GetCursorScreenPos();
					clicked = ImGui::InvisibleButton(
						"##AssetPreview",
						ImVec2(projectThumbnailSize_, projectThumbnailSize_)
					);
					const ImVec2 previewMax = {
						previewMin.x + projectThumbnailSize_,
						previewMin.y + projectThumbnailSize_
					};
					ImDrawList* drawList = ImGui::GetWindowDrawList();
					drawList->AddRectFilled(
						previewMin,
						previewMax,
						ImGui::GetColorU32(
							isSelected ? ImGuiCol_Header : ImGuiCol_FrameBg
						),
						2.0f
					);
					const float innerSize = projectThumbnailSize_ - 8.0f;
					const float imageWidth = texturePreviewAspect >= 1.0f
						? innerSize
						: innerSize * texturePreviewAspect;
					const float imageHeight = texturePreviewAspect >= 1.0f
						? innerSize / texturePreviewAspect
						: innerSize;
					const ImVec2 imageMin = {
						previewMin.x + (projectThumbnailSize_ - imageWidth) * 0.5f,
						previewMin.y + (projectThumbnailSize_ - imageHeight) * 0.5f
					};
					drawList->AddImage(
						ImTextureRef(static_cast<ImTextureID>(textureHandle.ptr)),
						imageMin,
						ImVec2(imageMin.x + imageWidth, imageMin.y + imageHeight)
					);
					if (ImGui::IsItemHovered()) {
						drawList->AddRect(
							previewMin,
							previewMax,
							ImGui::GetColorU32(ImGuiCol_HeaderHovered),
							2.0f,
							0,
							2.0f
						);
					}
					drawDragSource();
				} else {
					const char* typeLabel = isDirectory ? "DIR" :
						isScene ? "SCENE" :
						isModel ? "3D" :
						isTexture ? "DDS" :
						extension == ".wav" ? "AUDIO" :
						extension == ".json" ? "JSON" :
						(extension == ".hlsl" || extension == ".hlsli") ? "SHADER" : "FILE";
					clicked = ImGui::Button(
						typeLabel,
						ImVec2(projectThumbnailSize_, projectThumbnailSize_)
					);
					openSceneRequested = isScene && ImGui::IsItemHovered() &&
						ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
					if (ImGui::IsItemHovered()) {
						loadHoveredTexturePreview();
					}
					drawDragSource();
				}
				ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + projectThumbnailSize_);
				ImGui::TextUnformatted(fileName.c_str());
				ImGui::PopTextWrapPos();
			} else {
				const char* prefix = isDirectory ? "[Folder]" :
					isScene ? "[Scene]" :
					isTexture ? "[Tex]" :
					isModel ? "[Model]" :
					extension == ".wav" ? "[Audio]" :
					extension == ".json" ? "[JSON]" :
					(extension == ".hlsl" || extension == ".hlsli") ? "[Shader]" : "[File]";
				const std::string label = std::string(prefix) + "  " + fileName;
				clicked = ImGui::Selectable(
					label.c_str(),
					isDirectory ? selectedProjectFolder_ == filePath : isSelected
				);
				openSceneRequested = isScene && ImGui::IsItemHovered() &&
					ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
				if (ImGui::IsItemHovered()) {
					loadHoveredTexturePreview();
				}
				drawDragSource();
			}

			if (isSelected || selectedProjectFolder_ == filePath) {
				ImGui::PopStyleColor();
			}
			if (clicked || openSceneRequested) {
				if (isDirectory) {
					selectedProjectFolder_ = filePath;
					selectedProjectFile_.clear();
					selectedEntityId_ = 0;
					projectDirectoryCacheDirty_ = true;
					if (previewSoundData_.pBuffer && Audio::GetInstance()) {
						Audio::GetInstance()->SoundUnload(&previewSoundData_);
					}
				} else {
					selectedProjectFile_ = filePath;
					selectedEntityId_ = 0;
					if (previewSoundData_.pBuffer && Audio::GetInstance()) {
						Audio::GetInstance()->SoundUnload(&previewSoundData_);
					}
					if (openSceneRequested && sceneCatalog_) {
						const SceneDescriptor* scene =
							sceneCatalog_->FindByFilePath(filePath);
						if (scene) {
							RequestOpenScene(scene->id);
						}
					}
				}
			}
			ImGui::PopID();
		}
		}
		if (tableOpen) {
			ImGui::EndTable();
		}
	}
	ImGui::Columns(1);
	ImGui::End();
}

void ImGuiManager::DrawDirectoryTreeNode(const ProjectDirectoryNode& node) {
	ImGui::PushID(node.folderPath.c_str());
	ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick;
	if (selectedProjectFolder_ == node.folderPath) {
		flags |= ImGuiTreeNodeFlags_Selected;
	}
	if (node.children.empty()) {
		flags |= ImGuiTreeNodeFlags_Leaf;
	}

	bool open = ImGui::TreeNodeEx(node.folderName.c_str(), flags);
	if (ImGui::IsItemClicked()) {
		selectedProjectFolder_ = node.folderPath;
		projectDirectoryCacheDirty_ = true;
	}

	if (open) {
		for (const ProjectDirectoryNode& child : node.children) {
			DrawDirectoryTreeNode(child);
		}
		ImGui::TreePop();
	}
	ImGui::PopID();
}

void ImGuiManager::DrawConsoleWindow() {
	SystemPerformanceMonitor& performanceMonitor =
		SystemPerformanceMonitor::GetInstance();
	performanceMonitor.Update();
	const SystemPerformanceMonitor::Snapshot& performance =
		performanceMonitor.GetSnapshot();
	const ParticleManager::RuntimeStats& particleStats =
		ParticleManager::GetInstance()->GetRuntimeStats();
	const GpuParticle::RuntimeInfo& gpuParticleInfo =
		particleStats.gpuParticle;
	const float fps = ImGui::GetIO().Framerate;
	const float frameMs = fps > 0.0f ? 1000.0f / fps : 0.0f;

	ImGui::Begin("Console", &showConsole_);
	ImGui::TextColored(
		ImVec4(0.45f, 0.8f, 0.55f, 1.0f),
		"Ready"
	);
	ImGui::SameLine();
	ImGui::TextDisabled("%.1f FPS / %.3f ms", fps, frameMs);

	ImGui::SeparatorText("System Load");
	if (performance.cpuSupported) {
		ImGui::Text(
			"CPU: %.1f%% system / %.1f%% process",
			performance.systemCpuUsage,
			performance.processCpuUsage
		);
		ImGui::TextDisabled(
			"Process CPU: %.1f%% of one logical core / %u logical cores",
			performance.processCpuOneCoreUsage,
			performance.logicalProcessorCount
		);
	} else {
		ImGui::TextDisabled("CPU counters are collecting...");
	}

	if (performance.gpuSupported) {
		ImGui::Text(
			"GPU Engine: %.1f%% system / %.1f%% process",
			performance.gpuUsage,
			performance.processGpuUsage
		);
		ImGui::Text(
			"GPU 3D: %.1f%% system / %.1f%% process",
			performance.gpu3DUsage,
			performance.processGpu3DUsage
		);
		ImGui::Text(
			"GPU Compute: %.1f%% system / %.1f%% process",
			performance.gpuComputeUsage,
			performance.processGpuComputeUsage
		);
		ImGui::Text(
			"GPU Copy: %.1f%% system / %.1f%% process",
			performance.gpuCopyUsage,
			performance.processGpuCopyUsage
		);
		if (performance.gpuRawEngineUsage > 100.0f ||
			performance.processGpuRawEngineUsage > 100.0f) {
			ImGui::TextDisabled(
				"Raw GPU engine sum: %.1f%% system / %.1f%% process",
				performance.gpuRawEngineUsage,
				performance.processGpuRawEngineUsage
			);
		}
		ImGui::TextDisabled(
			"GPU engines sampled: %u system / %u process",
			performance.gpuEngineSampleCount,
			performance.processGpuEngineSampleCount
		);
	} else {
		ImGui::TextDisabled("%s", performance.gpuStatus.c_str());
	}

	ImGui::SeparatorText("Particles");
	ImGui::Text(
		"Particle CPU Update: %.3f ms / Total: %.3f ms",
		particleStats.cpuParticleUpdateMs,
		particleStats.totalParticleUpdateMs
	);
	ImGui::Text(
		"CPU Particles: %u active / %u instanced",
		particleStats.cpuParticleActiveCount,
		particleStats.cpuParticleInstanceCount
	);
	ImGui::Text(
		"GpuParticle CPU Update: %.3f ms",
		particleStats.gpuParticleCpuUpdateMs
	);
	ImGui::Text(
		"GpuParticle: %s / %s / %u instances",
		particleStats.gpuParticleEnabled ? "enabled" : "disabled",
		gpuParticleInfo.initialized ? "initialized" : "not initialized",
		particleStats.gpuParticleInstanceCount
	);
	ImGui::Text(
		"GpuParticle Emit: %u / Max: %u / Flags: 0x%X",
		gpuParticleInfo.emitCount,
		gpuParticleInfo.maxParticles,
		gpuParticleInfo.emitFlags
	);
	ImGui::Text(
		"GpuParticle Frequency: %.3f / Timer: %.3f",
		gpuParticleInfo.frequency,
		gpuParticleInfo.frequencyTime
	);
	ImGui::End();
}

bool ImGuiManager::IsSceneViewInputActive() {
	return sceneViewInputActive_;
}
