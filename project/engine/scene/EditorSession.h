#pragma once

#include <string>

#include "SceneDocument.h"

enum class EditorPlayState {
	Edit,
	Playing,
	Paused
};

class EditorSession {
public:
	bool Initialize(
		const std::string& sceneName,
		const std::string& sceneFilePath
	);

	void Play();
	void Pause();
	void Resume();
	void Stop();
	bool Save();

	EditorPlayState GetState() const { return state_; }
	bool IsPlaying() const { return state_ == EditorPlayState::Playing; }
	bool IsPaused() const { return state_ == EditorPlayState::Paused; }
	bool IsEditing() const { return state_ == EditorPlayState::Edit; }

	SceneDocument& GetEditDocument() { return editDocument_; }
	const SceneDocument& GetEditDocument() const { return editDocument_; }
	SceneDocument& GetActiveDocument();
	const SceneDocument& GetActiveDocument() const;
	const std::string& GetSceneFilePath() const { return sceneFilePath_; }

	bool ConsumeReloadRequest();

private:
	SceneDocument editDocument_;
	SceneDocument runtimeDocument_;
	std::string sceneFilePath_;
	EditorPlayState state_ = EditorPlayState::Edit;
	bool reloadRequested_ = false;
};
