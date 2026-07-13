// 役割: AgentBehaviorとチーム設定を合成し、実行時に使う設定を解決する。
#pragma once

#include "../scene/SceneDocument.h"

namespace AgentSettingsResolver {
	SceneComponent ResolveAgentBehaviorSettings(
		const SceneDocument& document,
		const SceneEntity& entity,
		const SceneComponent& behavior
	);

	SceneComponent ResolveTeamLeaderSettings(
		const SceneDocument& document,
		const SceneEntity& entity,
		const SceneComponent& behavior
	);
}
