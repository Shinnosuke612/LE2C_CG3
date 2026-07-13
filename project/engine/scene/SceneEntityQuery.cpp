// 役割: EntityのComponent検索と親を含む有効状態判定を実装する。
#include "SceneEntityQuery.h"

#include "SceneDocument.h"

#include <algorithm>

namespace SceneEntityQuery {
	SceneComponent* FindComponent(
		SceneEntity& entity,
		const char* componentName
	) {
		const auto found = std::find_if(
			entity.components.begin(),
			entity.components.end(),
			[componentName](const SceneComponent& component) {
				return component.type == componentName;
			}
		);
		return found == entity.components.end() ? nullptr : &(*found);
	}

	const SceneComponent* FindComponent(
		const SceneEntity& entity,
		const char* componentName
	) {
		const auto found = std::find_if(
			entity.components.begin(),
			entity.components.end(),
			[componentName](const SceneComponent& component) {
				return component.type == componentName;
			}
		);
		return found == entity.components.end() ? nullptr : &(*found);
	}

	const SceneComponent* FindEnabledComponent(
		const SceneEntity& entity,
		const char* componentName
	) {
		const auto found = std::find_if(
			entity.components.begin(),
			entity.components.end(),
			[componentName](const SceneComponent& component) {
				return component.enabled && component.type == componentName;
			}
		);
		return found == entity.components.end() ? nullptr : &(*found);
	}

	bool HasComponent(
		const SceneEntity& entity,
		const char* componentName
	) {
		return FindEnabledComponent(entity, componentName) != nullptr;
	}

	bool IsEntityActiveInHierarchy(
		const SceneDocument& document,
		const SceneEntity& entity
	) {
		if (!entity.active) {
			return false;
		}

		const SceneEntity* parent = document.FindEntity(entity.parentId);
		while (parent) {
			if (!parent->active) {
				return false;
			}
			parent = document.FindEntity(parent->parentId);
		}
		return true;
	}
}
