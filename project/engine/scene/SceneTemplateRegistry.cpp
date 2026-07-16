// 役割: Empty Sceneと基本的な3D Sceneの初期Documentを生成する。
#include "SceneTemplateRegistry.h"

#include "SceneDocument.h"

#include <algorithm>

SceneTemplateRegistry::SceneTemplateRegistry()
	: templates_{
		{ "empty", "Empty Scene" },
		{ "basic3d", "Basic 3D Scene" }
	} {
}

const SceneTemplateDescriptor* SceneTemplateRegistry::Find(
	const std::string& templateId
) const {
	const auto found = std::find_if(
		templates_.begin(),
		templates_.end(),
		[&templateId](const SceneTemplateDescriptor& descriptor) {
			return descriptor.id == templateId;
		}
	);
	return found == templates_.end() ? nullptr : &*found;
}

bool SceneTemplateRegistry::CreateDocument(
	const std::string& templateId,
	const std::string& sceneName,
	SceneDocument& document,
	std::string& errorMessage
) const {
	if (!Find(templateId)) {
		errorMessage = "Scene template is not registered: " + templateId;
		return false;
	}

	document.Clear(sceneName);
	if (templateId == "empty") {
		errorMessage.clear();
		return true;
	}

	SceneEntity& camera = document.CreateEntity("Main Camera");
	camera.transform.translate = { 0.0f, 2.0f, -10.0f };
	document.AddComponent(camera.id, "Camera");
	for (SceneComponent& component : camera.components) {
		if (component.type == "Camera") {
			component.cameraIsMain = true;
			break;
		}
	}

	SceneEntity& environment = document.CreateEntity("Environment");
	document.AddComponent(environment.id, "Environment");
	errorMessage.clear();
	return true;
}
