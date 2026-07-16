// 役割: 新規Sceneへ適用できる初期Documentテンプレートを管理する。
#pragma once

#include <string>
#include <vector>

class SceneDocument;

struct SceneTemplateDescriptor {
	std::string id;
	std::string displayName;
};

class SceneTemplateRegistry {
public:
	SceneTemplateRegistry();

	const std::vector<SceneTemplateDescriptor>& GetTemplates() const {
		return templates_;
	}
	const SceneTemplateDescriptor* Find(const std::string& templateId) const;
	bool CreateDocument(
		const std::string& templateId,
		const std::string& sceneName,
		SceneDocument& document,
		std::string& errorMessage
	) const;

private:
	std::vector<SceneTemplateDescriptor> templates_;
};
