#pragma once

#include <cstdint>

#include "../math/Vector3.h"
#include "../math/Vector4.h"

class LightningRenderer {
public:
	struct Settings {
		Vector3 start{ -1.0f, 2.8f, -1.0f };
		Vector3 end{ 1.0f, 1.1f, -1.0f };
		Vector4 coreColor{ 3.5f, 4.5f, 8.0f, 1.0f };
		Vector4 branchColor{ 1.0f, 2.0f, 5.0f, 1.0f };
		float jitter = 0.35f;
		float branchLength = 0.45f;
		float branchProbability = 0.35f;
		float thickness = 0.03f;
		float duration = 0.12f;
		uint32_t segmentCount = 12;
		uint32_t seed = 1;
		bool enabled = true;
		bool previewContinuous = true;
	};

	void Update(float deltaTime);
	void DrawDebug() const;
	void DrawImGui(const char* label);
	void Trigger(const Settings& settings);

	Settings& GetSettings() { return settings_; }
	const Settings& GetSettings() const { return settings_; }

private:
	static float Random01(uint32_t& state);
	static Vector3 RandomUnitVector(uint32_t& state);
	void AddThickLine(
		const Vector3& start,
		const Vector3& end,
		const Vector4& color,
		float thickness,
		uint32_t& state
	) const;

	Settings settings_{};
	float time_ = 0.0f;
	float activeTime_ = 0.0f;
};
