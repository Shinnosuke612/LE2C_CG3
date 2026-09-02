// 役割: Fishing Score AttackのRuntime state、入力、Round抽選、接触得点を所有する。
#pragma once

#include "../SceneRuntimeObjectBinding.h"

#include <cstdint>
#include <random>
#include <string>
#include <vector>

class SceneDocument;
struct SceneComponent;

enum class SceneFishingScoreAttackState {
	Inactive,
	SelectingInitial,
	Navigating,
	SelectingNext,
	Result,
	Faulted
};

struct SceneFishingScoreAttackTextRequest {
	uint64_t entityId = 0;
	std::string text;
};

// SceneやObject、Colliderの所有権は持たず、保存済みComponentからRuntimeの判断だけを行う。
class SceneFishingScoreAttackSystem {
public:
	void UpdateBeforeSimulation(
		SceneDocument& document,
		float deltaTime,
		bool playing
	);
	void UpdateAfterSimulation(
		SceneDocument& document,
		const std::vector<SceneRuntimeObjectBinding>& bindings,
		bool playing
	);

	bool IsPlayerMovementAllowed() const;
	bool AcceptWheelZoom() const;
	const std::vector<SceneFishingScoreAttackTextRequest>& GetTextRequests() const {
		return textRequests_;
	}
	const std::string& GetDiagnostic() const { return diagnostic_; }
	void Clear();

private:
	bool Preflight(
		const SceneDocument& document,
		uint64_t directorEntityId,
		const SceneComponent& director,
		std::string& diagnostic
	) const;
	void InitializeRun(SceneDocument& document, const SceneComponent& director);
	void UpdateSelection(SceneDocument& document, const SceneComponent& director);
	void StartRound(SceneDocument& document, const SceneComponent& director);
	void Finish(SceneDocument& document, const SceneComponent& director);
	void Fault(SceneDocument& document, const SceneComponent& director, std::string diagnostic);
	void SetFishPreview(SceneDocument& document, const SceneComponent& director);
	void DeactivatePoolHooks(SceneDocument& document, const SceneComponent& director);
	void BuildTextRequests(const SceneComponent& director);

	SceneFishingScoreAttackState state_ = SceneFishingScoreAttackState::Inactive;
	uint64_t directorEntityId_ = 0;
	uint64_t activeHookEntityId_ = 0;
	int selectedFishCount_ = 0;
	int roundFishCount_ = 0;
	int roundDistanceBand_ = 0;
	float roundMultiplier_ = 0.0f;
	double elapsedSeconds_ = 0.0;
	long long totalScore_ = 0;
	bool timerRunning_ = false;
	bool hasDirector_ = false;
	std::mt19937 random_{};
	std::string diagnostic_;
	std::vector<SceneFishingScoreAttackTextRequest> textRequests_;
};
