#include "ParticleManager.h"
#include "ParticleCommon.h"

#include "../3d/SrvManager.h"
#include "../3d/Camera.h"
#include "../base/DirectXCommon.h"
#include "../2d/TextureManager.h"

#include <cassert>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <random>

#include "../externals/imgui/imgui.h"
#include "../externals/nlohmann/json.hpp"
#include "ParticleEffectResource.h"

using json = nlohmann::json;

ParticleManager* ParticleManager::instance_ = nullptr;

ParticleManager* ParticleManager::GetInstance() {
	if (instance_ == nullptr) {
		instance_ = new ParticleManager();
	}
	return instance_;
}

void ParticleManager::DeleteInstance() {
	delete instance_;
	instance_ = nullptr;
}

ParticleManager::~ParticleManager() {
	for (auto& [sceneName, placements] : sceneParticlePlacements_) {
		for (SceneParticlePlacement& placement : placements) {
			delete placement.effect;
			placement.effect = nullptr;
			delete placement.emitter;
			placement.emitter = nullptr;
		}
	}
}

void ParticleManager::Initialize(ParticleCommon* particleCommon, SrvManager* srvManager) {
	assert(particleCommon);
	assert(srvManager);

	particleCommon_ = particleCommon;
	srvManager_ = srvManager;
	camera_ = particleCommon_->GetDefaultCamera();

	CreateDirectionalLightResource();
	gpuParticle_.Initialize(particleCommon_, srvManager_, "resources/circle.png");
}

void ParticleManager::Reset() {
	for (auto& [sceneName, placements] : sceneParticlePlacements_) {
		for (SceneParticlePlacement& placement : placements) {
			delete placement.effect;
			placement.effect = nullptr;
			delete placement.emitter;
			placement.emitter = nullptr;
		}
	}
	sceneParticlePlacements_.clear();
	sceneParticleLayoutLoaded_ = false;

	particleGroups_.clear();
	gpuParticle_.Reset();
	gpuParticleEnabled_ = false;
	pendingLightningEvents_.clear();
	pendingExposureFlashEvents_.clear();
	lightningSeed_ = 1;

	directionalLightResource_.Reset();
	directionalLightData_ = nullptr;

	particleCommon_ = nullptr;
	srvManager_ = nullptr;
	camera_ = nullptr;
}

void ParticleManager::QueueLightning(
	const LightningEmitterDesc& desc,
	const Vector3& emitterPosition
) {
	if (!desc.enabled) {
		return;
	}

	const Vector3 halfRange = {
		desc.randomRange.x * 0.5f,
		desc.randomRange.y * 0.5f,
		desc.randomRange.z * 0.5f
	};
	const Vector3 randomOffset = RandomVector3Range(
		{ -halfRange.x, -halfRange.y, -halfRange.z },
		{ halfRange.x, halfRange.y, halfRange.z }
	);

	LightningEvent event{};
	event.desc = desc;
	event.start = {
		emitterPosition.x + desc.startOffset.x + randomOffset.x,
		emitterPosition.y + desc.startOffset.y + randomOffset.y,
		emitterPosition.z + desc.startOffset.z + randomOffset.z
	};
	event.end = {
		emitterPosition.x + desc.endOffset.x + randomOffset.x,
		emitterPosition.y + desc.endOffset.y + randomOffset.y,
		emitterPosition.z + desc.endOffset.z + randomOffset.z
	};
	event.seed = lightningSeed_++;

	pendingLightningEvents_.push_back(event);

	if (desc.flashExposure) {
		pendingExposureFlashEvents_.push_back({
			desc.flashExposureValue,
			desc.flashReturnSpeed
		});
	}
}

bool ParticleManager::ConsumeLightningEvent(LightningEvent& outEvent) {
	if (pendingLightningEvents_.empty()) {
		return false;
	}

	outEvent = pendingLightningEvents_.front();
	pendingLightningEvents_.erase(pendingLightningEvents_.begin());
	return true;
}

bool ParticleManager::ConsumeExposureFlashEvent(
	ExposureFlashEvent& outEvent
) {
	if (pendingExposureFlashEvents_.empty()) {
		return false;
	}

	outEvent = pendingExposureFlashEvents_.front();
	pendingExposureFlashEvents_.erase(pendingExposureFlashEvents_.begin());
	return true;
}

void ParticleManager::SetGroupBlendMode(
	const std::string& name,
	ParticleCommon::BlendMode blendMode
) {
	auto it = particleGroups_.find(name);
	assert(it != particleGroups_.end());

	it->second.blendMode = blendMode;
}

void ParticleManager::SetGroupRenderDesc(
	const std::string& name,
	const ParticleRenderDesc& render
) {
	auto it = particleGroups_.find(name);
	assert(it != particleGroups_.end());

	ParticleGroup& group = it->second;
	const ParticleRenderDesc& current = group.render;
	const auto sameColor = [](const Vector4& a, const Vector4& b) {
		return a.x == b.x && a.y == b.y && a.z == b.z && a.w == b.w;
	};
	const bool geometryUnchanged =
		current.primitiveType == render.primitiveType &&
		current.ring.divisions == render.ring.divisions &&
		current.ring.outerRadius == render.ring.outerRadius &&
		current.ring.innerRadius == render.ring.innerRadius &&
		current.ring.startAngle == render.ring.startAngle &&
		current.ring.endAngle == render.ring.endAngle &&
		sameColor(current.ring.outerColor, render.ring.outerColor) &&
		sameColor(current.ring.innerColor, render.ring.innerColor) &&
		current.ring.uvMode == render.ring.uvMode &&
		current.cylinder.divisions == render.cylinder.divisions &&
		current.cylinder.topRadius == render.cylinder.topRadius &&
		current.cylinder.bottomRadius == render.cylinder.bottomRadius &&
		current.cylinder.height == render.cylinder.height &&
		current.cylinder.startAngle == render.cylinder.startAngle &&
		current.cylinder.endAngle == render.cylinder.endAngle &&
		sameColor(current.cylinder.topColor, render.cylinder.topColor) &&
		sameColor(current.cylinder.bottomColor, render.cylinder.bottomColor) &&
		current.cylinder.uvMode == render.cylinder.uvMode;

	group.render = render;
	group.materialData->alphaCutoff = std::clamp(render.alphaCutoff, 0.0f, 1.0f);
	group.materialData->flipU = render.flipU ? 1 : 0;
	group.materialData->flipV = render.flipV ? 1 : 0;
	group.materialData->emissiveIntensity = (std::max)(0.0f, render.emissiveIntensity);

	if (!geometryUnchanged) {
		CreateGroupVertexResource(group);
	}
}

void ParticleManager::DrawGpuParticleImGui(const char* windowTitle) {
	gpuParticle_.DrawImGui(windowTitle);
}

namespace {

constexpr float kMinEmitterFrequency = 1.0f / 60.0f;

float NormalizeEmitterFrequency(float frequency) {
	return frequency > 0.0f ? frequency : kMinEmitterFrequency;
}

json ToJson(const Vector3& v) {
	return json::array({ v.x, v.y, v.z });
}

Vector3 ReadVector3(const json& j, const Vector3& defaultValue) {
	if (!j.is_array() || j.size() < 3) {
		return defaultValue;
	}

	return {
		j.at(0).get<float>(),
		j.at(1).get<float>(),
		j.at(2).get<float>()
	};
}

void CopyText(char* destination, size_t destinationSize, const std::string& source) {
	if (!destination || destinationSize == 0) {
		return;
	}
	strncpy_s(destination, destinationSize, source.c_str(), _TRUNCATE);
}

std::string MakeDefaultEffectPath(const char* name) {
	std::string safeName = name && name[0] != '\0' ? name : "newParticle";
	for (char& c : safeName) {
		if (c == '\\' || c == '/' || c == ':' || c == '*' || c == '?' ||
			c == '"' || c == '<' || c == '>' || c == '|') {
			c = '_';
		}
	}
	return "resources/particles/" + safeName + ".json";
}

void CollectEffectFiles(std::vector<std::string>& paths, std::vector<std::string>& names) {
	paths.clear();
	names.clear();

	std::error_code error;
	for (const std::filesystem::directory_entry& entry :
		std::filesystem::directory_iterator("resources/particles", error)) {
		if (error) {
			break;
		}
		if (!entry.is_regular_file() || entry.path().extension() != ".json") {
			continue;
		}
		if (entry.path().filename() == "scene_particles.json") {
			continue;
		}
		paths.push_back(entry.path().generic_string());
	}

	std::sort(paths.begin(), paths.end());
	for (const std::string& path : paths) {
		names.push_back(std::filesystem::path(path).filename().string());
	}
}

} // namespace

void ParticleManager::ApplySceneParticleEmitterSettings(SceneParticlePlacement& placement) {
	if (!placement.emitter) {
		RebuildSceneParticleEmitter(placement, false);
		return;
	}
	if (!placement.effect) {
		RebuildSceneParticleEmitter(placement, false);
		return;
	}

	placement.effect->emitter.translate = placement.translate;
	placement.effect->emitter.spawnSize = placement.spawnSize;
	placement.effect->emitter.count = placement.count;
	placement.effect->emitter.frequency = NormalizeEmitterFrequency(placement.frequency);
	placement.effect->emitter.isActive = placement.emitterActive;

	ParticleEffectResource::PrepareParticleGroup(*placement.effect, false);
	ParticleEffectResource::ApplyToEmitter(*placement.emitter, *placement.effect);
}

void ParticleManager::RebuildSceneParticleEmitter(
	SceneParticlePlacement& placement,
	bool clearParticles,
	bool useEffectEmitterSettings
) {
	delete placement.emitter;
	placement.emitter = nullptr;

	if (!placement.effect) {
		placement.effect = new ParticleEffectDesc();
	}

	if (!ParticleEffectResource::Load(placement.effectFilePath, *placement.effect)) {
		*placement.effect = ParticleEffectDesc{};
		placement.effect->name = placement.label.empty() ? "newParticle" : placement.label;
	}

	if (useEffectEmitterSettings) {
		placement.translate = placement.effect->emitter.translate;
		placement.spawnSize = placement.effect->emitter.spawnSize;
		placement.count = placement.effect->emitter.count;
		placement.frequency = NormalizeEmitterFrequency(placement.effect->emitter.frequency);
		placement.emitterActive = placement.effect->emitter.isActive;
	}

	placement.effect->emitter.translate = placement.translate;
	placement.effect->emitter.spawnSize = placement.spawnSize;
	placement.effect->emitter.count = placement.count;
	placement.effect->emitter.frequency = NormalizeEmitterFrequency(placement.frequency);
	placement.effect->emitter.isActive = placement.emitterActive;

	ParticleEffectResource::PrepareParticleGroup(*placement.effect, clearParticles);
	placement.emitter = new ParticleEmitter();
	placement.emitter->Initialize(this, placement.effect->name);
	ParticleEffectResource::ApplyToEmitter(*placement.emitter, *placement.effect);
}

void ParticleManager::LoadSceneParticleLayout(const std::string& filePath) {
	for (auto& [sceneName, placements] : sceneParticlePlacements_) {
		for (SceneParticlePlacement& placement : placements) {
			delete placement.effect;
			placement.effect = nullptr;
			delete placement.emitter;
			placement.emitter = nullptr;
		}
	}
	sceneParticlePlacements_.clear();

	std::ifstream file(filePath);
	if (!file.is_open()) {
		sceneParticlePlacements_["TITLE"];
		sceneParticlePlacements_["GAMEPLAY"];
		sceneParticleLayoutLoaded_ = true;
		return;
	}

	json root;
	try {
		file >> root;
	} catch (...) {
		sceneParticlePlacements_["TITLE"];
		sceneParticlePlacements_["GAMEPLAY"];
		sceneParticleLayoutLoaded_ = true;
		return;
	}

	if (root.contains("scenes") && root.at("scenes").is_object()) {
		const json& scenes = root.at("scenes");
		for (auto it = scenes.begin(); it != scenes.end(); ++it) {
			const std::string sceneName = it.key();
			std::vector<SceneParticlePlacement>& placements =
				sceneParticlePlacements_[sceneName];

			if (!it.value().is_array()) {
				continue;
			}

			for (const json& placementJson : it.value()) {
				SceneParticlePlacement placement{};
				placement.sceneName = sceneName;
				placement.label = placementJson.value("label", placement.label);
				placement.effectFilePath =
					placementJson.value("effectFilePath", placement.effectFilePath);
				placement.enabled = placementJson.value("enabled", placement.enabled);
				placement.emitterActive =
					placementJson.value("emitterActive", placement.emitterActive);
				const bool hasEmitterSettings =
					placementJson.contains("translate") ||
					placementJson.contains("spawnSize") ||
					placementJson.contains("count") ||
					placementJson.contains("frequency") ||
					placementJson.contains("emitterActive");
				if (placementJson.contains("translate")) {
					placement.translate =
						ReadVector3(placementJson.at("translate"), placement.translate);
				}
				if (placementJson.contains("spawnSize")) {
					placement.spawnSize =
						ReadVector3(placementJson.at("spawnSize"), placement.spawnSize);
				}
				placement.count = placementJson.value("count", placement.count);
				placement.frequency =
					placementJson.value("frequency", placement.frequency);
				placement.frequency = NormalizeEmitterFrequency(placement.frequency);

				RebuildSceneParticleEmitter(placement, false, !hasEmitterSettings);
				placements.push_back(placement);
				placements.back().effect = placement.effect;
				placements.back().emitter = placement.emitter;
				placement.effect = nullptr;
				placement.emitter = nullptr;
			}
		}
	}

	sceneParticlePlacements_["TITLE"];
	sceneParticlePlacements_["GAMEPLAY"];
	sceneParticleLayoutLoaded_ = true;
}

void ParticleManager::SaveSceneParticleLayout(const std::string& filePath) const {
	json root;
	root["scenes"] = json::object();

	for (const auto& [sceneName, placements] : sceneParticlePlacements_) {
		json sceneArray = json::array();
		for (const SceneParticlePlacement& placement : placements) {
			sceneArray.push_back({
				{ "label", placement.label },
				{ "effectFilePath", placement.effectFilePath },
				{ "enabled", placement.enabled },
				{ "emitterActive", placement.emitterActive },
				{ "translate", ToJson(placement.translate) },
				{ "spawnSize", ToJson(placement.spawnSize) },
				{ "count", placement.count },
				{ "frequency", placement.frequency }
			});
		}
		root["scenes"][sceneName] = sceneArray;
	}

	const std::filesystem::path parentPath =
		std::filesystem::path(filePath).parent_path();
	if (!parentPath.empty()) {
		std::filesystem::create_directories(parentPath);
	}

	std::ofstream file(filePath);
	if (!file.is_open()) {
		return;
	}
	file << std::setw(2) << root << std::endl;
}

void ParticleManager::UpdateSceneParticles(const std::string& sceneName) {
	if (!sceneParticleLayoutLoaded_) {
		LoadSceneParticleLayout();
	}

	auto it = sceneParticlePlacements_.find(sceneName);
	if (it == sceneParticlePlacements_.end()) {
		return;
	}

	for (SceneParticlePlacement& placement : it->second) {
		if (placement.enabled && placement.emitter) {
			placement.emitter->Update();
		}
	}
}

void ParticleManager::EmitSceneParticles(const std::string& sceneName) {
	if (!sceneParticleLayoutLoaded_) {
		LoadSceneParticleLayout();
	}

	auto it = sceneParticlePlacements_.find(sceneName);
	if (it == sceneParticlePlacements_.end()) {
		return;
	}

	for (SceneParticlePlacement& placement : it->second) {
		if (placement.enabled && placement.emitter) {
			placement.emitter->Emit();
		}
	}
}

void ParticleManager::DrawSceneParticleImGui(
	const std::string& currentSceneName,
	const char* windowTitle
) {
#if defined(_DEBUG) || defined(DEVELOPMENT)
	if (!sceneParticleLayoutLoaded_) {
		LoadSceneParticleLayout();
	}

	static char layoutPath[260] = "resources/particles/scene_particles.json";
	static char newEffectName[128] = "newParticle";
	std::vector<std::string> effectPaths;
	std::vector<std::string> effectNames;
	CollectEffectFiles(effectPaths, effectNames);

	ImGui::Begin(windowTitle);
	ImGui::InputText("Layout", layoutPath, sizeof(layoutPath));
	if (ImGui::Button("Load Layout")) {
		LoadSceneParticleLayout(layoutPath);
	}
	ImGui::SameLine();
	if (ImGui::Button("Save Layout")) {
		SaveSceneParticleLayout(layoutPath);
	}

	ImGui::SeparatorText("Effect");
	ImGui::InputText("New Effect Name", newEffectName, sizeof(newEffectName));
	ImGui::SameLine();
	if (ImGui::Button("Create New Effect")) {
		ParticleEffectDesc newEffect{};
		newEffect.name = newEffectName;
		const std::string path = MakeDefaultEffectPath(newEffectName);
		ParticleEffectResource::Save(path, newEffect);
	}

	const char* sceneNames[] = { "TITLE", "GAMEPLAY" };
	if (ImGui::BeginTabBar("SceneParticleTabs")) {
		for (const char* sceneNameText : sceneNames) {
			const bool isCurrent = currentSceneName == sceneNameText;
			ImGuiTabItemFlags flags =
				isCurrent ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None;

			if (ImGui::BeginTabItem(sceneNameText, nullptr, flags)) {
				std::vector<SceneParticlePlacement>& placements =
					sceneParticlePlacements_[sceneNameText];

				if (ImGui::Button("Add Particle Placement")) {
					SceneParticlePlacement placement{};
					placement.sceneName = sceneNameText;
					placement.label =
						std::string("Particle ") + std::to_string(placements.size());
					if (!effectPaths.empty()) {
						placement.effectFilePath = effectPaths.front();
					}
					RebuildSceneParticleEmitter(placement, false, true);
					placements.push_back(placement);
					placements.back().effect = placement.effect;
					placements.back().emitter = placement.emitter;
					placement.effect = nullptr;
					placement.emitter = nullptr;
				}
				ImGui::SameLine();
				if (ImGui::Button("Add New Effect Here")) {
					ParticleEffectDesc newEffect{};
					newEffect.name = newEffectName;
					const std::string path = MakeDefaultEffectPath(newEffectName);
					ParticleEffectResource::Save(path, newEffect);

					SceneParticlePlacement placement{};
					placement.sceneName = sceneNameText;
					placement.label = newEffect.name;
					placement.effectFilePath = path;
					RebuildSceneParticleEmitter(placement, false, true);
					placements.push_back(placement);
					placements.back().effect = placement.effect;
					placements.back().emitter = placement.emitter;
					placement.effect = nullptr;
					placement.emitter = nullptr;
				}

				int removeIndex = -1;
				for (int index = 0; index < static_cast<int>(placements.size()); ++index) {
					SceneParticlePlacement& placement = placements[index];
					ImGui::PushID(index);

					const std::string header =
						placement.label.empty() ? "(unnamed)" : placement.label;
					if (ImGui::CollapsingHeader(
						header.c_str(),
						ImGuiTreeNodeFlags_DefaultOpen
					)) {
						char label[128]{};
						char effectFilePath[260]{};
						CopyText(label, sizeof(label), placement.label);
						CopyText(
							effectFilePath,
							sizeof(effectFilePath),
							placement.effectFilePath
						);

						ImGui::Checkbox("Enabled", &placement.enabled);
						ImGui::SameLine();
						if (ImGui::Button("Emit Now") && placement.emitter) {
							placement.emitter->Emit();
						}
						ImGui::SameLine();
						if (ImGui::Button("Remove")) {
							removeIndex = index;
						}

						if (ImGui::InputText("Label", label, sizeof(label))) {
							placement.label = label;
						}

						const std::string selectedEffectName =
							std::filesystem::path(placement.effectFilePath)
							.filename()
							.string();
						if (ImGui::BeginCombo("Effect File", selectedEffectName.c_str())) {
							for (int effectIndex = 0;
								effectIndex < static_cast<int>(effectPaths.size());
								++effectIndex) {
								const bool selected =
									placement.effectFilePath == effectPaths[effectIndex];
								if (ImGui::Selectable(
									effectNames[effectIndex].c_str(),
									selected
								)) {
									placement.effectFilePath = effectPaths[effectIndex];
									RebuildSceneParticleEmitter(placement, false, true);
								}
								if (selected) {
									ImGui::SetItemDefaultFocus();
								}
							}
							ImGui::EndCombo();
						}

						if (ImGui::InputText(
							"Effect Path",
							effectFilePath,
							sizeof(effectFilePath)
						)) {
							placement.effectFilePath = effectFilePath;
						}

						if (ImGui::Button("Reload Effect")) {
							RebuildSceneParticleEmitter(placement, false);
						}
						ImGui::SameLine();
						if (ImGui::Button("Load Effect Emitter")) {
							RebuildSceneParticleEmitter(placement, false, true);
						}

						bool changed = false;
						changed |= ImGui::DragFloat3(
							"Translate",
							&placement.translate.x,
							0.05f
						);
						changed |= ImGui::DragFloat3(
							"SpawnSize",
							&placement.spawnSize.x,
							0.05f
						);

						int count = static_cast<int>(placement.count);
						if (ImGui::DragInt("Count", &count, 1, 0, 1000)) {
							placement.count =
								static_cast<uint32_t>((std::max)(count, 0));
							changed = true;
						}
						changed |= ImGui::DragFloat(
							"Frequency",
							&placement.frequency,
							0.001f,
							1.0f / 60.0f,
							10.0f
						);
						if (placement.frequency <= 0.0f) {
							placement.frequency = 1.0f / 60.0f;
							changed = true;
						}
						changed |= ImGui::Checkbox(
							"Emitter Active",
							&placement.emitterActive
						);

						if (changed) {
							ApplySceneParticleEmitterSettings(placement);
						}
					}

					ImGui::PopID();
				}

				if (removeIndex >= 0 && removeIndex < static_cast<int>(placements.size())) {
					delete placements[removeIndex].effect;
					delete placements[removeIndex].emitter;
					placements.erase(placements.begin() + removeIndex);
				}

				ImGui::EndTabItem();
			}
		}
		ImGui::EndTabBar();
	}

	ImGui::End();
#else
	(void)currentSceneName;
	(void)windowTitle;
#endif
}

void ParticleManager::CreateGroupVertexResource(ParticleGroup& group) {
	if (group.render.primitiveType == PrimitiveType::kPlane) {
		group.vertexResource.Reset();
		group.vertexBufferView = particleCommon_->GetVertexBufferView();
		group.vertexCount = 6;
		return;
	}

	std::vector<ParticleCommon::VertexData> vertices;
	if (group.render.primitiveType == PrimitiveType::kRing) {
		const RingPrimitiveDesc& ring = group.render.ring;
		const uint32_t divisions = std::clamp(ring.divisions, 3u, 256u);
		const float outerRadius = (std::max)(ring.outerRadius, 0.001f);
		const float innerRadius = std::clamp(ring.innerRadius, 0.0f, outerRadius);
		const float angleRange = ring.endAngle - ring.startAngle;
		vertices.reserve(static_cast<size_t>(divisions) * 6);

		for (uint32_t index = 0; index < divisions; ++index) {
			const float t0 = static_cast<float>(index) / static_cast<float>(divisions);
			const float t1 = static_cast<float>(index + 1) / static_cast<float>(divisions);
			const float angle0 = ring.startAngle + angleRange * t0;
			const float angle1 = ring.startAngle + angleRange * t1;
			const float sin0 = std::sin(angle0);
			const float cos0 = std::cos(angle0);
			const float sin1 = std::sin(angle1);
			const float cos1 = std::cos(angle1);

			const Vector2 outerUv0 = ring.uvMode == RingUvMode::kHorizontal
				? Vector2{ t0, 0.0f } : Vector2{ 0.0f, t0 };
			const Vector2 outerUv1 = ring.uvMode == RingUvMode::kHorizontal
				? Vector2{ t1, 0.0f } : Vector2{ 0.0f, t1 };
			const Vector2 innerUv0 = ring.uvMode == RingUvMode::kHorizontal
				? Vector2{ t0, 1.0f } : Vector2{ 1.0f, t0 };
			const Vector2 innerUv1 = ring.uvMode == RingUvMode::kHorizontal
				? Vector2{ t1, 1.0f } : Vector2{ 1.0f, t1 };

			const ParticleCommon::VertexData outer0{
				{ -sin0 * outerRadius, cos0 * outerRadius, 0.0f, 1.0f },
				outerUv0, { 0.0f, 0.0f, -1.0f }, ring.outerColor
			};
			const ParticleCommon::VertexData outer1{
				{ -sin1 * outerRadius, cos1 * outerRadius, 0.0f, 1.0f },
				outerUv1, { 0.0f, 0.0f, -1.0f }, ring.outerColor
			};
			const ParticleCommon::VertexData inner0{
				{ -sin0 * innerRadius, cos0 * innerRadius, 0.0f, 1.0f },
				innerUv0, { 0.0f, 0.0f, -1.0f }, ring.innerColor
			};
			const ParticleCommon::VertexData inner1{
				{ -sin1 * innerRadius, cos1 * innerRadius, 0.0f, 1.0f },
				innerUv1, { 0.0f, 0.0f, -1.0f }, ring.innerColor
			};

			vertices.insert(vertices.end(), { outer0, outer1, inner0, inner0, outer1, inner1 });
		}
	}
	else {
		const CylinderPrimitiveDesc& cylinder = group.render.cylinder;
		const uint32_t divisions = std::clamp(cylinder.divisions, 3u, 256u);
		const float topRadius = (std::max)(cylinder.topRadius, 0.0f);
		const float bottomRadius = (std::max)(cylinder.bottomRadius, 0.0f);
		const float height = (std::max)(cylinder.height, 0.001f);
		const float angleRange = cylinder.endAngle - cylinder.startAngle;
		vertices.reserve(static_cast<size_t>(divisions) * 6);

		for (uint32_t index = 0; index < divisions; ++index) {
			const float t0 = static_cast<float>(index) / static_cast<float>(divisions);
			const float t1 = static_cast<float>(index + 1) / static_cast<float>(divisions);
			const float angle0 = cylinder.startAngle + angleRange * t0;
			const float angle1 = cylinder.startAngle + angleRange * t1;
			const float sin0 = std::sin(angle0);
			const float cos0 = std::cos(angle0);
			const float sin1 = std::sin(angle1);
			const float cos1 = std::cos(angle1);

			const Vector2 topUv0 = cylinder.uvMode == RingUvMode::kHorizontal
				? Vector2{ t0, 0.0f } : Vector2{ 0.0f, t0 };
			const Vector2 topUv1 = cylinder.uvMode == RingUvMode::kHorizontal
				? Vector2{ t1, 0.0f } : Vector2{ 0.0f, t1 };
			const Vector2 bottomUv0 = cylinder.uvMode == RingUvMode::kHorizontal
				? Vector2{ t0, 1.0f } : Vector2{ 1.0f, t0 };
			const Vector2 bottomUv1 = cylinder.uvMode == RingUvMode::kHorizontal
				? Vector2{ t1, 1.0f } : Vector2{ 1.0f, t1 };

			const Vector3 normal0 = { -sin0, 0.0f, cos0 };
			const Vector3 normal1 = { -sin1, 0.0f, cos1 };
			const ParticleCommon::VertexData top0{
				{ -sin0 * topRadius, height, cos0 * topRadius, 1.0f },
				topUv0, normal0, cylinder.topColor
			};
			const ParticleCommon::VertexData top1{
				{ -sin1 * topRadius, height, cos1 * topRadius, 1.0f },
				topUv1, normal1, cylinder.topColor
			};
			const ParticleCommon::VertexData bottom0{
				{ -sin0 * bottomRadius, 0.0f, cos0 * bottomRadius, 1.0f },
				bottomUv0, normal0, cylinder.bottomColor
			};
			const ParticleCommon::VertexData bottom1{
				{ -sin1 * bottomRadius, 0.0f, cos1 * bottomRadius, 1.0f },
				bottomUv1, normal1, cylinder.bottomColor
			};

			vertices.insert(vertices.end(), { top0, top1, bottom0, bottom0, top1, bottom1 });
		}
	}

	group.vertexResource = particleCommon_->GetDxCommon()->CreateBufferResource(
		sizeof(ParticleCommon::VertexData) * vertices.size()
	);
	ParticleCommon::VertexData* mappedVertices = nullptr;
	group.vertexResource->Map(0, nullptr, reinterpret_cast<void**>(&mappedVertices));
	std::memcpy(
		mappedVertices,
		vertices.data(),
		sizeof(ParticleCommon::VertexData) * vertices.size()
	);

	group.vertexBufferView.BufferLocation = group.vertexResource->GetGPUVirtualAddress();
	group.vertexBufferView.SizeInBytes =
		static_cast<UINT>(sizeof(ParticleCommon::VertexData) * vertices.size());
	group.vertexBufferView.StrideInBytes = sizeof(ParticleCommon::VertexData);
	group.vertexCount = static_cast<uint32_t>(vertices.size());
}

void ParticleManager::CreateDirectionalLightResource() {
	directionalLightResource_ = particleCommon_->GetDxCommon()->CreateBufferResource(sizeof(DirectionalLight));
	directionalLightResource_->Map(0, nullptr, reinterpret_cast<void**>(&directionalLightData_));

	directionalLightData_->color = { 1.0f, 1.0f, 1.0f, 1.0f };
	directionalLightData_->direction = { 0.0f, -1.0f, 0.0f };
	directionalLightData_->intensity = 1.0f;
}

float ParticleManager::RandomRange(float min, float max) {
	if (min > max) {
		std::swap(min, max);
	}

	static std::random_device seedGenerator;
	static std::mt19937 randomEngine(seedGenerator());

	std::uniform_real_distribution<float> distribution(min, max);
	return distribution(randomEngine);
}

Vector3 ParticleManager::RandomVector3Range(const Vector3& min, const Vector3& max) {
	return {
		RandomRange(min.x, max.x),
		RandomRange(min.y, max.y),
		RandomRange(min.z, max.z)
	};
}

Vector4 ParticleManager::RandomVector4Range(const Vector4& min, const Vector4& max) {
	return {
		RandomRange(min.x, max.x),
		RandomRange(min.y, max.y),
		RandomRange(min.z, max.z),
		RandomRange(min.w, max.w)
	};
}

Vector4 ParticleManager::LerpColor(const Vector4& start, const Vector4& end, float t) {
	t = std::clamp(t, 0.0f, 1.0f);

	return {
		start.x + (end.x - start.x) * t,
		start.y + (end.y - start.y) * t,
		start.z + (end.z - start.z) * t,
		start.w + (end.w - start.w) * t
	};
}

void ParticleManager::CreateParticleGroup(const std::string& name, const std::string& textureFilePath) {
	assert(particleCommon_);
	assert(srvManager_);

	assert(!particleGroups_.contains(name));
	assert(srvManager_->CanAllocate());

	TextureManager::GetInstance()->LoadTexture(textureFilePath);

	ParticleGroup group{};
	group.textureFilePath = textureFilePath;
	group.textureSrvIndex = TextureManager::GetInstance()->GetSrvIndex(textureFilePath);

	group.materialResource = particleCommon_->GetDxCommon()->CreateBufferResource(sizeof(Material));
	group.materialResource->Map(0, nullptr, reinterpret_cast<void**>(&group.materialData));
	group.materialData->color = { 1.0f, 1.0f, 1.0f, 1.0f };
	group.materialData->enableLighting = false;
	group.materialData->alphaCutoff = 0.0f;
	group.materialData->flipU = false;
	group.materialData->flipV = false;
	group.materialData->uvTransform = MakeIdentity4x4();
	group.materialData->emissiveIntensity = 1.0f;

	group.instancingResource =
		particleCommon_->GetDxCommon()->CreateBufferResource(sizeof(TransformationMatrix) * kMaxInstanceCount);

	group.instancingResource->Map(0, nullptr, reinterpret_cast<void**>(&group.instancingData));

	group.instanceSrvIndex = srvManager_->Allocate();

	srvManager_->CreateSRVforStructuredBuffer(
		group.instanceSrvIndex,
		group.instancingResource.Get(),
		kMaxInstanceCount,
		sizeof(TransformationMatrix)
	);
	group.render = {};
	CreateGroupVertexResource(group);

	particleGroups_.emplace(name, std::move(group));
}

bool ParticleManager::HasParticleGroup(const std::string& name) const {
	return particleGroups_.contains(name);
}

void ParticleManager::ClearParticleGroup(const std::string& name) {
	auto it = particleGroups_.find(name);
	if (it == particleGroups_.end()) {
		return;
	}

	it->second.particles.clear();
	it->second.instanceCount = 0;
}

void ParticleManager::CreateParticleGroupIfNeeded(
	const std::string& name,
	const std::string& textureFilePath
) {
	if (HasParticleGroup(name)) {
		return;
	}

	CreateParticleGroup(name, textureFilePath);
}

void ParticleManager::InitializeParticleLife(Particle& particle, const ParticleBehavior& behavior) {
	particle.currentTime = 0.0f;
	particle.lifeTime = RandomRange(behavior.life.lifeTimeMin, behavior.life.lifeTimeMax);
	particle.isLooping = behavior.life.isLooping;
	particle.loopDuration = (std::max)(behavior.life.loopDuration, 0.001f);
	particle.loopPingPong = behavior.life.loopPingPong;

	particle.enableLifeFade =
		!particle.isLooping && behavior.life.enableLifeFade;
	particle.fadeOutStartRatio = std::clamp(behavior.life.fadeOutStartRatio, 0.0f, 0.99f);
}

void ParticleManager::InitializeParticleMotion(
	Particle& particle,
	const ParticleBehavior& behavior,
	const Vector3& emitterPosition
) {
	particle.movementMode = behavior.motion.mode;

	const ParticleLinearMotionDesc& linear = behavior.motion.linear;

	Vector3 velocityRandom = RandomVector3Range(
		{
			-linear.velocityRandomRange.x,
			-linear.velocityRandomRange.y,
			-linear.velocityRandomRange.z
		},
		{
			linear.velocityRandomRange.x,
			linear.velocityRandomRange.y,
			linear.velocityRandomRange.z
		}
	);

	Vector3 accelerationRandom = RandomVector3Range(
		{
			-linear.accelerationRandomRange.x,
			-linear.accelerationRandomRange.y,
			-linear.accelerationRandomRange.z
		},
		{
			linear.accelerationRandomRange.x,
			linear.accelerationRandomRange.y,
			linear.accelerationRandomRange.z
		}
	);

	particle.velocity = {
		linear.baseVelocity.x + velocityRandom.x,
		linear.baseVelocity.y + velocityRandom.y,
		linear.baseVelocity.z + velocityRandom.z
	};

	particle.acceleration = {
		linear.baseAcceleration.x + accelerationRandom.x,
		linear.baseAcceleration.y + accelerationRandom.y,
		linear.baseAcceleration.z + accelerationRandom.z
	};

	const ParticleSwayDesc& sway = behavior.motion.sway;

	particle.swayTime = 0.0f;
	particle.swayPhase = RandomRange(0.0f, 6.2831853f);
	particle.swayAxis = RandomVector3Range(
		{ -1.0f, -1.0f, -1.0f },
		{ 1.0f, 1.0f, 1.0f }
	);
	particle.swayAmplitude = sway.amplitude;
	particle.swayFrequency = sway.frequency;

	if (particle.movementMode == MovementMode::kVortexInward) {
		const ParticleVortexDesc& vortex = behavior.motion.vortex;

		if (vortex.useEmitterOffset) {
			particle.vortexCenter = {
				emitterPosition.x + vortex.center.x,
				emitterPosition.y + vortex.center.y,
				emitterPosition.z + vortex.center.z
			};
		} else {
			particle.vortexCenter = vortex.center;
		}
		particle.vortexAxis = vortex.axis;

		Vector3 offset = {
			particle.transform.translate.x - particle.vortexCenter.x,
			particle.transform.translate.y - particle.vortexCenter.y,
			particle.transform.translate.z - particle.vortexCenter.z
		};

		switch (particle.vortexAxis) {
		case VortexAxis::kX:
			// X軸まわり。YZ平面で回転し、Xが軸方向
			particle.vortexRadius = std::sqrt(offset.y * offset.y + offset.z * offset.z);
			particle.vortexAngle = std::atan2(offset.z, offset.y);
			particle.vortexHeightOffset = offset.x;
			break;

		case VortexAxis::kY:
			// Y軸まわり。XZ平面で回転し、Yが軸方向
			particle.vortexRadius = std::sqrt(offset.x * offset.x + offset.z * offset.z);
			particle.vortexAngle = std::atan2(offset.z, offset.x);
			particle.vortexHeightOffset = offset.y;
			break;

		case VortexAxis::kZ:
			// Z軸まわり。XY平面で回転し、Zが軸方向
			particle.vortexRadius = std::sqrt(offset.x * offset.x + offset.y * offset.y);
			particle.vortexAngle = std::atan2(offset.y, offset.x);
			particle.vortexHeightOffset = offset.z;
			break;
		}

		particle.vortexAngularSpeed = RandomRange(
			vortex.angularSpeedMin,
			vortex.angularSpeedMax
		);

		particle.vortexInwardSpeed = RandomRange(
			vortex.inwardSpeedMin,
			vortex.inwardSpeedMax
		);

		particle.vortexVerticalSpeed = RandomRange(
			vortex.verticalSpeedMin,
			vortex.verticalSpeedMax
		);
	}
}

void ParticleManager::InitializeParticleColor(Particle& particle, const ParticleBehavior& behavior) {
	const ParticleColorDesc& color = behavior.color;

	particle.colorChangeMode = color.mode;

	particle.startColor = RandomVector4Range(color.startColorMin, color.startColorMax);
	particle.endColor = RandomVector4Range(color.endColorMin, color.endColorMax);

	particle.randomColorMin = color.randomColorMin;
	particle.randomColorMax = color.randomColorMax;

	particle.randomColorChangeIntervalMin = color.randomColorChangeIntervalMin;
	particle.randomColorChangeIntervalMax = color.randomColorChangeIntervalMax;
	particle.randomColorLerpSpeed = color.randomColorLerpSpeed;

	particle.randomCurrentColor = particle.startColor;
	particle.randomTargetColor = RandomVector4Range(color.randomColorMin, color.randomColorMax);
	particle.randomColorChangeTimer = 0.0f;
	particle.randomColorChangeInterval = RandomRange(
		color.randomColorChangeIntervalMin,
		color.randomColorChangeIntervalMax
	);

	particle.color = particle.startColor;
}

void ParticleManager::Emit(
	const std::string& name,
	const Vector3& position,
	const Vector3& spawnSize,
	uint32_t count,
	const ParticleBehavior& behavior
) {
	auto it = particleGroups_.find(name);
	assert(it != particleGroups_.end());

	ParticleGroup& group = it->second;

	for (uint32_t i = 0; i < count; ++i) {
		if (group.particles.size() >= kMaxInstanceCount) {
			break;
		}

		Particle particle{};

		Vector3 halfSpawnSize = {
			spawnSize.x * 0.5f,
			spawnSize.y * 0.5f,
			spawnSize.z * 0.5f
		};

		Vector3 spawnOffset = RandomVector3Range(
			{ -halfSpawnSize.x, -halfSpawnSize.y, -halfSpawnSize.z },
			{ halfSpawnSize.x, halfSpawnSize.y, halfSpawnSize.z }
		);

		particle.transform.translate = {
			position.x + spawnOffset.x,
			position.y + spawnOffset.y,
			position.z + spawnOffset.z
		};

		particle.transform.rotate = RandomVector3Range(
			behavior.rotation.initialRotationMin,
			behavior.rotation.initialRotationMax
		);
		particle.enableRotationOverTime =
			behavior.rotation.enableRotationOverTime;
		particle.rotationSpeed = behavior.rotation.rotationSpeed;
		particle.startScale = RandomVector3Range(
			behavior.scale.startScaleMin,
			behavior.scale.startScaleMax
		);

		particle.endScale = RandomVector3Range(
			behavior.scale.endScaleMin,
			behavior.scale.endScaleMax
		);

		particle.enableScaleOverLife = behavior.scale.enableScaleOverLife;
		particle.transform.scale = particle.startScale;

		InitializeParticleLife(particle, behavior);
		InitializeParticleMotion(particle, behavior, position);
		InitializeParticleColor(particle, behavior);

		particle.billboardMode = behavior.render.billboardMode;

		group.particles.push_back(particle);
	}
}

void ParticleManager::UpdateParticleMotion(Particle& particle) {
	if (particle.movementMode == MovementMode::kLinear) {
		particle.velocity.x += particle.acceleration.x * deltaTime_;
		particle.velocity.y += particle.acceleration.y * deltaTime_;
		particle.velocity.z += particle.acceleration.z * deltaTime_;

		particle.transform.translate.x += particle.velocity.x * deltaTime_;
		particle.transform.translate.y += particle.velocity.y * deltaTime_;
		particle.transform.translate.z += particle.velocity.z * deltaTime_;
	}
	else if (particle.movementMode == MovementMode::kVortexInward) {
		particle.vortexAngle += particle.vortexAngularSpeed * deltaTime_;
		particle.vortexRadius -= particle.vortexInwardSpeed * deltaTime_;
		particle.vortexHeightOffset += particle.vortexVerticalSpeed * deltaTime_;

		if (particle.vortexRadius < 0.0f) {
			particle.vortexRadius = 0.0f;
		}

		const float cosAngle = std::cos(particle.vortexAngle);
		const float sinAngle = std::sin(particle.vortexAngle);

		switch (particle.vortexAxis) {
		case VortexAxis::kX:
			// X軸まわり。YZ平面で回る
			particle.transform.translate.x =
				particle.vortexCenter.x + particle.vortexHeightOffset;

			particle.transform.translate.y =
				particle.vortexCenter.y + cosAngle * particle.vortexRadius;

			particle.transform.translate.z =
				particle.vortexCenter.z + sinAngle * particle.vortexRadius;
			break;

		case VortexAxis::kY:
			// Y軸まわり。XZ平面で回る
			particle.transform.translate.x =
				particle.vortexCenter.x + cosAngle * particle.vortexRadius;

			particle.transform.translate.y =
				particle.vortexCenter.y + particle.vortexHeightOffset;

			particle.transform.translate.z =
				particle.vortexCenter.z + sinAngle * particle.vortexRadius;
			break;

		case VortexAxis::kZ:
			// Z軸まわり。XY平面で回る
			particle.transform.translate.x =
				particle.vortexCenter.x + cosAngle * particle.vortexRadius;

			particle.transform.translate.y =
				particle.vortexCenter.y + sinAngle * particle.vortexRadius;

			particle.transform.translate.z =
				particle.vortexCenter.z + particle.vortexHeightOffset;
			break;
		}
	}

	if (particle.swayAmplitude != 0.0f) {
		particle.swayTime += deltaTime_;

		float swayValue =
			std::sin(particle.swayTime * particle.swayFrequency + particle.swayPhase) *
			particle.swayAmplitude;

		particle.transform.translate.x += particle.swayAxis.x * swayValue * deltaTime_;
		particle.transform.translate.y += particle.swayAxis.y * swayValue * deltaTime_;
		particle.transform.translate.z += particle.swayAxis.z * swayValue * deltaTime_;
	}
}

void ParticleManager::UpdateParticleColor(Particle& particle) {
	const float lifeRatio = GetAnimationRatio(particle);

	Vector4 baseColor = particle.startColor;

	switch (particle.colorChangeMode) {
	case ColorChangeMode::kConstant:
		baseColor = particle.startColor;
		break;

	case ColorChangeMode::kOverLife:
		baseColor = LerpColor(particle.startColor, particle.endColor, lifeRatio);
		break;

	case ColorChangeMode::kRandomLoop:
		particle.randomColorChangeTimer += deltaTime_;

		if (particle.randomColorChangeTimer >= particle.randomColorChangeInterval) {
			particle.randomColorChangeTimer = 0.0f;

			particle.randomTargetColor = RandomVector4Range(
				particle.randomColorMin,
				particle.randomColorMax
			);

			particle.randomColorChangeInterval = RandomRange(
				particle.randomColorChangeIntervalMin,
				particle.randomColorChangeIntervalMax
			);
		}

		particle.randomCurrentColor = LerpColor(
			particle.randomCurrentColor,
			particle.randomTargetColor,
			std::clamp(particle.randomColorLerpSpeed * deltaTime_, 0.0f, 1.0f)
		);

		baseColor = particle.randomCurrentColor;
		break;
	}

	if (particle.enableLifeFade) {
		float fadeRatio =
			(lifeRatio - particle.fadeOutStartRatio) /
			(1.0f - particle.fadeOutStartRatio);

		fadeRatio = std::clamp(fadeRatio, 0.0f, 1.0f);

		baseColor.w *= (1.0f - fadeRatio);
	}

	particle.color = baseColor;
}

bool ParticleManager::IsDeadParticle(const Particle& particle) const {
	if (particle.isLooping) {
		return false;
	}
	return particle.currentTime >= particle.lifeTime;
}

float ParticleManager::GetAnimationRatio(const Particle& particle) const {
	if (!particle.isLooping) {
		return std::clamp(particle.currentTime / particle.lifeTime, 0.0f, 1.0f);
	}

	float ratio = std::fmod(particle.currentTime, particle.loopDuration) /
		particle.loopDuration;
	if (particle.loopPingPong) {
		ratio = ratio < 0.5f ? ratio * 2.0f : (1.0f - ratio) * 2.0f;
	}
	return ratio;
}

Vector3 ParticleManager::LerpVector3(const Vector3& start, const Vector3& end, float t) {
	t = std::clamp(t, 0.0f, 1.0f);

	return {
		start.x + (end.x - start.x) * t,
		start.y + (end.y - start.y) * t,
		start.z + (end.z - start.z) * t
	};
}

void ParticleManager::UpdateParticleScale(Particle& particle) {
	if (!particle.enableScaleOverLife) {
		return;
	}

	const float lifeRatio = GetAnimationRatio(particle);

	particle.transform.scale = LerpVector3(
		particle.startScale,
		particle.endScale,
		lifeRatio
	);
}

void ParticleManager::UpdateParticleRotation(Particle& particle) {
	if (!particle.enableRotationOverTime) {
		return;
	}

	constexpr float kTwoPi = 6.2831853f;
	particle.transform.rotate.x = std::fmod(
		particle.transform.rotate.x + particle.rotationSpeed.x * deltaTime_,
		kTwoPi
	);
	particle.transform.rotate.y = std::fmod(
		particle.transform.rotate.y + particle.rotationSpeed.y * deltaTime_,
		kTwoPi
	);
	particle.transform.rotate.z = std::fmod(
		particle.transform.rotate.z + particle.rotationSpeed.z * deltaTime_,
		kTwoPi
	);
}

void ParticleManager::Update() {
	if (!camera_) {
		return;
	}

	if (gpuParticleEnabled_) {
		gpuParticle_.Update();
	}

	for (auto& [name, group] : particleGroups_) {
		group.uvOffset.x += group.render.uvScrollSpeed.x * deltaTime_;
		group.uvOffset.y += group.render.uvScrollSpeed.y * deltaTime_;
		group.materialData->uvTransform = MakeIdentity4x4();
		group.materialData->uvTransform.m[3][0] = group.uvOffset.x;
		group.materialData->uvTransform.m[3][1] = group.uvOffset.y;

		for (auto& particle : group.particles) {
			particle.currentTime += deltaTime_;

			UpdateParticleMotion(particle);
			UpdateParticleRotation(particle);
			UpdateParticleScale(particle);
			UpdateParticleColor(particle);
		}

		group.particles.erase(
			std::remove_if(
				group.particles.begin(),
				group.particles.end(),
				[this](const Particle& particle) {
					return IsDeadParticle(particle);
				}
			),
			group.particles.end()
		);

		group.instanceCount = 0;

		for (const auto& particle : group.particles) {
			if (group.instanceCount >= kMaxInstanceCount) {
				break;
			}

			Matrix4x4 worldMatrix{};

			if (particle.billboardMode == BillboardMode::kBillboard) {
				worldMatrix = MakeBillboardMatrix(
					camera_->GetWorldMatrix(),
					particle.transform.scale,
					particle.transform.translate,
					particle.transform.rotate.z
				);
			}
			else {
				worldMatrix = MakeAffineMatrix(
					particle.transform.scale,
					particle.transform.rotate,
					particle.transform.translate
				);
			}
			Matrix4x4 wvp = Multiply(worldMatrix, camera_->GetViewProjectionMatrix());

			group.instancingData[group.instanceCount].World = worldMatrix;
			group.instancingData[group.instanceCount].WVP = wvp;
			group.instancingData[group.instanceCount].color = particle.color;

			++group.instanceCount;
		}
	}
}

void ParticleManager::Draw() {
	auto* commandList = particleCommon_->GetDxCommon()->GetCommandList();

	for (auto& [name, group] : particleGroups_) {
		if (group.instanceCount == 0) {
			continue;
		}

		// ParticleGroupごとにBlendModeを切り替える
		particleCommon_->SetRenderState(
			group.blendMode,
			group.render.cullMode,
			group.render.depthTest,
			group.render.depthWrite
		);
		particleCommon_->SetCommonRenderState();
		commandList->IASetVertexBuffers(0, 1, &group.vertexBufferView);

		// b0 Material
		commandList->SetGraphicsRootConstantBufferView(
			0,
			group.materialResource->GetGPUVirtualAddress()
		);

		// VS t0 StructuredBuffer<TransformationMatrix>
		srvManager_->SetGraphicsRootDescriptorTable(1, group.instanceSrvIndex);

		// PS t0 Texture2D
		srvManager_->SetGraphicsRootDescriptorTable(2, group.textureSrvIndex);

		// b1 DirectionalLight
		commandList->SetGraphicsRootConstantBufferView(
			3,
			directionalLightResource_->GetGPUVirtualAddress()
		);

		commandList->DrawInstanced(group.vertexCount, group.instanceCount, 0, 0);
	}

	if (gpuParticleEnabled_) {
		gpuParticle_.Draw(camera_);
	}
}
