// 役割: Eventから選ばれたPost Process ProfileのRuntime Copyだけを保持する。
#pragma once

#include <cstdint>

#include "SceneEventSystem.h"
#include "../../../engine/scene/SceneSettings.h"

class SceneDocument;

class ScenePostProcessProfileSystem {
public:
	void Reset(const SceneDocument* document = nullptr);
	void Sync(const SceneDocument& document);
	void ApplyEventResult(
		const SceneDocument& document,
		const SceneEventResult& result
	);
	const ScenePostProcessSettings& GetEffectiveSettings() const {
		return effectiveSettings_;
	}
	uint64_t GetGeneration() const { return generation_; }

private:
	void ApplyBaseline(const SceneDocument& document);

	ScenePostProcessSettings effectiveSettings_{};
	uint64_t activeManagerEntityId_ = 0;
	std::string activeProfileId_;
	uint64_t generation_ = 1;
};
