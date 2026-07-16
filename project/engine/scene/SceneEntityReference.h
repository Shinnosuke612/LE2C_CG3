// 役割: 保存可能なScene間Entity参照と、実行時に解決済みのHandleを定義する。
#pragma once

#include <cstdint>
#include <string>

using SceneInstanceId = std::uint64_t;
inline constexpr SceneInstanceId kInvalidSceneInstanceId = 0;

// Runtime専用。SceneInstanceIdは再利用しないため、Unload後は自然に無効になる。
struct SceneEntityHandle {
	SceneInstanceId sceneInstanceId = kInvalidSceneInstanceId;
	std::uint64_t entityId = 0;

	bool IsValid() const {
		return
			sceneInstanceId != kInvalidSceneInstanceId &&
			entityId != 0;
	}
};

// Scene JSON保存用。sceneIdが空なら参照元と同じSceneInstanceを表す。
struct SceneEntityReference {
	std::string sceneId;
	std::string instanceKey;
	std::uint64_t entityId = 0;

	bool IsEmpty() const {
		return entityId == 0;
	}
};
