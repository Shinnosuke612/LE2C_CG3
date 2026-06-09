#pragma once
#include <string>
#include <vector>

#include "ParticleEffectResource.h"

class ParticleEmitter;

class ParticleEffectEditor {
public:
	void Initialize(const ParticleEffectDesc& effect, const std::string& filePath);

	// Applyが押されたらtrueを返す
	// previewEmitterはこの関数内で作り直すので、呼び出し側が所有する
	bool DrawImGui(
		ParticleEffectDesc& effect,
		ParticleEmitter*& previewEmitter,
		const char* windowTitle = "Particle Effect Editor"
	);

private:
	void CopyStringsFromEffect(const ParticleEffectDesc& effect);
	void CopyStringsToEffect(ParticleEffectDesc& effect);
	void RefreshEffectFiles();
	bool LoadEffectFile(
		const std::string& filePath,
		ParticleEffectDesc& effect,
		ParticleEmitter*& previewEmitter
	);

private:
	bool initialized_ = false;

	char filePath_[260] = {};
	char name_[128] = {};
	char textureFilePath_[260] = {};

	std::vector<std::string> effectFilePaths_;
	std::vector<std::string> effectFileNames_;
	int selectedEffectIndex_ = -1;
};
