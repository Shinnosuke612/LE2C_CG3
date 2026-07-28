// 役割: Post Process Profileの実行時選択をDocumentへ書き戻さずに管理する。
#include "ScenePostProcessProfileSystem.h"

#include "../../../engine/scene/SceneDocument.h"
#include "../../../engine/scene/SceneEntityQuery.h"

#include <algorithm>
#include <iterator>

namespace {
	const SceneEntity* ResolveManager(
		const SceneDocument& document,
		uint64_t entityId,
		const std::string& entityName
	) {
		const SceneEntity* entity = entityId != 0
			? document.FindEntity(entityId)
			: nullptr;
		if (!entity && !entityName.empty()) {
			entity = document.FindEntityByName(entityName);
		}
		if (!entity ||
			!SceneEntityQuery::IsEntityActiveInHierarchy(document, *entity) ||
			!SceneEntityQuery::FindEnabledComponent(*entity, "PostProcessProfileManager")) {
			return nullptr;
		}
		return entity;
	}
}

void ScenePostProcessProfileSystem::ApplyBaseline(const SceneDocument& document) {
	effectiveSettings_ = document.GetPostProcessSettings();
	activeManagerEntityId_ = 0;
	activeProfileId_.clear();
	++generation_;
}

void ScenePostProcessProfileSystem::Reset(const SceneDocument* document) {
	if (document) {
		ApplyBaseline(*document);
		return;
	}
	effectiveSettings_ = {};
	activeManagerEntityId_ = 0;
	activeProfileId_.clear();
	++generation_;
}

void ScenePostProcessProfileSystem::Sync(const SceneDocument& document) {
	if (activeManagerEntityId_ == 0) {
		return;
	}
	const SceneEntity* manager = ResolveManager(
		document, activeManagerEntityId_, {}
	);
	if (!manager) {
		ApplyBaseline(document);
		return;
	}
	const SceneComponent* component = SceneEntityQuery::FindEnabledComponent(
		*manager, "PostProcessProfileManager"
	);
	const auto profile = std::find_if(
		component->postProcessProfiles.begin(),
		component->postProcessProfiles.end(),
		[this](const ScenePostProcessProfile& candidate) {
			return candidate.id == activeProfileId_;
		}
	);
	if (profile == component->postProcessProfiles.end()) {
		ApplyBaseline(document);
	}
}

void ScenePostProcessProfileSystem::ApplyEventResult(
	const SceneDocument& document,
	const SceneEventResult& result
) {
	const ScenePostProcessRequest& request = result.postProcessRequest;
	if (request.type == ScenePostProcessRequestType::None) {
		return;
	}
	if (request.type == ScenePostProcessRequestType::ResetToSceneDefault) {
		ApplyBaseline(document);
		return;
	}
	const SceneEntity* manager = ResolveManager(
		document, request.managerEntityId, request.managerEntityName
	);
	if (!manager) {
		return;
	}
	const SceneComponent* component = SceneEntityQuery::FindEnabledComponent(
		*manager, "PostProcessProfileManager"
	);
	if (request.type == ScenePostProcessRequestType::NextProfile) {
		if (component->postProcessProfiles.empty()) {
			return;
		}
		const bool continuesCurrentManager =
			activeManagerEntityId_ == manager->id;
		auto current = continuesCurrentManager
			? std::find_if(
				component->postProcessProfiles.begin(),
				component->postProcessProfiles.end(),
				[this](const ScenePostProcessProfile& candidate) {
					return candidate.id == activeProfileId_;
				}
			)
			: component->postProcessProfiles.end();
		const auto next = current == component->postProcessProfiles.end() ||
			std::next(current) == component->postProcessProfiles.end()
			? component->postProcessProfiles.begin()
			: std::next(current);
		effectiveSettings_ = next->settings;
		activeManagerEntityId_ = manager->id;
		activeProfileId_ = next->id;
		++generation_;
		return;
	}
	if (
		request.type != ScenePostProcessRequestType::SetProfile ||
		request.profileId.empty()
	) {
		return;
	}
	const auto profile = std::find_if(
		component->postProcessProfiles.begin(),
		component->postProcessProfiles.end(),
		[&request](const ScenePostProcessProfile& candidate) {
			return candidate.id == request.profileId;
		}
	);
	if (profile == component->postProcessProfiles.end()) {
		return;
	}
	effectiveSettings_ = profile->settings;
	activeManagerEntityId_ = manager->id;
	activeProfileId_ = profile->id;
	++generation_;
}
