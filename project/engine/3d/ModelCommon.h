// 役割: モデル描画で共有するRootSignatureとPipelineStateを管理する。
#pragma once
#include <d3dx12.h>
class DirectXCommon;

class ModelCommon{

public:
	void Initialize(DirectXCommon* dxCommon);
public://ゲッター
	DirectXCommon* GetDxCommon() const{
		return dxCommon_;
	}
private://メンバ変数

	//引数で受け取ってメンバ変数に記録する
	DirectXCommon* dxCommon_;


};

