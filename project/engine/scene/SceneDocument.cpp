#include "SceneDocument.h"
#include "../math/Matrix4x4.h"

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

	json VectorToJson(const Vector3& value) {
		return json::array({ value.x, value.y, value.z });
	}

	json VectorToJson(const Vector2& value) {
		return json::array({ value.x, value.y });
	}

	json VectorToJson(const Vector4& value) {
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
		} else if (component.type == "PlayerBehavior") {
			result["moveSpeed"] = component.playerMoveSpeed;
			result["jumpVelocity"] = component.playerJumpVelocity;
			result["turnResponsiveness"] = component.playerTurnResponsiveness;
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
			result["boundsWeight"] = component.agentBoundsWeight;
			result["schooling"] = component.agentSchooling;
			result["separationRadius"] = component.agentSeparationRadius;
			result["alignmentRadius"] = component.agentAlignmentRadius;
			result["cohesionRadius"] = component.agentCohesionRadius;
			result["separationWeight"] = component.agentSeparationWeight;
			result["alignmentWeight"] = component.agentAlignmentWeight;
			result["cohesionWeight"] = component.agentCohesionWeight;
			result["attractorWeight"] = component.agentAttractorWeight;
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
			result["moveSpeedMultiplier"] =
				component.waterMoveSpeedMultiplier;
			result["gravityScale"] = component.waterGravityScale;
			result["drag"] = component.waterDrag;
			result["maxFallSpeed"] = component.waterMaxFallSpeed;
			result["swimUpSpeed"] = component.waterSwimUpSpeed;
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
				component.agentBoundsWeight = value.value(
					"boundsWeight",
					component.agentBoundsWeight
				);
				component.agentSchooling = value.value(
					"schooling",
					component.agentSchooling
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
					component.agentBoundsWeight =
						(std::max)(component.agentBoundsWeight, 0.0f);
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

	SceneComponent* FindComponent(
		SceneEntity& entity,
		const std::string& type
	) {
		const auto found = std::find_if(
			entity.components.begin(),
			entity.components.end(),
			[&type](const SceneComponent& component) {
				return component.type == type;
			}
		);
		return found == entity.components.end() ? nullptr : &(*found);
	}

	const SceneComponent* FindComponent(
		const SceneEntity& entity,
		const std::string& type
	) {
		const auto found = std::find_if(
			entity.components.begin(),
			entity.components.end(),
			[&type](const SceneComponent& component) {
				return component.type == type;
			}
		);
		return found == entity.components.end() ? nullptr : &(*found);
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

	Matrix4x4 CalculateEntityWorldMatrix(
		const SceneDocument& document,
		const SceneEntity& entity,
		std::unordered_set<uint64_t>& visited
	) {
		const Matrix4x4 local = MakeAffineMatrix(
			entity.transform.scale,
			entity.transform.rotate,
			entity.transform.translate
		);
		if (entity.parentId == 0 || !visited.insert(entity.id).second) {
			return local;
		}
		const SceneEntity* parent = document.FindEntity(entity.parentId);
		if (!parent) {
			return local;
		}
		return Multiply(
			local,
			CalculateEntityWorldMatrix(document, *parent, visited)
		);
	}

	Matrix4x4 CalculateEntityWorldMatrix(
		const SceneDocument& document,
		const SceneEntity& entity
	) {
		std::unordered_set<uint64_t> visited;
		return CalculateEntityWorldMatrix(document, entity, visited);
	}
}

void SceneDocument::Clear(const std::string& sceneName) {
	sceneName_ = sceneName;
	entities_.clear();
	postProcessSettings_ = {};
	nextId_ = 1;
	dirty_ = false;
	revision_ = 0;
}

bool SceneDocument::Load(const std::string& filePath) {
	if (LoadInternal(filePath)) {
		return true;
	}

	const std::string backupPath = filePath + ".bak";
	if (!LoadInternal(backupPath)) {
		return false;
	}

	MarkDirty();
	return true;
}

bool SceneDocument::Save(const std::string& filePath) {
	json root;
	root["version"] = 10;
	root["sceneName"] = sceneName_;
	root["postProcess"] = PostProcessToJson(postProcessSettings_);
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
			{ "active", entity.active },
			{ "locked", entity.locked },
			{ "transform", {
				{ "scale", VectorToJson(entity.transform.scale) },
				{ "rotate", VectorToJson(entity.transform.rotate) },
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

	const std::filesystem::path target(filePath);
	const std::filesystem::path temporary = target.string() + ".tmp";
	const std::filesystem::path backup = target.string() + ".bak";
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
		duplicate.active = found->active;
		duplicate.locked = found->locked;
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
	const Matrix4x4 worldMatrix = CalculateEntityWorldMatrix(*this, *entity);
	Matrix4x4 localMatrix = worldMatrix;
	if (const SceneEntity* newParent = FindEntity(parentId)) {
		const Matrix4x4 parentWorld =
			CalculateEntityWorldMatrix(*this, *newParent);
		if (std::abs(Determinant(parentWorld)) < 0.000001f) {
			return false;
		}
		localMatrix = Multiply(
			worldMatrix,
			Inverse(parentWorld)
		);
	}
	Vector3 localScale{};
	Vector3 localRotate{};
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

bool SceneDocument::AddComponent(uint64_t id, const std::string& type) {
	if (type.empty()) {
		return false;
	}
	SceneEntity* entity = FindEntity(id);
	if (!entity) {
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
		component.waterMoveSpeedMultiplier = 0.45f;
		component.waterGravityScale = 0.55f;
		component.waterDrag = 4.0f;
		component.waterMaxFallSpeed = 5.0f;
		component.waterSwimUpSpeed = 12.0f;
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
	std::ifstream input(filePath, std::ios::binary);
	if (!input.is_open()) {
		return false;
	}

	try {
		const json root = json::parse(input);
		if (!root.is_object() || !root.contains("entities")) {
			return false;
		}

		sceneName_ = root.value("sceneName", std::string{});
		entities_.clear();
		postProcessSettings_ = PostProcessFromJson(
			root.value("postProcess", json::object()),
			ScenePostProcessSettings{}
		);
		for (const json& source : root.at("entities")) {
			SceneEntity entity{};
			entity.id = source.value("id", uint64_t{});
			entity.parentId = source.value("parentId", uint64_t{});
			entity.name = source.value("name", std::string("Entity"));
			entity.active = source.value("active", true);
			entity.locked = source.value("locked", false);
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
				if (transform.contains("rotate")) {
					entity.transform.rotate = JsonToVector(
						transform.at("rotate"),
						entity.transform.rotate
					);
				}
				if (transform.contains("translate")) {
					entity.transform.translate = JsonToVector(
						transform.at("translate"),
						entity.transform.translate
					);
				}
			}
			if (entity.id != 0) {
				SynchronizeLegacyRendererFields(entity);
				entities_.push_back(std::move(entity));
			}
		}
	}
	catch (...) {
		entities_.clear();
		return false;
	}

	RebuildNextId();
	ValidateHierarchy();
	dirty_ = false;
	return true;
}

void SceneDocument::RebuildNextId() {
	nextId_ = 1;
	for (const SceneEntity& entity : entities_) {
		nextId_ = (std::max)(nextId_, entity.id + 1);
	}
}

void SceneDocument::ValidateHierarchy() {
	for (SceneEntity& entity : entities_) {
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
