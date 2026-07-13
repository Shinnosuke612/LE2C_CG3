// 役割: ゲームプレイ中のアニメーション操作とデバッグ描画を実装する。
#include "GamePlayScene.h"

#include "../../engine/debug/DebugRenderer.h"
#include "../../engine/scene/SceneDocument.h"
#include "../../engine/scene/SceneEntityQuery.h"
#include "../../engine/scene/SceneManager.h"
#include "../../engine/3d/Object3d.h"

#include "../../externals/imgui/imgui.h"

#include <algorithm>
#include <cstring>

using SceneEntityQuery::FindEnabledComponent;
using SceneEntityQuery::IsEntityActiveInHierarchy;

#if defined(_DEBUG) || defined(DEVELOPMENT)
void GamePlayScene::DrawAnimationControls(const SceneDocument& document) {
	ImGui::SeparatorText("Animation");
	std::vector<const SceneEntity*> animatorEntities;
	for (const SceneEntity& entity : document.GetEntities()) {
		if (FindEnabledComponent(entity, "Animator")) {
			animatorEntities.push_back(&entity);
		}
	}

	if (animatorEntities.empty()) {
		animationControlEntityId_ = 0;
		ImGui::TextDisabled("No enabled Animator components");
		return;
	}

	auto selected = std::find_if(
		animatorEntities.begin(),
		animatorEntities.end(),
		[this](const SceneEntity* entity) {
			return entity->id == animationControlEntityId_;
		}
	);
	if (selected == animatorEntities.end()) {
		selected = animatorEntities.begin();
		animationControlEntityId_ = (*selected)->id;
		const auto runtime = sceneModelObjects_.find(animationControlEntityId_);
		if (runtime != sceneModelObjects_.end()) {
			animationControlTransitionDuration_ =
				runtime->second.animatorTransitionDuration;
		}
	}

	const SceneEntity* selectedEntity = *selected;
	if (ImGui::BeginCombo("Animator Entity", selectedEntity->name.c_str())) {
		for (const SceneEntity* entity : animatorEntities) {
			const std::string itemLabel = entity->name + "##" +
				std::to_string(entity->id);
			if (ImGui::Selectable(
				itemLabel.c_str(),
				entity->id == animationControlEntityId_
			)) {
				animationControlEntityId_ = entity->id;
				selectedEntity = entity;
				const auto runtime = sceneModelObjects_.find(entity->id);
				if (runtime != sceneModelObjects_.end()) {
					animationControlTransitionDuration_ =
						runtime->second.animatorTransitionDuration;
				}
			}
		}
		ImGui::EndCombo();
	}

	const auto runtime = sceneModelObjects_.find(animationControlEntityId_);
	if (
		runtime == sceneModelObjects_.end() ||
		!runtime->second.object ||
		!runtime->second.object->HasAnimation()
	) {
		ImGui::TextDisabled("The selected model has no animation clips");
		return;
	}

	Object3d* object = runtime->second.object;
	size_t currentClip = object->GetAnimationClipIndex();
	std::string currentClipName = object->GetAnimationClipName(currentClip);
	if (currentClipName.empty()) {
		currentClipName = "Clip " + std::to_string(currentClip);
	}
	if (ImGui::BeginCombo("Clip", currentClipName.c_str())) {
		for (size_t index = 0; index < object->GetAnimationClipCount(); ++index) {
			std::string clipName = object->GetAnimationClipName(index);
			if (clipName.empty()) {
				clipName = "Clip " + std::to_string(index);
			}
			const std::string itemLabel = clipName + "##" +
				std::to_string(index);
			if (ImGui::Selectable(itemLabel.c_str(), index == currentClip)) {
				if (object->PlayAnimation(
					index,
					animationControlTransitionDuration_,
					true
				)) {
					currentClip = index;
				}
			}
		}
		ImGui::EndCombo();
	}

	bool playing = object->IsAnimationPlaying();
	if (ImGui::Checkbox("Playing", &playing)) {
		object->SetAnimationPlaying(playing);
	}
	bool looping = object->IsAnimationLooping();
	if (ImGui::Checkbox("Loop", &looping)) {
		object->SetAnimationLoop(looping);
	}
	float speed = object->GetAnimationSpeed();
	if (ImGui::DragFloat("Speed", &speed, 0.01f, -8.0f, 8.0f)) {
		object->SetAnimationSpeed(speed);
	}
	ImGui::DragFloat(
		"Transition Duration",
		&animationControlTransitionDuration_,
		0.01f,
		0.0f,
		10.0f
	);
	animationControlTransitionDuration_ = (std::max)(
		animationControlTransitionDuration_,
		0.0f
	);

	const char* blendCurve =
		object->GetAnimationBlendCurve() == AnimationBlendCurve::Linear
		? "Linear"
		: "SmoothStep";
	if (ImGui::BeginCombo("Blend Curve", blendCurve)) {
		for (const char* candidate : { "Linear", "SmoothStep" }) {
			if (ImGui::Selectable(candidate, std::strcmp(candidate, blendCurve) == 0)) {
				object->SetAnimationBlendCurve(
					std::strcmp(candidate, "Linear") == 0
					? AnimationBlendCurve::Linear
					: AnimationBlendCurve::SmoothStep
				);
			}
		}
		ImGui::EndCombo();
	}

	float time = object->GetAnimationTime();
	const float duration = object->GetAnimationDuration();
	if (ImGui::SliderFloat("Time", &time, 0.0f, duration)) {
		object->SetAnimationTime(time);
	}
	if (ImGui::Button("Restart")) {
		object->PlayAnimation(currentClip, 0.0f, true);
	}
	if (object->IsAnimationTransitioning()) {
		ImGui::SameLine();
		ImGui::Text("Blend %.0f%%", object->GetAnimationBlendWeight() * 100.0f);
	}
}
#endif

void GamePlayScene::DrawColliderDebug() const {
	DebugRenderer* debugRenderer = DebugRenderer::GetInstance();
	auto drawCollider = [debugRenderer](
		const Collider* collider,
		const Vector4& color,
		const std::string& mode,
		uint32_t segments
	) {
		if (!collider) {
			return;
		}
		const bool drawWire = mode != "Solid";
		const bool drawSolid = mode != "Wireframe";
		Vector4 solidColor = color;
		solidColor.w = (std::min)(solidColor.w, 0.25f);
		if (collider->GetType() == Collider::Type::Sphere) {
			const auto& sphere = static_cast<const SphereCollider&>(*collider);
			if (drawSolid) {
				debugRenderer->AddSolidSphere(
					sphere.GetWorldCenter(), sphere.GetRadius(), solidColor, segments
				);
			}
			if (drawWire) {
				debugRenderer->AddSphere(
					sphere.GetWorldCenter(), sphere.GetRadius(), color, segments
				);
			}
			return;
		}
		const auto& box = static_cast<const OBBCollider&>(*collider);
		const OBBCollider::OBB obb = box.GetOBB();
		if (drawSolid) {
			debugRenderer->AddSolidOBB(
				obb.center, obb.axis, obb.halfSize, solidColor
			);
		}
		if (drawWire) {
			debugRenderer->AddOBB(obb.center, obb.axis, obb.halfSize, color);
		}
	};
	SceneDocument* document = sceneManager_
		? sceneManager_->GetActiveSceneDocument()
		: nullptr;
	if (document) {
		for (const SceneEntity& entity : document->GetEntities()) {
			const auto found = sceneModelObjects_.find(entity.id);
			if (
				found != sceneModelObjects_.end() &&
				found->second.hasCollider &&
				IsEntityActiveInHierarchy(*document, entity) &&
				found->second.colliderDebugVisible
			) {
				drawCollider(
					found->second.collider,
					found->second.colliderDebugColor,
					found->second.colliderDebugDrawMode,
					found->second.colliderDebugSegments
				);
			}
		}
	}
}

void GamePlayScene::LoadSceneDebugSettings() {
	const SceneDocument* document = sceneManager_
		? sceneManager_->GetActiveSceneDocument()
		: nullptr;
	if (!document) {
		return;
	}

	const SceneDebugSettings& settings = document->GetDebugSettings();
	showCameraDebug_ = settings.showCameraDirection;
	showColliderDebug_ = settings.showColliders;
	showCameraPathDebug_ = settings.showCameraPath;
	showCameraPathPointCameraDebug_ =
		settings.showCameraPathPointCameraDirection;
	showSkeletonDebug_ = settings.showSkeleton;
	showJointNames_ = settings.showJointNames;
	showJointAxes_ = settings.showJointAxes;
	jointRadius_ = settings.jointRadius;
	jointAxisLength_ = settings.jointAxisLength;
}

void GamePlayScene::SaveSceneDebugSettings() {
	SceneDocument* document = sceneManager_
		? sceneManager_->GetActiveSceneDocument()
		: nullptr;
	if (!document) {
		return;
	}

	SceneDebugSettings settings = document->GetDebugSettings();
	settings.showCameraDirection = showCameraDebug_;
	settings.showColliders = showColliderDebug_;
	settings.showCameraPath = showCameraPathDebug_;
	settings.showCameraPathPointCameraDirection =
		showCameraPathPointCameraDebug_;
	settings.showSkeleton = showSkeletonDebug_;
	settings.showJointNames = showJointNames_;
	settings.showJointAxes = showJointAxes_;
	settings.jointRadius = jointRadius_;
	settings.jointAxisLength = jointAxisLength_;
	document->SetDebugSettings(settings);
}
