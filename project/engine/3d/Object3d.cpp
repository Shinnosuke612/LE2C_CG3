#include "Object3d.h"
#include "Object3dCommon.h"
#include "../base/DirectXCommon.h"
#include "../2d/TextureManager.h"
#include "ModelManager.h"
#include "Model.h"
#include "Camera.h"
#include "SrvManager.h"
#include "../debug/DebugRenderer.h"
#include <algorithm>
#include <cmath>

#if defined(_DEBUG) || defined(DEVELOPMENT)
#include "../../externals/imgui/imgui.h"
#endif

namespace {

#if defined(_DEBUG) || defined(DEVELOPMENT)
bool ProjectSkeletonPoint(
	const Matrix4x4& skeletonSpaceMatrix,
	const Matrix4x4& objectWorldMatrix,
	const Matrix4x4& viewProjectionMatrix,
	const ImGuiViewport& viewport,
	ImVec2& screenPosition
) {
	const Matrix4x4 worldMatrix = Multiply(
		skeletonSpaceMatrix,
		objectWorldMatrix
	);
	const Matrix4x4 worldViewProjectionMatrix = Multiply(
		worldMatrix,
		viewProjectionMatrix
	);

	const float clipX = worldViewProjectionMatrix.m[3][0];
	const float clipY = worldViewProjectionMatrix.m[3][1];
	const float clipZ = worldViewProjectionMatrix.m[3][2];
	const float clipW = worldViewProjectionMatrix.m[3][3];

	if (clipW <= 0.0001f || clipZ < 0.0f || clipZ > clipW) {
		return false;
	}

	const float ndcX = clipX / clipW;
	const float ndcY = clipY / clipW;
	screenPosition = {
		viewport.Pos.x + (ndcX + 1.0f) * 0.5f * viewport.Size.x,
		viewport.Pos.y + (1.0f - ndcY) * 0.5f * viewport.Size.y
	};
	return true;
}
#endif

} // namespace

void Object3d::Initialize(Object3dCommon* object3dCommon){
	this->object3dCommon = object3dCommon;

	CreateTransformationMatrixResource();
	CreateCameraResource();
	CreateShadowTransformationMatrixResource();
	CreateMaterialResource();
	environmentTextureFilePath_ = "resources/rostock_laage_airport_4k.dds";
	TextureManager::GetInstance()->LoadTexture(environmentTextureFilePath_);

	//Transform変数を作る
	transform = { {1.0f,1.0f,1.0f},{0.0f,0.0f,0.0f},{0.0f,0.0f,0.0f} };
	this->camera = object3dCommon->GetDefaultCamera();
}

void Object3d::Update(){
	objectWorldMatrix_ = MakeAffineMatrix(
		transform.scale,
		transform.rotate,
		transform.translate
	);
	if (parent_) {
		objectWorldMatrix_ = Multiply(
			objectWorldMatrix_,
			parent_->objectWorldMatrix_
		);
	}
	const Matrix4x4& worldMatrix = objectWorldMatrix_;

	// ModelのRootNode行列を適用する
	Matrix4x4 modelWorldMatrix = worldMatrix;
	if (model) {
		Matrix4x4 localMatrix = model->HasSkinning()
			? MakeIdentity4x4()
			: model->GetRootNodeLocalMatrix();
		const Animation& animation = model->GetAnimation();
		if (animation.IsValid()) {
			if (isAnimationPlaying_) {
				animationTime_ += (1.0f / 60.0f) * animationSpeed_;
				if (isAnimationLooping_) {
					animationTime_ = std::fmod(animationTime_, animation.duration);
					if (animationTime_ < 0.0f) {
						animationTime_ += animation.duration;
					}
				}
				else {
					animationTime_ = std::clamp(
						animationTime_,
						0.0f,
						animation.duration
					);
					if (animationTime_ >= animation.duration) {
						isAnimationPlaying_ = false;
					}
				}
			}

			ApplyAnimation(skeleton_, animation, animationTime_);
		}

		if (skeleton_.IsValid()) {
			UpdateSkeleton(skeleton_);
			if (skinCluster_) {
				skinCluster_->Update(skeleton_);
			}
			if (!model->HasSkinning()) {
				localMatrix =
					skeleton_.joints[skeleton_.root].skeletonSpaceMatrix;
			}
		}
		modelWorldMatrix = Multiply(localMatrix, worldMatrix);
	}

	Matrix4x4 worldViewProjectionMatrix;
	if (camera) {
		const Matrix4x4& viewProjectionMatrix = camera->GetViewProjectionMatrix();
		worldViewProjectionMatrix = Multiply(modelWorldMatrix, viewProjectionMatrix);
	}
	else {
		worldViewProjectionMatrix = modelWorldMatrix;
	}

	transformationMatrixData->WVP = worldViewProjectionMatrix;
	transformationMatrixData->World = modelWorldMatrix;

	if (camera) {
		cameraData->worldPosition = camera->GetTranslate();
	}
}

void Object3d::SetModel(Model* model) {
	this->model = model;
	skeleton_ = model
		? CreateSkeleton(model->GetRootNode())
		: Skeleton{};
	skinCluster_.reset();
	if (
		model &&
		model->HasSkinning() &&
		skeleton_.IsValid()
	) {
		skinCluster_ = std::make_unique<SkinCluster>();
		skinCluster_->Initialize(
			object3dCommon->GetDxCommon(),
			SrvManager::GetInstance(),
			skeleton_,
			*model
		);
	}
	ResetAnimation();
}

void Object3d::Draw(){
	if (!model) {
		return;
	}

	auto* commandList = object3dCommon->GetDxCommon()->GetCommandList();
	if (skinCluster_ && skinCluster_->IsValid()) {
		object3dCommon->DispatchSkinning(*skinCluster_);
		object3dCommon->SetCommonRenderState();
	}
	else {
		object3dCommon->SetCommonRenderState();
	}

	// 座標変換行列CBufferの場所を設定
	commandList->SetGraphicsRootConstantBufferView(1, transformationMatrixResource->GetGPUVirtualAddress());

	commandList->SetGraphicsRootConstantBufferView(4, cameraResource->GetGPUVirtualAddress());

	commandList->SetGraphicsRootDescriptorTable(
		7,
		TextureManager::GetInstance()->GetSrvHandleGPU(environmentTextureFilePath_)
	);

	if (skinCluster_ && skinCluster_->IsValid()) {
		model->DrawWithVertexBufferAndMaterial(
			skinCluster_->GetSkinnedVertexBufferView(),
			materialResource
		);
	}
	else {
		model->DrawWithMaterial(materialResource);
	}
}

void Object3d::DrawShadow(const Matrix4x4& lightViewProjection) {
	if (!model) {
		return;
	}

	shadowTransformationMatrixData->WVP = Multiply(transformationMatrixData->World, lightViewProjection);

	auto* commandList = object3dCommon->GetDxCommon()->GetCommandList();
	if (skinCluster_ && skinCluster_->IsValid()) {
		object3dCommon->DispatchSkinning(*skinCluster_);
		object3dCommon->SetShadowRenderState();
	}
	else {
		object3dCommon->SetShadowRenderState();
	}
	commandList->SetGraphicsRootConstantBufferView(
		0,
		shadowTransformationMatrixResource->GetGPUVirtualAddress()
	);

	if (skinCluster_ && skinCluster_->IsValid()) {
		model->DrawForShadowWithVertexBuffer(
			skinCluster_->GetSkinnedVertexBufferView()
		);
	}
	else {
		model->DrawForShadow();
	}
}

void Object3d::DrawSkeletonDebug(
	bool drawJointNames,
	bool drawJointAxes,
	float jointRadius,
	float axisLength
) const {
#if defined(_DEBUG) || defined(DEVELOPMENT)
	if (!skeleton_.IsValid() || !camera || !transformationMatrixData) {
		return;
	}

	DebugRenderer* debugRenderer = DebugRenderer::GetInstance();
	const Vector4 lineColor = { 1.0f, 0.65f, 0.12f, 1.0f };
	const Vector4 jointColor = { 0.2f, 0.85f, 1.0f, 1.0f };
	const Vector4 rootColor = { 1.0f, 0.95f, 0.35f, 1.0f };
	const Matrix4x4& viewProjection = camera->GetViewProjectionMatrix();
	const Matrix4x4 objectWorldMatrix = MakeAffineMatrix(
		transform.scale,
		transform.rotate,
		transform.translate
	);

	std::vector<Matrix4x4> jointWorldMatrices(skeleton_.joints.size());
	for (const Joint& joint : skeleton_.joints) {
		jointWorldMatrices[joint.index] = Multiply(
			joint.skeletonSpaceMatrix,
			objectWorldMatrix
		);
	}

	for (const Joint& joint : skeleton_.joints) {
		const Matrix4x4& jointWorldMatrix =
			jointWorldMatrices[joint.index];
		const Vector3 jointPosition = {
			jointWorldMatrix.m[3][0],
			jointWorldMatrix.m[3][1],
			jointWorldMatrix.m[3][2]
		};

		debugRenderer->AddSphere(
			jointPosition,
			joint.index == skeleton_.root
				? jointRadius * 1.35f
				: jointRadius,
			joint.index == skeleton_.root ? rootColor : jointColor
		);

		if (drawJointAxes) {
			debugRenderer->AddAxis(jointWorldMatrix, axisLength);
		}

		if (joint.parent.has_value()) {
			const Matrix4x4& parentWorldMatrix =
				jointWorldMatrices[*joint.parent];
			const Vector3 parentPosition = {
				parentWorldMatrix.m[3][0],
				parentWorldMatrix.m[3][1],
				parentWorldMatrix.m[3][2]
			};
			debugRenderer->AddLine(
				parentPosition,
				jointPosition,
				lineColor
			);
		}
	}

	if (drawJointNames) {
		ImGuiViewport* viewport = ImGui::GetMainViewport();
		if (viewport) {
			ImDrawList* drawList = ImGui::GetForegroundDrawList(viewport);
			for (const Joint& joint : skeleton_.joints) {
				ImVec2 screenPosition{};
				if (!ProjectSkeletonPoint(
					joint.skeletonSpaceMatrix,
					objectWorldMatrix,
					viewProjection,
					*viewport,
					screenPosition
				)) {
					continue;
				}

			drawList->AddText(
				{
					screenPosition.x + 7.0f,
					screenPosition.y - 7.0f
				},
				IM_COL32_WHITE,
				joint.name.c_str()
			);
			}
		}
	}
#else
	(void)drawJointNames;
	(void)drawJointAxes;
	(void)jointRadius;
	(void)axisLength;
#endif
}

void Object3d::SetModel(const std::string& filePath){
//モデルを検索してセットする
	SetModel(ModelManager::GetInstance()->FindModel(filePath));
}

void Object3d::ResetAnimation() {
	animationTime_ = 0.0f;
	isAnimationPlaying_ = false;
}

bool Object3d::HasAnimation() const {
	return model != nullptr && model->HasAnimation();
}

float Object3d::GetAnimationDuration() const {
	return HasAnimation() ? model->GetAnimation().duration : 0.0f;
}

void Object3d::SetEnvironmentMap(const std::string& textureFilePath, float coefficient) {
	environmentTextureFilePath_ = textureFilePath;
	if (!environmentTextureFilePath_.empty()) {
		TextureManager::GetInstance()->LoadTexture(environmentTextureFilePath_);
	}
	SetEnvironmentCoefficient(coefficient);
}

void Object3d::SetEnvironmentCoefficient(float coefficient) {
	cameraData->environmentCoefficient = std::clamp(coefficient, 0.0f, 1.0f);
}

void Object3d::SetColor(const Vector4& color) {
	if (materialData) {
		materialData->color = color;
	}
}

void Object3d::SetEnableLighting(bool enableLighting) {
	if (materialData) {
		materialData->enableLighting = enableLighting ? 1 : 0;
	}
}

void Object3d::SetEmissive(float intensity, const Vector4& color) {
	if (materialData) {
		materialData->emissiveIntensity = (std::max)(0.0f, intensity);
		materialData->emissiveColor = color;
	}
}

void Object3d::CreateTransformationMatrixResource(){
	//WVP用リソースのリソースを作る。Matrix4x4 1つ分のサイズを用意する
	transformationMatrixResource = *&object3dCommon->GetDxCommon()->CreateBufferResource(sizeof(TransformationMatrix));
	//書き込むためのアドレス取得
	transformationMatrixResource->Map(0, nullptr, reinterpret_cast<void**>(&transformationMatrixData));
	//単位行列を書き込んでおく
	transformationMatrixData->WVP = MakeIdentity4x4();
	transformationMatrixData->World = MakeIdentity4x4();
}

void Object3d::CreateShadowTransformationMatrixResource() {
	shadowTransformationMatrixResource = *&object3dCommon->GetDxCommon()->CreateBufferResource(sizeof(ShadowTransformationMatrix));
	shadowTransformationMatrixResource->Map(0, nullptr, reinterpret_cast<void**>(&shadowTransformationMatrixData));
	shadowTransformationMatrixData->WVP = MakeIdentity4x4();
}

void Object3d::CreateCameraResource() {
	cameraResource = *&object3dCommon->GetDxCommon()->CreateBufferResource(sizeof(CameraForGPU));
	cameraResource->Map(0, nullptr, reinterpret_cast<void**>(&cameraData));
	cameraData->worldPosition = { 0.0f, 0.0f, -10.0f };
	cameraData->environmentCoefficient = 0.0f;
}

void Object3d::CreateMaterialResource() {
	materialResource = *&object3dCommon->GetDxCommon()->CreateBufferResource(sizeof(Material));
	materialResource->Map(0, nullptr, reinterpret_cast<void**>(&materialData));
	materialData->color = Vector4(1.0f, 1.0f, 1.0f, 1.0f);
	materialData->enableLighting = 1;
	materialData->emissiveIntensity = 0.0f;
	materialData->uvTransform = MakeIdentity4x4();
	materialData->emissiveColor = Vector4(1.0f, 1.0f, 1.0f, 1.0f);
	materialData->shininess = 40.0f;
}
