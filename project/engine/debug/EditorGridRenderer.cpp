// 役割: 任意の2軸で構成したGridをDepth Test付き線分Queueへ追加する。
#include "EditorGridRenderer.h"

#include "DebugRenderer.h"
#include "../math/Math.h"

#include <algorithm>
#include <cmath>

void EditorGridRenderer::AddGrid(
	DebugRenderer& debugRenderer,
	const EditorGridSettings& settings
) {
	const float axisULength = Math::Length(settings.axisU);
	const float axisVLength = Math::Length(settings.axisV);
	if (axisULength <= 0.0001f || axisVLength <= 0.0001f) {
		return;
	}

	const Vector3 axisU = Math::Multiply(settings.axisU, 1.0f / axisULength);
	const Vector3 axisV = Math::Multiply(settings.axisV, 1.0f / axisVLength);
	if (std::abs(Math::Dot(axisU, axisV)) >= 0.999f) {
		return;
	}

	const float spacing = std::clamp(settings.spacing, 0.001f, 10000.0f);
	const float requestedExtent = std::clamp(
		settings.extent,
		spacing,
		100000.0f
	);
	const int halfLineCount = std::clamp(
		static_cast<int>(std::ceil(requestedExtent / spacing)),
		1,
		512
	);
	const int majorLineEvery = std::clamp(settings.majorLineEvery, 1, 512);
	const float extent = spacing * static_cast<float>(halfLineCount);
	const Vector3 extentU = Math::Multiply(axisU, extent);
	const Vector3 extentV = Math::Multiply(axisV, extent);

	for (int index = -halfLineCount; index <= halfLineCount; ++index) {
		const float offset = spacing * static_cast<float>(index);
		const bool axisLine = index == 0;
		const bool majorLine = index % majorLineEvery == 0;
		const Vector4 regularColor = majorLine
			? settings.majorColor
			: settings.minorColor;

		const Vector3 offsetU = Math::Multiply(axisU, offset);
		const Vector3 centerOnU = Math::Add(settings.origin, offsetU);
		debugRenderer.AddDepthTestedLine(
			Math::Subtract(centerOnU, extentV),
			Math::Add(centerOnU, extentV),
			axisLine ? settings.axisVColor : regularColor
		);

		const Vector3 offsetV = Math::Multiply(axisV, offset);
		const Vector3 centerOnV = Math::Add(settings.origin, offsetV);
		debugRenderer.AddDepthTestedLine(
			Math::Subtract(centerOnV, extentU),
			Math::Add(centerOnV, extentU),
			axisLine ? settings.axisUColor : regularColor
		);
	}
}
