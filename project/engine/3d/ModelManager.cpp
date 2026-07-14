// 役割: モデルファイルの非重複読み込みとModelキャッシュを実装する。
#include "ModelManager.h"
#include "../base/DirectXCommon.h"
#include "ModelCommon.h"
#include "Model.h"
#include "../utility/EditableResourcePath.h"
#include "../utility/StringUtility.h"
#include <filesystem>

namespace {

std::string NormalizeModelKey(const std::string& filePath) {
	const std::filesystem::path requestedPath = StringUtility::ToPath(filePath);
	std::filesystem::path normalizedPath = requestedPath;
	if (requestedPath.is_absolute()) {
		normalizedPath = EditableResourcePath::ToProjectRelative(
			EditableResourcePath::ResolveResource(requestedPath)
		);
	}

	std::string key = StringUtility::ToUtf8(normalizedPath.lexically_normal());
	const std::string resourcePrefix = "resources/";
	if (key.rfind(resourcePrefix, 0) == 0) {
		key = key.substr(resourcePrefix.size());
	}
	return key;
}

} // namespace

ModelManager* ModelManager::instance = nullptr;

void ModelManager::Initialize(DirectXCommon* dxCommon){
	modelCommon = new ModelCommon;
	modelCommon->Initialize(dxCommon);
}

ModelManager* ModelManager::GetInstance(){
	if(instance == nullptr){
		instance = new ModelManager;
	}
	return instance;
}

void ModelManager::Finalize(){
	delete instance;
	instance = nullptr;
}

void ModelManager::LoadModel(const std::string& filePath){
	const std::string modelKey = NormalizeModelKey(filePath);
//読み込み済みモデルを検索
	if(models.contains(modelKey)){
		//読み込み済みなら早期return
		return;
	}
	//モデルの生成とファイル読み込み、初期化
	const std::filesystem::path modelFilePath =
		EditableResourcePath::ResolveResource(StringUtility::ToPath(modelKey));
	std::unique_ptr<Model> model = std::make_unique<Model>();
	if (!model->Initialize(
		modelCommon,
		modelFilePath
	)) {
		return;
	}

	//モデルをmapコンテナに格納する
	models.insert(std::make_pair(modelKey, std::move(model)));
}

Model* ModelManager::FindModel(const std::string& filePath){
	const std::string modelKey = NormalizeModelKey(filePath);
	//読み込み済みモデルを検索
	if(models.contains(modelKey)){
		//読み込みモデルを戻り値としてreturn
		return models.at(modelKey).get();
	}

	//ファイル名一致なし
	return nullptr;
}
