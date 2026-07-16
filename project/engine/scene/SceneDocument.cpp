// 役割: SceneDocumentのJSON入出力、Hierarchy操作、Component検証を実装する。
#include "SceneDocument.h"
#include "SceneDocumentMigrator.h"
#include "SceneEntityQuery.h"
#include "SceneTransformResolver.h"
#include "SceneValidator.h"
#include "../math/Matrix4x4.h"
#include "../utility/StringUtility.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <functional>
#include <system_error>
#include <unordered_set>

#include <Windows.h>

#include "../../externals/nlohmann/json.hpp"

namespace {
	using json = nlohmann::json;
	using SceneEntityQuery::FindComponent;
	using SceneTransformResolver::ResolveSceneWorldMatrix;

	json VectorToJson(const Vector3& value) {
		return json::array({ value.x, value.y, value.z });
	}

	json VectorToJson(const Vector2& value) {
		return json::array({ value.x, value.y });
	}

	json VectorToJson(const Vector4& value) {
		return json::array({ value.x, value.y, value.z, value.w });
	}

	json QuaternionToJson(const Quaternion& value) {
		return json::array({ value.x, value.y, value.z, value.w });
	}

	Vector3 JsonToVector(const json& value, const Vector3& fallback) {
		if (!value.is_array() || value.size() != 3) {
			return fallback;
		}
		return {
			value[0].get<float>(),
			value[1].get<float>(),
			value[2].get<float>()
		};
	}

	Vector2 JsonToVector(const json& value, const Vector2& fallback) {
		if (!value.is_array() || value.size() != 2) {
			return fallback;
		}
		return { value[0].get<float>(), value[1].get<float>() };
	}

	Vector4 JsonToVector(const json& value, const Vector4& fallback) {
		if (!value.is_array() || value.size() != 4) {
			return fallback;
		}
		return {
			value[0].get<float>(), value[1].get<float>(),
			value[2].get<float>(), value[3].get<float>()
		};
	}

	json PostProcessToJson(const ScenePostProcessSettings& settings) {
		return {
			{ "bloomEnabled", settings.bloomEnabled },
			{ "baseExposure", settings.baseExposure },
			{ "toneMapMode", settings.toneMapMode },
			{ "bloomThreshold", settings.bloomThreshold },
			{ "bloomSoftKnee", settings.bloomSoftKnee },
			{ "bloomIntensity", settings.bloomIntensity },
			{ "bloomBlurIterations", settings.bloomBlurIterations },
			{ "bloomDownsampleScale", settings.bloomDownsampleScale },
			{ "bloomBlurRadius", settings.bloomBlurRadius },
			{ "grayscaleEnabled", settings.grayscaleEnabled },
			{ "vignetteEnabled", settings.vignetteEnabled },
			{ "boxBlurEnabled", settings.boxBlurEnabled },
			{ "gaussianBlurEnabled", settings.gaussianBlurEnabled },
			{ "depthOfFieldEnabled", settings.depthOfFieldEnabled },
			{ "radialBlurEnabled", settings.radialBlurEnabled },
			{ "noiseEnabled", settings.noiseEnabled },
			{ "dissolveEnabled", settings.dissolveEnabled },
			{ "outlineEnabled", settings.outlineEnabled },
			{ "underwaterEnabled", settings.underwaterEnabled },
			{ "waterRefractionEnabled", settings.waterRefractionEnabled },
			{ "vignetteScale", settings.vignetteScale },
			{ "vignettePower", settings.vignettePower },
			{ "vignetteIntensity", settings.vignetteIntensity },
			{ "boxBlurKernelSize", settings.boxBlurKernelSize },
			{ "boxBlurStrength", settings.boxBlurStrength },
			{ "gaussianBlurKernelSize", settings.gaussianBlurKernelSize },
			{ "gaussianBlurSigma", settings.gaussianBlurSigma },
			{ "gaussianBlurStrength", settings.gaussianBlurStrength },
			{ "depthOfFieldFocusDistance", settings.depthOfFieldFocusDistance },
			{ "depthOfFieldFocusRange", settings.depthOfFieldFocusRange },
			{ "depthOfFieldBlurStrength", settings.depthOfFieldBlurStrength },
			{ "depthOfFieldNearStrength", settings.depthOfFieldNearStrength },
			{ "depthOfFieldFarStrength", settings.depthOfFieldFarStrength },
			{ "depthOfFieldMaxRadius", settings.depthOfFieldMaxRadius },
			{ "radialBlurCenter", VectorToJson(settings.radialBlurCenter) },
			{ "radialBlurWidth", settings.radialBlurWidth },
			{ "radialBlurSamples", settings.radialBlurSamples },
			{ "noiseAnimate", settings.noiseAnimate },
			{ "noiseAmount", settings.noiseAmount },
			{ "noiseScale", settings.noiseScale },
			{ "noiseSpeed", settings.noiseSpeed },
			{ "noiseSeed", settings.noiseSeed },
			{ "dissolveMaskIndex", settings.dissolveMaskIndex },
			{ "dissolveThreshold", settings.dissolveThreshold },
			{ "dissolveEdgeWidth", settings.dissolveEdgeWidth },
			{ "dissolveEdgeColor", VectorToJson(settings.dissolveEdgeColor) },
			{ "outlineLuminanceEnabled", settings.outlineLuminanceEnabled },
			{ "outlineDepthEnabled", settings.outlineDepthEnabled },
			{ "outlineLuminanceWeight", settings.outlineLuminanceWeight },
			{ "outlineDepthWeight", settings.outlineDepthWeight },
			{ "outlineThreshold", settings.outlineThreshold },
			{ "outlineSoftness", settings.outlineSoftness },
			{ "outlineThickness", settings.outlineThickness },
			{ "outlineColor", VectorToJson(settings.outlineColor) },
			{ "underwaterTintColor", VectorToJson(settings.underwaterTintColor) },
			{ "underwaterIntensity", settings.underwaterIntensity },
			{ "underwaterFogDensity", settings.underwaterFogDensity },
			{ "underwaterDistortion", settings.underwaterDistortion },
			{ "waterRefractionTintColor", VectorToJson(settings.waterRefractionTintColor) },
			{ "waterRefractionStrength", settings.waterRefractionStrength },
			{ "waterRefractionEdgeSoftness", settings.waterRefractionEdgeSoftness },
			{ "waterRefractionTintStrength", settings.waterRefractionTintStrength }
		};
	}

	ScenePostProcessSettings PostProcessFromJson(
		const json& source,
		const ScenePostProcessSettings& fallback
	) {
		if (!source.is_object()) {
			return fallback;
		}

		ScenePostProcessSettings settings = fallback;
		settings.bloomEnabled = source.value("bloomEnabled", settings.bloomEnabled);
		settings.baseExposure = source.value("baseExposure", settings.baseExposure);
		settings.toneMapMode = source.value("toneMapMode", settings.toneMapMode);
		settings.bloomThreshold = source.value("bloomThreshold", settings.bloomThreshold);
		settings.bloomSoftKnee = source.value("bloomSoftKnee", settings.bloomSoftKnee);
		settings.bloomIntensity = source.value("bloomIntensity", settings.bloomIntensity);
		settings.bloomBlurIterations = source.value("bloomBlurIterations", settings.bloomBlurIterations);
		settings.bloomDownsampleScale = source.value("bloomDownsampleScale", settings.bloomDownsampleScale);
		settings.bloomBlurRadius = source.value("bloomBlurRadius", settings.bloomBlurRadius);
		settings.grayscaleEnabled = source.value("grayscaleEnabled", settings.grayscaleEnabled);
		settings.vignetteEnabled = source.value("vignetteEnabled", settings.vignetteEnabled);
		settings.boxBlurEnabled = source.value("boxBlurEnabled", settings.boxBlurEnabled);
		settings.gaussianBlurEnabled = source.value("gaussianBlurEnabled", settings.gaussianBlurEnabled);
		settings.depthOfFieldEnabled = source.value("depthOfFieldEnabled", settings.depthOfFieldEnabled);
		settings.radialBlurEnabled = source.value("radialBlurEnabled", settings.radialBlurEnabled);
		settings.noiseEnabled = source.value("noiseEnabled", settings.noiseEnabled);
		settings.dissolveEnabled = source.value("dissolveEnabled", settings.dissolveEnabled);
		settings.outlineEnabled = source.value("outlineEnabled", settings.outlineEnabled);
		settings.underwaterEnabled = source.value("underwaterEnabled", settings.underwaterEnabled);
		settings.waterRefractionEnabled = source.value("waterRefractionEnabled", settings.waterRefractionEnabled);
		settings.vignetteScale = source.value("vignetteScale", settings.vignetteScale);
		settings.vignettePower = source.value("vignettePower", settings.vignettePower);
		settings.vignetteIntensity = source.value("vignetteIntensity", settings.vignetteIntensity);
		settings.boxBlurKernelSize = source.value("boxBlurKernelSize", settings.boxBlurKernelSize);
		settings.boxBlurStrength = source.value("boxBlurStrength", settings.boxBlurStrength);
		settings.gaussianBlurKernelSize = source.value("gaussianBlurKernelSize", settings.gaussianBlurKernelSize);
		settings.gaussianBlurSigma = source.value("gaussianBlurSigma", settings.gaussianBlurSigma);
		settings.gaussianBlurStrength = source.value("gaussianBlurStrength", settings.gaussianBlurStrength);
		settings.depthOfFieldFocusDistance = source.value("depthOfFieldFocusDistance", settings.depthOfFieldFocusDistance);
		settings.depthOfFieldFocusRange = source.value("depthOfFieldFocusRange", settings.depthOfFieldFocusRange);
		settings.depthOfFieldBlurStrength = source.value("depthOfFieldBlurStrength", settings.depthOfFieldBlurStrength);
		settings.depthOfFieldNearStrength = source.value("depthOfFieldNearStrength", settings.depthOfFieldNearStrength);
		settings.depthOfFieldFarStrength = source.value("depthOfFieldFarStrength", settings.depthOfFieldFarStrength);
		settings.depthOfFieldMaxRadius = source.value("depthOfFieldMaxRadius", settings.depthOfFieldMaxRadius);
		if (source.contains("radialBlurCenter")) {
			settings.radialBlurCenter = JsonToVector(
				source.at("radialBlurCenter"),
				settings.radialBlurCenter
			);
		}
		settings.radialBlurWidth = source.value("radialBlurWidth", settings.radialBlurWidth);
		settings.radialBlurSamples = source.value("radialBlurSamples", settings.radialBlurSamples);
		settings.noiseAnimate = source.value("noiseAnimate", settings.noiseAnimate);
		settings.noiseAmount = source.value("noiseAmount", settings.noiseAmount);
		settings.noiseScale = source.value("noiseScale", settings.noiseScale);
		settings.noiseSpeed = source.value("noiseSpeed", settings.noiseSpeed);
		settings.noiseSeed = source.value("noiseSeed", settings.noiseSeed);
		settings.dissolveMaskIndex = source.value("dissolveMaskIndex", settings.dissolveMaskIndex);
		settings.dissolveThreshold = source.value("dissolveThreshold", settings.dissolveThreshold);
		settings.dissolveEdgeWidth = source.value("dissolveEdgeWidth", settings.dissolveEdgeWidth);
		if (source.contains("dissolveEdgeColor")) {
			settings.dissolveEdgeColor = JsonToVector(
				source.at("dissolveEdgeColor"),
				settings.dissolveEdgeColor
			);
		}
		settings.outlineLuminanceEnabled = source.value("outlineLuminanceEnabled", settings.outlineLuminanceEnabled);
		settings.outlineDepthEnabled = source.value("outlineDepthEnabled", settings.outlineDepthEnabled);
		settings.outlineLuminanceWeight = source.value("outlineLuminanceWeight", settings.outlineLuminanceWeight);
		settings.outlineDepthWeight = source.value("outlineDepthWeight", settings.outlineDepthWeight);
		settings.outlineThreshold = source.value("outlineThreshold", settings.outlineThreshold);
		settings.outlineSoftness = source.value("outlineSoftness", settings.outlineSoftness);
		settings.outlineThickness = source.value("outlineThickness", settings.outlineThickness);
		if (source.contains("outlineColor")) {
			settings.outlineColor = JsonToVector(
				source.at("outlineColor"),
				settings.outlineColor
			);
		}
		if (source.contains("underwaterTintColor")) {
			settings.underwaterTintColor = JsonToVector(
				source.at("underwaterTintColor"),
				settings.underwaterTintColor
			);
		}
		settings.underwaterIntensity = source.value("underwaterIntensity", settings.underwaterIntensity);
		settings.underwaterFogDensity = source.value("underwaterFogDensity", settings.underwaterFogDensity);
		settings.underwaterDistortion = source.value("underwaterDistortion", settings.underwaterDistortion);
		if (source.contains("waterRefractionTintColor")) {
			settings.waterRefractionTintColor = JsonToVector(
				source.at("waterRefractionTintColor"),
				settings.waterRefractionTintColor
			);
		}
		settings.waterRefractionStrength = source.value("waterRefractionStrength", settings.waterRefractionStrength);
		settings.waterRefractionEdgeSoftness = source.value("waterRefractionEdgeSoftness", settings.waterRefractionEdgeSoftness);
		settings.waterRefractionTintStrength = source.value("waterRefractionTintStrength", settings.waterRefractionTintStrength);

		settings.baseExposure = (std::max)(0.01f, settings.baseExposure);
		settings.toneMapMode = std::clamp(settings.toneMapMode, 0, 1);
		settings.bloomThreshold = (std::max)(0.0f, settings.bloomThreshold);
		settings.bloomSoftKnee = std::clamp(settings.bloomSoftKnee, 0.0f, 1.0f);
		settings.bloomIntensity = (std::max)(0.0f, settings.bloomIntensity);
		settings.bloomBlurIterations = std::clamp(settings.bloomBlurIterations, 0, 12);
		settings.bloomDownsampleScale = std::clamp(settings.bloomDownsampleScale, 1, 8);
		settings.bloomBlurRadius = (std::max)(0.0f, settings.bloomBlurRadius);
		settings.boxBlurKernelSize = settings.boxBlurKernelSize == 5 ? 5 : 3;
		settings.gaussianBlurKernelSize = settings.gaussianBlurKernelSize == 5 ? 5 : 3;
		settings.radialBlurSamples = std::clamp(settings.radialBlurSamples, 2, 32);
		settings.dissolveMaskIndex = std::clamp(settings.dissolveMaskIndex, 0, 1);
		settings.underwaterIntensity = std::clamp(settings.underwaterIntensity, 0.0f, 1.0f);
		settings.underwaterFogDensity = (std::max)(0.0f, settings.underwaterFogDensity);
		settings.underwaterDistortion = (std::max)(0.0f, settings.underwaterDistortion);
		settings.waterRefractionStrength =
			std::clamp(settings.waterRefractionStrength, 0.0f, 0.12f);
		settings.waterRefractionEdgeSoftness =
			std::clamp(settings.waterRefractionEdgeSoftness, 0.0f, 2.0f);
		settings.waterRefractionTintStrength =
			std::clamp(settings.waterRefractionTintStrength, 0.0f, 1.0f);
		return settings;
	}

	json DebugSettingsToJson(const SceneDebugSettings& settings) {
		return {
			{ "showCameraDirection", settings.showCameraDirection },
			{ "showColliders", settings.showColliders },
			{ "showCameraPath", settings.showCameraPath },
			{ "showCameraPathPointCameraDirection", settings.showCameraPathPointCameraDirection },
			{ "showSkeleton", settings.showSkeleton },
			{ "showJointNames", settings.showJointNames },
			{ "showJointAxes", settings.showJointAxes },
			{ "jointRadius", settings.jointRadius },
			{ "jointAxisLength", settings.jointAxisLength }
		};
	}

	SceneDebugSettings DebugSettingsFromJson(
		const json& source,
		const SceneDebugSettings& fallback
	) {
		if (!source.is_object()) {
			return fallback;
		}

		SceneDebugSettings settings = fallback;
		settings.showCameraDirection = source.value(
			"showCameraDirection",
			settings.showCameraDirection
		);
		settings.showColliders = source.value(
			"showColliders",
			settings.showColliders
		);
		settings.showCameraPath = source.value(
			"showCameraPath",
			settings.showCameraPath
		);
		settings.showCameraPathPointCameraDirection = source.value(
			"showCameraPathPointCameraDirection",
			settings.showCameraPathPointCameraDirection
		);
		settings.showSkeleton = source.value(
			"showSkeleton",
			settings.showSkeleton
		);
		settings.showJointNames = source.value(
			"showJointNames",
			settings.showJointNames
		);
		settings.showJointAxes = source.value(
			"showJointAxes",
			settings.showJointAxes
		);
		settings.jointRadius = (std::max)(
			source.value("jointRadius", settings.jointRadius),
			0.001f
		);
		settings.jointAxisLength = (std::max)(
			source.value("jointAxisLength", settings.jointAxisLength),
			0.001f
		);
		return settings;
	}

	Quaternion JsonToQuaternion(
		const json& value,
		const Quaternion& fallback
	) {
		if (!value.is_array() || value.size() != 4) {
			return fallback;
		}
		return Normalize({
			value[0].get<float>(), value[1].get<float>(),
			value[2].get<float>(), value[3].get<float>()
		});
	}

	json LightingSettingsToJson(const SceneLightingSettings& settings) {
		return {
			{ "shadowMapSize", settings.shadowMapSize }
		};
	}

	SceneLightingSettings LightingSettingsFromJson(
		const json& source,
		const SceneLightingSettings& fallback
	) {
		if (!source.is_object()) {
			return fallback;
		}

		SceneLightingSettings settings = fallback;
		const uint32_t requested = source.value(
			"shadowMapSize",
			settings.shadowMapSize
		);
		settings.shadowMapSize = requested <= 1024
			? 1024
			: requested <= 2048 ? 2048 : 4096;
		return settings;
	}

	Vector3 NormalizeDirectionVector(
		const Vector3& value,
		const Vector3& fallback
	) {
		const float length = std::sqrt(
			value.x * value.x +
			value.y * value.y +
			value.z * value.z
		);
		if (length <= 0.000001f) {
			return fallback;
		}
		return {
			value.x / length,
			value.y / length,
			value.z / length
		};
	}

	void NormalizeTeamSettings(SceneTeamSettings& team) {
		if (team.name.empty()) {
			team.name = "Team";
		}
		auto normalizeForwardAxis = [](std::string& axis) {
			if (
				axis != "+Z" &&
				axis != "-Z" &&
				axis != "+X" &&
				axis != "-X" &&
				axis != "+Y" &&
				axis != "-Y"
			) {
				axis = "+Z";
			}
		};
		normalizeForwardAxis(team.agentForwardAxis);
		team.agentMinSpeed = (std::max)(team.agentMinSpeed, 0.0f);
		team.agentMaxSpeed = (std::max)(team.agentMaxSpeed, team.agentMinSpeed);
		team.agentTurnSpeed = (std::max)(team.agentTurnSpeed, 0.0f);
		team.agentWanderStrength =
			(std::max)(team.agentWanderStrength, 0.0f);
		team.agentWanderChangeInterval =
			(std::max)(team.agentWanderChangeInterval, 0.0f);
		team.agentWanderDirectionRange = std::clamp(
			team.agentWanderDirectionRange,
			0.0f,
			3.14159265359f
		);
		team.agentWanderVerticalRange = std::clamp(
			team.agentWanderVerticalRange,
			0.0f,
			1.0f
		);
		team.agentRandomSeed = (std::max)(team.agentRandomSeed, 0);
		team.agentFlockDecisionInterval =
			(std::max)(team.agentFlockDecisionInterval, 0.0f);
		team.agentFlockAcceleration =
			(std::max)(team.agentFlockAcceleration, 0.0f);
		team.agentFlockTurnRate =
			(std::max)(team.agentFlockTurnRate, 0.0f);
		team.agentMemberCenterFollow =
			(std::max)(team.agentMemberCenterFollow, 0.0f);
		team.agentMemberJitterStrength =
			(std::max)(team.agentMemberJitterStrength, 0.0f);
		team.agentMemberJitterFrequency =
			(std::max)(team.agentMemberJitterFrequency, 0.0f);
		team.agentMemberJitterUpdateInterval =
			(std::max)(team.agentMemberJitterUpdateInterval, 0.0f);
		team.agentMemberJitterFollowSpeed =
			(std::max)(team.agentMemberJitterFollowSpeed, 0.0f);
		team.agentMemberSpeedVariation = std::clamp(
			team.agentMemberSpeedVariation,
			0.0f,
			1.0f
		);
		team.agentMemberLeashDistance =
			(std::max)(team.agentMemberLeashDistance, 0.0f);
		team.agentMemberLeashStrength =
			(std::max)(team.agentMemberLeashStrength, 0.0f);
		team.agentMemberCatchupSpeed =
			(std::max)(team.agentMemberCatchupSpeed, 0.0f);
		team.agentMemberSeparationUpdateInterval =
			(std::max)(team.agentMemberSeparationUpdateInterval, 0.0f);
		team.agentMemberSeparationBlend = std::clamp(
			team.agentMemberSeparationBlend,
			0.0f,
			1.0f
		);
		team.agentTeamHeadingDirection = NormalizeDirectionVector(
			team.agentTeamHeadingDirection,
			{ 0.0f, 0.0f, 1.0f }
		);
		team.agentTeamHeadingWeight =
			(std::max)(team.agentTeamHeadingWeight, 0.0f);
		team.agentTeamHeadingFollowSpeed =
			(std::max)(team.agentTeamHeadingFollowSpeed, 0.0f);
		team.agentTeamRotationWeight =
			std::clamp(team.agentTeamRotationWeight, 0.0f, 1.0f);
		team.agentTeamRotationFollowSpeed =
			(std::max)(team.agentTeamRotationFollowSpeed, 0.0f);
		team.agentRotationFollowSpeed =
			(std::max)(team.agentRotationFollowSpeed, 0.0f);
		team.agentPitchFromVerticalVelocity =
			(std::max)(team.agentPitchFromVerticalVelocity, 0.0f);
		team.agentBankingStrength =
			(std::max)(team.agentBankingStrength, 0.0f);
		team.agentSchoolingUpdateInterval =
			(std::max)(team.agentSchoolingUpdateInterval, 0.0f);
		team.agentSchoolingUpdateJitter =
			(std::max)(team.agentSchoolingUpdateJitter, 0.0f);
		team.agentNeighborLimit = (std::max)(team.agentNeighborLimit, 0);
		team.agentSchoolingBlend =
			std::clamp(team.agentSchoolingBlend, 0.0f, 1.0f);
		team.agentSeparationRadius =
			(std::max)(team.agentSeparationRadius, 0.0f);
		team.agentAlignmentRadius =
			(std::max)(team.agentAlignmentRadius, 0.0f);
		team.agentCohesionRadius =
			(std::max)(team.agentCohesionRadius, 0.0f);
		team.agentSeparationWeight =
			(std::max)(team.agentSeparationWeight, 0.0f);
		team.agentAlignmentWeight =
			(std::max)(team.agentAlignmentWeight, 0.0f);
		team.agentCohesionWeight =
			(std::max)(team.agentCohesionWeight, 0.0f);
	}

	json TeamToJson(const SceneTeamSettings& team) {
		return {
			{ "name", team.name },
			{ "agentBehaviorOverride", team.agentBehaviorOverride },
			{ "agentGroupName", team.agentGroupName },
			{ "agentMinSpeed", team.agentMinSpeed },
			{ "agentMaxSpeed", team.agentMaxSpeed },
			{ "agentTurnSpeed", team.agentTurnSpeed },
			{ "agentWanderStrength", team.agentWanderStrength },
			{ "agentWanderChangeInterval", team.agentWanderChangeInterval },
			{ "agentWanderDirectionRange", team.agentWanderDirectionRange },
			{ "agentWanderVerticalRange", team.agentWanderVerticalRange },
			{ "agentRandomizeSeedOnPlay", team.agentRandomizeSeedOnPlay },
			{ "agentRandomSeed", team.agentRandomSeed },
			{ "agentUseLeaderStartPosition", team.agentUseLeaderStartPosition },
			{ "agentLeaderStartPosition",
				VectorToJson(team.agentLeaderStartPosition) },
			{ "agentFlockDecisionInterval", team.agentFlockDecisionInterval },
			{ "agentFlockAcceleration", team.agentFlockAcceleration },
			{ "agentFlockTurnRate", team.agentFlockTurnRate },
			{ "agentMemberCenterFollow", team.agentMemberCenterFollow },
			{ "agentMemberJitterStrength", team.agentMemberJitterStrength },
			{ "agentMemberJitterFrequency", team.agentMemberJitterFrequency },
			{ "agentMemberJitterUpdateInterval",
				team.agentMemberJitterUpdateInterval },
			{ "agentMemberJitterFollowSpeed",
				team.agentMemberJitterFollowSpeed },
			{ "agentMemberSpeedVariation", team.agentMemberSpeedVariation },
			{ "agentMemberLeashDistance", team.agentMemberLeashDistance },
			{ "agentMemberLeashStrength", team.agentMemberLeashStrength },
			{ "agentMemberCatchupSpeed", team.agentMemberCatchupSpeed },
			{ "agentMemberSeparationUpdateInterval",
				team.agentMemberSeparationUpdateInterval },
			{ "agentMemberSeparationBlend", team.agentMemberSeparationBlend },
			{ "agentUseTeamHeading", team.agentUseTeamHeading },
			{ "agentTeamHeadingFromAverage",
				team.agentTeamHeadingFromAverage },
			{ "agentTeamHeadingDirection",
				VectorToJson(team.agentTeamHeadingDirection) },
			{ "agentTeamHeadingWeight", team.agentTeamHeadingWeight },
			{ "agentTeamHeadingFollowSpeed",
				team.agentTeamHeadingFollowSpeed },
			{ "agentUseTeamRotation", team.agentUseTeamRotation },
			{ "agentTeamRotationWeight", team.agentTeamRotationWeight },
			{ "agentTeamRotationFollowSpeed",
				team.agentTeamRotationFollowSpeed },
			{ "agentAlignForwardToVelocity",
				team.agentAlignForwardToVelocity },
			{ "agentForwardAxis", team.agentForwardAxis },
			{ "agentRotateAxisX", team.agentRotateAxisX },
			{ "agentRotateAxisY", team.agentRotateAxisY },
			{ "agentRotateAxisZ", team.agentRotateAxisZ },
			{ "agentRotationFollowSpeed", team.agentRotationFollowSpeed },
			{ "agentPitchFromVerticalVelocity",
				team.agentPitchFromVerticalVelocity },
			{ "agentBankingStrength", team.agentBankingStrength },
			{ "agentSchooling", team.agentSchooling },
			{ "agentSchoolingUpdateInterval",
				team.agentSchoolingUpdateInterval },
			{ "agentSchoolingUpdateJitter",
				team.agentSchoolingUpdateJitter },
			{ "agentNeighborLimit", team.agentNeighborLimit },
			{ "agentSchoolingBlend", team.agentSchoolingBlend },
			{ "agentSeparationRadius", team.agentSeparationRadius },
			{ "agentAlignmentRadius", team.agentAlignmentRadius },
			{ "agentCohesionRadius", team.agentCohesionRadius },
			{ "agentSeparationWeight", team.agentSeparationWeight },
			{ "agentAlignmentWeight", team.agentAlignmentWeight },
			{ "agentCohesionWeight", team.agentCohesionWeight },
			{ "agentVisualColor", VectorToJson(team.agentVisualColor) },
			{ "agentEnableLighting", team.agentEnableLighting }
		};
	}

	SceneTeamSettings TeamFromJson(const json& source) {
		SceneTeamSettings team{};
		if (!source.is_object()) {
			return team;
		}
		team.name = source.value("name", team.name);
		team.agentBehaviorOverride = source.value(
			"agentBehaviorOverride",
			team.agentBehaviorOverride
		);
		team.agentGroupName = source.value("agentGroupName", team.agentGroupName);
		team.agentMinSpeed = source.value("agentMinSpeed", team.agentMinSpeed);
		team.agentMaxSpeed = source.value("agentMaxSpeed", team.agentMaxSpeed);
		team.agentTurnSpeed = source.value("agentTurnSpeed", team.agentTurnSpeed);
		team.agentWanderStrength = source.value(
			"agentWanderStrength",
			team.agentWanderStrength
		);
		team.agentWanderChangeInterval = source.value(
			"agentWanderChangeInterval",
			team.agentWanderChangeInterval
		);
		team.agentWanderDirectionRange = source.value(
			"agentWanderDirectionRange",
			team.agentWanderDirectionRange
		);
		team.agentWanderVerticalRange = source.value(
			"agentWanderVerticalRange",
			team.agentWanderVerticalRange
		);
		team.agentRandomizeSeedOnPlay = source.value(
			"agentRandomizeSeedOnPlay",
			team.agentRandomizeSeedOnPlay
		);
		team.agentRandomSeed = source.value(
			"agentRandomSeed",
			team.agentRandomSeed
		);
		team.agentUseLeaderStartPosition = source.value(
			"agentUseLeaderStartPosition",
			team.agentUseLeaderStartPosition
		);
		if (source.contains("agentLeaderStartPosition")) {
			team.agentLeaderStartPosition = JsonToVector(
				source.at("agentLeaderStartPosition"),
				team.agentLeaderStartPosition
			);
		}
		team.agentFlockDecisionInterval = source.value(
			"agentFlockDecisionInterval",
			team.agentFlockDecisionInterval
		);
		team.agentFlockAcceleration = source.value(
			"agentFlockAcceleration",
			team.agentFlockAcceleration
		);
		team.agentFlockTurnRate = source.value(
			"agentFlockTurnRate",
			team.agentFlockTurnRate
		);
		team.agentMemberCenterFollow = source.value(
			"agentMemberCenterFollow",
			team.agentMemberCenterFollow
		);
		team.agentMemberJitterStrength = source.value(
			"agentMemberJitterStrength",
			team.agentMemberJitterStrength
		);
		team.agentMemberJitterFrequency = source.value(
			"agentMemberJitterFrequency",
			team.agentMemberJitterFrequency
		);
		team.agentMemberJitterUpdateInterval = source.value(
			"agentMemberJitterUpdateInterval",
			team.agentMemberJitterUpdateInterval
		);
		team.agentMemberJitterFollowSpeed = source.value(
			"agentMemberJitterFollowSpeed",
			team.agentMemberJitterFollowSpeed
		);
		team.agentMemberSpeedVariation = source.value(
			"agentMemberSpeedVariation",
			team.agentMemberSpeedVariation
		);
		team.agentMemberLeashDistance = source.value(
			"agentMemberLeashDistance",
			team.agentMemberLeashDistance
		);
		team.agentMemberLeashStrength = source.value(
			"agentMemberLeashStrength",
			team.agentMemberLeashStrength
		);
		team.agentMemberCatchupSpeed = source.value(
			"agentMemberCatchupSpeed",
			team.agentMemberCatchupSpeed
		);
		team.agentMemberSeparationUpdateInterval = source.value(
			"agentMemberSeparationUpdateInterval",
			team.agentMemberSeparationUpdateInterval
		);
		team.agentMemberSeparationBlend = source.value(
			"agentMemberSeparationBlend",
			team.agentMemberSeparationBlend
		);
		team.agentUseTeamHeading = source.value(
			"agentUseTeamHeading",
			team.agentUseTeamHeading
		);
		team.agentTeamHeadingFromAverage = source.value(
			"agentTeamHeadingFromAverage",
			team.agentTeamHeadingFromAverage
		);
		if (source.contains("agentTeamHeadingDirection")) {
			team.agentTeamHeadingDirection = JsonToVector(
				source.at("agentTeamHeadingDirection"),
				team.agentTeamHeadingDirection
			);
		}
		team.agentTeamHeadingWeight = source.value(
			"agentTeamHeadingWeight",
			team.agentTeamHeadingWeight
		);
		team.agentTeamHeadingFollowSpeed = source.value(
			"agentTeamHeadingFollowSpeed",
			team.agentTeamHeadingFollowSpeed
		);
		team.agentUseTeamRotation = source.value(
			"agentUseTeamRotation",
			team.agentUseTeamRotation
		);
		team.agentTeamRotationWeight = source.value(
			"agentTeamRotationWeight",
			team.agentTeamRotationWeight
		);
		team.agentTeamRotationFollowSpeed = source.value(
			"agentTeamRotationFollowSpeed",
			team.agentTeamRotationFollowSpeed
		);
		team.agentAlignForwardToVelocity = source.value(
			"agentAlignForwardToVelocity",
			team.agentAlignForwardToVelocity
		);
		team.agentForwardAxis = source.value(
			"agentForwardAxis",
			team.agentForwardAxis
		);
		team.agentRotateAxisX = source.value(
			"agentRotateAxisX",
			team.agentRotateAxisX
		);
		team.agentRotateAxisY = source.value(
			"agentRotateAxisY",
			team.agentRotateAxisY
		);
		team.agentRotateAxisZ = source.value(
			"agentRotateAxisZ",
			team.agentRotateAxisZ
		);
		team.agentRotationFollowSpeed = source.value(
			"agentRotationFollowSpeed",
			team.agentRotationFollowSpeed
		);
		team.agentPitchFromVerticalVelocity = source.value(
			"agentPitchFromVerticalVelocity",
			team.agentPitchFromVerticalVelocity
		);
		team.agentBankingStrength = source.value(
			"agentBankingStrength",
			team.agentBankingStrength
		);
		team.agentSchooling = source.value("agentSchooling", team.agentSchooling);
		team.agentSchoolingUpdateInterval = source.value(
			"agentSchoolingUpdateInterval",
			team.agentSchoolingUpdateInterval
		);
		team.agentSchoolingUpdateJitter = source.value(
			"agentSchoolingUpdateJitter",
			team.agentSchoolingUpdateJitter
		);
		team.agentNeighborLimit = source.value(
			"agentNeighborLimit",
			team.agentNeighborLimit
		);
		team.agentSchoolingBlend = source.value(
			"agentSchoolingBlend",
			team.agentSchoolingBlend
		);
		team.agentSeparationRadius = source.value(
			"agentSeparationRadius",
			team.agentSeparationRadius
		);
		team.agentAlignmentRadius = source.value(
			"agentAlignmentRadius",
			team.agentAlignmentRadius
		);
		team.agentCohesionRadius = source.value(
			"agentCohesionRadius",
			team.agentCohesionRadius
		);
		team.agentSeparationWeight = source.value(
			"agentSeparationWeight",
			team.agentSeparationWeight
		);
		team.agentAlignmentWeight = source.value(
			"agentAlignmentWeight",
			team.agentAlignmentWeight
		);
		team.agentCohesionWeight = source.value(
			"agentCohesionWeight",
			team.agentCohesionWeight
		);
		if (source.contains("agentVisualColor")) {
			team.agentVisualColor = JsonToVector(
				source.at("agentVisualColor"),
				team.agentVisualColor
			);
		}
		team.agentEnableLighting = source.value(
			"agentEnableLighting",
			team.agentEnableLighting
		);
		NormalizeTeamSettings(team);
		return team;
	}

	std::string MakeUniqueTeamName(
		const std::vector<SceneTeamSettings>& teams,
		const std::string& requestedName,
		const std::string& ignoredName = {}
	) {
		const std::string baseName = requestedName.empty()
			? std::string("Team")
			: requestedName;
		std::string candidate = baseName;
		uint32_t suffix = 2;
		auto exists = [&](const std::string& name) {
			if (!ignoredName.empty() && name == ignoredName) {
				return false;
			}
			return std::any_of(
				teams.begin(),
				teams.end(),
				[&](const SceneTeamSettings& team) {
					return team.name == name;
				}
			);
		};
		while (exists(candidate)) {
			candidate = baseName + " " + std::to_string(suffix++);
		}
		return candidate;
	}

	json ComponentToJson(const SceneComponent& component) {
		json result = {
			{ "type", component.type },
			{ "enabled", component.enabled }
		};
		if (component.type == "MeshRenderer") {
			result["modelPath"] = component.modelPath;
			result["cullMode"] = component.meshCullMode;
			result["environmentReflectionOverride"] =
				component.meshEnvironmentReflectionOverride;
			result["environmentReflectionIntensity"] =
				component.meshEnvironmentReflectionIntensity;
			json materialOverrides = json::array();
			for (const SceneMeshMaterialOverride& override :
				component.meshMaterialOverrides) {
				materialOverrides.push_back({
					{ "materialName", override.materialName },
					{ "enabled", override.enabled },
					{ "colorOverrideEnabled", override.colorOverrideEnabled },
					{ "color", VectorToJson(override.color) },
					{ "texturePath", override.texturePath }
				});
			}
			result["materialOverrides"] = std::move(materialOverrides);
		} else if (component.type == "Environment") {
			result["skyboxEnabled"] = component.environmentSkyboxEnabled;
			result["skyboxPath"] = component.environmentSkyboxPath;
			result["skyboxIntensity"] = component.environmentSkyboxIntensity;
			result["reflectionIntensity"] =
				component.environmentReflectionIntensity;
		} else if (component.type == "SpriteRenderer") {
			result["texturePath"] = component.texturePath;
			result["size"] = VectorToJson(component.spriteSize);
			result["anchor"] = VectorToJson(component.spriteAnchor);
			result["color"] = VectorToJson(component.spriteColor);
			result["flipX"] = component.spriteFlipX;
			result["flipY"] = component.spriteFlipY;
		} else if (component.type == "Camera") {
			result["isMain"] = component.cameraIsMain;
			result["fovY"] = component.cameraFovY;
			result["nearClip"] = component.cameraNearClip;
			result["farClip"] = component.cameraFarClip;
			result["invertYaw"] = component.cameraInvertYaw;
			result["invertPitch"] = component.cameraInvertPitch;
		} else if (component.type == "Light") {
			result["lightType"] = component.lightType;
			result["color"] = VectorToJson(component.lightColor);
			result["intensity"] = component.lightIntensity;
			result["range"] = component.lightRange;
			result["decay"] = component.lightDecay;
			result["innerAngle"] = component.lightSpotInnerAngle;
			result["outerAngle"] = component.lightSpotOuterAngle;
			result["castsShadow"] = component.lightCastsShadow;
			result["shadow"] = {
				{ "bias", component.lightShadowBias },
				{ "normalBias", component.lightShadowNormalBias },
				{ "strength", component.lightShadowStrength },
				{ "distance", component.lightShadowDistance },
				{ "orthographicSize", component.lightShadowOrthographicSize },
				{ "nearClip", component.lightShadowNearClip },
				{ "farClip", component.lightShadowFarClip },
				{ "texelSnap", component.lightShadowTexelSnap }
			};
		} else if (component.type == "MonitorRenderer") {
			result["cameraEntityId"] = component.monitorCameraEntityId;
			result["cameraName"] = component.monitorCameraName;
			result["resolutionPreset"] = component.monitorResolutionPreset;
			result["width"] = component.monitorWidth;
			result["height"] = component.monitorHeight;
			result["hideSelf"] = component.monitorHideSelf;
		} else if (component.type == "ThirdPersonCamera") {
			result["distance"] = component.thirdPersonDistance;
			result["aimDistance"] = component.thirdPersonAimDistance;
			result["targetOffset"] = VectorToJson(component.thirdPersonTargetOffset);
			result["aimTargetOffset"] =
				VectorToJson(component.thirdPersonAimTargetOffset);
			result["mouseSensitivity"] = component.thirdPersonMouseSensitivity;
			result["minPitch"] = component.thirdPersonMinPitch;
			result["maxPitch"] = component.thirdPersonMaxPitch;
			result["occlusionMargin"] = component.thirdPersonOcclusionMargin;
			result["invertYaw"] = component.thirdPersonInvertYaw;
			result["invertPitch"] = component.thirdPersonInvertPitch;
		} else if (component.type == "Animator") {
			result["playOnStart"] = component.animatorPlayOnStart;
			result["loop"] = component.animatorLoop;
			result["speed"] = component.animatorSpeed;
			result["defaultClip"] = component.animatorDefaultClip;
			result["transitionDuration"] =
				component.animatorTransitionDuration;
			result["blendCurve"] = component.animatorBlendCurve;
		} else if (component.type == "PhysicsBody") {
			result["bodyType"] = component.physicsBodyType;
			result["mass"] = component.physicsMass;
			result["useGravity"] = component.physicsUseGravity;
			result["gravityScale"] = component.physicsGravityScale;
			result["drag"] = component.physicsDrag;
			result["restitution"] = component.physicsRestitution;
			result["friction"] = component.physicsFriction;
			result["maxFallSpeed"] = component.physicsMaxFallSpeed;
			result["velocity"] = VectorToJson(component.physicsVelocity);
			result["freezePositionX"] = component.physicsFreezePositionX;
			result["freezePositionY"] = component.physicsFreezePositionY;
			result["freezePositionZ"] = component.physicsFreezePositionZ;
		} else if (component.type == "OBBCollider") {
			result["offset"] = VectorToJson(component.colliderOffset);
			result["sizeMultiplier"] =
				VectorToJson(component.colliderSizeMultiplier);
			result["debugColor"] = VectorToJson(component.colliderDebugColor);
			result["shape"] = component.colliderShape;
			result["sphereRadius"] = component.colliderSphereRadius;
			result["debugVisible"] = component.colliderDebugVisible;
			result["debugDrawMode"] = component.colliderDebugDrawMode;
			result["debugSegments"] = component.colliderDebugSegments;
		} else if (component.type == "PlayerBehavior") {
			result["moveSpeed"] = component.playerMoveSpeed;
			result["jumpVelocity"] = component.playerJumpVelocity;
			result["turnResponsiveness"] = component.playerTurnResponsiveness;
			result["dashMultiplier"] = component.playerDashMultiplier;
			result["cameraRelativeMove"] = component.playerCameraRelativeMove;
			result["allowJump"] = component.playerAllowJump;
		} else if (component.type == "AgentBehavior") {
			result["behaviorName"] = component.agentBehaviorName;
			result["profileName"] = component.agentProfileName;
			result["groupName"] = component.agentGroupName;
			result["boundsEntityId"] = component.agentBoundsEntityId;
			result["boundsName"] = component.agentBoundsName;
			result["attractorEntityId"] = component.agentAttractorEntityId;
			result["attractorTag"] = component.agentAttractorTag;
			result["useWaterBounds"] = component.agentUseWaterBounds;
			result["minSpeed"] = component.agentMinSpeed;
			result["maxSpeed"] = component.agentMaxSpeed;
			result["turnSpeed"] = component.agentTurnSpeed;
			result["wanderStrength"] = component.agentWanderStrength;
			result["wanderChangeInterval"] = component.agentWanderChangeInterval;
			result["wanderDirectionRange"] = component.agentWanderDirectionRange;
			result["wanderVerticalRange"] = component.agentWanderVerticalRange;
			result["randomizeSeedOnPlay"] = component.agentRandomizeSeedOnPlay;
			result["randomSeed"] = component.agentRandomSeed;
			result["flockDecisionInterval"] = component.agentFlockDecisionInterval;
			result["flockAcceleration"] = component.agentFlockAcceleration;
			result["flockTurnRate"] = component.agentFlockTurnRate;
			result["memberCenterFollow"] = component.agentMemberCenterFollow;
			result["memberJitterStrength"] = component.agentMemberJitterStrength;
			result["memberJitterFrequency"] = component.agentMemberJitterFrequency;
			result["memberJitterUpdateInterval"] =
				component.agentMemberJitterUpdateInterval;
			result["memberJitterFollowSpeed"] =
				component.agentMemberJitterFollowSpeed;
			result["memberSpeedVariation"] = component.agentMemberSpeedVariation;
			result["memberLeashDistance"] = component.agentMemberLeashDistance;
			result["memberLeashStrength"] = component.agentMemberLeashStrength;
			result["memberCatchupSpeed"] = component.agentMemberCatchupSpeed;
			result["memberSeparationUpdateInterval"] =
				component.agentMemberSeparationUpdateInterval;
			result["memberSeparationBlend"] =
				component.agentMemberSeparationBlend;
			result["boundsWeight"] = component.agentBoundsWeight;
			result["useTeamHeading"] = component.agentUseTeamHeading;
			result["teamHeadingFromAverage"] =
				component.agentTeamHeadingFromAverage;
			result["teamHeadingDirection"] =
				VectorToJson(component.agentTeamHeadingDirection);
			result["teamHeadingWeight"] =
				component.agentTeamHeadingWeight;
			result["teamHeadingFollowSpeed"] =
				component.agentTeamHeadingFollowSpeed;
			result["useTeamRotation"] = component.agentUseTeamRotation;
			result["teamRotationWeight"] =
				component.agentTeamRotationWeight;
			result["teamRotationFollowSpeed"] =
				component.agentTeamRotationFollowSpeed;
			result["alignForwardToVelocity"] =
				component.agentAlignForwardToVelocity;
			result["forwardAxis"] = component.agentForwardAxis;
			result["rotateAxisX"] = component.agentRotateAxisX;
			result["rotateAxisY"] = component.agentRotateAxisY;
			result["rotateAxisZ"] = component.agentRotateAxisZ;
			result["rotationFollowSpeed"] =
				component.agentRotationFollowSpeed;
			result["pitchFromVerticalVelocity"] =
				component.agentPitchFromVerticalVelocity;
			result["bankingStrength"] = component.agentBankingStrength;
			result["schooling"] = component.agentSchooling;
			result["schoolingUpdateInterval"] =
				component.agentSchoolingUpdateInterval;
			result["schoolingUpdateJitter"] =
				component.agentSchoolingUpdateJitter;
			result["neighborLimit"] = component.agentNeighborLimit;
			result["schoolingBlend"] = component.agentSchoolingBlend;
			result["separationRadius"] = component.agentSeparationRadius;
			result["alignmentRadius"] = component.agentAlignmentRadius;
			result["cohesionRadius"] = component.agentCohesionRadius;
			result["separationWeight"] = component.agentSeparationWeight;
			result["alignmentWeight"] = component.agentAlignmentWeight;
			result["cohesionWeight"] = component.agentCohesionWeight;
			result["attractorWeight"] = component.agentAttractorWeight;
			result["teamSettingsOverride"] =
				component.agentTeamSettingsOverride;
			result["visualColor"] = VectorToJson(component.agentVisualColor);
			result["enableLighting"] = component.agentEnableLighting;
		} else if (component.type == "AgentAttractor") {
			result["tag"] = component.attractorTag;
			result["targetBehaviorName"] =
				component.attractorTargetBehaviorName;
			result["targetProfileName"] =
				component.attractorTargetProfileName;
			result["radius"] = component.attractorRadius;
			result["strength"] = component.attractorStrength;
			result["visualColor"] =
				VectorToJson(component.attractorVisualColor);
		} else if (component.type == "WaterVolume") {
			result["halfSize"] = VectorToJson(component.waterHalfSize);
			result["offset"] = VectorToJson(component.waterOffset);
			result["surfaceEnabled"] = component.waterSurfaceEnabled;
			result["surfaceBaseColor"] =
				VectorToJson(component.waterSurfaceBaseColor);
			result["surfaceHighlightColor"] =
				VectorToJson(component.waterSurfaceHighlightColor);
			result["surfaceAlpha"] = component.waterSurfaceAlpha;
			result["surfaceWaveScale"] = component.waterSurfaceWaveScale;
			result["surfaceNormalStrength"] =
				component.waterSurfaceNormalStrength;
			result["surfaceFresnelPower"] =
				component.waterSurfaceFresnelPower;
			result["lightShaftEnabled"] =
				component.waterLightShaftEnabled;
			result["lightColor"] = VectorToJson(component.waterLightColor);
			result["lightDirection"] =
				VectorToJson(component.waterLightDirection);
			result["lightIntensity"] = component.waterLightIntensity;
			result["lightDensity"] = component.waterLightDensity;
			result["causticsIntensity"] =
				component.waterLightCausticsIntensity;
			result["causticsScale"] = component.waterLightCausticsScale;
			result["causticsSpeed"] = component.waterLightCausticsSpeed;
			result["breakupStrength"] =
				component.waterLightBreakupStrength;
			result["warpStrength"] = component.waterLightWarpStrength;
			result["noiseScale"] = component.waterLightNoiseScale;
			result["lightSampleCount"] = component.waterLightSampleCount;
			result["moveSpeedMultiplier"] =
				component.waterMoveSpeedMultiplier;
			result["gravityScale"] = component.waterGravityScale;
			result["drag"] = component.waterDrag;
			result["maxFallSpeed"] = component.waterMaxFallSpeed;
			result["swimUpSpeed"] = component.waterSwimUpSpeed;
		} else if (component.type == "EntityReference") {
			result["referenceName"] = component.entityReferenceName;
			result["target"] = {
				{ "sceneId", component.entityReferenceTarget.sceneId },
				{ "instanceKey", component.entityReferenceTarget.instanceKey },
				{ "entityId", component.entityReferenceTarget.entityId }
			};
		} else if (component.type == "SceneTransition") {
			result["targetSceneId"] =
				component.sceneTransitionTargetSceneId;
			result["triggerType"] = component.sceneTransitionTriggerType;
			result["triggerKey"] = component.sceneTransitionTriggerKey;
		} else if (component.type == "CameraPath") {
			result["targetCameraName"] = component.cameraPathTargetCameraName;
			result["triggerType"] = component.cameraPathTriggerType;
			result["triggerKey"] = component.cameraPathTriggerKey;
			result["enterDuration"] = component.cameraPathEnterDuration;
			result["exitDuration"] = component.cameraPathExitDuration;
			result["interpolation"] = component.cameraPathInterpolation;
			result["defaultEasing"] = component.cameraPathDefaultEasing;
			result["returnToPreviousCamera"] =
				component.cameraPathReturnToPreviousCamera;
			result["startFromCurrentCamera"] =
				component.cameraPathStartFromCurrentCamera;
			result["autoCollectChildPoints"] =
				component.cameraPathAutoCollectChildPoints;
		} else if (component.type == "CameraPathPoint") {
			result["durationToNext"] =
				component.cameraPathPointDurationToNext;
			result["easingToNext"] =
				component.cameraPathPointEasingToNext;
		}
		return result;
	}

	std::vector<SceneComponent> ComponentsFromJson(const json& source) {
		std::vector<SceneComponent> components;
		if (!source.is_array()) {
			return components;
		}
		for (const json& value : source) {
			SceneComponent component{};
			if (value.is_string()) {
				component.type = value.get<std::string>();
			} else if (value.is_object()) {
				component.type = value.value("type", std::string{});
				component.enabled = value.value("enabled", true);
				component.modelPath = value.value("modelPath", std::string{});
				component.meshCullMode = value.value(
					"cullMode",
					component.meshCullMode
				);
				component.meshEnvironmentReflectionOverride = value.value(
					"environmentReflectionOverride",
					component.meshEnvironmentReflectionOverride
				);
				component.meshEnvironmentReflectionIntensity = value.value(
					"environmentReflectionIntensity",
					component.meshEnvironmentReflectionIntensity
				);
				if (const auto overrides = value.find("materialOverrides");
					overrides != value.end() && overrides->is_array()) {
					for (const json& item : *overrides) {
						if (!item.is_object()) {
							continue;
						}
						SceneMeshMaterialOverride override{};
						override.materialName = item.value(
							"materialName", std::string{}
						);
						override.enabled = item.value("enabled", false);
						override.colorOverrideEnabled = item.value(
							"colorOverrideEnabled", false
						);
						override.texturePath = item.value(
							"texturePath", std::string{}
						);
						if (item.contains("color")) {
							override.color = JsonToVector(
								item.at("color"), override.color
							);
						}
						if (!override.materialName.empty()) {
							component.meshMaterialOverrides.push_back(
								std::move(override)
							);
						}
					}
				}
				component.environmentSkyboxEnabled = value.value(
					"skyboxEnabled",
					component.environmentSkyboxEnabled
				);
				component.environmentSkyboxPath = value.value(
					"skyboxPath",
					component.environmentSkyboxPath
				);
				component.environmentSkyboxIntensity = value.value(
					"skyboxIntensity",
					component.environmentSkyboxIntensity
				);
				component.environmentReflectionIntensity = value.value(
					"reflectionIntensity",
					component.environmentReflectionIntensity
				);
				component.texturePath = value.value("texturePath", std::string{});
				if (value.contains("size")) {
					component.spriteSize = JsonToVector(
						value.at("size"),
						component.spriteSize
					);
				}
				if (value.contains("anchor")) {
					component.spriteAnchor = JsonToVector(
						value.at("anchor"),
						component.spriteAnchor
					);
				}
				if (value.contains("color")) {
					component.spriteColor = JsonToVector(
						value.at("color"),
						component.spriteColor
					);
				}
				component.spriteFlipX = value.value("flipX", false);
				component.spriteFlipY = value.value("flipY", false);
				component.cameraIsMain = value.value("isMain", false);
				component.cameraFovY = value.value("fovY", component.cameraFovY);
				component.cameraNearClip = value.value(
					"nearClip",
					component.cameraNearClip
				);
				component.cameraFarClip = value.value(
					"farClip",
					component.cameraFarClip
				);
				component.cameraInvertYaw = value.value(
					"invertYaw",
					component.cameraInvertYaw
				);
				component.cameraInvertPitch = value.value(
					"invertPitch",
					component.cameraInvertPitch
				);
				if (component.type == "Light") {
					component.lightType = value.value(
						"lightType",
						component.lightType
					);
					if (value.contains("color")) {
						component.lightColor = JsonToVector(
							value.at("color"),
							component.lightColor
						);
					}
					component.lightColor.x = std::clamp(
						component.lightColor.x,
						0.0f,
						1.0f
					);
					component.lightColor.y = std::clamp(
						component.lightColor.y,
						0.0f,
						1.0f
					);
					component.lightColor.z = std::clamp(
						component.lightColor.z,
						0.0f,
						1.0f
					);
					component.lightColor.w = std::clamp(
						component.lightColor.w,
						0.0f,
						1.0f
					);
					component.lightIntensity = (std::max)(
						value.value("intensity", component.lightIntensity),
						0.0f
					);
					component.lightRange = (std::max)(
						value.value("range", component.lightRange),
						0.1f
					);
					component.lightDecay = (std::max)(
						value.value("decay", component.lightDecay),
						0.0f
					);
					component.lightSpotOuterAngle = std::clamp(
						value.value("outerAngle", component.lightSpotOuterAngle),
						1.0f,
						89.0f
					);
					component.lightSpotInnerAngle = std::clamp(
						value.value("innerAngle", component.lightSpotInnerAngle),
						0.0f,
						component.lightSpotOuterAngle
					);
					component.lightCastsShadow = value.value(
						"castsShadow",
						component.lightCastsShadow
					);
					if (value.contains("shadow") && value.at("shadow").is_object()) {
						const json& shadow = value.at("shadow");
						component.lightShadowBias = (std::max)(
							shadow.value("bias", component.lightShadowBias),
							0.0f
						);
						component.lightShadowNormalBias = (std::max)(
							shadow.value("normalBias", component.lightShadowNormalBias),
							0.0f
						);
						component.lightShadowStrength = std::clamp(
							shadow.value("strength", component.lightShadowStrength),
							0.0f,
							1.0f
						);
						component.lightShadowDistance = (std::max)(
							shadow.value("distance", component.lightShadowDistance),
							1.0f
						);
						component.lightShadowOrthographicSize = (std::max)(
							shadow.value(
								"orthographicSize",
								component.lightShadowOrthographicSize
							),
							1.0f
						);
						component.lightShadowNearClip = (std::max)(
							shadow.value("nearClip", component.lightShadowNearClip),
							0.001f
						);
						component.lightShadowFarClip = (std::max)(
							shadow.value("farClip", component.lightShadowFarClip),
							component.lightShadowNearClip + 0.001f
						);
						component.lightShadowTexelSnap = shadow.value(
							"texelSnap",
							component.lightShadowTexelSnap
						);
					}
					if (
						component.lightType != "Directional" &&
						component.lightType != "Point" &&
						component.lightType != "Spot"
					) {
						component.lightType = "Point";
					}
					if (component.lightType == "Point") {
						component.lightCastsShadow = false;
					}
				}
				component.monitorCameraName = value.value(
					"cameraName",
					component.monitorCameraName
				);
				component.monitorCameraEntityId = value.value(
					"cameraEntityId",
					component.monitorCameraEntityId
				);
				component.monitorResolutionPreset = value.value(
					"resolutionPreset",
					component.monitorResolutionPreset
				);
				component.monitorWidth = value.value(
					"width",
					component.monitorWidth
				);
				component.monitorHeight = value.value(
					"height",
					component.monitorHeight
				);
				component.monitorHideSelf = value.value(
					"hideSelf",
					component.monitorHideSelf
				);
				component.thirdPersonDistance = value.value(
					"distance",
					component.thirdPersonDistance
				);
				component.thirdPersonAimDistance = value.value(
					"aimDistance",
					component.thirdPersonAimDistance
				);
				if (value.contains("targetOffset")) {
					component.thirdPersonTargetOffset = JsonToVector(
						value.at("targetOffset"),
						component.thirdPersonTargetOffset
					);
				}
				if (value.contains("aimTargetOffset")) {
					component.thirdPersonAimTargetOffset = JsonToVector(
						value.at("aimTargetOffset"),
						component.thirdPersonAimTargetOffset
					);
				}
				component.thirdPersonMouseSensitivity = value.value(
					"mouseSensitivity",
					component.thirdPersonMouseSensitivity
				);
				component.thirdPersonMinPitch = value.value(
					"minPitch",
					component.thirdPersonMinPitch
				);
				component.thirdPersonMaxPitch = value.value(
					"maxPitch",
					component.thirdPersonMaxPitch
				);
				component.thirdPersonOcclusionMargin = value.value(
					"occlusionMargin",
					component.thirdPersonOcclusionMargin
				);
				component.thirdPersonInvertYaw = value.value(
					"invertYaw",
					component.thirdPersonInvertYaw
				);
				component.thirdPersonInvertPitch = value.value(
					"invertPitch",
					component.thirdPersonInvertPitch
				);
				if (component.type == "Animator") {
					component.animatorPlayOnStart = value.value(
						"playOnStart",
						component.animatorPlayOnStart
					);
					component.animatorLoop = value.value(
						"loop",
						component.animatorLoop
					);
					component.animatorSpeed = value.value(
						"speed",
						component.animatorSpeed
					);
					component.animatorDefaultClip = (std::max)(
						value.value(
							"defaultClip",
							component.animatorDefaultClip
						),
						0
					);
					component.animatorTransitionDuration = (std::max)(
						value.value(
							"transitionDuration",
							component.animatorTransitionDuration
						),
						0.0f
					);
					component.animatorBlendCurve = value.value(
						"blendCurve",
						component.animatorBlendCurve
					);
					if (component.animatorBlendCurve != "Linear") {
						component.animatorBlendCurve = "SmoothStep";
					}
				}
				component.physicsBodyType = value.value(
					"bodyType",
					component.physicsBodyType
				);
				component.physicsMass = value.value(
					"mass",
					component.physicsMass
				);
				component.physicsUseGravity = value.value(
					"useGravity",
					component.physicsUseGravity
				);
				component.physicsGravityScale = value.value(
					"gravityScale",
					component.physicsGravityScale
				);
				component.physicsDrag = value.value(
					"drag",
					component.physicsDrag
				);
				component.physicsRestitution = value.value(
					"restitution",
					component.physicsRestitution
				);
				component.physicsFriction = value.value(
					"friction",
					component.physicsFriction
				);
				component.physicsMaxFallSpeed = value.value(
					"maxFallSpeed",
					component.physicsMaxFallSpeed
				);
				if (value.contains("velocity")) {
					component.physicsVelocity = JsonToVector(
						value.at("velocity"),
						component.physicsVelocity
					);
				}
				component.physicsFreezePositionX = value.value(
					"freezePositionX",
					component.physicsFreezePositionX
				);
				component.physicsFreezePositionY = value.value(
					"freezePositionY",
					component.physicsFreezePositionY
				);
				component.physicsFreezePositionZ = value.value(
					"freezePositionZ",
					component.physicsFreezePositionZ
				);
				if (component.type == "OBBCollider") {
					if (value.contains("offset")) {
						component.colliderOffset = JsonToVector(
							value.at("offset"),
							component.colliderOffset
						);
					}
					if (value.contains("sizeMultiplier")) {
						component.colliderSizeMultiplier = JsonToVector(
							value.at("sizeMultiplier"),
							component.colliderSizeMultiplier
						);
					}
					if (value.contains("debugColor")) {
						component.colliderDebugColor = JsonToVector(
							value.at("debugColor"),
							component.colliderDebugColor
						);
					}
					component.colliderShape = value.value(
						"shape",
						component.colliderShape
					);
					component.colliderSphereRadius = value.value(
						"sphereRadius",
						component.colliderSphereRadius
					);
					component.colliderDebugVisible = value.value(
						"debugVisible",
						component.colliderDebugVisible
					);
					component.colliderDebugDrawMode = value.value(
						"debugDrawMode",
						component.colliderDebugDrawMode
					);
					component.colliderDebugSegments = value.value(
						"debugSegments",
						component.colliderDebugSegments
					);
					if (component.colliderShape != "Sphere") {
						component.colliderShape = "Box";
					}
					if (
						component.colliderDebugDrawMode != "Solid" &&
						component.colliderDebugDrawMode != "WireframeAndSolid"
					) {
						component.colliderDebugDrawMode = "Wireframe";
					}
					component.colliderSphereRadius = (std::max)(
						component.colliderSphereRadius,
						0.001f
					);
					component.colliderDebugSegments = std::clamp(
						component.colliderDebugSegments,
						4,
						64
					);
				}
				component.playerMoveSpeed = value.value(
					"moveSpeed",
					component.playerMoveSpeed
				);
				component.playerJumpVelocity = value.value(
					"jumpVelocity",
					component.playerJumpVelocity
				);
				component.playerTurnResponsiveness = value.value(
					"turnResponsiveness",
					component.playerTurnResponsiveness
				);
				component.playerDashMultiplier = value.value(
					"dashMultiplier",
					component.playerDashMultiplier
				);
				component.playerCameraRelativeMove = value.value(
					"cameraRelativeMove",
					component.playerCameraRelativeMove
				);
				component.playerAllowJump = value.value(
					"allowJump",
					component.playerAllowJump
				);
				component.agentBehaviorName = value.value(
					"behaviorName",
					component.agentBehaviorName
				);
				component.agentProfileName = value.value(
					"profileName",
					component.agentProfileName
				);
				component.agentGroupName = value.value(
					"groupName",
					component.agentGroupName
				);
				component.agentBoundsEntityId = value.value(
					"boundsEntityId",
					component.agentBoundsEntityId
				);
				component.agentBoundsName = value.value(
					"boundsName",
					component.agentBoundsName
				);
				component.agentAttractorEntityId = value.value(
					"attractorEntityId",
					component.agentAttractorEntityId
				);
				component.agentAttractorTag = value.value(
					"attractorTag",
					component.agentAttractorTag
				);
				component.agentUseWaterBounds = value.value(
					"useWaterBounds",
					component.agentUseWaterBounds
				);
				component.agentMinSpeed = value.value(
					"minSpeed",
					component.agentMinSpeed
				);
				component.agentMaxSpeed = value.value(
					"maxSpeed",
					component.agentMaxSpeed
				);
				component.agentTurnSpeed = value.value(
					"turnSpeed",
					component.agentTurnSpeed
				);
				component.agentWanderStrength = value.value(
					"wanderStrength",
					component.agentWanderStrength
				);
				component.agentWanderChangeInterval = value.value(
					"wanderChangeInterval",
					component.agentWanderChangeInterval
				);
				component.agentWanderDirectionRange = value.value(
					"wanderDirectionRange",
					component.agentWanderDirectionRange
				);
				component.agentWanderVerticalRange = value.value(
					"wanderVerticalRange",
					component.agentWanderVerticalRange
				);
				component.agentRandomizeSeedOnPlay = value.value(
					"randomizeSeedOnPlay",
					component.agentRandomizeSeedOnPlay
				);
				component.agentRandomSeed = value.value(
					"randomSeed",
					component.agentRandomSeed
				);
				component.agentFlockDecisionInterval = value.value(
					"flockDecisionInterval",
					component.agentFlockDecisionInterval
				);
				component.agentFlockAcceleration = value.value(
					"flockAcceleration",
					component.agentFlockAcceleration
				);
				component.agentFlockTurnRate = value.value(
					"flockTurnRate",
					component.agentFlockTurnRate
				);
				component.agentMemberCenterFollow = value.value(
					"memberCenterFollow",
					component.agentMemberCenterFollow
				);
				component.agentMemberJitterStrength = value.value(
					"memberJitterStrength",
					component.agentMemberJitterStrength
				);
				component.agentMemberJitterFrequency = value.value(
					"memberJitterFrequency",
					component.agentMemberJitterFrequency
				);
				component.agentMemberJitterUpdateInterval = value.value(
					"memberJitterUpdateInterval",
					component.agentMemberJitterUpdateInterval
				);
				component.agentMemberJitterFollowSpeed = value.value(
					"memberJitterFollowSpeed",
					component.agentMemberJitterFollowSpeed
				);
				component.agentMemberSpeedVariation = value.value(
					"memberSpeedVariation",
					component.agentMemberSpeedVariation
				);
				component.agentMemberLeashDistance = value.value(
					"memberLeashDistance",
					component.agentMemberLeashDistance
				);
				component.agentMemberLeashStrength = value.value(
					"memberLeashStrength",
					component.agentMemberLeashStrength
				);
				component.agentMemberCatchupSpeed = value.value(
					"memberCatchupSpeed",
					component.agentMemberCatchupSpeed
				);
				component.agentMemberSeparationUpdateInterval = value.value(
					"memberSeparationUpdateInterval",
					component.agentMemberSeparationUpdateInterval
				);
				component.agentMemberSeparationBlend = value.value(
					"memberSeparationBlend",
					component.agentMemberSeparationBlend
				);
				component.agentBoundsWeight = value.value(
					"boundsWeight",
					component.agentBoundsWeight
				);
				component.agentUseTeamHeading = value.value(
					"useTeamHeading",
					component.agentUseTeamHeading
				);
				component.agentTeamHeadingFromAverage = value.value(
					"teamHeadingFromAverage",
					component.agentTeamHeadingFromAverage
				);
				if (value.contains("teamHeadingDirection")) {
					component.agentTeamHeadingDirection = JsonToVector(
						value.at("teamHeadingDirection"),
						component.agentTeamHeadingDirection
					);
				}
				component.agentTeamHeadingWeight = value.value(
					"teamHeadingWeight",
					component.agentTeamHeadingWeight
				);
				component.agentTeamHeadingFollowSpeed = value.value(
					"teamHeadingFollowSpeed",
					component.agentTeamHeadingFollowSpeed
				);
				component.agentUseTeamRotation = value.value(
					"useTeamRotation",
					component.agentUseTeamRotation
				);
				component.agentTeamRotationWeight = value.value(
					"teamRotationWeight",
					component.agentTeamRotationWeight
				);
				component.agentTeamRotationFollowSpeed = value.value(
					"teamRotationFollowSpeed",
					component.agentTeamRotationFollowSpeed
				);
				component.agentAlignForwardToVelocity = value.value(
					"alignForwardToVelocity",
					component.agentAlignForwardToVelocity
				);
				component.agentForwardAxis = value.value(
					"forwardAxis",
					component.agentForwardAxis
				);
				component.agentRotateAxisX = value.value(
					"rotateAxisX",
					component.agentRotateAxisX
				);
				component.agentRotateAxisY = value.value(
					"rotateAxisY",
					component.agentRotateAxisY
				);
				component.agentRotateAxisZ = value.value(
					"rotateAxisZ",
					component.agentRotateAxisZ
				);
				component.agentRotationFollowSpeed = value.value(
					"rotationFollowSpeed",
					component.agentRotationFollowSpeed
				);
				component.agentPitchFromVerticalVelocity = value.value(
					"pitchFromVerticalVelocity",
					component.agentPitchFromVerticalVelocity
				);
				component.agentBankingStrength = value.value(
					"bankingStrength",
					component.agentBankingStrength
				);
				component.agentSchooling = value.value(
					"schooling",
					component.agentSchooling
				);
				component.agentSchoolingUpdateInterval = value.value(
					"schoolingUpdateInterval",
					component.agentSchoolingUpdateInterval
				);
				component.agentSchoolingUpdateJitter = value.value(
					"schoolingUpdateJitter",
					component.agentSchoolingUpdateJitter
				);
				component.agentNeighborLimit = value.value(
					"neighborLimit",
					component.agentNeighborLimit
				);
				component.agentSchoolingBlend = value.value(
					"schoolingBlend",
					component.agentSchoolingBlend
				);
				component.agentSeparationRadius = value.value(
					"separationRadius",
					component.agentSeparationRadius
				);
				component.agentAlignmentRadius = value.value(
					"alignmentRadius",
					component.agentAlignmentRadius
				);
				component.agentCohesionRadius = value.value(
					"cohesionRadius",
					component.agentCohesionRadius
				);
				component.agentSeparationWeight = value.value(
					"separationWeight",
					component.agentSeparationWeight
				);
				component.agentAlignmentWeight = value.value(
					"alignmentWeight",
					component.agentAlignmentWeight
				);
				component.agentCohesionWeight = value.value(
					"cohesionWeight",
					component.agentCohesionWeight
				);
				component.agentAttractorWeight = value.value(
					"attractorWeight",
					component.agentAttractorWeight
				);
				component.agentTeamSettingsOverride = value.value(
					"teamSettingsOverride",
					component.agentTeamSettingsOverride
				);
				if (value.contains("visualColor")) {
					component.agentVisualColor = JsonToVector(
						value.at("visualColor"),
						component.agentVisualColor
					);
					component.attractorVisualColor = JsonToVector(
						value.at("visualColor"),
						component.attractorVisualColor
					);
				}
				component.agentEnableLighting = value.value(
					"enableLighting",
					component.agentEnableLighting
				);
				component.attractorTag = value.value(
					"tag",
					component.attractorTag
				);
				component.attractorTargetBehaviorName = value.value(
					"targetBehaviorName",
					component.attractorTargetBehaviorName
				);
				component.attractorTargetProfileName = value.value(
					"targetProfileName",
					component.attractorTargetProfileName
				);
				component.attractorRadius = value.value(
					"radius",
					component.attractorRadius
				);
				component.attractorStrength = value.value(
					"strength",
					component.attractorStrength
				);
				if (value.contains("halfSize")) {
					component.waterHalfSize = JsonToVector(
						value.at("halfSize"),
						component.waterHalfSize
					);
				}
				if (value.contains("offset")) {
					component.waterOffset = JsonToVector(
						value.at("offset"),
						component.waterOffset
					);
				}
				component.waterSurfaceEnabled = value.value(
					"surfaceEnabled",
					component.waterSurfaceEnabled
				);
				if (value.contains("surfaceBaseColor")) {
					component.waterSurfaceBaseColor = JsonToVector(
						value.at("surfaceBaseColor"),
						component.waterSurfaceBaseColor
					);
				}
				if (value.contains("surfaceHighlightColor")) {
					component.waterSurfaceHighlightColor = JsonToVector(
						value.at("surfaceHighlightColor"),
						component.waterSurfaceHighlightColor
					);
				}
				component.waterSurfaceAlpha = value.value(
					"surfaceAlpha",
					component.waterSurfaceAlpha
				);
				component.waterSurfaceWaveScale = value.value(
					"surfaceWaveScale",
					component.waterSurfaceWaveScale
				);
				component.waterSurfaceNormalStrength = value.value(
					"surfaceNormalStrength",
					component.waterSurfaceNormalStrength
				);
				component.waterSurfaceFresnelPower = value.value(
					"surfaceFresnelPower",
					component.waterSurfaceFresnelPower
				);
				component.waterLightShaftEnabled = value.value(
					"lightShaftEnabled",
					component.waterLightShaftEnabled
				);
				if (value.contains("lightColor")) {
					component.waterLightColor = JsonToVector(
						value.at("lightColor"),
						component.waterLightColor
					);
				}
				if (value.contains("lightDirection")) {
					component.waterLightDirection = JsonToVector(
						value.at("lightDirection"),
						component.waterLightDirection
					);
				}
				component.waterLightIntensity = value.value(
					"lightIntensity",
					component.waterLightIntensity
				);
				component.waterLightDensity = value.value(
					"lightDensity",
					component.waterLightDensity
				);
				component.waterLightCausticsIntensity = value.value(
					"causticsIntensity",
					component.waterLightCausticsIntensity
				);
				component.waterLightCausticsScale = value.value(
					"causticsScale",
					component.waterLightCausticsScale
				);
				component.waterLightCausticsSpeed = value.value(
					"causticsSpeed",
					component.waterLightCausticsSpeed
				);
				component.waterLightBreakupStrength = value.value(
					"breakupStrength",
					component.waterLightBreakupStrength
				);
				component.waterLightWarpStrength = value.value(
					"warpStrength",
					component.waterLightWarpStrength
				);
				component.waterLightNoiseScale = value.value(
					"noiseScale",
					component.waterLightNoiseScale
				);
				component.waterLightSampleCount = value.value(
					"lightSampleCount",
					component.waterLightSampleCount
				);
				component.waterMoveSpeedMultiplier = value.value(
					"moveSpeedMultiplier",
					component.waterMoveSpeedMultiplier
				);
				component.waterGravityScale = value.value(
					"gravityScale",
					component.waterGravityScale
				);
				component.waterDrag = value.value(
					"drag",
					component.waterDrag
				);
				component.waterMaxFallSpeed = value.value(
					"maxFallSpeed",
					component.waterMaxFallSpeed
				);
				component.waterSwimUpSpeed = value.value(
					"swimUpSpeed",
					component.waterSwimUpSpeed
				);
				component.entityReferenceName = value.value(
					"referenceName",
					component.entityReferenceName
				);
				if (
					const auto target = value.find("target");
					target != value.end() && target->is_object()
				) {
					component.entityReferenceTarget.sceneId = target->value(
						"sceneId",
						component.entityReferenceTarget.sceneId
					);
					component.entityReferenceTarget.instanceKey = target->value(
						"instanceKey",
						component.entityReferenceTarget.instanceKey
					);
					component.entityReferenceTarget.entityId = target->value(
						"entityId",
						component.entityReferenceTarget.entityId
					);
				}
				component.sceneTransitionTargetSceneId = value.value(
					"targetSceneId",
					component.sceneTransitionTargetSceneId
				);
				component.sceneTransitionTriggerType = value.value(
					"triggerType",
					component.sceneTransitionTriggerType
				);
				component.sceneTransitionTriggerKey = value.value(
					"triggerKey",
					component.sceneTransitionTriggerKey
				);
				component.cameraPathTargetCameraName = value.value(
					"targetCameraName",
					component.cameraPathTargetCameraName
				);
				component.cameraPathTriggerType = value.value(
					"triggerType",
					component.cameraPathTriggerType
				);
				component.cameraPathTriggerKey = value.value(
					"triggerKey",
					component.cameraPathTriggerKey
				);
				component.cameraPathEnterDuration = value.value(
					"enterDuration",
					component.cameraPathEnterDuration
				);
				component.cameraPathExitDuration = value.value(
					"exitDuration",
					component.cameraPathExitDuration
				);
				component.cameraPathInterpolation = value.value(
					"interpolation",
					component.cameraPathInterpolation
				);
				component.cameraPathDefaultEasing = value.value(
					"defaultEasing",
					component.cameraPathDefaultEasing
				);
				component.cameraPathReturnToPreviousCamera = value.value(
					"returnToPreviousCamera",
					component.cameraPathReturnToPreviousCamera
				);
				component.cameraPathStartFromCurrentCamera = value.value(
					"startFromCurrentCamera",
					component.cameraPathStartFromCurrentCamera
				);
				component.cameraPathAutoCollectChildPoints = value.value(
					"autoCollectChildPoints",
					component.cameraPathAutoCollectChildPoints
				);
				component.cameraPathPointDurationToNext = value.value(
					"durationToNext",
					component.cameraPathPointDurationToNext
				);
				component.cameraPathPointEasingToNext = value.value(
					"easingToNext",
					component.cameraPathPointEasingToNext
				);
			}
			if (!component.type.empty()) {
				if (component.type == "Environment") {
					if (component.environmentSkyboxPath.empty()) {
						component.environmentSkyboxPath =
							"resources/rostock_laage_airport_4k.dds";
					}
					component.environmentSkyboxIntensity =
						(std::max)(0.0f, component.environmentSkyboxIntensity);
					component.environmentReflectionIntensity = std::clamp(
						component.environmentReflectionIntensity,
						0.0f,
						1.0f
					);
				} else if (component.type == "SceneTransition") {
					if (component.sceneTransitionTriggerType != "Key") {
						component.sceneTransitionTriggerType = "Key";
					}
					if (component.sceneTransitionTriggerKey.empty()) {
						component.sceneTransitionTriggerKey = "ENTER";
					}
				} else if (component.type == "AgentBehavior") {
					if (component.agentBehaviorName.empty()) {
						component.agentBehaviorName = "Agent";
					}
					if (component.agentProfileName.empty()) {
						component.agentProfileName = "Default";
					}
					component.agentMinSpeed =
						(std::max)(component.agentMinSpeed, 0.0f);
					component.agentMaxSpeed =
						(std::max)(component.agentMaxSpeed, component.agentMinSpeed);
					component.agentTurnSpeed =
						(std::max)(component.agentTurnSpeed, 0.0f);
					component.agentWanderStrength =
						(std::max)(component.agentWanderStrength, 0.0f);
					component.agentWanderChangeInterval =
						(std::max)(component.agentWanderChangeInterval, 0.0f);
					component.agentWanderDirectionRange = std::clamp(
						component.agentWanderDirectionRange,
						0.0f,
						3.14159265359f
					);
					component.agentWanderVerticalRange = std::clamp(
						component.agentWanderVerticalRange,
						0.0f,
						1.0f
					);
					component.agentRandomSeed =
						(std::max)(component.agentRandomSeed, 0);
					component.agentFlockDecisionInterval =
						(std::max)(component.agentFlockDecisionInterval, 0.0f);
					component.agentFlockAcceleration =
						(std::max)(component.agentFlockAcceleration, 0.0f);
					component.agentFlockTurnRate =
						(std::max)(component.agentFlockTurnRate, 0.0f);
					component.agentMemberCenterFollow =
						(std::max)(component.agentMemberCenterFollow, 0.0f);
					component.agentMemberJitterStrength =
						(std::max)(component.agentMemberJitterStrength, 0.0f);
					component.agentMemberJitterFrequency =
						(std::max)(component.agentMemberJitterFrequency, 0.0f);
					component.agentMemberJitterUpdateInterval =
						(std::max)(component.agentMemberJitterUpdateInterval, 0.0f);
					component.agentMemberJitterFollowSpeed =
						(std::max)(component.agentMemberJitterFollowSpeed, 0.0f);
					component.agentMemberSpeedVariation = std::clamp(
						component.agentMemberSpeedVariation,
						0.0f,
						1.0f
					);
					component.agentMemberLeashDistance =
						(std::max)(component.agentMemberLeashDistance, 0.0f);
					component.agentMemberLeashStrength =
						(std::max)(component.agentMemberLeashStrength, 0.0f);
					component.agentMemberCatchupSpeed =
						(std::max)(component.agentMemberCatchupSpeed, 0.0f);
					component.agentMemberSeparationUpdateInterval =
						(std::max)(
							component.agentMemberSeparationUpdateInterval,
							0.0f
						);
					component.agentMemberSeparationBlend = std::clamp(
						component.agentMemberSeparationBlend,
						0.0f,
						1.0f
					);
					component.agentBoundsWeight =
						(std::max)(component.agentBoundsWeight, 0.0f);
					component.agentTeamHeadingDirection =
						NormalizeDirectionVector(
							component.agentTeamHeadingDirection,
							{ 0.0f, 0.0f, 1.0f }
						);
					component.agentTeamHeadingWeight =
						(std::max)(component.agentTeamHeadingWeight, 0.0f);
					component.agentTeamHeadingFollowSpeed =
						(std::max)(
							component.agentTeamHeadingFollowSpeed,
							0.0f
						);
					component.agentTeamRotationWeight =
						std::clamp(
							component.agentTeamRotationWeight,
							0.0f,
							1.0f
						);
					component.agentTeamRotationFollowSpeed =
						(std::max)(
							component.agentTeamRotationFollowSpeed,
							0.0f
						);
					if (
						component.agentForwardAxis != "+Z" &&
						component.agentForwardAxis != "-Z" &&
						component.agentForwardAxis != "+X" &&
						component.agentForwardAxis != "-X" &&
						component.agentForwardAxis != "+Y" &&
						component.agentForwardAxis != "-Y"
					) {
						component.agentForwardAxis = "+Z";
					}
					component.agentRotationFollowSpeed =
						(std::max)(component.agentRotationFollowSpeed, 0.0f);
					component.agentPitchFromVerticalVelocity =
						(std::max)(
							component.agentPitchFromVerticalVelocity,
							0.0f
						);
					component.agentBankingStrength =
						(std::max)(component.agentBankingStrength, 0.0f);
					component.agentSchoolingUpdateInterval =
						(std::max)(
							component.agentSchoolingUpdateInterval,
							0.0f
						);
					component.agentSchoolingUpdateJitter =
						(std::max)(component.agentSchoolingUpdateJitter, 0.0f);
					component.agentNeighborLimit =
						(std::max)(component.agentNeighborLimit, 0);
					component.agentSchoolingBlend =
						std::clamp(component.agentSchoolingBlend, 0.0f, 1.0f);
					component.agentSeparationRadius =
						(std::max)(component.agentSeparationRadius, 0.0f);
					component.agentAlignmentRadius =
						(std::max)(component.agentAlignmentRadius, 0.0f);
					component.agentCohesionRadius =
						(std::max)(component.agentCohesionRadius, 0.0f);
					component.agentSeparationWeight =
						(std::max)(component.agentSeparationWeight, 0.0f);
					component.agentAlignmentWeight =
						(std::max)(component.agentAlignmentWeight, 0.0f);
					component.agentCohesionWeight =
						(std::max)(component.agentCohesionWeight, 0.0f);
					component.agentAttractorWeight =
						(std::max)(component.agentAttractorWeight, 0.0f);
				} else if (component.type == "AgentAttractor") {
					if (component.attractorTag.empty()) {
						component.attractorTag = "Default";
					}
					component.attractorRadius =
						(std::max)(component.attractorRadius, 0.0f);
					component.attractorStrength =
						(std::max)(component.attractorStrength, 0.0f);
				}
				const auto duplicate = std::find_if(
					components.begin(),
					components.end(),
					[&component](const SceneComponent& existing) {
						return existing.type == component.type;
					}
				);
				if (duplicate == components.end()) {
					components.push_back(std::move(component));
				}
			}
		}
		return components;
	}

	void SynchronizeLegacyRendererFields(SceneEntity& entity) {
		if (SceneComponent* meshRenderer = FindComponent(entity, "MeshRenderer")) {
			if (meshRenderer->modelPath.empty()) {
				meshRenderer->modelPath = entity.modelPath;
			}
			entity.modelPath = meshRenderer->modelPath;
		}
		if (SceneComponent* spriteRenderer = FindComponent(entity, "SpriteRenderer")) {
			if (spriteRenderer->texturePath.empty() && !entity.spriteTexturePath.empty()) {
				spriteRenderer->texturePath = entity.spriteTexturePath;
				spriteRenderer->spriteSize = entity.spriteSize;
				spriteRenderer->spriteAnchor = entity.spriteAnchor;
				spriteRenderer->spriteColor = entity.spriteColor;
				spriteRenderer->spriteFlipX = entity.spriteFlipX;
				spriteRenderer->spriteFlipY = entity.spriteFlipY;
			}
			entity.spriteTexturePath = spriteRenderer->texturePath;
			entity.spriteSize = spriteRenderer->spriteSize;
			entity.spriteAnchor = spriteRenderer->spriteAnchor;
			entity.spriteColor = spriteRenderer->spriteColor;
			entity.spriteFlipX = spriteRenderer->spriteFlipX;
			entity.spriteFlipY = spriteRenderer->spriteFlipY;
		}
	}

}

void SceneDocument::Clear(const std::string& sceneName) {
	sceneName_ = sceneName;
	entities_.clear();
	teams_.clear();
	lightingSettings_ = {};
	postProcessSettings_ = {};
	debugSettings_ = {};
	nextId_ = 1;
	dirty_ = false;
	revision_ = 0;
	lastLoadError_.clear();
}

bool SceneDocument::Load(const std::string& filePath) {
	lastLoadError_.clear();
	if (LoadInternal(filePath)) {
		return true;
	}

	const std::string primaryError = lastLoadError_;
	const std::string backupPath = filePath + ".bak";
	if (!LoadInternal(backupPath)) {
		lastLoadError_ =
			"Primary: " + primaryError + " | Backup: " + lastLoadError_;
		return false;
	}

	MarkDirty();
	lastLoadError_ = "Recovered from backup: " + backupPath;
	return true;
}

bool SceneDocument::Save(const std::string& filePath) {
	json root;
	root["version"] = SceneDocumentMigrator::kCurrentVersion;
	root["sceneName"] = sceneName_;
	root["lighting"] = LightingSettingsToJson(lightingSettings_);
	root["postProcess"] = PostProcessToJson(postProcessSettings_);
	root["debug"] = DebugSettingsToJson(debugSettings_);
	root["teams"] = json::array();
	for (const SceneTeamSettings& team : teams_) {
		root["teams"].push_back(TeamToJson(team));
	}
	root["entities"] = json::array();

	for (const SceneEntity& entity : entities_) {
		json components = json::array();
		for (const SceneComponent& component : entity.components) {
			components.push_back(ComponentToJson(component));
		}
		const SceneComponent* meshRenderer = FindComponent(entity, "MeshRenderer");
		const SceneComponent* spriteRenderer = FindComponent(entity, "SpriteRenderer");
		const std::string modelPath = meshRenderer
			? meshRenderer->modelPath
			: entity.modelPath;
		const std::string spriteTexturePath = spriteRenderer
			? spriteRenderer->texturePath
			: entity.spriteTexturePath;
		const Vector2 spriteSize = spriteRenderer
			? spriteRenderer->spriteSize
			: entity.spriteSize;
		const Vector2 spriteAnchor = spriteRenderer
			? spriteRenderer->spriteAnchor
			: entity.spriteAnchor;
		const Vector4 spriteColor = spriteRenderer
			? spriteRenderer->spriteColor
			: entity.spriteColor;
		const bool spriteFlipX = spriteRenderer
			? spriteRenderer->spriteFlipX
			: entity.spriteFlipX;
		const bool spriteFlipY = spriteRenderer
			? spriteRenderer->spriteFlipY
			: entity.spriteFlipY;
		root["entities"].push_back({
			{ "id", entity.id },
			{ "parentId", entity.parentId },
			{ "name", entity.name },
			{ "folder", entity.folder },
			{ "folderTeamEnabled", entity.folderTeamEnabled },
			{ "active", entity.active },
			{ "locked", entity.locked },
			{ "team", entity.teamName },
			{ "transform", {
				{ "scale", VectorToJson(entity.transform.scale) },
				{ "rotation", QuaternionToJson(entity.transform.rotate) },
				{ "translate", VectorToJson(entity.transform.translate) }
			} },
			{ "modelPath", modelPath },
			{ "sprite", {
				{ "texturePath", spriteTexturePath },
				{ "size", VectorToJson(spriteSize) },
				{ "anchor", VectorToJson(spriteAnchor) },
				{ "color", VectorToJson(spriteColor) },
				{ "flipX", spriteFlipX },
				{ "flipY", spriteFlipY }
			} },
			{ "components", components }
		});
	}

	const std::filesystem::path target = StringUtility::ToPath(filePath);
	std::filesystem::path temporary = target;
	temporary += L".tmp";
	std::filesystem::path backup = target;
	backup += L".bak";
	std::error_code error;
	if (!target.parent_path().empty()) {
		std::filesystem::create_directories(target.parent_path(), error);
		if (error) {
			return false;
		}
	}

	{
		std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
		if (!output.is_open()) {
			return false;
		}
		output << root.dump(2);
		output.flush();
		if (!output.good()) {
			output.close();
			std::filesystem::remove(temporary, error);
			return false;
		}
	}

	if (std::filesystem::exists(target, error) && !error) {
		std::filesystem::copy_file(
			target,
			backup,
			std::filesystem::copy_options::overwrite_existing,
			error
		);
		if (error) {
			std::filesystem::remove(temporary, error);
			return false;
		}
	}

	if (!MoveFileExW(
		temporary.c_str(),
		target.c_str(),
		MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH
	)) {
		std::filesystem::remove(temporary, error);
		return false;
	}

	dirty_ = false;
	return true;
}

SceneEntity& SceneDocument::CreateEntity(
	const std::string& name,
	uint64_t parentId
) {
	SceneEntity entity{};
	entity.id = nextId_++;
	entity.parentId = FindEntity(parentId) ? parentId : 0;
	entity.name = name.empty() ? "Entity" : name;
	entities_.push_back(entity);
	MarkDirty();
	return entities_.back();
}

bool SceneDocument::RemoveEntity(uint64_t id) {
	if (!FindEntity(id)) {
		return false;
	}

	std::unordered_set<uint64_t> removeIds{ id };
	bool foundChild = true;
	while (foundChild) {
		foundChild = false;
		for (const SceneEntity& entity : entities_) {
			if (
				removeIds.contains(entity.parentId) &&
				!removeIds.contains(entity.id)
			) {
				removeIds.insert(entity.id);
				foundChild = true;
			}
		}
	}

	const auto oldSize = entities_.size();
	entities_.erase(
		std::remove_if(
			entities_.begin(),
			entities_.end(),
			[&removeIds](const SceneEntity& entity) {
				return removeIds.contains(entity.id);
			}
		),
		entities_.end()
	);
	if (entities_.size() == oldSize) {
		return false;
	}
	MarkDirty();
	return true;
}

uint64_t SceneDocument::DuplicateEntity(uint64_t id) {
	const SceneEntity* source = FindEntity(id);
	if (!source) {
		return 0;
	}

	const std::vector<SceneEntity> sourceEntities = entities_;
	std::function<uint64_t(uint64_t, uint64_t, bool)> duplicateBranch;
	duplicateBranch = [this, &sourceEntities, &duplicateBranch](
		uint64_t sourceId,
		uint64_t newParentId,
		bool isRoot
	) -> uint64_t {
		const auto found = std::find_if(
			sourceEntities.begin(),
			sourceEntities.end(),
			[sourceId](const SceneEntity& entity) {
				return entity.id == sourceId;
			}
		);
		if (found == sourceEntities.end()) {
			return 0;
		}

		SceneEntity& duplicate = CreateEntity(
			isRoot ? found->name + " Copy" : found->name,
			newParentId
		);
		const uint64_t duplicateId = duplicate.id;
		duplicate.folder = found->folder;
		duplicate.folderTeamEnabled = found->folderTeamEnabled;
		duplicate.active = found->active;
		duplicate.locked = found->locked;
		duplicate.teamName = found->teamName;
		duplicate.transform = found->transform;
		duplicate.modelPath = found->modelPath;
		duplicate.spriteTexturePath = found->spriteTexturePath;
		duplicate.spriteSize = found->spriteSize;
		duplicate.spriteAnchor = found->spriteAnchor;
		duplicate.spriteColor = found->spriteColor;
		duplicate.spriteFlipX = found->spriteFlipX;
		duplicate.spriteFlipY = found->spriteFlipY;
		duplicate.components = found->components;
		for (const SceneEntity& child : sourceEntities) {
			if (child.parentId == sourceId) {
				duplicateBranch(child.id, duplicateId, false);
			}
		}
		return duplicateId;
	};

	return duplicateBranch(id, source->parentId, true);
}

bool SceneDocument::SetParent(uint64_t id, uint64_t parentId) {
	SceneEntity* entity = FindEntity(id);
	if (!entity || id == parentId) {
		return false;
	}
	if (parentId != 0 && !FindEntity(parentId)) {
		return false;
	}
	if (parentId != 0 && IsDescendantOf(parentId, id)) {
		return false;
	}
	if (entity->parentId == parentId) {
		return true;
	}
	// 親変更後も見た目の位置を維持するため、変更前のワールド行列を基準にする。
	const Matrix4x4 worldMatrix = ResolveSceneWorldMatrix(*this, *entity);
	Matrix4x4 localMatrix = worldMatrix;
	if (const SceneEntity* newParent = FindEntity(parentId)) {
		const Matrix4x4 parentWorld =
			ResolveSceneWorldMatrix(*this, *newParent);
		if (std::abs(Determinant(parentWorld)) < 0.000001f) {
			return false;
		}
		localMatrix = Multiply(
			worldMatrix,
			Inverse(parentWorld)
		);
	}
	Vector3 localScale{};
	Quaternion localRotate = MakeIdentityQuaternion();
	Vector3 localTranslate{};
	if (!DecomposeAffineMatrix(
		localMatrix,
		localScale,
		localRotate,
		localTranslate
	)) {
		return false;
	}
	entity->parentId = parentId;
	entity->transform.scale = localScale;
	entity->transform.rotate = localRotate;
	entity->transform.translate = localTranslate;
	MarkDirty();
	return true;
}

bool SceneDocument::MoveEntity(uint64_t id, int direction) {
	if (direction == 0) {
		return false;
	}
	const SceneEntity* entity = FindEntity(id);
	if (!entity) {
		return false;
	}
	const uint64_t parentId = entity->parentId;
	std::vector<size_t> siblingIndices;
	for (size_t index = 0; index < entities_.size(); ++index) {
		if (entities_[index].parentId == parentId) {
			siblingIndices.push_back(index);
		}
	}
	const auto sibling = std::find_if(
		siblingIndices.begin(),
		siblingIndices.end(),
		[this, id](size_t index) { return entities_[index].id == id; }
	);
	if (sibling == siblingIndices.end()) {
		return false;
	}
	const std::ptrdiff_t position = std::distance(
		siblingIndices.begin(),
		sibling
	);
	const std::ptrdiff_t targetPosition = position + (direction < 0 ? -1 : 1);
	if (
		targetPosition < 0 ||
		targetPosition >= static_cast<std::ptrdiff_t>(siblingIndices.size())
	) {
		return false;
	}
	std::swap(
		entities_[siblingIndices[position]],
		entities_[siblingIndices[targetPosition]]
	);
	MarkDirty();
	return true;
}

bool SceneDocument::MoveEntityToParent(uint64_t id, uint64_t parentId) {
	if (id == 0 || id == parentId) {
		return false;
	}
	if (parentId != 0 && !FindEntity(parentId)) {
		return false;
	}
	if (parentId != 0 && IsDescendantOf(parentId, id)) {
		return false;
	}

	SceneEntity* entity = FindEntity(id);
	if (!entity) {
		return false;
	}

	const bool parentChanged = entity->parentId != parentId;
	if (parentChanged && !SetParent(id, parentId)) {
		return false;
	}

	const auto sourceIt = std::find_if(
		entities_.begin(),
		entities_.end(),
		[id](const SceneEntity& candidate) {
			return candidate.id == id;
		}
	);
	if (sourceIt == entities_.end()) {
		return false;
	}

	size_t sourceIndex =
		static_cast<size_t>(std::distance(entities_.begin(), sourceIt));
	size_t insertIndex = 0;
	bool foundSibling = false;
	bool siblingAfterSource = false;
	for (size_t index = 0; index < entities_.size(); ++index) {
		if (index != sourceIndex && entities_[index].parentId == parentId) {
			insertIndex = index + 1;
			foundSibling = true;
			if (index > sourceIndex) {
				siblingAfterSource = true;
			}
		}
	}
	if (!foundSibling) {
		insertIndex = entities_.size();
	}
	if (!parentChanged && !siblingAfterSource) {
		return false;
	}

	if (insertIndex == sourceIndex || insertIndex == sourceIndex + 1) {
		if (!parentChanged) {
			return false;
		}
		return true;
	}

	SceneEntity moved = std::move(entities_[sourceIndex]);
	entities_.erase(entities_.begin() + static_cast<std::ptrdiff_t>(sourceIndex));
	if (sourceIndex < insertIndex) {
		--insertIndex;
	}
	insertIndex = (std::min)(insertIndex, entities_.size());
	entities_.insert(
		entities_.begin() + static_cast<std::ptrdiff_t>(insertIndex),
		std::move(moved)
	);
	MarkDirty();
	return true;
}

bool SceneDocument::MoveEntityToSibling(
	uint64_t id,
	uint64_t siblingId,
	bool after
) {
	if (id == 0 || siblingId == 0 || id == siblingId) {
		return false;
	}
	SceneEntity* entity = FindEntity(id);
	const SceneEntity* sibling = FindEntity(siblingId);
	if (!entity || !sibling) {
		return false;
	}
	const uint64_t targetParentId = sibling->parentId;
	if (targetParentId != 0 && IsDescendantOf(targetParentId, id)) {
		return false;
	}
	if (entity->parentId != targetParentId) {
		if (!SetParent(id, targetParentId)) {
			return false;
		}
	}

	const auto sourceIt = std::find_if(
		entities_.begin(),
		entities_.end(),
		[id](const SceneEntity& candidate) {
			return candidate.id == id;
		}
	);
	const auto targetIt = std::find_if(
		entities_.begin(),
		entities_.end(),
		[siblingId](const SceneEntity& candidate) {
			return candidate.id == siblingId;
		}
	);
	if (sourceIt == entities_.end() || targetIt == entities_.end()) {
		return false;
	}

	size_t sourceIndex =
		static_cast<size_t>(std::distance(entities_.begin(), sourceIt));
	size_t targetIndex =
		static_cast<size_t>(std::distance(entities_.begin(), targetIt));
	SceneEntity moved = std::move(entities_[sourceIndex]);
	entities_.erase(entities_.begin() + static_cast<std::ptrdiff_t>(sourceIndex));
	if (sourceIndex < targetIndex) {
		--targetIndex;
	}
	size_t insertIndex = targetIndex + (after ? 1u : 0u);
	insertIndex = (std::min)(insertIndex, entities_.size());
	entities_.insert(
		entities_.begin() + static_cast<std::ptrdiff_t>(insertIndex),
		std::move(moved)
	);
	MarkDirty();
	return true;
}

SceneTeamSettings& SceneDocument::CreateTeam(const std::string& name) {
	SceneTeamSettings team{};
	team.name = MakeUniqueTeamName(teams_, name);
	NormalizeTeamSettings(team);
	teams_.push_back(std::move(team));
	MarkDirty();
	return teams_.back();
}

bool SceneDocument::RenameTeam(
	const std::string& oldName,
	const std::string& newName
) {
	if (oldName.empty()) {
		return false;
	}
	SceneTeamSettings* team = FindTeam(oldName);
	if (!team) {
		return false;
	}
	const std::string resolvedName = MakeUniqueTeamName(
		teams_,
		newName,
		oldName
	);
	if (resolvedName == oldName) {
		return true;
	}
	team->name = resolvedName;
	for (SceneEntity& entity : entities_) {
		if (entity.teamName == oldName) {
			entity.teamName = resolvedName;
		}
	}
	MarkDirty();
	return true;
}

bool SceneDocument::RemoveTeam(const std::string& name) {
	const auto oldSize = teams_.size();
	teams_.erase(
		std::remove_if(
			teams_.begin(),
			teams_.end(),
			[&](const SceneTeamSettings& team) {
				return team.name == name;
			}
		),
		teams_.end()
	);
	if (teams_.size() == oldSize) {
		return false;
	}
	for (SceneEntity& entity : entities_) {
		if (entity.teamName == name) {
			entity.teamName.clear();
			entity.folderTeamEnabled = false;
		}
	}
	MarkDirty();
	return true;
}

SceneTeamSettings* SceneDocument::FindTeam(const std::string& name) {
	const auto found = std::find_if(
		teams_.begin(),
		teams_.end(),
		[&](const SceneTeamSettings& team) {
			return team.name == name;
		}
	);
	return found == teams_.end() ? nullptr : &(*found);
}

const SceneTeamSettings* SceneDocument::FindTeam(
	const std::string& name
) const {
	const auto found = std::find_if(
		teams_.begin(),
		teams_.end(),
		[&](const SceneTeamSettings& team) {
			return team.name == name;
		}
	);
	return found == teams_.end() ? nullptr : &(*found);
}

std::string SceneDocument::ResolveInheritedFolderTeamName(
	uint64_t entityId
) const {
	const SceneEntity* entity = FindEntity(entityId);
	if (!entity) {
		return {};
	}
	std::unordered_set<uint64_t> visited;
	uint64_t parentId = entity->parentId;
	while (parentId != 0 && visited.insert(parentId).second) {
		const SceneEntity* parent = FindEntity(parentId);
		if (!parent) {
			break;
		}
		if (
			parent->folder &&
			parent->folderTeamEnabled &&
			!parent->teamName.empty() &&
			FindTeam(parent->teamName)
		) {
			return parent->teamName;
		}
		parentId = parent->parentId;
	}
	return {};
}

const SceneTeamSettings* SceneDocument::ResolveEntityTeam(
	const SceneEntity& entity
) const {
	if (!entity.teamName.empty()) {
		if (const SceneTeamSettings* team = FindTeam(entity.teamName)) {
			return team;
		}
	}
	const std::string inheritedName = ResolveInheritedFolderTeamName(entity.id);
	return inheritedName.empty() ? nullptr : FindTeam(inheritedName);
}

bool SceneDocument::AddComponent(uint64_t id, const std::string& type) {
	if (type.empty()) {
		return false;
	}
	SceneEntity* entity = FindEntity(id);
	if (!entity) {
		return false;
	}
	if (entity->folder) {
		return false;
	}
	const auto found = std::find_if(
		entity->components.begin(),
		entity->components.end(),
		[&type](const SceneComponent& component) {
			return component.type == type;
		}
	);
	if (found != entity->components.end()) {
		bool changed = false;
		if (type == "MeshRenderer" && found->modelPath.empty()) {
			found->modelPath = entity->modelPath;
			changed = true;
		} else if (type == "MeshRenderer" && found->meshCullMode.empty()) {
			found->meshCullMode = "Back";
			changed = true;
		} else if (type == "MeshRenderer") {
			const float reflectionIntensity = std::clamp(
				found->meshEnvironmentReflectionIntensity,
				0.0f,
				1.0f
			);
			if (
				found->meshEnvironmentReflectionIntensity !=
				reflectionIntensity
			) {
				found->meshEnvironmentReflectionIntensity = reflectionIntensity;
				changed = true;
			}
		} else if (type == "Environment") {
			if (found->environmentSkyboxPath.empty()) {
				found->environmentSkyboxPath = "resources/rostock_laage_airport_4k.dds";
				changed = true;
			}
			if (found->environmentSkyboxIntensity < 0.0f) {
				found->environmentSkyboxIntensity = 1.0f;
				changed = true;
			}
			if (found->environmentReflectionIntensity < 0.0f) {
				found->environmentReflectionIntensity = 0.3f;
				changed = true;
			}
		} else if (type == "SpriteRenderer" && found->texturePath.empty()) {
			found->texturePath = entity->spriteTexturePath;
			found->spriteSize = entity->spriteSize;
			found->spriteAnchor = entity->spriteAnchor;
			found->spriteColor = entity->spriteColor;
			found->spriteFlipX = entity->spriteFlipX;
			found->spriteFlipY = entity->spriteFlipY;
			changed = true;
		} else if (type == "Camera") {
			if (found->cameraNearClip <= 0.0f) {
				found->cameraNearClip = 0.1f;
				changed = true;
			}
			if (found->cameraFarClip <= found->cameraNearClip) {
				found->cameraFarClip = 1000.0f;
				changed = true;
			}
		} else if (type == "Light") {
			if (
				found->lightType != "Directional" &&
				found->lightType != "Point" &&
				found->lightType != "Spot"
			) {
				found->lightType = "Point";
				changed = true;
			}
			const float range = (std::max)(found->lightRange, 0.1f);
			const float decay = (std::max)(found->lightDecay, 0.0f);
			const float intensity = (std::max)(found->lightIntensity, 0.0f);
			const Vector4 color{
				std::clamp(found->lightColor.x, 0.0f, 1.0f),
				std::clamp(found->lightColor.y, 0.0f, 1.0f),
				std::clamp(found->lightColor.z, 0.0f, 1.0f),
				std::clamp(found->lightColor.w, 0.0f, 1.0f)
			};
			const float outerAngle = std::clamp(
				found->lightSpotOuterAngle,
				1.0f,
				89.0f
			);
			const float innerAngle = std::clamp(
				found->lightSpotInnerAngle,
				0.0f,
				outerAngle
			);
			if (
				found->lightRange != range ||
				found->lightDecay != decay ||
				found->lightIntensity != intensity ||
				found->lightSpotOuterAngle != outerAngle ||
				found->lightSpotInnerAngle != innerAngle ||
				found->lightColor.x != color.x ||
				found->lightColor.y != color.y ||
				found->lightColor.z != color.z ||
				found->lightColor.w != color.w
			) {
				found->lightColor = color;
				found->lightRange = range;
				found->lightDecay = decay;
				found->lightIntensity = intensity;
				found->lightSpotOuterAngle = outerAngle;
				found->lightSpotInnerAngle = innerAngle;
				changed = true;
			}
			if (found->lightType == "Point" && found->lightCastsShadow) {
				found->lightCastsShadow = false;
				changed = true;
			}
		} else if (type == "MonitorRenderer") {
			if (found->monitorResolutionPreset.empty()) {
				found->monitorResolutionPreset = "Custom";
				changed = true;
			}
			const uint32_t width = std::clamp<uint32_t>(
				found->monitorWidth,
				64,
				2048
			);
			const uint32_t height = std::clamp<uint32_t>(
				found->monitorHeight,
				64,
				2048
			);
			if (found->monitorWidth != width || found->monitorHeight != height) {
				found->monitorWidth = width;
				found->monitorHeight = height;
				changed = true;
			}
		} else if (type == "ThirdPersonCamera") {
			if (found->thirdPersonDistance < 0.01f) {
				found->thirdPersonDistance = 0.01f;
				changed = true;
			}
			if (found->thirdPersonAimDistance < 0.01f) {
				found->thirdPersonAimDistance = 0.01f;
				changed = true;
			}
			if (found->thirdPersonMouseSensitivity < 0.0f) {
				found->thirdPersonMouseSensitivity = 0.0f;
				changed = true;
			}
			if (found->thirdPersonMaxPitch < found->thirdPersonMinPitch) {
				std::swap(found->thirdPersonMinPitch, found->thirdPersonMaxPitch);
				changed = true;
			}
			if (found->thirdPersonOcclusionMargin < 0.0f) {
				found->thirdPersonOcclusionMargin = 0.0f;
				changed = true;
			}
		} else if (type == "PhysicsBody") {
			if (found->physicsBodyType.empty()) {
				found->physicsBodyType = "Static";
				changed = true;
			}
			if (found->physicsMass <= 0.0f) {
				found->physicsMass = 1.0f;
				changed = true;
			}
			if (found->physicsMaxFallSpeed <= 0.0f) {
				found->physicsMaxFallSpeed = 100.0f;
				changed = true;
			}
		} else if (type == "PlayerBehavior") {
			if (found->playerMoveSpeed < 0.0f) {
				found->playerMoveSpeed = 0.0f;
				changed = true;
			}
			if (found->playerJumpVelocity < 0.0f) {
				found->playerJumpVelocity = 0.0f;
				changed = true;
			}
			const float turnResponsiveness = std::clamp(
				found->playerTurnResponsiveness,
				0.0f,
				1.0f
			);
			if (found->playerTurnResponsiveness != turnResponsiveness) {
				found->playerTurnResponsiveness = turnResponsiveness;
				changed = true;
			}
			if (found->playerDashMultiplier < 1.0f) {
				found->playerDashMultiplier = 1.0f;
				changed = true;
			}
		} else if (type == "AgentBehavior") {
			if (found->agentBehaviorName.empty()) {
				found->agentBehaviorName = "Agent";
				changed = true;
			}
			if (found->agentProfileName.empty()) {
				found->agentProfileName = "Default";
				changed = true;
			}
			const float minSpeed = (std::max)(found->agentMinSpeed, 0.0f);
			const float maxSpeed =
				(std::max)(found->agentMaxSpeed, minSpeed);
			if (found->agentMinSpeed != minSpeed) {
				found->agentMinSpeed = minSpeed;
				changed = true;
			}
			if (found->agentMaxSpeed != maxSpeed) {
				found->agentMaxSpeed = maxSpeed;
				changed = true;
			}
			if (found->agentTurnSpeed < 0.0f) {
				found->agentTurnSpeed = 0.0f;
				changed = true;
			}
			if (found->agentWanderStrength < 0.0f) {
				found->agentWanderStrength = 0.0f;
				changed = true;
			}
			if (found->agentBoundsWeight < 0.0f) {
				found->agentBoundsWeight = 0.0f;
				changed = true;
			}
			const Vector3 teamHeadingDirection = NormalizeDirectionVector(
				found->agentTeamHeadingDirection,
				{ 0.0f, 0.0f, 1.0f }
			);
			if (
				found->agentTeamHeadingDirection.x != teamHeadingDirection.x ||
				found->agentTeamHeadingDirection.y != teamHeadingDirection.y ||
				found->agentTeamHeadingDirection.z != teamHeadingDirection.z
			) {
				found->agentTeamHeadingDirection = teamHeadingDirection;
				changed = true;
			}
			if (found->agentTeamHeadingWeight < 0.0f) {
				found->agentTeamHeadingWeight = 0.0f;
				changed = true;
			}
			if (found->agentTeamHeadingFollowSpeed < 0.0f) {
				found->agentTeamHeadingFollowSpeed = 0.0f;
				changed = true;
			}
			const float teamRotationWeight = std::clamp(
				found->agentTeamRotationWeight,
				0.0f,
				1.0f
			);
			if (found->agentTeamRotationWeight != teamRotationWeight) {
				found->agentTeamRotationWeight = teamRotationWeight;
				changed = true;
			}
			if (found->agentTeamRotationFollowSpeed < 0.0f) {
				found->agentTeamRotationFollowSpeed = 0.0f;
				changed = true;
			}
			if (
				found->agentForwardAxis != "+Z" &&
				found->agentForwardAxis != "-Z" &&
				found->agentForwardAxis != "+X" &&
				found->agentForwardAxis != "-X" &&
				found->agentForwardAxis != "+Y" &&
				found->agentForwardAxis != "-Y"
			) {
				found->agentForwardAxis = "+Z";
				changed = true;
			}
			if (found->agentRotationFollowSpeed < 0.0f) {
				found->agentRotationFollowSpeed = 0.0f;
				changed = true;
			}
			if (found->agentPitchFromVerticalVelocity < 0.0f) {
				found->agentPitchFromVerticalVelocity = 0.0f;
				changed = true;
			}
			if (found->agentBankingStrength < 0.0f) {
				found->agentBankingStrength = 0.0f;
				changed = true;
			}
			if (found->agentSchoolingUpdateInterval < 0.0f) {
				found->agentSchoolingUpdateInterval = 0.0f;
				changed = true;
			}
			if (found->agentSchoolingUpdateJitter < 0.0f) {
				found->agentSchoolingUpdateJitter = 0.0f;
				changed = true;
			}
			if (found->agentNeighborLimit < 0) {
				found->agentNeighborLimit = 0;
				changed = true;
			}
			const float schoolingBlend = std::clamp(
				found->agentSchoolingBlend,
				0.0f,
				1.0f
			);
			if (found->agentSchoolingBlend != schoolingBlend) {
				found->agentSchoolingBlend = schoolingBlend;
				changed = true;
			}
			if (found->agentSeparationRadius < 0.0f) {
				found->agentSeparationRadius = 0.0f;
				changed = true;
			}
			if (found->agentAlignmentRadius < 0.0f) {
				found->agentAlignmentRadius = 0.0f;
				changed = true;
			}
			if (found->agentCohesionRadius < 0.0f) {
				found->agentCohesionRadius = 0.0f;
				changed = true;
			}
			if (found->agentSeparationWeight < 0.0f) {
				found->agentSeparationWeight = 0.0f;
				changed = true;
			}
			if (found->agentAlignmentWeight < 0.0f) {
				found->agentAlignmentWeight = 0.0f;
				changed = true;
			}
			if (found->agentCohesionWeight < 0.0f) {
				found->agentCohesionWeight = 0.0f;
				changed = true;
			}
			if (found->agentAttractorWeight < 0.0f) {
				found->agentAttractorWeight = 0.0f;
				changed = true;
			}
		} else if (type == "AgentAttractor") {
			if (found->attractorTag.empty()) {
				found->attractorTag = "Default";
				changed = true;
			}
			if (found->attractorRadius < 0.0f) {
				found->attractorRadius = 0.0f;
				changed = true;
			}
			if (found->attractorStrength < 0.0f) {
				found->attractorStrength = 0.0f;
				changed = true;
			}
		} else if (type == "WaterVolume") {
			const Vector3 halfSize = {
				(std::max)(found->waterHalfSize.x, 0.1f),
				(std::max)(found->waterHalfSize.y, 0.1f),
				(std::max)(found->waterHalfSize.z, 0.1f)
			};
			if (
				found->waterHalfSize.x != halfSize.x ||
				found->waterHalfSize.y != halfSize.y ||
				found->waterHalfSize.z != halfSize.z
			) {
				found->waterHalfSize = halfSize;
				changed = true;
			}
			const float moveMultiplier = std::clamp(
				found->waterMoveSpeedMultiplier,
				0.0f,
				1.0f
			);
			if (found->waterMoveSpeedMultiplier != moveMultiplier) {
				found->waterMoveSpeedMultiplier = moveMultiplier;
				changed = true;
			}
			if (found->waterDrag < 0.0f) {
				found->waterDrag = 0.0f;
				changed = true;
			}
			if (found->waterMaxFallSpeed < 0.0f) {
				found->waterMaxFallSpeed = 0.0f;
				changed = true;
			}
			if (found->waterSwimUpSpeed < 0.0f) {
				found->waterSwimUpSpeed = 0.0f;
				changed = true;
			}
			const float surfaceAlpha = std::clamp(
				found->waterSurfaceAlpha,
				0.0f,
				1.0f
			);
			if (found->waterSurfaceAlpha != surfaceAlpha) {
				found->waterSurfaceAlpha = surfaceAlpha;
				changed = true;
			}
			const float surfaceWaveScale = std::clamp(
				found->waterSurfaceWaveScale,
				0.0f,
				3.0f
			);
			if (found->waterSurfaceWaveScale != surfaceWaveScale) {
				found->waterSurfaceWaveScale = surfaceWaveScale;
				changed = true;
			}
			const float surfaceNormalStrength = std::clamp(
				found->waterSurfaceNormalStrength,
				0.0f,
				2.0f
			);
			if (found->waterSurfaceNormalStrength != surfaceNormalStrength) {
				found->waterSurfaceNormalStrength = surfaceNormalStrength;
				changed = true;
			}
			const float surfaceFresnelPower = std::clamp(
				found->waterSurfaceFresnelPower,
				0.2f,
				8.0f
			);
			if (found->waterSurfaceFresnelPower != surfaceFresnelPower) {
				found->waterSurfaceFresnelPower = surfaceFresnelPower;
				changed = true;
			}
			const float lightIntensity = (std::max)(
				found->waterLightIntensity,
				0.0f
			);
			if (found->waterLightIntensity != lightIntensity) {
				found->waterLightIntensity = lightIntensity;
				changed = true;
			}
			const float lightDensity = (std::max)(
				found->waterLightDensity,
				0.0f
			);
			if (found->waterLightDensity != lightDensity) {
				found->waterLightDensity = lightDensity;
				changed = true;
			}
			const float causticsIntensity = (std::max)(
				found->waterLightCausticsIntensity,
				0.0f
			);
			if (found->waterLightCausticsIntensity != causticsIntensity) {
				found->waterLightCausticsIntensity = causticsIntensity;
				changed = true;
			}
			const float causticsScale = (std::max)(
				found->waterLightCausticsScale,
				0.001f
			);
			if (found->waterLightCausticsScale != causticsScale) {
				found->waterLightCausticsScale = causticsScale;
				changed = true;
			}
			const float breakupStrength = std::clamp(
				found->waterLightBreakupStrength,
				0.0f,
				3.0f
			);
			if (found->waterLightBreakupStrength != breakupStrength) {
				found->waterLightBreakupStrength = breakupStrength;
				changed = true;
			}
			const float warpStrength = std::clamp(
				found->waterLightWarpStrength,
				0.0f,
				3.0f
			);
			if (found->waterLightWarpStrength != warpStrength) {
				found->waterLightWarpStrength = warpStrength;
				changed = true;
			}
			const float noiseScale = (std::max)(
				found->waterLightNoiseScale,
				0.001f
			);
			if (found->waterLightNoiseScale != noiseScale) {
				found->waterLightNoiseScale = noiseScale;
				changed = true;
			}
			const int sampleCount = std::clamp(
				found->waterLightSampleCount,
				4,
				32
			);
			if (found->waterLightSampleCount != sampleCount) {
				found->waterLightSampleCount = sampleCount;
				changed = true;
			}
		} else if (type == "CameraPath") {
			if (found->cameraPathTriggerType.empty()) {
				found->cameraPathTriggerType = "Key";
				changed = true;
			}
			if (found->cameraPathTriggerKey.empty()) {
				found->cameraPathTriggerKey = "C";
				changed = true;
			}
			if (found->cameraPathEnterDuration < 0.0f) {
				found->cameraPathEnterDuration = 0.0f;
				changed = true;
			}
			if (found->cameraPathExitDuration < 0.0f) {
				found->cameraPathExitDuration = 0.0f;
				changed = true;
			}
			if (found->cameraPathInterpolation.empty()) {
				found->cameraPathInterpolation = "Linear";
				changed = true;
			}
			if (found->cameraPathDefaultEasing.empty()) {
				found->cameraPathDefaultEasing = "SmoothStep";
				changed = true;
			}
		} else if (type == "CameraPathPoint") {
			if (found->cameraPathPointDurationToNext < 0.0f) {
				found->cameraPathPointDurationToNext = 0.0f;
				changed = true;
			}
			if (found->cameraPathPointEasingToNext.empty()) {
				found->cameraPathPointEasingToNext = "SmoothStep";
				changed = true;
			}
		}
		if (!found->enabled) {
			found->enabled = true;
			changed = true;
		}
		if (changed) {
			MarkDirty();
		}
		return true;
	}
	SceneComponent component{ type, true };
	if (type == "MeshRenderer") {
		component.modelPath = entity->modelPath;
		component.meshCullMode = "Back";
		component.meshEnvironmentReflectionOverride = false;
		component.meshEnvironmentReflectionIntensity = 0.3f;
	} else if (type == "Environment") {
		component.environmentSkyboxEnabled = true;
		component.environmentSkyboxPath = "resources/rostock_laage_airport_4k.dds";
		component.environmentSkyboxIntensity = 1.0f;
		component.environmentReflectionIntensity = 0.3f;
	} else if (type == "SpriteRenderer") {
		component.texturePath = entity->spriteTexturePath;
		component.spriteSize = entity->spriteSize;
		component.spriteAnchor = entity->spriteAnchor;
		component.spriteColor = entity->spriteColor;
		component.spriteFlipX = entity->spriteFlipX;
		component.spriteFlipY = entity->spriteFlipY;
	} else if (type == "Camera") {
		component.cameraIsMain = false;
		component.cameraFovY = 0.45f;
		component.cameraNearClip = 0.1f;
		component.cameraFarClip = 1000.0f;
		component.cameraInvertYaw = false;
		component.cameraInvertPitch = false;
	} else if (type == "Light") {
		component.lightType = "Point";
		component.lightColor = { 1.0f, 0.85f, 0.65f, 1.0f };
		component.lightIntensity = 2.0f;
		component.lightRange = 8.0f;
		component.lightDecay = 1.0f;
		component.lightSpotInnerAngle = 25.0f;
		component.lightSpotOuterAngle = 35.0f;
		component.lightCastsShadow = false;
	} else if (type == "MonitorRenderer") {
		component.monitorCameraEntityId = 0;
		component.monitorCameraName = "";
		component.monitorResolutionPreset = "Square 512";
		component.monitorWidth = 512;
		component.monitorHeight = 512;
		component.monitorHideSelf = true;
	} else if (type == "ThirdPersonCamera") {
		component.thirdPersonDistance = 8.0f;
		component.thirdPersonAimDistance = 3.0f;
		component.thirdPersonTargetOffset = { 0.0f, 1.35f, 0.0f };
		component.thirdPersonAimTargetOffset = { 0.0f, 1.55f, 0.0f };
		component.thirdPersonMouseSensitivity = 0.005f;
		component.thirdPersonMinPitch = -1.45f;
		component.thirdPersonMaxPitch = 1.35f;
		component.thirdPersonOcclusionMargin = 0.45f;
		component.thirdPersonInvertYaw = false;
		component.thirdPersonInvertPitch = false;
	} else if (type == "Animator") {
		component.animatorPlayOnStart = true;
		component.animatorLoop = true;
		component.animatorSpeed = 1.0f;
		component.animatorDefaultClip = 0;
		component.animatorTransitionDuration = 0.2f;
		component.animatorBlendCurve = "SmoothStep";
	} else if (type == "PhysicsBody") {
		component.physicsBodyType = "Static";
		component.physicsMass = 1.0f;
		component.physicsUseGravity = true;
		component.physicsGravityScale = 1.0f;
		component.physicsDrag = 0.0f;
		component.physicsRestitution = 0.0f;
		component.physicsFriction = 0.0f;
		component.physicsMaxFallSpeed = 100.0f;
		component.physicsVelocity = { 0.0f, 0.0f, 0.0f };
	} else if (type == "PlayerBehavior") {
		component.playerMoveSpeed = 10.8f;
		component.playerJumpVelocity = 37.2f;
		component.playerTurnResponsiveness = 0.018f;
		component.playerDashMultiplier = 1.65f;
		component.playerCameraRelativeMove = true;
		component.playerAllowJump = true;
	} else if (type == "AgentBehavior") {
		component.agentBehaviorName = "Agent";
		component.agentProfileName = "Default";
		component.agentGroupName = "";
		component.agentBoundsEntityId = 0;
		component.agentBoundsName = "";
		component.agentAttractorEntityId = 0;
		component.agentAttractorTag = "";
		component.agentUseWaterBounds = true;
		component.agentMinSpeed = 1.0f;
		component.agentMaxSpeed = 3.0f;
		component.agentTurnSpeed = 2.5f;
		component.agentWanderStrength = 0.8f;
		component.agentBoundsWeight = 3.0f;
		component.agentSchooling = false;
		component.agentSeparationRadius = 1.2f;
		component.agentAlignmentRadius = 4.0f;
		component.agentCohesionRadius = 5.0f;
		component.agentSeparationWeight = 1.8f;
		component.agentAlignmentWeight = 0.8f;
		component.agentCohesionWeight = 0.9f;
		component.agentAttractorWeight = 0.0f;
		component.agentVisualColor = { 0.25f, 0.75f, 1.0f, 1.0f };
		component.agentEnableLighting = true;
	} else if (type == "AgentAttractor") {
		component.attractorTag = "Default";
		component.attractorTargetBehaviorName = "";
		component.attractorTargetProfileName = "";
		component.attractorRadius = 6.0f;
		component.attractorStrength = 1.0f;
		component.attractorVisualColor = { 1.0f, 0.35f, 0.45f, 1.0f };
	} else if (type == "WaterVolume") {
		component.waterHalfSize = { 10.0f, 4.0f, 10.0f };
		component.waterOffset = { 0.0f, 0.0f, 0.0f };
		component.waterSurfaceEnabled = true;
		component.waterSurfaceBaseColor = { 0.04f, 0.55f, 0.78f, 1.0f };
		component.waterSurfaceHighlightColor = { 0.42f, 0.95f, 1.20f, 1.0f };
		component.waterSurfaceAlpha = 0.36f;
		component.waterSurfaceWaveScale = 1.0f;
		component.waterSurfaceNormalStrength = 0.75f;
		component.waterSurfaceFresnelPower = 3.0f;
		component.waterLightShaftEnabled = true;
		component.waterLightColor = { 0.55f, 0.90f, 1.15f, 1.0f };
		component.waterLightDirection = { -0.25f, -1.0f, 0.18f };
		component.waterLightIntensity = 0.55f;
		component.waterLightDensity = 0.045f;
		component.waterLightCausticsIntensity = 0.35f;
		component.waterLightCausticsScale = 0.08f;
		component.waterLightCausticsSpeed = 1.0f;
		component.waterLightBreakupStrength = 1.0f;
		component.waterLightWarpStrength = 1.0f;
		component.waterLightNoiseScale = 1.0f;
		component.waterLightSampleCount = 16;
		component.waterMoveSpeedMultiplier = 0.45f;
		component.waterGravityScale = 0.55f;
		component.waterDrag = 4.0f;
		component.waterMaxFallSpeed = 5.0f;
		component.waterSwimUpSpeed = 12.0f;
	} else if (type == "EntityReference") {
		component.entityReferenceName = "Target";
		component.entityReferenceTarget = {};
	} else if (type == "SceneTransition") {
		component.sceneTransitionTargetSceneId = "gameplay";
		component.sceneTransitionTriggerType = "Key";
		component.sceneTransitionTriggerKey = "ENTER";
	} else if (type == "CameraPath") {
		component.cameraPathTargetCameraName = "";
		component.cameraPathTriggerType = "Key";
		component.cameraPathTriggerKey = "C";
		component.cameraPathEnterDuration = 0.5f;
		component.cameraPathExitDuration = 0.5f;
		component.cameraPathInterpolation = "Linear";
		component.cameraPathDefaultEasing = "SmoothStep";
		component.cameraPathReturnToPreviousCamera = true;
		component.cameraPathStartFromCurrentCamera = true;
		component.cameraPathAutoCollectChildPoints = true;
	} else if (type == "CameraPathPoint") {
		component.cameraPathPointDurationToNext = 1.0f;
		component.cameraPathPointEasingToNext = "SmoothStep";
	}
	entity->components.push_back(std::move(component));
	if (type == "CameraPath") {
		const uint64_t pathEntityId = entity->id;
		for (uint32_t index = 0; index < 2; ++index) {
			SceneEntity& point = CreateEntity(
				index == 0 ? "Point_00" : "Point_01",
				pathEntityId
			);
			point.transform.translate = {
				0.0f,
				0.0f,
				static_cast<float>(index) * 5.0f
			};
			point.components.push_back(SceneComponent{ "CameraPathPoint", true });
			point.components.back().cameraPathPointDurationToNext = 1.0f;
			point.components.back().cameraPathPointEasingToNext = "SmoothStep";
		}
	}
	MarkDirty();
	return true;
}

bool SceneDocument::RemoveComponent(uint64_t id, const std::string& type) {
	SceneEntity* entity = FindEntity(id);
	if (!entity) {
		return false;
	}
	const auto oldSize = entity->components.size();
	entity->components.erase(
		std::remove_if(
			entity->components.begin(),
			entity->components.end(),
			[&type](const SceneComponent& component) {
				return component.type == type;
			}
		),
		entity->components.end()
	);
	if (entity->components.size() == oldSize) {
		return false;
	}
	MarkDirty();
	return true;
}

bool SceneDocument::IsDescendantOf(
	uint64_t id,
	uint64_t potentialAncestorId
) const {
	std::unordered_set<uint64_t> visited;
	const SceneEntity* current = FindEntity(id);
	while (current && current->parentId != 0) {
		if (current->parentId == potentialAncestorId) {
			return true;
		}
		if (!visited.insert(current->id).second) {
			return false;
		}
		current = FindEntity(current->parentId);
	}
	return false;
}

SceneEntity* SceneDocument::FindEntity(uint64_t id) {
	const auto found = std::find_if(
		entities_.begin(),
		entities_.end(),
		[id](const SceneEntity& entity) { return entity.id == id; }
	);
	return found == entities_.end() ? nullptr : &(*found);
}

const SceneEntity* SceneDocument::FindEntity(uint64_t id) const {
	const auto found = std::find_if(
		entities_.begin(),
		entities_.end(),
		[id](const SceneEntity& entity) { return entity.id == id; }
	);
	return found == entities_.end() ? nullptr : &(*found);
}

SceneEntity* SceneDocument::FindEntityByName(const std::string& name) {
	const auto found = std::find_if(
		entities_.begin(),
		entities_.end(),
		[&name](const SceneEntity& entity) { return entity.name == name; }
	);
	return found == entities_.end() ? nullptr : &(*found);
}

const SceneEntity* SceneDocument::FindEntityByName(const std::string& name) const {
	const auto found = std::find_if(
		entities_.begin(),
		entities_.end(),
		[&name](const SceneEntity& entity) { return entity.name == name; }
	);
	return found == entities_.end() ? nullptr : &(*found);
}

bool SceneDocument::LoadInternal(const std::string& filePath) {
	std::ifstream input(StringUtility::ToPath(filePath), std::ios::binary);
	if (!input.is_open()) {
		lastLoadError_ = "Scene file could not be opened: " + filePath;
		return false;
	}

	bool migrated = false;
	try {
		json root = json::parse(input);
		std::string migrationError;
		if (!SceneDocumentMigrator::Migrate(
			root,
			migrated,
			migrationError
		)) {
			lastLoadError_ = migrationError;
			return false;
		}
		if (!root.contains("entities") || !root.at("entities").is_array()) {
			lastLoadError_ = "Scene JSON must contain an entities array";
			return false;
		}
		if (root.contains("lighting") && !root.at("lighting").is_object()) {
			lastLoadError_ = "Scene lighting settings must be an object";
			return false;
		}
		if (root.contains("postProcess") && !root.at("postProcess").is_object()) {
			lastLoadError_ = "Scene postProcess must be an object";
			return false;
		}
		if (root.contains("debug") && !root.at("debug").is_object()) {
			lastLoadError_ = "Scene debug settings must be an object";
			return false;
		}
		if (root.contains("teams") && !root.at("teams").is_array()) {
			lastLoadError_ = "Scene teams must be an array";
			return false;
		}
		if (root.contains("teams")) {
			std::unordered_set<std::string> teamNames;
			for (const json& team : root.at("teams")) {
				if (!team.is_object()) {
					lastLoadError_ = "Scene contains an invalid Team entry";
					return false;
				}
				if (!team.contains("name") || !team.at("name").is_string() ||
					team.at("name").get<std::string>().empty()) {
					lastLoadError_ = "Scene Team requires a name";
					return false;
				}
				const std::string teamName = team.at("name").get<std::string>();
				if (!teamNames.insert(teamName).second) {
					lastLoadError_ = "Duplicate Scene Team name: " + teamName;
					return false;
				}
			}
		}

		sceneName_ = root.value("sceneName", std::string{});
		entities_.clear();
		teams_.clear();
		lightingSettings_ = LightingSettingsFromJson(
			root.value("lighting", json::object()),
			SceneLightingSettings{}
		);
		postProcessSettings_ = PostProcessFromJson(
			root.value("postProcess", json::object()),
			ScenePostProcessSettings{}
		);
		debugSettings_ = DebugSettingsFromJson(
			root.value("debug", json::object()),
			SceneDebugSettings{}
		);
		if (root.contains("teams") && root.at("teams").is_array()) {
			for (const json& source : root.at("teams")) {
				SceneTeamSettings team = TeamFromJson(source);
				team.name = MakeUniqueTeamName(teams_, team.name);
				teams_.push_back(std::move(team));
			}
		}
		for (const json& source : root.at("entities")) {
			if (!source.is_object()) {
				lastLoadError_ = "Scene contains an invalid Entity entry";
				return false;
			}
			if (source.contains("transform") &&
				!source.at("transform").is_object()) {
				lastLoadError_ = "Entity transform must be an object";
				return false;
			}
			if (source.contains("sprite") && !source.at("sprite").is_object()) {
				lastLoadError_ = "Entity sprite must be an object";
				return false;
			}
			if (source.contains("components")) {
				if (!source.at("components").is_array()) {
					lastLoadError_ = "Entity components must be an array";
					return false;
				}
				for (const json& component : source.at("components")) {
					if (!component.is_object() ||
						!component.contains("type") ||
						!component.at("type").is_string() ||
						component.at("type").get<std::string>().empty()) {
						lastLoadError_ =
							"Entity contains an invalid Component entry";
						return false;
					}
				}
			}
			SceneEntity entity{};
			entity.id = source.value("id", uint64_t{});
			entity.parentId = source.value("parentId", uint64_t{});
			entity.name = source.value("name", std::string("Entity"));
			entity.folder = source.value("folder", false);
			entity.folderTeamEnabled = source.value(
				"folderTeamEnabled",
				false
			);
			entity.active = source.value("active", true);
			entity.locked = source.value("locked", false);
			entity.teamName = source.value("team", std::string{});
			entity.modelPath = source.value("modelPath", std::string{});
			if (source.contains("sprite") && source.at("sprite").is_object()) {
				const json& sprite = source.at("sprite");
				entity.spriteTexturePath = sprite.value(
					"texturePath",
					std::string{}
				);
				if (sprite.contains("size")) {
					entity.spriteSize = JsonToVector(sprite.at("size"), entity.spriteSize);
				}
				if (sprite.contains("anchor")) {
					entity.spriteAnchor = JsonToVector(sprite.at("anchor"), entity.spriteAnchor);
				}
				if (sprite.contains("color")) {
					entity.spriteColor = JsonToVector(sprite.at("color"), entity.spriteColor);
				}
				entity.spriteFlipX = sprite.value("flipX", false);
				entity.spriteFlipY = sprite.value("flipY", false);
			}
			if (source.contains("components")) {
				entity.components = ComponentsFromJson(source.at("components"));
			}
			if (source.contains("transform")) {
				const json& transform = source.at("transform");
				if (transform.contains("scale")) {
					entity.transform.scale = JsonToVector(
						transform.at("scale"),
						entity.transform.scale
					);
				}
				if (transform.contains("rotation")) {
					entity.transform.rotate = JsonToQuaternion(
						transform.at("rotation"),
						entity.transform.rotate
					);
				} else if (transform.contains("rotate")) {
					entity.transform.rotate = MakeQuaternionFromEuler(
						JsonToVector(transform.at("rotate"), Vector3{})
					);
				}
				if (transform.contains("translate")) {
					entity.transform.translate = JsonToVector(
						transform.at("translate"),
						entity.transform.translate
					);
				}
			}
			SynchronizeLegacyRendererFields(entity);
			entities_.push_back(std::move(entity));
		}
	}
	catch (const json::exception& exception) {
		entities_.clear();
		teams_.clear();
		lastLoadError_ = "Scene JSON is invalid: ";
		lastLoadError_ += exception.what();
		return false;
	}

	std::vector<SceneValidationIssue> issues;
	if (!SceneValidator::ValidateDocument(
		*this,
		nullptr,
		{},
		filePath,
		issues
	)) {
		lastLoadError_ = SceneValidator::FormatIssues(issues);
		entities_.clear();
		teams_.clear();
		return false;
	}
	RebuildNextId();
	ValidateHierarchy();
	dirty_ = migrated;
	if (migrated) {
		++revision_;
	}
	lastLoadError_.clear();
	return true;
}

void SceneDocument::RebuildNextId() {
	nextId_ = 1;
	for (const SceneEntity& entity : entities_) {
		nextId_ = (std::max)(nextId_, entity.id + 1);
	}
}

void SceneDocument::ValidateHierarchy() {
	for (SceneTeamSettings& team : teams_) {
		NormalizeTeamSettings(team);
	}
	for (SceneEntity& entity : entities_) {
		if (!entity.teamName.empty() && !FindTeam(entity.teamName)) {
			entity.teamName.clear();
		}
		if (!entity.folder) {
			entity.folderTeamEnabled = false;
		}
		if (entity.folder) {
			entity.components.clear();
			entity.modelPath.clear();
			entity.spriteTexturePath.clear();
			if (entity.teamName.empty()) {
				entity.folderTeamEnabled = false;
			}
		}
		if (
			entity.parentId == entity.id ||
			(entity.parentId != 0 && !FindEntity(entity.parentId))
		) {
			entity.parentId = 0;
			continue;
		}

		std::unordered_set<uint64_t> visited{ entity.id };
		const SceneEntity* parent = FindEntity(entity.parentId);
		while (parent) {
			if (!visited.insert(parent->id).second) {
				entity.parentId = 0;
				break;
			}
			parent = FindEntity(parent->parentId);
		}
	}
}
