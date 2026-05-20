#pragma once
#include <string>

#include "ParticleEffectResource.h"

class ParticleEmitter;

class ParticleEffectEditor {
public:
	void Initialize(const ParticleEffectDesc& effect, const std::string& filePath);

	// Applyが押されたらtrueを返す
	// previewEmitterはこの関数内で作り直すので、呼び出し側が所有する
	bool DrawImGui(ParticleEffectDesc& effect, ParticleEmitter*& previewEmitter);

private:
	void CopyStringsFromEffect(const ParticleEffectDesc& effect);
	void CopyStringsToEffect(ParticleEffectDesc& effect);

private:
	bool initialized_ = false;

	char filePath_[260] = {};
	char name_[128] = {};
	char textureFilePath_[260] = {};
};
