#include "EditorSession.h"

#include <algorithm>

namespace {
	constexpr size_t kMaxUndoSnapshots = 100;
}

bool EditorSession::Initialize(
	const std::string& sceneName,
	const std::string& sceneFilePath
) {
	sceneFilePath_ = sceneFilePath;
	state_ = EditorPlayState::Edit;
	reloadRequested_ = false;
	editFrameActive_ = false;
	frameStartRevision_ = 0;
	undoStack_.clear();
	redoStack_.clear();

	if (editDocument_.Load(sceneFilePath_)) {
		frameStartDocument_ = editDocument_;
		return true;
	}

	editDocument_.Clear(sceneName);
	frameStartDocument_ = editDocument_;
	return false;
}

void EditorSession::Play() {
	if (state_ != EditorPlayState::Edit) {
		return;
	}
	runtimeDocument_ = editDocument_;
	runtimeDocument_.MarkClean();
	state_ = EditorPlayState::Playing;
	reloadRequested_ = true;
}

void EditorSession::Pause() {
	if (state_ == EditorPlayState::Playing) {
		state_ = EditorPlayState::Paused;
	}
}

void EditorSession::Resume() {
	if (state_ == EditorPlayState::Paused) {
		state_ = EditorPlayState::Playing;
	}
}

void EditorSession::Stop() {
	if (state_ == EditorPlayState::Edit) {
		return;
	}
	state_ = EditorPlayState::Edit;
	runtimeDocument_.Clear();
	reloadRequested_ = true;
}

bool EditorSession::Save() {
	if (!IsEditing() || sceneFilePath_.empty()) {
		return false;
	}
	const bool saved = editDocument_.Save(sceneFilePath_);
	if (saved) {
		frameStartDocument_ = editDocument_;
		frameStartRevision_ = editDocument_.GetRevision();
		editFrameActive_ = false;
	}
	return saved;
}

void EditorSession::BeginEditFrame() {
	if (!IsEditing() || editFrameActive_) {
		return;
	}
	frameStartDocument_ = editDocument_;
	frameStartRevision_ = editDocument_.GetRevision();
	editFrameActive_ = true;
}

void EditorSession::EndEditFrame(bool commit) {
	if (!editFrameActive_) {
		return;
	}
	if (!IsEditing()) {
		editFrameActive_ = false;
		return;
	}
	if (editDocument_.GetRevision() == frameStartRevision_) {
		editFrameActive_ = false;
		return;
	}
	if (!commit) {
		return;
	}
	editFrameActive_ = false;
	PushUndoSnapshot(frameStartDocument_);
	redoStack_.clear();
}

bool EditorSession::Undo() {
	if (!IsEditing() || undoStack_.empty()) {
		return false;
	}
	redoStack_.push_back(editDocument_);
	editDocument_ = undoStack_.back();
	undoStack_.pop_back();
	editDocument_.MarkDirty();
	frameStartDocument_ = editDocument_;
	frameStartRevision_ = editDocument_.GetRevision();
	editFrameActive_ = false;
	reloadRequested_ = true;
	return true;
}

bool EditorSession::Redo() {
	if (!IsEditing() || redoStack_.empty()) {
		return false;
	}
	undoStack_.push_back(editDocument_);
	editDocument_ = redoStack_.back();
	redoStack_.pop_back();
	editDocument_.MarkDirty();
	frameStartDocument_ = editDocument_;
	frameStartRevision_ = editDocument_.GetRevision();
	editFrameActive_ = false;
	reloadRequested_ = true;
	return true;
}

SceneDocument& EditorSession::GetActiveDocument() {
	return IsEditing() ? editDocument_ : runtimeDocument_;
}

const SceneDocument& EditorSession::GetActiveDocument() const {
	return IsEditing() ? editDocument_ : runtimeDocument_;
}

bool EditorSession::ConsumeReloadRequest() {
	const bool requested = reloadRequested_;
	reloadRequested_ = false;
	return requested;
}

void EditorSession::PushUndoSnapshot(const SceneDocument& snapshot) {
	undoStack_.push_back(snapshot);
	if (undoStack_.size() > kMaxUndoSnapshots) {
		undoStack_.erase(undoStack_.begin());
	}
}
