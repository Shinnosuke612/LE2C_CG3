// 役割: SceneDocument内のEntityとComponentを検索する操作を定義する。
#pragma once

class SceneDocument;
struct SceneComponent;
struct SceneEntity;

namespace SceneEntityQuery {
	SceneComponent* FindComponent(
		SceneEntity& entity,
		const char* componentName
	);
	const SceneComponent* FindComponent(
		const SceneEntity& entity,
		const char* componentName
	);

	const SceneComponent* FindEnabledComponent(
		const SceneEntity& entity,
		const char* componentName
	);

	bool HasComponent(
		const SceneEntity& entity,
		const char* componentName
	);

	bool IsEntityActiveInHierarchy(
		const SceneDocument& document,
		const SceneEntity& entity
	);
}
