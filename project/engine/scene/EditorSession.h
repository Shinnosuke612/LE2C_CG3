#pragma once

#include <string>
#include <vector>

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
	void BeginEditFrame();
	void EndEditFrame(bool commit);
	bool Undo();
	bool Redo();
	bool CanUndo() const { return !undoStack_.empty(); }
	bool CanRedo() const { return !redoStack_.empty(); }

	EditorPlayState GetState() const { return state_; }
	bool IsPlaying() const { return state_ == EditorPlayState::Playing; }
	bool IsPaused() const { return state_ == EditorPlayState::Paused; }
	bool IsEditing() const { return state_ == EditorPlayState::Edit; }

	SceneDocument& GetEditDocument() { return editDocument_; }
	const SceneDocument& GetEditDocument() const { return editDocument_; }
	SceneDocument& GetActiveDocument();
	const SceneDocument& GetActiveDocument() const;
	const std::string& GetSceneFilePath() const { return sceneFilePath_; }
	void RequestSceneReload() { reloadRequested_ = true; }

	bool ConsumeReloadRequest();

private:
	void PushUndoSnapshot(const SceneDocument& snapshot);

	SceneDocument editDocument_;
	SceneDocument runtimeDocument_;
	SceneDocument frameStartDocument_;
	std::string sceneFilePath_;
	EditorPlayState state_ = EditorPlayState::Edit;
	bool reloadRequested_ = false;
	bool editFrameActive_ = false;
	uint64_t frameStartRevision_ = 0;
	std::vector<SceneDocument> undoStack_;
	std::vector<SceneDocument> redoStack_;
};
