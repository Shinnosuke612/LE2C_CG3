// 役割: ImGuiエディタ各ウィンドウの描画、入力、シーン編集操作を実装する。
#include "ImGuiManager.h"
#include "PostProcessSettingsEditor.h"

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
#include <unordered_set>
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
#include "../debug/DebugRenderer.h"
#include "../io/Input.h"
#include "../math/Matrix4x4.h"
#include "../math/Math.h"
#include "../particle/ParticleEffectResource.h"
#include "../particle/ParticleManager.h"
#include "../scene/EditorSession.h"
#include "../scene/PrefabAssetRegistry.h"
#include "../scene/PrefabEditorSession.h"
#include "../scene/SceneCatalog.h"
#include "../scene/SceneDocument.h"
#include "../scene/SceneEntityQuery.h"
#include "../scene/SceneInputKey.h"
#include "../scene/SceneManager.h"
#include "../scene/ScenePrefabAnimationEvaluator.h"
#include "../scene/SceneTemplateRegistry.h"
#include "../scene/SceneTransformResolver.h"
#include "../scene/SceneValidator.h"
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
	using SceneEntityQuery::IsEntityActiveInHierarchy;
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

	bool InputTextString(const char* label, std::string& value) {
		char buffer[256]{};
		CopyTextBuffer(buffer, sizeof(buffer), value);
		if (!ImGui::InputText(label, buffer, sizeof(buffer))) {
			return false;
		}
		value = buffer;
		return true;
	}

	bool InputTextMultilineString(const char* label, std::string& value) {
		char buffer[2048]{};
		CopyTextBuffer(buffer, sizeof(buffer), value);
		if (!ImGui::InputTextMultiline(
			label,
			buffer,
			sizeof(buffer),
			ImVec2(-1.0f, ImGui::GetTextLineHeight() * 5.0f)
		)) {
			return false;
		}
		value = buffer;
		return true;
	}

	std::string BuildEntityHierarchyLabel(
		const SceneDocument& document,
		const SceneEntity& entity
	) {
		std::vector<std::string> names;
		const SceneEntity* current = &entity;
		while (current) {
			names.push_back(current->name.empty() ? "Entity" : current->name);
			current = current->parentId != 0
				? document.FindEntity(current->parentId)
				: nullptr;
		}
		std::reverse(names.begin(), names.end());
		std::string result;
		for (const std::string& name : names) {
			if (!result.empty()) {
				result += " / ";
			}
			result += name;
		}
		return result;
	}

	std::vector<std::string> CollectEntityJointNames(
		const SceneEntity& entity
	) {
		const SceneComponent* meshRenderer =
			FindEnabledComponent(entity, "MeshRenderer");
		if (!meshRenderer || meshRenderer->modelPath.empty()) {
			return {};
		}
		ModelManager* modelManager = ModelManager::GetInstance();
		if (!modelManager) {
			return {};
		}
		modelManager->LoadModel(meshRenderer->modelPath);
		const Model* model = modelManager->FindModel(meshRenderer->modelPath);
		if (!model) {
			return {};
		}

		std::vector<std::string> jointNames;
		std::function<void(const Model::Node&)> collectNode;
		collectNode = [&](const Model::Node& node) {
			if (
				!node.name.empty() &&
				std::find(jointNames.begin(), jointNames.end(), node.name) ==
					jointNames.end()
			) {
				jointNames.push_back(node.name);
			}
			for (const Model::Node& child : node.children) {
				collectNode(child);
			}
		};
		collectNode(model->GetRootNode());
		return jointNames;
	}

	bool DrawJointNameCombo(
		const char* label,
		const std::vector<std::string>& jointNames,
		std::string& selectedJointName
	) {
		const std::string preview = selectedJointName.empty()
			? "Select Bone..."
			: selectedJointName;
		bool changed = false;
		if (ImGui::BeginCombo(label, preview.c_str())) {
			for (size_t index = 0; index < jointNames.size(); ++index) {
				const std::string& jointName = jointNames[index];
				ImGui::PushID(static_cast<int>(index));
				if (ImGui::Selectable(
					jointName.c_str(), selectedJointName == jointName
				)) {
					selectedJointName = jointName;
					changed = true;
				}
				ImGui::PopID();
			}
			ImGui::EndCombo();
		}
		return changed;
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

	bool IsPrefabAssetPath(const std::filesystem::path& path) {
		return PathToUtf8(path.filename()).ends_with(".prefab.json");
	}

	bool TryParsePrefabAssetReference(
		const json& value,
		PrefabAssetReference& reference
	) {
		if (value.is_string()) {
			reference = PrefabAssetRegistry::CreateReference(
				value.get<std::string>()
			);
		} else if (value.is_object()) {
			reference.assetId = value.value("assetId", std::string{});
			reference.fallbackPath = value.value(
				"fallbackPath",
				std::string{}
			);
			if (reference.assetId.empty()) {
				reference = PrefabAssetRegistry::CreateReference(
					reference.fallbackPath
				);
			}
		} else {
			return false;
		}

		const std::string resolvedPath =
			PrefabAssetRegistry::ResolvePath(reference);
		const std::string& validationPath = resolvedPath.empty()
			? reference.fallbackPath
			: resolvedPath;
		if (!IsPrefabAssetPath(PathFromUtf8(validationPath))) {
			return false;
		}
		if (!resolvedPath.empty()) {
			reference.fallbackPath = resolvedPath;
		}
		return true;
	}

	json PrefabAssetReferenceToJson(
		const PrefabAssetReference& source
	) {
		PrefabAssetReference reference = source;
		if (reference.assetId.empty()) {
			reference = PrefabAssetRegistry::CreateReference(
				reference.fallbackPath
			);
		}
		const std::string resolvedPath =
			PrefabAssetRegistry::ResolvePath(reference);
		if (!resolvedPath.empty()) {
			reference.fallbackPath = resolvedPath;
		}
		return {
			{ "assetId", reference.assetId },
			{ "fallbackPath", reference.fallbackPath }
		};
	}

	bool ContainsPrefabAssetReference(
		const std::vector<PrefabAssetReference>& references,
		const PrefabAssetReference& candidate
	) {
		return std::any_of(
			references.begin(),
			references.end(),
			[&candidate](const PrefabAssetReference& reference) {
				return PrefabAssetRegistry::IsSameAsset(
					reference,
					candidate
				);
			}
		);
	}

	bool ContainsCaseInsensitive(
		const std::string& text,
		const std::string& search
	) {
		if (search.empty()) {
			return true;
		}
		auto toLower = [](const std::string& value) {
			std::string result = value;
			std::transform(
				result.begin(),
				result.end(),
				result.begin(),
				[](unsigned char character) {
					return static_cast<char>(std::tolower(character));
				}
			);
			return result;
		};
		return toLower(text).find(toLower(search)) != std::string::npos;
	}

	std::filesystem::path GetProjectResourceRoot() {
		return EditableResourcePath::Resolve("resources");
	}

	constexpr char kEditorSettingsPath[] = "editor_settings.json";

	std::string MakeComponentFoldoutKey(
		const std::string& sceneId,
		uint64_t entityId,
		const std::string& componentType
	) {
		return sceneId + "/" + std::to_string(entityId) + "/" + componentType;
	}

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

	bool IntersectRaySphere(
		const Vector3& rayOrigin,
		const Vector3& rayDirection,
		const Vector3& center,
		float radius,
		float& outDistance
	) {
		const Vector3 originToCenter = Math::Subtract(rayOrigin, center);
		const float projected = Math::Dot(originToCenter, rayDirection);
		const float squaredDistance = Math::Dot(
			originToCenter,
			originToCenter
		);
		const float discriminant =
			projected * projected - (squaredDistance - radius * radius);
		if (discriminant < 0.0f) {
			return false;
		}
		outDistance = -projected - std::sqrt(discriminant);
		if (outDistance < 0.0f) {
			outDistance = -projected + std::sqrt(discriminant);
		}
		return outDistance >= 0.0f;
	}

	float GetMaxWorldAxisScale(const Matrix4x4& matrix) {
		return (std::max)(
			Math::Length({ matrix.m[0][0], matrix.m[0][1], matrix.m[0][2] }),
			(std::max)(
				Math::Length({ matrix.m[1][0], matrix.m[1][1], matrix.m[1][2] }),
				Math::Length({ matrix.m[2][0], matrix.m[2][1], matrix.m[2][2] })
			)
		);
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

bool ImGuiManager::LoadStartFullscreenSetting() {
	std::string text;
	if (!EditableResourcePath::ReadText(kEditorSettingsPath, text)) {
		return false;
	}

	try {
		const json settings = json::parse(text);
		return settings.contains("startFullscreen") &&
			settings["startFullscreen"].is_boolean() &&
			settings["startFullscreen"].get<bool>();
	} catch (const json::exception&) {
		return false;
	}
}

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
	prefabEditorSession_ = new PrefabEditorSession();
	prefabAnimationPreviewDocument_ = new SceneDocument();
	playerCombatPreviewDocument_ = new SceneDocument();
	prefabHitBoxGhostDocument_ = new SceneDocument();

	IMGUI_CHECKVERSION();
	ImGui::CreateContext();

	ImGuiIO& io = ImGui::GetIO();
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
	io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
	io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
	// 旧PrefabはEditor起動時に一度だけIDを付与し、アクセス履歴を即時安定化する。
	PrefabAssetRegistry::MigrateMissingAssetIds();
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
		if (
			settings.contains("startFullscreen") &&
			settings["startFullscreen"].is_boolean()
		) {
			startFullscreen_ = settings["startFullscreen"].get<bool>();
		}
		if (
			settings.contains("sceneGridVisible") &&
			settings["sceneGridVisible"].is_boolean()
		) {
			sceneGridVisible_ = settings["sceneGridVisible"].get<bool>();
		}
		if (
			settings.contains("prefabGridVisible") &&
			settings["prefabGridVisible"].is_boolean()
		) {
			prefabGridVisible_ = settings["prefabGridVisible"].get<bool>();
		}
		if (
			settings.contains("sceneAxisVisible") &&
			settings["sceneAxisVisible"].is_boolean()
		) {
			sceneAxisVisible_ = settings["sceneAxisVisible"].get<bool>();
		}
		if (
			settings.contains("prefabAxisVisible") &&
			settings["prefabAxisVisible"].is_boolean()
		) {
			prefabAxisVisible_ = settings["prefabAxisVisible"].get<bool>();
		}
		if (
			settings.contains("componentFoldouts") &&
			settings["componentFoldouts"].is_object()
		) {
			componentFoldoutStates_.clear();
			for (const auto& [key, value] : settings["componentFoldouts"].items()) {
				if (value.is_boolean()) {
					componentFoldoutStates_[key] = value.get<bool>();
				}
			}
		}
		if (
			settings.contains("recentPrefabs") &&
			settings["recentPrefabs"].is_array()
		) {
			recentPrefabReferences_.clear();
			for (const json& value : settings["recentPrefabs"]) {
				PrefabAssetReference reference{};
				if (!TryParsePrefabAssetReference(value, reference)) {
					continue;
				}
				if (!ContainsPrefabAssetReference(
					recentPrefabReferences_,
					reference
				)) {
					recentPrefabReferences_.push_back(std::move(reference));
				}
				if (recentPrefabReferences_.size() >= 12) {
					break;
				}
			}
		}
		if (
			settings.contains("favoritePrefabs") &&
			settings["favoritePrefabs"].is_array()
		) {
			favoritePrefabReferences_.clear();
			for (const json& value : settings["favoritePrefabs"]) {
				PrefabAssetReference reference{};
				if (!TryParsePrefabAssetReference(value, reference)) {
					continue;
				}
				if (!ContainsPrefabAssetReference(
					favoritePrefabReferences_,
					reference
				)) {
					favoritePrefabReferences_.push_back(std::move(reference));
				}
			}
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

	json componentFoldouts = json::object();
	for (const auto& [key, open] : componentFoldoutStates_) {
		componentFoldouts[key] = open;
	}
	json recentPrefabs = json::array();
	for (const PrefabAssetReference& reference : recentPrefabReferences_) {
		recentPrefabs.push_back(PrefabAssetReferenceToJson(reference));
	}
	std::vector<PrefabAssetReference> favoritePrefabReferences =
		favoritePrefabReferences_;
	std::sort(
		favoritePrefabReferences.begin(),
		favoritePrefabReferences.end(),
		[](const PrefabAssetReference& left, const PrefabAssetReference& right) {
			return PrefabAssetRegistry::ResolvePath(left) <
				PrefabAssetRegistry::ResolvePath(right);
		}
	);
	json favoritePrefabs = json::array();
	for (const PrefabAssetReference& reference : favoritePrefabReferences) {
		favoritePrefabs.push_back(PrefabAssetReferenceToJson(reference));
	}

	const json settings = {
		{ "fontPreset", preset },
		{ "fontSize", editorFontSize_ },
		{ "startFullscreen", startFullscreen_ },
		{ "sceneGridVisible", sceneGridVisible_ },
		{ "prefabGridVisible", prefabGridVisible_ },
		{ "sceneAxisVisible", sceneAxisVisible_ },
		{ "prefabAxisVisible", prefabAxisVisible_ },
		{ "componentFoldouts", std::move(componentFoldouts) },
		{ "recentPrefabs", std::move(recentPrefabs) },
		{ "favoritePrefabs", std::move(favoritePrefabs) }
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
	if (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
		// Platform WindowはBackend側のSwapChainとCommand Queueで描画する。
		// メインDraw Dataの後に更新し、Dock解除したWindowをアプリ外にも表示する。
		ImGui::UpdatePlatformWindows();
		ImGui::RenderPlatformWindowsDefault();
	}
}

void ImGuiManager::DrawEditorWorkspace(
	D3D12_GPU_DESCRIPTOR_HANDLE sceneTexture,
	uint32_t textureWidth,
	uint32_t textureHeight,
	const char* sceneName
) {
	prefabKeyboardFocusThisFrame_ = false;
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
	if (Camera* camera = Object3dCommon::GetInstance()
		? Object3dCommon::GetInstance()->GetDefaultCamera()
		: nullptr) {
		DrawSceneDebugLabels(
			sceneMin,
			sceneMax,
			camera->GetViewProjectionMatrix()
		);
	}
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
				document.AddComponent(entity.id, "SpriteRenderer");
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
				document.AddComponent(entity.id, "MeshRenderer");
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
		if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(
			"PROJECT_PREFAB_PATH"
		)) {
			const char* droppedPath = static_cast<const char*>(payload->Data);
			if (droppedPath && droppedPath[0] != '\0') {
				Vector3 dropPosition{};
				const Vector3* rootTranslate = nullptr;
				Camera* camera = Object3dCommon::GetInstance()
					? Object3dCommon::GetInstance()->GetDefaultCamera()
					: nullptr;
				if (camera) {
					const ImVec2 mouse = ImGui::GetMousePos();
					const float width = (std::max)(
						sceneMax.x - sceneMin.x,
						1.0f
					);
					const float height = (std::max)(
						sceneMax.y - sceneMin.y,
						1.0f
					);
					const float ndcX =
						((mouse.x - sceneMin.x) / width) * 2.0f - 1.0f;
					const float ndcY =
						1.0f - ((mouse.y - sceneMin.y) / height) * 2.0f;
					const Matrix4x4 inverseViewProjection = Inverse(
						Multiply(
							camera->GetViewMatrix(),
							camera->GetProjectionMatrix()
						)
					);
					const Vector3 nearPoint = TransformCoord(
						{ ndcX, ndcY, 0.0f },
						inverseViewProjection
					);
					const Vector3 farPoint = TransformCoord(
						{ ndcX, ndcY, 1.0f },
						inverseViewProjection
					);
					const Vector3 rayDirection = Math::Normalize(
						Math::Subtract(farPoint, nearPoint)
					);
					dropPosition = Math::Add(
						nearPoint,
						Math::Multiply(rayDirection, 5.0f)
					);
					rootTranslate = &dropPosition;
				}
				InstantiatePrefabInEditScene(
					droppedPath,
					0,
					rootTranslate
				);
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
	if (sceneAxisVisible_) {
		Camera* camera = Object3dCommon::GetInstance()
			? Object3dCommon::GetInstance()->GetDefaultCamera()
			: nullptr;
		if (camera) {
			DrawWorldAxisIndicator(
				sceneMin,
				sceneMax,
				camera->GetViewMatrix()
			);
		}
	}

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
			mouse.x <= sceneMin.x + 420.0f &&
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
	if (showPrefab_) {
		DrawPrefabWindow();
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
	HandleEditShortcuts();

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

void ImGuiManager::SetPrefabPreviewTexture(
	const std::string& assetPath,
	uint64_t revision,
	D3D12_GPU_DESCRIPTOR_HANDLE texture,
	uint32_t width,
	uint32_t height,
	const Matrix4x4& viewMatrix,
	const Matrix4x4& projectionMatrix
) {
	prefabPreviewRenderedPath_ = assetPath;
	prefabPreviewRenderedRevision_ = revision;
	prefabPreviewTexture_ = texture;
	prefabPreviewTextureWidth_ = (std::max)(width, 1u);
	prefabPreviewTextureHeight_ = (std::max)(height, 1u);
	prefabPreviewViewMatrix_ = viewMatrix;
	prefabPreviewProjectionMatrix_ = projectionMatrix;
	prefabPreviewCameraValid_ = texture.ptr != 0;
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

const std::vector<std::string>& ImGuiManager::GetCachedPrefabAssetPaths() {
	if (prefabAssetPathCacheDirty_) {
		RefreshPrefabAssetPathCache();
	}
	return cachedPrefabAssetPaths_;
}

void ImGuiManager::RefreshPrefabAssetPathCache() {
	cachedPrefabAssetPaths_.clear();
	std::error_code error;
	std::filesystem::recursive_directory_iterator iterator(
		GetProjectResourceRoot(),
		std::filesystem::directory_options::skip_permission_denied,
		error
	);
	const std::filesystem::recursive_directory_iterator end;
	while (!error && iterator != end) {
		if (
			iterator->is_regular_file(error) &&
			IsPrefabAssetPath(iterator->path())
		) {
			cachedPrefabAssetPaths_.push_back(
				PathToUtf8(iterator->path().lexically_normal())
			);
		}
		iterator.increment(error);
	}
	std::sort(
		cachedPrefabAssetPaths_.begin(),
		cachedPrefabAssetPaths_.end()
	);
	prefabAssetPathCacheDirty_ = false;
}

void ImGuiManager::RecordRecentPrefab(const std::string& filePath) {
	const PrefabAssetReference reference =
		PrefabAssetRegistry::CreateReference(filePath);
	recentPrefabReferences_.erase(
		std::remove_if(
			recentPrefabReferences_.begin(),
			recentPrefabReferences_.end(),
			[&reference](const PrefabAssetReference& recent) {
				return PrefabAssetRegistry::IsSameAsset(recent, reference);
			}
		),
		recentPrefabReferences_.end()
	);
	recentPrefabReferences_.insert(
		recentPrefabReferences_.begin(),
		reference
	);
	if (recentPrefabReferences_.size() > 12) {
		recentPrefabReferences_.resize(12);
	}
	SaveEditorSettings();
}

bool ImGuiManager::IsFavoritePrefab(const std::string& filePath) const {
	return ContainsPrefabAssetReference(
		favoritePrefabReferences_,
		PrefabAssetRegistry::CreateReference(filePath)
	);
}

void ImGuiManager::ToggleFavoritePrefab(const std::string& filePath) {
	ToggleFavoritePrefab(PrefabAssetRegistry::CreateReference(filePath));
}

void ImGuiManager::ToggleFavoritePrefab(
	const PrefabAssetReference& reference
) {
	const auto found = std::find_if(
		favoritePrefabReferences_.begin(),
		favoritePrefabReferences_.end(),
		[&reference](const PrefabAssetReference& favorite) {
			return PrefabAssetRegistry::IsSameAsset(favorite, reference);
		}
	);
	if (found != favoritePrefabReferences_.end()) {
		favoritePrefabReferences_.erase(found);
	} else {
		favoritePrefabReferences_.push_back(reference);
	}
	SaveEditorSettings();
}

void ImGuiManager::RefreshAssetPathCache() {
	cachedModelAssetPaths_ = CollectModelAssetPaths();
	cachedTextureAssetPaths_ = CollectTextureAssetPaths();
	assetPathCacheDirty_ = false;
}

void ImGuiManager::InvalidateProjectCache() {
	assetPathCacheDirty_ = true;
	prefabAssetPathCacheDirty_ = true;
	prefabAssetValidationCompleted_ = false;
	prefabAssetValidationScannedCount_ = 0;
	prefabAssetValidationResults_.clear();
	PrefabAssetRegistry::Invalidate();
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
	if (prefabEditorSession_ && prefabEditorSession_->IsOpen()) {
		const SceneDocument& prefab = prefabEditorSession_->GetDocument();
		if (const SceneEntity* entity = prefab.FindEntity(prefabSelectedEntityId_)) {
			if (const SceneComponent* meshRenderer =
				FindEnabledComponent(*entity, "MeshRenderer")) {
				if (!meshRenderer->modelPath.empty()) {
					modelPath = meshRenderer->modelPath;
					yaw = modelPreviewYaw_;
					pitch = modelPreviewPitch_;
					zoom = modelPreviewZoom_;
					return true;
				}
			}
		}
	}
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

const SceneDocument& ImGuiManager::GetPrefabStageDocument() const {
	const SceneDocument& sourceDocument = prefabEditorSession_->GetDocument();
	if (
		prefabAnimationPreviewActive_ &&
		prefabAnimationPreviewDocument_ &&
		prefabAnimationPreviewAssetPath_ == prefabEditorSession_->GetFilePath()
	) {
		return *prefabAnimationPreviewDocument_;
	}
	return sourceDocument;
}

void ImGuiManager::RebuildPrefabAnimationPreviewDocument() {
	if (
		!prefabAnimationPreviewActive_ ||
		!prefabAnimationPreviewDocument_ ||
		!prefabEditorSession_ ||
		!prefabEditorSession_->IsOpen()
	) {
		return;
	}

	const SceneDocument& sourceDocument = prefabEditorSession_->GetDocument();
	const SceneEntity* owner = sourceDocument.FindEntity(
		prefabAnimationPreviewOwnerEntityId_
	);
	const SceneComponent* animator = owner
		? FindEnabledComponent(*owner, "PrefabAnimator")
		: nullptr;
	if (
		!animator ||
		prefabAnimationPreviewClipIndex_ < 0 ||
		prefabAnimationPreviewClipIndex_ >=
			static_cast<int>(animator->prefabAnimationClips.size())
	) {
		prefabAnimationPreviewPlaying_ = false;
		prefabAnimationPreviewActive_ = false;
		return;
	}

	*prefabAnimationPreviewDocument_ = sourceDocument;
	ScenePrefabAnimationEvaluator::ApplyClip(
		*prefabAnimationPreviewDocument_,
		owner->id,
		animator->prefabAnimationClips[prefabAnimationPreviewClipIndex_],
		prefabAnimationPreviewTime_
	);
	if (prefabAttackPreviewMode_) {
		const SceneComponent* attackSet = FindEnabledComponent(*owner, "AttackSet");
		if (attackSet && prefabAttackPreviewIndex_ >= 0 &&
			prefabAttackPreviewIndex_ < static_cast<int>(attackSet->attackDefinitions.size())) {
			const SceneAttackDefinition& attack =
				attackSet->attackDefinitions[prefabAttackPreviewIndex_];
			std::unordered_set<uint64_t> windowHitBoxIds;
			std::unordered_set<uint64_t> activeHitBoxIds;
			for (const SceneAttackHitWindow& window : attack.hitWindows) {
				SceneEntity* hitBox = window.hitBoxEntityId != 0
					? prefabAnimationPreviewDocument_->FindEntity(window.hitBoxEntityId)
					: nullptr;
				if (!hitBox && !window.hitBoxEntityName.empty()) {
					hitBox = prefabAnimationPreviewDocument_->FindEntityByName(
						window.hitBoxEntityName
					);
				}
				if (!hitBox || !FindComponent(*hitBox, "HitBox")) {
					continue;
				}
				windowHitBoxIds.insert(hitBox->id);
				if (prefabAnimationPreviewTime_ >= window.startTime &&
					prefabAnimationPreviewTime_ < window.endTime) {
					activeHitBoxIds.insert(hitBox->id);
					if (window.payloadSource == "WindowLegacy" &&
						window.overrideHitBoxHalfSize) {
						if (SceneComponent* collider = FindComponent(*hitBox, "OBBCollider");
							collider && collider->enabled && collider->colliderShape == "Box") {
							Vector3 halfSize = window.hitBoxHalfSize;
							halfSize.x = (std::max)(halfSize.x, 0.001f);
							halfSize.y = (std::max)(halfSize.y, 0.001f);
							halfSize.z = (std::max)(halfSize.z, 0.001f);
							collider->colliderSizeMultiplier = halfSize;
						}
					}
				}
			}
			// Sourceを変更せず、Attackが参照するColliderだけをPreview Copyで
			// active/ghost表示へ分ける。選択Entityはこの表示で書き換えない。
			for (uint64_t hitBoxId : windowHitBoxIds) {
				if (SceneEntity* hitBox = prefabAnimationPreviewDocument_->FindEntity(hitBoxId)) {
					hitBox->active = activeHitBoxIds.contains(hitBoxId);
				}
			}
		}
	}
	prefabAnimationPreviewAssetPath_ = prefabEditorSession_->GetFilePath();
	prefabAnimationPreviewSourceRevision_ = sourceDocument.GetRevision();
}

void ImGuiManager::RebuildPlayerCombatPreviewDocument() {
	if (!playerCombatPreviewEnabled_ || !playerCombatPreviewDocument_ ||
		!editorSession_ || !editorSession_->IsEditing() ||
		!prefabEditorSession_ || !prefabEditorSession_->IsOpen()) {
		return;
	}

	const SceneDocument& source = editorSession_->GetEditDocument();
	const SceneEntity* root = source.FindEntity(playerCombatPreviewRootId_);
	const SceneEntity* weapon = source.FindEntity(playerCombatPreviewWeaponId_);
	if (!root || !weapon ||
		(weapon->id != root->id && !source.IsDescendantOf(weapon->id, root->id))) {
		playerCombatPreviewStatus_ = "Select a Player Root and its PlayerWeapon instance.";
		return;
	}
	const SceneComponent* animator = FindEnabledComponent(*weapon, "PrefabAnimator");
	const SceneComponent* attackSet = FindEnabledComponent(*weapon, "AttackSet");
	const SceneDocument& prefab = prefabEditorSession_->GetDocument();
	const SceneEntity* prefabOwner = prefab.FindEntity(
		prefabAnimationPreviewOwnerEntityId_
	);
	const SceneComponent* prefabAnimator = prefabOwner
		? FindEnabledComponent(*prefabOwner, "PrefabAnimator") : nullptr;
	if (!animator || !attackSet || !prefabAnimator ||
		prefabAnimationPreviewClipIndex_ < 0 ||
		prefabAnimationPreviewClipIndex_ >= static_cast<int>(
			prefabAnimator->prefabAnimationClips.size()
		)) {
		playerCombatPreviewStatus_ = "The selected Weapon needs PrefabAnimator, AttackSet, and a Clip.";
		return;
	}
	const std::string& clipName = prefabAnimator->prefabAnimationClips[
		prefabAnimationPreviewClipIndex_
	].name;
	auto sourceClip = std::find_if(
		animator->prefabAnimationClips.begin(),
		animator->prefabAnimationClips.end(),
		[&clipName](const ScenePrefabAnimationClip& candidate) {
			return candidate.name == clipName;
		}
	);
	auto attack = std::find_if(
		attackSet->attackDefinitions.begin(),
		attackSet->attackDefinitions.end(),
		[&clipName](const SceneAttackDefinition& candidate) {
			return candidate.animation == clipName;
		}
	);
	if (sourceClip == animator->prefabAnimationClips.end() ||
		attack == attackSet->attackDefinitions.end()) {
		playerCombatPreviewStatus_ = "The selected title Scene Weapon has no matching Clip or Attack.";
		return;
	}
	if (attack->facingMode != "FixedAtStart") {
		playerCombatPreviewStatus_ = "Combat Rig Preview currently supports Fixed At Start facing only.";
		return;
	}

	*playerCombatPreviewDocument_ = source;
	for (SceneEntity& entity : playerCombatPreviewDocument_->GetEntities()) {
		if (entity.id != root->id &&
			!playerCombatPreviewDocument_->IsDescendantOf(entity.id, root->id)) {
			entity.active = false;
		}
	}
	SceneEntity* previewRoot = playerCombatPreviewDocument_->FindEntity(root->id);
	ScenePrefabAnimationEvaluator::ApplyClip(
		*playerCombatPreviewDocument_, weapon->id, *sourceClip,
		prefabAnimationPreviewTime_
	);
	for (const SceneAttackHitWindow& window : attack->hitWindows) {
		SceneEntity* hitBox = window.hitBoxEntityId != 0
			? playerCombatPreviewDocument_->FindEntity(window.hitBoxEntityId)
			: nullptr;
		if (!hitBox && !window.hitBoxEntityName.empty()) {
			hitBox = playerCombatPreviewDocument_->FindEntityByName(
				window.hitBoxEntityName
			);
		}
		if (hitBox) {
			hitBox->active = prefabAnimationPreviewTime_ >= window.startTime &&
				prefabAnimationPreviewTime_ < window.endTime;
		}
	}
	const float activeDuration = (std::max)(attack->activeTime, 0.0001f);
	const float rawProgress = std::clamp(
		(prefabAnimationPreviewTime_ - attack->windup) / activeDuration,
		0.0f, 1.0f
	);
	float progress = Math::SmoothStep(rawProgress);
	if (attack->motionEasing == "Linear") progress = rawProgress;
	if (attack->motionEasing == "EaseIn") progress = rawProgress * rawProgress * rawProgress;
	if (attack->motionEasing == "EaseOut") progress = Math::EaseOutCubic(rawProgress);
	if (attack->motionEasing == "EaseInOut") {
		progress = rawProgress < 0.5f
			? 4.0f * rawProgress * rawProgress * rawProgress
			: 1.0f - std::pow(-2.0f * rawProgress + 2.0f, 3.0f) * 0.5f;
	}
	if (previewRoot) {
		previewRoot->transform.translate.x += attack->sideDistance * progress;
		previewRoot->transform.translate.z += attack->forwardDistance * progress;
	}
	playerCombatPreviewStatus_.clear();
}

void ImGuiManager::RebuildPrefabHitBoxGhostDocument() {
	if (
		!prefabHitBoxSetupMode_ ||
		!prefabHitBoxGhostVisible_ ||
		!prefabHitBoxGhostDocument_ ||
		!prefabEditorSession_ ||
		!prefabEditorSession_->IsOpen()
	) {
		return;
	}

	const SceneDocument& sourceDocument = prefabEditorSession_->GetDocument();
	const SceneEntity* owner = sourceDocument.FindEntity(
		prefabAnimationPreviewOwnerEntityId_
	);
	const SceneComponent* animator = owner
		? FindEnabledComponent(*owner, "PrefabAnimator")
		: nullptr;
	if (
		!animator ||
		prefabAnimationPreviewClipIndex_ < 0 ||
		prefabAnimationPreviewClipIndex_ >=
			static_cast<int>(animator->prefabAnimationClips.size())
	) {
		return;
	}

	*prefabHitBoxGhostDocument_ = sourceDocument;
	ScenePrefabAnimationEvaluator::ApplyClip(
		*prefabHitBoxGhostDocument_,
		owner->id,
		animator->prefabAnimationClips[prefabAnimationPreviewClipIndex_],
		prefabHitBoxGhostTime_
	);
}

bool ImGuiManager::GetPrefabPreviewRequest(PrefabPreviewRequest& request) {
	if (
		!showPrefab_ ||
		!prefabEditorSession_ ||
		!prefabEditorSession_->IsOpen()
	) {
		return false;
	}
	// InspectorはTimelineより後にSource Documentを変更する。ここで最後に
	// Snapshotを作り直し、編集FrameだけAuthoring Poseへ戻る表示を防ぐ。
	RebuildPrefabAnimationPreviewDocument();
	RebuildPrefabHitBoxGhostDocument();
	RebuildPlayerCombatPreviewDocument();

	const SceneDocument& sourceDocument = prefabEditorSession_->GetDocument();
	const bool useCombatRigPreview =
		playerCombatPreviewEnabled_ &&
		playerCombatPreviewDocument_ &&
		editorSession_ &&
		editorSession_->IsEditing() &&
		!prefabHitBoxSetupMode_ &&
		playerCombatPreviewStatus_.empty();
	request.document = useCombatRigPreview
		? playerCombatPreviewDocument_
		: &GetPrefabStageDocument();
	request.ghostDocument = (!useCombatRigPreview &&
		prefabHitBoxSetupMode_ &&
		prefabHitBoxGhostVisible_ &&
		prefabHitBoxGhostDocument_
	) ? prefabHitBoxGhostDocument_ : nullptr;
	request.assetPath = useCombatRigPreview
		? editorSession_->GetSceneFilePath() + "#CombatRig"
		: prefabEditorSession_->GetFilePath();
	request.revision = useCombatRigPreview
		? editorSession_->GetEditDocument().GetRevision()
		: sourceDocument.GetRevision();
	request.yaw = prefabPreviewYaw_;
	request.pitch = prefabPreviewPitch_;
	request.zoom = prefabPreviewZoom_;
	request.width = prefabPreviewRequestedWidth_;
	request.height = prefabPreviewRequestedHeight_;
	request.showSkeleton = prefabPreviewShowSkeleton_;
	request.showJointAxes = prefabPreviewShowJointAxes_;
	request.showColliders = prefabPreviewShowColliders_;
	request.showCombatVolumes = prefabPreviewShowCombatVolumes_;
	request.selectedEntityId = useCombatRigPreview
		? playerCombatPreviewWeaponId_
		: prefabSelectedEntityId_;
	request.isolateSelectedCollider = !useCombatRigPreview && prefabHitBoxSetupMode_;
	request.showGrid = prefabGridVisible_;
	request.framingSerial = prefabPreviewFramingSerial_;
	// Keep the exact final request identity. The renderer returns this one on a
	// later frame, including the composed Combat Rig's source scene identity.
	prefabPreviewRequestedPath_ = request.assetPath;
	prefabPreviewRequestedRevision_ = request.revision;
	prefabPreviewRequestUsesCombatRig_ = useCombatRigPreview;
	return true;
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
	ImGui::SameLine();
	if (ImGui::Checkbox("Grid", &sceneGridVisible_)) {
		SaveEditorSettings();
	}
	ImGui::SameLine();
	if (ImGui::Checkbox("Axis", &sceneAxisVisible_)) {
		SaveEditorSettings();
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
	delete prefabEditorSession_;
	prefabEditorSession_ = nullptr;
	delete prefabAnimationPreviewDocument_;
	prefabAnimationPreviewDocument_ = nullptr;
	delete playerCombatPreviewDocument_;
	playerCombatPreviewDocument_ = nullptr;
	delete prefabHitBoxGhostDocument_;
	prefabHitBoxGhostDocument_ = nullptr;
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
		ImGui::MenuItem("Prefab", nullptr, &showPrefab_);
		bool prefabInspectorVisible = showPrefabInspector_;
		if (ImGui::MenuItem(
			"Prefab Inspector",
			nullptr,
			&prefabInspectorVisible
		)) {
			showPrefabInspector_ = prefabInspectorVisible;
			if (showPrefabInspector_) {
				showPrefab_ = true;
				prefabInspectorFocusRequested_ = true;
			}
		}
		if (ImGui::MenuItem("Prefab Quick Open", "Ctrl+Shift+P")) {
			RequestPrefabQuickOpen();
		}
		ImGui::MenuItem("Console", nullptr, &showConsole_);
		ImGui::MenuItem("Loaded Scenes", nullptr, &showLoadedScenes_);
		ImGui::Separator();
		if (ImGui::MenuItem("Reset Layout")) {
			resetLayout_ = true;
		}
		ImGui::EndMenu();
	}
	if (ImGui::BeginMenu("Application")) {
		if (ImGui::MenuItem(
			"Start in Fullscreen",
			nullptr,
			startFullscreen_
		)) {
			startFullscreen_ = !startFullscreen_;
			SaveEditorSettings();
		}
		ImGui::TextDisabled("Applied on next launch. F11 toggles now.");
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
		if (ImGui::BeginMenu("Prefab")) {
			if (ImGui::MenuItem("Show Prefab Window")) {
				showPrefab_ = true;
				prefabFocusFramesRemaining_ = 2;
			}
			if (ImGui::MenuItem("Show Prefab Inspector")) {
				showPrefab_ = true;
				showPrefabInspector_ = true;
				prefabInspectorFocusRequested_ = true;
			}
			if (ImGui::MenuItem("Quick Open...", "Ctrl+Shift+P")) {
				RequestPrefabQuickOpen();
			}
			ImGui::EndMenu();
		}
		DrawSettingsMenu();
		DrawPlaybackControls();
		ImGui::EndMenuBar();
	}
	const ImGuiIO& dockSpaceIo = ImGui::GetIO();
	if (
		dockSpaceIo.KeyCtrl &&
		dockSpaceIo.KeyShift &&
		ImGui::IsKeyPressed(ImGuiKey_P, false)
	) {
		RequestPrefabQuickOpen();
	}
	if (prefabQuickOpenPopupRequested_) {
		prefabQuickOpenSearchBuffer_[0] = '\0';
		prefabQuickOpenFocusRequested_ = true;
		ImGui::OpenPopup("Quick Open Prefab");
		prefabQuickOpenPopupRequested_ = false;
	}
	DrawPrefabQuickOpenPopup();
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
	if (editorSession_->IsEditing() && saveRequested) {
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
	if (editorSession_->IsEditing() && undoRequested) {
		editorSession_->Undo();
	}
	if (editorSession_->IsEditing() && redoRequested) {
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
	ImGui::DockBuilderDockWindow("Prefab", centerId);
	ImGui::DockBuilderDockWindow("Hierarchy", leftId);
	ImGui::DockBuilderDockWindow("Inspector", rightId);
	ImGui::DockBuilderDockWindow("Prefab Inspector", rightId);
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
		std::string hierarchyPrefabDropPath;
		uint64_t hierarchyPrefabDropParentId = 0;
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
			if (editable && ImGui::BeginDragDropTarget()) {
				if (const ImGuiPayload* payload =
					ImGui::AcceptDragDropPayload("PROJECT_PREFAB_PATH")) {
					const char* droppedPath =
						static_cast<const char*>(payload->Data);
					if (droppedPath && droppedPath[0] != '\0') {
						hierarchyPrefabDropPath = droppedPath;
						hierarchyPrefabDropParentId = entity->id;
					}
				}
				ImGui::EndDragDropTarget();
			}
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
			if (treeItemDoubleClicked && !renaming) {
				const uint64_t prefabRootId =
					document.FindPrefabInstanceRoot(entity->id);
				const SceneEntity* prefabRoot = prefabRootId != 0
					? document.FindEntity(prefabRootId)
					: nullptr;
				const std::string prefabAssetPath = prefabRoot
					? PrefabAssetRegistry::ResolvePath(
						prefabRoot->prefabAssetId,
						prefabRoot->prefabSourcePath
					)
					: std::string{};
				if (!prefabAssetPath.empty()) {
					RequestOpenPrefab(prefabAssetPath);
				} else if (editable) {
					hierarchyRenameEntityId_ = entity->id;
					strncpy_s(
						hierarchyRenameBuffer_,
						entity->name.c_str(),
						_TRUNCATE
					);
					hierarchyRenameFocusRequested_ = true;
				}
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
		if (editorSession_->IsEditing() && ImGui::BeginDragDropTarget()) {
			if (const ImGuiPayload* payload =
				ImGui::AcceptDragDropPayload("PROJECT_PREFAB_PATH")) {
				const char* droppedPath =
					static_cast<const char*>(payload->Data);
				if (droppedPath && droppedPath[0] != '\0') {
					hierarchyPrefabDropPath = droppedPath;
					hierarchyPrefabDropParentId = 0;
				}
			}
			ImGui::EndDragDropTarget();
		}
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
		if (!hierarchyPrefabDropPath.empty()) {
			InstantiatePrefabInEditScene(
				hierarchyPrefabDropPath,
				hierarchyPrefabDropParentId
			);
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
				document.AddComponent(entity.id, "SpriteRenderer");
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
				document.AddComponent(entity.id, "MeshRenderer");
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
			if (fileName.ends_with(".prefab.json")) {
				const PrefabAssetReference variantBase =
					PrefabAssetRegistry::ReadVariantBase(selectedProjectFile_);
				ImGui::Text(
					"Type: %s",
					variantBase.assetId.empty()
						? "Entity Prefab"
						: "Prefab Variant"
				);
				if (!variantBase.assetId.empty()) {
					const std::string basePath =
						PrefabAssetRegistry::ResolvePath(variantBase);
					ImGui::TextWrapped(
						"Base: %s",
						basePath.empty()
							? "Missing or ambiguous"
							: basePath.c_str()
					);
				}
				SceneDocument prefabPreview;
				if (prefabPreview.Load(selectedProjectFile_)) {
					ImGui::Text(
						"Entities: %zu",
						prefabPreview.GetEntities().size()
					);
				} else {
					ImGui::TextWrapped(
						"Load error: %s",
						prefabPreview.GetLastLoadError().c_str()
					);
				}
				if (ImGui::Button("Open Prefab Editor")) {
					RequestOpenPrefab(selectedProjectFile_);
				}
				ImGui::BeginDisabled(
					!editorSession_ || !editorSession_->IsEditing()
				);
				static uint64_t prefabParentEntityId = 0;
				SceneDocument* editDocument = editorSession_ && editorSession_->IsEditing()
					? &editorSession_->GetEditDocument()
					: nullptr;
				const SceneEntity* prefabParent = editDocument
					? editDocument->FindEntity(prefabParentEntityId)
					: nullptr;
				if (!prefabParent) {
					prefabParentEntityId = 0;
				}
				if (ImGui::BeginCombo(
					"Instance Parent",
					prefabParent ? prefabParent->name.c_str() : "Scene Root"
				)) {
					if (ImGui::Selectable("Scene Root", prefabParentEntityId == 0)) {
						prefabParentEntityId = 0;
					}
					if (editDocument) {
						for (const SceneEntity& candidate : editDocument->GetEntities()) {
							if (ImGui::Selectable(
								candidate.name.c_str(),
								prefabParentEntityId == candidate.id
							)) {
								prefabParentEntityId = candidate.id;
							}
						}
					}
					ImGui::EndCombo();
				}
				if (ImGui::Button("Instantiate Prefab")) {
					InstantiatePrefabInEditScene(
						selectedProjectFile_,
						prefabParentEntityId
					);
				}
				ImGui::EndDisabled();
			} else {
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

		ImGui::SeparatorText("Prefab");
		const uint64_t prefabInstanceRootId =
			document.FindPrefabInstanceRoot(entity->id);
		if (prefabInstanceRootId != 0) {
			const SceneEntity* prefabRoot =
				document.FindEntity(prefabInstanceRootId);
			const std::string linkedPrefabAssetPath = prefabRoot
				? PrefabAssetRegistry::ResolvePath(
					prefabRoot->prefabAssetId,
					prefabRoot->prefabSourcePath
				)
				: std::string{};
			bool prefabEditConflict = false;
			if (
				prefabRoot &&
				!linkedPrefabAssetPath.empty() &&
				prefabEditorSession_ &&
				prefabEditorSession_->IsOpen() &&
				prefabEditorSession_->IsDirty()
			) {
				const std::filesystem::path openPrefabPath =
					EditableResourcePath::ResolveResource(
						PathFromUtf8(prefabEditorSession_->GetFilePath())
					);
				const std::filesystem::path linkedPrefabPath =
					EditableResourcePath::ResolveResource(
						PathFromUtf8(linkedPrefabAssetPath)
					);
				std::error_code equivalentError;
				prefabEditConflict = std::filesystem::equivalent(
					openPrefabPath,
					linkedPrefabPath,
					equivalentError
				);
				if (equivalentError) {
					prefabEditConflict =
						openPrefabPath.lexically_normal() ==
						linkedPrefabPath.lexically_normal();
				}
			}
			bool applyPrefabRequested = false;
			bool revertPrefabRequested = false;
			bool unpackPrefabRequested = false;
			ImGui::TextWrapped(
				"Linked Asset: %s",
				!linkedPrefabAssetPath.empty()
					? linkedPrefabAssetPath.c_str()
					: "Missing or ambiguous"
			);
			if (prefabRoot && !prefabRoot->prefabAssetId.empty()) {
				ImGui::TextDisabled(
					"Asset ID: %s",
					prefabRoot->prefabAssetId.c_str()
				);
			}
			if (ImGui::BeginPopupContextItem("PrefabInstanceContext")) {
				const bool hasLinkedAsset = !linkedPrefabAssetPath.empty();
				if (ImGui::MenuItem(
					"Open Prefab",
					nullptr,
					false,
					hasLinkedAsset
				)) {
					RequestOpenPrefab(linkedPrefabAssetPath);
				}
				if (ImGui::MenuItem(
					"Select Asset",
					nullptr,
					false,
					hasLinkedAsset
				)) {
					SelectPrefabAssetInProject(linkedPrefabAssetPath);
				}
				ImGui::Separator();
				const bool canModifyInstance =
					!entityLocked &&
					editorSession_->IsEditing() &&
					!prefabEditConflict;
				if (ImGui::MenuItem(
					"Apply Instance To Prefab",
					nullptr,
					false,
					canModifyInstance
				)) {
					applyPrefabRequested = true;
				}
				if (ImGui::MenuItem(
					"Revert Instance",
					nullptr,
					false,
					canModifyInstance
				)) {
					revertPrefabRequested = true;
				}
				if (ImGui::MenuItem(
					"Unpack",
					nullptr,
					false,
					canModifyInstance
				)) {
					unpackPrefabRequested = true;
				}
				ImGui::EndPopup();
			}
			ImGui::TextDisabled(
				"Instance Root: %llu / Local Entity: %llu",
				static_cast<unsigned long long>(prefabInstanceRootId),
				static_cast<unsigned long long>(entity->prefabLocalId)
			);
			ImGui::BeginDisabled(linkedPrefabAssetPath.empty());
			if (ImGui::Button("Open Prefab")) {
				RequestOpenPrefab(linkedPrefabAssetPath);
			}
			ImGui::SameLine();
			if (ImGui::Button("Select Asset")) {
				SelectPrefabAssetInProject(linkedPrefabAssetPath);
			}
			ImGui::EndDisabled();
			static std::string prefabInstanceStatus;
			std::vector<ScenePrefabPropertyOverride> propertyOverrides;
			int applyPropertyOverrideIndex = -1;
			int revertPropertyOverrideIndex = -1;
			if (ImGui::TreeNode("Overrides")) {
				const std::vector<std::string> entityOverrides =
					document.CollectPrefabInstanceOverrides(prefabInstanceRootId);
				propertyOverrides = document.CollectPrefabPropertyOverrides(
					prefabInstanceRootId
				);
				bool hasLegacyOverrideStatus = false;
				for (const std::string& overrideLabel : entityOverrides) {
					if (
						overrideLabel.starts_with("Modified Entity:") ||
						overrideLabel.starts_with("Added Entity:") ||
						overrideLabel.starts_with("Removed Entity:") ||
						overrideLabel.starts_with("Stale Entity:")
					) {
						continue;
					}
					hasLegacyOverrideStatus = true;
					ImGui::BulletText("%s", overrideLabel.c_str());
				}
				if (!propertyOverrides.empty()) {
					ImGui::SeparatorText("Individual Overrides");
				}
				const bool canModifyProperty =
					!entityLocked &&
					editorSession_->IsEditing() &&
					!prefabEditConflict;
				for (size_t index = 0; index < propertyOverrides.size(); ++index) {
					const ScenePrefabPropertyOverride& overrideValue =
						propertyOverrides[index];
					ImGui::PushID(static_cast<int>(index));
					ImGui::BulletText("%s", overrideValue.label.c_str());
					ImGui::Indent();
					ImGui::BeginDisabled(!canModifyProperty);
					if (ImGui::SmallButton("Apply")) {
						applyPropertyOverrideIndex = static_cast<int>(index);
					}
					ImGui::SameLine();
					if (ImGui::SmallButton("Revert")) {
						revertPropertyOverrideIndex = static_cast<int>(index);
					}
					ImGui::EndDisabled();
					ImGui::Unindent();
					ImGui::PopID();
				}
				if (propertyOverrides.empty() && !hasLegacyOverrideStatus) {
					ImGui::TextDisabled("No overrides.");
				}
				ImGui::TreePop();
			}
			if (applyPropertyOverrideIndex >= 0) {
				const ScenePrefabPropertyOverride& overrideValue =
					propertyOverrides[applyPropertyOverrideIndex];
				if (document.ApplyPrefabPropertyOverride(
					prefabInstanceRootId,
					overrideValue
				)) {
					prefabInstanceStatus = "Applied: " + overrideValue.label;
					InvalidateProjectCache();
				} else {
					prefabInstanceStatus = "Failed to apply: " + overrideValue.label;
				}
			}
			if (revertPropertyOverrideIndex >= 0) {
				const ScenePrefabPropertyOverride& overrideValue =
					propertyOverrides[revertPropertyOverrideIndex];
				const uint64_t selectedId = entity->id;
				const bool removesSelectedBranch =
					(
						overrideValue.kind == ScenePrefabOverrideKind::AddedEntity ||
						overrideValue.kind == ScenePrefabOverrideKind::StaleEntity
					) &&
					(
						selectedId == overrideValue.instanceEntityId ||
						document.IsDescendantOf(
							selectedId,
							overrideValue.instanceEntityId
						)
					);
				if (document.RevertPrefabPropertyOverride(
					prefabInstanceRootId,
					overrideValue
				)) {
					prefabInstanceStatus = "Reverted: " + overrideValue.label;
					selectedEntityId_ = removesSelectedBranch
						? prefabInstanceRootId
						: selectedId;
					selectedEntityIds_ = { selectedEntityId_ };
					editorSession_->RequestSceneReload();
					ImGui::End();
					return;
				}
				prefabInstanceStatus = "Failed to revert: " + overrideValue.label;
			}
			if (prefabEditConflict) {
				ImGui::TextColored(
					ImVec4(0.95f, 0.65f, 0.25f, 1.0f),
					"Save or close the open Prefab before changing this instance."
				);
			}
			ImGui::BeginDisabled(
				entityLocked ||
				!editorSession_->IsEditing() ||
				prefabEditConflict
			);
			if (
				ImGui::Button("Apply Instance To Prefab") ||
				applyPrefabRequested
			) {
				if (document.ApplyPrefabInstance(prefabInstanceRootId)) {
					prefabInstanceStatus = "Applied instance values to the Prefab asset.";
					InvalidateProjectCache();
				} else {
					prefabInstanceStatus = "Failed to apply the Prefab instance.";
				}
			}
			ImGui::SameLine();
			if (
				ImGui::Button("Revert Instance") ||
				revertPrefabRequested
			) {
				if (document.RevertPrefabInstance(prefabInstanceRootId)) {
					prefabInstanceStatus = "Reverted the instance from its Prefab asset.";
					selectedEntityId_ = prefabInstanceRootId;
					selectedEntityIds_ = { prefabInstanceRootId };
					editorSession_->RequestSceneReload();
					ImGui::EndDisabled();
					ImGui::End();
					return;
				}
				prefabInstanceStatus = "Failed to revert the Prefab instance.";
			}
			ImGui::SameLine();
			if (ImGui::Button("Unpack") || unpackPrefabRequested) {
				if (document.UnpackPrefabInstance(prefabInstanceRootId)) {
					prefabInstanceStatus = "Unpacked the Prefab instance.";
				} else {
					prefabInstanceStatus = "Failed to unpack the Prefab instance.";
				}
			}
			ImGui::EndDisabled();
			if (!prefabInstanceStatus.empty()) {
				ImGui::TextWrapped("%s", prefabInstanceStatus.c_str());
			}
			ImGui::SeparatorText("Create Prefab Asset");
		}
		ImGui::BeginDisabled(entityLocked || !editorSession_->IsEditing());
		static std::string prefabOperationStatus;
		static uint64_t prefabFileNameEntityId = 0;
		static char prefabFileName[192]{};
		if (prefabFileNameEntityId != entity->id) {
			prefabFileNameEntityId = entity->id;
			std::string defaultName = entity->name.empty() ? "Prefab" : entity->name;
			CopyTextBuffer(prefabFileName, sizeof(prefabFileName), defaultName);
		}
		ImGui::SetNextItemWidth(-1.0f);
		ImGui::InputText(
			"Prefab File Name",
			prefabFileName,
			sizeof(prefabFileName)
		);
		if (ImGui::Button("Save / Overwrite Prefab")) {
			std::string prefabName = prefabFileName;
			for (char& character : prefabName) {
				if (
					static_cast<unsigned char>(character) < 32 ||
					std::strchr("<>:\"/\\|?*", character)
				) {
					character = '_';
				}
			}
			if (prefabName.empty()) {
				prefabName = "Prefab";
			}
			if (!prefabName.ends_with(".prefab.json")) {
				prefabName += ".prefab.json";
			}
			const std::filesystem::path prefabDirectory =
				GetProjectResourceRoot() / "prefabs";
			const std::filesystem::path prefabFilePath =
				prefabDirectory / PathFromUtf8(prefabName);
			const std::string prefabPath = PathToUtf8(prefabFilePath);
			std::error_code prefabDirectoryError;
			std::filesystem::create_directories(
				prefabDirectory,
				prefabDirectoryError
			);
			if (!prefabDirectoryError) {
				const bool prefabSaved = document.SaveEntityBranchAsPrefab(
					entity->id,
					prefabPath
				);
				if (prefabSaved) {
					prefabOperationStatus = "Saved: resources/prefabs/" +
						prefabName;
					selectedProjectFolder_ = PathToUtf8(prefabDirectory);
					selectedProjectFile_ = prefabPath;
					InvalidateProjectCache();
				} else {
					prefabOperationStatus = "Failed to save: " + prefabPath;
				}
			} else {
				prefabOperationStatus = "Failed to create: " +
					PathToUtf8(prefabDirectory);
			}
		}
		static char prefabInstantiatePath[256] =
			"resources/prefabs/Prefab.prefab.json";
		ImGui::SetNextItemWidth(-1.0f);
		ImGui::InputText(
			"Prefab Path",
			prefabInstantiatePath,
			sizeof(prefabInstantiatePath)
		);
		if (ImGui::Button("Instantiate As Child")) {
			const uint64_t instanceId = document.InstantiatePrefab(
				prefabInstantiatePath,
				entity->id
			);
			if (instanceId != 0) {
				prefabOperationStatus = "Instantiated: " +
					std::string(prefabInstantiatePath);
				selectedEntityId_ = instanceId;
				editorSession_->RequestSceneReload();
				ImGui::EndDisabled();
				ImGui::End();
				return;
			}
			prefabOperationStatus = "Failed to instantiate: " +
				std::string(prefabInstantiatePath);
		}
		if (!prefabOperationStatus.empty()) {
			ImGui::TextWrapped("%s", prefabOperationStatus.c_str());
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
		} else if (HasComponent(*entity, "TextRenderer")) {
			ImGui::TextDisabled("TextRenderer placement is edited in its active Render Space profile.");
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
			const char* componentLabel = component.type == "OBBCollider"
				? "Collider"
				: component.type.c_str();
			const std::string componentHeaderLabel =
				std::string(componentLabel) + "##ComponentHeader";
			const std::string foldoutKey = MakeComponentFoldoutKey(
				editorSession_->GetActiveSceneId(),
				entity->id,
				component.type
			);
			const auto savedFoldout = componentFoldoutStates_.find(foldoutKey);
			const bool wasComponentOpen = savedFoldout == componentFoldoutStates_.end()
				? true
				: savedFoldout->second;
			ImGui::SetNextItemOpen(wasComponentOpen, ImGuiCond_Always);
			const bool componentOpen = ImGui::CollapsingHeader(
				componentHeaderLabel.c_str(),
				ImGuiTreeNodeFlags_SpanAvailWidth
			);
			if (componentOpen != wasComponentOpen) {
				componentFoldoutStates_[foldoutKey] = componentOpen;
				SaveEditorSettings();
			}
			if (!componentOpen) {
				ImGui::PopID();
				continue;
			}
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
			} else if (component.type == "TextRenderer") {
				ImGui::BeginDisabled(!editorSession_->IsEditing() || entityLocked);
				bool textChanged = false;
				textChanged |= InputTextMultilineString("Text", component.textValue);
				textChanged |= InputTextString("Font Family", component.textFontFamily);
				textChanged |= ImGui::DragFloat(
					"Font Size", &component.textFontSize, 1.0f, 1.0f, 512.0f
				);
				const char* renderSpaces[] = { "ScreenOverlay", "Scene2D" };
				int renderSpaceIndex = component.textRenderSpace == "Scene2D" ? 1 : 0;
				if (ImGui::Combo(
					"Render Space", &renderSpaceIndex, renderSpaces, IM_ARRAYSIZE(renderSpaces)
				)) {
					component.textRenderSpace = renderSpaces[renderSpaceIndex];
					textChanged = true;
				}
				const char* weights[] = { "Regular", "Bold" };
				int weightIndex = component.textFontWeight == "Bold" ? 1 : 0;
				if (ImGui::Combo("Weight", &weightIndex, weights, IM_ARRAYSIZE(weights))) {
					component.textFontWeight = weights[weightIndex];
					textChanged = true;
				}
				const char* styles[] = { "Normal", "Italic" };
				int styleIndex = component.textFontStyle == "Italic" ? 1 : 0;
				if (ImGui::Combo("Style", &styleIndex, styles, IM_ARRAYSIZE(styles))) {
					component.textFontStyle = styles[styleIndex];
					textChanged = true;
				}
				textChanged |= ImGui::ColorEdit4("Color", &component.textColor.x);
				textChanged |= ImGui::DragFloat(
					"Opacity", &component.textOpacity, 0.01f, 0.0f, 1.0f
				);
				const char* horizontalAlignments[] = { "Left", "Center", "Right" };
				int horizontalIndex = component.textHorizontalAlignment == "Center"
					? 1 : component.textHorizontalAlignment == "Right" ? 2 : 0;
				if (ImGui::Combo(
					"Horizontal Align", &horizontalIndex, horizontalAlignments,
					IM_ARRAYSIZE(horizontalAlignments)
				)) {
					component.textHorizontalAlignment = horizontalAlignments[horizontalIndex];
					textChanged = true;
				}
				const char* verticalAlignments[] = { "Top", "Center", "Bottom" };
				int verticalIndex = component.textVerticalAlignment == "Center"
					? 1 : component.textVerticalAlignment == "Bottom" ? 2 : 0;
				if (ImGui::Combo(
					"Vertical Align", &verticalIndex, verticalAlignments,
					IM_ARRAYSIZE(verticalAlignments)
				)) {
					component.textVerticalAlignment = verticalAlignments[verticalIndex];
					textChanged = true;
				}
				const char* wrapModes[] = { "NoWrap", "Word" };
				int wrapIndex = component.textWrapMode == "Word" ? 1 : 0;
				if (ImGui::Combo("Wrap", &wrapIndex, wrapModes, IM_ARRAYSIZE(wrapModes))) {
					component.textWrapMode = wrapModes[wrapIndex];
					textChanged = true;
				}
				const char* overflowModes[] = { "Overflow", "Clip", "Ellipsis" };
				int overflowIndex = component.textOverflowMode == "Clip"
					? 1 : component.textOverflowMode == "Ellipsis" ? 2 : 0;
				if (ImGui::Combo(
					"Overflow", &overflowIndex, overflowModes, IM_ARRAYSIZE(overflowModes)
				)) {
					component.textOverflowMode = overflowModes[overflowIndex];
					textChanged = true;
				}
				textChanged |= ImGui::DragFloat2(
					"Layout Size", &component.textLayoutSize.x, 1.0f, 0.0f, 4096.0f
				);
				textChanged |= ImGui::DragFloat(
					"Character Spacing", &component.textCharacterSpacing, 0.1f, -32.0f, 128.0f
				);
				textChanged |= ImGui::DragFloat(
					"Line Spacing", &component.textLineSpacing, 0.01f, 0.1f, 8.0f
				);
				textChanged |= ImGui::Checkbox("Outline", &component.textOutlineEnabled);
				if (component.textOutlineEnabled) {
					textChanged |= ImGui::ColorEdit4("Outline Color", &component.textOutlineColor.x);
					textChanged |= ImGui::DragFloat(
						"Outline Width", &component.textOutlineWidth, 0.1f, 0.0f, 32.0f
					);
				}
				textChanged |= ImGui::Checkbox("Shadow", &component.textShadowEnabled);
				if (component.textShadowEnabled) {
					textChanged |= ImGui::ColorEdit4("Shadow Color", &component.textShadowColor.x);
					textChanged |= ImGui::DragFloat2(
						"Shadow Offset", &component.textShadowOffset.x, 0.1f, -128.0f, 128.0f
					);
				}
				if (!component.textHasPlacementProfiles) {
					const Vector3 legacyEuler = MakeEulerFromQuaternion(entity->transform.rotate);
					Text2DPlacement legacyPlacement{};
					legacyPlacement.position = { entity->transform.translate.x, entity->transform.translate.y };
					legacyPlacement.rotation = legacyEuler.z;
					legacyPlacement.scale = { entity->transform.scale.x, entity->transform.scale.y };
					legacyPlacement.pivot = component.textPivot;
					legacyPlacement.viewportAnchor = component.textViewportAnchor;
					legacyPlacement.sortingOrder = component.textSortingOrder;
					legacyPlacement.clipEnabled = component.textClipEnabled;
					component.textOverlayPlacement = legacyPlacement;
					component.textScene2DPlacement = legacyPlacement;
					component.textHasPlacementProfiles = true;
					textChanged = true;
				}
				Text2DPlacement& placement = component.textRenderSpace == "Scene2D"
					? component.textScene2DPlacement : component.textOverlayPlacement;
				ImGui::SeparatorText(component.textRenderSpace == "Scene2D"
					? "Placement: Scene 2D" : "Placement: Screen Overlay");
				if (component.textRenderSpace == "ScreenOverlay") {
					textChanged |= ImGui::DragFloat2(
						"Viewport Anchor", &placement.viewportAnchor.x, 0.01f, 0.0f, 1.0f
					);
				}
				textChanged |= ImGui::DragFloat2("Position", &placement.position.x, 0.5f);
				textChanged |= ImGui::DragFloat("Rotation", &placement.rotation, 0.01f);
				textChanged |= ImGui::DragFloat2(
					"Scale", &placement.scale.x, 0.01f, 0.001f, 1000.0f
				);
				textChanged |= ImGui::DragFloat2("Pivot", &placement.pivot.x, 0.01f, 0.0f, 1.0f);
				textChanged |= ImGui::DragInt("Sorting Order", &placement.sortingOrder);
				textChanged |= ImGui::Checkbox("Clip", &placement.clipEnabled);
				if (textChanged) {
					component.textFontSize = std::clamp(component.textFontSize, 1.0f, 512.0f);
					component.textOpacity = std::clamp(component.textOpacity, 0.0f, 1.0f);
					component.textLayoutSize.x = std::clamp(component.textLayoutSize.x, 0.0f, 4096.0f);
					component.textLayoutSize.y = std::clamp(component.textLayoutSize.y, 0.0f, 4096.0f);
					component.textLineSpacing = std::clamp(component.textLineSpacing, 0.1f, 8.0f);
					component.textOutlineWidth = std::clamp(component.textOutlineWidth, 0.0f, 32.0f);
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
			} else if (component.type == "CameraSwitcher") {
				ImGui::BeginDisabled(!editorSession_->IsEditing() || entityLocked);
				bool switcherChanged = false;
				ImGui::TextDisabled(
					"Cycles registered cameras in Play Mode"
				);
				const char* currentKey = component.cameraSwitchTriggerKey.empty()
					? "F5"
					: component.cameraSwitchTriggerKey.c_str();
				if (ImGui::BeginCombo("Switch Key", currentKey)) {
					for (const char* key : {
						"F1", "F2", "F3", "F4", "F5", "F6",
						"F7", "F8", "F9", "F10", "F11", "F12"
					}) {
						if (ImGui::Selectable(
							key,
							component.cameraSwitchTriggerKey == key ||
								(component.cameraSwitchTriggerKey.empty() &&
									std::strcmp(key, "F5") == 0)
						)) {
							component.cameraSwitchTriggerKey = key;
							switcherChanged = true;
						}
					}
					ImGui::EndCombo();
				}
				switcherChanged |= ImGui::Checkbox(
					"Wrap To First",
					&component.cameraSwitchWrap
				);
				int removeCameraIndex = -1;
				int moveCameraIndex = -1;
				int moveCameraDirection = 0;
				for (size_t cameraIndex = 0;
					cameraIndex < component.cameraSwitchEntries.size();
					++cameraIndex) {
					SceneCameraSwitchEntry& entry =
						component.cameraSwitchEntries[cameraIndex];
					ImGui::PushID(static_cast<int>(cameraIndex));
					const SceneEntity* selectedCamera =
						entry.cameraEntityId != 0
						? document.FindEntity(entry.cameraEntityId)
						: nullptr;
					if (
						!entry.cameraEntityName.empty() &&
						(!selectedCamera ||
							selectedCamera->name != entry.cameraEntityName)
					) {
						selectedCamera = document.FindEntityByName(
							entry.cameraEntityName
						);
					}
					const std::string cameraLabel = selectedCamera
						? selectedCamera->name
						: "Select Camera...";
					if (ImGui::BeginCombo("Camera", cameraLabel.c_str())) {
						for (const SceneEntity& candidate : document.GetEntities()) {
							if (!FindEnabledComponent(candidate, "Camera")) {
								continue;
							}
							std::string label = candidate.name;
							if (const SceneComponent* cameraComponent =
								FindEnabledComponent(candidate, "Camera");
								cameraComponent && cameraComponent->cameraIsMain) {
								label += " (Main)";
							}
							if (ImGui::Selectable(
								label.c_str(),
								selectedCamera && selectedCamera->id == candidate.id
							)) {
								entry.cameraEntityId = candidate.id;
								entry.cameraEntityName = candidate.name;
								switcherChanged = true;
							}
						}
						ImGui::EndCombo();
					}
					ImGui::BeginDisabled(cameraIndex == 0);
					if (ImGui::SmallButton("Up")) {
						moveCameraIndex = static_cast<int>(cameraIndex);
						moveCameraDirection = -1;
					}
					ImGui::EndDisabled();
					ImGui::SameLine();
					ImGui::BeginDisabled(
						cameraIndex + 1 >= component.cameraSwitchEntries.size()
					);
					if (ImGui::SmallButton("Down")) {
						moveCameraIndex = static_cast<int>(cameraIndex);
						moveCameraDirection = 1;
					}
					ImGui::EndDisabled();
					ImGui::SameLine();
					if (ImGui::SmallButton("Remove")) {
						removeCameraIndex = static_cast<int>(cameraIndex);
					}
					ImGui::PopID();
				}
				if (moveCameraIndex >= 0) {
					std::swap(
						component.cameraSwitchEntries[moveCameraIndex],
						component.cameraSwitchEntries[
							moveCameraIndex + moveCameraDirection
						]
					);
					switcherChanged = true;
				}
				if (removeCameraIndex >= 0) {
					component.cameraSwitchEntries.erase(
						component.cameraSwitchEntries.begin() + removeCameraIndex
					);
					switcherChanged = true;
				}
				SceneCameraSwitchEntry nextCameraEntry{};
				for (const SceneEntity& candidate : document.GetEntities()) {
					if (!FindEnabledComponent(candidate, "Camera")) {
						continue;
					}
					const bool alreadyRegistered = std::any_of(
						component.cameraSwitchEntries.begin(),
						component.cameraSwitchEntries.end(),
						[&candidate](const SceneCameraSwitchEntry& existing) {
							return existing.cameraEntityId == candidate.id ||
								(!existing.cameraEntityName.empty() &&
									existing.cameraEntityName == candidate.name);
						}
					);
					if (!alreadyRegistered) {
						nextCameraEntry.cameraEntityId = candidate.id;
						nextCameraEntry.cameraEntityName = candidate.name;
						break;
					}
				}
				ImGui::BeginDisabled(nextCameraEntry.cameraEntityId == 0);
				if (ImGui::Button("Add Camera")) {
					component.cameraSwitchEntries.push_back(
						std::move(nextCameraEntry)
					);
					switcherChanged = true;
				}
				ImGui::EndDisabled();
				if (switcherChanged) {
					document.MarkDirty();
				}
				ImGui::EndDisabled();
			} else if (component.type == "ThirdPersonCamera") {
				ImGui::BeginDisabled(!editorSession_->IsEditing() || entityLocked);
				bool thirdPersonChanged = false;
				const SceneEntity* targetEntity =
					component.thirdPersonTargetEntityId != 0
					? document.FindEntity(component.thirdPersonTargetEntityId)
					: nullptr;
				if (
					!component.thirdPersonTargetEntityName.empty() &&
					(!targetEntity ||
						targetEntity->name != component.thirdPersonTargetEntityName)
				) {
					targetEntity = document.FindEntityByName(
						component.thirdPersonTargetEntityName
					);
				}
				const std::string targetLabel = targetEntity
					? targetEntity->name
					: "Auto / Legacy Target";
				if (ImGui::BeginCombo("Target Entity", targetLabel.c_str())) {
					if (ImGui::Selectable(
						"Auto / Legacy Target",
						component.thirdPersonTargetEntityId == 0 &&
							component.thirdPersonTargetEntityName.empty()
					)) {
						component.thirdPersonTargetEntityId = 0;
						component.thirdPersonTargetEntityName.clear();
						thirdPersonChanged = true;
					}
					for (const SceneEntity& candidate : document.GetEntities()) {
						if (ImGui::Selectable(
							candidate.name.c_str(),
							targetEntity && targetEntity->id == candidate.id
						)) {
							component.thirdPersonTargetEntityId = candidate.id;
							component.thirdPersonTargetEntityName = candidate.name;
							thirdPersonChanged = true;
						}
					}
					ImGui::EndCombo();
				}
				if (!targetEntity) {
					ImGui::TextDisabled(
						"Auto uses this Entity when it has a model/behavior, otherwise Player"
					);
				}
				thirdPersonChanged |= ImGui::Checkbox(
					"Allow Mouse Input",
					&component.thirdPersonAllowMouseInput
				);
				const char* yawReference =
					component.thirdPersonYawReference == "Target"
					? "Target"
					: "World";
				if (ImGui::BeginCombo("Yaw Reference", yawReference)) {
					for (const char* reference : { "World", "Target" }) {
						if (ImGui::Selectable(
							reference,
							component.thirdPersonYawReference == reference
						)) {
							component.thirdPersonYawReference = reference;
							thirdPersonChanged = true;
						}
					}
					ImGui::EndCombo();
				}
				ImGui::TextDisabled(
					"World: fixed orbit direction / Target: inherit target yaw"
				);
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
				thirdPersonChanged |= ImGui::Checkbox(
					"Aim Mode Enabled",
					&component.thirdPersonAimModeEnabled
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
					"Occlusion Enabled",
					&component.thirdPersonOcclusionEnabled
				);
				thirdPersonChanged |= ImGui::InputScalar(
					"Occlusion Layer Mask",
					ImGuiDataType_U32,
					&component.thirdPersonOcclusionMask
				);
				ImGui::TextDisabled(
					"Only non-trigger colliders on matching layers block the camera."
				);
				thirdPersonChanged |= ImGui::DragFloat(
					"Occlusion Pull-In Smooth Time",
					&component.thirdPersonOcclusionPullInSmoothTime,
					0.01f,
					0.0f,
					5.0f,
					"%.2f s"
				);
				thirdPersonChanged |= ImGui::DragFloat(
					"Occlusion Recovery Smooth Time",
					&component.thirdPersonOcclusionRecoverySmoothTime,
					0.01f,
					0.0f,
					5.0f,
					"%.2f s"
				);
				thirdPersonChanged |= ImGui::DragFloat(
					"Position Smooth Time",
					&component.thirdPersonPositionSmoothTime,
					0.01f,
					0.0f,
					5.0f,
					"%.2f s"
				);
				thirdPersonChanged |= ImGui::DragFloat(
					"Rotation Smooth Time",
					&component.thirdPersonRotationSmoothTime,
					0.01f,
					0.0f,
					5.0f,
					"%.2f s"
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
				if (component.thirdPersonOcclusionPullInSmoothTime < 0.0f) {
					component.thirdPersonOcclusionPullInSmoothTime = 0.0f;
					thirdPersonChanged = true;
				}
				if (component.thirdPersonOcclusionRecoverySmoothTime < 0.0f) {
					component.thirdPersonOcclusionRecoverySmoothTime = 0.0f;
					thirdPersonChanged = true;
				}
				if (component.thirdPersonPositionSmoothTime < 0.0f) {
					component.thirdPersonPositionSmoothTime = 0.0f;
					thirdPersonChanged = true;
				}
				if (component.thirdPersonRotationSmoothTime < 0.0f) {
					component.thirdPersonRotationSmoothTime = 0.0f;
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
					document.AddComponent(point.id, "CameraPathPoint");
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
				colliderChanged |= ImGui::Checkbox(
					"Collider Active",
					&component.colliderActive
				);
				colliderChanged |= ImGui::Checkbox(
					"Is Trigger",
					&component.colliderIsTrigger
				);
				colliderChanged |= ImGui::InputScalar(
					"Collision Layer",
					ImGuiDataType_U32,
					&component.colliderLayer
				);
				colliderChanged |= ImGui::InputScalar(
					"Collision Mask",
					ImGuiDataType_U32,
					&component.colliderMask
				);
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
			} else if (component.type == "StatSet") {
				ImGui::BeginDisabled(!editorSession_->IsEditing() || entityLocked);
				bool statsChanged = false;
				int removeStatIndex = -1;
				for (size_t statIndex = 0; statIndex < component.stats.size(); ++statIndex) {
					SceneStatDefinition& stat = component.stats[statIndex];
					ImGui::PushID(static_cast<int>(statIndex));
					const std::string statLabel = stat.displayName.empty()
						? stat.id
						: stat.displayName;
					if (ImGui::TreeNodeEx(
						"Stat",
						ImGuiTreeNodeFlags_DefaultOpen,
						"%s",
						statLabel.empty() ? "Stat" : statLabel.c_str()
					)) {
						statsChanged |= InputTextString("Id", stat.id);
						statsChanged |= InputTextString("Display Name", stat.displayName);
						statsChanged |= ImGui::DragFloat(
							"Min", &stat.minValue, 0.1f
						);
						statsChanged |= ImGui::DragFloat(
							"Max", &stat.maxValue, 0.1f
						);
						statsChanged |= ImGui::DragFloat(
							"Initial", &stat.initialValue, 0.1f
						);
						if (stat.maxValue < stat.minValue) {
							stat.maxValue = stat.minValue;
							statsChanged = true;
						}
						const float clampedInitial = std::clamp(
							stat.initialValue,
							stat.minValue,
							stat.maxValue
						);
						if (clampedInitial != stat.initialValue) {
							stat.initialValue = clampedInitial;
							statsChanged = true;
						}
						if (ImGui::SmallButton("Remove Stat")) {
							removeStatIndex = static_cast<int>(statIndex);
						}
						ImGui::TreePop();
					}
					ImGui::PopID();
				}
				if (removeStatIndex >= 0) {
					component.stats.erase(
						component.stats.begin() + removeStatIndex
					);
					statsChanged = true;
				}
				if (ImGui::Button("Add Stat")) {
					SceneStatDefinition stat{};
					stat.id = "stat" + std::to_string(component.stats.size() + 1);
					stat.displayName = stat.id;
					component.stats.push_back(std::move(stat));
					statsChanged = true;
				}
				if (statsChanged) {
					document.MarkDirty();
				}
				ImGui::EndDisabled();
			} else if (component.type == "StateMachine") {
				ImGui::BeginDisabled(!editorSession_->IsEditing() || entityLocked);
				bool stateMachineChanged = false;
				const char* initialPreview = component.stateMachineInitialState.empty()
					? "Select State..."
					: component.stateMachineInitialState.c_str();
				if (ImGui::BeginCombo("Initial State", initialPreview)) {
					for (const SceneStateDefinition& state :
						component.stateMachineStates) {
						if (ImGui::Selectable(
							state.name.c_str(),
							component.stateMachineInitialState == state.name
						)) {
							component.stateMachineInitialState = state.name;
							stateMachineChanged = true;
						}
					}
					ImGui::EndCombo();
				}
				stateMachineChanged |= ImGui::Checkbox(
					"Reset On Disable", &component.stateMachineResetOnDisable
				);
				ImGui::TextDisabled(
					"Actions are C++ classes registered by Action Id."
				);
				int removeStateIndex = -1;
				for (size_t stateIndex = 0;
					stateIndex < component.stateMachineStates.size();
					++stateIndex) {
					SceneStateDefinition& state =
						component.stateMachineStates[stateIndex];
					ImGui::PushID(static_cast<int>(stateIndex));
					if (ImGui::TreeNodeEx(
						"State",
						ImGuiTreeNodeFlags_DefaultOpen,
						"%s",
						state.name.empty() ? "State" : state.name.c_str()
					)) {
						stateMachineChanged |= InputTextString("Name", state.name);
						stateMachineChanged |= InputTextString(
							"Action Id", state.actionId
						);
						if (ImGui::BeginCombo("Built-in Action", state.actionId.c_str())) {
							for (const char* actionId : {
								"Builtin.Idle", "Builtin.Move", "Builtin.MeleeAttack",
								"Builtin.MeleeComboAttack"
							}) {
								if (ImGui::Selectable(
									actionId, state.actionId == actionId
								)) {
									state.actionId = actionId;
									stateMachineChanged = true;
								}
							}
							ImGui::EndCombo();
						}

						int removeParameterIndex = -1;
						for (size_t parameterIndex = 0;
							parameterIndex < state.parameters.size();
							++parameterIndex) {
							SceneStateParameter& parameter =
								state.parameters[parameterIndex];
							ImGui::PushID(static_cast<int>(parameterIndex));
							ImGui::SeparatorText(
								parameter.name.empty() ? "Parameter" : parameter.name.c_str()
							);
							stateMachineChanged |= InputTextString(
								"Parameter Name", parameter.name
							);
							if (ImGui::BeginCombo("Type", parameter.type.c_str())) {
								for (const char* type : {
									"Float", "Int", "Bool", "String", "Input", "Entity"
								}) {
									if (ImGui::Selectable(
										type, parameter.type == type
									)) {
										parameter.type = type;
										stateMachineChanged = true;
									}
								}
								ImGui::EndCombo();
							}
							if (parameter.type == "Float") {
								stateMachineChanged |= ImGui::DragFloat(
									"Value", &parameter.floatValue, 0.01f
								);
							} else if (parameter.type == "Int") {
								stateMachineChanged |= ImGui::DragInt(
									"Value", &parameter.intValue
								);
							} else if (parameter.type == "Bool") {
								stateMachineChanged |= ImGui::Checkbox(
									"Value", &parameter.boolValue
								);
							} else if (parameter.type == "String") {
								stateMachineChanged |= InputTextString(
									"Value", parameter.stringValue
								);
							} else if (parameter.type == "Input") {
								const char* inputPreview = parameter.stringValue.empty()
									? "Select Input..."
									: parameter.stringValue.c_str();
								if (ImGui::BeginCombo("Value", inputPreview)) {
									for (const char* inputName : {
										"Mouse Left", "Mouse Right", "Mouse Middle",
										"Space", "Enter", "Escape", "Tab",
										"A", "B", "C", "D", "E", "F", "G",
										"H", "I", "J", "K", "L", "M", "N",
										"O", "P", "Q", "R", "S", "T", "U",
										"V", "W", "X", "Y", "Z"
									}) {
										if (ImGui::Selectable(
											inputName,
											parameter.stringValue == inputName
										)) {
											parameter.stringValue = inputName;
											stateMachineChanged = true;
										}
									}
									ImGui::EndCombo();
								}
							} else if (parameter.type == "Entity") {
								const SceneEntity* selectedParameterEntity =
									parameter.entityId != 0
									? document.FindEntity(parameter.entityId)
									: nullptr;
								if (
									!selectedParameterEntity &&
									!parameter.entityName.empty()
								) {
									selectedParameterEntity = document.FindEntityByName(
										parameter.entityName
									);
								}
								const char* entityPreview = selectedParameterEntity
									? selectedParameterEntity->name.c_str()
									: "Select Entity...";
								if (ImGui::BeginCombo("Value", entityPreview)) {
									for (const SceneEntity& candidate : document.GetEntities()) {
										if (ImGui::Selectable(
											candidate.name.c_str(),
											selectedParameterEntity &&
											selectedParameterEntity->id == candidate.id
										)) {
											parameter.entityId = candidate.id;
											parameter.entityName = candidate.name;
											stateMachineChanged = true;
										}
									}
									ImGui::EndCombo();
								}
							}
							if (ImGui::SmallButton("Remove Parameter")) {
								removeParameterIndex = static_cast<int>(parameterIndex);
							}
							ImGui::PopID();
						}
						if (removeParameterIndex >= 0) {
							state.parameters.erase(
								state.parameters.begin() + removeParameterIndex
							);
							stateMachineChanged = true;
						}
						if (ImGui::SmallButton("Add Parameter")) {
							state.parameters.push_back(SceneStateParameter{});
							stateMachineChanged = true;
						}
						ImGui::SameLine();
						if (ImGui::SmallButton("Remove State")) {
							removeStateIndex = static_cast<int>(stateIndex);
						}
						ImGui::TreePop();
					}
					ImGui::PopID();
				}
				if (removeStateIndex >= 0) {
					component.stateMachineStates.erase(
						component.stateMachineStates.begin() + removeStateIndex
					);
					stateMachineChanged = true;
				}
				if (ImGui::Button("Add State")) {
					SceneStateDefinition state{};
					state.name = "State" + std::to_string(
						component.stateMachineStates.size() + 1
					);
					component.stateMachineStates.push_back(std::move(state));
					stateMachineChanged = true;
				}
				if (stateMachineChanged) {
					document.MarkDirty();
				}
				ImGui::EndDisabled();
			} else if (component.type == "EventTrigger") {
				ImGui::BeginDisabled(!editorSession_->IsEditing() || entityLocked);
				bool eventsChanged = false;
				auto drawComponentTargetCombo = [
					&document,
					&eventsChanged
				](
					const char* label,
					uint64_t& targetId,
					std::string& targetName,
					const char* componentName,
					const char* missingLabel
				) {
					const SceneEntity* selected = targetId != 0
						? document.FindEntity(targetId)
						: nullptr;
					if (!selected && !targetName.empty()) {
						selected = document.FindEntityByName(targetName);
					}
					const SceneComponent* selectedComponent = selected
						? FindComponent(*selected, componentName)
						: nullptr;
					const bool selectedValid = selectedComponent && selectedComponent->enabled;
					const std::string preview = selectedValid
						? BuildEntityHierarchyLabel(document, *selected)
						: missingLabel;
					if (ImGui::BeginCombo(label, preview.c_str())) {
						for (const SceneEntity& candidate : document.GetEntities()) {
							const SceneComponent* candidateComponent =
								FindComponent(candidate, componentName);
							if (!candidateComponent || !candidateComponent->enabled) {
								continue;
							}
							const std::string candidateLabel =
								BuildEntityHierarchyLabel(document, candidate);
							if (ImGui::Selectable(
								candidateLabel.c_str(), targetId == candidate.id
							)) {
								targetId = candidate.id;
								targetName = candidate.name;
								eventsChanged = true;
							}
						}
						ImGui::EndCombo();
					}
					if (!selectedValid) {
						ImGui::TextDisabled("%s", missingLabel);
					}
				};
				int removeBindingIndex = -1;
				for (size_t bindingIndex = 0;
					bindingIndex < component.eventBindings.size();
					++bindingIndex) {
					SceneEventBinding& binding = component.eventBindings[bindingIndex];
					ImGui::PushID(static_cast<int>(bindingIndex));
					if (ImGui::TreeNodeEx(
						"Event Binding",
						ImGuiTreeNodeFlags_DefaultOpen,
						"Event %zu: %s",
						bindingIndex + 1,
						binding.triggerType.c_str()
					)) {
						if (ImGui::BeginCombo("Trigger", binding.triggerType.c_str())) {
							for (const char* trigger : {
								"OnStart", "OnInterval", "OnStatReachedMin", "OnStatCompare",
								"OnPositionReached", "OnKeyPressed",
								"OnCameraPathCompleted"
							}) {
								if (ImGui::Selectable(
									trigger,
									binding.triggerType == trigger
								)) {
									binding.triggerType = trigger;
									if (binding.triggerType == "OnKeyPressed") {
										binding.triggerOnce = false;
									}
									if (binding.triggerType == "OnInterval") {
										binding.triggerOnce = false;
										if (binding.cooldown <= 0.0f) {
											binding.cooldown = 1.0f;
										}
									}
									eventsChanged = true;
								}
							}
							ImGui::EndCombo();
						}
						const bool triggerNeedsTarget =
							binding.triggerType == "OnStatReachedMin" ||
							binding.triggerType == "OnStatCompare" ||
							binding.triggerType == "OnPositionReached";
						if (binding.triggerType == "OnCameraPathCompleted") {
							drawComponentTargetCombo(
								"Camera Path",
								binding.targetEntityId,
								binding.targetEntityName,
								"CameraPath",
								"Missing CameraPath"
							);
						} else if (triggerNeedsTarget) {
							eventsChanged |= ImGui::InputScalar(
								"Target Entity Id",
								ImGuiDataType_U64,
								&binding.targetEntityId
							);
							eventsChanged |= InputTextString(
								"Target Entity Name", binding.targetEntityName
							);
						}
						if (
							binding.triggerType == "OnStatReachedMin" ||
							binding.triggerType == "OnStatCompare"
						) {
							eventsChanged |= InputTextString("Stat Id", binding.statId);
						}
						if (binding.triggerType == "OnStatCompare") {
							if (ImGui::BeginCombo(
								"Comparison", binding.statComparison.c_str()
							)) {
								for (const char* comparison : {
									"LessOrEqual", "Less", "Equal",
									"Greater", "GreaterOrEqual"
								}) {
									if (ImGui::Selectable(
										comparison,
										binding.statComparison == comparison
									)) {
										binding.statComparison = comparison;
										eventsChanged = true;
									}
								}
								ImGui::EndCombo();
							}
							eventsChanged |= ImGui::DragFloat(
								"Compare Value", &binding.statValue, 0.1f
							);
						}
						if (binding.triggerType == "OnPositionReached") {
							eventsChanged |= ImGui::DragFloat3(
								"Target Position", &binding.targetPosition.x, 0.05f
							);
							eventsChanged |= ImGui::DragFloat(
								"Radius", &binding.radius, 0.05f, 0.0f, 10000.0f
							);
						}
						if (binding.triggerType == "OnKeyPressed") {
							if (ImGui::BeginCombo(
								"Key",
								binding.triggerKey.empty()
									? "Select..."
									: binding.triggerKey.c_str()
							)) {
								for (const SceneInputKeyDefinition& key :
									kSceneInputKeyDefinitions) {
									if (ImGui::Selectable(
										key.name,
										binding.triggerKey == key.name
									)) {
										binding.triggerKey = key.name;
										eventsChanged = true;
									}
								}
								ImGui::EndCombo();
							}
						}
						eventsChanged |= ImGui::Checkbox(
							"Trigger Once", &binding.triggerOnce
						);
						eventsChanged |= ImGui::DragFloat(
							"Cooldown", &binding.cooldown, 0.01f, 0.0f, 10000.0f
						);
						binding.radius = (std::max)(binding.radius, 0.0f);
						binding.cooldown = (std::max)(binding.cooldown, 0.0f);

						ImGui::SeparatorText("Actions");
						int removeActionIndex = -1;
						for (size_t actionIndex = 0;
							actionIndex < binding.actions.size();
							++actionIndex) {
							SceneEventAction& action = binding.actions[actionIndex];
							ImGui::PushID(static_cast<int>(actionIndex));
							if (ImGui::TreeNodeEx(
								"Action",
								ImGuiTreeNodeFlags_DefaultOpen,
								"Action %zu: %s",
								actionIndex + 1,
								action.type.c_str()
							)) {
								if (ImGui::BeginCombo("Type", action.type.c_str())) {
									for (const char* actionType : {
										"ModifyStat", "SetEntityActive",
										"InstantiatePrefab", "ChangeState",
										"SceneTransition", "SetPostProcessProfile",
										"NextPostProcessProfile",
										"ResetPostProcessProfile", "PlayCameraPath",
										"StopCameraPath", "SelectCamera"
									}) {
										if (ImGui::Selectable(
											actionType,
											action.type == actionType
										)) {
											action.type = actionType;
											eventsChanged = true;
										}
									}
									ImGui::EndCombo();
								}
								if (
								action.type != "SceneTransition" &&
								action.type != "SetPostProcessProfile" &&
								action.type != "NextPostProcessProfile" &&
								action.type != "ResetPostProcessProfile" &&
								action.type != "PlayCameraPath" &&
								action.type != "StopCameraPath" &&
								action.type != "SelectCamera"
								) {
									eventsChanged |= ImGui::InputScalar(
										"Action Target Entity Id",
										ImGuiDataType_U64,
										&action.targetEntityId
									);
									eventsChanged |= InputTextString(
										"Action Target Entity Name",
										action.targetEntityName
									);
								}
								if (action.type == "ModifyStat") {
									eventsChanged |= InputTextString(
										"Action Stat Id", action.statId
									);
									if (ImGui::BeginCombo(
										"Operation", action.statOperation.c_str()
									)) {
										for (const char* operation : {
											"Add", "Subtract", "Set", "Multiply",
											"SetMin", "SetMax", "RestoreToMax"
										}) {
											if (ImGui::Selectable(
												operation,
												action.statOperation == operation
											)) {
												action.statOperation = operation;
												eventsChanged = true;
											}
										}
										ImGui::EndCombo();
									}
									eventsChanged |= ImGui::DragFloat(
										"Value", &action.value, 0.1f
									);
								} else if (action.type == "SetEntityActive") {
									eventsChanged |= ImGui::Checkbox(
										"Active", &action.active
									);
								} else if (action.type == "InstantiatePrefab") {
									eventsChanged |= InputTextString(
										"Prefab Path", action.prefabPath
									);
									eventsChanged |= ImGui::Checkbox(
										"Parent To Target", &action.prefabParentToTarget
									);
									eventsChanged |= ImGui::Checkbox(
										"Spawn At Target Transform",
										&action.prefabUseTargetTransform
									);
								} else if (action.type == "ChangeState") {
									eventsChanged |= InputTextString(
										"State Name", action.stateName
									);
								} else if (action.type == "SceneTransition") {
									eventsChanged |= InputTextString(
										"Scene Id", action.sceneId
									);
								} else if (
									action.type == "PlayCameraPath" ||
									action.type == "StopCameraPath"
								) {
									drawComponentTargetCombo(
										"Camera Path",
										action.targetEntityId,
										action.targetEntityName,
										"CameraPath",
										"Missing CameraPath"
									);
								} else if (action.type == "SelectCamera") {
									drawComponentTargetCombo(
										"Camera",
										action.targetEntityId,
										action.targetEntityName,
										"Camera",
										"Missing Camera"
									);
								} else if (
									action.type == "SetPostProcessProfile" ||
									action.type == "NextPostProcessProfile"
								) {
									const SceneEntity* selectedManager =
										action.postProcessManagerEntityId != 0
										? document.FindEntity(action.postProcessManagerEntityId)
										: nullptr;
									if (
										!selectedManager &&
										!action.postProcessManagerEntityName.empty()
									) {
										selectedManager = document.FindEntityByName(
											action.postProcessManagerEntityName
										);
									}
									const SceneComponent* managerComponent =
										selectedManager
										? FindComponent(
											*selectedManager,
											"PostProcessProfileManager"
										)
										: nullptr;
									const std::string managerPreview = managerComponent
										? BuildEntityHierarchyLabel(document, *selectedManager)
										: "Missing Manager";
									if (ImGui::BeginCombo(
										"Manager", managerPreview.c_str()
									)) {
										for (const SceneEntity& candidate : document.GetEntities()) {
											if (!FindComponent(
												candidate, "PostProcessProfileManager"
											)) {
												continue;
											}
											const std::string label =
												BuildEntityHierarchyLabel(document, candidate);
											if (ImGui::Selectable(
												label.c_str(),
												action.postProcessManagerEntityId == candidate.id
											)) {
												action.postProcessManagerEntityId = candidate.id;
												action.postProcessManagerEntityName = candidate.name;
												action.postProcessProfileId.clear();
												eventsChanged = true;
											}
										}
										ImGui::EndCombo();
									}
									if (!managerComponent) {
										ImGui::TextDisabled("Select a PostProcessProfileManager.");
									} else if (action.type == "SetPostProcessProfile") {
										const ScenePostProcessProfile* selectedProfile = nullptr;
										for (const ScenePostProcessProfile& profile :
											managerComponent->postProcessProfiles) {
											if (profile.id == action.postProcessProfileId) {
												selectedProfile = &profile;
												break;
											}
										}
										const char* profilePreview = selectedProfile
											? (selectedProfile->label.empty()
												? selectedProfile->id.c_str()
												: selectedProfile->label.c_str())
											: "Missing Profile";
										if (ImGui::BeginCombo("Profile", profilePreview)) {
											for (const ScenePostProcessProfile& profile :
												managerComponent->postProcessProfiles) {
												const char* label = profile.label.empty()
													? profile.id.c_str()
													: profile.label.c_str();
												if (ImGui::Selectable(
													label,
													profile.id == action.postProcessProfileId
												)) {
													action.postProcessProfileId = profile.id;
													eventsChanged = true;
												}
											}
											ImGui::EndCombo();
										}
									}
								}
								if (ImGui::SmallButton("Remove Action")) {
									removeActionIndex = static_cast<int>(actionIndex);
								}
								ImGui::TreePop();
							}
							ImGui::PopID();
						}
						if (removeActionIndex >= 0) {
							binding.actions.erase(
								binding.actions.begin() + removeActionIndex
							);
							eventsChanged = true;
						}
						if (ImGui::SmallButton("Add Action")) {
							binding.actions.push_back(SceneEventAction{});
							eventsChanged = true;
						}
						if (ImGui::SmallButton("Remove Event")) {
							removeBindingIndex = static_cast<int>(bindingIndex);
						}
						ImGui::TreePop();
					}
					ImGui::PopID();
				}
				if (removeBindingIndex >= 0) {
					component.eventBindings.erase(
						component.eventBindings.begin() + removeBindingIndex
					);
					eventsChanged = true;
				}
				if (ImGui::Button("Add Event")) {
					component.eventBindings.push_back(SceneEventBinding{});
					eventsChanged = true;
				}
				if (eventsChanged) {
					document.MarkDirty();
				}
				ImGui::EndDisabled();
			} else if (component.type == "PostProcessProfileManager") {
				ImGui::BeginDisabled(!editorSession_->IsEditing() || entityLocked);
				bool profilesChanged = false;
				int removeProfileIndex = -1;
				int moveProfileIndex = -1;
				int moveProfileDirection = 0;
				for (size_t profileIndex = 0;
					profileIndex < component.postProcessProfiles.size();
					++profileIndex
				) {
					ScenePostProcessProfile& profile =
						component.postProcessProfiles[profileIndex];
					ImGui::PushID(static_cast<int>(profileIndex));
					if (ImGui::TreeNodeEx(
						"Profile", ImGuiTreeNodeFlags_DefaultOpen,
						"Profile %zu: %s", profileIndex + 1, profile.label.c_str()
					)) {
						ImGui::TextDisabled("Id: %s", profile.id.c_str());
						profilesChanged |= InputTextString("Label", profile.label);
						const bool duplicateProfileId = !profile.id.empty() &&
							std::any_of(
								component.postProcessProfiles.begin(),
								component.postProcessProfiles.end(),
								[&profile](const ScenePostProcessProfile& candidate) {
									return &candidate != &profile &&
										candidate.id == profile.id;
								}
							);
						if (duplicateProfileId) {
							ImGui::TextDisabled(
								"Profile Id must be unique within this Manager."
							);
						}
						if (ImGui::SmallButton("Copy Scene Baseline")) {
							profile.settings = document.GetPostProcessSettings();
							profilesChanged = true;
						}
						profilesChanged |= DrawPostProcessSettingsEditor(profile.settings);
						bool dissolveAutomationEnabled =
							!profile.automations.empty();
						if (ImGui::Checkbox(
							"Animate Dissolve Threshold",
							&dissolveAutomationEnabled
						)) {
							if (dissolveAutomationEnabled) {
								profile.automations = {
									ScenePostProcessAutomation{}
								};
							} else {
								profile.automations.clear();
							}
							profilesChanged = true;
						}
						if (!profile.automations.empty()) {
							ScenePostProcessAutomation& automation =
								profile.automations.front();
							profilesChanged |= ImGui::SliderFloat(
								"Automation Start",
								&automation.startValue,
								0.0f,
								1.0f
							);
							profilesChanged |= ImGui::SliderFloat(
								"Automation End",
								&automation.endValue,
								0.0f,
								1.0f
							);
							profilesChanged |= ImGui::DragFloat(
								"Automation Duration",
								&automation.duration,
								0.05f,
								0.05f,
								60.0f,
								"%.2f s"
							);
							automation.duration = (std::max)(
								automation.duration,
								0.05f
							);
							ImGui::TextDisabled(
								"Playback: OneShot / Easing: Linear"
							);
						}
						if (profile.id.empty()) {
							ImGui::TextDisabled("Profile Id is required for Event actions.");
						}
						if (ImGui::SmallButton("Remove Profile")) {
							removeProfileIndex = static_cast<int>(profileIndex);
						}
						ImGui::SameLine();
						ImGui::BeginDisabled(profileIndex == 0);
						if (ImGui::SmallButton("Move Up")) {
							moveProfileIndex = static_cast<int>(profileIndex);
							moveProfileDirection = -1;
						}
						ImGui::EndDisabled();
						ImGui::SameLine();
						ImGui::BeginDisabled(
							profileIndex + 1 == component.postProcessProfiles.size()
						);
						if (ImGui::SmallButton("Move Down")) {
							moveProfileIndex = static_cast<int>(profileIndex);
							moveProfileDirection = 1;
						}
						ImGui::EndDisabled();
						ImGui::TreePop();
					}
					ImGui::PopID();
				}
				if (removeProfileIndex >= 0) {
					component.postProcessProfiles.erase(
						component.postProcessProfiles.begin() + removeProfileIndex
					);
					profilesChanged = true;
				}
				if (moveProfileIndex >= 0) {
					const int destination =
						moveProfileIndex + moveProfileDirection;
					std::swap(
						component.postProcessProfiles[moveProfileIndex],
						component.postProcessProfiles[destination]
					);
					profilesChanged = true;
				}
				if (ImGui::Button("Add Profile")) {
					ScenePostProcessProfile profile{};
					for (size_t candidateIndex = 1;; ++candidateIndex) {
						profile.id = "Profile" + std::to_string(candidateIndex);
						const bool alreadyExists = std::any_of(
							component.postProcessProfiles.begin(),
							component.postProcessProfiles.end(),
							[&profile](const ScenePostProcessProfile& candidate) {
								return candidate.id == profile.id;
							}
						);
						if (!alreadyExists) {
							break;
						}
					}
					profile.label = profile.id;
					component.postProcessProfiles.push_back(std::move(profile));
					profilesChanged = true;
				}
				if (profilesChanged) {
					document.MarkDirty();
				}
				ImGui::EndDisabled();
			} else if (component.type == "PrefabAnimator") {
				ImGui::BeginDisabled(!editorSession_->IsEditing() || entityLocked);
				bool animationChanged = false;
				int removeClipIndex = -1;
				for (size_t clipIndex = 0;
					clipIndex < component.prefabAnimationClips.size();
					++clipIndex) {
					ScenePrefabAnimationClip& clip =
						component.prefabAnimationClips[clipIndex];
					ImGui::PushID(static_cast<int>(clipIndex));
					if (ImGui::TreeNodeEx(
						"Prefab Clip",
						ImGuiTreeNodeFlags_DefaultOpen,
						"Clip %zu: %s",
						clipIndex + 1,
						clip.name.c_str()
					)) {
						animationChanged |= InputTextString("Clip Name", clip.name);
						animationChanged |= ImGui::DragFloat(
							"Duration", &clip.duration, 0.01f, 0.001f, 10000.0f
						);
						animationChanged |= ImGui::Checkbox("Loop", &clip.loop);
						animationChanged |= ImGui::Checkbox(
							"Play On Start", &clip.playOnStart
						);
						clip.duration = (std::max)(clip.duration, 0.001f);
						int removeTrackIndex = -1;
						for (size_t trackIndex = 0;
							trackIndex < clip.tracks.size();
							++trackIndex) {
							SceneAnimationTrack& track = clip.tracks[trackIndex];
							ImGui::PushID(static_cast<int>(trackIndex));
							if (ImGui::TreeNodeEx(
								"Track",
								ImGuiTreeNodeFlags_DefaultOpen,
								"Track %zu: %s",
								trackIndex + 1,
								track.property.c_str()
							)) {
								animationChanged |= ImGui::InputScalar(
									"Target Entity Id",
									ImGuiDataType_U64,
									&track.targetEntityId
								);
								animationChanged |= InputTextString(
									"Target Entity Name", track.targetEntityName
								);
								if (ImGui::BeginCombo("Property", track.property.c_str())) {
									for (const char* property : {
										"LocalPosition", "LocalRotation", "LocalScale", "Active"
									}) {
										if (ImGui::Selectable(
											property,
											track.property == property
										)) {
											track.property = property;
											animationChanged = true;
										}
									}
									ImGui::EndCombo();
								}
								if (track.property != "Active") {
									if (ImGui::BeginCombo(
										"Easing",
										track.easing.empty()
											? "SmoothStep"
											: track.easing.c_str()
									)) {
										for (const char* easing : {
											"Linear", "EaseIn", "EaseOut", "EaseInOut",
											"SmoothStep"
										}) {
											if (ImGui::Selectable(
												easing,
												track.easing == easing
											)) {
												track.easing = easing;
												animationChanged = true;
											}
										}
										ImGui::EndCombo();
									}
								}
								int removeKeyframeIndex = -1;
								bool keyframeTimeChanged = false;
								for (size_t keyframeIndex = 0;
									keyframeIndex < track.keyframes.size();
									++keyframeIndex) {
									SceneAnimationKeyframe& keyframe =
										track.keyframes[keyframeIndex];
									ImGui::PushID(static_cast<int>(keyframeIndex));
									ImGui::SeparatorText("Keyframe");
									if (ImGui::DragFloat(
										"Time", &keyframe.time, 0.01f, 0.0f, clip.duration
									)) {
										keyframe.time = std::clamp(
											keyframe.time,
											0.0f,
											clip.duration
										);
										keyframeTimeChanged = true;
										animationChanged = true;
									}
									if (track.property == "Active") {
										bool activeValue = keyframe.value.x >= 0.5f;
										if (ImGui::Checkbox("Active Value", &activeValue)) {
											keyframe.value.x = activeValue ? 1.0f : 0.0f;
											animationChanged = true;
										}
									} else {
										animationChanged |= ImGui::DragFloat3(
											track.property == "LocalRotation"
												? "Euler Value (Radians)"
												: "Value",
											&keyframe.value.x,
											0.01f
										);
									}
									if (ImGui::SmallButton("Remove Keyframe")) {
										removeKeyframeIndex = static_cast<int>(keyframeIndex);
									}
									ImGui::PopID();
								}
								if (removeKeyframeIndex >= 0) {
									track.keyframes.erase(
										track.keyframes.begin() + removeKeyframeIndex
									);
									animationChanged = true;
								}
								if (keyframeTimeChanged) {
									std::stable_sort(
										track.keyframes.begin(),
										track.keyframes.end(),
										[](const SceneAnimationKeyframe& left,
											const SceneAnimationKeyframe& right) {
											return left.time < right.time;
										}
									);
								}
								if (ImGui::SmallButton("Add Keyframe")) {
									SceneAnimationKeyframe keyframe{};
									keyframe.time = track.keyframes.empty()
										? 0.0f
										: (std::min)(
											track.keyframes.back().time + 0.1f,
											clip.duration
										);
									if (!track.keyframes.empty()) {
										keyframe.value = track.keyframes.back().value;
									}
									track.keyframes.push_back(keyframe);
									animationChanged = true;
								}
								if (ImGui::SmallButton("Remove Track")) {
									removeTrackIndex = static_cast<int>(trackIndex);
								}
								ImGui::TreePop();
							}
							ImGui::PopID();
						}
						if (removeTrackIndex >= 0) {
							clip.tracks.erase(clip.tracks.begin() + removeTrackIndex);
							animationChanged = true;
						}
						if (ImGui::SmallButton("Add Track")) {
							SceneAnimationTrack track{};
							track.keyframes = {
								{ 0.0f, {} },
								{ clip.duration, {} }
							};
							clip.tracks.push_back(std::move(track));
							animationChanged = true;
						}
						if (ImGui::SmallButton("Remove Clip")) {
							removeClipIndex = static_cast<int>(clipIndex);
						}
						ImGui::TreePop();
					}
					ImGui::PopID();
				}
				if (removeClipIndex >= 0) {
					component.prefabAnimationClips.erase(
						component.prefabAnimationClips.begin() + removeClipIndex
					);
					animationChanged = true;
				}
				if (ImGui::Button("Add Clip")) {
					component.prefabAnimationClips.push_back(
						ScenePrefabAnimationClip{}
					);
					animationChanged = true;
				}
				if (animationChanged) {
					document.MarkDirty();
				}
				ImGui::EndDisabled();
			} else if (component.type == "Faction") {
				ImGui::BeginDisabled(!editorSession_->IsEditing() || entityLocked);
				if (InputTextString("Faction", component.factionName)) {
					document.MarkDirty();
				}
				ImGui::EndDisabled();
			} else if (component.type == "HitBox") {
				ImGui::BeginDisabled(!editorSession_->IsEditing() || entityLocked);
				bool hitBoxChanged = false;
				hitBoxChanged |= ImGui::DragFloat(
					"Damage", &component.hitBoxDamage, 0.1f, 0.0f, 100000.0f
				);
				hitBoxChanged |= ImGui::DragFloat(
					"Poise Damage",
					&component.hitBoxPoiseDamage,
					0.1f,
					0.0f,
					100000.0f
				);
				hitBoxChanged |= ImGui::DragFloat(
					"Knockback",
					&component.hitBoxKnockback,
					0.1f, 0.0f, 100000.0f
				);
				hitBoxChanged |= ImGui::DragFloat(
					"Vertical Knockback",
					&component.hitBoxVerticalKnockback,
					0.1f, 0.0f, 100000.0f
				);
				hitBoxChanged |= ImGui::DragFloat(
					"Hit Stop Duration",
					&component.hitBoxHitStopDuration,
					0.001f, 0.0f, 1.0f
				);
				hitBoxChanged |= InputTextString(
					"Reaction Tag", component.hitBoxReactionTag
				);
				hitBoxChanged |= InputTextString(
					"Damage Stat", component.hitBoxDamageStatId
				);
				hitBoxChanged |= InputTextString(
					"Poise Stat", component.hitBoxPoiseStatId
				);
				hitBoxChanged |= ImGui::InputScalar(
					"Owner Entity Id",
					ImGuiDataType_U64,
					&component.hitBoxOwnerEntityId
				);
				hitBoxChanged |= InputTextString(
					"Owner Entity Name", component.hitBoxOwnerEntityName
				);
				hitBoxChanged |= ImGui::Checkbox(
					"Ignore Same Faction", &component.hitBoxIgnoreSameFaction
				);
				component.hitBoxDamage = (std::max)(component.hitBoxDamage, 0.0f);
				component.hitBoxPoiseDamage = (std::max)(
					component.hitBoxPoiseDamage,
					0.0f
				);
				if (hitBoxChanged) {
					document.MarkDirty();
				}
				ImGui::TextDisabled("Requires a Trigger Collider on this Entity.");
				ImGui::EndDisabled();
			} else if (component.type == "HurtBox") {
				ImGui::BeginDisabled(!editorSession_->IsEditing() || entityLocked);
				bool hurtBoxChanged = false;
				hurtBoxChanged |= ImGui::DragFloat(
					"Damage Multiplier",
					&component.hurtBoxDamageMultiplier,
					0.01f,
					0.0f,
					100.0f
				);
				hurtBoxChanged |= InputTextString(
					"Health Stat", component.hurtBoxHealthStatId
				);
				hurtBoxChanged |= ImGui::InputScalar(
					"Stats Entity Id",
					ImGuiDataType_U64,
					&component.hurtBoxStatsEntityId
				);
				hurtBoxChanged |= InputTextString(
					"Stats Entity Name", component.hurtBoxStatsEntityName
				);
				component.hurtBoxDamageMultiplier = (std::max)(
					component.hurtBoxDamageMultiplier,
					0.0f
				);
				if (hurtBoxChanged) {
					document.MarkDirty();
				}
				ImGui::TextDisabled("Requires a Trigger Collider on this Entity.");
				ImGui::EndDisabled();
			} else if (component.type == "HitReaction") {
				ImGui::BeginDisabled(!editorSession_->IsEditing() || entityLocked);
				bool reactionChanged = false;
				reactionChanged |= ImGui::DragFloat(
					"Knockback Multiplier",
					&component.hitReactionKnockbackMultiplier,
					0.01f, 0.0f, 100.0f
				);
				const char* reactionModePreview =
					component.hitReactionTriggerMode == "PoiseBreak"
					? "Poise Break" : "Minimum Damage";
				if (ImGui::BeginCombo("Reaction Trigger", reactionModePreview)) {
					if (ImGui::Selectable(
						"Minimum Damage",
						component.hitReactionTriggerMode == "MinimumDamage"
					)) {
						component.hitReactionTriggerMode = "MinimumDamage";
						reactionChanged = true;
					}
					if (ImGui::Selectable(
						"Poise Break",
						component.hitReactionTriggerMode == "PoiseBreak"
					)) {
						component.hitReactionTriggerMode = "PoiseBreak";
						reactionChanged = true;
					}
					ImGui::EndCombo();
				}
				if (component.hitReactionTriggerMode == "PoiseBreak") {
					reactionChanged |= InputTextString(
						"Poise Stat", component.hitReactionPoiseStatId
					);
					reactionChanged |= ImGui::DragFloat(
						"Poise Recovery Delay",
						&component.hitReactionPoiseRecoveryDelay,
						0.05f, 0.0f, 60.0f
					);
				} else {
					reactionChanged |= ImGui::DragFloat(
						"Minimum Poise Damage",
						&component.hitReactionMinimumPoiseDamage,
						0.1f, 0.0f, 100000.0f
					);
				}
				reactionChanged |= InputTextString(
					"Hit State", component.hitReactionStateName
				);
				if (reactionChanged) { document.MarkDirty(); }
				ImGui::EndDisabled();
			} else if (component.type == "DeathPresentation") {
				ImGui::BeginDisabled(!editorSession_->IsEditing() || entityLocked);
				bool deathChanged = false;
				deathChanged |= InputTextString(
					"Death State", component.deathPresentationStateName
				);
				deathChanged |= ImGui::DragFloat(
					"Deactivate Delay",
					&component.deathPresentationDeactivateDelay,
					0.05f, 0.0f, 60.0f
				);
				deathChanged |= InputTextString(
					"Death Effect Path", component.deathPresentationEffectPath
				);
				if (deathChanged) { document.MarkDirty(); }
				ImGui::EndDisabled();
			} else if (component.type == "BoneAttachment") {
				ImGui::BeginDisabled(!editorSession_->IsEditing() || entityLocked);
				bool attachmentChanged = false;
				const SceneEntity* targetEntity = nullptr;
				if (component.boneAttachmentTargetEntityId != 0) {
					targetEntity = document.FindEntity(
						component.boneAttachmentTargetEntityId
					);
				}
				if (
					!targetEntity &&
					!component.boneAttachmentTargetEntityName.empty()
				) {
					targetEntity = document.FindEntityByName(
						component.boneAttachmentTargetEntityName
					);
				}
				const SceneEntity* parentEntity = document.FindEntity(entity->parentId);
				const SceneEntity* effectiveTargetEntity = targetEntity
					? targetEntity
					: parentEntity;
				const std::string targetLabel = targetEntity
					? targetEntity->name
					: "Parent / Auto";
				if (ImGui::BeginCombo("Target Entity", targetLabel.c_str())) {
					const bool autoSelected =
						component.boneAttachmentTargetEntityId == 0 &&
						component.boneAttachmentTargetEntityName.empty();
					if (ImGui::Selectable("Parent / Auto", autoSelected)) {
						component.boneAttachmentTargetEntityId = 0;
						component.boneAttachmentTargetEntityName.clear();
						component.boneAttachmentJointName.clear();
						attachmentChanged = true;
					}
					for (const SceneEntity& candidate : document.GetEntities()) {
						const SceneComponent* meshRenderer =
							FindEnabledComponent(candidate, "MeshRenderer");
						if (
							candidate.id == entity->id ||
							!meshRenderer ||
							meshRenderer->modelPath.empty()
						) {
							continue;
						}
						if (ImGui::Selectable(
							candidate.name.c_str(),
							targetEntity && targetEntity->id == candidate.id
						)) {
							component.boneAttachmentTargetEntityId = candidate.id;
							component.boneAttachmentTargetEntityName = candidate.name;
							component.boneAttachmentJointName.clear();
							attachmentChanged = true;
						}
					}
					ImGui::EndCombo();
				}
				if (targetEntity) {
					ImGui::TextDisabled(
						"Bound Entity ID: %llu",
						static_cast<unsigned long long>(targetEntity->id)
					);
				} else if (!parentEntity) {
					ImGui::TextDisabled("Select a target Entity or set a parent.");
				}

				const std::vector<std::string> targetJointNames =
					effectiveTargetEntity
					? CollectEntityJointNames(*effectiveTargetEntity)
					: std::vector<std::string>{};
				ImGui::BeginDisabled(targetJointNames.empty());
				attachmentChanged |= DrawJointNameCombo(
					"Target Bone",
					targetJointNames,
					component.boneAttachmentJointName
				);
				ImGui::EndDisabled();
				if (targetJointNames.empty()) {
					ImGui::TextDisabled(
						"The target Entity needs a MeshRenderer model with bones."
					);
				}

				const bool matchesSourceBone =
					component.boneAttachmentAlignmentMode == "MatchSourceBone";
				if (ImGui::BeginCombo(
					"Alignment Mode",
					matchesSourceBone ? "Match Weapon Bone" : "Manual Offset"
				)) {
					if (ImGui::Selectable(
						"Manual Offset", !matchesSourceBone
					)) {
						component.boneAttachmentAlignmentMode = "ManualOffset";
						attachmentChanged = true;
					}
					if (ImGui::Selectable(
						"Match Weapon Bone", matchesSourceBone
					)) {
						component.boneAttachmentAlignmentMode = "MatchSourceBone";
						attachmentChanged = true;
					}
					ImGui::EndCombo();
				}
				if (matchesSourceBone) {
					const std::vector<std::string> sourceJointNames =
						CollectEntityJointNames(*entity);
					ImGui::BeginDisabled(sourceJointNames.empty());
					attachmentChanged |= DrawJointNameCombo(
						"Weapon Bone",
						sourceJointNames,
						component.boneAttachmentSourceJointName
					);
					ImGui::EndDisabled();
					if (sourceJointNames.empty()) {
						ImGui::TextDisabled(
							"This Entity needs a MeshRenderer model with bones."
						);
					} else {
						ImGui::TextDisabled(
							"This Entity Transform is ignored so both bones match exactly."
						);
					}
				} else {
					ImGui::TextDisabled(
						"Use this Entity's Transform section above as the attachment offset."
					);
				}
				attachmentChanged |= ImGui::Checkbox(
					"Inherit Bone Scale", &component.boneAttachmentInheritScale
				);
				if (attachmentChanged) {
					document.MarkDirty();
				}
				ImGui::EndDisabled();
			} else if (component.type == "EnemyBehavior") {
				ImGui::BeginDisabled(!editorSession_->IsEditing() || entityLocked);
				bool enemyChanged = false;
				enemyChanged |= ImGui::InputScalar(
					"Target Entity Id",
					ImGuiDataType_U64,
					&component.enemyTargetEntityId
				);
				enemyChanged |= InputTextString(
					"Target Entity Name", component.enemyTargetEntityName
				);
				enemyChanged |= InputTextString(
					"Health Stat", component.enemyHealthStatId
				);
				enemyChanged |= ImGui::DragFloat(
					"Detection Range", &component.enemyDetectionRange, 0.1f, 0.0f, 10000.0f
				);
				enemyChanged |= ImGui::DragFloat(
					"Lose Range", &component.enemyLoseRange, 0.1f, 0.0f, 10000.0f
				);
				enemyChanged |= ImGui::DragFloat(
					"Attack Range", &component.enemyAttackRange, 0.1f, 0.0f, 10000.0f
				);
				enemyChanged |= ImGui::DragFloat(
					"Move Speed", &component.enemyMoveSpeed, 0.05f, 0.0f, 1000.0f
				);
				enemyChanged |= ImGui::DragFloat(
					"Turn Speed", &component.enemyTurnSpeed, 0.05f, 0.0f, 1000.0f
				);
				enemyChanged |= ImGui::DragFloat(
					"Attack Cooldown", &component.enemyAttackCooldown, 0.01f, 0.0f, 1000.0f
				);
				enemyChanged |= ImGui::DragFloat(
					"Attack Windup", &component.enemyAttackWindup, 0.01f, 0.0f, 1000.0f
				);
				enemyChanged |= ImGui::DragFloat(
					"Attack Active Time", &component.enemyAttackActiveTime, 0.01f, 0.0f, 1000.0f
				);
				enemyChanged |= ImGui::DragFloat(
					"Attack Recovery", &component.enemyAttackRecovery, 0.01f, 0.0f, 1000.0f
				);
				enemyChanged |= ImGui::DragInt(
					"Attack Animation Clip", &component.enemyAttackAnimationClip, 1.0f, 0, 1024
				);
				enemyChanged |= InputTextString(
					"Attack Prefab Animation Clip",
					component.enemyAttackPrefabAnimationClip
				);
				enemyChanged |= ImGui::InputScalar(
					"Attack HitBox Entity Id",
					ImGuiDataType_U64,
					&component.enemyAttackHitBoxEntityId
				);
				enemyChanged |= InputTextString(
					"Attack HitBox Entity Name",
					component.enemyAttackHitBoxEntityName
				);
				component.enemyLoseRange = (std::max)(
					component.enemyLoseRange,
					component.enemyDetectionRange
				);
				if (enemyChanged) {
					document.MarkDirty();
				}
				ImGui::EndDisabled();
			} else if (component.type == "EnemySpawner") {
				ImGui::BeginDisabled(!editorSession_->IsEditing() || entityLocked);
				bool spawnerChanged = false;
				spawnerChanged |= InputTextString(
					"Enemy Prefab", component.enemySpawnerPrefabPath
				);
				spawnerChanged |= ImGui::DragInt(
					"Initial Count", &component.enemySpawnerInitialCount,
					1.0f, 0, 10000
				);
				spawnerChanged |= ImGui::DragInt(
					"Max Alive", &component.enemySpawnerMaxAlive,
					1.0f, 0, 10000
				);
				spawnerChanged |= ImGui::DragFloat(
					"Respawn Interval", &component.enemySpawnerInterval,
					0.05f, 0.0f, 3600.0f
				);
				spawnerChanged |= ImGui::DragFloat(
					"Spawn Radius", &component.enemySpawnerRadius,
					0.1f, 0.0f, 10000.0f
				);
				spawnerChanged |= ImGui::Checkbox(
					"Auto Start", &component.enemySpawnerAutoStart
				);
				component.enemySpawnerInitialCount = (std::max)(
					component.enemySpawnerInitialCount, 0
				);
				component.enemySpawnerMaxAlive = (std::max)(
					component.enemySpawnerMaxAlive,
					component.enemySpawnerInitialCount
				);
				component.enemySpawnerInterval = (std::max)(
					component.enemySpawnerInterval, 0.0f
				);
				component.enemySpawnerRadius = (std::max)(
					component.enemySpawnerRadius, 0.0f
				);
				if (spawnerChanged) {
					document.MarkDirty();
				}
				ImGui::TextDisabled(
					"Runtime-only instances are reset to their prefab baseline before reuse."
				);
				ImGui::EndDisabled();
			} else if (component.type == "Projectile") {
				ImGui::BeginDisabled(!editorSession_->IsEditing() || entityLocked);
				bool projectileChanged = false;
				projectileChanged |= ImGui::DragFloat3(
					"Local Direction", &component.projectileDirection.x, 0.01f
				);
				projectileChanged |= ImGui::DragFloat(
					"Speed", &component.projectileSpeed, 0.1f, 0.0f, 10000.0f
				);
				projectileChanged |= ImGui::DragFloat(
					"Gravity", &component.projectileGravity, 0.1f, -1000.0f, 1000.0f
				);
				projectileChanged |= ImGui::DragFloat(
					"Lifetime", &component.projectileLifetime, 0.05f, 0.0f, 10000.0f
				);
				projectileChanged |= ImGui::Checkbox(
					"Destroy On Hit", &component.projectileDestroyOnHit
				);
				projectileChanged |= ImGui::InputScalar(
					"Homing Target Entity Id",
					ImGuiDataType_U64,
					&component.projectileHomingTargetEntityId
				);
				projectileChanged |= InputTextString(
					"Homing Target Entity Name",
					component.projectileHomingTargetEntityName
				);
				projectileChanged |= ImGui::DragFloat(
					"Homing Strength",
					&component.projectileHomingStrength,
					0.1f,
					0.0f,
					1000.0f
				);
				if (projectileChanged) {
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
				const bool belongsToAgentTeam =
					agentTeam && !agentTeam->name.empty();
				const bool hasTeamAgentSettings =
					belongsToAgentTeam && agentTeam->agentBehaviorOverride;
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
				const bool isGroundAgent =
					component.agentMovementMode == "GroundXZ";
				if (ImGui::BeginCombo(
					"Movement Mode",
					isGroundAgent ? "Ground XZ" : "Free 3D"
				)) {
					if (ImGui::Selectable("Free 3D", !isGroundAgent)) {
						component.agentMovementMode = "Free3D";
						agentChanged = true;
					}
					if (ImGui::Selectable("Ground XZ", isGroundAgent)) {
						component.agentMovementMode = "GroundXZ";
						agentChanged = true;
					}
					ImGui::EndCombo();
				}
				if (isGroundAgent) {
					ImGui::TextDisabled(
						"Adds XZ separation to PhysicsBody velocity after EnemyBehavior."
					);
					ImGui::TextDisabled(
						"Transform, rotation, and vertical velocity remain owned by other systems."
					);
					ImGui::BeginDisabled(useTeamAgentSettings);
					ImGui::SeparatorText("Ground Separation");
					agentChanged |= ImGui::DragFloat(
						"Separation Radius",
						&component.agentSeparationRadius,
						0.05f,
						0.0f,
						100.0f
					);
					agentChanged |= ImGui::DragFloat(
						"Separation Weight",
						&component.agentSeparationWeight,
						0.05f,
						0.0f,
						100.0f
					);
					agentChanged |= ImGui::InputInt(
						"Neighbor Limit",
						&component.agentNeighborLimit
					);
					ImGui::EndDisabled();
				}
				ImGui::BeginDisabled(useTeamAgentSettings);
				if (
					belongsToAgentTeam ||
					component.agentSchooling ||
					isGroundAgent
				) {
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
				if (component.agentWanderStrength > 0.0f) {
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
					if (!component.agentRandomizeSeedOnPlay) {
						agentChanged |= ImGui::InputInt(
							"Random Seed",
							&component.agentRandomSeed
						);
					}
				}
				if (belongsToAgentTeam) {
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
					if (component.agentUseTeamHeading) {
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
					}
				}

				ImGui::SeparatorText("Rotation");
				agentChanged |= ImGui::Checkbox(
					"Align Forward To Velocity",
					&component.agentAlignForwardToVelocity
				);
				if (component.agentAlignForwardToVelocity) {
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
				}
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
				agentChanged |= ImGui::DragFloat(
					"Attractor Weight",
					&component.agentAttractorWeight,
					0.05f,
					0.0f,
					50.0f
				);
				if (component.agentAttractorWeight > 0.0f) {
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
				}

				ImGui::BeginDisabled(useTeamAgentSettings);
				ImGui::SeparatorText("Schooling");
				agentChanged |= ImGui::Checkbox(
					"Schooling",
					&component.agentSchooling
				);
				if (component.agentSchooling) {
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
				}

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
				"TextRenderer",
				"Camera",
				"Light",
				"MonitorRenderer",
				"CameraSwitcher",
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
				"OBBCollider",
				"StatSet",
				"StateMachine",
				"EventTrigger",
				"PostProcessProfileManager",
				"PrefabAnimator",
				"Faction",
				"HitBox",
				"HurtBox",
				"HitReaction",
				"DeathPresentation",
				"BoneAttachment",
				"EnemyBehavior",
				"EnemySpawner",
				"Projectile"
			};
			for (const char* componentType : availableComponents) {
				if (HasComponent(*entity, componentType)) {
					continue;
				}
				if (ImGui::Selectable(componentType)) {
					if (
						std::strcmp(componentType, "ThirdPersonCamera") == 0 &&
						!HasComponent(*entity, "Camera")
					) {
						document.AddComponent(entity->id, "Camera");
					}
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

void ImGuiManager::RequestPrefabQuickOpen() {
	showProject_ = true;
	prefabQuickOpenPopupRequested_ = true;
}

void ImGuiManager::DrawProjectPrefabAccessPanel() {
	ImGui::SeparatorText("Prefabs");
	if (ImGui::Button("Quick Open...", ImVec2(-1.0f, 0.0f))) {
		RequestPrefabQuickOpen();
	}

	std::string openRequestedPath;
	PrefabAssetReference toggleFavoriteReference{};
	bool toggleFavoriteRequested = false;
	PrefabAssetReference removeRecentReference{};
	bool removeRecentRequested = false;
	auto drawPrefabList = [&](
		const char* label,
		const std::vector<PrefabAssetReference>& references,
		bool recentList
	) {
		if (!ImGui::TreeNodeEx(label, ImGuiTreeNodeFlags_DefaultOpen)) {
			return;
		}
		if (references.empty()) {
			ImGui::TextDisabled("None");
		}
		for (const PrefabAssetReference& reference : references) {
			const std::string resolvedProjectPath =
				PrefabAssetRegistry::ResolvePath(reference);
			const std::string displayProjectPath = resolvedProjectPath.empty()
				? reference.fallbackPath
				: resolvedProjectPath;
			const std::filesystem::path path =
				EditableResourcePath::ResolveResource(
					PathFromUtf8(displayProjectPath)
				).lexically_normal();
			const std::string prefabPath = PathToUtf8(path);
			const std::string fileName = PathToUtf8(path.filename());
			std::error_code existsError;
			const bool exists =
				!resolvedProjectPath.empty() &&
				std::filesystem::exists(path, existsError);
			const std::string itemLabel = exists
				? fileName
				: fileName + " [Missing]";
			const std::string itemId = reference.assetId.empty()
				? reference.fallbackPath
				: reference.assetId + "|" + reference.fallbackPath;
			ImGui::PushID(itemId.c_str());
			const bool selected = selectedProjectFile_ == prefabPath;
			if (ImGui::Selectable(itemLabel.c_str(), selected) && exists) {
				SelectPrefabAssetInProject(prefabPath);
			}
			if (
				exists &&
				ImGui::IsItemHovered() &&
				ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)
			) {
				openRequestedPath = prefabPath;
			}
			if (ImGui::IsItemHovered()) {
				ImGui::SetTooltip("%s", displayProjectPath.c_str());
			}
			if (ImGui::BeginPopupContextItem("PrefabAccessContext")) {
				if (ImGui::MenuItem("Open Prefab", nullptr, false, exists)) {
					openRequestedPath = prefabPath;
				}
				const bool favorite = ContainsPrefabAssetReference(
					favoritePrefabReferences_,
					reference
				);
				if (ImGui::MenuItem(
					favorite ? "Remove from Favorites" : "Add to Favorites"
				)) {
					toggleFavoriteReference = reference;
					toggleFavoriteRequested = true;
				}
				if (
					recentList &&
					ImGui::MenuItem("Remove from Recent")
				) {
					removeRecentReference = reference;
					removeRecentRequested = true;
				}
				ImGui::EndPopup();
			}
			ImGui::PopID();
		}
		ImGui::TreePop();
	};

	std::vector<PrefabAssetReference> favorites = favoritePrefabReferences_;
	std::sort(
		favorites.begin(),
		favorites.end(),
		[](const PrefabAssetReference& left, const PrefabAssetReference& right) {
			return PathFromUtf8(PrefabAssetRegistry::ResolvePath(left)).filename() <
				PathFromUtf8(PrefabAssetRegistry::ResolvePath(right)).filename();
		}
	);
	drawPrefabList("Favorites", favorites, false);
	drawPrefabList("Recent", recentPrefabReferences_, true);

	if (toggleFavoriteRequested) {
		ToggleFavoritePrefab(toggleFavoriteReference);
	}
	if (removeRecentRequested) {
		recentPrefabReferences_.erase(
			std::remove_if(
				recentPrefabReferences_.begin(),
				recentPrefabReferences_.end(),
				[&removeRecentReference](const PrefabAssetReference& recent) {
					return PrefabAssetRegistry::IsSameAsset(
						recent,
						removeRecentReference
					);
				}
			),
			recentPrefabReferences_.end()
		);
		SaveEditorSettings();
	}
	if (!openRequestedPath.empty()) {
		RequestOpenPrefab(openRequestedPath);
	}
}

void ImGuiManager::DrawPrefabQuickOpenPopup() {
	if (!ImGui::BeginPopup("Quick Open Prefab")) {
		return;
	}
	ImGui::SetNextItemWidth(520.0f);
	if (prefabQuickOpenFocusRequested_) {
		ImGui::SetKeyboardFocusHere();
		prefabQuickOpenFocusRequested_ = false;
	}
	const bool searchSubmitted = ImGui::InputTextWithHint(
		"##PrefabQuickSearch",
		"Search Prefabs...",
		prefabQuickOpenSearchBuffer_,
		sizeof(prefabQuickOpenSearchBuffer_),
		ImGuiInputTextFlags_EnterReturnsTrue
	);
	ImGui::Separator();

	std::string openRequestedPath;
	std::string selectRequestedPath;
	std::string firstVisiblePath;
	int visibleResultCount = 0;
	if (ImGui::BeginChild(
		"PrefabQuickOpenResults",
		ImVec2(520.0f, 300.0f),
		ImGuiChildFlags_Borders
	)) {
		for (const std::string& prefabPath : GetCachedPrefabAssetPaths()) {
			const std::string relativePath = PathToUtf8(
				EditableResourcePath::ToProjectRelative(
					PathFromUtf8(prefabPath)
				)
			);
			if (!ContainsCaseInsensitive(
				relativePath,
				prefabQuickOpenSearchBuffer_
			)) {
				continue;
			}
			if (firstVisiblePath.empty()) {
				firstVisiblePath = prefabPath;
			}
			++visibleResultCount;
			const std::string fileName = PathToUtf8(
				PathFromUtf8(prefabPath).filename()
			);
			const std::string label =
				(IsFavoritePrefab(prefabPath) ? "[Favorite] " : "") +
				fileName + "##" + prefabPath;
			if (ImGui::Selectable(label.c_str())) {
				openRequestedPath = prefabPath;
			}
			if (ImGui::IsItemHovered()) {
				ImGui::SetTooltip("%s", relativePath.c_str());
			}
			if (ImGui::BeginPopupContextItem("QuickPrefabContext")) {
				if (ImGui::MenuItem("Open Prefab")) {
					openRequestedPath = prefabPath;
				}
				if (ImGui::MenuItem("Select Asset")) {
					selectRequestedPath = prefabPath;
				}
				const bool favorite = IsFavoritePrefab(prefabPath);
				if (ImGui::MenuItem(
					favorite ? "Remove from Favorites" : "Add to Favorites"
				)) {
					ToggleFavoritePrefab(prefabPath);
				}
				ImGui::EndPopup();
			}
		}
		if (visibleResultCount == 0) {
			ImGui::TextDisabled("No matching Prefabs.");
		}
	}
	ImGui::EndChild();
	if (searchSubmitted && !firstVisiblePath.empty()) {
		openRequestedPath = firstVisiblePath;
	}
	if (!selectRequestedPath.empty()) {
		SelectPrefabAssetInProject(selectRequestedPath);
		ImGui::CloseCurrentPopup();
	}
	if (!openRequestedPath.empty()) {
		RequestOpenPrefab(openRequestedPath);
		ImGui::CloseCurrentPopup();
	}
	ImGui::EndPopup();
}

void ImGuiManager::DrawProjectWindow() {
	if (projectFocusRequested_) {
		ImGui::SetNextWindowFocus();
		projectFocusRequested_ = false;
	}
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
	DrawProjectPrefabAccessPanel();
	
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
	ImGui::SameLine();
	ImGui::Checkbox("Prefabs Only", &projectPrefabFilterEnabled_);
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
			const bool isPrefab =
				!isDirectory && IsPrefabAssetPath(PathFromUtf8(filePath));
			if (
				projectPrefabFilterEnabled_ &&
				!isDirectory &&
				!isPrefab
			) {
				continue;
			}
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
			bool openPrefabRequested = false;
			bool openPrefabContextRequested = false;
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
				if (
					!(isModel || isTexture || isPrefab) ||
					!ImGui::BeginDragDropSource()
				) {
					return;
				}
				const std::string dragPath = isModel
					? GetModelPathRelativeToResources(filePath)
					: isTexture
						? resourcePath
						: GetProjectResourcePath(filePath);
				const char* payloadType = isModel
					? "PROJECT_MODEL_PATH"
					: isTexture
						? "PROJECT_TEXTURE_PATH"
						: "PROJECT_PREFAB_PATH";
				ImGui::SetDragDropPayload(
					payloadType,
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
			auto drawPrefabContextMenu = [&]() {
				if (!isPrefab || !ImGui::BeginPopupContextItem(
					"PrefabAssetContext"
				)) {
					return;
				}
				if (ImGui::MenuItem("Open Prefab")) {
					openPrefabContextRequested = true;
				}
				if (ImGui::MenuItem("Select Asset")) {
					SelectPrefabAssetInProject(filePath);
				}
				const bool favorite = IsFavoritePrefab(filePath);
				if (ImGui::MenuItem(
					favorite ? "Remove from Favorites" : "Add to Favorites"
				)) {
					ToggleFavoritePrefab(filePath);
				}
				ImGui::EndPopup();
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
					openPrefabRequested = fileName.ends_with(".prefab.json") &&
						ImGui::IsItemHovered() &&
						ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
					if (ImGui::IsItemHovered()) {
						loadHoveredTexturePreview();
					}
					drawPrefabContextMenu();
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
				openPrefabRequested = fileName.ends_with(".prefab.json") &&
					ImGui::IsItemHovered() &&
					ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
				if (ImGui::IsItemHovered()) {
					loadHoveredTexturePreview();
				}
				drawPrefabContextMenu();
				drawDragSource();
			}
			openPrefabRequested =
				openPrefabRequested || openPrefabContextRequested;

			if (isSelected || selectedProjectFolder_ == filePath) {
				ImGui::PopStyleColor();
			}
			if (clicked || openSceneRequested || openPrefabRequested) {
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
					if (openPrefabRequested) {
						RequestOpenPrefab(filePath);
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

void ImGuiManager::RequestOpenPrefab(
	const std::string& filePath,
	int historyIndex
) {
	if (!prefabEditorSession_ || filePath.empty()) {
		return;
	}

	const std::string resolvedPath = PathToUtf8(
		EditableResourcePath::ResolveResource(
			PathFromUtf8(filePath)
		).lexically_normal()
	);
	showPrefab_ = true;
	prefabFocusFramesRemaining_ = 2;

	if (prefabEditorSession_->IsOpen()) {
		const std::filesystem::path currentPath =
			EditableResourcePath::ResolveResource(
				PathFromUtf8(prefabEditorSession_->GetFilePath())
			).lexically_normal();
		const std::filesystem::path targetPath = PathFromUtf8(resolvedPath);
		std::error_code equivalentError;
		const bool samePrefab = std::filesystem::equivalent(
			currentPath,
			targetPath,
			equivalentError
		);
		if (
			samePrefab ||
			(equivalentError && currentPath == targetPath)
		) {
			if (
				historyIndex >= 0 &&
				historyIndex <
					static_cast<int>(prefabNavigationHistory_.size())
			) {
				prefabNavigationHistory_[historyIndex] =
					PrefabAssetRegistry::CreateReference(resolvedPath);
				prefabNavigationIndex_ = historyIndex;
			}
			return;
		}
		if (prefabEditorSession_->IsDirty()) {
			pendingPrefabOpenPath_ = resolvedPath;
			pendingPrefabHistoryIndex_ = historyIndex;
			prefabOpenPopupRequested_ = true;
			return;
		}
	}

	OpenPrefab(resolvedPath, historyIndex);
}

bool ImGuiManager::OpenPrefab(
	const std::string& filePath,
	int historyIndex
) {
	if (!prefabEditorSession_ || filePath.empty()) {
		return false;
	}

	const std::string resolvedPath = PathToUtf8(
		EditableResourcePath::ResolveResource(
			PathFromUtf8(filePath)
		).lexically_normal()
	);
	if (!prefabEditorSession_->Open(resolvedPath)) {
		prefabNavigationStatus_ = prefabEditorSession_->GetLastError();
		return false;
	}

	showPrefab_ = true;
	prefabFocusFramesRemaining_ = 2;
	const SceneDocument& prefab = prefabEditorSession_->GetDocument();
	prefabSelectedEntityId_ = prefab.GetEntities().empty()
		? 0
		: prefab.GetEntities().front().id;
	++prefabPreviewFramingSerial_;
	prefabNavigationStatus_.clear();
	RecordRecentPrefab(resolvedPath);
	const PrefabAssetReference openedReference =
		PrefabAssetRegistry::CreateReference(resolvedPath);

	if (
		historyIndex >= 0 &&
		historyIndex < static_cast<int>(prefabNavigationHistory_.size())
	) {
		prefabNavigationHistory_[historyIndex] = openedReference;
		prefabNavigationIndex_ = historyIndex;
		return true;
	}

	if (
		prefabNavigationIndex_ + 1 <
			static_cast<int>(prefabNavigationHistory_.size())
	) {
		prefabNavigationHistory_.erase(
			prefabNavigationHistory_.begin() + prefabNavigationIndex_ + 1,
			prefabNavigationHistory_.end()
		);
	}
	if (
		prefabNavigationHistory_.empty() ||
		!PrefabAssetRegistry::IsSameAsset(
			prefabNavigationHistory_.back(),
			openedReference
		)
	) {
		prefabNavigationHistory_.push_back(openedReference);
	} else {
		prefabNavigationHistory_.back() = openedReference;
	}
	prefabNavigationIndex_ =
		static_cast<int>(prefabNavigationHistory_.size()) - 1;
	return true;
}

void ImGuiManager::DrawPrefabOpenConfirmation() {
	if (!prefabEditorSession_) {
		return;
	}
	if (prefabOpenPopupRequested_) {
		ImGui::OpenPopup("Open Another Prefab?");
		prefabOpenPopupRequested_ = false;
	}
	if (!ImGui::BeginPopupModal(
		"Open Another Prefab?",
		nullptr,
		ImGuiWindowFlags_AlwaysAutoResize
	)) {
		return;
	}

	ImGui::TextUnformatted("The current Prefab has unsaved changes.");
	ImGui::TextWrapped("Open: %s", pendingPrefabOpenPath_.c_str());
	if (ImGui::Button("Save and Open")) {
		if (prefabEditorSession_->Save()) {
			OpenPrefab(
				pendingPrefabOpenPath_,
				pendingPrefabHistoryIndex_
			);
			pendingPrefabOpenPath_.clear();
			pendingPrefabHistoryIndex_ = -1;
			ImGui::CloseCurrentPopup();
		}
	}
	ImGui::SameLine();
	if (ImGui::Button("Discard and Open")) {
		prefabEditorSession_->Close(true);
		OpenPrefab(
			pendingPrefabOpenPath_,
			pendingPrefabHistoryIndex_
		);
		pendingPrefabOpenPath_.clear();
		pendingPrefabHistoryIndex_ = -1;
		ImGui::CloseCurrentPopup();
	}
	ImGui::SameLine();
	if (ImGui::Button("Cancel")) {
		pendingPrefabOpenPath_.clear();
		pendingPrefabHistoryIndex_ = -1;
		ImGui::CloseCurrentPopup();
	}
	ImGui::EndPopup();
}

void ImGuiManager::SelectPrefabAssetInProject(const std::string& filePath) {
	if (filePath.empty()) {
		return;
	}
	const std::filesystem::path resolvedPath =
		EditableResourcePath::ResolveResource(
			PathFromUtf8(filePath)
		).lexically_normal();
	selectedProjectFolder_ = PathToUtf8(resolvedPath.parent_path());
	selectedProjectFile_ = PathToUtf8(resolvedPath);
	selectedEntityId_ = 0;
	selectedEntityIds_.clear();
	projectDirectoryCacheDirty_ = true;
	showProject_ = true;
	projectFocusRequested_ = true;
}

uint64_t ImGuiManager::InstantiatePrefabInEditScene(
	const std::string& filePath,
	uint64_t parentId,
	const Vector3* rootTranslate
) {
	if (
		!editorSession_ ||
		!editorSession_->IsEditing() ||
		filePath.empty()
	) {
		return 0;
	}
	SceneDocument& document = editorSession_->GetEditDocument();
	const uint64_t instanceId = document.InstantiatePrefab(
		filePath,
		parentId
	);
	if (instanceId == 0) {
		return 0;
	}
	if (rootTranslate) {
		if (SceneEntity* root = document.FindEntity(instanceId)) {
			root->transform.translate = *rootTranslate;
			document.MarkDirty();
		}
	}
	selectedEntityId_ = instanceId;
	selectedEntityIds_ = { instanceId };
	hierarchySelectionAnchorId_ = instanceId;
	hierarchyRevealRequested_ = true;
	selectedProjectFile_.clear();
	editorSession_->RequestSceneReload();
	return instanceId;
}

void ImGuiManager::ValidateAllPrefabAssets() {
	prefabAssetValidationCompleted_ = false;
	prefabAssetValidationScannedCount_ = 0;
	prefabAssetValidationResults_.clear();
	prefabAssetPathCacheDirty_ = true;
	RefreshPrefabAssetPathCache();
	PrefabAssetRegistry::Invalidate();

	std::unordered_map<std::string, std::string> firstPathByAssetId;
	std::unordered_set<std::string> reportedDuplicatePaths;
	std::unordered_map<std::string, std::unordered_set<std::string>>
		dependenciesByPath;
	std::unordered_map<std::string, std::string> resultPathByResolvedPath;
	auto resolveAbsolutePath = [](const std::string& path) {
		return PathToUtf8(
			EditableResourcePath::ResolveResource(
				PathFromUtf8(path)
			).lexically_normal()
		);
	};
	auto addResult = [this](
		const std::string& filePath,
		bool error,
		std::string message
	) {
		prefabAssetValidationResults_.push_back({
			filePath,
			std::move(message),
			error
		});
	};

	for (const std::string& prefabPath : cachedPrefabAssetPaths_) {
		++prefabAssetValidationScannedCount_;
		const std::string resolvedPrefabPath = resolveAbsolutePath(prefabPath);
		dependenciesByPath.try_emplace(resolvedPrefabPath);
		resultPathByResolvedPath[resolvedPrefabPath] = prefabPath;
		const std::string rawAssetId =
			PrefabAssetRegistry::ReadAssetId(prefabPath);
		if (!rawAssetId.empty()) {
			const auto [found, inserted] = firstPathByAssetId.emplace(
				rawAssetId,
				prefabPath
			);
			if (!inserted && found->second != prefabPath) {
				const std::string message =
					"Duplicate Prefab Asset ID: " + rawAssetId;
				if (reportedDuplicatePaths.insert(found->second).second) {
					addResult(found->second, true, message);
				}
				if (reportedDuplicatePaths.insert(prefabPath).second) {
					addResult(prefabPath, true, message);
				}
			}
		}
		SceneDocument document;
		if (!document.Load(prefabPath)) {
			addResult(prefabPath, true, document.GetLastLoadError());
			continue;
		}
		if (!document.GetLastLoadError().empty()) {
			addResult(prefabPath, false, document.GetLastLoadError());
		}
		if (document.IsDirty()) {
			addResult(
				prefabPath,
				false,
				"Migrated or recovered data must be saved to persist the current format."
			);
		}

		const std::string& assetId = document.GetAssetId();
		if (assetId.empty()) {
			addResult(prefabPath, true, "Prefab Asset ID is missing.");
		}

		const PrefabAssetReference currentAsset{ assetId, prefabPath };
		if (document.IsPrefabVariant()) {
			const std::string basePath = PrefabAssetRegistry::ResolvePath(
				document.GetVariantBaseAssetId(),
				document.GetVariantBasePath()
			);
			if (!basePath.empty()) {
				dependenciesByPath[resolvedPrefabPath].insert(
					resolveAbsolutePath(basePath)
				);
			}
		}
		std::unordered_set<std::string> dependencyKeys;
		for (const SceneEntity& entity : document.GetEntities()) {
			for (size_t linkIndex = 0;
				linkIndex < entity.prefabLinks.size();
				++linkIndex) {
				const ScenePrefabLink& link = entity.prefabLinks[linkIndex];
				const PrefabAssetReference source{
					link.assetId,
					link.sourcePath
				};
				const std::string key = !source.assetId.empty()
					? "id:" + source.assetId
					: !source.fallbackPath.empty()
						? "path:" + source.fallbackPath
						: "invalid:" + std::to_string(entity.id) + ":" +
							std::to_string(linkIndex);
				if (!dependencyKeys.insert(key).second) {
					continue;
				}
				if (PrefabAssetRegistry::IsSameAsset(currentAsset, source)) {
					dependenciesByPath[resolvedPrefabPath].insert(
						resolvedPrefabPath
					);
					continue;
				}
				const std::string resolvedPath =
					PrefabAssetRegistry::ResolvePath(source);
				if (resolvedPath.empty()) {
					std::string message =
						"Nested Prefab reference cannot be resolved";
					if (!entity.name.empty()) {
						message += " (Entity: " + entity.name + ")";
					}
					if (!source.fallbackPath.empty()) {
						message += ": " + source.fallbackPath;
					}
					addResult(prefabPath, true, std::move(message));
				} else {
					dependenciesByPath[resolvedPrefabPath].insert(
						resolveAbsolutePath(resolvedPath)
					);
					if (source.assetId.empty()) {
						addResult(
							prefabPath,
							false,
							"Nested Prefab uses a legacy Path-only reference: " +
								resolvedPath
						);
					}
				}
			}
		}
	}

	std::unordered_map<std::string, uint8_t> visitStates;
	std::vector<std::string> dependencyStack;
	std::unordered_set<std::string> reportedCyclePaths;
	std::function<void(const std::string&)> visitDependency;
	visitDependency = [&](const std::string& path) {
		visitStates[path] = 1;
		dependencyStack.push_back(path);
		const auto dependencies = dependenciesByPath.find(path);
		if (dependencies != dependenciesByPath.end()) {
			for (const std::string& dependency : dependencies->second) {
				const uint8_t dependencyState = visitStates[dependency];
				if (dependencyState == 0) {
					visitDependency(dependency);
					continue;
				}
				if (dependencyState != 1) {
					continue;
				}
				const auto cycleStart = std::find(
					dependencyStack.begin(),
					dependencyStack.end(),
					dependency
				);
				std::string cycleLabel;
				for (auto cyclePath = cycleStart;
					cyclePath != dependencyStack.end();
					++cyclePath) {
					if (!cycleLabel.empty()) {
						cycleLabel += " -> ";
					}
					cycleLabel += PathToUtf8(
						PathFromUtf8(*cyclePath).filename()
					);
				}
				cycleLabel += " -> " + PathToUtf8(
					PathFromUtf8(dependency).filename()
				);
				for (auto cyclePath = cycleStart;
					cyclePath != dependencyStack.end();
					++cyclePath) {
					if (!reportedCyclePaths.insert(*cyclePath).second) {
						continue;
					}
					const auto resultPath =
						resultPathByResolvedPath.find(*cyclePath);
					addResult(
						resultPath == resultPathByResolvedPath.end()
							? *cyclePath
							: resultPath->second,
						true,
						"Prefab dependency cycle detected: " + cycleLabel
					);
				}
			}
		}
		dependencyStack.pop_back();
		visitStates[path] = 2;
	};
	for (const auto& [path, dependencies] : dependenciesByPath) {
		(void)dependencies;
		if (visitStates[path] == 0) {
			visitDependency(path);
		}
	}
	prefabAssetValidationCompleted_ = true;
}

void ImGuiManager::DrawPrefabDiagnostics() {
	if (!prefabEditorSession_ || !prefabEditorSession_->IsOpen()) {
		return;
	}

	const SceneDocument& document = prefabEditorSession_->GetDocument();
	const std::string& filePath = prefabEditorSession_->GetFilePath();
	std::vector<SceneValidationIssue> issues;
	SceneValidator::ValidateDocument(document, nullptr, {}, filePath, issues);

	struct DependencyStatus {
		PrefabAssetReference reference;
		std::string resolvedPath;
		uint64_t entityId = 0;
	};
	std::vector<DependencyStatus> dependencies;
	std::unordered_set<std::string> dependencyKeys;
	const PrefabAssetReference currentAsset{
		document.GetAssetId(),
		filePath
	};
	for (const SceneEntity& entity : document.GetEntities()) {
		for (size_t linkIndex = 0;
			linkIndex < entity.prefabLinks.size();
			++linkIndex) {
			const ScenePrefabLink& link = entity.prefabLinks[linkIndex];
			const PrefabAssetReference source{ link.assetId, link.sourcePath };
			if (PrefabAssetRegistry::IsSameAsset(currentAsset, source)) {
				continue;
			}
			const std::string key = !source.assetId.empty()
				? "id:" + source.assetId
				: !source.fallbackPath.empty()
					? "path:" + source.fallbackPath
					: "invalid:" + std::to_string(entity.id) + ":" +
						std::to_string(linkIndex);
			if (!dependencyKeys.insert(key).second) {
				continue;
			}
			dependencies.push_back({
				source,
				PrefabAssetRegistry::ResolvePath(source),
				entity.id
			});
		}
	}

	const bool assetIdMissing = document.GetAssetId().empty();
	const bool isVariant = document.IsPrefabVariant();
	const std::string variantBasePath = isVariant
		? PrefabAssetRegistry::ResolvePath(
			document.GetVariantBaseAssetId(),
			document.GetVariantBasePath()
		)
		: std::string{};
	const bool variantBaseMissing = isVariant && variantBasePath.empty();
	size_t errorCount = 0;
	if (assetIdMissing) {
		++errorCount;
	}
	if (variantBaseMissing) {
		++errorCount;
	}
	size_t warningCount = 0;
	for (const DependencyStatus& dependency : dependencies) {
		if (dependency.resolvedPath.empty()) {
			++errorCount;
		} else if (dependency.reference.assetId.empty()) {
			++warningCount;
		}
	}
	for (const SceneValidationIssue& issue : issues) {
		if (issue.severity == SceneValidationSeverity::Error) {
			++errorCount;
		} else {
			++warningCount;
		}
	}
	if (prefabAssetValidationCompleted_) {
		for (const PrefabAssetValidationResult& result :
			prefabAssetValidationResults_) {
			if (result.error) {
				++errorCount;
			} else {
				++warningCount;
			}
		}
	}

	const std::string headerLabel = errorCount == 0 && warningCount == 0
		? "Diagnostics: OK###PrefabDiagnostics"
		: "Diagnostics: " + std::to_string(errorCount) + " error(s), " +
			std::to_string(warningCount) + " warning(s)###PrefabDiagnostics";
	const ImGuiTreeNodeFlags headerFlags =
		errorCount > 0 || !prefabAssetValidationCompleted_
		? ImGuiTreeNodeFlags_DefaultOpen
		: ImGuiTreeNodeFlags_None;
	if (!ImGui::CollapsingHeader(headerLabel.c_str(), headerFlags)) {
		return;
	}

	size_t componentCount = 0;
	for (const SceneEntity& entity : document.GetEntities()) {
		componentCount += entity.components.size();
	}
	ImGui::Text("Type: %s", isVariant ? "Prefab Variant" : "Prefab");
	ImGui::SameLine();
	ImGui::TextDisabled(
		"Entities: %zu  Components: %zu",
		document.GetEntities().size(),
		componentCount
	);
	if (assetIdMissing) {
		ImGui::TextColored(
			ImVec4(0.95f, 0.35f, 0.3f, 1.0f),
			"Asset ID: Missing"
		);
	} else {
		ImGui::TextWrapped("Asset ID: %s", document.GetAssetId().c_str());
	}
	if (isVariant) {
		if (variantBaseMissing) {
			ImGui::TextColored(
				ImVec4(0.95f, 0.35f, 0.3f, 1.0f),
				"Variant Base: Missing or ambiguous"
			);
		} else {
			ImGui::TextWrapped("Variant Base: %s", variantBasePath.c_str());
		}
	}

	ImGui::SeparatorText("Project Prefab Validation");
	if (ImGui::Button("Validate All Prefabs")) {
		ValidateAllPrefabAssets();
	}
	ImGui::SameLine();
	ImGui::TextDisabled(
		"Read-only validation of saved resources/**/*.prefab.json"
	);
	if (!prefabAssetValidationCompleted_) {
		ImGui::TextDisabled("Not validated in this Editor session.");
	} else {
		size_t projectErrorCount = 0;
		size_t projectWarningCount = 0;
		for (const PrefabAssetValidationResult& result :
			prefabAssetValidationResults_) {
			if (result.error) {
				++projectErrorCount;
			} else {
				++projectWarningCount;
			}
		}
		ImGui::Text(
			"Scanned: %zu  Errors: %zu  Warnings: %zu",
			prefabAssetValidationScannedCount_,
			projectErrorCount,
			projectWarningCount
		);
		if (prefabAssetValidationResults_.empty()) {
			ImGui::TextColored(
				ImVec4(0.35f, 0.85f, 0.45f, 1.0f),
				"All Prefab assets passed load and reference validation."
			);
		} else {
			if (ImGui::BeginChild(
				"PrefabAssetValidationResults",
				ImVec2(0.0f, 200.0f),
				ImGuiChildFlags_Borders
			)) {
				for (size_t resultIndex = 0;
					resultIndex < prefabAssetValidationResults_.size();
					++resultIndex) {
					const PrefabAssetValidationResult& result =
						prefabAssetValidationResults_[resultIndex];
					const std::string fileName = PathToUtf8(
						PathFromUtf8(result.filePath).filename()
					);
					ImGui::PushID(static_cast<int>(resultIndex));
					if (ImGui::SmallButton("Select Asset")) {
						SelectPrefabAssetInProject(result.filePath);
					}
					ImGui::SameLine();
					ImGui::TextColored(
						result.error
							? ImVec4(0.95f, 0.35f, 0.3f, 1.0f)
							: ImVec4(0.95f, 0.65f, 0.25f, 1.0f),
						"[%s] %s",
						result.error ? "Error" : "Warning",
						fileName.c_str()
					);
					ImGui::TextWrapped("%s", result.message.c_str());
					ImGui::PopID();
				}
			}
			ImGui::EndChild();
		}
	}

	ImGui::SeparatorText("Nested Sources");
	if (dependencies.empty()) {
		ImGui::TextDisabled("No Nested Prefab dependencies.");
	}
	ImGui::PushID("PrefabDiagnosticDependencies");
	for (size_t dependencyIndex = 0;
		dependencyIndex < dependencies.size();
		++dependencyIndex) {
		const DependencyStatus& dependency = dependencies[dependencyIndex];
		const std::string displayPath = dependency.resolvedPath.empty()
			? dependency.reference.fallbackPath
			: dependency.resolvedPath;
		const std::string displayName = displayPath.empty()
			? "Missing Prefab"
			: PathToUtf8(PathFromUtf8(displayPath).filename());
		ImGui::PushID(static_cast<int>(dependencyIndex));
		if (document.FindEntity(dependency.entityId)) {
			if (ImGui::SmallButton("Select")) {
				prefabSelectedEntityId_ = dependency.entityId;
			}
			ImGui::SameLine();
		}
		if (dependency.resolvedPath.empty()) {
			ImGui::TextColored(
				ImVec4(0.95f, 0.35f, 0.3f, 1.0f),
				"[Error] %s: missing or ambiguous",
				displayName.c_str()
			);
		} else {
			ImGui::TextUnformatted(displayName.c_str());
			if (ImGui::IsItemHovered()) {
				ImGui::SetTooltip("%s", dependency.resolvedPath.c_str());
			}
			if (dependency.reference.assetId.empty()) {
				ImGui::SameLine();
				ImGui::TextColored(
					ImVec4(0.95f, 0.65f, 0.25f, 1.0f),
					"[Warning] Path-only reference"
				);
			}
		}
		ImGui::PopID();
	}
	ImGui::PopID();

	ImGui::SeparatorText("Document Validation");
	if (issues.empty() && !assetIdMissing && !variantBaseMissing) {
		ImGui::TextColored(
			ImVec4(0.35f, 0.85f, 0.45f, 1.0f),
			"No document validation issues."
		);
		return;
	}
	if (assetIdMissing) {
		ImGui::TextColored(
			ImVec4(0.95f, 0.35f, 0.3f, 1.0f),
			"[Error] Prefab Asset ID is missing."
		);
	}
	if (variantBaseMissing) {
		ImGui::TextColored(
			ImVec4(0.95f, 0.35f, 0.3f, 1.0f),
			"[Error] Variant Base cannot be resolved."
		);
	}
	ImGui::PushID("PrefabDiagnosticIssues");
	for (size_t issueIndex = 0; issueIndex < issues.size(); ++issueIndex) {
		const SceneValidationIssue& issue = issues[issueIndex];
		ImGui::PushID(static_cast<int>(issueIndex));
		if (issue.entityId != 0 && document.FindEntity(issue.entityId)) {
			if (ImGui::SmallButton("Select")) {
				prefabSelectedEntityId_ = issue.entityId;
			}
			ImGui::SameLine();
		}
		const bool isError = issue.severity == SceneValidationSeverity::Error;
		ImGui::TextColored(
			isError
				? ImVec4(0.95f, 0.35f, 0.3f, 1.0f)
				: ImVec4(0.95f, 0.65f, 0.25f, 1.0f),
			"[%s] %s",
			isError ? "Error" : "Warning",
			issue.message.c_str()
		);
		ImGui::PopID();
	}
	ImGui::PopID();
}

void ImGuiManager::DrawPrefabWindow() {
	if (!prefabEditorSession_) {
		showPrefab_ = false;
		return;
	}

	const bool requestPrefabFocus = prefabFocusFramesRemaining_ > 0;
	if (requestPrefabFocus) {
		ImGui::SetNextWindowFocus();
	}
	bool windowOpen = true;
	const bool prefabWindowContentsVisible = ImGui::Begin(
		"Prefab",
		&windowOpen
	);
	if (requestPrefabFocus) {
		ImGui::SetWindowFocus("Prefab");
		--prefabFocusFramesRemaining_;
	}
	prefabKeyboardFocusThisFrame_ |= ImGui::IsWindowFocused(
		ImGuiFocusedFlags_RootAndChildWindows
	);
	if (!prefabWindowContentsVisible) {
		ImGui::End();
		if (windowOpen && prefabEditorSession_->IsOpen()) {
			prefabEditorSession_->BeginEditFrame();
			DrawPrefabInspectorWindow();
			const bool editingInteractionActive =
				ImGui::IsAnyItemActive() ||
				ImGui::IsMouseDown(ImGuiMouseButton_Left) ||
				ImGui::IsMouseDown(ImGuiMouseButton_Right) ||
				ImGui::IsMouseDown(ImGuiMouseButton_Middle);
			prefabEditorSession_->EndEditFrame(!editingInteractionActive);
		}
		if (!windowOpen) {
			if (prefabEditorSession_->IsDirty()) {
				prefabClosePopupRequested_ = true;
			} else {
				prefabEditorSession_->Close(true);
				showPrefab_ = false;
				prefabSelectedEntityId_ = 0;
			}
		}
		if (!prefabEditorSession_->IsOpen()) {
			DrawPrefabInspectorWindow();
		}
		DrawPrefabOpenConfirmation();
		return;
	}

	const bool canNavigateBack = prefabNavigationIndex_ > 0;
	const bool canNavigateForward =
		prefabNavigationIndex_ >= 0 &&
		prefabNavigationIndex_ + 1 <
			static_cast<int>(prefabNavigationHistory_.size());
	ImGui::BeginDisabled(!canNavigateBack);
	if (ImGui::Button("<##PrefabBack")) {
		const int targetIndex = prefabNavigationIndex_ - 1;
		const std::string targetPath = PrefabAssetRegistry::ResolvePath(
			prefabNavigationHistory_[targetIndex]
		);
		if (targetPath.empty()) {
			prefabNavigationStatus_ =
				"Unable to resolve the previous Prefab asset.";
		} else {
			RequestOpenPrefab(targetPath, targetIndex);
		}
	}
	ImGui::EndDisabled();
	ImGui::SameLine();
	ImGui::BeginDisabled(!canNavigateForward);
	if (ImGui::Button(">##PrefabForward")) {
		const int targetIndex = prefabNavigationIndex_ + 1;
		const std::string targetPath = PrefabAssetRegistry::ResolvePath(
			prefabNavigationHistory_[targetIndex]
		);
		if (targetPath.empty()) {
			prefabNavigationStatus_ =
				"Unable to resolve the next Prefab asset.";
		} else {
			RequestOpenPrefab(targetPath, targetIndex);
		}
	}
	ImGui::EndDisabled();
	ImGui::SameLine();
	if (ImGui::Button("Scene")) {
		ImGui::SetWindowFocus("Scene");
	}
	std::string nestedBreadcrumbOpenPath;
	if (prefabEditorSession_->IsOpen()) {
		ImGui::SameLine();
		ImGui::TextDisabled(">");
		ImGui::SameLine();
		const std::string prefabFileName = PathToUtf8(
			PathFromUtf8(
				prefabEditorSession_->GetFilePath()
			).filename()
		);
		if (ImGui::Button(prefabFileName.c_str())) {
			SelectPrefabAssetInProject(
				prefabEditorSession_->GetFilePath()
			);
		}
		if (ImGui::IsItemHovered()) {
			ImGui::SetTooltip("Select this Prefab in Project");
		}

		const SceneDocument& prefabDocument =
			prefabEditorSession_->GetDocument();
		const SceneEntity* selectedPrefabEntity =
			prefabDocument.FindEntity(prefabSelectedEntityId_);
		if (selectedPrefabEntity) {
			const PrefabAssetReference currentPrefab{
				prefabDocument.GetAssetId(),
				prefabEditorSession_->GetFilePath()
			};
			std::unordered_set<std::string> displayedSources;
			for (size_t linkIndex = 0;
				linkIndex < selectedPrefabEntity->prefabLinks.size();
				++linkIndex) {
				const ScenePrefabLink& link =
					selectedPrefabEntity->prefabLinks[linkIndex];
				const PrefabAssetReference source{
					link.assetId,
					link.sourcePath
				};
				if (PrefabAssetRegistry::IsSameAsset(currentPrefab, source)) {
					continue;
				}
				const std::string sourceKey = !link.assetId.empty()
					? "id:" + link.assetId
					: "path:" + link.sourcePath;
				if (!displayedSources.insert(sourceKey).second) {
					continue;
				}
				const std::string sourcePath =
					PrefabAssetRegistry::ResolvePath(source);
				const std::string displayPath = sourcePath.empty()
					? link.sourcePath
					: sourcePath;
				const std::string displayName = displayPath.empty()
					? "Missing Prefab"
					: PathToUtf8(PathFromUtf8(displayPath).filename());
				ImGui::SameLine();
				ImGui::TextDisabled(">");
				ImGui::SameLine();
				ImGui::PushID(static_cast<int>(linkIndex));
				ImGui::BeginDisabled(sourcePath.empty());
				if (ImGui::Button(displayName.c_str())) {
					nestedBreadcrumbOpenPath = sourcePath;
				}
				ImGui::EndDisabled();
				if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
					ImGui::SetTooltip(
						"%s",
						sourcePath.empty()
							? "Prefab asset is missing or ambiguous."
							: sourcePath.c_str()
					);
				}
				ImGui::PopID();
			}
		}
	}
	if (!nestedBreadcrumbOpenPath.empty()) {
		RequestOpenPrefab(nestedBreadcrumbOpenPath);
		ImGui::End();
		DrawPrefabOpenConfirmation();
		return;
	}
	ImGui::Separator();

	if (!prefabEditorSession_->IsOpen()) {
		ImGui::TextDisabled(
			"Select a .prefab.json asset in Project and choose Open Prefab Editor."
		);
		if (!prefabNavigationStatus_.empty()) {
			ImGui::TextColored(
				ImVec4(0.95f, 0.35f, 0.3f, 1.0f),
				"%s",
				prefabNavigationStatus_.c_str()
			);
		}
		ImGui::End();
		DrawPrefabInspectorWindow();
		DrawPrefabOpenConfirmation();
		return;
	}

	std::string variantOpenRequest;
	static char variantFileName[192]{};
	static std::string variantOperationStatus;
	prefabEditorSession_->BeginEditFrame();
	ImGui::TextUnformatted(prefabEditorSession_->GetFilePath().c_str());
	if (prefabEditorSession_->IsDirty()) {
		ImGui::SameLine();
		ImGui::TextColored(ImVec4(0.95f, 0.65f, 0.25f, 1.0f), "Unsaved");
	}
	if (ImGui::Button("Save")) {
		if (prefabEditorSession_->Save()) {
			InvalidateProjectCache();
		}
	}
	ImGui::SameLine();
	ImGui::BeginDisabled(!prefabEditorSession_->CanUndo());
	if (ImGui::Button("Undo")) {
		prefabEditorSession_->Undo();
	}
	ImGui::EndDisabled();
	ImGui::SameLine();
	ImGui::BeginDisabled(!prefabEditorSession_->CanRedo());
	if (ImGui::Button("Redo")) {
		prefabEditorSession_->Redo();
	}
	ImGui::EndDisabled();
	ImGui::SameLine();
	ImGui::BeginDisabled(prefabEditorSession_->IsDirty());
	if (ImGui::Button("Reload")) {
		prefabEditorSession_->Reload();
		const SceneDocument& document = prefabEditorSession_->GetDocument();
		prefabSelectedEntityId_ = document.GetEntities().empty()
			? 0
			: document.GetEntities().front().id;
		++prefabPreviewFramingSerial_;
	}
	ImGui::EndDisabled();
	ImGui::SameLine();
	if (ImGui::Button("Inspector")) {
		showPrefabInspector_ = true;
		prefabInspectorFocusRequested_ = true;
	}
	ImGui::SameLine();
	if (ImGui::Button("Close")) {
		windowOpen = false;
	}
	ImGui::SameLine();
	ImGui::BeginDisabled(prefabEditorSession_->IsDirty());
	if (ImGui::Button("Create Variant")) {
		std::string sourceName = PathToUtf8(
			PathFromUtf8(prefabEditorSession_->GetFilePath()).filename()
		);
		if (sourceName.ends_with(".prefab.json")) {
			sourceName.resize(
				sourceName.size() - std::string(".prefab.json").size()
			);
		}
		const std::string defaultName = sourceName + " Variant.prefab.json";
		strncpy_s(
			variantFileName,
			sizeof(variantFileName),
			defaultName.c_str(),
			_TRUNCATE
		);
		variantOperationStatus.clear();
		ImGui::OpenPopup("Create Prefab Variant");
	}
	ImGui::EndDisabled();
	if (ImGui::BeginPopupModal(
		"Create Prefab Variant",
		nullptr,
		ImGuiWindowFlags_AlwaysAutoResize
	)) {
		ImGui::TextUnformatted(
			"Create a Variant that inherits from the open Prefab."
		);
		ImGui::SetNextItemWidth(360.0f);
		ImGui::InputText(
			"File Name",
			variantFileName,
			sizeof(variantFileName)
		);
		if (ImGui::Button("Create")) {
			std::string fileName = variantFileName;
			for (char& character : fileName) {
				if (
					static_cast<unsigned char>(character) < 32 ||
					std::strchr("<>:\"/\\|?*", character)
				) {
					character = '_';
				}
			}
			if (!fileName.ends_with(".prefab.json")) {
				fileName += ".prefab.json";
			}
			const std::filesystem::path sourcePath =
				EditableResourcePath::ResolveResource(
					PathFromUtf8(prefabEditorSession_->GetFilePath())
				).lexically_normal();
			const std::filesystem::path targetPath =
				sourcePath.parent_path() / PathFromUtf8(fileName);
			std::error_code existsError;
			if (std::filesystem::exists(targetPath, existsError)) {
				variantOperationStatus =
					"A Prefab with that file name already exists.";
			} else if (prefabEditorSession_->GetDocument().SaveAsPrefabVariant(
				PathToUtf8(targetPath),
				prefabEditorSession_->GetFilePath()
			)) {
				InvalidateProjectCache();
				variantOpenRequest = PathToUtf8(targetPath);
				variantOperationStatus.clear();
				ImGui::CloseCurrentPopup();
			} else {
				variantOperationStatus = "Failed to create Prefab Variant.";
			}
		}
		ImGui::SameLine();
		if (ImGui::Button("Cancel")) {
			variantOperationStatus.clear();
			ImGui::CloseCurrentPopup();
		}
		if (!variantOperationStatus.empty()) {
			ImGui::TextColored(
				ImVec4(0.95f, 0.35f, 0.3f, 1.0f),
				"%s",
				variantOperationStatus.c_str()
			);
		}
		ImGui::EndPopup();
	}

	SceneDocument& openPrefabDocument =
		prefabEditorSession_->GetDocument();
	if (openPrefabDocument.IsPrefabVariant()) {
		const std::string basePath = PrefabAssetRegistry::ResolvePath(
			openPrefabDocument.GetVariantBaseAssetId(),
			openPrefabDocument.GetVariantBasePath()
		);
		ImGui::TextDisabled(
			"Variant Base: %s",
			basePath.empty() ? "Missing or ambiguous" : basePath.c_str()
		);
		ImGui::SameLine();
		ImGui::BeginDisabled(basePath.empty());
		if (ImGui::SmallButton("Open Base")) {
			variantOpenRequest = basePath;
		}
		ImGui::SameLine();
		if (ImGui::SmallButton("Revert to Base")) {
			if (openPrefabDocument.RevertPrefabVariantToBase()) {
				prefabSelectedEntityId_ =
					openPrefabDocument.GetEntities().empty()
						? 0
						: openPrefabDocument.GetEntities().front().id;
				++prefabPreviewFramingSerial_;
				variantOperationStatus = "Reverted all Variant overrides.";
			} else {
				variantOperationStatus = "Failed to reload the Variant Base.";
			}
		}
		ImGui::EndDisabled();
	}
	if (!variantOperationStatus.empty()) {
		ImGui::TextWrapped("%s", variantOperationStatus.c_str());
	}
	if (!prefabEditorSession_->GetLastError().empty()) {
		ImGui::TextColored(
			ImVec4(0.95f, 0.35f, 0.3f, 1.0f),
			"%s",
			prefabEditorSession_->GetLastError().c_str()
		);
	}

	DrawPrefabDiagnostics();
	ImGui::Separator();
	DrawPrefabPreview();
	ImGui::Separator();
	const float prefabHierarchyHeight = std::clamp(
		ImGui::GetWindowHeight() * 0.5f,
		260.0f,
		640.0f
	);
	if (ImGui::BeginChild(
		"PrefabHierarchyPane",
		ImVec2(0.0f, prefabHierarchyHeight),
		ImGuiChildFlags_AlwaysUseWindowPadding |
			ImGuiChildFlags_Borders
	)) {
		ImGui::SeparatorText("Hierarchy");
		DrawPrefabHierarchy();
	}
	ImGui::EndChild();

	ImGui::End();
	DrawPrefabInspectorWindow();
	const bool editingInteractionActive =
		ImGui::IsAnyItemActive() ||
		ImGui::IsMouseDown(ImGuiMouseButton_Left) ||
		ImGui::IsMouseDown(ImGuiMouseButton_Right) ||
		ImGui::IsMouseDown(ImGuiMouseButton_Middle) ||
		ImGuizmo::IsUsing();
	prefabEditorSession_->EndEditFrame(!editingInteractionActive);
	if (!variantOpenRequest.empty()) {
		RequestOpenPrefab(variantOpenRequest);
	}

	if (!windowOpen) {
		if (prefabEditorSession_->IsDirty()) {
			prefabClosePopupRequested_ = true;
		} else {
			prefabEditorSession_->Close(true);
			showPrefab_ = false;
			prefabSelectedEntityId_ = 0;
		}
	}
	if (prefabClosePopupRequested_) {
		ImGui::OpenPopup("Close Prefab?");
		prefabClosePopupRequested_ = false;
	}
	if (ImGui::BeginPopupModal(
		"Close Prefab?",
		nullptr,
		ImGuiWindowFlags_AlwaysAutoResize
	)) {
		ImGui::TextUnformatted("The Prefab has unsaved changes.");
		if (ImGui::Button("Save and Close")) {
			if (prefabEditorSession_->Save()) {
				prefabEditorSession_->Close(true);
				showPrefab_ = false;
				prefabSelectedEntityId_ = 0;
				ImGui::CloseCurrentPopup();
			}
		}
		ImGui::SameLine();
		if (ImGui::Button("Discard")) {
			prefabEditorSession_->Close(true);
			showPrefab_ = false;
			prefabSelectedEntityId_ = 0;
			ImGui::CloseCurrentPopup();
		}
		ImGui::SameLine();
		if (ImGui::Button("Cancel")) {
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndPopup();
	}
	DrawPrefabOpenConfirmation();
}

void ImGuiManager::DrawPrefabInspectorWindow() {
	if (!showPrefabInspector_) {
		return;
	}

	if (prefabInspectorFocusRequested_) {
		ImGui::SetNextWindowFocus();
		prefabInspectorFocusRequested_ = false;
	}
	bool windowOpen = true;
	const bool inspectorContentsVisible = ImGui::Begin(
		"Prefab Inspector",
		&windowOpen
	);
	prefabKeyboardFocusThisFrame_ |= ImGui::IsWindowFocused(
		ImGuiFocusedFlags_RootAndChildWindows
	);
	if (!inspectorContentsVisible) {
		ImGui::End();
		showPrefabInspector_ = windowOpen;
		return;
	}

	if (!prefabEditorSession_ || !prefabEditorSession_->IsOpen()) {
		ImGui::TextDisabled("Open a Prefab to inspect its Entities.");
	} else {
		const std::string prefabFileName = PathToUtf8(
			PathFromUtf8(prefabEditorSession_->GetFilePath()).filename()
		);
		ImGui::TextDisabled("%s", prefabFileName.c_str());
		ImGui::Separator();
		DrawPrefabInspector();
	}

	ImGui::End();
	showPrefabInspector_ = windowOpen;
}

void ImGuiManager::DrawPrefabPreview() {
	if (!prefabEditorSession_ || !prefabEditorSession_->IsOpen()) {
		return;
	}

	const SceneDocument& document = prefabEditorSession_->GetDocument();
	ImGui::SeparatorText("Stage");
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

	if (ImGui::Button("Frame All")) {
		prefabPreviewYaw_ = 0.65f;
		prefabPreviewPitch_ = 0.25f;
		prefabPreviewZoom_ = 1.0f;
		++prefabPreviewFramingSerial_;
	}
	ImGui::SameLine();
	ImGui::PushID("PrefabStageTools");
	const char* operationLabels[] = { "W", "E", "R" };
	for (int operation = 0; operation < 3; ++operation) {
		if (operation > 0) {
			ImGui::SameLine();
		}
		if (gizmoOperation_ == operation) {
			ImGui::PushStyleColor(
				ImGuiCol_Button,
				ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive)
			);
		}
		if (ImGui::Button(operationLabels[operation], ImVec2(28.0f, 0.0f))) {
			gizmoOperation_ = operation;
		}
		if (gizmoOperation_ == operation) {
			ImGui::PopStyleColor();
		}
	}
	ImGui::SameLine();
	if (ImGui::Button(gizmoLocalMode_ ? "Local" : "World")) {
		gizmoLocalMode_ = !gizmoLocalMode_;
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
			"##SnapValue",
			snapValue,
			gizmoOperation_ == 1 ? 1.0f : 0.05f,
			0.01f,
			0.0f,
			"%.2f"
		);
	}
	ImGui::PopID();
	ImGui::SameLine();
	ImGui::TextDisabled("LMB: Select | RMB: Orbit | Wheel: Zoom");
	ImGui::PushID("PrefabStageOverlays");
	ImGui::Checkbox("Skeleton", &prefabPreviewShowSkeleton_);
	ImGui::SameLine();
	ImGui::BeginDisabled(!prefabPreviewShowSkeleton_);
	ImGui::Checkbox("Joint Axes", &prefabPreviewShowJointAxes_);
	ImGui::EndDisabled();
	ImGui::SameLine();
	ImGui::Checkbox("Colliders", &prefabPreviewShowColliders_);
	ImGui::SameLine();
	ImGui::Checkbox("Hit/Hurt", &prefabPreviewShowCombatVolumes_);
	ImGui::SameLine();
	if (ImGui::Checkbox("HitBox Setup", &prefabHitBoxSetupMode_)) {
		if (prefabHitBoxSetupMode_) {
			// Colliderの基準位置はAuthoring Poseで編集する。Preview Poseを
			// 維持したままでは親の回転によりLocal Offsetを判断しづらい。
			prefabAnimationPreviewPlaying_ = false;
			prefabAnimationPreviewActive_ = false;
			prefabAttackPreviewMode_ = false;
			playerCombatPreviewEnabled_ = false;
			playerCombatPreviewStatus_.clear();
		}
	}
	ImGui::SameLine();
	if (ImGui::Checkbox("Attack Preview", &prefabAttackPreviewMode_)) {
		if (prefabAttackPreviewMode_) {
			// Attack PreviewはAnimator Poseを正とするため、Base Pose編集とは排他にする。
			prefabHitBoxSetupMode_ = false;
			prefabAnimationPreviewPlaying_ = false;
			prefabAnimationPreviewActive_ = true;
		}
	}
	if (prefabHitBoxSetupMode_) {
		ImGui::SameLine();
		ImGui::TextDisabled("Base Pose");
		const SceneDocument& document = prefabEditorSession_->GetDocument();
		const SceneEntity* owner = document.FindEntity(
			prefabAnimationPreviewOwnerEntityId_
		);
		const SceneComponent* animator = owner
			? FindEnabledComponent(*owner, "PrefabAnimator")
			: nullptr;
		if (!animator) {
			for (const SceneEntity& candidate : document.GetEntities()) {
				if (const SceneComponent* candidateAnimator =
					FindEnabledComponent(candidate, "PrefabAnimator")) {
					owner = &candidate;
					animator = candidateAnimator;
					prefabAnimationPreviewOwnerEntityId_ = candidate.id;
					prefabAnimationPreviewClipIndex_ = 0;
					break;
				}
			}
		}
		if (animator && !animator->prefabAnimationClips.empty()) {
			prefabAnimationPreviewClipIndex_ = std::clamp(
				prefabAnimationPreviewClipIndex_,
				0,
				static_cast<int>(animator->prefabAnimationClips.size() - 1)
			);
			ImGui::SameLine();
			ImGui::Checkbox("Ghost", &prefabHitBoxGhostVisible_);
			ImGui::SameLine();
			const ScenePrefabAnimationClip& ghostClip =
				animator->prefabAnimationClips[prefabAnimationPreviewClipIndex_];
			ImGui::SetNextItemWidth(140.0f);
			if (ImGui::BeginCombo("##HitBoxGhostClip", ghostClip.name.c_str())) {
				for (size_t index = 0;
					index < animator->prefabAnimationClips.size();
					++index) {
					const bool selected = static_cast<int>(index) ==
						prefabAnimationPreviewClipIndex_;
					if (ImGui::Selectable(
						animator->prefabAnimationClips[index].name.c_str(),
						selected
					)) {
						prefabAnimationPreviewClipIndex_ = static_cast<int>(index);
						prefabHitBoxGhostTime_ = 0.0f;
					}
				}
				ImGui::EndCombo();
			}
			const ScenePrefabAnimationClip& selectedGhostClip =
				animator->prefabAnimationClips[prefabAnimationPreviewClipIndex_];
			ImGui::SameLine();
			ImGui::SetNextItemWidth(100.0f);
			ImGui::BeginDisabled(!prefabHitBoxGhostVisible_);
			ImGui::SliderFloat(
				"##HitBoxGhostTime",
				&prefabHitBoxGhostTime_,
				0.0f,
				(std::max)(selectedGhostClip.duration, 0.001f),
				"Ghost %.2f s"
			);
			ImGui::EndDisabled();
		} else {
			ImGui::SameLine();
			ImGui::TextDisabled("No PrefabAnimator for Ghost");
		}
	}
	ImGui::SameLine();
	if (ImGui::Checkbox("Grid", &prefabGridVisible_)) {
		SaveEditorSettings();
	}
	ImGui::SameLine();
	if (ImGui::Checkbox("Axis", &prefabAxisVisible_)) {
		SaveEditorSettings();
	}
	ImGui::PopID();

	const float availableWidth = (std::max)(
		ImGui::GetContentRegionAvail().x,
		240.0f
	);
	const float previewHeight = std::clamp(
		availableWidth * 9.0f / 16.0f,
		220.0f,
		440.0f
	);
	// Wheel scrolling is resolved before widgets are submitted. Keep this child
	// scrollable as the wheel target, then reset it so only Stage zoom changes.
	ImGui::SetNextWindowScroll(ImVec2(0.0f, 0.0f));
	if (!ImGui::BeginChild(
		"PrefabStagePreview",
		ImVec2(0.0f, previewHeight),
		ImGuiChildFlags_Borders,
		ImGuiWindowFlags_NoScrollbar
	)) {
		ImGui::EndChild();
		return;
	}

	const ImVec2 imageSize = ImGui::GetContentRegionAvail();
	const float imageStartCursorY = ImGui::GetCursorPosY();
	const bool stageHovered = ImGui::IsWindowHovered(
		ImGuiHoveredFlags_AllowWhenBlockedByActiveItem
	);
	if (stageHovered) {
		// Image has no Item ID, so use a stable Stage ID to own the wheel.
		ImGui::SetKeyOwner(
			ImGuiKey_MouseWheelY,
			ImGui::GetID("PrefabStageWheelOwner"),
			ImGuiInputFlags_LockThisFrame
		);
		const float wheel = ImGui::GetIO().MouseWheel;
		if (wheel != 0.0f) {
			prefabPreviewZoom_ = std::clamp(
				prefabPreviewZoom_ * (1.0f - wheel * 0.1f),
				0.02f,
				1.75f
			);
		}
	}
	const ImVec2 framebufferScale = ImGui::GetIO().DisplayFramebufferScale;
	prefabPreviewRequestedWidth_ = static_cast<uint32_t>(std::clamp(
		imageSize.x * framebufferScale.x,
		320.0f,
		1600.0f
	));
	prefabPreviewRequestedHeight_ = static_cast<uint32_t>(std::clamp(
		imageSize.y * framebufferScale.y,
		180.0f,
		900.0f
	));

	const bool previewReady =
		prefabPreviewTexture_.ptr != 0 &&
		prefabPreviewCameraValid_ &&
		prefabPreviewRenderedPath_ == prefabPreviewRequestedPath_ &&
		prefabPreviewRenderedRevision_ == prefabPreviewRequestedRevision_;
	if (previewReady) {
		const ImVec2 imageMin = ImGui::GetCursorScreenPos();
		ImGui::Image(
			ImTextureRef(static_cast<ImTextureID>(prefabPreviewTexture_.ptr)),
			imageSize
		);
		const bool imageHovered = ImGui::IsItemHovered();
		if (imageHovered) {
			const ImGuiIO& io = ImGui::GetIO();
			if (ImGui::IsMouseDragging(ImGuiMouseButton_Right)) {
				prefabPreviewYaw_ += io.MouseDelta.x * 0.01f;
				prefabPreviewPitch_ = std::clamp(
					prefabPreviewPitch_ + io.MouseDelta.y * 0.01f,
					-1.45f,
					1.45f
				);
			}
		}
		if (!prefabPreviewRequestUsesCombatRig_) {
			DrawPrefabGizmo(imageMin.x, imageMin.y, imageSize.x, imageSize.y);
		}
		if (prefabAxisVisible_) {
			DrawWorldAxisIndicator(
				imageMin,
				ImVec2(
					imageMin.x + imageSize.x,
					imageMin.y + imageSize.y
				),
				prefabPreviewViewMatrix_
			);
		}
		if (prefabPreviewRequestUsesCombatRig_) {
			ImGui::GetWindowDrawList()->AddText(
				ImVec2(imageMin.x + 8.0f, imageMin.y + 8.0f),
				IM_COL32(130, 220, 150, 255),
				"Combat Rig Preview: read-only composition."
			);
		} else if (prefabAnimationPreviewActive_) {
			ImGui::GetWindowDrawList()->AddText(
				ImVec2(imageMin.x + 8.0f, imageMin.y + 8.0f),
				IM_COL32(130, 220, 150, 255),
				"Animation Preview: Gizmo writes a key at the current time."
			);
		}
		if (
			!prefabPreviewRequestUsesCombatRig_ &&
			imageHovered &&
			ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
			!ImGuizmo::IsOver() &&
			!ImGuizmo::IsUsing()
		) {
			PickPrefabEntity(imageMin.x, imageMin.y, imageSize.x, imageSize.y);
		}
	} else {
		const char* message = document.GetEntities().empty()
			? "Prefab has no Entities."
			: "Preparing Prefab Preview...";
		const ImVec2 textSize = ImGui::CalcTextSize(message);
		ImGui::SetCursorPos({
			(std::max)((imageSize.x - textSize.x) * 0.5f, 0.0f),
			(std::max)((imageSize.y - textSize.y) * 0.5f, 0.0f)
		});
		ImGui::TextDisabled("%s", message);
	}
	// A tiny hidden overflow makes this child the ImGui wheel target. Its scroll
	// is reset before BeginChild, so the preview image and Gizmo never shift.
	ImGui::SetCursorPosY(imageStartCursorY + imageSize.y + 1.0f);
	ImGui::Dummy(ImVec2(0.0f, 1.0f));
	ImGui::EndChild();
	DrawPrefabAnimationTimeline();
}

void ImGuiManager::DrawWorldAxisIndicator(
	const ImVec2& imageMin,
	const ImVec2& imageMax,
	const Matrix4x4& viewMatrix
) const {
	const float width = imageMax.x - imageMin.x;
	const float height = imageMax.y - imageMin.y;
	if (width < 80.0f || height < 80.0f) {
		return;
	}

	// View MatrixでWorld軸をCamera空間へ変換するため、Cameraを回しても
	// 色とラベルが常にWorld X/Y/Zの向きを示す。表示専用で入力は受け取らない。
	const ImVec2 center(imageMax.x - 34.0f, imageMax.y - 34.0f);
	constexpr float kAxisLength = 22.0f;
	constexpr float kBackgroundRadius = 27.0f;
	ImDrawList* drawList = ImGui::GetWindowDrawList();
	drawList->AddCircleFilled(center, kBackgroundRadius, IM_COL32(12, 15, 19, 190));
	drawList->AddCircle(center, kBackgroundRadius, IM_COL32(210, 220, 230, 170));

	struct AxisStyle {
		const char* label;
		ImU32 color;
		Vector3 worldDirection;
	};
	const AxisStyle axes[] = {
		{ "X", IM_COL32(235, 78, 78, 255), { 1.0f, 0.0f, 0.0f } },
		{ "Y", IM_COL32(98, 210, 105, 255), { 0.0f, 1.0f, 0.0f } },
		{ "Z", IM_COL32(83, 145, 245, 255), { 0.0f, 0.0f, 1.0f } }
	};
	for (const AxisStyle& axis : axes) {
		const float viewX =
			axis.worldDirection.x * viewMatrix.m[0][0] +
			axis.worldDirection.y * viewMatrix.m[1][0] +
			axis.worldDirection.z * viewMatrix.m[2][0];
		const float viewY =
			axis.worldDirection.x * viewMatrix.m[0][1] +
			axis.worldDirection.y * viewMatrix.m[1][1] +
			axis.worldDirection.z * viewMatrix.m[2][1];
		const ImVec2 direction(viewX, -viewY);
		const float directionLength = std::sqrt(
			direction.x * direction.x + direction.y * direction.y
		);
		const ImVec2 endpoint(
			center.x + direction.x * kAxisLength,
			center.y + direction.y * kAxisLength
		);
		drawList->AddLine(center, endpoint, axis.color, 2.5f);
		drawList->AddCircleFilled(endpoint, 3.5f, axis.color);

		const ImVec2 labelSize = ImGui::CalcTextSize(axis.label);
		const ImVec2 labelOffset = directionLength > 0.001f
			? ImVec2(
				direction.x / directionLength * 5.0f,
				direction.y / directionLength * 5.0f
			)
			: ImVec2(4.0f, 4.0f);
		drawList->AddText(
			ImVec2(
				endpoint.x + labelOffset.x - labelSize.x * 0.5f,
				endpoint.y + labelOffset.y - labelSize.y * 0.5f
			),
			axis.color,
			axis.label
		);
	}
}

void ImGuiManager::HandleEditShortcuts() {
	const ImGuiIO& io = ImGui::GetIO();
	if (
		!io.KeyCtrl ||
		io.WantTextInput ||
		ImGui::IsAnyItemActive()
	) {
		return;
	}

	const bool saveRequested = ImGui::IsKeyPressed(ImGuiKey_S, false);
	const bool undoRequested =
		!io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_Z, false);
	const bool redoRequested =
		ImGui::IsKeyPressed(ImGuiKey_Y, false) ||
		(io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_Z, false));
	if (!saveRequested && !undoRequested && !redoRequested) {
		return;
	}

	if (
		prefabKeyboardFocusThisFrame_ &&
		prefabEditorSession_ &&
		prefabEditorSession_->IsOpen()
	) {
		if (saveRequested && prefabEditorSession_->Save()) {
			InvalidateProjectCache();
		}
		if (undoRequested) {
			prefabEditorSession_->Undo();
		}
		if (redoRequested) {
			prefabEditorSession_->Redo();
		}
		return;
	}

	if (!editorSession_ || !editorSession_->IsEditing()) {
		return;
	}
	if (saveRequested) {
		editorSession_->Save();
	}
	if (undoRequested) {
		editorSession_->Undo();
	}
	if (redoRequested) {
		editorSession_->Redo();
	}
}

void ImGuiManager::DrawSceneDebugLabels(
	const ImVec2& imageMin,
	const ImVec2& imageMax,
	const Matrix4x4& viewProjectionMatrix
) const {
	const float width = imageMax.x - imageMin.x;
	const float height = imageMax.y - imageMin.y;
	if (width <= 1.0f || height <= 1.0f) {
		return;
	}

	const std::vector<DebugRenderer::WorldLabel>& labels =
		DebugRenderer::GetInstance()->GetWorldLabels();
	if (labels.empty()) {
		return;
	}

	ImDrawList* drawList = ImGui::GetWindowDrawList();
	drawList->PushClipRect(imageMin, imageMax, true);
	for (const DebugRenderer::WorldLabel& label : labels) {
		const Vector3& position = label.position;
		const float clipX =
			position.x * viewProjectionMatrix.m[0][0] +
			position.y * viewProjectionMatrix.m[1][0] +
			position.z * viewProjectionMatrix.m[2][0] +
			viewProjectionMatrix.m[3][0];
		const float clipY =
			position.x * viewProjectionMatrix.m[0][1] +
			position.y * viewProjectionMatrix.m[1][1] +
			position.z * viewProjectionMatrix.m[2][1] +
			viewProjectionMatrix.m[3][1];
		const float clipZ =
			position.x * viewProjectionMatrix.m[0][2] +
			position.y * viewProjectionMatrix.m[1][2] +
			position.z * viewProjectionMatrix.m[2][2] +
			viewProjectionMatrix.m[3][2];
		const float clipW =
			position.x * viewProjectionMatrix.m[0][3] +
			position.y * viewProjectionMatrix.m[1][3] +
			position.z * viewProjectionMatrix.m[2][3] +
			viewProjectionMatrix.m[3][3];
		if (clipW <= 0.0001f || clipZ < 0.0f || clipZ > clipW) {
			continue;
		}

		const float ndcX = clipX / clipW;
		const float ndcY = clipY / clipW;
		if (ndcX < -1.0f || ndcX > 1.0f || ndcY < -1.0f || ndcY > 1.0f) {
			continue;
		}
		const ImVec2 screenPosition{
			imageMin.x + (ndcX + 1.0f) * 0.5f * width,
			imageMin.y + (1.0f - ndcY) * 0.5f * height
		};
		drawList->AddText(
			ImVec2(screenPosition.x + 7.0f, screenPosition.y - 7.0f),
			ImGui::ColorConvertFloat4ToU32(ImVec4(
				label.color.x,
				label.color.y,
				label.color.z,
				label.color.w
			)),
			label.text.c_str()
		);
	}
	drawList->PopClipRect();
}

void ImGuiManager::DrawPrefabAnimationTimeline() {
	if (!prefabEditorSession_ || !prefabEditorSession_->IsOpen()) {
		return;
	}
	if (prefabHitBoxSetupMode_) {
		ImGui::SeparatorText("Prefab Animator Timeline");
		ImGui::TextDisabled(
			"HitBox Setup uses the Authoring Pose. Disable HitBox Setup to preview animation."
		);
		return;
	}

	const std::string& assetPath = prefabEditorSession_->GetFilePath();
	if (prefabAnimationPreviewAssetPath_ != assetPath) {
		prefabAnimationPreviewAssetPath_ = assetPath;
		prefabAnimationPreviewSourceRevision_ = 0;
		prefabAnimationPreviewOwnerEntityId_ = 0;
		prefabAnimationPreviewClipIndex_ = 0;
		prefabAnimationPreviewTime_ = 0.0f;
		prefabAnimationPreviewPlaying_ = false;
		prefabAnimationPreviewActive_ = false;
		prefabAttackPreviewIndex_ = 0;
		prefabTransformPoseAddTime_ = 0.0f;
		prefabTransformPoseStatus_.clear();
	}

	const SceneDocument& document = prefabEditorSession_->GetDocument();
	struct AnimatorEntry {
		const SceneEntity* entity = nullptr;
		const SceneComponent* component = nullptr;
	};
	std::vector<AnimatorEntry> animators;
	for (const SceneEntity& entity : document.GetEntities()) {
		if (const SceneComponent* component =
			FindEnabledComponent(entity, "PrefabAnimator")) {
			animators.push_back({ &entity, component });
		}
	}

	ImGui::SeparatorText("Prefab Animator Timeline");
	if (animators.empty()) {
		prefabAnimationPreviewPlaying_ = false;
		prefabAnimationPreviewActive_ = false;
		ImGui::TextDisabled("Add an enabled PrefabAnimator to preview clips.");
		return;
	}

	int animatorIndex = -1;
	for (size_t index = 0; index < animators.size(); ++index) {
		if (animators[index].entity->id == prefabAnimationPreviewOwnerEntityId_) {
			animatorIndex = static_cast<int>(index);
			break;
		}
	}
	if (animatorIndex < 0) {
		animatorIndex = 0;
		prefabAnimationPreviewOwnerEntityId_ = animators.front().entity->id;
		prefabAnimationPreviewClipIndex_ = 0;
		prefabAnimationPreviewTime_ = 0.0f;
		prefabAnimationPreviewPlaying_ = false;
		prefabAnimationPreviewActive_ = false;
		prefabAttackPreviewIndex_ = 0;
		prefabTransformPoseAddTime_ = 0.0f;
		prefabTransformPoseStatus_.clear();
	}

	if (ImGui::BeginCombo(
		"Animator",
		animators[animatorIndex].entity->name.c_str()
	)) {
		for (size_t index = 0; index < animators.size(); ++index) {
			const bool selected = static_cast<int>(index) == animatorIndex;
			ImGui::PushID(static_cast<int>(animators[index].entity->id));
			if (ImGui::Selectable(
				animators[index].entity->name.c_str(),
				selected
			)) {
				animatorIndex = static_cast<int>(index);
				prefabAnimationPreviewOwnerEntityId_ =
					animators[index].entity->id;
				prefabAnimationPreviewClipIndex_ = 0;
				prefabAnimationPreviewTime_ = 0.0f;
				prefabAnimationPreviewPlaying_ = false;
				prefabAnimationPreviewActive_ =
					!animators[index].component->prefabAnimationClips.empty();
				prefabAttackPreviewIndex_ = 0;
				prefabTransformPoseAddTime_ = 0.0f;
				prefabTransformPoseStatus_.clear();
			}
			ImGui::PopID();
		}
		ImGui::EndCombo();
	}

	const AnimatorEntry& animatorEntry = animators[animatorIndex];
	const std::vector<ScenePrefabAnimationClip>& clips =
		animatorEntry.component->prefabAnimationClips;
	if (clips.empty()) {
		prefabAnimationPreviewPlaying_ = false;
		prefabAnimationPreviewActive_ = false;
		ImGui::TextDisabled("The selected PrefabAnimator has no clips.");
		return;
	}

	const SceneComponent* attackSet = FindEnabledComponent(
		*animatorEntry.entity,
		"AttackSet"
	);
	if (prefabAttackPreviewMode_) {
		if (!attackSet || attackSet->attackDefinitions.empty()) {
			ImGui::TextDisabled(
				"Attack Preview requires an AttackSet on the selected Animator."
			);
		} else {
			prefabAttackPreviewIndex_ = std::clamp(
				prefabAttackPreviewIndex_,
				0,
				static_cast<int>(attackSet->attackDefinitions.size() - 1)
			);
			const SceneAttackDefinition& selectedAttack =
				attackSet->attackDefinitions[prefabAttackPreviewIndex_];
			if (ImGui::BeginCombo("Attack", selectedAttack.name.c_str())) {
				for (size_t index = 0;
					index < attackSet->attackDefinitions.size();
					++index) {
					const bool selected = static_cast<int>(index) ==
						prefabAttackPreviewIndex_;
					if (ImGui::Selectable(
						attackSet->attackDefinitions[index].name.c_str(),
						selected
					)) {
						prefabAttackPreviewIndex_ = static_cast<int>(index);
					}
				}
				ImGui::EndCombo();
			}
			const SceneAttackDefinition& attack =
				attackSet->attackDefinitions[prefabAttackPreviewIndex_];
			auto clipEntry = std::find_if(
				clips.begin(),
				clips.end(),
				[&attack](const ScenePrefabAnimationClip& candidate) {
					return candidate.name == attack.animation;
				}
			);
			if (clipEntry == clips.end()) {
				ImGui::TextDisabled(
					"Attack animation '%s' was not found.",
					attack.animation.c_str()
				);
			} else {
				const int attackClipIndex = static_cast<int>(
					std::distance(clips.begin(), clipEntry)
				);
				if (prefabAnimationPreviewClipIndex_ != attackClipIndex) {
					prefabAnimationPreviewClipIndex_ = attackClipIndex;
					prefabAnimationPreviewTime_ = 0.0f;
					prefabAnimationPreviewPlaying_ = false;
				}
				prefabAnimationPreviewActive_ = true;
			}
		}
	}

	prefabAnimationPreviewClipIndex_ = std::clamp(
		prefabAnimationPreviewClipIndex_,
		0,
		static_cast<int>(clips.size() - 1)
	);
	const char* clipPreview =
		clips[prefabAnimationPreviewClipIndex_].name.empty()
			? "(Unnamed Clip)"
			: clips[prefabAnimationPreviewClipIndex_].name.c_str();
	ImGui::BeginDisabled(prefabAttackPreviewMode_);
	if (ImGui::BeginCombo("Clip", clipPreview)) {
		for (size_t index = 0; index < clips.size(); ++index) {
			const bool selected =
				static_cast<int>(index) == prefabAnimationPreviewClipIndex_;
			const char* label = clips[index].name.empty()
				? "(Unnamed Clip)"
				: clips[index].name.c_str();
			ImGui::PushID(static_cast<int>(index));
			if (ImGui::Selectable(label, selected)) {
				prefabAnimationPreviewClipIndex_ = static_cast<int>(index);
				prefabSelectedEntityId_ = animatorEntry.entity->id;
				prefabAnimationPreviewTime_ = 0.0f;
				prefabAnimationPreviewPlaying_ = false;
				prefabAnimationPreviewActive_ = true;
				prefabTransformPoseAddTime_ = 0.0f;
				prefabTransformPoseStatus_.clear();
			}
			ImGui::PopID();
		}
		ImGui::EndCombo();
	}
	ImGui::EndDisabled();
	ImGui::SameLine();
	ImGui::Checkbox("Clip Focus", &prefabClipFocusEnabled_);
	if (ImGui::IsItemHovered()) {
		ImGui::SetTooltip(
			"When the Animator owner is selected, show only this Clip and its Attack."
		);
	}

	const ScenePrefabAnimationClip& clip =
		clips[prefabAnimationPreviewClipIndex_];
	const float duration = (std::max)(clip.duration, 0.001f);
	const SceneAttackDefinition* timelineAttack = nullptr;
	if (attackSet) {
		auto found = std::find_if(
			attackSet->attackDefinitions.begin(),
			attackSet->attackDefinitions.end(),
			[&clip](const SceneAttackDefinition& attack) {
				return attack.animation == clip.name;
			}
		);
		if (found != attackSet->attackDefinitions.end()) {
			timelineAttack = &*found;
		}
	}
	ImGui::SameLine();
	if (ImGui::Checkbox("Combat Rig Preview", &playerCombatPreviewEnabled_)) {
		playerCombatPreviewStatus_.clear();
	}
	if (playerCombatPreviewEnabled_) {
		if (!editorSession_ || !editorSession_->IsEditing()) {
			playerCombatPreviewStatus_ = "Open the title Scene in Edit mode to use Combat Rig Preview.";
		} else {
			const SceneDocument& scene = editorSession_->GetEditDocument();
			struct RigCandidate { uint64_t rootId; uint64_t weaponId; };
			std::vector<RigCandidate> candidates;
			for (const SceneEntity& weapon : scene.GetEntities()) {
				if (!FindEnabledComponent(weapon, "PrefabAnimator") ||
					!FindEnabledComponent(weapon, "AttackSet")) {
					continue;
				}
				const SceneEntity* root = scene.FindEntity(weapon.parentId);
				if (root) candidates.push_back({ root->id, weapon.id });
			}
			if (candidates.empty()) {
				playerCombatPreviewStatus_ = "No PlayerWeapon instance with Animator and AttackSet was found.";
			} else {
				auto selected = std::find_if(
					candidates.begin(), candidates.end(), [this](const RigCandidate& candidate) {
						return candidate.rootId == playerCombatPreviewRootId_ &&
							candidate.weaponId == playerCombatPreviewWeaponId_;
					}
				);
				if (selected == candidates.end()) {
					selected = candidates.begin();
					playerCombatPreviewRootId_ = selected->rootId;
					playerCombatPreviewWeaponId_ = selected->weaponId;
				}
				const SceneEntity* selectedRoot = scene.FindEntity(selected->rootId);
				const SceneEntity* selectedWeapon = scene.FindEntity(selected->weaponId);
				const std::string label = (selectedRoot ? selectedRoot->name : "Missing") +
					" / " + (selectedWeapon ? selectedWeapon->name : "Missing");
				if (ImGui::BeginCombo("Rig Source", label.c_str())) {
					for (const RigCandidate& candidate : candidates) {
						const SceneEntity* root = scene.FindEntity(candidate.rootId);
						const SceneEntity* weapon = scene.FindEntity(candidate.weaponId);
						const std::string candidateLabel =
							(root ? root->name : "Missing") + " / " +
							(weapon ? weapon->name : "Missing");
						if (ImGui::Selectable(
							candidateLabel.c_str(), candidate.weaponId == selected->weaponId
						)) {
							playerCombatPreviewRootId_ = candidate.rootId;
							playerCombatPreviewWeaponId_ = candidate.weaponId;
							playerCombatPreviewStatus_.clear();
						}
					}
					ImGui::EndCombo();
				}
			}
		}
		if (!playerCombatPreviewStatus_.empty()) {
			ImGui::TextDisabled("%s", playerCombatPreviewStatus_.c_str());
		}
	}
	static constexpr int kPreviewFrameRates[] = { 30, 60, 120 };
	const int previewFrameRate = std::clamp(
		prefabAnimationPreviewFrameRate_,
		kPreviewFrameRates[0],
		kPreviewFrameRates[2]
	);
	prefabAnimationPreviewFrameRate_ = previewFrameRate;
	const int previewFrameCount = (std::max)(
		1,
		static_cast<int>(std::ceil(duration * static_cast<float>(previewFrameRate)))
	);
	const auto frameToTime = [&](int frame) {
		return (std::min)(
			static_cast<float>(std::clamp(frame, 0, previewFrameCount)) /
				static_cast<float>(previewFrameRate),
			duration
		);
	};
	const auto timeToFrame = [&](float time) {
		return std::clamp(
			static_cast<int>(std::round(time * static_cast<float>(previewFrameRate))),
			0,
			previewFrameCount
		);
	};
	const auto snapPreviewTimeToFrame = [&]() {
		if (prefabAnimationPreviewSnapToFrames_) {
			prefabAnimationPreviewTime_ = frameToTime(
				timeToFrame(prefabAnimationPreviewTime_)
			);
		}
	};
	const auto evaluateDistanceEasedMotionProgress = [&](float time) {
		if (!timelineAttack) {
			return 0.0f;
		}
		const float activeDuration = (std::max)(timelineAttack->activeTime, 0.0001f);
		float progress = std::clamp(
			(time - timelineAttack->windup) / activeDuration,
			0.0f,
			1.0f
		);
		if (timelineAttack->motionEasing == "EaseOut") {
			return Math::EaseOutCubic(progress);
		}
		if (timelineAttack->motionEasing == "EaseIn") {
			return progress * progress * progress;
		}
		if (timelineAttack->motionEasing == "EaseInOut") {
			return progress < 0.5f
				? 4.0f * progress * progress * progress
				: 1.0f - std::pow(-2.0f * progress + 2.0f, 3.0f) * 0.5f;
		}
		return timelineAttack->motionEasing == "Linear"
			? progress
			: Math::SmoothStep(progress);
	};
	prefabAnimationPreviewTime_ = std::clamp(
		prefabAnimationPreviewTime_,
		0.0f,
		duration
	);
	if (prefabAnimationPreviewPlaying_) {
		prefabAnimationPreviewActive_ = true;
		if (prefabAnimationPreviewSnapToFrames_) {
			const float frameDuration = 1.0f / static_cast<float>(previewFrameRate);
			prefabAnimationPreviewFrameAccumulator_ += (std::max)(
				ImGui::GetIO().DeltaTime,
				0.0f
			);
			while (prefabAnimationPreviewFrameAccumulator_ >= frameDuration) {
				prefabAnimationPreviewFrameAccumulator_ -= frameDuration;
				const int nextFrame = timeToFrame(prefabAnimationPreviewTime_) + 1;
				if (nextFrame > previewFrameCount) {
					if (clip.loop) {
						prefabAnimationPreviewTime_ = 0.0f;
					} else {
						prefabAnimationPreviewTime_ = duration;
						prefabAnimationPreviewPlaying_ = false;
						prefabAnimationPreviewFrameAccumulator_ = 0.0f;
						break;
					}
				} else {
					prefabAnimationPreviewTime_ = frameToTime(nextFrame);
				}
			}
		} else {
			prefabAnimationPreviewTime_ += (std::max)(
				ImGui::GetIO().DeltaTime,
				0.0f
			);
			if (clip.loop) {
				prefabAnimationPreviewTime_ = std::fmod(
					prefabAnimationPreviewTime_,
					duration
				);
			} else if (prefabAnimationPreviewTime_ >= duration) {
				prefabAnimationPreviewTime_ = duration;
				prefabAnimationPreviewPlaying_ = false;
			}
		}
	}

	if (ImGui::Button(prefabAnimationPreviewPlaying_ ? "Pause" : "Play")) {
		if (
			!prefabAnimationPreviewPlaying_ &&
			!clip.loop &&
			prefabAnimationPreviewTime_ >= duration
		) {
			prefabAnimationPreviewTime_ = 0.0f;
		}
		prefabAnimationPreviewPlaying_ = !prefabAnimationPreviewPlaying_;
		prefabAnimationPreviewFrameAccumulator_ = 0.0f;
		prefabAnimationPreviewActive_ = true;
	}
	ImGui::SameLine();
	if (ImGui::Button("Stop")) {
		prefabAnimationPreviewPlaying_ = false;
		prefabAnimationPreviewTime_ = 0.0f;
		prefabAnimationPreviewFrameAccumulator_ = 0.0f;
		prefabAnimationPreviewActive_ = true;
	}
	ImGui::SameLine();
	if (ImGui::Button("Reset Pose")) {
		prefabAnimationPreviewPlaying_ = false;
		prefabAnimationPreviewTime_ = 0.0f;
		prefabAnimationPreviewFrameAccumulator_ = 0.0f;
		prefabAnimationPreviewActive_ = false;
	}
	ImGui::SameLine();
	ImGui::TextDisabled(
		prefabAnimationPreviewActive_ ? "Preview Pose" : "Authoring Pose"
	);

	if (ImGui::SliderFloat(
		"Time",
		&prefabAnimationPreviewTime_,
		0.0f,
		duration,
		"%.3f s"
	)) {
		prefabAnimationPreviewPlaying_ = false;
		prefabAnimationPreviewActive_ = true;
		snapPreviewTimeToFrame();
	}
	if (ImGui::BeginCombo("Preview FPS", (std::to_string(previewFrameRate) + " FPS").c_str())) {
		for (const int frameRate : kPreviewFrameRates) {
			const bool selected = frameRate == previewFrameRate;
			const std::string label = std::to_string(frameRate) + " FPS";
			if (ImGui::Selectable(label.c_str(), selected)) {
				prefabAnimationPreviewFrameRate_ = frameRate;
				prefabAnimationPreviewFrameAccumulator_ = 0.0f;
				const int selectedFrame = std::clamp(
					static_cast<int>(std::round(
						prefabAnimationPreviewTime_ * static_cast<float>(frameRate)
					)),
					0,
					(std::max)(1, static_cast<int>(std::ceil(
						duration * static_cast<float>(frameRate)
					)))
				);
				prefabAnimationPreviewTime_ = (std::min)(
					static_cast<float>(selectedFrame) / static_cast<float>(frameRate),
					duration
				);
			}
		}
		ImGui::EndCombo();
	}
	ImGui::Checkbox("Frame Snap", &prefabAnimationPreviewSnapToFrames_);
	if (prefabAnimationPreviewSnapToFrames_) {
		snapPreviewTimeToFrame();
	}
	int previewFrame = timeToFrame(prefabAnimationPreviewTime_);
	if (ImGui::Button("< Frame")) {
		prefabAnimationPreviewTime_ = frameToTime(previewFrame - 1);
		prefabAnimationPreviewPlaying_ = false;
		prefabAnimationPreviewActive_ = true;
	}
	ImGui::SameLine();
	if (ImGui::Button("Frame >")) {
		prefabAnimationPreviewTime_ = frameToTime(previewFrame + 1);
		prefabAnimationPreviewPlaying_ = false;
		prefabAnimationPreviewActive_ = true;
	}
	ImGui::SameLine();
	if (ImGui::DragInt("Frame", &previewFrame, 1.0f, 0, previewFrameCount)) {
		prefabAnimationPreviewTime_ = frameToTime(previewFrame);
		prefabAnimationPreviewPlaying_ = false;
		prefabAnimationPreviewActive_ = true;
	}

	const float timelineWidth = (std::max)(
		ImGui::GetContentRegionAvail().x,
		280.0f
	);
	const auto makeTrackLabel = [&](const SceneAnimationTrack& track) {
		const SceneEntity* target = track.targetEntityId != 0
			? document.FindEntity(track.targetEntityId)
			: nullptr;
		if (!target && !track.targetEntityName.empty()) {
			target = document.FindEntityByName(track.targetEntityName);
		}
		if (!target) {
			target = animatorEntry.entity;
		}
		const std::string targetName = target
			? target->name
			: std::string("Missing Target");
		return targetName + " / " + track.property;
	};
	float desiredLabelWidth = 120.0f;
	for (const SceneAnimationTrack& track : clip.tracks) {
		desiredLabelWidth = (std::max)(
			desiredLabelWidth,
			ImGui::CalcTextSize(makeTrackLabel(track).c_str()).x + 12.0f
		);
	}
	if (timelineAttack) {
		desiredLabelWidth = (std::max)(
			desiredLabelWidth,
			ImGui::CalcTextSize("Player Motion / Distance Eased").x + 12.0f
		);
		desiredLabelWidth = (std::max)(
			desiredLabelWidth,
			ImGui::CalcTextSize(
				("Effect / " + timelineAttack->name).c_str()
			).x + 12.0f
		);
	}
	const float maximumLabelWidth = (std::max)(
		120.0f,
		(std::min)(280.0f, timelineWidth - 160.0f)
	);
	const float labelWidth = std::clamp(
		desiredLabelWidth,
		120.0f,
		maximumLabelWidth
	);
	const float headerHeight = 24.0f;
	const float rowHeight = 24.0f;
	const size_t hitWindowCount = timelineAttack
		? timelineAttack->hitWindows.size()
		: 0;
	const size_t effectEventRowCount = timelineAttack &&
		!timelineAttack->effectEvents.empty()
		? size_t{ 1 }
		: size_t{ 0 };
	const size_t motionRowCount = timelineAttack ? size_t{ 1 } : size_t{ 0 };
	const size_t rowCount = (std::max)(
		clip.tracks.size() + hitWindowCount + effectEventRowCount + motionRowCount,
		size_t{ 1 }
	);
	const float timelineHeight = headerHeight + rowHeight * rowCount;
	const ImVec2 timelineOrigin = ImGui::GetCursorScreenPos();
	ImGui::InvisibleButton(
		"##PrefabAnimationTimeline",
		ImVec2(timelineWidth, timelineHeight),
		ImGuiButtonFlags_MouseButtonLeft
	);
	const float trackLeft = timelineOrigin.x + labelWidth;
	const float trackRight = timelineOrigin.x + timelineWidth;
	const float trackWidth = (std::max)(trackRight - trackLeft, 1.0f);
	if (
		ImGui::IsItemActive() &&
		ImGui::GetMousePos().x >= trackLeft
	) {
		const float amount = std::clamp(
			(ImGui::GetMousePos().x - trackLeft) / trackWidth,
			0.0f,
			1.0f
		);
		prefabAnimationPreviewTime_ = amount * duration;
		snapPreviewTimeToFrame();
		prefabAnimationPreviewPlaying_ = false;
		prefabAnimationPreviewActive_ = true;
	}
	if (
		ImGui::IsItemHovered() &&
		ImGui::GetMousePos().x >= trackLeft
	) {
		ImGui::SetItemTooltip(
			"Drag to seek | %.3f / %.3f s",
			prefabAnimationPreviewTime_,
			duration
		);
	}

	ImDrawList* drawList = ImGui::GetWindowDrawList();
	drawList->AddRectFilled(
		timelineOrigin,
		ImVec2(trackRight, timelineOrigin.y + timelineHeight),
		IM_COL32(27, 30, 34, 255)
	);
	drawList->AddLine(
		ImVec2(trackLeft, timelineOrigin.y),
		ImVec2(trackLeft, timelineOrigin.y + timelineHeight),
		IM_COL32(82, 88, 96, 255)
	);
	for (int division = 0; division <= 5; ++division) {
		const float amount = static_cast<float>(division) / 5.0f;
		const float x = trackLeft + trackWidth * amount;
		drawList->AddLine(
			ImVec2(x, timelineOrigin.y + headerHeight),
			ImVec2(x, timelineOrigin.y + timelineHeight),
			IM_COL32(58, 63, 70, 255)
		);
		char timeLabel[32]{};
		std::snprintf(timeLabel, sizeof(timeLabel), "%.2f", duration * amount);
		drawList->AddText(
			ImVec2(x + 3.0f, timelineOrigin.y + 4.0f),
			IM_COL32(165, 172, 182, 255),
			timeLabel
		);
	}

	for (size_t trackIndex = 0; trackIndex < clip.tracks.size(); ++trackIndex) {
		const SceneAnimationTrack& track = clip.tracks[trackIndex];
		const float rowTop =
			timelineOrigin.y + headerHeight + rowHeight * trackIndex;
		const float rowCenter = rowTop + rowHeight * 0.5f;
		if ((trackIndex & 1u) != 0u) {
			drawList->AddRectFilled(
				ImVec2(timelineOrigin.x, rowTop),
				ImVec2(trackRight, rowTop + rowHeight),
				IM_COL32(35, 39, 44, 255)
			);
		}
		const SceneEntity* target = track.targetEntityId != 0
			? document.FindEntity(track.targetEntityId)
			: nullptr;
		if (!target && !track.targetEntityName.empty()) {
			target = document.FindEntityByName(track.targetEntityName);
		}
		if (!target) {
			target = animatorEntry.entity;
		}
		const std::string rowLabel = makeTrackLabel(track);
		const ImVec2 labelMin(timelineOrigin.x, rowTop);
		const ImVec2 labelMax(trackLeft - 4.0f, rowTop + rowHeight);
		drawList->PushClipRect(labelMin, labelMax, true);
		drawList->AddText(
			ImVec2(timelineOrigin.x + 5.0f, rowTop + 4.0f),
			IM_COL32(215, 220, 228, 255),
			rowLabel.c_str()
		);
		drawList->PopClipRect();
		if (ImGui::IsMouseHoveringRect(labelMin, labelMax)) {
			ImGui::SetTooltip("%s", rowLabel.c_str());
		}

		ImU32 keyColor = IM_COL32(75, 170, 255, 255);
		ImU32 activeColor = IM_COL32(80, 205, 120, 190);
		if (target && FindEnabledComponent(*target, "HitBox")) {
			activeColor = IM_COL32(255, 70, 45, 205);
		} else if (target && FindEnabledComponent(*target, "HurtBox")) {
			activeColor = IM_COL32(45, 190, 255, 205);
		}
		if (track.property == "Active" && !track.keyframes.empty()) {
			keyColor = activeColor;
			for (size_t keyIndex = 0;
				keyIndex < track.keyframes.size();
				++keyIndex) {
				const float startTime = keyIndex == 0
					? 0.0f
					: std::clamp(
						track.keyframes[keyIndex].time,
						0.0f,
						duration
					);
				const float endTime = keyIndex + 1 < track.keyframes.size()
					? std::clamp(
						track.keyframes[keyIndex + 1].time,
						0.0f,
						duration
					)
					: duration;
				if (
					track.keyframes[keyIndex].value.x < 0.5f ||
					endTime <= startTime
				) {
					continue;
				}
				drawList->AddRectFilled(
					ImVec2(
						trackLeft + trackWidth * (startTime / duration),
						rowCenter - 6.0f
					),
					ImVec2(
						trackLeft + trackWidth * (endTime / duration),
						rowCenter + 6.0f
					),
					activeColor,
					2.0f
				);
			}
		}
		for (const SceneAnimationKeyframe& keyframe : track.keyframes) {
			const float keyTime = std::clamp(
				keyframe.time,
				0.0f,
				duration
			);
			const float keyX = trackLeft + trackWidth * (keyTime / duration);
			drawList->AddCircleFilled(
				ImVec2(keyX, rowCenter),
				3.5f,
				keyColor
			);
		}
	}

	if (timelineAttack) {
		for (size_t windowIndex = 0;
			windowIndex < timelineAttack->hitWindows.size();
			++windowIndex) {
			const SceneAttackHitWindow& window =
				timelineAttack->hitWindows[windowIndex];
			const size_t rowIndex = clip.tracks.size() + windowIndex;
			const float rowTop =
				timelineOrigin.y + headerHeight + rowHeight * rowIndex;
			const float rowCenter = rowTop + rowHeight * 0.5f;
			if ((rowIndex & 1u) != 0u) {
				drawList->AddRectFilled(
					ImVec2(timelineOrigin.x, rowTop),
					ImVec2(trackRight, rowTop + rowHeight),
					IM_COL32(35, 39, 44, 255)
				);
			}
			const SceneEntity* hitBox = window.hitBoxEntityId != 0
				? document.FindEntity(window.hitBoxEntityId)
				: nullptr;
			if (!hitBox && !window.hitBoxEntityName.empty()) {
				hitBox = document.FindEntityByName(window.hitBoxEntityName);
			}
			const std::string targetName = hitBox
				? hitBox->name
				: std::string("StateMachine HitBox");
			const std::string rowLabel = "Hit / " + timelineAttack->name +
				" / " + targetName;
			const ImVec2 labelMin(timelineOrigin.x, rowTop);
			const ImVec2 labelMax(trackLeft - 4.0f, rowTop + rowHeight);
			drawList->PushClipRect(labelMin, labelMax, true);
			drawList->AddText(
				ImVec2(timelineOrigin.x + 5.0f, rowTop + 4.0f),
				IM_COL32(255, 183, 90, 255),
				rowLabel.c_str()
			);
			drawList->PopClipRect();
			const float startTime = std::clamp(window.startTime, 0.0f, duration);
			const float endTime = std::clamp(window.endTime, startTime, duration);
			const bool active = prefabAnimationPreviewTime_ >= startTime &&
				prefabAnimationPreviewTime_ < endTime;
			const ImU32 hitColor = active
				? IM_COL32(255, 92, 48, 245)
				: IM_COL32(220, 92, 42, 185);
			drawList->AddRectFilled(
				ImVec2(
					trackLeft + trackWidth * (startTime / duration),
					rowCenter - 6.0f
				),
				ImVec2(
					trackLeft + trackWidth * (endTime / duration),
					rowCenter + 6.0f
				),
				hitColor,
				2.0f
			);
			if (ImGui::IsMouseHoveringRect(
				labelMin,
				ImVec2(trackRight, rowTop + rowHeight)
			)) {
				if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
					prefabAnimationPreviewTime_ = startTime;
					prefabAnimationPreviewPlaying_ = false;
					prefabAnimationPreviewActive_ = true;
				}
				ImGui::SetTooltip(
					"%s | %.3f - %.3f s | Damage %.1f | Poise %.1f | Knockback %.1f",
					rowLabel.c_str(),
					window.startTime,
					window.endTime,
					window.damage,
					window.poiseDamage,
					window.knockback
				);
			}
		}

		if (effectEventRowCount != 0) {
			const size_t rowIndex = clip.tracks.size() + hitWindowCount;
			const float rowTop =
				timelineOrigin.y + headerHeight + rowHeight * rowIndex;
			const float rowCenter = rowTop + rowHeight * 0.5f;
			if ((rowIndex & 1u) != 0u) {
				drawList->AddRectFilled(
					ImVec2(timelineOrigin.x, rowTop),
					ImVec2(trackRight, rowTop + rowHeight),
					IM_COL32(35, 39, 44, 255)
				);
			}
			const std::string rowLabel = "Effect / " + timelineAttack->name;
			const ImVec2 labelMin(timelineOrigin.x, rowTop);
			const ImVec2 labelMax(trackLeft - 4.0f, rowTop + rowHeight);
			drawList->PushClipRect(labelMin, labelMax, true);
			drawList->AddText(
				ImVec2(timelineOrigin.x + 5.0f, rowTop + 4.0f),
				IM_COL32(135, 224, 244, 255),
				rowLabel.c_str()
			);
			drawList->PopClipRect();
			for (size_t effectIndex = 0;
				effectIndex < timelineAttack->effectEvents.size();
				++effectIndex) {
				const SceneAttackEffectEvent& effect =
					timelineAttack->effectEvents[effectIndex];
				const float effectTime = std::clamp(effect.time, 0.0f, duration);
				const float effectX = trackLeft + trackWidth * (effectTime / duration);
				const ImVec2 markerMin(effectX - 6.0f, rowCenter - 6.0f);
				const ImVec2 markerMax(effectX + 6.0f, rowCenter + 6.0f);
				drawList->AddQuadFilled(
					ImVec2(effectX, rowCenter - 6.0f),
					ImVec2(effectX + 6.0f, rowCenter),
					ImVec2(effectX, rowCenter + 6.0f),
					ImVec2(effectX - 6.0f, rowCenter),
					IM_COL32(82, 205, 236, 245)
				);
				if (ImGui::IsMouseHoveringRect(markerMin, markerMax)) {
					if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
						prefabAnimationPreviewTime_ = effectTime;
						snapPreviewTimeToFrame();
						prefabAnimationPreviewPlaying_ = false;
						prefabAnimationPreviewActive_ = true;
					}
					ImGui::SetTooltip(
						"Effect Event %zu | %.3f s | %s",
						effectIndex + 1,
						effect.time,
						effect.groundEffectType.empty()
							? "Particle"
							: effect.groundEffectType.c_str()
					);
				}
			}
		}

		const size_t rowIndex = clip.tracks.size() + hitWindowCount + effectEventRowCount;
		const float rowTop = timelineOrigin.y + headerHeight + rowHeight * rowIndex;
		const float rowCenter = rowTop + rowHeight * 0.5f;
		if ((rowIndex & 1u) != 0u) {
			drawList->AddRectFilled(
				ImVec2(timelineOrigin.x, rowTop),
				ImVec2(trackRight, rowTop + rowHeight),
				IM_COL32(35, 39, 44, 255)
			);
		}
		const std::string motionLabel = "Player Motion / Distance Eased";
		const ImVec2 labelMin(timelineOrigin.x, rowTop);
		const ImVec2 labelMax(trackLeft - 4.0f, rowTop + rowHeight);
		drawList->PushClipRect(labelMin, labelMax, true);
		drawList->AddText(
			ImVec2(timelineOrigin.x + 5.0f, rowTop + 4.0f),
			IM_COL32(150, 215, 255, 255),
			motionLabel.c_str()
		);
		drawList->PopClipRect();
		const float motionStart = std::clamp(timelineAttack->windup, 0.0f, duration);
		const float motionEnd = std::clamp(
			timelineAttack->windup + timelineAttack->activeTime,
			motionStart,
			duration
		);
		const int firstMotionFrame = timeToFrame(motionStart);
		const int lastMotionFrame = timeToFrame(motionEnd);
		float maxMotionSpeed = 0.0f;
		for (int frame = firstMotionFrame; frame < lastMotionFrame; ++frame) {
			const float firstTime = frameToTime(frame);
			const float secondTime = frameToTime(frame + 1);
			const float deltaTime = secondTime - firstTime;
			if (deltaTime <= 0.000001f) { continue; }
			const float deltaProgress = evaluateDistanceEasedMotionProgress(secondTime) -
				evaluateDistanceEasedMotionProgress(firstTime);
			const float distance = std::sqrt(
				timelineAttack->forwardDistance * timelineAttack->forwardDistance +
				timelineAttack->sideDistance * timelineAttack->sideDistance
			) * std::abs(deltaProgress);
			maxMotionSpeed = (std::max)(maxMotionSpeed, distance / deltaTime);
		}
		for (int frame = firstMotionFrame; frame < lastMotionFrame; ++frame) {
			const float firstTime = frameToTime(frame);
			const float secondTime = frameToTime(frame + 1);
			const float deltaTime = secondTime - firstTime;
			if (deltaTime <= 0.000001f) { continue; }
			const float deltaProgress = evaluateDistanceEasedMotionProgress(secondTime) -
				evaluateDistanceEasedMotionProgress(firstTime);
			const float distance = std::sqrt(
				timelineAttack->forwardDistance * timelineAttack->forwardDistance +
				timelineAttack->sideDistance * timelineAttack->sideDistance
			) * std::abs(deltaProgress);
			const float speedRate = maxMotionSpeed > 0.000001f
				? std::clamp((distance / deltaTime) / maxMotionSpeed, 0.0f, 1.0f)
				: 0.0f;
			const ImU32 speedColor = IM_COL32(
				static_cast<int>(70.0f + 120.0f * speedRate),
				static_cast<int>(130.0f + 100.0f * speedRate),
				255,
				220
			);
			drawList->AddRectFilled(
				ImVec2(trackLeft + trackWidth * (firstTime / duration), rowCenter - 6.0f),
				ImVec2(trackLeft + trackWidth * (secondTime / duration), rowCenter + 6.0f),
				speedColor,
				1.0f
			);
		}
		if (ImGui::IsMouseHoveringRect(labelMin, ImVec2(trackRight, rowTop + rowHeight))) {
			const float progress = evaluateDistanceEasedMotionProgress(prefabAnimationPreviewTime_);
			const float previousTime = frameToTime((std::max)(previewFrame - 1, 0));
			const float previewDeltaTime = prefabAnimationPreviewTime_ - previousTime;
			const float previewDeltaProgress = progress -
				evaluateDistanceEasedMotionProgress(previousTime);
			const float previewDistance = std::sqrt(
				timelineAttack->forwardDistance * timelineAttack->forwardDistance +
				timelineAttack->sideDistance * timelineAttack->sideDistance
			) * std::abs(previewDeltaProgress);
			const float previewSpeed = previewDeltaTime > 0.000001f
				? previewDistance / previewDeltaTime
				: 0.0f;
			ImGui::SetTooltip(
				"Frame %d | Local X %.3f, Z %.3f | XZ Speed %.3f units/s",
				previewFrame,
				timelineAttack->sideDistance * progress,
				timelineAttack->forwardDistance * progress,
				previewSpeed
			);
		}
	}

	const float playheadX = trackLeft + trackWidth *
		(prefabAnimationPreviewTime_ / duration);
	drawList->AddLine(
		ImVec2(playheadX, timelineOrigin.y),
		ImVec2(playheadX, timelineOrigin.y + timelineHeight),
		IM_COL32(255, 215, 70, 255),
		2.0f
	);
	RebuildPrefabAnimationPreviewDocument();
}

void ImGuiManager::DrawPrefabTransformPoseInspector(SceneEntity& entity) {
	if (!prefabEditorSession_ || !prefabEditorSession_->IsOpen()) {
		return;
	}

	SceneDocument& document = prefabEditorSession_->GetDocument();
	SceneEntity* owner = document.FindEntity(
		prefabAnimationPreviewOwnerEntityId_
	);
	SceneComponent* animator = owner
		? FindComponent(*owner, "PrefabAnimator")
		: nullptr;
	if (
		!animator ||
		!animator->enabled ||
		prefabAnimationPreviewClipIndex_ < 0 ||
		prefabAnimationPreviewClipIndex_ >=
			static_cast<int>(animator->prefabAnimationClips.size())
	) {
		return;
	}

	ScenePrefabAnimationClip& clip =
		animator->prefabAnimationClips[prefabAnimationPreviewClipIndex_];
	const float duration = (std::max)(clip.duration, 0.001f);
	constexpr float kKeyTimeTolerance = 0.005f;
	constexpr float kRadiansToDegrees = 57.2957795f;
	constexpr float kDegreesToRadians = 0.0174532925f;

	const auto resolveTrackTargetId = [&](const SceneAnimationTrack& track) {
		if (track.targetEntityId != 0) {
			if (const SceneEntity* byId =
				document.FindEntity(track.targetEntityId)) {
				return byId->id;
			}
		}
		if (!track.targetEntityName.empty()) {
			if (const SceneEntity* byName =
				document.FindEntityByName(track.targetEntityName)) {
				return byName->id;
			}
		}
		return owner->id;
	};
	const auto propertyIndex = [](const std::string& property) {
		if (property == "LocalPosition") {
			return 0;
		}
		if (property == "LocalRotation") {
			return 1;
		}
		if (property == "LocalScale") {
			return 2;
		}
		return -1;
	};
	const auto propertyName = [](int index) -> const char* {
		return index == 0
			? "LocalPosition"
			: index == 1 ? "LocalRotation" : "LocalScale";
	};

	struct PoseKeyRef {
		int trackIndex = -1;
		int keyIndex = -1;
	};
	struct TransformPose {
		float time = 0.0f;
		PoseKeyRef keys[3];
	};
	struct TimedKeyRef {
		float time = 0.0f;
		int property = -1;
		int trackIndex = -1;
		int keyIndex = -1;
	};

	std::vector<TimedKeyRef> transformKeys;
	for (size_t trackIndex = 0; trackIndex < clip.tracks.size(); ++trackIndex) {
		const SceneAnimationTrack& track = clip.tracks[trackIndex];
		const int property = propertyIndex(track.property);
		if (property < 0 || resolveTrackTargetId(track) != entity.id) {
			continue;
		}
		for (size_t keyIndex = 0; keyIndex < track.keyframes.size(); ++keyIndex) {
			transformKeys.push_back({
				track.keyframes[keyIndex].time,
				property,
				static_cast<int>(trackIndex),
				static_cast<int>(keyIndex)
			});
		}
	}
	std::stable_sort(
		transformKeys.begin(),
		transformKeys.end(),
		[](const TimedKeyRef& left, const TimedKeyRef& right) {
			return left.time < right.time;
		}
	);

	std::vector<TransformPose> poses;
	for (const TimedKeyRef& key : transformKeys) {
		if (
			poses.empty() ||
			std::abs(poses.back().time - key.time) > kKeyTimeTolerance
		) {
			poses.push_back({});
			poses.back().time = key.time;
		}
		PoseKeyRef& destination = poses.back().keys[key.property];
		if (destination.trackIndex < 0) {
			destination.trackIndex = key.trackIndex;
			destination.keyIndex = key.keyIndex;
		}
	}

	const auto authoringValue = [&](int property) {
		return property == 0
			? entity.transform.translate
			: property == 1
				? MakeEulerFromQuaternion(entity.transform.rotate)
				: entity.transform.scale;
	};
	const auto resolvePoseValue = [&](const TransformPose& pose, int property) {
		const PoseKeyRef& key = pose.keys[property];
		if (key.trackIndex >= 0 && key.keyIndex >= 0) {
			return clip.tracks[key.trackIndex].keyframes[key.keyIndex].value;
		}

		Vector3 value = authoringValue(property);
		float latestTime = -1.0f;
		for (size_t trackIndex = 0;
			trackIndex < clip.tracks.size();
			++trackIndex) {
			const SceneAnimationTrack& track = clip.tracks[trackIndex];
			if (
				propertyIndex(track.property) != property ||
				resolveTrackTargetId(track) != entity.id
			) {
				continue;
			}
			for (const SceneAnimationKeyframe& keyframe : track.keyframes) {
				if (
					keyframe.time <= pose.time + kKeyTimeTolerance &&
					keyframe.time > latestTime
				) {
					latestTime = keyframe.time;
					value = keyframe.value;
				}
			}
		}
		return value;
	};
	const auto ensureTrack = [&](int property) {
		for (size_t trackIndex = 0;
			trackIndex < clip.tracks.size();
			++trackIndex) {
			SceneAnimationTrack& track = clip.tracks[trackIndex];
			if (
				track.property == propertyName(property) &&
				resolveTrackTargetId(track) == entity.id
			) {
				return static_cast<int>(trackIndex);
			}
		}
		SceneAnimationTrack track{};
		track.targetEntityId = entity.id;
		track.targetEntityName = entity.name;
		track.property = propertyName(property);
		clip.tracks.push_back(std::move(track));
		return static_cast<int>(clip.tracks.size() - 1);
	};
	const auto upsertKey = [&](int property, float time, const Vector3& value) {
		SceneAnimationTrack& track = clip.tracks[ensureTrack(property)];
		for (SceneAnimationKeyframe& keyframe : track.keyframes) {
			if (std::abs(keyframe.time - time) <= kKeyTimeTolerance) {
				keyframe.value = value;
				return;
			}
		}
		track.keyframes.push_back({ time, value });
		std::stable_sort(
			track.keyframes.begin(),
			track.keyframes.end(),
			[](const SceneAnimationKeyframe& left,
				const SceneAnimationKeyframe& right) {
				return left.time < right.time;
			}
		);
	};
	const auto sortTrack = [&](int trackIndex) {
		if (trackIndex < 0) {
			return;
		}
		std::stable_sort(
			clip.tracks[trackIndex].keyframes.begin(),
			clip.tracks[trackIndex].keyframes.end(),
			[](const SceneAnimationKeyframe& left,
				const SceneAnimationKeyframe& right) {
				return left.time < right.time;
			}
		);
	};
	const auto activatePreviewAt = [&](float time) {
		prefabAnimationPreviewTime_ = std::clamp(time, 0.0f, duration);
		prefabAnimationPreviewPlaying_ = false;
		prefabAnimationPreviewActive_ = true;
	};

	ImGui::SeparatorText("Transform Poses");
	ImGui::TextDisabled("Model Forward: -Z");
	ImGui::SameLine();
	ImGui::TextDisabled("%s / %s", owner->name.c_str(), clip.name.c_str());
	prefabTransformPoseAddTime_ = std::clamp(
		prefabTransformPoseAddTime_,
		0.0f,
		duration
	);
	ImGui::SetNextItemWidth(120.0f);
	if (ImGui::DragFloat(
		"New Pose Time",
		&prefabTransformPoseAddTime_,
		0.01f,
		0.0f,
		duration,
		"%.3f s"
	)) {
		prefabTransformPoseStatus_.clear();
	}
	ImGui::SameLine();
	if (ImGui::SmallButton("Use Preview Time")) {
		prefabTransformPoseAddTime_ = std::clamp(
			prefabAnimationPreviewTime_,
			0.0f,
			duration
		);
		prefabTransformPoseStatus_.clear();
	}
	if (ImGui::Button("Add Pose")) {
		const float time = std::clamp(
			prefabTransformPoseAddTime_,
			0.0f,
			duration
		);
		const TransformPose* existingPose = nullptr;
		for (const TransformPose& pose : poses) {
			if (std::abs(pose.time - time) <= kKeyTimeTolerance) {
				existingPose = &pose;
				break;
			}
		}
		if (existingPose) {
			char message[160]{};
			std::snprintf(
				message,
				sizeof(message),
				"A Pose already exists at %.3f s. Choose another time or use Complete Pose.",
				existingPose->time
			);
			prefabTransformPoseStatus_ = message;
			activatePreviewAt(existingPose->time);
		} else {
			TransformPose newPose{};
			newPose.time = time;
			for (int property = 0; property < 3; ++property) {
				upsertKey(property, time, resolvePoseValue(newPose, property));
			}
			document.MarkDirty();
			activatePreviewAt(time);
			char message[96]{};
			std::snprintf(
				message,
				sizeof(message),
				"Added Transform Pose at %.3f s.",
				time
			);
			prefabTransformPoseStatus_ = message;
		}
	}
	ImGui::SameLine();
	ImGui::TextDisabled("Adds Position, Rotation, and Scale at New Pose Time.");
	if (!prefabTransformPoseStatus_.empty()) {
		ImGui::TextWrapped("%s", prefabTransformPoseStatus_.c_str());
	}
	std::string interpolationPreview;
	bool hasTransformTrack = false;
	bool mixedInterpolation = false;
	for (const SceneAnimationTrack& track : clip.tracks) {
		if (
			propertyIndex(track.property) < 0 ||
			resolveTrackTargetId(track) != entity.id
		) {
			continue;
		}
		const std::string easing = track.easing.empty()
			? "SmoothStep"
			: track.easing;
		if (!hasTransformTrack) {
			interpolationPreview = easing;
			hasTransformTrack = true;
		} else if (interpolationPreview != easing) {
			mixedInterpolation = true;
		}
	}
	if (mixedInterpolation) {
		interpolationPreview = "Mixed";
	}
	ImGui::BeginDisabled(!hasTransformTrack);
	if (ImGui::BeginCombo(
		"Default Easing (Transform Tracks)",
		interpolationPreview.empty() ? "SmoothStep" : interpolationPreview.c_str()
	)) {
		for (const char* easing : {
			"Linear", "EaseIn", "EaseOut", "EaseInOut", "SmoothStep"
		}) {
			if (ImGui::Selectable(easing, interpolationPreview == easing)) {
				for (SceneAnimationTrack& track : clip.tracks) {
					if (
						propertyIndex(track.property) >= 0 &&
						resolveTrackTargetId(track) == entity.id
					) {
						track.easing = easing;
					}
				}
				document.MarkDirty();
			}
		}
		ImGui::EndCombo();
	}
	ImGui::EndDisabled();

	if (poses.empty()) {
		ImGui::TextDisabled("No Transform Pose keys for the selected Entity.");
		return;
	}

	for (size_t poseIndex = 0; poseIndex < poses.size(); ++poseIndex) {
		const TransformPose& pose = poses[poseIndex];
		const bool complete =
			pose.keys[0].trackIndex >= 0 &&
			pose.keys[1].trackIndex >= 0 &&
			pose.keys[2].trackIndex >= 0;
		const bool hasNextPose = poseIndex + 1 < poses.size();
		const bool nextComplete = hasNextPose &&
			poses[poseIndex + 1].keys[0].trackIndex >= 0 &&
			poses[poseIndex + 1].keys[1].trackIndex >= 0 &&
			poses[poseIndex + 1].keys[2].trackIndex >= 0;
		ImGui::PushID(static_cast<int>(poseIndex));
		const char* state = complete ? "Transform Pose" : "Partial Pose";
		if (ImGui::TreeNodeEx(
			"Pose",
			ImGuiTreeNodeFlags_DefaultOpen,
			"%s  %.3f s",
			state,
			pose.time
		)) {
			float editedTime = pose.time;
			if (ImGui::DragFloat(
				"Time", &editedTime, 0.01f, 0.0f, duration, "%.3f s"
			)) {
				editedTime = std::clamp(editedTime, 0.0f, duration);
				for (const PoseKeyRef& key : pose.keys) {
					if (key.trackIndex >= 0 && key.keyIndex >= 0) {
						clip.tracks[key.trackIndex].keyframes[key.keyIndex].time = editedTime;
						sortTrack(key.trackIndex);
					}
				}
				document.MarkDirty();
				prefabAnimationPreviewTime_ = editedTime;
				prefabAnimationPreviewPlaying_ = false;
				prefabAnimationPreviewActive_ = true;
			}

			Vector3 position = resolvePoseValue(pose, 0);
			Vector3 rotationDegrees = resolvePoseValue(pose, 1);
			rotationDegrees.x *= kRadiansToDegrees;
			rotationDegrees.y *= kRadiansToDegrees;
			rotationDegrees.z *= kRadiansToDegrees;
			Vector3 scale = resolvePoseValue(pose, 2);
			if (ImGui::DragFloat3("Position", &position.x, 0.01f)) {
				upsertKey(0, pose.time, position);
				document.MarkDirty();
				activatePreviewAt(pose.time);
			}
			if (ImGui::DragFloat3("Rotation (Degrees)", &rotationDegrees.x, 0.1f)) {
				rotationDegrees.x *= kDegreesToRadians;
				rotationDegrees.y *= kDegreesToRadians;
				rotationDegrees.z *= kDegreesToRadians;
				upsertKey(1, pose.time, rotationDegrees);
				document.MarkDirty();
				activatePreviewAt(pose.time);
			}
			if (ImGui::DragFloat3("Scale", &scale.x, 0.01f)) {
				upsertKey(2, pose.time, scale);
				document.MarkDirty();
				activatePreviewAt(pose.time);
			}

			if (hasNextPose) {
				const TransformPose& nextPose = poses[poseIndex + 1];
				ImGui::SeparatorText("To Next Pose");
				ImGui::TextDisabled(
					"%.3f s -> %.3f s",
					pose.time,
					nextPose.time
				);
				if (!complete || !nextComplete) {
					ImGui::TextDisabled(
						"Complete both Poses to edit this interval."
					);
				} else {
					std::string segmentEasing;
					bool segmentEasingInitialized = false;
					bool mixedSegmentEasing = false;
					for (const PoseKeyRef& key : pose.keys) {
						const std::string& easing = clip.tracks[key.trackIndex]
							.keyframes[key.keyIndex].easingToNext;
						if (!segmentEasingInitialized) {
							segmentEasing = easing;
							segmentEasingInitialized = true;
						} else if (segmentEasing != easing) {
							mixedSegmentEasing = true;
						}
					}
					const char* segmentEasingPreview = mixedSegmentEasing
						? "Mixed"
						: segmentEasing.empty()
							? "Track Default"
							: segmentEasing.c_str();
					if (ImGui::BeginCombo(
						"Easing To Next",
						segmentEasingPreview
					)) {
						if (ImGui::Selectable(
							"Track Default",
							!mixedSegmentEasing && segmentEasing.empty()
						)) {
							for (const PoseKeyRef& key : pose.keys) {
								clip.tracks[key.trackIndex]
									.keyframes[key.keyIndex].easingToNext.clear();
							}
							document.MarkDirty();
							activatePreviewAt((pose.time + nextPose.time) * 0.5f);
						}
						for (const char* easing : {
							"Linear", "EaseIn", "EaseOut", "EaseInOut", "SmoothStep"
						}) {
							if (ImGui::Selectable(
								easing,
								!mixedSegmentEasing && segmentEasing == easing
							)) {
								for (const PoseKeyRef& key : pose.keys) {
									clip.tracks[key.trackIndex]
										.keyframes[key.keyIndex].easingToNext = easing;
								}
								document.MarkDirty();
								activatePreviewAt((pose.time + nextPose.time) * 0.5f);
							}
						}
						ImGui::EndCombo();
					}

					SceneAnimationKeyframe& positionStartKey =
						clip.tracks[pose.keys[0].trackIndex]
							.keyframes[pose.keys[0].keyIndex];
					ImGui::SetNextItemWidth(220.0f);
					if (ImGui::DragFloat3(
						"Position Bulge Offset",
						&positionStartKey.positionBulge.x,
						0.01f
					)) {
						document.MarkDirty();
						activatePreviewAt((pose.time + nextPose.time) * 0.5f);
					}
					if (ImGui::SmallButton("Reset Bulge")) {
						positionStartKey.positionBulge = {};
						document.MarkDirty();
						activatePreviewAt((pose.time + nextPose.time) * 0.5f);
					}
					ImGui::TextDisabled(
						"Local offset from the straight path at the interpolation midpoint."
		);
	}
			}

			if (!complete && ImGui::SmallButton("Complete Pose")) {
				for (int property = 0; property < 3; ++property) {
					if (pose.keys[property].trackIndex < 0) {
						upsertKey(
							property,
							pose.time,
							resolvePoseValue(pose, property)
						);
					}
				}
				document.MarkDirty();
				activatePreviewAt(pose.time);
			}
			if (!complete) {
				ImGui::SameLine();
				ImGui::TextDisabled("Missing Transform keys are kept unchanged.");
			}

			if (ImGui::SmallButton("Edit with Gizmo")) {
				prefabAnimationPreviewTime_ = pose.time;
				prefabAnimationPreviewPlaying_ = false;
				prefabAnimationPreviewActive_ = true;
			}
			ImGui::SameLine();
			if (ImGui::SmallButton("Duplicate")) {
				float duplicateTime = (std::min)(pose.time + 0.1f, duration);
				if (std::abs(duplicateTime - pose.time) <= kKeyTimeTolerance) {
					duplicateTime = (std::max)(pose.time - 0.1f, 0.0f);
				}
				if (std::abs(duplicateTime - pose.time) > kKeyTimeTolerance) {
					for (int property = 0; property < 3; ++property) {
						upsertKey(
							property,
							duplicateTime,
							resolvePoseValue(pose, property)
						);
					}
					document.MarkDirty();
					prefabAnimationPreviewTime_ = duplicateTime;
					prefabAnimationPreviewPlaying_ = false;
					prefabAnimationPreviewActive_ = true;
				}
			}
			ImGui::SameLine();
			if (ImGui::SmallButton("Delete")) {
				for (int property = 0; property < 3; ++property) {
					const PoseKeyRef& key = pose.keys[property];
					if (key.trackIndex >= 0 && key.keyIndex >= 0) {
						std::vector<SceneAnimationKeyframe>& keys =
							clip.tracks[key.trackIndex].keyframes;
						keys.erase(keys.begin() + key.keyIndex);
					}
				}
				document.MarkDirty();
			}
			ImGui::TreePop();
		}
		ImGui::PopID();
	}

	ImGui::TextDisabled(
		"Track Default is used when an interval has no Easing To Next override."
	);
}

bool ImGuiManager::WritePrefabAnimationGizmoKey(
	uint64_t entityId,
	const std::string& property,
	const Vector3& value
) {
	if (
		!prefabAnimationPreviewActive_ ||
		!prefabEditorSession_ ||
		!prefabEditorSession_->IsOpen()
	) {
		return false;
	}

	SceneDocument& document = prefabEditorSession_->GetDocument();
	SceneEntity* targetEntity = document.FindEntity(entityId);
	SceneEntity* owner = document.FindEntity(
		prefabAnimationPreviewOwnerEntityId_
	);
	SceneComponent* animator = owner
		? FindComponent(*owner, "PrefabAnimator")
		: nullptr;
	if (
		!targetEntity ||
		!animator ||
		!animator->enabled ||
		prefabAnimationPreviewClipIndex_ < 0 ||
		prefabAnimationPreviewClipIndex_ >=
			static_cast<int>(animator->prefabAnimationClips.size())
	) {
		return false;
	}

	ScenePrefabAnimationClip& clip =
		animator->prefabAnimationClips[prefabAnimationPreviewClipIndex_];
	auto resolveTrackTargetId = [&](const SceneAnimationTrack& track) {
		if (track.targetEntityId != 0) {
			if (const SceneEntity* byId =
				document.FindEntity(track.targetEntityId)) {
				return byId->id;
			}
		}
		if (!track.targetEntityName.empty()) {
			if (const SceneEntity* byName =
				document.FindEntityByName(track.targetEntityName)) {
				return byName->id;
			}
		}
		return owner->id;
	};

	SceneAnimationTrack* destinationTrack = nullptr;
	for (SceneAnimationTrack& track : clip.tracks) {
		if (
			track.property == property &&
			resolveTrackTargetId(track) == entityId
		) {
			destinationTrack = &track;
			break;
		}
	}
	if (!destinationTrack) {
		SceneAnimationTrack track{};
		track.targetEntityId = targetEntity->id;
		track.targetEntityName = targetEntity->name;
		track.property = property;
		clip.tracks.push_back(std::move(track));
		destinationTrack = &clip.tracks.back();
	}

	const float keyTime = std::clamp(
		prefabAnimationPreviewTime_,
		0.0f,
		(std::max)(clip.duration, 0.0f)
	);
	constexpr float kKeyTimeTolerance = 0.005f;
	for (SceneAnimationKeyframe& keyframe : destinationTrack->keyframes) {
		if (std::abs(keyframe.time - keyTime) > kKeyTimeTolerance) {
			continue;
		}
		prefabAnimationPreviewTime_ = keyframe.time;
		keyframe.value = value;
		document.MarkDirty();
		return true;
	}

	destinationTrack->keyframes.push_back({ keyTime, value });
	std::stable_sort(
		destinationTrack->keyframes.begin(),
		destinationTrack->keyframes.end(),
		[](const SceneAnimationKeyframe& left,
			const SceneAnimationKeyframe& right) {
			return left.time < right.time;
		}
	);
	document.MarkDirty();
	return true;
}

void ImGuiManager::DrawPrefabGizmo(
	float x,
	float y,
	float width,
	float height
) {
	if (
		!prefabEditorSession_ ||
		!prefabEditorSession_->IsOpen() ||
		!prefabPreviewCameraValid_ ||
		prefabSelectedEntityId_ == 0 ||
		width <= 1.0f ||
		height <= 1.0f
	) {
		return;
	}

	SceneDocument& sourceDocument = prefabEditorSession_->GetDocument();
	const SceneDocument& stageDocument = GetPrefabStageDocument();
	SceneEntity* sourceEntity = sourceDocument.FindEntity(prefabSelectedEntityId_);
	const SceneEntity* stageEntity = stageDocument.FindEntity(
		prefabSelectedEntityId_
	);
	if (!sourceEntity || !stageEntity || sourceEntity->locked) {
		return;
	}
	if (
		prefabHitBoxSetupMode_ &&
		!FindEnabledComponent(*sourceEntity, "OBBCollider")
	) {
		ImGui::GetWindowDrawList()->AddText(
			ImVec2(x + 8.0f, y + 8.0f),
			IM_COL32(255, 190, 80, 255),
			"HitBox Setup: select an Entity with a Collider."
		);
		return;
	}

	const SceneEntity* parentEntity = stageDocument.FindEntity(
		stageEntity->parentId
	);
	const Matrix4x4 parentWorld = parentEntity
		? ResolveSceneWorldMatrix(stageDocument, *parentEntity)
		: MakeIdentity4x4();
	Matrix4x4 worldMatrix = ResolveSceneWorldMatrix(
		stageDocument,
		*stageEntity
	);
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
			ImVec2(x + 8.0f, y + 8.0f),
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
	ImGuizmo::SetOrthographic(false);
	ImGuizmo::SetDrawlist(ImGui::GetWindowDrawList());
	ImGuizmo::SetRect(x, y, width, height);
	const bool changed = ImGuizmo::Manipulate(
		&prefabPreviewViewMatrix_.m[0][0],
		&prefabPreviewProjectionMatrix_.m[0][0],
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
	if (prefabAnimationPreviewActive_) {
		// Preview Documentは一時表示専用。操作値はSource Clipの現在時刻へ
		// 書き戻し、次のPreview再構築で表示へ反映する。
		prefabAnimationPreviewPlaying_ = false;
		const std::string property = gizmoOperation_ == 0
			? "LocalPosition"
			: gizmoOperation_ == 1
				? "LocalRotation"
				: "LocalScale";
		const Vector3 value = gizmoOperation_ == 0
			? localTranslate
			: gizmoOperation_ == 1
				? MakeEulerFromQuaternion(localRotate)
				: localScale;
		WritePrefabAnimationGizmoKey(
			sourceEntity->id,
			property,
			value
		);
		return;
	}
	if (gizmoOperation_ == 0) {
		sourceEntity->transform.translate = localTranslate;
	} else if (gizmoOperation_ == 1) {
		sourceEntity->transform.rotate = localRotate;
	} else {
		sourceEntity->transform.scale = localScale;
	}
	sourceDocument.MarkDirty();
}

bool ImGuiManager::PickPrefabEntity(
	float x,
	float y,
	float width,
	float height
) {
	if (
		!prefabEditorSession_ ||
		!prefabEditorSession_->IsOpen() ||
		!prefabPreviewCameraValid_ ||
		width <= 1.0f ||
		height <= 1.0f
	) {
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
	const Matrix4x4 inverseViewProjection = Inverse(Multiply(
		prefabPreviewViewMatrix_,
		prefabPreviewProjectionMatrix_
	));
	const Vector3 nearPoint = TransformCoord(
		{ ndcX, ndcY, 0.0f },
		inverseViewProjection
	);
	const Vector3 farPoint = TransformCoord(
		{ ndcX, ndcY, 1.0f },
		inverseViewProjection
	);

	const SceneDocument& document = GetPrefabStageDocument();
	uint64_t bestEntityId = 0;
	float bestDistance = (std::numeric_limits<float>::max)();
	const Vector3 worldRayDirection = Math::Normalize(
		Math::Subtract(farPoint, nearPoint)
	);
	const auto considerHit = [&](uint64_t entityId, float worldDistance) {
		if (worldDistance < bestDistance) {
			bestDistance = worldDistance;
			bestEntityId = entityId;
		}
	};
	for (const SceneEntity& entity : document.GetEntities()) {
		if (entity.locked) {
			continue;
		}

		if (IsEntityActiveInHierarchy(document, entity)) {
			const SceneComponent* meshRenderer =
				FindEnabledComponent(entity, "MeshRenderer");
			if (meshRenderer && !meshRenderer->modelPath.empty()) {
				ModelManager::GetInstance()->LoadModel(meshRenderer->modelPath);
				Model* model = ModelManager::GetInstance()->FindModel(
					meshRenderer->modelPath
				);
				Vector3 localMin{};
				Vector3 localMax{};
				if (model && model->GetLocalBounds(localMin, localMax)) {
					Matrix4x4 modelWorld =
						ResolveSceneWorldMatrix(document, entity);
					if (!model->HasSkinning()) {
						modelWorld = Multiply(
							model->GetRootNodeLocalMatrix(),
							modelWorld
						);
					}
					const Matrix4x4 inverseWorld = Inverse(modelWorld);
					const Vector3 localRayOrigin =
						TransformCoord(nearPoint, inverseWorld);
					const Vector3 localRayFar =
						TransformCoord(farPoint, inverseWorld);
					const Vector3 localRayDirection = Math::Normalize(
						Math::Subtract(localRayFar, localRayOrigin)
					);
					float localDistance = 0.0f;
					if (IntersectRayAabb(
						localRayOrigin,
						localRayDirection,
						localMin,
						localMax,
						localDistance
					)) {
						const Vector3 localHit = Math::Add(
							localRayOrigin,
							Math::Multiply(localRayDirection, localDistance)
						);
						const Vector3 worldHit =
							TransformCoord(localHit, modelWorld);
						considerHit(
							entity.id,
							Math::Length(Math::Subtract(worldHit, nearPoint))
						);
					}
				}
			}
		}

		const SceneComponent* colliderComponent =
			FindEnabledComponent(entity, "OBBCollider");
		if (!colliderComponent) {
			continue;
		}
		const bool isCombatVolume =
			FindEnabledComponent(entity, "HitBox") != nullptr ||
			FindEnabledComponent(entity, "HurtBox") != nullptr;
		const bool colliderVisible = isCombatVolume
			? prefabPreviewShowCombatVolumes_ || prefabPreviewShowColliders_
			: prefabPreviewShowColliders_;
		if (!colliderVisible) {
			continue;
		}

		const Matrix4x4 colliderWorld =
			ResolveSceneWorldMatrix(document, entity);
		if (colliderComponent->colliderShape == "Sphere") {
			const Vector3 center = TransformCoord(
				colliderComponent->colliderOffset,
				colliderWorld
			);
			const float radius =
				(std::max)(colliderComponent->colliderSphereRadius, 0.001f) *
				GetMaxWorldAxisScale(colliderWorld);
			float worldDistance = 0.0f;
			if (IntersectRaySphere(
				nearPoint,
				worldRayDirection,
				center,
				radius,
				worldDistance
			)) {
				considerHit(entity.id, worldDistance);
			}
			continue;
		}

		const Matrix4x4 inverseWorld = Inverse(colliderWorld);
		const Vector3 localRayOrigin = TransformCoord(nearPoint, inverseWorld);
		const Vector3 localRayFar = TransformCoord(farPoint, inverseWorld);
		const Vector3 localRayDirection = Math::Normalize(
			Math::Subtract(localRayFar, localRayOrigin)
		);
		const Vector3 halfSize{
			(std::max)(std::abs(colliderComponent->colliderSizeMultiplier.x), 0.001f),
			(std::max)(std::abs(colliderComponent->colliderSizeMultiplier.y), 0.001f),
			(std::max)(std::abs(colliderComponent->colliderSizeMultiplier.z), 0.001f)
		};
		const Vector3 localMin = Math::Subtract(
			colliderComponent->colliderOffset,
			halfSize
		);
		const Vector3 localMax = Math::Add(
			colliderComponent->colliderOffset,
			halfSize
		);
		float localDistance = 0.0f;
		if (IntersectRayAabb(
			localRayOrigin,
			localRayDirection,
			localMin,
			localMax,
			localDistance
		)) {
			const Vector3 localHit = Math::Add(
				localRayOrigin,
				Math::Multiply(localRayDirection, localDistance)
			);
			const Vector3 worldHit = TransformCoord(localHit, colliderWorld);
			considerHit(
				entity.id,
				Math::Length(Math::Subtract(worldHit, nearPoint))
			);
		}
	}

	prefabSelectedEntityId_ = bestEntityId;
	return bestEntityId != 0;
}

void ImGuiManager::DrawPrefabHierarchy() {
	if (!prefabEditorSession_ || !prefabEditorSession_->IsOpen()) {
		return;
	}
	SceneDocument& document = prefabEditorSession_->GetDocument();
	static std::string nestedPrefabStatus;
	std::string nestedPrefabDropPath;
	uint64_t nestedPrefabDropParentId = 0;
	std::string nestedPrefabOpenPath;
	const uint64_t selectedParentId = document.FindEntity(prefabSelectedEntityId_)
		? prefabSelectedEntityId_
		: 0;
	const bool hasRoot = std::any_of(
		document.GetEntities().begin(),
		document.GetEntities().end(),
		[](const SceneEntity& entity) { return entity.parentId == 0; }
	);
	ImGui::BeginDisabled(hasRoot);
	if (ImGui::Button("Create Root")) {
		SceneEntity& entity = document.CreateEntity("Entity");
		prefabSelectedEntityId_ = entity.id;
	}
	ImGui::EndDisabled();
	ImGui::SameLine();
	ImGui::BeginDisabled(selectedParentId == 0);
	if (ImGui::Button("Create Child")) {
		SceneEntity& entity = document.CreateEntity("Entity", selectedParentId);
		prefabSelectedEntityId_ = entity.id;
	}
	ImGui::EndDisabled();
	ImGui::SameLine();
	ImGui::BeginDisabled(selectedParentId == 0);
	if (ImGui::Button("Delete")) {
		document.RemoveEntity(selectedParentId);
		prefabSelectedEntityId_ = 0;
	}
	ImGui::EndDisabled();
	ImGui::Separator();
	if (!nestedPrefabStatus.empty()) {
		ImGui::TextWrapped("%s", nestedPrefabStatus.c_str());
	}

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
		ImGuiTreeNodeFlags flags =
			ImGuiTreeNodeFlags_OpenOnArrow |
			ImGuiTreeNodeFlags_SpanAvailWidth;
		if (!hasChildren) {
			flags |= ImGuiTreeNodeFlags_Leaf |
				ImGuiTreeNodeFlags_NoTreePushOnOpen;
		}
		if (prefabSelectedEntityId_ == entityId) {
			flags |= ImGuiTreeNodeFlags_Selected;
		}
		const char* prefabLabel = entity->prefabLinks.size() > 1
			? "[Nested] "
			: entity->prefabInstanceRootId != 0
				? "[Prefab] "
				: "";
		ImGui::PushID(static_cast<int>(entityId));
		const bool open = ImGui::TreeNodeEx(
			"##PrefabEntity",
			flags,
			"%s%s%s",
			entity->active ? "" : "(inactive) ",
			prefabLabel,
			entity->name.c_str()
		);
		if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) {
			prefabSelectedEntityId_ = entityId;
		}
		if (ImGui::IsItemHovered() &&
			ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
			if (!entity->prefabLinks.empty()) {
				// The last Link is the most specific Nested Prefab source.
				const ScenePrefabLink& source = entity->prefabLinks.back();
				nestedPrefabOpenPath = PrefabAssetRegistry::ResolvePath(
					source.assetId,
					source.sourcePath
				);
			}
		}
		if (ImGui::BeginDragDropTarget()) {
			if (const ImGuiPayload* payload =
				ImGui::AcceptDragDropPayload("PROJECT_PREFAB_PATH")) {
				const char* droppedPath =
					static_cast<const char*>(payload->Data);
				if (droppedPath && droppedPath[0] != '\0') {
					nestedPrefabDropPath = droppedPath;
					nestedPrefabDropParentId = entityId;
				}
			}
			ImGui::EndDragDropTarget();
		}
		if (hasChildren && open) {
			for (const SceneEntity& child : document.GetEntities()) {
				if (child.parentId == entityId) {
					drawEntity(child.id);
				}
			}
			ImGui::TreePop();
		}
		ImGui::PopID();
	};

	for (const SceneEntity& entity : document.GetEntities()) {
		if (entity.parentId == 0) {
			drawEntity(entity.id);
		}
	}
	if (!nestedPrefabDropPath.empty()) {
		const uint64_t instanceId = document.InstantiatePrefab(
			nestedPrefabDropPath,
			nestedPrefabDropParentId
		);
		if (instanceId != 0) {
			prefabSelectedEntityId_ = instanceId;
			nestedPrefabStatus = "Nested Prefab added: " +
				nestedPrefabDropPath;
		} else {
			nestedPrefabStatus =
				"Failed to add Nested Prefab (missing asset or cycle): " +
				nestedPrefabDropPath;
		}
	}
	if (!nestedPrefabOpenPath.empty()) {
		RequestOpenPrefab(nestedPrefabOpenPath);
	}
}

void ImGuiManager::DrawPrefabInspector() {
	if (!prefabEditorSession_ || !prefabEditorSession_->IsOpen()) {
		return;
	}
	SceneDocument& document = prefabEditorSession_->GetDocument();
	SceneEntity* entity = document.FindEntity(prefabSelectedEntityId_);
	if (!entity) {
		ImGui::TextDisabled("Select a Prefab Entity.");
		return;
	}
	const SceneEntity* clipFocusOwner = document.FindEntity(
		prefabAnimationPreviewOwnerEntityId_
	);
	const SceneComponent* clipFocusAnimator = clipFocusOwner
		? FindEnabledComponent(*clipFocusOwner, "PrefabAnimator")
		: nullptr;
	const bool clipFocusActive =
		prefabClipFocusEnabled_ &&
		clipFocusOwner == entity &&
		clipFocusAnimator &&
		prefabAnimationPreviewClipIndex_ >= 0 &&
		prefabAnimationPreviewClipIndex_ < static_cast<int>(
			clipFocusAnimator->prefabAnimationClips.size()
		);
	const std::string* clipFocusName = clipFocusActive
		? &clipFocusAnimator->prefabAnimationClips[
			prefabAnimationPreviewClipIndex_
		].name
		: nullptr;

	if (document.IsPrefabVariant()) {
		static std::string variantStatusDocumentPath;
		static std::string variantOverrideStatus;
		if (variantStatusDocumentPath != prefabEditorSession_->GetFilePath()) {
			variantStatusDocumentPath = prefabEditorSession_->GetFilePath();
			variantOverrideStatus.clear();
		}
		const std::string basePath = PrefabAssetRegistry::ResolvePath(
			document.GetVariantBaseAssetId(),
			document.GetVariantBasePath()
		);
		std::vector<ScenePrefabPropertyOverride> variantOverrides =
			document.CollectPrefabVariantOverrides();
		int applyVariantOverrideIndex = -1;
		int revertVariantOverrideIndex = -1;
		ImGui::SeparatorText("Variant Overrides");
		ImGui::TextDisabled(
			"Base: %s",
			basePath.empty() ? "Missing or ambiguous" : basePath.c_str()
		);
		ImGui::PushID("PrefabVariantOverrides");
		if (ImGui::TreeNodeEx(
			"Overrides",
			ImGuiTreeNodeFlags_DefaultOpen
		)) {
			for (size_t index = 0; index < variantOverrides.size(); ++index) {
				const ScenePrefabPropertyOverride& overrideValue =
					variantOverrides[index];
				const uint64_t targetEntityId =
					overrideValue.instanceEntityId != 0
						? overrideValue.instanceEntityId
						: overrideValue.entityLocalId;
				const SceneEntity* targetEntity =
					document.FindEntity(targetEntityId);
				const bool canModify =
					!basePath.empty() &&
					(!targetEntity || !targetEntity->locked);
				ImGui::PushID(static_cast<int>(index));
				ImGui::BulletText("%s", overrideValue.label.c_str());
				ImGui::Indent();
				ImGui::BeginDisabled(!canModify);
				if (ImGui::SmallButton("Apply to Base")) {
					applyVariantOverrideIndex = static_cast<int>(index);
				}
				ImGui::SameLine();
				if (ImGui::SmallButton("Revert")) {
					revertVariantOverrideIndex = static_cast<int>(index);
				}
				ImGui::EndDisabled();
				ImGui::Unindent();
				ImGui::PopID();
			}
			if (variantOverrides.empty()) {
				ImGui::TextDisabled("No Entity or Component overrides.");
			}
			ImGui::TreePop();
		}
		ImGui::PopID();

		if (applyVariantOverrideIndex >= 0) {
			const ScenePrefabPropertyOverride overrideValue =
				variantOverrides[applyVariantOverrideIndex];
			if (document.ApplyPrefabVariantOverrideToBase(overrideValue)) {
				variantOverrideStatus = "Applied to Base: " +
					overrideValue.label;
				InvalidateProjectCache();
			} else {
				variantOverrideStatus = "Failed to apply to Base: " +
					overrideValue.label;
			}
		}
		if (revertVariantOverrideIndex >= 0) {
			const ScenePrefabPropertyOverride overrideValue =
				variantOverrides[revertVariantOverrideIndex];
			const uint64_t selectedId = entity->id;
			const bool removesSelectedBranch =
				overrideValue.kind == ScenePrefabOverrideKind::AddedEntity &&
				(
					selectedId == overrideValue.instanceEntityId ||
					document.IsDescendantOf(
						selectedId,
						overrideValue.instanceEntityId
					)
				);
			if (document.RevertPrefabVariantOverride(overrideValue)) {
				variantOverrideStatus = "Reverted: " + overrideValue.label;
				if (removesSelectedBranch ||
					!document.FindEntity(selectedId)) {
					const auto root = std::find_if(
						document.GetEntities().begin(),
						document.GetEntities().end(),
						[](const SceneEntity& candidate) {
							return candidate.parentId == 0;
						}
					);
					prefabSelectedEntityId_ =
						root == document.GetEntities().end() ? 0 : root->id;
				}
				return;
			}
			variantOverrideStatus = "Failed to revert: " +
				overrideValue.label;
		}
		if (!variantOverrideStatus.empty()) {
			ImGui::TextWrapped("%s", variantOverrideStatus.c_str());
		}
	}

	if (!entity->prefabLinks.empty()) {
		std::string nestedSourceOpenPath;
		ImGui::SeparatorText("Prefab Sources");
		for (size_t linkIndex = 0;
			linkIndex < entity->prefabLinks.size();
			++linkIndex) {
			const ScenePrefabLink& link = entity->prefabLinks[linkIndex];
			const std::string sourcePath = PrefabAssetRegistry::ResolvePath(
				link.assetId,
				link.sourcePath
			);
			const std::string displayPath = sourcePath.empty()
				? link.sourcePath
				: sourcePath;
			const std::string displayName = displayPath.empty()
				? "Missing Prefab"
				: PathToUtf8(PathFromUtf8(displayPath).filename());
			ImGui::PushID(static_cast<int>(linkIndex));
			ImGui::Text(
				"%zu. %s%s",
				linkIndex + 1,
				link.instanceRootId == entity->id ? "[Root] " : "",
				displayName.c_str()
			);
			ImGui::Indent();
			ImGui::TextDisabled(
				"Instance Root: %llu / Local Entity: %llu",
				static_cast<unsigned long long>(link.instanceRootId),
				static_cast<unsigned long long>(link.localId)
			);
			ImGui::BeginDisabled(sourcePath.empty());
			if (ImGui::SmallButton("Open")) {
				nestedSourceOpenPath = sourcePath;
			}
			ImGui::SameLine();
			if (ImGui::SmallButton("Select Asset")) {
				SelectPrefabAssetInProject(sourcePath);
			}
			ImGui::EndDisabled();
			if (sourcePath.empty()) {
				ImGui::SameLine();
				ImGui::TextColored(
					ImVec4(0.95f, 0.35f, 0.3f, 1.0f),
					"Missing or ambiguous asset"
				);
			}
			ImGui::Unindent();
			ImGui::PopID();
		}
		if (!nestedSourceOpenPath.empty()) {
			RequestOpenPrefab(nestedSourceOpenPath);
			return;
		}
	}

	const std::vector<uint64_t> nestedInstanceRoots =
		document.CollectPrefabInstanceRoots(entity->id);
	if (
		prefabNestedTargetDocumentPath_ !=
			prefabEditorSession_->GetFilePath() ||
		std::find(
			nestedInstanceRoots.begin(),
			nestedInstanceRoots.end(),
			prefabNestedTargetRootId_
		) == nestedInstanceRoots.end()
	) {
		prefabNestedTargetDocumentPath_ =
			prefabEditorSession_->GetFilePath();
		prefabNestedTargetRootId_ = nestedInstanceRoots.empty()
			? 0
			: nestedInstanceRoots.front();
	}
	const uint64_t nestedInstanceRootId = prefabNestedTargetRootId_;
	if (nestedInstanceRootId != 0) {
		auto findInstanceLink = [](
			const SceneEntity* root,
			uint64_t rootId
		) -> const ScenePrefabLink* {
			if (!root) {
				return nullptr;
			}
			const auto found = std::find_if(
				root->prefabLinks.begin(),
				root->prefabLinks.end(),
				[rootId](const ScenePrefabLink& link) {
					return link.instanceRootId == rootId;
				}
			);
			return found == root->prefabLinks.end() ? nullptr : &(*found);
		};
		const SceneEntity* nestedInstanceRoot =
			document.FindEntity(nestedInstanceRootId);
		const ScenePrefabLink* nestedInstanceLink =
			findInstanceLink(nestedInstanceRoot, nestedInstanceRootId);
		const std::string nestedSourcePath = nestedInstanceLink
			? PrefabAssetRegistry::ResolvePath(
				nestedInstanceLink->assetId,
				nestedInstanceLink->sourcePath
			)
			: std::string{};
		static std::string nestedStatusDocumentPath;
		static uint64_t nestedStatusRootId = 0;
		static std::string nestedInstanceStatus;
		if (
			nestedStatusDocumentPath != prefabEditorSession_->GetFilePath() ||
			nestedStatusRootId != nestedInstanceRootId
		) {
			nestedStatusDocumentPath = prefabEditorSession_->GetFilePath();
			nestedStatusRootId = nestedInstanceRootId;
			nestedInstanceStatus.clear();
		}

		ImGui::SeparatorText("Nested Prefab Instance");
		if (nestedInstanceRoots.size() > 1) {
			const std::string currentTargetLabel = nestedSourcePath.empty()
				? "Missing Prefab"
				: PathToUtf8(PathFromUtf8(nestedSourcePath).filename());
			if (ImGui::BeginCombo("Apply Target", currentTargetLabel.c_str())) {
				for (uint64_t targetRootId : nestedInstanceRoots) {
					const SceneEntity* targetRoot =
						document.FindEntity(targetRootId);
					const ScenePrefabLink* targetLink =
						findInstanceLink(targetRoot, targetRootId);
					const std::string targetPath = targetLink
						? PrefabAssetRegistry::ResolvePath(
							targetLink->assetId,
							targetLink->sourcePath
						)
						: std::string{};
					const std::string targetName = targetPath.empty()
						? "Missing Prefab"
						: PathToUtf8(PathFromUtf8(targetPath).filename());
					const std::string targetLabel = targetName +
						" / Root " + std::to_string(targetRootId);
					const bool selected =
						targetRootId == nestedInstanceRootId;
					ImGui::PushID(static_cast<int>(targetRootId));
					if (ImGui::Selectable(targetLabel.c_str(), selected)) {
						prefabNestedTargetRootId_ = targetRootId;
					}
					if (selected) {
						ImGui::SetItemDefaultFocus();
					}
					ImGui::PopID();
				}
				ImGui::EndCombo();
			}
		}
		ImGui::TextWrapped(
			"Target: %s",
			nestedSourcePath.empty()
				? "Missing or ambiguous asset"
				: nestedSourcePath.c_str()
		);
		ImGui::TextDisabled(
			"Instance Root: %llu",
			static_cast<unsigned long long>(nestedInstanceRootId)
		);

		std::vector<ScenePrefabPropertyOverride> nestedOverrides;
		int applyNestedOverrideIndex = -1;
		int revertNestedOverrideIndex = -1;
		ImGui::PushID("NestedPrefabInstance");
		if (ImGui::TreeNode("Overrides")) {
			const std::vector<std::string> overrideSummary =
				document.CollectPrefabInstanceOverrides(
					nestedInstanceRootId
				);
			nestedOverrides = document.CollectPrefabPropertyOverrides(
				nestedInstanceRootId
			);
			bool hasStatusMessage = false;
			for (const std::string& message : overrideSummary) {
				if (
					message.starts_with("Modified Entity:") ||
					message.starts_with("Added Entity:") ||
					message.starts_with("Removed Entity:") ||
					message.starts_with("Stale Entity:")
				) {
					continue;
				}
				hasStatusMessage = true;
				ImGui::BulletText("%s", message.c_str());
			}
			const bool canModifyNested =
				nestedInstanceRoot &&
				nestedInstanceLink &&
				!nestedInstanceRoot->locked &&
				!entity->locked &&
				!nestedSourcePath.empty();
			for (size_t index = 0; index < nestedOverrides.size(); ++index) {
				const ScenePrefabPropertyOverride& overrideValue =
					nestedOverrides[index];
				ImGui::PushID(static_cast<int>(index));
				ImGui::BulletText("%s", overrideValue.label.c_str());
				ImGui::Indent();
				ImGui::BeginDisabled(!canModifyNested);
				if (ImGui::SmallButton("Apply")) {
					applyNestedOverrideIndex = static_cast<int>(index);
				}
				ImGui::SameLine();
				if (ImGui::SmallButton("Revert")) {
					revertNestedOverrideIndex = static_cast<int>(index);
				}
				ImGui::EndDisabled();
				ImGui::Unindent();
				ImGui::PopID();
			}
			if (nestedOverrides.empty() && !hasStatusMessage) {
				ImGui::TextDisabled("No overrides.");
			}
			ImGui::TreePop();
		}

		if (applyNestedOverrideIndex >= 0) {
			const ScenePrefabPropertyOverride overrideValue =
				nestedOverrides[applyNestedOverrideIndex];
			if (document.ApplyPrefabPropertyOverride(
				nestedInstanceRootId,
				overrideValue
			)) {
				nestedInstanceStatus = "Applied: " + overrideValue.label;
				InvalidateProjectCache();
				ImGui::PopID();
				return;
			}
			nestedInstanceStatus = "Failed to apply: " + overrideValue.label;
		}
		if (revertNestedOverrideIndex >= 0) {
			const ScenePrefabPropertyOverride overrideValue =
				nestedOverrides[revertNestedOverrideIndex];
			const uint64_t selectedId = entity->id;
			const bool removesSelectedBranch =
				(
					overrideValue.kind == ScenePrefabOverrideKind::AddedEntity ||
					overrideValue.kind == ScenePrefabOverrideKind::StaleEntity
				) &&
				(
					selectedId == overrideValue.instanceEntityId ||
					document.IsDescendantOf(
						selectedId,
						overrideValue.instanceEntityId
					)
				);
			if (document.RevertPrefabPropertyOverride(
				nestedInstanceRootId,
				overrideValue
			)) {
				nestedInstanceStatus = "Reverted: " + overrideValue.label;
				prefabSelectedEntityId_ = removesSelectedBranch
					? nestedInstanceRootId
					: selectedId;
				ImGui::PopID();
				return;
			}
			nestedInstanceStatus = "Failed to revert: " + overrideValue.label;
		}

		const bool canModifyNested =
			nestedInstanceRoot &&
			nestedInstanceLink &&
			!nestedInstanceRoot->locked &&
			!entity->locked &&
			!nestedSourcePath.empty();
		ImGui::BeginDisabled(!canModifyNested);
		if (ImGui::Button("Apply Instance")) {
			if (document.ApplyPrefabInstance(nestedInstanceRootId)) {
				nestedInstanceStatus =
					"Applied the Nested instance to its Prefab asset.";
				InvalidateProjectCache();
				ImGui::EndDisabled();
				ImGui::PopID();
				return;
			}
			nestedInstanceStatus = "Failed to apply the Nested instance.";
		}
		ImGui::SameLine();
		if (ImGui::Button("Revert Instance")) {
			if (document.RevertPrefabInstance(nestedInstanceRootId)) {
				nestedInstanceStatus =
					"Reverted the Nested instance from its Prefab asset.";
				prefabSelectedEntityId_ = nestedInstanceRootId;
				ImGui::EndDisabled();
				ImGui::PopID();
				return;
			}
			nestedInstanceStatus = "Failed to revert the Nested instance.";
		}
		ImGui::SameLine();
		if (ImGui::Button("Unpack")) {
			if (document.UnpackPrefabInstance(nestedInstanceRootId)) {
				nestedInstanceStatus = "Unpacked the Nested instance.";
			} else {
				nestedInstanceStatus = "Failed to unpack the Nested instance.";
			}
		}
		ImGui::EndDisabled();
		ImGui::PopID();
		if (!nestedInstanceStatus.empty()) {
			ImGui::TextWrapped("%s", nestedInstanceStatus.c_str());
		}
	}

	bool entityChanged = false;
	entityChanged |= InputTextString("Name", entity->name);
	entityChanged |= ImGui::Checkbox("Active", &entity->active);
	ImGui::SeparatorText("Transform");
	entityChanged |= ImGui::DragFloat3(
		"Position", &entity->transform.translate.x, 0.01f
	);
	Vector3 rotationEuler = MakeEulerFromQuaternion(entity->transform.rotate);
	if (ImGui::DragFloat3("Rotation", &rotationEuler.x, 0.01f)) {
		entity->transform.rotate = MakeQuaternionFromEuler(rotationEuler);
		entityChanged = true;
	}
	entityChanged |= ImGui::DragFloat3(
		"Scale", &entity->transform.scale.x, 0.01f
	);
	if (entityChanged) {
		document.MarkDirty();
	}
	DrawPrefabTransformPoseInspector(*entity);

	if (const SceneComponent* meshRenderer =
		FindEnabledComponent(*entity, "MeshRenderer")) {
		if (
			!meshRenderer->modelPath.empty() &&
			modelPreviewRenderedPath_ == meshRenderer->modelPath &&
			modelPreviewTexture_.ptr != 0
		) {
			ImGui::SeparatorText("Preview");
			const float availableWidth = ImGui::GetContentRegionAvail().x;
			const float previewSize = std::clamp(availableWidth, 180.0f, 420.0f);
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
						modelPreviewZoom_ * (1.0f - io.MouseWheel * 0.1f),
						0.15f,
						8.0f
					);
				}
			}
		}
	}

	int removeComponentIndex = -1;
	for (size_t componentIndex = 0;
		componentIndex < entity->components.size();
		++componentIndex) {
		SceneComponent& component = entity->components[componentIndex];
		if (
			clipFocusActive &&
			component.type != "AttackSet" &&
			component.type != "PrefabAnimator"
		) {
			continue;
		}
		ImGui::PushID(static_cast<int>(componentIndex));
		const bool open = ImGui::CollapsingHeader(
			component.type.c_str(),
			ImGuiTreeNodeFlags_DefaultOpen
		);
		if (!open) {
			ImGui::PopID();
			continue;
		}
		bool changed = ImGui::Checkbox("Enabled", &component.enabled);
		if (component.type == "MeshRenderer") {
			const char* currentModel = component.modelPath.empty()
				? "None"
				: component.modelPath.c_str();
			if (ImGui::BeginCombo("Model", currentModel)) {
				for (const std::string& modelPath : GetCachedModelAssetPaths()) {
					if (ImGui::Selectable(
						modelPath.c_str(), component.modelPath == modelPath
					)) {
						component.modelPath = modelPath;
						entity->modelPath = modelPath;
						changed = true;
					}
				}
				ImGui::EndCombo();
			}
		} else if (component.type == "Animator") {
			changed |= ImGui::Checkbox(
				"Play On Start", &component.animatorPlayOnStart
			);
			changed |= ImGui::Checkbox("Loop", &component.animatorLoop);
			changed |= ImGui::DragFloat(
				"Speed", &component.animatorSpeed, 0.01f, 0.0f, 100.0f
			);
			changed |= ImGui::DragInt(
				"Default Clip", &component.animatorDefaultClip, 1.0f, 0, 1024
			);
		} else if (component.type == "StatSet") {
			int removeStatIndex = -1;
			for (size_t statIndex = 0; statIndex < component.stats.size(); ++statIndex) {
				SceneStatDefinition& stat = component.stats[statIndex];
				ImGui::PushID(static_cast<int>(statIndex));
				const std::string statLabel = stat.displayName.empty()
					? stat.id
					: stat.displayName;
				if (ImGui::TreeNodeEx(
					"Stat",
					ImGuiTreeNodeFlags_DefaultOpen,
					"%s",
					statLabel.empty() ? "Stat" : statLabel.c_str()
				)) {
					changed |= InputTextString("Id", stat.id);
					changed |= InputTextString("Display Name", stat.displayName);
					changed |= ImGui::DragFloat("Min", &stat.minValue, 0.1f);
					changed |= ImGui::DragFloat("Max", &stat.maxValue, 0.1f);
					changed |= ImGui::DragFloat(
						"Initial", &stat.initialValue, 0.1f
					);
					if (stat.maxValue < stat.minValue) {
						stat.maxValue = stat.minValue;
						changed = true;
					}
					const float clampedInitial = std::clamp(
						stat.initialValue,
						stat.minValue,
						stat.maxValue
					);
					if (clampedInitial != stat.initialValue) {
						stat.initialValue = clampedInitial;
						changed = true;
					}
					if (ImGui::SmallButton("Remove Stat")) {
						removeStatIndex = static_cast<int>(statIndex);
					}
					ImGui::TreePop();
				}
				ImGui::PopID();
			}
			if (removeStatIndex >= 0) {
				component.stats.erase(component.stats.begin() + removeStatIndex);
				changed = true;
			}
			if (ImGui::Button("Add Stat")) {
				SceneStatDefinition stat{};
				stat.id = "stat" + std::to_string(component.stats.size() + 1);
				stat.displayName = stat.id;
				component.stats.push_back(std::move(stat));
				changed = true;
			}
		} else if (component.type == "OBBCollider") {
			if (ImGui::BeginCombo("Shape", component.colliderShape.c_str())) {
				for (const char* shape : { "Box", "Sphere" }) {
					if (ImGui::Selectable(
						shape, component.colliderShape == shape
					)) {
						component.colliderShape = shape;
						changed = true;
					}
				}
				ImGui::EndCombo();
			}
			changed |= ImGui::DragFloat3(
				"Offset", &component.colliderOffset.x, 0.01f
			);
			if (component.colliderShape == "Sphere") {
				changed |= ImGui::DragFloat(
					"Radius", &component.colliderSphereRadius, 0.01f, 0.001f, 10000.0f
				);
			} else {
				changed |= ImGui::DragFloat3(
					"Half Size", &component.colliderSizeMultiplier.x, 0.01f
				);
			}
			changed |= ImGui::Checkbox("Is Trigger", &component.colliderIsTrigger);
			changed |= ImGui::Checkbox("Collider Active", &component.colliderActive);
			changed |= ImGui::Checkbox(
				"Debug Visible", &component.colliderDebugVisible
			);
		} else if (component.type == "HitBox") {
			changed |= ImGui::DragFloat(
				"Damage", &component.hitBoxDamage, 0.1f, 0.0f, 100000.0f
			);
			changed |= ImGui::DragFloat(
				"Poise Damage", &component.hitBoxPoiseDamage, 0.1f, 0.0f, 100000.0f
			);
			changed |= ImGui::DragFloat(
				"Knockback", &component.hitBoxKnockback, 0.1f, 0.0f, 100000.0f
			);
			changed |= ImGui::DragFloat(
				"Vertical Knockback", &component.hitBoxVerticalKnockback,
				0.1f, 0.0f, 100000.0f
			);
			changed |= ImGui::DragFloat(
				"Hit Stop Duration", &component.hitBoxHitStopDuration, 0.001f, 0.0f, 1.0f
			);
			changed |= InputTextString(
				"Reaction Tag", component.hitBoxReactionTag
			);
			changed |= InputTextString(
				"Damage Stat", component.hitBoxDamageStatId
			);
			changed |= InputTextString(
				"Poise Stat", component.hitBoxPoiseStatId
			);
			changed |= ImGui::InputScalar(
				"Owner Entity Id",
				ImGuiDataType_U64,
				&component.hitBoxOwnerEntityId
			);
			changed |= InputTextString(
				"Owner Entity Name", component.hitBoxOwnerEntityName
			);
			changed |= ImGui::Checkbox(
				"Ignore Same Faction", &component.hitBoxIgnoreSameFaction
			);
			ImGui::TextDisabled("Use with an active Trigger Collider.");
		} else if (component.type == "HurtBox") {
			changed |= ImGui::DragFloat(
				"Damage Multiplier",
				&component.hurtBoxDamageMultiplier,
				0.01f,
				0.0f,
				1000.0f
			);
			changed |= InputTextString(
				"Stats Entity Name", component.hurtBoxStatsEntityName
			);
		} else if (component.type == "AgentBehavior") {
			const bool isGroundAgent =
				component.agentMovementMode == "GroundXZ";
			if (ImGui::BeginCombo(
				"Movement Mode",
				isGroundAgent ? "Ground XZ" : "Free 3D"
			)) {
				if (ImGui::Selectable("Free 3D", !isGroundAgent)) {
					component.agentMovementMode = "Free3D";
					changed = true;
				}
				if (ImGui::Selectable("Ground XZ", isGroundAgent)) {
					component.agentMovementMode = "GroundXZ";
					changed = true;
				}
				ImGui::EndCombo();
			}
			changed |= InputTextString("Group", component.agentGroupName);
			if (isGroundAgent) {
				changed |= ImGui::DragFloat(
					"Separation Radius",
					&component.agentSeparationRadius,
					0.05f,
					0.0f,
					100.0f
				);
				changed |= ImGui::DragFloat(
					"Separation Weight",
					&component.agentSeparationWeight,
					0.05f,
					0.0f,
					100.0f
				);
				changed |= ImGui::InputInt(
					"Neighbor Limit",
					&component.agentNeighborLimit
				);
				ImGui::TextDisabled(
					"Requires PhysicsBody. EnemyBehavior retains movement and rotation ownership."
				);
			}
		} else if (component.type == "HitReaction") {
			changed |= ImGui::DragFloat(
				"Knockback Multiplier",
				&component.hitReactionKnockbackMultiplier,
				0.01f, 0.0f, 100.0f
			);
			const char* reactionModePreview =
				component.hitReactionTriggerMode == "PoiseBreak"
				? "Poise Break" : "Minimum Damage";
			if (ImGui::BeginCombo("Reaction Trigger", reactionModePreview)) {
				if (ImGui::Selectable(
					"Minimum Damage",
					component.hitReactionTriggerMode == "MinimumDamage"
				)) {
					component.hitReactionTriggerMode = "MinimumDamage";
					changed = true;
				}
				if (ImGui::Selectable(
					"Poise Break",
					component.hitReactionTriggerMode == "PoiseBreak"
				)) {
					component.hitReactionTriggerMode = "PoiseBreak";
					changed = true;
				}
				ImGui::EndCombo();
			}
			if (component.hitReactionTriggerMode == "PoiseBreak") {
				changed |= InputTextString(
					"Poise Stat", component.hitReactionPoiseStatId
				);
				changed |= ImGui::DragFloat(
					"Poise Recovery Delay",
					&component.hitReactionPoiseRecoveryDelay,
					0.05f, 0.0f, 60.0f
				);
			} else {
				changed |= ImGui::DragFloat(
					"Minimum Poise Damage",
					&component.hitReactionMinimumPoiseDamage,
					0.1f, 0.0f, 100000.0f
				);
			}
			changed |= InputTextString(
				"Hit State", component.hitReactionStateName
			);
		} else if (component.type == "DeathPresentation") {
			changed |= InputTextString(
				"Death State", component.deathPresentationStateName
			);
			changed |= ImGui::DragFloat(
				"Deactivate Delay",
				&component.deathPresentationDeactivateDelay,
				0.05f, 0.0f, 60.0f
			);
			changed |= InputTextString(
				"Death Effect Path", component.deathPresentationEffectPath
			);
		} else if (component.type == "EnemySpawner") {
				changed |= InputTextString(
					"Enemy Prefab", component.enemySpawnerPrefabPath
				);
				changed |= ImGui::DragInt(
					"Initial Count", &component.enemySpawnerInitialCount,
					1.0f, 0, 10000
				);
				changed |= ImGui::DragInt(
					"Max Alive", &component.enemySpawnerMaxAlive,
					1.0f, 0, 10000
				);
				changed |= ImGui::DragFloat(
					"Respawn Interval", &component.enemySpawnerInterval,
					0.05f, 0.0f, 3600.0f
				);
				changed |= ImGui::DragFloat(
					"Spawn Radius", &component.enemySpawnerRadius,
					0.1f, 0.0f, 10000.0f
				);
				changed |= ImGui::Checkbox(
					"Auto Start", &component.enemySpawnerAutoStart
				);
				component.enemySpawnerInitialCount = (std::max)(
					component.enemySpawnerInitialCount, 0
				);
				component.enemySpawnerMaxAlive = (std::max)(
					component.enemySpawnerMaxAlive,
					component.enemySpawnerInitialCount
				);
				component.enemySpawnerInterval = (std::max)(
					component.enemySpawnerInterval, 0.0f
				);
				component.enemySpawnerRadius = (std::max)(
					component.enemySpawnerRadius, 0.0f
				);
				ImGui::TextDisabled(
					"Runtime-only instances are reset to their prefab baseline before reuse."
				);
			} else if (component.type == "BoneAttachment") {
			SceneEntity* targetEntity = component.boneAttachmentTargetEntityId != 0
				? document.FindEntity(component.boneAttachmentTargetEntityId)
				: nullptr;
			if (!targetEntity && !component.boneAttachmentTargetEntityName.empty()) {
				targetEntity = document.FindEntityByName(
					component.boneAttachmentTargetEntityName
				);
			}
			SceneEntity* parentEntity = document.FindEntity(entity->parentId);
			SceneEntity* effectiveTarget = targetEntity ? targetEntity : parentEntity;
			const char* targetLabel = targetEntity
				? targetEntity->name.c_str()
				: "Parent / Auto";
			if (ImGui::BeginCombo("Target Entity", targetLabel)) {
				if (ImGui::Selectable(
					"Parent / Auto",
					component.boneAttachmentTargetEntityId == 0 &&
						component.boneAttachmentTargetEntityName.empty()
				)) {
					component.boneAttachmentTargetEntityId = 0;
					component.boneAttachmentTargetEntityName.clear();
					component.boneAttachmentJointName.clear();
					changed = true;
				}
				for (const SceneEntity& candidate : document.GetEntities()) {
					if (
						candidate.id == entity->id ||
						!FindEnabledComponent(candidate, "MeshRenderer")
					) {
						continue;
					}
					if (ImGui::Selectable(
						candidate.name.c_str(),
						targetEntity && targetEntity->id == candidate.id
					)) {
						component.boneAttachmentTargetEntityId = candidate.id;
						component.boneAttachmentTargetEntityName = candidate.name;
						component.boneAttachmentJointName.clear();
						changed = true;
					}
				}
				ImGui::EndCombo();
			}
			const std::vector<std::string> jointNames = effectiveTarget
				? CollectEntityJointNames(*effectiveTarget)
				: std::vector<std::string>{};
			ImGui::BeginDisabled(jointNames.empty());
			changed |= DrawJointNameCombo(
				"Target Bone", jointNames, component.boneAttachmentJointName
			);
			ImGui::EndDisabled();
			const bool matchesSourceBone =
				component.boneAttachmentAlignmentMode == "MatchSourceBone";
			if (ImGui::BeginCombo(
				"Alignment Mode",
				matchesSourceBone ? "Match Weapon Bone" : "Manual Offset"
			)) {
				if (ImGui::Selectable("Manual Offset", !matchesSourceBone)) {
					component.boneAttachmentAlignmentMode = "ManualOffset";
					changed = true;
				}
				if (ImGui::Selectable("Match Weapon Bone", matchesSourceBone)) {
					component.boneAttachmentAlignmentMode = "MatchSourceBone";
					changed = true;
				}
				ImGui::EndCombo();
			}
			if (matchesSourceBone) {
				const std::vector<std::string> sourceJointNames =
					CollectEntityJointNames(*entity);
				ImGui::BeginDisabled(sourceJointNames.empty());
				changed |= DrawJointNameCombo(
					"Weapon Bone",
					sourceJointNames,
					component.boneAttachmentSourceJointName
				);
				ImGui::EndDisabled();
				ImGui::TextDisabled(
					"The weapon bone is aligned exactly with the target bone."
				);
			} else {
				ImGui::TextDisabled(
					"The Entity Transform is used as the attachment offset."
				);
			}
			changed |= ImGui::Checkbox(
				"Inherit Bone Scale", &component.boneAttachmentInheritScale
			);
		} else if (component.type == "AttackSet") {
			if (clipFocusName) {
				const bool hasFocusedAttack = std::any_of(
					component.attackDefinitions.begin(),
					component.attackDefinitions.end(),
					[&clipFocusName](const SceneAttackDefinition& attack) {
						return attack.animation == *clipFocusName;
					}
				);
				if (!hasFocusedAttack) {
					ImGui::TextDisabled("No Attack Definition for this Clip.");
				}
			}
			auto hasHitBox = [](const SceneEntity& candidate) {
				return std::any_of(
					candidate.components.begin(),
					candidate.components.end(),
					[](const SceneComponent& candidateComponent) {
						return candidateComponent.type == "HitBox";
					}
				);
			};
			auto resolveHitBox = [&document](const SceneAttackHitWindow& window) {
				SceneEntity* hitBox = window.hitBoxEntityId != 0
					? document.FindEntity(window.hitBoxEntityId)
					: nullptr;
				if (!hitBox && !window.hitBoxEntityName.empty()) {
					hitBox = document.FindEntityByName(window.hitBoxEntityName);
				}
				return hitBox;
			};
			auto makeDedicatedHitBoxName = [&document](
				const SceneAttackDefinition& attack,
				size_t windowIndex
			) {
				const std::string base = attack.name + "_HitBox_" +
					std::to_string(windowIndex + 1);
				std::string result = base;
				for (uint32_t suffix = 2; document.FindEntityByName(result); ++suffix) {
					result = base + "_" + std::to_string(suffix);
				}
				return result;
			};
			auto copyLegacyPayloadToHitBox = [](
				SceneComponent& hitBox,
				const SceneAttackHitWindow& window
			) {
				hitBox.hitBoxDamage = window.damage;
				hitBox.hitBoxPoiseDamage = window.poiseDamage;
				hitBox.hitBoxKnockback = window.knockback;
				hitBox.hitBoxVerticalKnockback = window.verticalKnockback;
				hitBox.hitBoxHitStopDuration = window.hitStopDuration;
				hitBox.hitBoxReactionTag = window.reactionTag;
				hitBox.hitBoxKnockbackDirectionMode = window.knockbackDirectionMode;
				hitBox.hitBoxKnockbackLocalDirection = window.knockbackLocalDirection;
				hitBox.hitBoxHitPolicy = window.hitPolicy;
				hitBox.hitBoxTargetCooldown = window.targetCooldown;
			};
			auto isDedicatedHitBox = [&component](uint64_t entityId) {
				for (const SceneAttackDefinition& attack : component.attackDefinitions) {
					for (const SceneAttackHitWindow& window : attack.hitWindows) {
						if (window.payloadSource == "HitBox" &&
							window.hitBoxEntityId == entityId) {
							return true;
						}
					}
				}
				return false;
			};
			auto createDedicatedHitBox = [
				&document,
				&hasHitBox,
				&resolveHitBox,
				&makeDedicatedHitBoxName,
				&copyLegacyPayloadToHitBox,
				&isDedicatedHitBox
			](
				SceneAttackDefinition& attack,
				size_t windowIndex,
				bool copyWindowPayload
			) -> uint64_t {
				SceneAttackHitWindow& window = attack.hitWindows[windowIndex];
				SceneEntity* templateEntity = resolveHitBox(window);
				if (!templateEntity && !copyWindowPayload) {
					// 新規Windowは既存の専用HitBoxを連鎖複製せず、Dedicated
					// Windowから未参照のAuthoring HitBoxを基準Shapeとして使う。
					for (const SceneEntity& candidate : document.GetEntities()) {
						if (hasHitBox(candidate) && !isDedicatedHitBox(candidate.id)) {
							templateEntity = document.FindEntity(candidate.id);
							break;
						}
					}
				}
				if (!templateEntity) {
					for (const SceneAttackHitWindow& candidate : attack.hitWindows) {
						if (SceneEntity* candidateEntity = resolveHitBox(candidate)) {
							templateEntity = candidateEntity;
							break;
						}
					}
				}
				if (!templateEntity) {
					for (const SceneEntity& candidate : document.GetEntities()) {
						if (hasHitBox(candidate)) {
							templateEntity = document.FindEntity(candidate.id);
							break;
						}
					}
				}
				if (!templateEntity) {
					return 0;
				}
				const uint64_t dedicatedId = document.DuplicateEntity(templateEntity->id);
				SceneEntity* dedicated = document.FindEntity(dedicatedId);
				SceneComponent* dedicatedHitBox = dedicated
					? FindComponent(*dedicated, "HitBox") : nullptr;
				if (!dedicated || !dedicatedHitBox) {
					if (dedicatedId != 0) {
						document.RemoveEntity(dedicatedId);
					}
					return 0;
				}
				dedicated->name = makeDedicatedHitBoxName(attack, windowIndex);
				dedicated->active = false;
				if (!copyWindowPayload) {
					SceneComponent* collider = FindComponent(*dedicated, "OBBCollider");
					if (!collider) {
						document.RemoveEntity(dedicatedId);
						return 0;
					}
					dedicated->transform.scale = { 1.0f, 1.0f, 1.0f };
					collider->enabled = true;
					collider->colliderShape = "Box";
					collider->colliderOffset = { 0.0f, 0.0f, 0.0f };
					collider->colliderSizeMultiplier = { 0.5f, 0.5f, 0.5f };
					collider->colliderIsTrigger = true;
					collider->colliderActive = true;
				}
				else {
					copyLegacyPayloadToHitBox(*dedicatedHitBox, window);
					if (window.overrideHitBoxHalfSize) {
						if (SceneComponent* collider = FindComponent(*dedicated, "OBBCollider");
							collider && collider->enabled && collider->colliderShape == "Box") {
							collider->colliderSizeMultiplier = window.hitBoxHalfSize;
						}
					}
				}
				window.hitBoxEntityId = dedicatedId;
				window.hitBoxEntityName = dedicated->name;
				window.payloadSource = "HitBox";
				window.overrideHitBoxHalfSize = false;
				return dedicatedId;
			};
			int removeAttack = -1;
			for (size_t attackIndex = 0; attackIndex < component.attackDefinitions.size(); ++attackIndex) {
				SceneAttackDefinition& attack = component.attackDefinitions[attackIndex];
				if (clipFocusName && attack.animation != *clipFocusName) {
					continue;
				}
				ImGui::PushID(static_cast<int>(attackIndex));
				if (ImGui::TreeNodeEx("Attack", ImGuiTreeNodeFlags_DefaultOpen, "%s", attack.name.c_str())) {
					ImGui::SeparatorText("Identity");
					changed |= InputTextString("Name", attack.name);
					changed |= InputTextString("Animation", attack.animation);
					const SceneComponent* animator = FindEnabledComponent(
						*entity,
						"PrefabAnimator"
					);
					const ScenePrefabAnimationClip* animationClip = nullptr;
					if (animator) {
						auto foundClip = std::find_if(
							animator->prefabAnimationClips.begin(),
							animator->prefabAnimationClips.end(),
							[&attack](const ScenePrefabAnimationClip& clip) {
								return clip.name == attack.animation;
							}
						);
						if (foundClip != animator->prefabAnimationClips.end()) {
							animationClip = &*foundClip;
						}
					}
					ImGui::SeparatorText("Timing");
					changed |= ImGui::DragFloat("Windup", &attack.windup, 0.01f, 0.0f, 60.0f);
					changed |= ImGui::DragFloat("Active Time", &attack.activeTime, 0.01f, 0.0f, 60.0f);
					changed |= ImGui::DragFloat("Recovery", &attack.recovery, 0.01f, 0.0f, 60.0f);
					const float attackDuration = attack.windup +
						attack.activeTime + attack.recovery;
					if (animationClip) {
						const float durationDifference =
							std::abs(animationClip->duration - attackDuration);
						ImGui::TextDisabled(
							"Attack %.3f s / Clip %.3f s",
							attackDuration,
							animationClip->duration
						);
						if (durationDifference > 0.02f) {
							ImGui::TextColored(
								ImVec4(0.95f, 0.65f, 0.25f, 1.0f),
								"Timing and Clip duration differ by %.3f s.",
								durationDifference
							);
						}
					} else if (!attack.animation.empty()) {
						ImGui::TextColored(
							ImVec4(0.95f, 0.65f, 0.25f, 1.0f),
							"Animation '%s' was not found on this PrefabAnimator.",
							attack.animation.c_str()
						);
					}
					ImGui::SeparatorText("Motion");
					changed |= ImGui::DragFloat("Forward Distance", &attack.forwardDistance, 0.01f, -100.0f, 100.0f);
					changed |= ImGui::DragFloat("Side Distance", &attack.sideDistance, 0.01f, -100.0f, 100.0f);
					const char* facingPreview = "Fixed At Start";
					if (attack.facingMode == "InputDirection") facingPreview = "Input Direction";
					if (attack.facingMode == "TargetDirection") facingPreview = "Target Direction";
					if (attack.facingMode == "RotateByAngle") facingPreview = "Rotate By Angle";
					if (ImGui::BeginCombo("Facing", facingPreview)) {
						for (const char* mode : {
							"FixedAtStart", "InputDirection", "TargetDirection", "RotateByAngle"
						}) {
							const bool selected = attack.facingMode == mode;
							const char* label = mode[0] == 'I'
								? "Input Direction"
								: mode[0] == 'T'
									? "Target Direction"
									: mode[0] == 'R'
										? "Rotate By Angle"
										: "Fixed At Start";
							if (ImGui::Selectable(label, selected)) {
								attack.facingMode = mode;
								changed = true;
							}
						}
						ImGui::EndCombo();
					}
					if (attack.facingMode == "TargetDirection") {
						const SceneEntity* target = attack.facingTargetEntityId != 0
							? document.FindEntity(attack.facingTargetEntityId)
							: nullptr;
						if (!target && !attack.facingTargetEntityName.empty()) {
							target = document.FindEntityByName(attack.facingTargetEntityName);
						}
						const char* targetPreview = target ? target->name.c_str() : "None (keep start facing)";
						if (ImGui::BeginCombo("Facing Target", targetPreview)) {
							if (ImGui::Selectable("None (keep start facing)", !target)) {
								attack.facingTargetEntityId = 0;
								attack.facingTargetEntityName.clear();
								changed = true;
							}
							for (const SceneEntity& candidate : document.GetEntities()) {
								if (candidate.id == entity->id) { continue; }
								const bool selected = candidate.id == attack.facingTargetEntityId;
								const std::string label = candidate.name + " (" + std::to_string(candidate.id) + ")";
								if (ImGui::Selectable(label.c_str(), selected)) {
									attack.facingTargetEntityId = candidate.id;
									attack.facingTargetEntityName = candidate.name;
									changed = true;
								}
							}
							ImGui::EndCombo();
						}
					} else if (attack.facingMode == "RotateByAngle") {
						changed |= ImGui::DragFloat(
							"Rotate Angle (radians)", &attack.facingRotateAngle,
							0.01f, -25.1328f, 25.1328f
						);
					}
					ImGui::SeparatorText("Loop");
					changed |= ImGui::Checkbox("Loop Enabled", &attack.loopEnabled);
					ImGui::BeginDisabled(!attack.loopEnabled);
					changed |= ImGui::DragInt(
						"Loop Max Count (0 = Unlimited)", &attack.loopMaxCount,
						1.0f, 0, 1000
					);
					changed |= ImGui::DragFloat(
						"Loop Safety Timeout", &attack.loopSafetyTimeout,
						0.05f, 0.0f, 120.0f
					);
					ImGui::EndDisabled();
					ImGui::SeparatorText("Hit Windows");
					int removeWindow = -1;
					for (size_t windowIndex = 0; windowIndex < attack.hitWindows.size(); ++windowIndex) {
						SceneAttackHitWindow& window = attack.hitWindows[windowIndex];
						ImGui::PushID(static_cast<int>(windowIndex));
						if (ImGui::TreeNodeEx("Hit Window", ImGuiTreeNodeFlags_DefaultOpen, "Hit Window %zu", windowIndex + 1)) {
							changed |= ImGui::DragFloat("Start", &window.startTime, 0.01f, 0.0f, 60.0f);
							changed |= ImGui::DragFloat("End", &window.endTime, 0.01f, window.startTime, 60.0f);
							const SceneEntity* selectedHitBox =
								window.hitBoxEntityId != 0
									? document.FindEntity(window.hitBoxEntityId)
									: nullptr;
							if (!selectedHitBox && !window.hitBoxEntityName.empty()) {
								selectedHitBox = document.FindEntityByName(window.hitBoxEntityName);
							}
							if (window.payloadSource == "HitBox") {
								ImGui::TextDisabled("Payload Source: Dedicated HitBox");
								const char* hitBoxPreview = selectedHitBox && hasHitBox(*selectedHitBox)
									? selectedHitBox->name.c_str()
									: "Missing Dedicated HitBox";
								if (ImGui::BeginCombo("Dedicated HitBox", hitBoxPreview)) {
									for (const SceneEntity& candidate : document.GetEntities()) {
										if (!hasHitBox(candidate)) { continue; }
										const bool selected = candidate.id == window.hitBoxEntityId;
										const std::string label = candidate.name + " (" +
											std::to_string(candidate.id) + ")";
										if (ImGui::Selectable(label.c_str(), selected)) {
											window.hitBoxEntityId = candidate.id;
											window.hitBoxEntityName = candidate.name;
											changed = true;
										}
									}
									ImGui::EndCombo();
								}
								if (selectedHitBox) {
									const SceneComponent* hitBox = FindComponent(*selectedHitBox, "HitBox");
									if (hitBox) {
										ImGui::TextDisabled(
											"Damage %.1f | Poise %.1f | Knockback %.1f | Vertical %.1f",
											hitBox->hitBoxDamage,
											hitBox->hitBoxPoiseDamage,
											hitBox->hitBoxKnockback,
											hitBox->hitBoxVerticalKnockback
										);
									}
									if (ImGui::SmallButton("Select Dedicated HitBox")) {
										prefabSelectedEntityId_ = selectedHitBox->id;
									}
								}
							} else {
								ImGui::TextDisabled("Payload Source: Window Legacy (migrate before new authoring)");
							std::string hitBoxPreview = "StateMachine HitBox (Fallback)";
							if (selectedHitBox && hasHitBox(*selectedHitBox)) {
								hitBoxPreview = selectedHitBox->name;
							} else if (window.hitBoxEntityId != 0 || !window.hitBoxEntityName.empty()) {
								hitBoxPreview = "Missing HitBox";
							}
							if (ImGui::BeginCombo("HitBox", hitBoxPreview.c_str())) {
								const bool usesFallback = window.hitBoxEntityId == 0 &&
									window.hitBoxEntityName.empty();
								if (ImGui::Selectable("StateMachine HitBox (Fallback)", usesFallback)) {
									window.hitBoxEntityId = 0;
									window.hitBoxEntityName.clear();
									changed = true;
								}
								for (const SceneEntity& candidate : document.GetEntities()) {
									if (!hasHitBox(candidate)) { continue; }
									const bool selected = candidate.id == window.hitBoxEntityId;
									const std::string label = candidate.name + " (" + std::to_string(candidate.id) + ")";
									if (ImGui::Selectable(label.c_str(), selected)) {
										window.hitBoxEntityId = candidate.id;
										window.hitBoxEntityName = candidate.name;
										changed = true;
									}
								}
								ImGui::EndCombo();
							}
							const SceneComponent* selectedCollider = selectedHitBox
								? FindEnabledComponent(*selectedHitBox, "OBBCollider")
								: nullptr;
							const bool canOverrideHalfSize = selectedCollider &&
								selectedCollider->colliderShape == "Box";
							if (!canOverrideHalfSize) {
								ImGui::TextDisabled(
									"Half Size Override requires the selected HitBox to have a Box OBBCollider."
								);
							}
							changed |= ImGui::Checkbox(
								"Override HitBox Half Size", &window.overrideHitBoxHalfSize
							);
							ImGui::BeginDisabled(!canOverrideHalfSize);
							if (window.overrideHitBoxHalfSize) {
								changed |= ImGui::DragFloat3(
									"HitBox Half Size", &window.hitBoxHalfSize.x,
									0.01f, 0.001f, 10000.0f
								);
								window.hitBoxHalfSize.x = (std::max)(window.hitBoxHalfSize.x, 0.001f);
								window.hitBoxHalfSize.y = (std::max)(window.hitBoxHalfSize.y, 0.001f);
								window.hitBoxHalfSize.z = (std::max)(window.hitBoxHalfSize.z, 0.001f);
							}
							ImGui::EndDisabled();
							ImGui::SeparatorText("Damage & Reaction");
							changed |= ImGui::DragFloat("Damage", &window.damage, 0.1f, 0.0f, 100000.0f);
							changed |= ImGui::DragFloat("Poise Damage", &window.poiseDamage, 0.1f, 0.0f, 100000.0f);
							changed |= ImGui::DragFloat("Knockback", &window.knockback, 0.1f, 0.0f, 100000.0f);
							changed |= ImGui::DragFloat(
								"Vertical Knockback", &window.verticalKnockback,
								0.1f, 0.0f, 100000.0f
							);
							changed |= ImGui::DragFloat(
								"Hit Stop Duration", &window.hitStopDuration,
								0.001f, 0.0f, 1.0f
							);
							changed |= InputTextString("Reaction Tag", window.reactionTag);
							struct HitPolicyOption {
								const char* value;
								const char* label;
							};
							static constexpr HitPolicyOption hitPolicies[] = {
								{ "OncePerActivation", "Once Per Activation" },
								{ "OncePerLoop", "Once Per Loop" },
								{ "TargetCooldown", "Target Cooldown" }
							};
							const char* policyPreview = "Once Per Activation";
							for (const HitPolicyOption& option : hitPolicies) {
								if (window.hitPolicy == option.value) {
									policyPreview = option.label;
									break;
								}
							}
							if (ImGui::BeginCombo("Hit Policy", policyPreview)) {
								for (const HitPolicyOption& option : hitPolicies) {
									const bool selected = window.hitPolicy == option.value;
									if (ImGui::Selectable(option.label, selected)) {
										window.hitPolicy = option.value;
										changed = true;
									}
								}
								ImGui::EndCombo();
							}
							if (window.hitPolicy == "TargetCooldown") {
								changed |= ImGui::DragFloat(
									"Target Cooldown", &window.targetCooldown,
									0.01f, 0.0f, 60.0f
								);
							}
							struct DirectionModeOption {
								const char* value;
								const char* label;
							};
							static constexpr DirectionModeOption directionModes[] = {
								{ "RadialFromAttacker", "Radial from Attacker" },
								{ "AttackFacingLocal", "Attack Facing Local" },
								{ "HitBoxLocal", "HitBox Local" },
								{ "World", "World" }
							};
							const char* directionPreview = "Radial from Attacker";
							for (const DirectionModeOption& option : directionModes) {
								if (window.knockbackDirectionMode == option.value) {
									directionPreview = option.label;
									break;
								}
							}
							if (ImGui::BeginCombo("Direction Mode", directionPreview)) {
								for (const DirectionModeOption& option : directionModes) {
									const bool selected = window.knockbackDirectionMode == option.value;
									if (ImGui::Selectable(option.label, selected)) {
										window.knockbackDirectionMode = option.value;
										changed = true;
									}
								}
								ImGui::EndCombo();
							}
							const bool usesLocalDirection =
								window.knockbackDirectionMode != "RadialFromAttacker";
							ImGui::BeginDisabled(!usesLocalDirection);
							changed |= ImGui::DragFloat3("Local Direction", &window.knockbackLocalDirection.x, 0.01f);
							ImGui::EndDisabled();
							if (selectedHitBox && ImGui::SmallButton("Create Dedicated HitBox From Window")) {
								const uint64_t dedicatedId = createDedicatedHitBox(
									attack, windowIndex, true
								);
								if (dedicatedId != 0) {
									prefabSelectedEntityId_ = dedicatedId;
									changed = true;
								}
							}
							}
							if (ImGui::SmallButton("Remove Hit Window")) removeWindow = static_cast<int>(windowIndex);
							ImGui::TreePop();
						}
						ImGui::PopID();
					}
					if (removeWindow >= 0) { attack.hitWindows.erase(attack.hitWindows.begin() + removeWindow); changed = true; }
					if (ImGui::SmallButton("Add Hit Window")) {
						attack.hitWindows.push_back(SceneAttackHitWindow{});
						const size_t newWindowIndex = attack.hitWindows.size() - 1;
						const uint64_t dedicatedId = createDedicatedHitBox(
							attack, newWindowIndex, false
						);
						if (dedicatedId == 0) {
							attack.hitWindows.pop_back();
						} else {
							prefabSelectedEntityId_ = dedicatedId;
							changed = true;
						}
					}
					ImGui::SeparatorText("Effect Events");
					int removeEffect = -1;
					for (size_t effectIndex = 0;
						effectIndex < attack.effectEvents.size(); ++effectIndex) {
						SceneAttackEffectEvent& effect = attack.effectEvents[effectIndex];
						ImGui::PushID(static_cast<int>(effectIndex));
						if (ImGui::TreeNodeEx(
							"Effect Event", ImGuiTreeNodeFlags_DefaultOpen,
							"Effect Event %zu", effectIndex + 1
						)) {
							changed |= ImGui::DragFloat(
								"Time", &effect.time, 0.01f, 0.0f, 60.0f
							);
							changed |= InputTextString(
								"Particle Effect Path", effect.particleEffectPath
							);
							const SceneEntity* selectedSpawn = effect.spawnEntityId != 0
								? document.FindEntity(effect.spawnEntityId)
								: nullptr;
							if (!selectedSpawn && !effect.spawnEntityName.empty()) {
								selectedSpawn = document.FindEntityByName(effect.spawnEntityName);
							}
							const char* spawnPreview = selectedSpawn
								? selectedSpawn->name.c_str()
								: "AttackSet (Fallback)";
							if (ImGui::BeginCombo("Spawn Entity", spawnPreview)) {
								if (ImGui::Selectable(
									"AttackSet (Fallback)", effect.spawnEntityId == 0 &&
									effect.spawnEntityName.empty()
								)) {
									effect.spawnEntityId = 0;
									effect.spawnEntityName.clear();
									changed = true;
								}
								for (const SceneEntity& candidate : document.GetEntities()) {
									const bool selected = candidate.id == effect.spawnEntityId;
									const std::string label = candidate.name + " (" +
										std::to_string(candidate.id) + ")";
									if (ImGui::Selectable(label.c_str(), selected)) {
										effect.spawnEntityId = candidate.id;
										effect.spawnEntityName = candidate.name;
										changed = true;
									}
								}
								ImGui::EndCombo();
							}
							changed |= ImGui::DragFloat3(
								"Local Offset", &effect.localOffset.x, 0.01f
							);
							const char* groundEffectTypes[] = {
								"None", "Prefab", "ProceduralCrack"
							};
							int groundEffectTypeIndex = effect.groundEffectType == "Prefab"
								? 1
								: effect.groundEffectType == "ProceduralCrack" ? 2 : 0;
							if (ImGui::Combo(
								"Ground Effect Type",
								&groundEffectTypeIndex,
								groundEffectTypes,
								IM_ARRAYSIZE(groundEffectTypes)
							)) {
								effect.groundEffectType =
									groundEffectTypes[groundEffectTypeIndex];
								changed = true;
							}
							if (groundEffectTypeIndex != 0) {
								changed |= ImGui::DragFloat(
									"Ground Probe Distance", &effect.groundProbeDistance,
									0.05f, 0.0f, 100.0f
								);
							}
							if (groundEffectTypeIndex == 1) {
								changed |= InputTextString(
									"Ground Prefab Path", effect.groundPrefabPath
								);
								changed |= ImGui::DragFloat(
									"Ground Prefab Lifetime", &effect.groundPrefabLifetime,
									0.05f, 0.0f, 60.0f
								);
							} else if (groundEffectTypeIndex == 2) {
								int primaryBranchCount = static_cast<int>(
									effect.groundCrackPrimaryBranchCount
								);
								int segmentsPerBranch = static_cast<int>(
									effect.groundCrackSegmentsPerBranch
								);
								changed |= ImGui::DragFloat(
									"Crack Radius", &effect.groundCrackRadius,
									0.05f, 0.0f, 100.0f
								);
								if (ImGui::DragInt(
									"Primary Branch Count", &primaryBranchCount, 1.0f, 1, 24
								)) {
									effect.groundCrackPrimaryBranchCount =
										static_cast<uint32_t>(primaryBranchCount);
									changed = true;
								}
								if (ImGui::DragInt(
									"Segments Per Branch", &segmentsPerBranch, 1.0f, 1, 12
								)) {
									effect.groundCrackSegmentsPerBranch =
										static_cast<uint32_t>(segmentsPerBranch);
									changed = true;
								}
								changed |= ImGui::DragFloat(
									"Branch Probability", &effect.groundCrackBranchProbability,
									0.01f, 0.0f, 1.0f
								);
								changed |= ImGui::DragFloat(
									"Crack Width", &effect.groundCrackWidth,
									0.005f, 0.0f, 10.0f
								);
								changed |= ImGui::DragFloat(
									"Crack Lifetime", &effect.groundCrackLifetime,
									0.05f, 0.0f, 60.0f
								);
								changed |= ImGui::DragFloat(
									"Crack Surface Offset", &effect.groundCrackSurfaceOffset,
									0.001f, 0.0f, 1.0f
								);
							}
							if (ImGui::SmallButton("Remove Effect Event")) {
								removeEffect = static_cast<int>(effectIndex);
							}
							ImGui::TreePop();
						}
						ImGui::PopID();
					}
					if (removeEffect >= 0) {
						attack.effectEvents.erase(
							attack.effectEvents.begin() + removeEffect
						);
						changed = true;
					}
					if (ImGui::SmallButton("Add Effect Event")) {
						attack.effectEvents.push_back(SceneAttackEffectEvent{});
						changed = true;
					}
					if (ImGui::SmallButton("Remove Attack")) removeAttack = static_cast<int>(attackIndex);
					ImGui::TreePop();
				}
				ImGui::PopID();
			}
			if (removeAttack >= 0) { component.attackDefinitions.erase(component.attackDefinitions.begin() + removeAttack); changed = true; }
			if (!clipFocusName && ImGui::Button("Add Attack")) {
				component.attackDefinitions.push_back(SceneAttackDefinition{});
				changed = true;
			}
		} else if (component.type == "PrefabAnimator") {
			int removeClipIndex = -1;
			for (size_t clipIndex = 0;
				clipIndex < component.prefabAnimationClips.size();
				++clipIndex) {
				if (
					clipFocusActive &&
					static_cast<int>(clipIndex) != prefabAnimationPreviewClipIndex_
				) {
					continue;
				}
				ScenePrefabAnimationClip& clip =
					component.prefabAnimationClips[clipIndex];
				ImGui::PushID(static_cast<int>(clipIndex));
				if (ImGui::TreeNodeEx(
					"Clip", ImGuiTreeNodeFlags_DefaultOpen, "%s", clip.name.c_str()
				)) {
					changed |= InputTextString("Clip Name", clip.name);
					changed |= ImGui::DragFloat(
						"Duration", &clip.duration, 0.01f, 0.001f, 3600.0f
					);
					changed |= ImGui::Checkbox("Loop", &clip.loop);
					changed |= ImGui::Checkbox("Play On Start", &clip.playOnStart);
					int removeTrackIndex = -1;
					for (size_t trackIndex = 0;
						trackIndex < clip.tracks.size();
						++trackIndex) {
						SceneAnimationTrack& track = clip.tracks[trackIndex];
						ImGui::PushID(static_cast<int>(trackIndex));
						if (ImGui::TreeNodeEx(
							"Track", ImGuiTreeNodeFlags_DefaultOpen, "%s", track.property.c_str()
						)) {
							const SceneEntity* trackTarget = track.targetEntityId != 0
								? document.FindEntity(track.targetEntityId)
								: entity;
							if (ImGui::BeginCombo(
								"Target",
								trackTarget ? trackTarget->name.c_str() : "Self"
							)) {
								if (ImGui::Selectable("Self", track.targetEntityId == 0)) {
									track.targetEntityId = 0;
									track.targetEntityName.clear();
									changed = true;
								}
								for (const SceneEntity& candidate : document.GetEntities()) {
									if (ImGui::Selectable(
										candidate.name.c_str(),
										track.targetEntityId == candidate.id
									)) {
										track.targetEntityId = candidate.id;
										track.targetEntityName = candidate.name;
										changed = true;
									}
								}
								ImGui::EndCombo();
							}
							if (ImGui::BeginCombo("Property", track.property.c_str())) {
								for (const char* property : {
									"LocalPosition", "LocalRotation", "LocalScale", "Active"
								}) {
									if (ImGui::Selectable(
										property, track.property == property
									)) {
										track.property = property;
										changed = true;
									}
								}
								ImGui::EndCombo();
							}
							if (track.property != "Active" && ImGui::BeginCombo(
								"Easing",
								track.easing.empty() ? "SmoothStep" : track.easing.c_str()
							)) {
								for (const char* easing : {
									"Linear", "EaseIn", "EaseOut", "EaseInOut",
									"SmoothStep"
								}) {
									if (ImGui::Selectable(easing, track.easing == easing)) {
										track.easing = easing;
										changed = true;
									}
								}
								ImGui::EndCombo();
							}
							int removeKeyIndex = -1;
							bool keyframeTimeChanged = false;
							for (size_t keyIndex = 0;
								keyIndex < track.keyframes.size();
								++keyIndex) {
								SceneAnimationKeyframe& key = track.keyframes[keyIndex];
								ImGui::PushID(static_cast<int>(keyIndex));
								if (ImGui::DragFloat(
									"Time", &key.time, 0.01f, 0.0f, clip.duration
								)) {
									key.time = std::clamp(key.time, 0.0f, clip.duration);
									keyframeTimeChanged = true;
									changed = true;
								}
								if (track.property == "Active") {
									bool activeValue = key.value.x >= 0.5f;
									if (ImGui::Checkbox("Active Value", &activeValue)) {
										key.value.x = activeValue ? 1.0f : 0.0f;
										changed = true;
									}
								} else {
									changed |= ImGui::DragFloat3(
										track.property == "LocalRotation"
											? "Euler Value (Radians)"
											: "Value",
										&key.value.x,
										0.01f
									);
								}
								ImGui::SameLine();
								if (ImGui::SmallButton("X")) {
									removeKeyIndex = static_cast<int>(keyIndex);
								}
								ImGui::PopID();
							}
							if (removeKeyIndex >= 0) {
								track.keyframes.erase(
									track.keyframes.begin() + removeKeyIndex
								);
								changed = true;
							}
							if (keyframeTimeChanged) {
								std::stable_sort(
									track.keyframes.begin(),
									track.keyframes.end(),
									[](const SceneAnimationKeyframe& left,
										const SceneAnimationKeyframe& right) {
										return left.time < right.time;
									}
								);
							}
							if (ImGui::SmallButton("Add Keyframe")) {
								SceneAnimationKeyframe keyframe = track.keyframes.empty()
									? SceneAnimationKeyframe{}
									: track.keyframes.back();
								keyframe.time = track.keyframes.empty()
									? 0.0f
									: (std::min)(keyframe.time + 0.1f, clip.duration);
								keyframe.easingToNext.clear();
								keyframe.positionBulge = {};
								track.keyframes.push_back(keyframe);
								changed = true;
							}
							ImGui::SameLine();
							if (ImGui::SmallButton("Remove Track")) {
								removeTrackIndex = static_cast<int>(trackIndex);
							}
							ImGui::TreePop();
						}
						ImGui::PopID();
					}
					if (removeTrackIndex >= 0) {
						clip.tracks.erase(clip.tracks.begin() + removeTrackIndex);
						changed = true;
					}
					if (ImGui::SmallButton("Add Track")) {
						clip.tracks.push_back(SceneAnimationTrack{});
						changed = true;
					}
					ImGui::SameLine();
					if (ImGui::SmallButton("Remove Clip")) {
						removeClipIndex = static_cast<int>(clipIndex);
					}
					ImGui::TreePop();
				}
				ImGui::PopID();
			}
			if (removeClipIndex >= 0) {
				component.prefabAnimationClips.erase(
					component.prefabAnimationClips.begin() + removeClipIndex
				);
				changed = true;
			}
			if (ImGui::Button("Add Clip")) {
				component.prefabAnimationClips.push_back(ScenePrefabAnimationClip{});
				changed = true;
			}
		}

		if (changed) {
			document.MarkDirty();
		}
		if (ImGui::SmallButton("Remove Component")) {
			removeComponentIndex = static_cast<int>(componentIndex);
		}
		ImGui::Separator();
		ImGui::PopID();
	}
	if (removeComponentIndex >= 0) {
		const std::string type = entity->components[removeComponentIndex].type;
		document.RemoveComponent(entity->id, type);
	}

	ImGui::SeparatorText("Add Component");
	static int componentTypeIndex = 0;
	static constexpr const char* componentTypes[] = {
		"MeshRenderer", "Animator", "OBBCollider", "HitBox", "HurtBox",
		"BoneAttachment", "PrefabAnimator", "AttackSet", "Faction", "StateMachine"
	};
	componentTypeIndex = std::clamp(
		componentTypeIndex,
		0,
		static_cast<int>(IM_ARRAYSIZE(componentTypes) - 1)
	);
	if (ImGui::BeginCombo("##PrefabComponentType", componentTypes[componentTypeIndex])) {
		for (int index = 0; index < IM_ARRAYSIZE(componentTypes); ++index) {
			if (ImGui::Selectable(
				componentTypes[index], componentTypeIndex == index
			)) {
				componentTypeIndex = index;
			}
		}
		ImGui::EndCombo();
	}
	ImGui::SameLine();
	if (ImGui::Button("Add")) {
		document.AddComponent(entity->id, componentTypes[componentTypeIndex]);
	}
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
