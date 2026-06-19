#include "LightningRenderer.h"

#include <algorithm>
#include <cmath>

#include "../debug/DebugRenderer.h"
#include "../externals/imgui/imgui.h"
#include "../math/Math.h"

void LightningRenderer::Update(float deltaTime) {
	time_ += deltaTime;
	if (activeTime_ > 0.0f) {
		activeTime_ = (std::max)(0.0f, activeTime_ - deltaTime);
	}
}

void LightningRenderer::DrawDebug() const {
	if (
		!settings_.enabled ||
		settings_.segmentCount == 0 ||
		(activeTime_ <= 0.0f && !settings_.previewContinuous)
	) {
		return;
	}

	uint32_t state =
		settings_.seed ^
		(static_cast<uint32_t>(time_ * 24.0f) * 747796405u);

	const uint32_t segmentCount = (std::max)(settings_.segmentCount, 1u);
	const Vector3 direction = Math::Subtract(settings_.end, settings_.start);
	const float length = Math::Length(direction);
	const Vector3 forward =
		length > 0.0001f ? Math::Normalize(direction) : Vector3{ 0.0f, 1.0f, 0.0f };

	Vector3 previous = settings_.start;
	for (uint32_t index = 1; index <= segmentCount; ++index) {
		const float t = static_cast<float>(index) / static_cast<float>(segmentCount);
		Vector3 point = Math::Add(settings_.start, Math::Multiply(direction, t));

		if (index < segmentCount) {
			Vector3 side = RandomUnitVector(state);
			side = Math::Subtract(
				side,
				Math::Multiply(forward, Math::Dot(side, forward))
			);
			const float sideLength = Math::Length(side);
			if (sideLength > 0.0001f) {
				side = Math::Normalize(side);
				const float width =
					settings_.jitter *
					(0.25f + Random01(state)) *
					std::sin(t * 3.14159265f);
				point = Math::Add(point, Math::Multiply(side, width));
			}
		}

		AddThickLine(
			previous,
			point,
			settings_.coreColor,
			settings_.thickness,
			state
		);

		if (
			index < segmentCount &&
			Random01(state) < settings_.branchProbability
		) {
			Vector3 branchDirection = RandomUnitVector(state);
			branchDirection = Math::Normalize(
				Math::Add(
					branchDirection,
					Math::Multiply(forward, 0.45f)
				)
			);
			const float branchLength =
				settings_.branchLength * (0.4f + Random01(state));
			AddThickLine(
				point,
				Math::Add(point, Math::Multiply(branchDirection, branchLength)),
				settings_.branchColor,
				settings_.thickness * 0.55f,
				state
			);
		}

		previous = point;
	}
}

void LightningRenderer::Trigger(const Settings& settings) {
	settings_ = settings;
	settings_.enabled = true;
	settings_.previewContinuous = false;
	activeTime_ = (std::max)(settings_.duration, 0.01f);
}

void LightningRenderer::DrawImGui(const char* label) {
#if defined(_DEBUG) || defined(DEVELOPMENT)
	if (!ImGui::Begin(label)) {
		ImGui::End();
		return;
	}

	ImGui::Checkbox("Enabled", &settings_.enabled);
	ImGui::Checkbox("Preview Continuous", &settings_.previewContinuous);
	ImGui::DragFloat3("Start", &settings_.start.x, 0.05f);
	ImGui::DragFloat3("End", &settings_.end.x, 0.05f);
	ImGui::ColorEdit4("Core Color", &settings_.coreColor.x, ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR);
	ImGui::ColorEdit4("Branch Color", &settings_.branchColor.x, ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR);
	ImGui::DragFloat("Jitter", &settings_.jitter, 0.01f, 0.0f, 3.0f);
	ImGui::DragFloat("Branch Length", &settings_.branchLength, 0.01f, 0.0f, 3.0f);
	ImGui::SliderFloat("Branch Probability", &settings_.branchProbability, 0.0f, 1.0f);
	ImGui::DragFloat("Thickness", &settings_.thickness, 0.001f, 0.001f, 1.0f, "%.3f");
	ImGui::DragFloat("Duration", &settings_.duration, 0.01f, 0.01f, 5.0f);
	int segments = static_cast<int>(settings_.segmentCount);
	if (ImGui::SliderInt("Segments", &segments, 1, 64)) {
		settings_.segmentCount = static_cast<uint32_t>(segments);
	}
	int seed = static_cast<int>(settings_.seed);
	if (ImGui::DragInt("Seed", &seed, 1.0f, 0, 999999)) {
		settings_.seed = static_cast<uint32_t>((std::max)(seed, 0));
	}
	if (ImGui::Button("Trigger")) {
		Settings triggerSettings = settings_;
		triggerSettings.previewContinuous = false;
		Trigger(triggerSettings);
	}

	ImGui::End();
#else
	(void)label;
#endif
}

void LightningRenderer::AddThickLine(
	const Vector3& start,
	const Vector3& end,
	const Vector4& color,
	float thickness,
	uint32_t& state
) const {
	DebugRenderer::GetInstance()->AddLine(start, end, color);

	const int extraLineCount = std::clamp(
		static_cast<int>(std::ceil(thickness * 90.0f)),
		0,
		14
	);
	if (extraLineCount <= 0 || thickness <= 0.001f) {
		return;
	}

	const Vector3 segment = Math::Subtract(end, start);
	const float segmentLength = Math::Length(segment);
	if (segmentLength <= 0.0001f) {
		return;
	}
	const Vector3 forward = Math::Normalize(segment);

	for (int index = 0; index < extraLineCount; ++index) {
		Vector3 side = RandomUnitVector(state);
		side = Math::Subtract(side, Math::Multiply(forward, Math::Dot(side, forward)));
		const float sideLength = Math::Length(side);
		if (sideLength <= 0.0001f) {
			continue;
		}
		side = Math::Normalize(side);

		const float radius = thickness * (0.2f + Random01(state) * 0.8f);
		const Vector3 offset = Math::Multiply(side, radius);
		DebugRenderer::GetInstance()->AddLine(
			Math::Add(start, offset),
			Math::Add(end, offset),
			color
		);
	}
}

float LightningRenderer::Random01(uint32_t& state) {
	state ^= state << 13;
	state ^= state >> 17;
	state ^= state << 5;
	return static_cast<float>(state & 0x00ffffffu) / static_cast<float>(0x01000000u);
}

Vector3 LightningRenderer::RandomUnitVector(uint32_t& state) {
	Vector3 v{
		Random01(state) * 2.0f - 1.0f,
		Random01(state) * 2.0f - 1.0f,
		Random01(state) * 2.0f - 1.0f
	};
	const float length = Math::Length(v);
	if (length <= 0.0001f) {
		return { 1.0f, 0.0f, 0.0f };
	}
	return Math::Multiply(v, 1.0f / length);
}
