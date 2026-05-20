#pragma once
#include <d3dx12.h>
#include "../math/Vector3.h"
#include "../math/Vector4.h"
class DirectXCommon;
class Camera;

class Object3dCommon{
public: //メンバ関数

	// シングルトンインスタンスの取得
	static Object3dCommon* GetInstance();

	// コピー禁止
	Object3dCommon(const Object3dCommon&) = delete;
	Object3dCommon& operator=(const Object3dCommon&) = delete;

	//初期化
	void Initialize(DirectXCommon* dxCommon);
	//共通描画設定
	void SetCommonRenderState();

public:

	struct DirectionalLight {
		Vector4 color;
		Vector3 direction;
		float intensity;
		int32_t enable;
		float padding[3];
	};

	struct PointLight {
		Vector4 color;
		Vector3 position;
		float intensity;
		float radius;
		float decay;
		int32_t enable;
		float padding;
	};

	struct SpotLight {
		Vector4 color;
		Vector3 position;
		float intensity;
		Vector3 direction;
		float distance;
		float decay;
		float cosAngle;
		float cosFalloffStart;
		int32_t enable;
	};

	struct LightingForGPU {
		DirectionalLight directionalLight;
		PointLight pointLight;
		SpotLight spotLight;
	};

public:
	
	//setter
	void SetDefaultCamera(Camera* camera){ this->defaultCamera = camera; }

	void SetLightColor(const Vector4& color);
	void SetLightDirection(const Vector3& direction);
	void SetLightIntensity(float intensity);

	void SetDirectionalLight(const DirectionalLight& light);
	void SetPointLight(const PointLight& light);
	void SetSpotLight(const SpotLight& light);

	//getter
	DirectXCommon* GetDxCommon() const{return dxCommon_;}
	Camera* GetDefaultCamera() const{ return defaultCamera; }
	DirectionalLight* GetDirectionalLight();
	PointLight* GetPointLight();
	SpotLight* GetSpotLight();


private://非公開メンバ関数
	Object3dCommon() = default;
	~Object3dCommon() = default;

	//ルートシグネチャの作成
	void MakeRootSignature();
	//グラフィックパイプラインの生成
	void GenerateGraphicsPipeline();

	// どこか共通ヘルパに
	inline D3D12_STATIC_SAMPLER_DESC MakeStaticSamplerS0();

	void CreateLightingResource();

private://メンバ変数

	//処理用にDirectXCommonを保持する
	DirectXCommon* dxCommon_ = nullptr;
	//ルートシグネチャ
	Microsoft::WRL::ComPtr < ID3D12RootSignature> rootSignature_ = nullptr;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> graphicsPipelineState_ = nullptr;
	//デフォルトカメラ
	Camera* defaultCamera = nullptr;

	ID3D12Resource* lightingResource_ = nullptr;
	LightingForGPU* lightingData_ = nullptr;
private://メンバクラス
	enum class BlendMode{
		//!< ブレンドなし
		kBlendModeNone,
		//!< 通常アルファブレンド
		kBlendModeNormal,
		//!< 加算
		kBlendModeAdd,
		//!< 減算
		kBlendModeSubtract,
		//!< 乗算
		kBlendModeMultiply,
		//!< スクリーン
		kBlendModeScreen,
		//利用してはいけない
		kCountOfBlendMode,
	};
};

