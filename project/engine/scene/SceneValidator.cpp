// 役割: Entity ID、Hierarchy、Entity参照、Scene遷移先の整合性を検証する。
#include "SceneValidator.h"

#include "SceneCatalog.h"
#include "SceneDocument.h"
#include "SceneEntityQuery.h"
#include "SceneInputKey.h"

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
	uint32_t cameraSwitcherCount = 0;
	std::unordered_map<uint64_t, std::unordered_set<uint64_t>> prefabLocalIds;
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
		std::unordered_set<uint64_t> componentLocalIds;
		for (const SceneComponent& component : entity.components) {
			if (component.localId == 0) {
				addIssue(
					SceneValidationSeverity::Error,
					entity.id,
					"Component local ID must be greater than zero: " +
						component.type
				);
			} else if (!componentLocalIds.insert(component.localId).second) {
				addIssue(
					SceneValidationSeverity::Error,
					entity.id,
					"Entity contains a duplicate Component local ID: " +
						std::to_string(component.localId)
				);
			}
		}
		const bool hasPrefabMetadata =
			!entity.prefabLinks.empty() ||
			!entity.prefabAssetId.empty() ||
			!entity.prefabSourcePath.empty() ||
			entity.prefabInstanceRootId != 0 ||
			entity.prefabLocalId != 0;
		if (hasPrefabMetadata) {
			if (
				(
					entity.prefabAssetId.empty() &&
					entity.prefabSourcePath.empty()
				) ||
				entity.prefabInstanceRootId == 0 ||
				entity.prefabLocalId == 0
			) {
				addIssue(
					SceneValidationSeverity::Error,
					entity.id,
					"Prefab instance metadata is incomplete"
				);
			} else {
				const auto root = entitiesById.find(entity.prefabInstanceRootId);
				if (root == entitiesById.end()) {
					addIssue(
						SceneValidationSeverity::Error,
						entity.id,
						"Prefab instance root does not exist: " +
						std::to_string(entity.prefabInstanceRootId)
					);
				} else {
					const bool assetLinkMatches = !entity.prefabAssetId.empty()
						? root->second->prefabAssetId == entity.prefabAssetId
						: root->second->prefabAssetId.empty() &&
							root->second->prefabSourcePath ==
								entity.prefabSourcePath;
					if (
						root->second->prefabInstanceRootId !=
							entity.prefabInstanceRootId ||
						!assetLinkMatches
					) {
					addIssue(
						SceneValidationSeverity::Error,
						entity.id,
						"Prefab instance root metadata does not match"
					);
					}
				}
				if (!prefabLocalIds[entity.prefabInstanceRootId].insert(
					entity.prefabLocalId
				).second) {
					addIssue(
						SceneValidationSeverity::Error,
						entity.id,
						"Prefab instance contains a duplicate local Entity ID"
					);
				}
			}
		}
		if (!entity.prefabLinks.empty()) {
			const ScenePrefabLink& active = entity.prefabLinks.front();
			if (
				active.assetId != entity.prefabAssetId ||
				active.sourcePath != entity.prefabSourcePath ||
				active.instanceRootId != entity.prefabInstanceRootId ||
				active.localId != entity.prefabLocalId
			) {
				addIssue(
					SceneValidationSeverity::Error,
					entity.id,
					"Active Prefab link does not match compatibility metadata"
				);
			}
			for (size_t linkIndex = 1;
				linkIndex < entity.prefabLinks.size();
				++linkIndex) {
				const ScenePrefabLink& link = entity.prefabLinks[linkIndex];
				if (
					(link.assetId.empty() && link.sourcePath.empty()) ||
					link.instanceRootId == 0 ||
					link.localId == 0
				) {
					addIssue(
						SceneValidationSeverity::Error,
						entity.id,
						"Nested Prefab link metadata is incomplete"
					);
					continue;
				}
				const auto root = entitiesById.find(link.instanceRootId);
				if (root == entitiesById.end()) {
					addIssue(
						SceneValidationSeverity::Error,
						entity.id,
						"Nested Prefab instance root does not exist: " +
						std::to_string(link.instanceRootId)
					);
				} else {
					const auto rootLink = std::find_if(
						root->second->prefabLinks.begin(),
						root->second->prefabLinks.end(),
						[&link](const ScenePrefabLink& candidate) {
							return candidate.instanceRootId ==
								link.instanceRootId;
						}
					);
					const bool assetMatches =
						rootLink != root->second->prefabLinks.end() &&
						(!link.assetId.empty()
							? rootLink->assetId == link.assetId
							: rootLink->assetId.empty() &&
								rootLink->sourcePath == link.sourcePath);
					if (!assetMatches) {
						addIssue(
							SceneValidationSeverity::Error,
							entity.id,
							"Nested Prefab root metadata does not match"
						);
					}
				}
				if (!prefabLocalIds[link.instanceRootId].insert(
					link.localId
				).second) {
					addIssue(
						SceneValidationSeverity::Error,
						entity.id,
						"Nested Prefab contains a duplicate local Entity ID"
					);
				}
			}
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
			} else if (component.type == "CameraSwitcher") {
				if (component.enabled && activeInHierarchy) {
					++cameraSwitcherCount;
					if (cameraSwitcherCount > 1) {
						addIssue(
							SceneValidationSeverity::Warning,
							entity.id,
							"Only the first active CameraSwitcher is used"
						);
					}
				}
				std::unordered_set<uint64_t> registeredCameraIds;
				for (const SceneCameraSwitchEntry& entry :
					component.cameraSwitchEntries) {
					validateEntityReference(
						entry.cameraEntityId,
						"CameraSwitcher camera"
					);
					const SceneEntity* cameraEntity = entry.cameraEntityId != 0
						? document.FindEntity(entry.cameraEntityId)
						: nullptr;
					if (
						!entry.cameraEntityName.empty() &&
						(!cameraEntity ||
							cameraEntity->name != entry.cameraEntityName)
					) {
						cameraEntity = document.FindEntityByName(
							entry.cameraEntityName
						);
					}
					if (!cameraEntity) {
						addIssue(
							SceneValidationSeverity::Error,
							entity.id,
							"CameraSwitcher contains an unresolved camera"
						);
						continue;
					}
					const bool hasCamera = std::any_of(
						cameraEntity->components.begin(),
						cameraEntity->components.end(),
						[](const SceneComponent& candidate) {
							return candidate.enabled && candidate.type == "Camera";
						}
					);
					if (!hasCamera) {
						addIssue(
							SceneValidationSeverity::Error,
							entity.id,
							"CameraSwitcher target has no enabled Camera: " +
								cameraEntity->name
						);
					} else if (!registeredCameraIds.insert(cameraEntity->id).second) {
						addIssue(
							SceneValidationSeverity::Warning,
							entity.id,
							"CameraSwitcher contains a duplicate camera: " +
								cameraEntity->name
						);
					}
				}
				if (component.cameraSwitchEntries.empty()) {
					addIssue(
						SceneValidationSeverity::Warning,
						entity.id,
						"CameraSwitcher has no registered cameras"
					);
				}
			} else if (component.type == "ThirdPersonCamera") {
				const bool hasCamera = std::any_of(
					entity.components.begin(),
					entity.components.end(),
					[](const SceneComponent& candidate) {
						return candidate.enabled && candidate.type == "Camera";
					}
				);
				if (!hasCamera) {
					addIssue(
						SceneValidationSeverity::Error,
						entity.id,
						"ThirdPersonCamera requires Camera on the same Entity"
					);
				}
				validateEntityReference(
					component.thirdPersonTargetEntityId,
					"ThirdPerson target"
				);
				if (
					component.thirdPersonTargetEntityId == 0 &&
					!component.thirdPersonTargetEntityName.empty() &&
					!document.FindEntityByName(
						component.thirdPersonTargetEntityName
					)
				) {
					addIssue(
						SceneValidationSeverity::Error,
						entity.id,
						"ThirdPerson target name cannot be resolved: " +
							component.thirdPersonTargetEntityName
					);
				}
				if (
					component.thirdPersonYawReference != "World" &&
					component.thirdPersonYawReference != "Target"
				) {
					addIssue(
						SceneValidationSeverity::Error,
						entity.id,
						"ThirdPersonCamera has an unknown yawReference: " +
							component.thirdPersonYawReference
					);
				}
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
			} else if (component.type == "StatSet") {
				std::unordered_set<std::string> statIds;
				for (const SceneStatDefinition& stat : component.stats) {
					if (stat.id.empty()) {
						addIssue(
							SceneValidationSeverity::Error,
							entity.id,
							"StatSet contains an empty Stat Id"
						);
					} else if (!statIds.insert(stat.id).second) {
						addIssue(
							SceneValidationSeverity::Error,
							entity.id,
							"StatSet contains a duplicate Stat Id: " + stat.id
						);
					}
					if (stat.maxValue < stat.minValue) {
						addIssue(
							SceneValidationSeverity::Error,
							entity.id,
							"Stat max is below min: " + stat.id
						);
					}
				}
			} else if (component.type == "EventTrigger") {
				auto validateCameraEventTarget = [
					&addIssue,
					&document,
					&entity
				](
					uint64_t targetId,
					const std::string& targetName,
					const char* componentName,
					const char* label,
					bool checkPathPoints
				) {
					if (targetId == 0 && targetName.empty()) {
						addIssue(
							SceneValidationSeverity::Warning,
							entity.id,
							std::string(label) + " target is not set"
						);
						return;
					}
					const SceneEntity* target = targetId != 0
						? document.FindEntity(targetId)
						: nullptr;
					if (!target && !targetName.empty()) {
						target = document.FindEntityByName(targetName);
					}
					const SceneComponent* targetComponent = target
						? SceneEntityQuery::FindEnabledComponent(
							*target, componentName
						)
						: nullptr;
					if (
						!target || !targetComponent ||
						!SceneEntityQuery::IsEntityActiveInHierarchy(document, *target)
					) {
						addIssue(
							SceneValidationSeverity::Error,
							entity.id,
							std::string(label) +
								" target is unresolved, inactive, or disabled"
						);
						return;
					}
					if (!checkPathPoints) {
						return;
					}
					const bool hasPoint = std::any_of(
						document.GetEntities().begin(),
						document.GetEntities().end(),
						[&document, target](const SceneEntity& candidate) {
							return candidate.parentId == target->id &&
								SceneEntityQuery::IsEntityActiveInHierarchy(
									document, candidate
								) &&
								SceneEntityQuery::FindEnabledComponent(
									candidate, "CameraPathPoint"
								);
						}
					);
					if (!hasPoint) {
						addIssue(
							SceneValidationSeverity::Warning,
							entity.id,
							"CameraPath Event target has no active CameraPathPoint"
						);
					}
					if (!targetComponent->cameraPathTargetCameraName.empty()) {
						const SceneEntity* targetCamera = document.FindEntityByName(
							targetComponent->cameraPathTargetCameraName
						);
						if (
							!targetCamera ||
							!SceneEntityQuery::IsEntityActiveInHierarchy(
								document, *targetCamera
							) ||
							!SceneEntityQuery::FindEnabledComponent(
								*targetCamera, "Camera"
							)
						) {
							addIssue(
								SceneValidationSeverity::Warning,
								entity.id,
								"CameraPath Event target has an unresolved target Camera"
							);
						}
					}
				};
				for (const SceneEventBinding& binding : component.eventBindings) {
					const bool triggerUsesTarget =
						binding.triggerType == "OnStatReachedMin" ||
						binding.triggerType == "OnStatCompare" ||
						binding.triggerType == "OnPositionReached";
					if (triggerUsesTarget) {
						validateEntityReference(
							binding.targetEntityId,
							"Event target"
						);
					}
					if (
						binding.triggerType == "OnKeyPressed" &&
						ResolveSceneInputKey(binding.triggerKey) == 0
					) {
						addIssue(
							SceneValidationSeverity::Warning,
							entity.id,
							"OnKeyPressed Event has an unsupported key: " +
								binding.triggerKey
						);
					}
					if (binding.triggerType == "OnCameraPathCompleted") {
						validateCameraEventTarget(
							binding.targetEntityId,
							binding.targetEntityName,
							"CameraPath",
							"OnCameraPathCompleted",
							true
						);
					}
					for (const SceneEventAction& action : binding.actions) {
						const bool actionUsesTarget =
							action.type == "ModifyStat" ||
							action.type == "SetEntityActive" ||
							action.type == "InstantiatePrefab" ||
							action.type == "ChangeState";
						if (actionUsesTarget) {
							validateEntityReference(
								action.targetEntityId,
								"Event action target"
							);
						}
						if (
							action.type == "PlayCameraPath" ||
							action.type == "StopCameraPath"
						) {
							validateCameraEventTarget(
								action.targetEntityId,
								action.targetEntityName,
								"CameraPath",
								action.type.c_str(),
								action.type == "PlayCameraPath"
							);
						} else if (action.type == "SelectCamera") {
							validateCameraEventTarget(
								action.targetEntityId,
								action.targetEntityName,
								"Camera",
								"SelectCamera",
								false
							);
						}
						if (
							action.type == "SetPostProcessProfile" ||
							action.type == "NextPostProcessProfile"
						) {
							const SceneEntity* manager =
								action.postProcessManagerEntityId != 0
								? document.FindEntity(action.postProcessManagerEntityId)
								: nullptr;
							if (!manager && !action.postProcessManagerEntityName.empty()) {
								manager = document.FindEntityByName(
									action.postProcessManagerEntityName
								);
							}
							const SceneComponent* managerComponent = nullptr;
							if (manager) {
								const auto componentIterator = std::find_if(
									manager->components.begin(),
									manager->components.end(),
									[](const SceneComponent& candidate) {
										return candidate.enabled &&
											candidate.type == "PostProcessProfileManager";
									}
								);
								if (componentIterator != manager->components.end()) {
									managerComponent = &*componentIterator;
								}
							}
							if (!managerComponent) {
								addIssue(
									SceneValidationSeverity::Error,
									entity.id,
									"Event PostProcess action has an unresolved or disabled manager"
								);
							} else if (action.type == "NextPostProcessProfile" &&
								managerComponent->postProcessProfiles.empty()) {
								addIssue(
									SceneValidationSeverity::Warning,
									entity.id,
									"NextPostProcessProfile manager has no profiles"
								);
							} else if (action.type == "SetPostProcessProfile") {
								const bool profileExists = std::any_of(
									managerComponent->postProcessProfiles.begin(),
									managerComponent->postProcessProfiles.end(),
									[&action](const ScenePostProcessProfile& profile) {
										return profile.id == action.postProcessProfileId;
									}
								);
								if (!profileExists) {
									addIssue(
										SceneValidationSeverity::Error,
										entity.id,
										"SetPostProcessProfile profile cannot be resolved: " +
											action.postProcessProfileId
									);
								}
							}
						}
						if (
							action.type == "ChangeState" &&
							action.stateName.empty()
						) {
							addIssue(
								SceneValidationSeverity::Warning,
								entity.id,
								"ChangeState action has an empty state name"
							);
						}
						if (
							action.type == "SceneTransition" &&
							!action.sceneId.empty() &&
							catalog &&
							!catalog->Find(action.sceneId)
						) {
							addIssue(
								SceneValidationSeverity::Error,
								entity.id,
								"Event SceneTransition target is not registered: " +
									action.sceneId
							);
						}
					}
				}
			} else if (component.type == "PostProcessProfileManager") {
				std::unordered_set<std::string> profileIds;
				for (const ScenePostProcessProfile& profile :
					component.postProcessProfiles) {
					if (profile.id.empty()) {
						addIssue(
							SceneValidationSeverity::Error,
							entity.id,
							"PostProcessProfileManager contains an empty Profile Id"
						);
					} else if (!profileIds.insert(profile.id).second) {
						addIssue(
							SceneValidationSeverity::Error,
							entity.id,
							"PostProcessProfileManager contains a duplicate Profile Id: " +
								profile.id
						);
					}
					if (profile.label.empty()) {
						addIssue(
							SceneValidationSeverity::Warning,
							entity.id,
							"PostProcessProfileManager Profile has an empty label: " +
								profile.id
						);
					}
					const ScenePostProcessSettings& settings = profile.settings;
					if (
						settings.pixelationBlockSize < 1 ||
						settings.pixelationBlockSize > 64 ||
						settings.motionBlurStrength < 0.0f ||
						settings.motionBlurStrength > 1.0f ||
						settings.motionBlurSamples < 2 ||
						settings.motionBlurSamples > 32 ||
						settings.motionBlurMaxRadius < 0.0f ||
						settings.motionBlurMaxRadius > 64.0f ||
						settings.chromaticAberrationCenter.x < 0.0f ||
						settings.chromaticAberrationCenter.x > 1.0f ||
						settings.chromaticAberrationCenter.y < 0.0f ||
						settings.chromaticAberrationCenter.y > 1.0f ||
						settings.chromaticAberrationIntensity < 0.0f ||
						settings.chromaticAberrationFalloff <= 0.0f
					) {
						addIssue(
							SceneValidationSeverity::Warning,
							entity.id,
							"Post Process settings are outside the supported range: " + profile.id
						);
					}
				}
			} else if (component.type == "AttackSet") {
				for (const SceneAttackDefinition& attack : component.attackDefinitions) {
					for (size_t windowIndex = 0;
						windowIndex < attack.hitWindows.size(); ++windowIndex) {
						const SceneAttackHitWindow& window =
							attack.hitWindows[windowIndex];
						if (window.payloadSource != "HitBox") {
							addIssue(
								SceneValidationSeverity::Warning,
								entity.id,
								"Attack '" + attack.name + "' Hit Window " +
									std::to_string(windowIndex + 1) +
									" uses WindowLegacy payload"
							);
							continue;
						}
						const SceneEntity* hitBox = window.hitBoxEntityId != 0
							? document.FindEntity(window.hitBoxEntityId)
							: nullptr;
						if (!hitBox && !window.hitBoxEntityName.empty()) {
							hitBox = document.FindEntityByName(window.hitBoxEntityName);
						}
						if (!hitBox) {
							addIssue(
								SceneValidationSeverity::Error,
								entity.id,
								"Attack '" + attack.name + "' Hit Window " +
									std::to_string(windowIndex + 1) +
									" has no resolvable Dedicated HitBox"
							);
							continue;
						}
						const bool hasEnabledHitBox = std::any_of(
							hitBox->components.begin(), hitBox->components.end(),
							[](const SceneComponent& candidate) {
								return candidate.enabled && candidate.type == "HitBox";
							}
						);
						const bool hasEnabledTrigger = std::any_of(
							hitBox->components.begin(), hitBox->components.end(),
							[](const SceneComponent& candidate) {
								return candidate.enabled && candidate.type == "OBBCollider" &&
									candidate.colliderIsTrigger;
							}
						);
						if (!hasEnabledHitBox || !hasEnabledTrigger) {
							addIssue(
								SceneValidationSeverity::Error,
								entity.id,
								"Attack '" + attack.name + "' Dedicated HitBox '" +
									hitBox->name +
									" requires enabled HitBox and Trigger Collider"
							);
						}
						if (hitBox->active) {
							addIssue(
								SceneValidationSeverity::Warning,
								entity.id,
								"Attack '" + attack.name + "' Dedicated HitBox '" +
									hitBox->name + "' is saved active"
							);
						}
					}
					for (size_t leftIndex = 0;
						leftIndex < attack.hitWindows.size(); ++leftIndex) {
						const SceneAttackHitWindow& left = attack.hitWindows[leftIndex];
						if (left.payloadSource != "HitBox" || left.hitBoxEntityId == 0) {
							continue;
						}
						for (size_t rightIndex = leftIndex + 1;
							rightIndex < attack.hitWindows.size(); ++rightIndex) {
							const SceneAttackHitWindow& right = attack.hitWindows[rightIndex];
							if (
								right.payloadSource == "HitBox" &&
								left.hitBoxEntityId == right.hitBoxEntityId &&
								left.startTime < right.endTime &&
								right.startTime < left.endTime
							) {
								addIssue(
									SceneValidationSeverity::Warning,
									entity.id,
									"Attack '" + attack.name + "' has overlapping Dedicated HitBox Windows " +
										std::to_string(leftIndex + 1) + " and " +
										std::to_string(rightIndex + 1)
								);
							}
						}
					}
				}
			} else if (component.type == "StateMachine") {
				std::unordered_set<std::string> stateNames;
				for (const SceneStateDefinition& state :
					component.stateMachineStates) {
					if (state.name.empty()) {
						addIssue(
							SceneValidationSeverity::Error,
							entity.id,
							"StateMachine contains an empty state name"
						);
					} else if (!stateNames.insert(state.name).second) {
						addIssue(
							SceneValidationSeverity::Error,
							entity.id,
							"StateMachine contains a duplicate state: " + state.name
						);
					}
					if (state.actionId.empty()) {
						addIssue(
							SceneValidationSeverity::Warning,
							entity.id,
							"State has an empty Action Id: " + state.name
						);
					}
					std::unordered_set<std::string> parameterNames;
					for (const SceneStateParameter& parameter : state.parameters) {
						if (!parameterNames.insert(parameter.name).second) {
							addIssue(
								SceneValidationSeverity::Warning,
								entity.id,
								"State contains a duplicate parameter: " +
									state.name + "." + parameter.name
							);
						}
						validateEntityReference(
							parameter.entityId,
							"State parameter"
						);
					}
				}
				if (
					component.stateMachineStates.empty() ||
					!stateNames.contains(component.stateMachineInitialState)
				) {
					addIssue(
						SceneValidationSeverity::Error,
						entity.id,
						"StateMachine initial state cannot be resolved"
					);
				}
			} else if (
				component.type == "HitBox" ||
				component.type == "HurtBox"
			) {
				const SceneComponent* collider = nullptr;
				for (const SceneComponent& candidate : entity.components) {
					if (candidate.type == "OBBCollider" && candidate.enabled) {
						collider = &candidate;
						break;
					}
				}
				if (!collider || !collider->colliderIsTrigger) {
					addIssue(
						SceneValidationSeverity::Warning,
						entity.id,
						component.type +
							" requires an enabled Trigger Collider on the same Entity"
					);
				}
				if (component.type == "HitBox") {
					validateEntityReference(
						component.hitBoxOwnerEntityId,
						"HitBox owner"
					);
				} else {
					validateEntityReference(
						component.hurtBoxStatsEntityId,
						"HurtBox stats owner"
					);
				}
			} else if (component.type == "BoneAttachment") {
				validateEntityReference(
					component.boneAttachmentTargetEntityId,
					"BoneAttachment target"
				);
				if (component.boneAttachmentJointName.empty()) {
					addIssue(
						SceneValidationSeverity::Warning,
						entity.id,
						"BoneAttachment jointName is empty"
					);
				}
				if (
					component.boneAttachmentAlignmentMode != "ManualOffset" &&
					component.boneAttachmentAlignmentMode != "MatchSourceBone"
				) {
					addIssue(
						SceneValidationSeverity::Warning,
						entity.id,
						"BoneAttachment alignmentMode is invalid"
					);
				} else if (
					component.boneAttachmentAlignmentMode == "MatchSourceBone" &&
					component.boneAttachmentSourceJointName.empty()
				) {
					addIssue(
						SceneValidationSeverity::Warning,
						entity.id,
						"BoneAttachment sourceJointName is empty"
					);
				}
			} else if (component.type == "EnemyBehavior") {
				validateEntityReference(
					component.enemyTargetEntityId,
					"Enemy target"
				);
				validateEntityReference(
					component.enemyAttackHitBoxEntityId,
					"Enemy attack HitBox"
				);
			} else if (component.type == "Projectile") {
				validateEntityReference(
					component.projectileHomingTargetEntityId,
					"Projectile homing target"
				);
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
