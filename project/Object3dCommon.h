#pragma once
#include <d3dx12.h>
class DirectXCommon;
class Camera;

class Object3dCommon{
public: //メンバ関数
	//初期化
	void Initialize(DirectXCommon* dxCommon);
	//共通描画設定
	void SetCommonRenderState();

public:
	
	//setter
	void SetDefaultCamera(Camera* camera){ this->defaultCamera = camera; }

	//getter
	DirectXCommon* GetDxCommon() const{return dxCommon_;}
	Camera* GetDefaultCamera() const{ return defaultCamera; }


private://非公開メンバ関数

	//ルートシグネチャの作成
	void MakeRootSignature();
	//グラフィックパイプラインの生成
	void GenerateGraphicsPipeline();

	// どこか共通ヘルパに
	inline D3D12_STATIC_SAMPLER_DESC MakeStaticSamplerS0();

private://メンバ変数

	//処理用にDirectXCommonを保持する
	DirectXCommon* dxCommon_;
	//ルートシグネチャ
	Microsoft::WRL::ComPtr < ID3D12RootSignature> rootSignature_ = nullptr;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> graphicsPipelineState_ = nullptr;
	//デフォルトカメラ
	Camera* defaultCamera = nullptr;
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

