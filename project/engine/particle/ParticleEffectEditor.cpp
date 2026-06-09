#include "ParticleEffectEditor.h"

#include <cstring>

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

} // namespace

void ParticleEffectEditor::Initialize(const ParticleEffectDesc& effect, const std::string& filePath) {
	CopyText(filePath_, sizeof(filePath_), filePath);
	CopyStringsFromEffect(effect);
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

bool ParticleEffectEditor::DrawImGui(ParticleEffectDesc& effect, ParticleEmitter*& previewEmitter) {
	if (!initialized_) {
		Initialize(effect, "resources/particles/newParticle.json");
	}

	bool applied = false;
	bool changed = false;

	ImGui::Begin("Particle Effect Editor");

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

		changed |= ImGui::DragFloat("Frequency", &effect.emitter.frequency, 0.001f, 0.001f, 10.0f);
		changed |= ImGui::Checkbox("Active", &effect.emitter.isActive);
	}

	if (ImGui::CollapsingHeader("Life", ImGuiTreeNodeFlags_DefaultOpen)) {
		ImGui::DragFloat("LifeTimeMin", &effect.behavior.life.lifeTimeMin, 0.01f, 0.0f, 100.0f);
		ImGui::DragFloat("LifeTimeMax", &effect.behavior.life.lifeTimeMax, 0.01f, 0.0f, 100.0f);
		ImGui::Checkbox("EnableLifeFade", &effect.behavior.life.enableLifeFade);
		ImGui::DragFloat("FadeOutStartRatio", &effect.behavior.life.fadeOutStartRatio, 0.01f, 0.0f, 0.99f);
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
		if (ParticleEffectResource::Load(filePath_, effect)) {
			CopyStringsFromEffect(effect);

			delete previewEmitter;
			previewEmitter = ParticleEffectResource::CreateEmitter(effect);
			applied = true;
		}
	}

	ImGui::End();

	return applied;
}
