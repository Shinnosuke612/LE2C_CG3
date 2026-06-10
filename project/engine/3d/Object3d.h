#pragma once
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
#include "Skeleton.h"
#include "SkinCluster.h"
#include <d3d12.h> 

class Object3dCommon;
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

public: //公開メンバ関数
	//初期化
	void Initialize(Object3dCommon* object3dCommon);
	//更新
	void Update();
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
	void SetRotate(const Vector3& rotate){ transform.rotate = rotate; }
	void SetTranslate(const Vector3& translate){transform.translate = translate;}
	void SetModel(const std::string& filePath);
	void SetCamera(Camera* camera){ this->camera = camera; }
	void SetEnvironmentMap(const std::string& textureFilePath, float coefficient);
	void SetEnvironmentCoefficient(float coefficient);
	void SetAnimationPlaying(bool isPlaying) { isAnimationPlaying_ = isPlaying; }
	void SetAnimationLoop(bool isLooping) { isAnimationLooping_ = isLooping; }
	void SetAnimationSpeed(float speed) { animationSpeed_ = speed; }
	void ResetAnimation();

	// getter（参照返しが軽くて安全）
	const Vector3& GetScale() const{return transform.scale;}
	const Vector3& GetRotate() const{return transform.rotate;}
	const Vector3& GetTranslate() const{return transform.translate;}
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
private://メンバ変数
	Transform transform;

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
	std::string environmentTextureFilePath_;
	float animationTime_ = 0.0f;
	float animationSpeed_ = 1.0f;
	bool isAnimationPlaying_ = true;
	bool isAnimationLooping_ = true;
};

