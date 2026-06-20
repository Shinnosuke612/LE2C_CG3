#pragma once

#include <cstdint>
#include <vector>

#include <d3d12.h>
#include <wrl.h>

#include "../math/Matrix4x4.h"
#include "../math/Vector3.h"
#include "../math/Vector4.h"

class Camera;
class DirectXCommon;

class LightningRenderer {
public:
	struct Settings {
		Vector3 start{ -1.0f, 2.8f, -1.0f };
		Vector3 end{ 1.0f, 1.1f, -1.0f };
		Vector4 coreColor{ 3.5f, 4.5f, 8.0f, 1.0f };
		Vector4 branchColor{ 1.0f, 2.0f, 5.0f, 1.0f };
		float jitter = 0.35f;
		float branchLength = 0.45f;
		float branchProbability = 0.35f;
		float thickness = 0.03f;
		float duration = 0.12f;
		uint32_t segmentCount = 12;
		uint32_t seed = 1;
		bool enabled = false;
		bool previewContinuous = true;
	};

	void Initialize(DirectXCommon* dxCommon, uint32_t maxVertexCount = 65536);
	void Finalize();
	void Update(float deltaTime);
	void Draw(const Camera* camera);
	void DrawImGui(const char* label);
	void Trigger(const Settings& settings);

	Settings& GetSettings() { return settings_; }
	const Settings& GetSettings() const { return settings_; }

private:
	struct Vertex {
		Vector3 position;
		Vector4 color;
	};

	struct CameraData {
		Matrix4x4 viewProjection;
	};

	static float Random01(uint32_t& state);
	static Vector3 RandomUnitVector(uint32_t& state);
	static Vector4 MultiplyAlpha(const Vector4& color, float alphaScale);

	void CreateRootSignature();
	void CreateGraphicsPipeline();
	void CreateResources(uint32_t maxVertexCount);
	void BuildVertices(const Camera* camera);
	void AddLightningSegment(
		const Vector3& start,
		const Vector3& end,
		const Vector4& color,
		float thickness,
		const Camera* camera,
		uint32_t& state
	);
	void AddRibbon(
		const Vector3& start,
		const Vector3& end,
		const Vector4& color,
		float halfWidth,
		const Camera* camera
	);

	Settings settings_{};
	DirectXCommon* dxCommon_ = nullptr;
	Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature_;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> pipelineState_;
	Microsoft::WRL::ComPtr<ID3D12Resource> vertexResource_;
	Microsoft::WRL::ComPtr<ID3D12Resource> cameraResource_;
	Vertex* mappedVertices_ = nullptr;
	CameraData* cameraData_ = nullptr;
	D3D12_VERTEX_BUFFER_VIEW vertexBufferView_{};
	std::vector<Vertex> vertices_;
	uint32_t maxVertexCount_ = 0;
	float time_ = 0.0f;
	float activeTime_ = 0.0f;
};
