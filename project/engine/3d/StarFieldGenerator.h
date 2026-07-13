// 役割: 星の位置と色を生成し、星空描画用データを提供する。
#pragma once

#include <atomic>
#include <cstdint>
#include <future>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "../math/Vector3.h"

class StarFieldGenerator {
public:
	struct Settings {
		uint32_t seed = 12345;
		uint32_t resolution = 1024;
		uint32_t starCount = 2200;
		// 1024x1024のCubemap面を基準にした半径。実際のTexel半径は解像度に追従する。
		float starSizeMin = 1.2f;
		float starSizeMax = 2.4f;
		float starBrightnessMin = 2.0f;
		float starBrightnessMax = 12.0f;
		bool milkyWayEnabled = false;
		Vector3 milkyWayNormal = { 0.15f, 0.92f, 0.36f };
		float milkyWayWidth = 0.18f;
		float milkyWayBrightness = 0.45f;
		float milkyWayNoiseScale = 7.0f;
		Vector3 backgroundColor = { 0.001f, 0.002f, 0.008f };
	};

	// Save & Applyが成功したフレームだけ、プロジェクト相対DDSパスを返す。
	std::optional<std::string> DrawImGui(const char* windowTitle = "Environment");
	bool GenerateAndSave(const std::string& requestedPath);

	const Settings& GetSettings() const { return settings_; }
	const std::string& GetStatus() const { return status_; }

private:
	struct GenerationResult {
		bool succeeded = false;
		std::string status;
		std::string requestedPath;
	};
	struct GenerationProgress {
		std::atomic<float> value{ 0.0f };
		std::atomic<uint32_t> stage{ 0 };
	};

	void RefreshSkyboxFiles();
	bool GenerateAndSave(const std::string& requestedPath, GenerationProgress* progress);

	Settings settings_{};
	char outputPath_[256] = "resources/skyboxes/generated_space.dds";
	std::string status_;
	std::vector<std::string> skyboxFiles_;
	int selectedSkyboxIndex_ = -1;
	std::future<GenerationResult> generationFuture_;
	std::shared_ptr<GenerationProgress> generationProgress_;
	bool generationInProgress_ = false;
	double generationStartTime_ = 0.0;
};
