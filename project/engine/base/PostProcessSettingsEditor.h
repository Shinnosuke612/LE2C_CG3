// 役割: 保存可能なPostEffect設定だけを共通のImGui UIとして編集する。
#pragma once

struct ScenePostProcessSettings;

// 呼出し側がDirtyとRuntime反映を所有する。Runtime専用の値は扱わない。
bool DrawPostProcessSettingsEditor(ScenePostProcessSettings& settings);
