#pragma once
#include <cstdint>
#include <string>
#include <fstream>
#include <sstream>
#include <cassert>
#include <memory>
#include <vector>
#include "../math/Vector2.h"
#include "../math/Vector3.h"
#include "../math/Vector4.h"
#include "../math/Matrix4x4.h"
#include "../math/Transform.h"
#include "Object3dCommon.h"
#include "Skeleton.h"
#include "SkinCluster.h"
#include <d3d12.h> 

class Model;
class Camera;

class Object3d{
private://インナークラス

	//座標変換行列データ
	struct TransformationMatrix{
		Matrix4x4 WVP;
		Matrix4x4 World;
	};

	struct CameraForGPU {
		Vector3 worldPosition;
		float environmentCoefficient;
	};

	struct ShadowTransformationMatrix {
		Matrix4x4 WVP;
	};

	struct Material {
		Vector4 color;
		int32_t enableLighting;
		float emissiveIntensity;
		float padding[2];
		Matrix4x4 uvTransform;
		Vector4 emissiveColor;
		float shininess;
		float dissolveAmount;
		float dissolveEdgeWidth;
		float dissolveNoiseScale;
	};

public: //公開メンバ関数
	//初期化
	void Initialize(Object3dCommon* object3dCommon);
	//更新
	void Update();
	void UpdateForCamera(Camera* camera);
	//描画
	void Draw();
	void DrawShadow(const Matrix4x4& lightViewProjection);
	void DrawSkeletonDebug(
		bool drawJointNames = false,
		bool drawJointAxes = true,
		float jointRadius = 0.025f,
		float axisLength = 0.08f
	) const;
	//setter
	void SetModel(Model* model);
	// setter
	void SetScale(const Vector3& scale){ transform.scale = scale; }
	void SetRotate(const Vector3& rotate){
		transform.rotate = rotate;
		transform.useQuaternionRotation = false;
	}
	void SetRotateQuaternion(const Quaternion& rotate);
	void ClearRotateQuaternion() { transform.useQuaternionRotation = false; }
	void SetTranslate(const Vector3& translate){transform.translate = translate;}
	void SetModel(const std::string& filePath);
	void SetCamera(Camera* camera){ this->camera = camera; }
	void SetParent(const Object3d* parent) { parent_ = parent; }
	void SetCullMode(Object3dCommon::CullMode cullMode) {
		cullMode_ = cullMode;
	}
	void SetEnvironmentMap(const std::string& textureFilePath, float coefficient);
	void SetEnvironmentCoefficient(float coefficient);
	void SetAnimationPlaying(bool isPlaying) { isAnimationPlaying_ = isPlaying; }
	void SetAnimationLoop(bool isLooping) { isAnimationLooping_ = isLooping; }
	void SetAnimationSpeed(float speed) { animationSpeed_ = speed; }
	void ResetAnimation();
	void SetColor(const Vector4& color);
	void SetEnableLighting(bool enableLighting);
	void SetEmissive(float intensity, const Vector4& color = { 1.0f, 1.0f, 1.0f, 1.0f });
	void SetDissolve(
		float amount,
		float edgeWidth = 0.08f,
		float noiseScale = 6.0f
	);
	void SetTextureOverride(D3D12_GPU_DESCRIPTOR_HANDLE handle) {
		textureOverrideHandle_ = handle;
	}
	void ClearTextureOverride() { textureOverrideHandle_ = {}; }
	uint64_t GetTextureOverridePtr() const {
		return textureOverrideHandle_.ptr;
	}

	// getter（参照返しが軽くて安全）
	const Vector3& GetScale() const{return transform.scale;}
	const Vector3& GetRotate() const{return transform.rotate;}
	const Vector3& GetTranslate() const{return transform.translate;}
	const Quaternion& GetRotateQuaternion() const {
		return transform.quaternionRotate;
	}
	bool UsesQuaternionRotation() const {
		return transform.useQuaternionRotation;
	}
	const Transform& GetTransform() const { return transform; }
	Transform& GetTransform() { return transform; }
	bool HasAnimation() const;
	bool IsAnimationPlaying() const { return isAnimationPlaying_; }
	bool IsAnimationLooping() const { return isAnimationLooping_; }
	float GetAnimationSpeed() const { return animationSpeed_; }
	float GetAnimationTime() const { return animationTime_; }
	float GetAnimationDuration() const;
	const Skeleton* GetSkeleton() const {
		return skeleton_.IsValid() ? &skeleton_ : nullptr;
	}
private: //非公開メンバ関数

	//座標変換行列用リソース作成関数
	void CreateTransformationMatrixResource();

	void CreateCameraResource();
	void CreateShadowTransformationMatrixResource();
	void CreateMaterialResource();
	void UpdateInternal(bool advanceAnimation);
private://メンバ変数
	Transform transform;
	Matrix4x4 objectWorldMatrix_ = MakeIdentity4x4();
	const Object3d* parent_ = nullptr;

	Object3dCommon* object3dCommon = nullptr;


	//バッファリソース
	ID3D12Resource* transformationMatrixResource;
	//バッファリソース内のデータおw指すポインタ
	TransformationMatrix* transformationMatrixData = nullptr;
	ID3D12Resource* shadowTransformationMatrixResource = nullptr;
	ShadowTransformationMatrix* shadowTransformationMatrixData = nullptr;

	//見た目用のモデル
	Model* model = nullptr;
	Skeleton skeleton_{};
	std::unique_ptr<SkinCluster> skinCluster_;
	//カメラ
	Camera* camera = nullptr;

	ID3D12Resource* cameraResource;
	CameraForGPU* cameraData = nullptr;
	ID3D12Resource* materialResource = nullptr;
	Material* materialData = nullptr;
	std::string environmentTextureFilePath_;
	float animationTime_ = 0.0f;
	float animationSpeed_ = 1.0f;
	bool isAnimationPlaying_ = true;
	bool isAnimationLooping_ = true;
	D3D12_GPU_DESCRIPTOR_HANDLE textureOverrideHandle_{};
	Object3dCommon::CullMode cullMode_ = Object3dCommon::CullMode::kBack;
};

