// 役割: BaseSceneの共通初期状態を実装する。
#include "BaseScene.h"

#include "SceneInstance.h"

void BaseScene::Initialize()
{
}

void BaseScene::Finalize()
{
}

void BaseScene::Update(float deltaTime)
{
	(void)deltaTime;
}

void BaseScene::Draw()
{
}

SceneDocument* BaseScene::GetSceneDocument() const
{
	return sceneInstance_ ? sceneInstance_->GetDocument() : nullptr;
}

const std::string& BaseScene::GetSceneAssetId() const
{
	static const std::string emptySceneId;
	return sceneInstance_ ? sceneInstance_->GetSceneId() : emptySceneId;
}

SceneInstanceId BaseScene::GetSceneInstanceId() const
{
	return sceneInstance_
		? sceneInstance_->GetId()
		: kInvalidSceneInstanceId;
}
