// 役割: 線、球、OBBなどの実行時デバッグ形状を収集して描画する。
#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include <d3d12.h>
#include <wrl.h>

#include "../math/Matrix4x4.h"
#include "../math/Vector3.h"
#include "../math/Vector4.h"

class Camera;
class DirectXCommon;

class DebugRenderer {
public:
	static DebugRenderer* GetInstance();

	void Initialize(
		DirectXCommon* dxCommon,
		uint32_t maxVertexCount = 131072
	);
	void Finalize();
	void Clear();

	void AddLine(
		const Vector3& start,
		const Vector3& end,
		const Vector4& color
	);
	void AddSphere(
		const Vector3& center,
		float radius,
		const Vector4& color,
		uint32_t segments = 12
	);
	void AddOBB(
		const Vector3& center,
		const std::array<Vector3, 3>& axis,
		const Vector3& halfSize,
		const Vector4& color
	);
	void AddSolidSphere(
		const Vector3& center,
		float radius,
		const Vector4& color,
		uint32_t segments = 12
	);
	void AddSolidOBB(
		const Vector3& center,
		const std::array<Vector3, 3>& axis,
		const Vector3& halfSize,
		const Vector4& color
	);
	void AddAxis(const Matrix4x4& worldMatrix, float length);

	void Draw(const Camera* camera);

private:
	struct Vertex {
		Vector3 position;
		Vector4 color;
	};

	struct CameraData {
		Matrix4x4 viewProjection;
	};

	DebugRenderer() = default;
	~DebugRenderer() = default;
	DebugRenderer(const DebugRenderer&) = delete;
	DebugRenderer& operator=(const DebugRenderer&) = delete;

	void CreateRootSignature();
	void CreateGraphicsPipeline();
	void CreateResources();
	void AddTriangle(
		const Vector3& a,
		const Vector3& b,
		const Vector3& c,
		const Vector4& color
	);

	DirectXCommon* dxCommon_ = nullptr;
	Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature_;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> pipelineState_;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> solidPipelineState_;
	Microsoft::WRL::ComPtr<ID3D12Resource> vertexResource_;
	Microsoft::WRL::ComPtr<ID3D12Resource> solidVertexResource_;
	Microsoft::WRL::ComPtr<ID3D12Resource> cameraResource_;
	Vertex* mappedVertices_ = nullptr;
	Vertex* mappedSolidVertices_ = nullptr;
	CameraData* cameraData_ = nullptr;
	D3D12_VERTEX_BUFFER_VIEW vertexBufferView_{};
	D3D12_VERTEX_BUFFER_VIEW solidVertexBufferView_{};
	std::vector<Vertex> vertices_;
	std::vector<Vertex> solidVertices_;
	uint32_t maxVertexCount_ = 0;
	bool isInitialized_ = false;
};
