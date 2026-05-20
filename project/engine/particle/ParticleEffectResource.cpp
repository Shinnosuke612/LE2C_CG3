#include "ParticleEffectResource.h"

#include <fstream>
#include <iomanip>

#include "../externals/nlohmann/json.hpp"

using json = nlohmann::json;

namespace {

json ToJson(const Vector3& v) {
	return json::array({ v.x, v.y, v.z });
}

json ToJson(const Vector4& v) {
	return json::array({ v.x, v.y, v.z, v.w });
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

Vector4 ReadVector4(const json& j, const Vector4& defaultValue) {
	if (!j.is_array() || j.size() < 4) {
		return defaultValue;
	}

	return {
		j.at(0).get<float>(),
		j.at(1).get<float>(),
		j.at(2).get<float>(),
		j.at(3).get<float>()
	};
}

std::string ToString(ParticleCommon::BlendMode mode) {
	switch (mode) {
	case ParticleCommon::BlendMode::kBlendModeNone: return "None";
	case ParticleCommon::BlendMode::kBlendModeNormal: return "Normal";
	case ParticleCommon::BlendMode::kBlendModeAdd: return "Add";
	case ParticleCommon::BlendMode::kBlendModeSubtract: return "Subtract";
	case ParticleCommon::BlendMode::kBlendModeMultiply: return "Multiply";
	case ParticleCommon::BlendMode::kBlendModeScreen: return "Screen";
	default: return "Add";
	}
}

ParticleCommon::BlendMode ToBlendMode(const std::string& text) {
	if (text == "None") return ParticleCommon::BlendMode::kBlendModeNone;
	if (text == "Normal") return ParticleCommon::BlendMode::kBlendModeNormal;
	if (text == "Add") return ParticleCommon::BlendMode::kBlendModeAdd;
	if (text == "Subtract") return ParticleCommon::BlendMode::kBlendModeSubtract;
	if (text == "Multiply") return ParticleCommon::BlendMode::kBlendModeMultiply;
	if (text == "Screen") return ParticleCommon::BlendMode::kBlendModeScreen;

	return ParticleCommon::BlendMode::kBlendModeAdd;
}

std::string ToString(ParticleManager::ColorChangeMode mode) {
	switch (mode) {
	case ParticleManager::ColorChangeMode::kConstant: return "Constant";
	case ParticleManager::ColorChangeMode::kOverLife: return "OverLife";
	case ParticleManager::ColorChangeMode::kRandomLoop: return "RandomLoop";
	default: return "Constant";
	}
}

ParticleManager::ColorChangeMode ToColorChangeMode(const std::string& text) {
	if (text == "Constant") return ParticleManager::ColorChangeMode::kConstant;
	if (text == "OverLife") return ParticleManager::ColorChangeMode::kOverLife;
	if (text == "RandomLoop") return ParticleManager::ColorChangeMode::kRandomLoop;

	return ParticleManager::ColorChangeMode::kConstant;
}

std::string ToString(ParticleManager::MovementMode mode) {
	switch (mode) {
	case ParticleManager::MovementMode::kLinear: return "Linear";
	case ParticleManager::MovementMode::kVortexInward: return "VortexInward";
	default: return "Linear";
	}
}

ParticleManager::MovementMode ToMovementMode(const std::string& text) {
	if (text == "Linear") return ParticleManager::MovementMode::kLinear;
	if (text == "VortexInward") return ParticleManager::MovementMode::kVortexInward;

	return ParticleManager::MovementMode::kLinear;
}

std::string ToString(ParticleManager::VortexAxis axis) {
	switch (axis) {
	case ParticleManager::VortexAxis::kX: return "X";
	case ParticleManager::VortexAxis::kY: return "Y";
	case ParticleManager::VortexAxis::kZ: return "Z";
	default: return "Y";
	}
}

ParticleManager::VortexAxis ToVortexAxis(const std::string& text) {
	if (text == "X") return ParticleManager::VortexAxis::kX;
	if (text == "Y") return ParticleManager::VortexAxis::kY;
	if (text == "Z") return ParticleManager::VortexAxis::kZ;

	return ParticleManager::VortexAxis::kY;
}

std::string ToString(ParticleManager::BillboardMode mode) {
	switch (mode) {
	case ParticleManager::BillboardMode::kNone: return "None";
	case ParticleManager::BillboardMode::kBillboard: return "Billboard";
	default: return "Billboard";
	}
}

ParticleManager::BillboardMode ToBillboardMode(const std::string& text) {
	if (text == "None") return ParticleManager::BillboardMode::kNone;
	if (text == "Billboard") return ParticleManager::BillboardMode::kBillboard;

	return ParticleManager::BillboardMode::kBillboard;
}

void WriteBehavior(json& j, const ParticleManager::ParticleBehavior& b) {
	j["life"] = {
		{ "lifeTimeMin", b.life.lifeTimeMin },
		{ "lifeTimeMax", b.life.lifeTimeMax },
		{ "enableLifeFade", b.life.enableLifeFade },
		{ "fadeOutStartRatio", b.life.fadeOutStartRatio }
	};

	j["scale"] = {
		{ "startScaleMin", ToJson(b.scale.startScaleMin) },
		{ "startScaleMax", ToJson(b.scale.startScaleMax) },
		{ "enableScaleOverLife", b.scale.enableScaleOverLife },
		{ "endScaleMin", ToJson(b.scale.endScaleMin) },
		{ "endScaleMax", ToJson(b.scale.endScaleMax) }
	};

	j["motion"]["mode"] = ToString(b.motion.mode);

	j["motion"]["linear"] = {
		{ "baseVelocity", ToJson(b.motion.linear.baseVelocity) },
		{ "velocityRandomRange", ToJson(b.motion.linear.velocityRandomRange) },
		{ "baseAcceleration", ToJson(b.motion.linear.baseAcceleration) },
		{ "accelerationRandomRange", ToJson(b.motion.linear.accelerationRandomRange) }
	};

	j["motion"]["sway"] = {
		{ "amplitude", b.motion.sway.amplitude },
		{ "frequency", b.motion.sway.frequency }
	};

	j["motion"]["vortex"] = {
		{ "center", ToJson(b.motion.vortex.center) },
		{ "axis", ToString(b.motion.vortex.axis) },
		{ "angularSpeedMin", b.motion.vortex.angularSpeedMin },
		{ "angularSpeedMax", b.motion.vortex.angularSpeedMax },
		{ "inwardSpeedMin", b.motion.vortex.inwardSpeedMin },
		{ "inwardSpeedMax", b.motion.vortex.inwardSpeedMax },
		{ "verticalSpeedMin", b.motion.vortex.verticalSpeedMin },
		{ "verticalSpeedMax", b.motion.vortex.verticalSpeedMax }
	};

	j["color"] = {
		{ "mode", ToString(b.color.mode) },
		{ "startColorMin", ToJson(b.color.startColorMin) },
		{ "startColorMax", ToJson(b.color.startColorMax) },
		{ "endColorMin", ToJson(b.color.endColorMin) },
		{ "endColorMax", ToJson(b.color.endColorMax) },
		{ "randomColorMin", ToJson(b.color.randomColorMin) },
		{ "randomColorMax", ToJson(b.color.randomColorMax) },
		{ "randomColorChangeIntervalMin", b.color.randomColorChangeIntervalMin },
		{ "randomColorChangeIntervalMax", b.color.randomColorChangeIntervalMax },
		{ "randomColorLerpSpeed", b.color.randomColorLerpSpeed }
	};

	j["render"] = {
		{ "billboardMode", ToString(b.render.billboardMode) }
	};
}

void ReadBehavior(const json& j, ParticleManager::ParticleBehavior& b) {
	if (j.contains("life")) {
		const json& life = j.at("life");
		b.life.lifeTimeMin = life.value("lifeTimeMin", b.life.lifeTimeMin);
		b.life.lifeTimeMax = life.value("lifeTimeMax", b.life.lifeTimeMax);
		b.life.enableLifeFade = life.value("enableLifeFade", b.life.enableLifeFade);
		b.life.fadeOutStartRatio = life.value("fadeOutStartRatio", b.life.fadeOutStartRatio);
	}

	if (j.contains("scale")) {
		const json& scale = j.at("scale");
		if (scale.contains("startScaleMin")) b.scale.startScaleMin = ReadVector3(scale.at("startScaleMin"), b.scale.startScaleMin);
		if (scale.contains("startScaleMax")) b.scale.startScaleMax = ReadVector3(scale.at("startScaleMax"), b.scale.startScaleMax);
		b.scale.enableScaleOverLife = scale.value("enableScaleOverLife", b.scale.enableScaleOverLife);
		if (scale.contains("endScaleMin")) b.scale.endScaleMin = ReadVector3(scale.at("endScaleMin"), b.scale.endScaleMin);
		if (scale.contains("endScaleMax")) b.scale.endScaleMax = ReadVector3(scale.at("endScaleMax"), b.scale.endScaleMax);
	}

	if (j.contains("motion")) {
		const json& motion = j.at("motion");

		b.motion.mode = ToMovementMode(motion.value("mode", ToString(b.motion.mode)));

		if (motion.contains("linear")) {
			const json& linear = motion.at("linear");
			if (linear.contains("baseVelocity")) b.motion.linear.baseVelocity = ReadVector3(linear.at("baseVelocity"), b.motion.linear.baseVelocity);
			if (linear.contains("velocityRandomRange")) b.motion.linear.velocityRandomRange = ReadVector3(linear.at("velocityRandomRange"), b.motion.linear.velocityRandomRange);
			if (linear.contains("baseAcceleration")) b.motion.linear.baseAcceleration = ReadVector3(linear.at("baseAcceleration"), b.motion.linear.baseAcceleration);
			if (linear.contains("accelerationRandomRange")) b.motion.linear.accelerationRandomRange = ReadVector3(linear.at("accelerationRandomRange"), b.motion.linear.accelerationRandomRange);
		}

		if (motion.contains("sway")) {
			const json& sway = motion.at("sway");
			b.motion.sway.amplitude = sway.value("amplitude", b.motion.sway.amplitude);
			b.motion.sway.frequency = sway.value("frequency", b.motion.sway.frequency);
		}

		if (motion.contains("vortex")) {
			const json& vortex = motion.at("vortex");
			if (vortex.contains("center")) b.motion.vortex.center = ReadVector3(vortex.at("center"), b.motion.vortex.center);
			b.motion.vortex.axis = ToVortexAxis(vortex.value("axis", ToString(b.motion.vortex.axis)));
			b.motion.vortex.angularSpeedMin = vortex.value("angularSpeedMin", b.motion.vortex.angularSpeedMin);
			b.motion.vortex.angularSpeedMax = vortex.value("angularSpeedMax", b.motion.vortex.angularSpeedMax);
			b.motion.vortex.inwardSpeedMin = vortex.value("inwardSpeedMin", b.motion.vortex.inwardSpeedMin);
			b.motion.vortex.inwardSpeedMax = vortex.value("inwardSpeedMax", b.motion.vortex.inwardSpeedMax);
			b.motion.vortex.verticalSpeedMin = vortex.value("verticalSpeedMin", b.motion.vortex.verticalSpeedMin);
			b.motion.vortex.verticalSpeedMax = vortex.value("verticalSpeedMax", b.motion.vortex.verticalSpeedMax);
		}
	}

	if (j.contains("color")) {
		const json& color = j.at("color");

		b.color.mode = ToColorChangeMode(color.value("mode", ToString(b.color.mode)));
		if (color.contains("startColorMin")) b.color.startColorMin = ReadVector4(color.at("startColorMin"), b.color.startColorMin);
		if (color.contains("startColorMax")) b.color.startColorMax = ReadVector4(color.at("startColorMax"), b.color.startColorMax);
		if (color.contains("endColorMin")) b.color.endColorMin = ReadVector4(color.at("endColorMin"), b.color.endColorMin);
		if (color.contains("endColorMax")) b.color.endColorMax = ReadVector4(color.at("endColorMax"), b.color.endColorMax);
		if (color.contains("randomColorMin")) b.color.randomColorMin = ReadVector4(color.at("randomColorMin"), b.color.randomColorMin);
		if (color.contains("randomColorMax")) b.color.randomColorMax = ReadVector4(color.at("randomColorMax"), b.color.randomColorMax);

		b.color.randomColorChangeIntervalMin = color.value("randomColorChangeIntervalMin", b.color.randomColorChangeIntervalMin);
		b.color.randomColorChangeIntervalMax = color.value("randomColorChangeIntervalMax", b.color.randomColorChangeIntervalMax);
		b.color.randomColorLerpSpeed = color.value("randomColorLerpSpeed", b.color.randomColorLerpSpeed);
	}

	if (j.contains("render")) {
		const json& render = j.at("render");
		b.render.billboardMode = ToBillboardMode(render.value("billboardMode", ToString(b.render.billboardMode)));
	}
}

} // namespace

namespace ParticleEffectResource {

bool Save(const std::string& filePath, const ParticleEffectDesc& effect) {
	json root;

	root["name"] = effect.name;
	root["textureFilePath"] = effect.textureFilePath;
	root["blendMode"] = ToString(effect.blendMode);

	root["emitter"] = {
		{ "translate", ToJson(effect.emitter.translate) },
		{ "spawnSize", ToJson(effect.emitter.spawnSize) },
		{ "count", effect.emitter.count },
		{ "frequency", effect.emitter.frequency },
		{ "isActive", effect.emitter.isActive }
	};

	WriteBehavior(root["behavior"], effect.behavior);

	std::ofstream file(filePath);
	if (!file.is_open()) {
		return false;
	}

	file << std::setw(2) << root << std::endl;
	return true;
}

bool Load(const std::string& filePath, ParticleEffectDesc& outEffect) {
	std::ifstream file(filePath);
	if (!file.is_open()) {
		return false;
	}

	json root;
	try {
		file >> root;
	} catch (...) {
		return false;
	}

	outEffect.name = root.value("name", outEffect.name);
	outEffect.textureFilePath = root.value("textureFilePath", outEffect.textureFilePath);
	outEffect.blendMode = ToBlendMode(root.value("blendMode", ToString(outEffect.blendMode)));

	if (root.contains("emitter")) {
		const json& emitter = root.at("emitter");

		if (emitter.contains("translate")) {
			outEffect.emitter.translate = ReadVector3(emitter.at("translate"), outEffect.emitter.translate);
		}
		if (emitter.contains("spawnSize")) {
			outEffect.emitter.spawnSize = ReadVector3(emitter.at("spawnSize"), outEffect.emitter.spawnSize);
		}

		outEffect.emitter.count = emitter.value("count", outEffect.emitter.count);
		outEffect.emitter.frequency = emitter.value("frequency", outEffect.emitter.frequency);
		outEffect.emitter.isActive = emitter.value("isActive", outEffect.emitter.isActive);
	}

	if (root.contains("behavior")) {
		ReadBehavior(root.at("behavior"), outEffect.behavior);
	}

	return true;
}

void PrepareParticleGroup(const ParticleEffectDesc& effect, bool clearParticles) {
	ParticleManager* particleManager = ParticleManager::GetInstance();

	particleManager->CreateParticleGroupIfNeeded(
		effect.name,
		effect.textureFilePath
	);

	particleManager->SetGroupBlendMode(effect.name, effect.blendMode);

	if (clearParticles) {
		particleManager->ClearParticleGroup(effect.name);
	}
}

void ApplyToEmitter(ParticleEmitter& emitter, const ParticleEffectDesc& effect) {
	emitter.SetTranslate(effect.emitter.translate);
	emitter.SetSpawnSize(effect.emitter.spawnSize);
	emitter.SetCount(effect.emitter.count);
	emitter.SetFrequency(effect.emitter.frequency);
	emitter.SetActive(effect.emitter.isActive);
	emitter.SetBehavior(effect.behavior);
}

ParticleEmitter* CreateEmitter(const ParticleEffectDesc& effect) {
	PrepareParticleGroup(effect, true);

	ParticleEmitter* emitter = new ParticleEmitter();
	emitter->Initialize(ParticleManager::GetInstance(), effect.name);
	ApplyToEmitter(*emitter, effect);

	return emitter;
}

} // namespace ParticleEffectResource
