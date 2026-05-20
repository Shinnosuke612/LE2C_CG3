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

	void SetLightColor(const Vector4& color);
	void SetLightDirection(const Vector3& direction);
	void SetLightIntensity(float intensity);

public:
	
	//setter
	void SetDefaultCamera(Camera* camera){ this->defaultCamera = camera; }

	//getter
	DirectXCommon* GetDxCommon() const{return dxCommon_;}
	Camera* GetDefaultCamera() const{ return defaultCamera; }


private://非公開メンバ関数
	Object3dCommon() = default;
	~Object3dCommon() = default;

	//ルートシグネチャの作成
	void MakeRootSignature();
	//グラフィックパイプラインの生成
	void GenerateGraphicsPipeline();

	// どこか共通ヘルパに
	inline D3D12_STATIC_SAMPLER_DESC MakeStaticSamplerS0();

	void CreateDirectionalLightResource();

private://メンバ変数

	struct DirectionalLight {
		Vector4 color;
		Vector3 direction;
		float intensity;
	};

	//処理用にDirectXCommonを保持する
	DirectXCommon* dxCommon_ = nullptr;
	//ルートシグネチャ
	Microsoft::WRL::ComPtr < ID3D12RootSignature> rootSignature_ = nullptr;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> graphicsPipelineState_ = nullptr;
	//デフォルトカメラ
	Camera* defaultCamera = nullptr;

	ID3D12Resource* directionalLightResource_ = nullptr;
	DirectionalLight* directionalLightData_ = nullptr;
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

