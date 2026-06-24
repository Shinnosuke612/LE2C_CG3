#include "EditorSession.h"

bool EditorSession::Initialize(
	const std::string& sceneName,
	const std::string& sceneFilePath
) {
	sceneFilePath_ = sceneFilePath;
	state_ = EditorPlayState::Edit;
	reloadRequested_ = false;

	if (editDocument_.Load(sceneFilePath_)) {
		return true;
	}

	editDocument_.Clear(sceneName);
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
	return editDocument_.Save(sceneFilePath_);
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
