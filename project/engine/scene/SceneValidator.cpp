// 役割: Entity ID、Hierarchy、Entity参照、Scene遷移先の整合性を検証する。
#include "SceneValidator.h"

#include "SceneCatalog.h"
#include "SceneDocument.h"

#include <algorithm>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace {
	bool HasErrors(const std::vector<SceneValidationIssue>& issues) {
		for (const SceneValidationIssue& issue : issues) {
			if (issue.severity == SceneValidationSeverity::Error) {
				return true;
			}
		}
		return false;
	}
}

bool SceneValidator::ValidateDocument(
	const SceneDocument& document,
	const SceneCatalog* catalog,
	const std::string& sceneId,
	const std::string& filePath,
	std::vector<SceneValidationIssue>& issues
) {
	const size_t firstIssueIndex = issues.size();
	auto addIssue = [
		&issues,
		&sceneId,
		&filePath
	](SceneValidationSeverity severity, uint64_t entityId, std::string message) {
		issues.push_back({ severity, sceneId, filePath, entityId, std::move(message) });
	};

	std::unordered_map<uint64_t, const SceneEntity*> entitiesById;
	for (const SceneEntity& entity : document.GetEntities()) {
		if (entity.id == 0) {
			addIssue(
				SceneValidationSeverity::Error,
				0,
				"Entity ID must be greater than zero"
			);
			continue;
		}
		if (!entitiesById.emplace(entity.id, &entity).second) {
			addIssue(
				SceneValidationSeverity::Error,
				entity.id,
				"Duplicate Entity ID"
			);
		}
	}

	std::unordered_set<uint64_t> reportedCycles;
	uint32_t directionalLightCount = 0;
	uint32_t pointLightCount = 0;
	uint32_t spotLightCount = 0;
	uint32_t spotShadowCount = 0;
	for (const SceneEntity& entity : document.GetEntities()) {
		if (!entity.teamName.empty() && !document.FindTeam(entity.teamName)) {
			addIssue(
				SceneValidationSeverity::Error,
				entity.id,
				"Entity references an unknown Team: " + entity.teamName
			);
		}
		if (entity.parentId == entity.id && entity.id != 0) {
			addIssue(
				SceneValidationSeverity::Error,
				entity.id,
				"Entity cannot be its own parent"
			);
		} else if (entity.parentId != 0 && !entitiesById.contains(entity.parentId)) {
			addIssue(
				SceneValidationSeverity::Error,
				entity.id,
				"Parent Entity does not exist: " + std::to_string(entity.parentId)
			);
		}

		std::unordered_set<uint64_t> visited;
		const SceneEntity* current = &entity;
		while (current && current->parentId != 0) {
			if (!visited.insert(current->id).second) {
				if (reportedCycles.insert(entity.id).second) {
					addIssue(
						SceneValidationSeverity::Error,
						entity.id,
						"Hierarchy contains a parent cycle"
					);
				}
				break;
			}
			const auto parent = entitiesById.find(current->parentId);
			current = parent == entitiesById.end() ? nullptr : parent->second;
		}

		bool activeInHierarchy = entity.active;
		std::unordered_set<uint64_t> activeVisited;
		const SceneEntity* activeParent = &entity;
		while (activeInHierarchy && activeParent && activeParent->parentId != 0) {
			if (!activeVisited.insert(activeParent->id).second) {
				activeInHierarchy = false;
				break;
			}
			const auto parent = entitiesById.find(activeParent->parentId);
			activeParent = parent == entitiesById.end() ? nullptr : parent->second;
			if (activeParent && !activeParent->active) {
				activeInHierarchy = false;
			}
		}
		for (const SceneComponent& component : entity.components) {
			auto validateEntityReference = [
				&entitiesById,
				&addIssue,
				&entity
			](uint64_t targetId, const char* label) {
				if (targetId != 0 && !entitiesById.contains(targetId)) {
					addIssue(
						SceneValidationSeverity::Error,
						entity.id,
						std::string(label) + " Entity does not exist: " +
						std::to_string(targetId)
					);
				}
			};
			if (component.type == "Light") {
				if (
					component.lightType != "Directional" &&
					component.lightType != "Point" &&
					component.lightType != "Spot"
				) {
					addIssue(
						SceneValidationSeverity::Error,
						entity.id,
						"Light has an unknown lightType: " + component.lightType
					);
					continue;
				}
				if (!component.enabled || !activeInHierarchy) {
					continue;
				}
				if (component.lightType == "Directional") {
					++directionalLightCount;
					if (directionalLightCount > 1) {
						addIssue(
							SceneValidationSeverity::Warning,
							entity.id,
							"Only the first active Directional Light is rendered"
						);
					}
				} else if (component.lightType == "Point") {
					++pointLightCount;
					if (pointLightCount > 16) {
						addIssue(
							SceneValidationSeverity::Warning,
							entity.id,
							"Only the first 16 active Point Lights are rendered"
						);
					}
				} else {
					++spotLightCount;
					if (spotLightCount > 8) {
						addIssue(
							SceneValidationSeverity::Warning,
							entity.id,
							"Only the first 8 active Spot Lights are rendered"
						);
					}
					if (component.lightCastsShadow) {
						++spotShadowCount;
						if (spotShadowCount > 4) {
							addIssue(
								SceneValidationSeverity::Warning,
								entity.id,
								"Only the first 4 active Spot Light shadows are rendered"
							);
						}
					}
				}
			} else if (component.type == "MonitorRenderer") {
				validateEntityReference(
					component.monitorCameraEntityId,
					"Monitor camera"
				);
			} else if (component.type == "AgentBehavior") {
				validateEntityReference(
					component.agentBoundsEntityId,
					"Agent bounds"
				);
				validateEntityReference(
					component.agentAttractorEntityId,
					"Agent attractor"
				);
			} else if (component.type == "EntityReference") {
				if (component.entityReferenceName.empty()) {
					addIssue(
						SceneValidationSeverity::Error,
						entity.id,
						"EntityReference referenceName is empty"
					);
				}
				const SceneEntityReference& reference =
					component.entityReferenceTarget;
				if (reference.entityId == 0) {
					addIssue(
						SceneValidationSeverity::Error,
						entity.id,
						"EntityReference target Entity ID is empty"
					);
				} else if (
					reference.sceneId.empty() ||
					reference.sceneId == sceneId
				) {
					validateEntityReference(
						reference.entityId,
						"EntityReference target"
					);
				}
				if (
					reference.sceneId.empty() &&
					!reference.instanceKey.empty()
				) {
					addIssue(
						SceneValidationSeverity::Error,
						entity.id,
						"EntityReference instanceKey requires a target sceneId"
					);
				} else if (
					!reference.sceneId.empty() &&
					catalog &&
					!catalog->Find(reference.sceneId)
				) {
					addIssue(
						SceneValidationSeverity::Error,
						entity.id,
						"EntityReference target Scene is not registered: " +
						reference.sceneId
					);
				}
			} else if (component.type == "SceneTransition") {
				if (component.sceneTransitionTargetSceneId.empty()) {
					addIssue(
						SceneValidationSeverity::Error,
						entity.id,
						"SceneTransition targetSceneId is empty"
					);
				} else if (catalog &&
					!catalog->Find(component.sceneTransitionTargetSceneId)) {
					addIssue(
						SceneValidationSeverity::Error,
						entity.id,
						"SceneTransition target is not registered: " +
						component.sceneTransitionTargetSceneId
					);
				}
			}
		}
	}

	for (size_t index = firstIssueIndex; index < issues.size(); ++index) {
		if (issues[index].severity == SceneValidationSeverity::Error) {
			return false;
		}
	}
	return true;
}

bool SceneValidator::ValidateCatalog(
	const SceneCatalog& catalog,
	std::vector<SceneValidationIssue>& issues
) {
	issues.clear();
	for (const SceneDescriptor& descriptor : catalog.GetScenes()) {
		SceneDocument document;
		if (!document.Load(descriptor.filePath)) {
			issues.push_back({
				SceneValidationSeverity::Error,
				descriptor.id,
				descriptor.filePath,
				0,
				"Scene could not be loaded: " + document.GetLastLoadError()
			});
			continue;
		}
		if (!document.GetLastLoadError().empty()) {
			issues.push_back({
				SceneValidationSeverity::Warning,
				descriptor.id,
				descriptor.filePath,
				0,
				document.GetLastLoadError()
			});
		} else if (document.IsDirty()) {
			issues.push_back({
				SceneValidationSeverity::Warning,
				descriptor.id,
				descriptor.filePath,
				0,
				"Scene JSON was migrated in memory; save it in the Editor"
			});
		}
		ValidateDocument(
			document,
			&catalog,
			descriptor.id,
			descriptor.filePath,
			issues
		);
		for (const SceneEntity& entity : document.GetEntities()) {
			for (const SceneComponent& component : entity.components) {
				if (
					component.type != "EntityReference" ||
					component.entityReferenceTarget.entityId == 0 ||
					component.entityReferenceTarget.sceneId.empty() ||
					component.entityReferenceTarget.sceneId == descriptor.id
				) {
					continue;
				}
				const SceneDescriptor* targetDescriptor = catalog.Find(
					component.entityReferenceTarget.sceneId
				);
				if (!targetDescriptor) {
					continue;
				}
				SceneDocument targetDocument;
				if (
					targetDocument.Load(targetDescriptor->filePath) &&
					!targetDocument.FindEntity(
						component.entityReferenceTarget.entityId
					)
				) {
					issues.push_back({
						SceneValidationSeverity::Error,
						descriptor.id,
						descriptor.filePath,
						entity.id,
						"EntityReference target Entity does not exist in Scene " +
							targetDescriptor->id + ": " +
							std::to_string(
								component.entityReferenceTarget.entityId
							)
					});
				}
			}
		}
	}
	return !HasErrors(issues);
}

std::string SceneValidator::FormatIssues(
	const std::vector<SceneValidationIssue>& issues,
	size_t maxIssueCount
) {
	std::ostringstream output;
	const size_t count = (std::min)(issues.size(), maxIssueCount);
	for (size_t index = 0; index < count; ++index) {
		const SceneValidationIssue& issue = issues[index];
		if (index != 0) {
			output << '\n';
		}
		output << (issue.severity == SceneValidationSeverity::Error
			? "Error"
			: "Warning");
		if (!issue.sceneId.empty()) {
			output << " [" << issue.sceneId << "]";
		}
		if (issue.entityId != 0) {
			output << " Entity " << issue.entityId;
		}
		output << ": " << issue.message;
	}
	if (issues.size() > count) {
		output << '\n' << "... and " << (issues.size() - count) << " more";
	}
	return output.str();
}
