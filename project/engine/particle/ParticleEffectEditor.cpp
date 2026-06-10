#include "ParticleEffectEditor.h"

#include <algorithm>
#include <cstring>
#include <filesystem>

#include "../externals/imgui/imgui.h"
#include "ParticleEmitter.h"

namespace {

	void CopyText(char* destination, size_t destinationSize, const std::string& source) {
		if (destination == nullptr || destinationSize == 0) {
			return;
		}

		strncpy_s(destination, destinationSize, source.c_str(), _TRUNCATE);
	}

void DragVector3(const char* label, Vector3& value, float speed = 0.01f) {
	ImGui::DragFloat3(label, &value.x, speed);
}

void DragVector4(const char* label, Vector4& value, float speed = 0.01f) {
	ImGui::DragFloat4(label, &value.x, speed, 0.0f, 1.0f);
}

void ComboBlendMode(const char* label, ParticleCommon::BlendMode& mode) {
	const char* items[] = { "None", "Normal", "Add", "Subtract", "Multiply", "Screen" };
	int current = static_cast<int>(mode);

	if (ImGui::Combo(label, &current, items, IM_ARRAYSIZE(items))) {
		mode = static_cast<ParticleCommon::BlendMode>(current);
	}
}

void ComboColorMode(const char* label, ParticleManager::ColorChangeMode& mode) {
	const char* items[] = { "Constant", "OverLife", "RandomLoop" };
	int current = static_cast<int>(mode);

	if (ImGui::Combo(label, &current, items, IM_ARRAYSIZE(items))) {
		mode = static_cast<ParticleManager::ColorChangeMode>(current);
	}
}

void ComboMovementMode(const char* label, ParticleManager::MovementMode& mode) {
	const char* items[] = { "Linear", "VortexInward" };
	int current = static_cast<int>(mode);

	if (ImGui::Combo(label, &current, items, IM_ARRAYSIZE(items))) {
		mode = static_cast<ParticleManager::MovementMode>(current);
	}
}

void ComboVortexAxis(const char* label, ParticleManager::VortexAxis& axis) {
	const char* items[] = { "X", "Y", "Z" };
	int current = static_cast<int>(axis);

	if (ImGui::Combo(label, &current, items, IM_ARRAYSIZE(items))) {
		axis = static_cast<ParticleManager::VortexAxis>(current);
	}
}

void ComboBillboardMode(const char* label, ParticleManager::BillboardMode& mode) {
	const char* items[] = { "None", "Billboard" };
	int current = static_cast<int>(mode);

	if (ImGui::Combo(label, &current, items, IM_ARRAYSIZE(items))) {
		mode = static_cast<ParticleManager::BillboardMode>(current);
	}
}

void ComboPrimitiveType(const char* label, ParticleManager::PrimitiveType& type) {
	const char* items[] = { "Plane", "Ring", "Cylinder" };
	int current = static_cast<int>(type);
	if (ImGui::Combo(label, &current, items, IM_ARRAYSIZE(items))) {
		type = static_cast<ParticleManager::PrimitiveType>(current);
	}
}

void ComboCullMode(const char* label, ParticleCommon::CullMode& mode) {
	const char* items[] = { "None", "Back", "Front" };
	int current = static_cast<int>(mode);
	if (ImGui::Combo(label, &current, items, IM_ARRAYSIZE(items))) {
		mode = static_cast<ParticleCommon::CullMode>(current);
	}
}

void ComboRingUvMode(const char* label, ParticleManager::RingUvMode& mode) {
	const char* items[] = { "Horizontal", "Vertical" };
	int current = static_cast<int>(mode);
	if (ImGui::Combo(label, &current, items, IM_ARRAYSIZE(items))) {
		mode = static_cast<ParticleManager::RingUvMode>(current);
	}
}

} // namespace

void ParticleEffectEditor::Initialize(const ParticleEffectDesc& effect, const std::string& filePath) {
	CopyText(filePath_, sizeof(filePath_), filePath);
	CopyStringsFromEffect(effect);
	RefreshEffectFiles();
	initialized_ = true;
}

void ParticleEffectEditor::CopyStringsFromEffect(const ParticleEffectDesc& effect) {
	CopyText(name_, sizeof(name_), effect.name);
	CopyText(textureFilePath_, sizeof(textureFilePath_), effect.textureFilePath);
}

void ParticleEffectEditor::CopyStringsToEffect(ParticleEffectDesc& effect) {
	effect.name = name_;
	effect.textureFilePath = textureFilePath_;
}

void ParticleEffectEditor::RefreshEffectFiles() {
	effectFilePaths_.clear();
	effectFileNames_.clear();
	selectedEffectIndex_ = -1;

	std::filesystem::path currentPath(filePath_);
	std::filesystem::path directory = currentPath.parent_path();
	if (directory.empty()) {
		directory = "resources/particles";
	}

	std::error_code error;
	for (const std::filesystem::directory_entry& entry :
		std::filesystem::directory_iterator(directory, error)) {
		if (error) {
			break;
		}
		if (!entry.is_regular_file() || entry.path().extension() != ".json") {
			continue;
		}

		effectFilePaths_.push_back(entry.path().generic_string());
	}

	std::sort(effectFilePaths_.begin(), effectFilePaths_.end());
	for (size_t index = 0; index < effectFilePaths_.size(); ++index) {
		const std::filesystem::path path(effectFilePaths_[index]);
		effectFileNames_.push_back(path.filename().string());

		if (path.lexically_normal() == currentPath.lexically_normal()) {
			selectedEffectIndex_ = static_cast<int>(index);
		}
	}
}

bool ParticleEffectEditor::LoadEffectFile(
	const std::string& filePath,
	ParticleEffectDesc& effect,
	ParticleEmitter*& previewEmitter
) {
	if (!ParticleEffectResource::Load(filePath, effect)) {
		return false;
	}

	CopyText(filePath_, sizeof(filePath_), filePath);
	CopyStringsFromEffect(effect);

	delete previewEmitter;
	previewEmitter = ParticleEffectResource::CreateEmitter(effect);
	return true;
}

bool ParticleEffectEditor::DrawImGui(
	ParticleEffectDesc& effect,
	ParticleEmitter*& previewEmitter,
	const char* windowTitle
) {
	if (!initialized_) {
		Initialize(effect, "resources/particles/newParticle.json");
	}

	bool applied = false;
	bool changed = false;

	ImGui::Begin(windowTitle);

	const char* selectedName =
		selectedEffectIndex_ >= 0 &&
		selectedEffectIndex_ < static_cast<int>(effectFileNames_.size())
		? effectFileNames_[selectedEffectIndex_].c_str()
		: "(select effect)";

	if (ImGui::BeginCombo("Existing Effect", selectedName)) {
		for (int index = 0; index < static_cast<int>(effectFileNames_.size()); ++index) {
			const bool isSelected = selectedEffectIndex_ == index;
			if (ImGui::Selectable(effectFileNames_[index].c_str(), isSelected)) {
				if (LoadEffectFile(effectFilePaths_[index], effect, previewEmitter)) {
					selectedEffectIndex_ = index;
					applied = true;
				}
			}
			if (isSelected) {
				ImGui::SetItemDefaultFocus();
			}
		}
		ImGui::EndCombo();
	}
	ImGui::SameLine();
	if (ImGui::Button("Refresh List")) {
		RefreshEffectFiles();
	}

	ImGui::InputText("FilePath", filePath_, sizeof(filePath_));
	bool resourceChanged = false;

	resourceChanged |= ImGui::InputText("Name", name_, sizeof(name_));
	resourceChanged |= ImGui::InputText("Texture", textureFilePath_, sizeof(textureFilePath_));

	CopyStringsToEffect(effect);

	ComboBlendMode("BlendMode", effect.blendMode);

	if (ImGui::CollapsingHeader("Emitter", ImGuiTreeNodeFlags_DefaultOpen)) {
		changed |= ImGui::DragFloat3("Translate", &effect.emitter.translate.x, 0.05f);
		changed |= ImGui::DragFloat3("SpawnSize", &effect.emitter.spawnSize.x, 0.05f);

		int count = static_cast<int>(effect.emitter.count);
		if (ImGui::DragInt("Count", &count, 1, 0, 1000)) {
			effect.emitter.count = static_cast<uint32_t>(count < 0 ? 0 : count);
			changed = true;
		}

		if (!effect.behavior.life.isLooping) {
			changed |= ImGui::DragFloat(
				"Frequency",
				&effect.emitter.frequency,
				0.001f,
				0.001f,
				10.0f
			);
		}
		changed |= ImGui::Checkbox("Active", &effect.emitter.isActive);
	}

	if (ImGui::CollapsingHeader("Life", ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::Checkbox("Loop", &effect.behavior.life.isLooping);
		if (effect.behavior.life.isLooping) {
			ImGui::DragFloat(
				"LoopDuration",
				&effect.behavior.life.loopDuration,
				0.01f,
				0.001f,
				100.0f
			);
			ImGui::Checkbox(
				"LoopPingPong",
				&effect.behavior.life.loopPingPong
			);
		}
		else {
			ImGui::DragFloat("LifeTimeMin", &effect.behavior.life.lifeTimeMin, 0.01f, 0.0f, 100.0f);
			ImGui::DragFloat("LifeTimeMax", &effect.behavior.life.lifeTimeMax, 0.01f, 0.0f, 100.0f);
			ImGui::Checkbox("EnableLifeFade", &effect.behavior.life.enableLifeFade);
			ImGui::DragFloat("FadeOutStartRatio", &effect.behavior.life.fadeOutStartRatio, 0.01f, 0.0f, 0.99f);
		}
	}

	if (ImGui::CollapsingHeader("Scale", ImGuiTreeNodeFlags_DefaultOpen)) {
		DragVector3("StartScaleMin", effect.behavior.scale.startScaleMin, 0.01f);
		DragVector3("StartScaleMax", effect.behavior.scale.startScaleMax, 0.01f);

		ImGui::Checkbox("EnableScaleOverLife", &effect.behavior.scale.enableScaleOverLife);

		DragVector3("EndScaleMin", effect.behavior.scale.endScaleMin, 0.01f);
		DragVector3("EndScaleMax", effect.behavior.scale.endScaleMax, 0.01f);
	}

	if (ImGui::CollapsingHeader("Rotation", ImGuiTreeNodeFlags_DefaultOpen)) {
		DragVector3("InitialRotationMin", effect.behavior.rotation.initialRotationMin, 0.01f);
		DragVector3("InitialRotationMax", effect.behavior.rotation.initialRotationMax, 0.01f);
		ImGui::Checkbox(
			"RotationOverTime",
			&effect.behavior.rotation.enableRotationOverTime
		);
		if (effect.behavior.rotation.enableRotationOverTime) {
			DragVector3(
				"RotationSpeed",
				effect.behavior.rotation.rotationSpeed,
				0.01f
			);
		}
	}

	if (ImGui::CollapsingHeader("Motion", ImGuiTreeNodeFlags_DefaultOpen)) {
		ComboMovementMode("MovementMode", effect.behavior.motion.mode);

		if (ImGui::TreeNode("Linear")) {
			DragVector3("BaseVelocity", effect.behavior.motion.linear.baseVelocity, 0.05f);
			DragVector3("VelocityRandomRange", effect.behavior.motion.linear.velocityRandomRange, 0.05f);
			DragVector3("BaseAcceleration", effect.behavior.motion.linear.baseAcceleration, 0.01f);
			DragVector3("AccelerationRandomRange", effect.behavior.motion.linear.accelerationRandomRange, 0.01f);
			ImGui::TreePop();
		}

		if (ImGui::TreeNode("Sway")) {
			ImGui::DragFloat("Amplitude", &effect.behavior.motion.sway.amplitude, 0.01f, 0.0f, 100.0f);
			ImGui::DragFloat("Frequency", &effect.behavior.motion.sway.frequency, 0.01f, 0.0f, 100.0f);
			ImGui::TreePop();
		}

		if (ImGui::TreeNode("Vortex")) {
			DragVector3("Center", effect.behavior.motion.vortex.center, 0.05f);
			ComboVortexAxis("Axis", effect.behavior.motion.vortex.axis);

			ImGui::DragFloat("AngularSpeedMin", &effect.behavior.motion.vortex.angularSpeedMin, 0.01f, -100.0f, 100.0f);
			ImGui::DragFloat("AngularSpeedMax", &effect.behavior.motion.vortex.angularSpeedMax, 0.01f, -100.0f, 100.0f);

			ImGui::DragFloat("InwardSpeedMin", &effect.behavior.motion.vortex.inwardSpeedMin, 0.01f, -100.0f, 100.0f);
			ImGui::DragFloat("InwardSpeedMax", &effect.behavior.motion.vortex.inwardSpeedMax, 0.01f, -100.0f, 100.0f);

			ImGui::DragFloat("VerticalSpeedMin", &effect.behavior.motion.vortex.verticalSpeedMin, 0.01f, -100.0f, 100.0f);
			ImGui::DragFloat("VerticalSpeedMax", &effect.behavior.motion.vortex.verticalSpeedMax, 0.01f, -100.0f, 100.0f);
			ImGui::TreePop();
		}
	}

	if (ImGui::CollapsingHeader("Color", ImGuiTreeNodeFlags_DefaultOpen)) {
		ComboColorMode("ColorMode", effect.behavior.color.mode);

		DragVector4("StartColorMin", effect.behavior.color.startColorMin, 0.01f);
		DragVector4("StartColorMax", effect.behavior.color.startColorMax, 0.01f);
		DragVector4("EndColorMin", effect.behavior.color.endColorMin, 0.01f);
		DragVector4("EndColorMax", effect.behavior.color.endColorMax, 0.01f);

		DragVector4("RandomColorMin", effect.behavior.color.randomColorMin, 0.01f);
		DragVector4("RandomColorMax", effect.behavior.color.randomColorMax, 0.01f);

		ImGui::DragFloat("RandomColorIntervalMin", &effect.behavior.color.randomColorChangeIntervalMin, 0.01f, 0.001f, 10.0f);
		ImGui::DragFloat("RandomColorIntervalMax", &effect.behavior.color.randomColorChangeIntervalMax, 0.01f, 0.001f, 10.0f);
		ImGui::DragFloat("RandomColorLerpSpeed", &effect.behavior.color.randomColorLerpSpeed, 0.01f, 0.0f, 100.0f);
	}

	if (ImGui::CollapsingHeader("Render", ImGuiTreeNodeFlags_DefaultOpen)) {
		ComboBillboardMode("BillboardMode", effect.behavior.render.billboardMode);
		ComboPrimitiveType("PrimitiveType", effect.behavior.render.primitiveType);
		ImGui::DragFloat2(
			"UVScrollSpeed",
			&effect.behavior.render.uvScrollSpeed.x,
			0.01f
		);
		ImGui::Checkbox("FlipU", &effect.behavior.render.flipU);
		ImGui::SameLine();
		ImGui::Checkbox("FlipV", &effect.behavior.render.flipV);
		ImGui::SliderFloat(
			"AlphaCutoff",
			&effect.behavior.render.alphaCutoff,
			0.0f,
			1.0f
		);
		ComboCullMode("CullMode", effect.behavior.render.cullMode);
		ImGui::Checkbox("DepthTest", &effect.behavior.render.depthTest);
		ImGui::SameLine();
		ImGui::Checkbox("DepthWrite", &effect.behavior.render.depthWrite);

		if (effect.behavior.render.primitiveType == ParticleManager::PrimitiveType::kRing) {
			int divisions = static_cast<int>(effect.behavior.render.ring.divisions);
			if (ImGui::DragInt("RingDivisions", &divisions, 1, 3, 256)) {
				effect.behavior.render.ring.divisions =
					static_cast<uint32_t>(std::clamp(divisions, 3, 256));
			}
			ImGui::DragFloat(
				"OuterRadius",
				&effect.behavior.render.ring.outerRadius,
				0.01f,
				0.001f,
				100.0f
			);
			ImGui::DragFloat(
				"InnerRadius",
				&effect.behavior.render.ring.innerRadius,
				0.01f,
				0.0f,
				100.0f
			);
			ImGui::DragFloat(
				"StartAngle",
				&effect.behavior.render.ring.startAngle,
				0.01f
			);
			ImGui::DragFloat(
				"EndAngle",
				&effect.behavior.render.ring.endAngle,
				0.01f
			);
			ImGui::ColorEdit4(
				"OuterColor",
				&effect.behavior.render.ring.outerColor.x
			);
			ImGui::ColorEdit4(
				"InnerColor",
				&effect.behavior.render.ring.innerColor.x
			);
			ComboRingUvMode("RingUV", effect.behavior.render.ring.uvMode);
		}
		else if (
			effect.behavior.render.primitiveType ==
			ParticleManager::PrimitiveType::kCylinder
		) {
			int divisions = static_cast<int>(effect.behavior.render.cylinder.divisions);
			if (ImGui::DragInt("CylinderDivisions", &divisions, 1, 3, 256)) {
				effect.behavior.render.cylinder.divisions =
					static_cast<uint32_t>(std::clamp(divisions, 3, 256));
			}
			ImGui::DragFloat(
				"TopRadius",
				&effect.behavior.render.cylinder.topRadius,
				0.01f,
				0.0f,
				100.0f
			);
			ImGui::DragFloat(
				"BottomRadius",
				&effect.behavior.render.cylinder.bottomRadius,
				0.01f,
				0.0f,
				100.0f
			);
			ImGui::DragFloat(
				"Height",
				&effect.behavior.render.cylinder.height,
				0.01f,
				0.001f,
				100.0f
			);
			ImGui::DragFloat(
				"CylinderStartAngle",
				&effect.behavior.render.cylinder.startAngle,
				0.01f
			);
			ImGui::DragFloat(
				"CylinderEndAngle",
				&effect.behavior.render.cylinder.endAngle,
				0.01f
			);
			ImGui::ColorEdit4(
				"TopColor",
				&effect.behavior.render.cylinder.topColor.x
			);
			ImGui::ColorEdit4(
				"BottomColor",
				&effect.behavior.render.cylinder.bottomColor.x
			);
			ComboRingUvMode(
				"CylinderUV",
				effect.behavior.render.cylinder.uvMode
			);
		}
	}

	// 数値編集をリアルタイム反映
	if (previewEmitter) {
		ParticleEffectResource::PrepareParticleGroup(effect, false);
		ParticleEffectResource::ApplyToEmitter(*previewEmitter, effect);
	}

	if (ImGui::Button("Apply")) {
		delete previewEmitter;
		previewEmitter = ParticleEffectResource::CreateEmitter(effect);
		applied = true;
	}

	ImGui::SameLine();

	if (ImGui::Button("Save")) {
		ParticleEffectResource::Save(filePath_, effect);
	}

	ImGui::SameLine();

	if (ImGui::Button("Load")) {
		if (LoadEffectFile(filePath_, effect, previewEmitter)) {
			RefreshEffectFiles();
			applied = true;
		}
	}

	ImGui::End();

	return applied;
}
