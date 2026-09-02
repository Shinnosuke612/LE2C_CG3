// 役割: Fishing Score AttackのComponent設定をRuntime stateへ展開する。
#include "SceneFishingScoreAttackSystem.h"

#include "../../../engine/collision/Collider.h"
#include "../../../engine/io/Input.h"
#include "../../../engine/math/Math.h"
#include "../../../engine/scene/SceneDocument.h"
#include "../../../engine/scene/SceneEntityQuery.h"
#include "../../../engine/scene/SceneTransformResolver.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <unordered_set>
#include <utility>

namespace {
	using SceneEntityQuery::FindEnabledComponent;
	using SceneEntityQuery::IsEntityActiveInHierarchy;

	constexpr float kTransformEpsilon = 0.0001f;

	const SceneComponent* FindDirector(
		const SceneDocument& document,
		uint64_t& entityId,
		bool& duplicate
	) {
		const SceneComponent* found = nullptr;
		entityId = 0;
		duplicate = false;
		for (const SceneEntity& entity : document.GetEntities()) {
			const SceneComponent* component =
				FindEnabledComponent(entity, "FishingScoreAttackDirector");
			if (!component) {
				continue;
			}
			if (found) {
				duplicate = true;
				return nullptr;
			}
			found = component;
			entityId = entity.id;
		}
		return found;
	}

	const SceneComponent* FindComponent(
		const SceneDocument& document,
		uint64_t entityId,
		const char* type
	) {
		const SceneEntity* entity = document.FindEntity(entityId);
		return entity ? FindEnabledComponent(*entity, type) : nullptr;
	}

	bool IsFiniteNonNegative(float value) {
		return std::isfinite(value) && value >= 0.0f;
	}

	bool IsIdentityTransform(const Transform& transform) {
		return
			std::abs(transform.translate.x) <= kTransformEpsilon &&
			std::abs(transform.translate.y) <= kTransformEpsilon &&
			std::abs(transform.translate.z) <= kTransformEpsilon &&
			std::abs(transform.rotate.x) <= kTransformEpsilon &&
			std::abs(transform.rotate.y) <= kTransformEpsilon &&
			std::abs(transform.rotate.z) <= kTransformEpsilon &&
			std::abs(transform.scale.x - 1.0f) <= kTransformEpsilon &&
			std::abs(transform.scale.y - 1.0f) <= kTransformEpsilon &&
			std::abs(transform.scale.z - 1.0f) <= kTransformEpsilon;
	}

	bool HasIdentityAncestors(
		const SceneDocument& document,
		const SceneEntity& entity
	) {
		std::unordered_set<uint64_t> visited;
		uint64_t parentId = entity.parentId;
		while (parentId != 0 && visited.insert(parentId).second) {
			const SceneEntity* parent = document.FindEntity(parentId);
			if (!parent) {
				return false;
			}
			if (!IsIdentityTransform(
				SceneTransformResolver::ResolveScene3DTransform(document, *parent)
			)) {
				return false;
			}
			parentId = parent->parentId;
		}
		return parentId == 0;
	}

	float DistanceXZ(const Vector3& left, const Vector3& right) {
		const float deltaX = left.x - right.x;
		const float deltaZ = left.z - right.z;
		return std::sqrt(deltaX * deltaX + deltaZ * deltaZ);
	}

	Vector3 ToSpawnWorldPosition(
		const Transform& areaTransform,
		float localX,
		float localZ,
		float y
	) {
		const float cosine = std::cos(areaTransform.rotate.y);
		const float sine = std::sin(areaTransform.rotate.y);
		return {
			areaTransform.translate.x + localX * cosine + localZ * sine,
			y,
			areaTransform.translate.z - localX * sine + localZ * cosine
		};
	}

	const SceneRuntimeObjectBinding* FindBinding(
		const std::vector<SceneRuntimeObjectBinding>& bindings,
		uint64_t entityId
	) {
		const auto found = std::find_if(
			bindings.begin(),
			bindings.end(),
			[entityId](const SceneRuntimeObjectBinding& binding) {
				return binding.entity && binding.entity->id == entityId;
			}
		);
		return found == bindings.end() ? nullptr : &(*found);
	}

	std::string FormatOneDecimal(float value) {
		char buffer[32]{};
		std::snprintf(buffer, sizeof(buffer), "%.1f", value);
		return buffer;
	}
}

void SceneFishingScoreAttackSystem::UpdateBeforeSimulation(
	SceneDocument& document,
	float deltaTime,
	bool playing
) {
	uint64_t foundDirectorEntityId = 0;
	bool duplicateDirector = false;
	const SceneComponent* director = FindDirector(
		document,
		foundDirectorEntityId,
		duplicateDirector
	);
	if (!playing || !director) {
		if (duplicateDirector) {
			hasDirector_ = true;
			state_ = SceneFishingScoreAttackState::Faulted;
			diagnostic_ = "Multiple FishingScoreAttackDirector components are active";
			textRequests_.clear();
		} else {
			Clear();
		}
		return;
	}

	if (directorEntityId_ != foundDirectorEntityId) {
		Clear();
		directorEntityId_ = foundDirectorEntityId;
	}
	hasDirector_ = true;
	if (state_ == SceneFishingScoreAttackState::Inactive) {
		std::string diagnostic;
		if (!Preflight(document, foundDirectorEntityId, *director, diagnostic)) {
			Fault(document, *director, std::move(diagnostic));
			return;
		}
		InitializeRun(document, *director);
	}

	if (timerRunning_) {
		elapsedSeconds_ += (std::max)(deltaTime, 0.0f);
		if (elapsedSeconds_ >= director->fishingDurationSeconds) {
			elapsedSeconds_ = director->fishingDurationSeconds;
			Finish(document, *director);
			return;
		}
	}

	if (
		state_ == SceneFishingScoreAttackState::SelectingInitial ||
		state_ == SceneFishingScoreAttackState::SelectingNext
	) {
		UpdateSelection(document, *director);
	}
	BuildTextRequests(*director);
}

void SceneFishingScoreAttackSystem::UpdateAfterSimulation(
	SceneDocument& document,
	const std::vector<SceneRuntimeObjectBinding>& bindings,
	bool playing
) {
	if (!playing || state_ != SceneFishingScoreAttackState::Navigating) {
		return;
	}
	const SceneEntity* directorEntity = document.FindEntity(directorEntityId_);
	const SceneComponent* director = directorEntity
		? FindEnabledComponent(*directorEntity, "FishingScoreAttackDirector")
		: nullptr;
	if (!director) {
		Clear();
		return;
	}
	const SceneRuntimeObjectBinding* playerBinding = FindBinding(
		bindings,
		director->fishingPlayerEntityId
	);
	const SceneRuntimeObjectBinding* hookBinding = FindBinding(
		bindings,
		activeHookEntityId_
	);
	if (
		!playerBinding || !hookBinding ||
		!playerBinding->entity || !hookBinding->entity ||
		!playerBinding->collider || !hookBinding->collider ||
		!IsEntityActiveInHierarchy(document, *playerBinding->entity) ||
		!IsEntityActiveInHierarchy(document, *hookBinding->entity) ||
		!playerBinding->collider->CanCollideWith(*hookBinding->collider) ||
		!playerBinding->collider->Intersects(*hookBinding->collider)
	) {
		return;
	}

	const SceneComponent* hook = FindComponent(
		document,
		activeHookEntityId_,
		"FishingHook"
	);
	if (!hook) {
		Fault(document, *director, "Active FishingHook is missing");
		return;
	}
	const double score = std::round(
		static_cast<double>(roundMultiplier_) *
		static_cast<double>(roundFishCount_) *
		static_cast<double>(hook->fishingHookBaseScore)
	);
	const double maximumScore = static_cast<double>(
		(std::numeric_limits<long long>::max)() - totalScore_
	);
	totalScore_ += static_cast<long long>((std::min)(score, maximumScore));
	if (SceneEntity* activeHook = document.FindEntity(activeHookEntityId_)) {
		activeHook->active = false;
	}
	activeHookEntityId_ = 0;
	state_ = SceneFishingScoreAttackState::SelectingNext;
	SetFishPreview(document, *director);
	BuildTextRequests(*director);
}

bool SceneFishingScoreAttackSystem::IsPlayerMovementAllowed() const {
	return !hasDirector_ || state_ == SceneFishingScoreAttackState::Navigating;
}

bool SceneFishingScoreAttackSystem::AcceptWheelZoom() const {
	return !hasDirector_ || state_ == SceneFishingScoreAttackState::Navigating;
}

bool SceneFishingScoreAttackSystem::Preflight(
	const SceneDocument& document,
	uint64_t directorEntityId,
	const SceneComponent& director,
	std::string& diagnostic
) const {
	if (
		director.fishingPlayerEntityId == 0 ||
		director.fishingHookSpawnAreaEntityId == 0 ||
		director.fishingHookPoolEntityId == 0 ||
		director.fishingFishEntityIds.empty() ||
		director.fishingMaxSelectableFishCount < 0 ||
		director.fishingMaxSelectableFishCount > 5 ||
		director.fishingMaxSelectableFishCount >
			static_cast<int>(director.fishingFishEntityIds.size()) ||
		director.fishingDistanceBandCount < 1 ||
		!std::isfinite(director.fishingDurationSeconds) ||
		director.fishingDurationSeconds <= 0.0f ||
		!IsFiniteNonNegative(director.fishingDistanceMultiplierBase) ||
		!IsFiniteNonNegative(director.fishingDistanceMultiplierStep)
	) {
		diagnostic = "FishingScoreAttackDirector values are invalid";
		return false;
	}
	const SceneEntity* playerEntity = document.FindEntity(
		director.fishingPlayerEntityId
	);
	const SceneComponent* playerCollider = FindComponent(
		document,
		director.fishingPlayerEntityId,
		"OBBCollider"
	);
	const SceneComponent* playerBody = FindComponent(
		document,
		director.fishingPlayerEntityId,
		"PhysicsBody"
	);
	if (
		!playerEntity ||
		!IsEntityActiveInHierarchy(document, *playerEntity) ||
		!FindComponent(document, director.fishingPlayerEntityId, "PlayerBehavior") ||
		!playerCollider || !playerBody || !playerBody->physicsFreezePositionY
	) {
		diagnostic = "Fishing player requires active PlayerBehavior, Collider, and Y freeze";
		return false;
	}
	const SceneEntity* spawnAreaEntity = document.FindEntity(
		director.fishingHookSpawnAreaEntityId
	);
	const SceneComponent* spawnArea = FindComponent(
		document,
		director.fishingHookSpawnAreaEntityId,
		"FishingHookSpawnArea"
	);
	if (
		!spawnAreaEntity || !spawnArea ||
		!std::isfinite(spawnArea->fishingSpawnHalfSizeX) ||
		!std::isfinite(spawnArea->fishingSpawnHalfSizeZ) ||
		!std::isfinite(spawnArea->fishingSpawnMinimumDistance) ||
		spawnArea->fishingSpawnHalfSizeX <= 0.0f ||
		spawnArea->fishingSpawnHalfSizeZ <= 0.0f ||
		spawnArea->fishingSpawnMinimumDistance < 0.0f ||
		spawnArea->fishingSpawnMaxAttempts < 1
	) {
		diagnostic = "FishingHookSpawnArea values are invalid";
		return false;
	}
	const SceneComponent* pool = FindComponent(
		document,
		director.fishingHookPoolEntityId,
		"FishingHookPool"
	);
	if (!pool || pool->fishingHookPoolEntries.empty()) {
		diagnostic = "FishingHookPool is missing or empty";
		return false;
	}

	std::unordered_set<uint64_t> fishIds;
	for (uint64_t fishEntityId : director.fishingFishEntityIds) {
		const SceneEntity* fish = document.FindEntity(fishEntityId);
		if (
			fishEntityId == 0 || !fish ||
			!fishIds.insert(fishEntityId).second ||
			!FindEnabledComponent(*fish, "AgentBehavior") ||
			!HasIdentityAncestors(document, *fish)
		) {
			diagnostic = "Fishing fish references require unique AgentBehavior and identity ancestors";
			return false;
		}
	}

	std::unordered_set<uint64_t> hookIds;
	std::vector<float> weightTotals(
		static_cast<size_t>(director.fishingDistanceBandCount),
		0.0f
	);
	for (const SceneFishingHookPoolEntry& entry : pool->fishingHookPoolEntries) {
		const SceneComponent* hook = FindComponent(
			document,
			entry.hookEntityId,
			"FishingHook"
		);
		const SceneComponent* hookCollider = FindComponent(
			document,
			entry.hookEntityId,
			"OBBCollider"
		);
		if (
			entry.hookEntityId == 0 || !hook || !hookCollider ||
			!hookIds.insert(entry.hookEntityId).second ||
			!hookCollider->colliderIsTrigger || !hookCollider->colliderActive ||
			entry.weightsByDistanceBand.size() != weightTotals.size() ||
			(playerCollider->colliderMask & hookCollider->colliderLayer) == 0 ||
			(hookCollider->colliderMask & playerCollider->colliderLayer) == 0
		) {
			diagnostic = "FishingHookPool entry or Player collision settings are invalid";
			return false;
		}
		for (size_t bandIndex = 0; bandIndex < weightTotals.size(); ++bandIndex) {
			const float weight = entry.weightsByDistanceBand[bandIndex];
			if (!IsFiniteNonNegative(weight)) {
				diagnostic = "FishingHookPool contains an invalid weight";
				return false;
			}
			weightTotals[bandIndex] += weight;
		}
	}
	if (std::any_of(weightTotals.begin(), weightTotals.end(), [](float total) {
		return !std::isfinite(total) || total <= 0.0f;
	})) {
		diagnostic = "FishingHookPool has no selectable hook for a distance band";
		return false;
	}

	const uint64_t textEntityIds[] = {
		director.fishingFishCountTextEntityId,
		director.fishingTimerTextEntityId,
		director.fishingScoreTextEntityId,
		director.fishingMultiplierTextEntityId,
		director.fishingResultTextEntityId
	};
	for (uint64_t textEntityId : textEntityIds) {
		if (textEntityId != 0 && !FindComponent(document, textEntityId, "TextRenderer")) {
			diagnostic = "Fishing HUD reference requires TextRenderer";
			return false;
		}
	}
	(void)directorEntityId;
	return true;
}

void SceneFishingScoreAttackSystem::InitializeRun(
	SceneDocument& document,
	const SceneComponent& director
) {
	if (director.fishingRandomizeSeedOnPlay) {
		std::random_device randomDevice;
		random_.seed(randomDevice());
	} else {
		random_.seed(static_cast<std::mt19937::result_type>(director.fishingRandomSeed));
	}
	selectedFishCount_ = 0;
	roundFishCount_ = 0;
	roundDistanceBand_ = 0;
	roundMultiplier_ = director.fishingDistanceMultiplierBase;
	elapsedSeconds_ = 0.0;
	totalScore_ = 0;
	timerRunning_ = false;
	diagnostic_.clear();
	DeactivatePoolHooks(document, director);
	SetFishPreview(document, director);
	state_ = SceneFishingScoreAttackState::SelectingInitial;
	BuildTextRequests(director);
}

void SceneFishingScoreAttackSystem::UpdateSelection(
	SceneDocument& document,
	const SceneComponent& director
) {
	Input* input = Input::GetInstance();
	if (!input) {
		return;
	}
	const float wheel = input->GetMouseWheel();
	if (std::abs(wheel) > 0.000001f) {
		const int wheelNotches = static_cast<int>(std::round(wheel));
		selectedFishCount_ = std::clamp(
			selectedFishCount_ + wheelNotches,
			0,
			director.fishingMaxSelectableFishCount
		);
		SetFishPreview(document, director);
	}
	if (!input->TriggerKey(DIK_RETURN)) {
		return;
	}
	if (state_ == SceneFishingScoreAttackState::SelectingInitial) {
		timerRunning_ = true;
	}
	StartRound(document, director);
}

void SceneFishingScoreAttackSystem::StartRound(
	SceneDocument& document,
	const SceneComponent& director
) {
	const SceneEntity* player = document.FindEntity(director.fishingPlayerEntityId);
	const SceneEntity* spawnAreaEntity = document.FindEntity(
		director.fishingHookSpawnAreaEntityId
	);
	const SceneComponent* spawnArea = FindComponent(
		document,
		director.fishingHookSpawnAreaEntityId,
		"FishingHookSpawnArea"
	);
	const SceneComponent* pool = FindComponent(
		document,
		director.fishingHookPoolEntityId,
		"FishingHookPool"
	);
	if (!player || !spawnAreaEntity || !spawnArea || !pool) {
		Fault(document, director, "Fishing round references became invalid");
		return;
	}

	const Transform playerTransform =
		SceneTransformResolver::ResolveScene3DTransform(document, *player);
	const Transform areaTransform =
		SceneTransformResolver::ResolveScene3DTransform(document, *spawnAreaEntity);
	std::uniform_real_distribution<float> xDistribution(
		-spawnArea->fishingSpawnHalfSizeX,
		spawnArea->fishingSpawnHalfSizeX
	);
	std::uniform_real_distribution<float> zDistribution(
		-spawnArea->fishingSpawnHalfSizeZ,
		spawnArea->fishingSpawnHalfSizeZ
	);
	Vector3 spawnPosition{};
	for (int attempt = 0; attempt < spawnArea->fishingSpawnMaxAttempts; ++attempt) {
		spawnPosition = ToSpawnWorldPosition(
			areaTransform,
			xDistribution(random_),
			zDistribution(random_),
			playerTransform.translate.y
		);
		if (
			DistanceXZ(spawnPosition, playerTransform.translate) >=
			spawnArea->fishingSpawnMinimumDistance
		) {
			break;
		}
	}

	float maximumReachableDistance = 0.0f;
	for (const float xSign : { -1.0f, 1.0f }) {
		for (const float zSign : { -1.0f, 1.0f }) {
			maximumReachableDistance = (std::max)(
				maximumReachableDistance,
				DistanceXZ(
					playerTransform.translate,
					ToSpawnWorldPosition(
						areaTransform,
						xSign * spawnArea->fishingSpawnHalfSizeX,
						zSign * spawnArea->fishingSpawnHalfSizeZ,
						playerTransform.translate.y
					)
				)
			);
		}
	}
	const float spawnDistance = DistanceXZ(spawnPosition, playerTransform.translate);
	const float distanceRange = maximumReachableDistance -
		spawnArea->fishingSpawnMinimumDistance;
	const float normalizedDistance = distanceRange > 0.0001f
		? std::clamp(
			(spawnDistance - spawnArea->fishingSpawnMinimumDistance) / distanceRange,
			0.0f,
			1.0f
		)
		: 0.0f;
	const int bandIndex = (std::min)(
		static_cast<int>(std::floor(
			normalizedDistance * director.fishingDistanceBandCount
		)),
		director.fishingDistanceBandCount - 1
	);

	float totalWeight = 0.0f;
	for (const SceneFishingHookPoolEntry& entry : pool->fishingHookPoolEntries) {
		totalWeight += entry.weightsByDistanceBand[static_cast<size_t>(bandIndex)];
	}
	if (!std::isfinite(totalWeight) || totalWeight <= 0.0f) {
		Fault(document, director, "FishingHookPool has no selectable hook for the selected band");
		return;
	}
	std::uniform_real_distribution<float> weightDistribution(0.0f, totalWeight);
	float remainingWeight = weightDistribution(random_);
	const SceneFishingHookPoolEntry* selectedEntry =
		&pool->fishingHookPoolEntries.back();
	for (const SceneFishingHookPoolEntry& entry : pool->fishingHookPoolEntries) {
		remainingWeight -= entry.weightsByDistanceBand[static_cast<size_t>(bandIndex)];
		if (remainingWeight <= 0.0f) {
			selectedEntry = &entry;
			break;
		}
	}

	DeactivatePoolHooks(document, director);
	SceneEntity* hookEntity = document.FindEntity(selectedEntry->hookEntityId);
	if (!hookEntity) {
		Fault(document, director, "Selected FishingHook is missing");
		return;
	}
	hookEntity->transform.translate = spawnPosition;
	hookEntity->active = true;
	activeHookEntityId_ = hookEntity->id;
	roundFishCount_ = selectedFishCount_;
	roundDistanceBand_ = bandIndex;
	roundMultiplier_ = director.fishingDistanceMultiplierBase +
		director.fishingDistanceMultiplierStep * static_cast<float>(bandIndex);
	state_ = SceneFishingScoreAttackState::Navigating;
	SetFishPreview(document, director);
}

void SceneFishingScoreAttackSystem::Finish(
	SceneDocument& document,
	const SceneComponent& director
) {
	timerRunning_ = false;
	DeactivatePoolHooks(document, director);
	for (uint64_t fishEntityId : director.fishingFishEntityIds) {
		if (SceneEntity* fish = document.FindEntity(fishEntityId)) {
			fish->active = false;
		}
	}
	state_ = SceneFishingScoreAttackState::Result;
	BuildTextRequests(director);
}

void SceneFishingScoreAttackSystem::Fault(
	SceneDocument& document,
	const SceneComponent& director,
	std::string diagnostic
) {
	diagnostic_ = std::move(diagnostic);
	timerRunning_ = false;
	DeactivatePoolHooks(document, director);
	for (uint64_t fishEntityId : director.fishingFishEntityIds) {
		if (SceneEntity* fish = document.FindEntity(fishEntityId)) {
			fish->active = false;
		}
	}
	state_ = SceneFishingScoreAttackState::Faulted;
	BuildTextRequests(director);
}

void SceneFishingScoreAttackSystem::SetFishPreview(
	SceneDocument& document,
	const SceneComponent& director
) {
	for (size_t index = 0; index < director.fishingFishEntityIds.size(); ++index) {
		if (SceneEntity* fish = document.FindEntity(
			director.fishingFishEntityIds[index]
		)) {
			fish->active = static_cast<int>(index) < selectedFishCount_;
		}
	}
}

void SceneFishingScoreAttackSystem::DeactivatePoolHooks(
	SceneDocument& document,
	const SceneComponent& director
) {
	const SceneComponent* pool = FindComponent(
		document,
		director.fishingHookPoolEntityId,
		"FishingHookPool"
	);
	if (pool) {
		for (const SceneFishingHookPoolEntry& entry : pool->fishingHookPoolEntries) {
			if (SceneEntity* hook = document.FindEntity(entry.hookEntityId)) {
				hook->active = false;
			}
		}
	}
	activeHookEntityId_ = 0;
}

void SceneFishingScoreAttackSystem::BuildTextRequests(
	const SceneComponent& director
) {
	textRequests_.clear();
	const auto addText = [this](uint64_t entityId, std::string text) {
		if (entityId != 0) {
			textRequests_.push_back({ entityId, std::move(text) });
		}
	};
	addText(
		director.fishingFishCountTextEntityId,
		director.fishingFishCountPrefix + std::to_string(selectedFishCount_)
	);
	const double remainingSeconds = (std::max)(
		static_cast<double>(director.fishingDurationSeconds) - elapsedSeconds_,
		0.0
	);
	addText(
		director.fishingTimerTextEntityId,
		director.fishingTimerPrefix + FormatOneDecimal(
			static_cast<float>(remainingSeconds)
		)
	);
	addText(
		director.fishingScoreTextEntityId,
		director.fishingScorePrefix + std::to_string(totalScore_)
	);
	addText(
		director.fishingMultiplierTextEntityId,
		director.fishingMultiplierPrefix + FormatOneDecimal(roundMultiplier_)
	);
	addText(
		director.fishingResultTextEntityId,
		state_ == SceneFishingScoreAttackState::Result
			? director.fishingResultPrefix + std::to_string(totalScore_)
			: std::string{}
	);
}

void SceneFishingScoreAttackSystem::Clear() {
	state_ = SceneFishingScoreAttackState::Inactive;
	directorEntityId_ = 0;
	activeHookEntityId_ = 0;
	selectedFishCount_ = 0;
	roundFishCount_ = 0;
	roundDistanceBand_ = 0;
	roundMultiplier_ = 0.0f;
	elapsedSeconds_ = 0.0;
	totalScore_ = 0;
	timerRunning_ = false;
	hasDirector_ = false;
	diagnostic_.clear();
	textRequests_.clear();
}
