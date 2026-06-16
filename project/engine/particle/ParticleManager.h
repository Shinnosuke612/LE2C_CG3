#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include <wrl.h>
#include <d3d12.h>

#include "../math/Vector3.h"
#include "../math/Vector4.h"
#include "../math/Vector2.h"
#include "../math/Transform.h"
#include "../math/Matrix4x4.h"

#include "ParticleCommon.h"
#include "GpuParticle.h"

class SrvManager;
class Camera;

class ParticleManager {
private:
	static ParticleManager* instance_;

	ParticleManager() = default;
	~ParticleManager() = default;
	ParticleManager(const ParticleManager&) = delete;
	ParticleManager& operator=(const ParticleManager&) = delete;

public:
	static ParticleManager* GetInstance();
	static void DeleteInstance();

public:
	struct TransformationMatrix {
		Matrix4x4 WVP;
		Matrix4x4 World;
		Vector4 color;
	};

	struct Material {
		Vector4 color;
		int32_t enableLighting;
		float alphaCutoff;
		int32_t flipU;
		int32_t flipV;
		Matrix4x4 uvTransform;
	};

	struct DirectionalLight {
		Vector4 color;
		Vector3 direction;
		float intensity;
	};

	enum class ColorChangeMode {
		kConstant,     // 最初の色のまま
		kOverLife,    // 寿命に応じて startColor から endColor へ変化
		kRandomLoop,  // 生存中ずっとランダム色へ変化し続ける
	};

	enum class MovementMode {
		kLinear,        // 通常の速度、加速度移動
		kVortexInward,  // 中心を回りながら吸い込まれる
	};

	enum class VortexAxis {
		kX, // X軸まわり。YZ平面で回る
		kY, // Y軸まわり。XZ平面で回る
		kZ, // Z軸まわり。XY平面で回る
	};

	struct ParticleLifeDesc {
		bool isLooping = false;
		float loopDuration = 1.0f;
		bool loopPingPong = true;

		float lifeTimeMin = 1.0f;
		float lifeTimeMax = 1.0f;

		bool enableLifeFade = true;
		float fadeOutStartRatio = 0.7f;
	};

	struct ParticleScaleDesc {
		Vector3 startScaleMin = { 1.0f, 1.0f, 1.0f };
		Vector3 startScaleMax = { 1.0f, 1.0f, 1.0f };

		bool enableScaleOverLife = false;

		Vector3 endScaleMin = { 1.0f, 1.0f, 1.0f };
		Vector3 endScaleMax = { 1.0f, 1.0f, 1.0f };
	};

	struct ParticleRotationDesc {
		Vector3 initialRotationMin = { 0.0f, 0.0f, 0.0f };
		Vector3 initialRotationMax = { 0.0f, 0.0f, 0.0f };
		bool enableRotationOverTime = false;
		Vector3 rotationSpeed = { 0.0f, 0.0f, 0.0f };
	};

	struct ParticleLinearMotionDesc {
		Vector3 baseVelocity = { 0.0f, -1.8f, 0.0f };
		Vector3 velocityRandomRange = { 0.0f, 0.0f, 0.01f };

		Vector3 baseAcceleration = { 0.0f, -0.001f, 0.0f };
		Vector3 accelerationRandomRange = { 0.0f, 0.0f, 0.0f };
	};

	struct ParticleSwayDesc {
		float amplitude = 0.0f;
		float frequency = 0.0f;
	};

	struct ParticleVortexDesc {
		// 渦の中心
		Vector3 center = { 0.0f, 0.0f, 0.0f };

		// 回転軸
		// kX: YZ平面で回る
		// kY: XZ平面で回る
		// kZ: XY平面で回る
		VortexAxis axis = VortexAxis::kY;

		// 角速度。マイナス値を使うと逆回転も可能
		float angularSpeedMin = 4.0f;
		float angularSpeedMax = 8.0f;

		// 中心へ近づく速さ
		float inwardSpeedMin = 0.8f;
		float inwardSpeedMax = 1.8f;

		// 回転軸方向への移動速度
		// axis が kY のときは Y方向、kX のときは X方向、kZ のときは Z方向
		float verticalSpeedMin = -0.1f;
		float verticalSpeedMax = 0.1f;
	};

	struct ParticleMotionDesc {
		MovementMode mode = MovementMode::kLinear;

		ParticleLinearMotionDesc linear;
		ParticleSwayDesc sway;
		ParticleVortexDesc vortex;
	};

	struct ParticleColorDesc {
		ColorChangeMode mode = ColorChangeMode::kConstant;

		Vector4 startColorMin = { 1.0f, 1.0f, 1.0f, 1.0f };
		Vector4 startColorMax = { 1.0f, 1.0f, 1.0f, 1.0f };

		Vector4 endColorMin = { 1.0f, 1.0f, 1.0f, 1.0f };
		Vector4 endColorMax = { 1.0f, 1.0f, 1.0f, 1.0f };

		// RandomLoop用
		Vector4 randomColorMin = { 0.0f, 0.0f, 0.0f, 1.0f };
		Vector4 randomColorMax = { 1.0f, 1.0f, 1.0f, 1.0f };

		float randomColorChangeIntervalMin = 0.15f;
		float randomColorChangeIntervalMax = 0.35f;
		float randomColorLerpSpeed = 6.0f;
	};

	enum class BillboardMode {
		kNone,
		kBillboard,
	};

	enum class PrimitiveType {
		kPlane,
		kRing,
		kCylinder,
	};

	enum class RingUvMode {
		kHorizontal,
		kVertical,
	};

	struct RingPrimitiveDesc {
		uint32_t divisions = 32;
		float outerRadius = 1.0f;
		float innerRadius = 0.2f;
		float startAngle = 0.0f;
		float endAngle = 6.2831853f;
		Vector4 outerColor = { 1.0f, 1.0f, 1.0f, 1.0f };
		Vector4 innerColor = { 1.0f, 1.0f, 1.0f, 0.0f };
		RingUvMode uvMode = RingUvMode::kHorizontal;
	};

	struct CylinderPrimitiveDesc {
		uint32_t divisions = 32;
		float topRadius = 1.0f;
		float bottomRadius = 1.0f;
		float height = 3.0f;
		float startAngle = 0.0f;
		float endAngle = 6.2831853f;
		Vector4 topColor = { 1.0f, 1.0f, 1.0f, 1.0f };
		Vector4 bottomColor = { 1.0f, 1.0f, 1.0f, 1.0f };
		RingUvMode uvMode = RingUvMode::kHorizontal;
	};

	struct ParticleRenderDesc {
		BillboardMode billboardMode = BillboardMode::kBillboard;
		PrimitiveType primitiveType = PrimitiveType::kPlane;
		RingPrimitiveDesc ring;
		CylinderPrimitiveDesc cylinder;
		Vector2 uvScrollSpeed = { 0.0f, 0.0f };
		bool flipU = false;
		bool flipV = false;
		float alphaCutoff = 0.0f;
		ParticleCommon::CullMode cullMode = ParticleCommon::CullMode::kNone;
		bool depthTest = true;
		bool depthWrite = false;
	};

	struct ParticleBehavior {
		ParticleLifeDesc life;
		ParticleScaleDesc scale;
		ParticleRotationDesc rotation;
		ParticleMotionDesc motion;
		ParticleColorDesc color;
		ParticleRenderDesc render;
	};

	struct Particle {
		Transform transform;

		Vector3 velocity;
		Vector3 acceleration;

		Vector3 startScale = { 1.0f, 1.0f, 1.0f };
		Vector3 endScale = { 1.0f, 1.0f, 1.0f };
		bool enableScaleOverLife = false;

		float currentTime = 0.0f;
		float lifeTime = 1.0f;
		bool isLooping = false;
		float loopDuration = 1.0f;
		bool loopPingPong = true;

		Vector4 color;
		Vector4 startColor;
		Vector4 endColor;

		ColorChangeMode colorChangeMode = ColorChangeMode::kConstant;

		Vector4 randomCurrentColor;
		Vector4 randomTargetColor;
		Vector4 randomColorMin;
		Vector4 randomColorMax;

		float randomColorChangeTimer = 0.0f;
		float randomColorChangeInterval = 0.0f;
		float randomColorChangeIntervalMin = 0.0f;
		float randomColorChangeIntervalMax = 0.0f;
		float randomColorLerpSpeed = 0.0f;

		bool enableLifeFade = true;
		float fadeOutStartRatio = 0.7f;
		bool enableRotationOverTime = false;
		Vector3 rotationSpeed = { 0.0f, 0.0f, 0.0f };

		MovementMode movementMode = MovementMode::kLinear;

		float swayTime = 0.0f;
		float swayPhase = 0.0f;
		Vector3 swayAxis = { 0.0f, 0.0f, 0.0f };
		float swayAmplitude = 0.0f;
		float swayFrequency = 0.0f;

		Vector3 vortexCenter = { 0.0f, 0.0f, 0.0f };
		VortexAxis vortexAxis = VortexAxis::kY;

		float vortexAngle = 0.0f;
		float vortexRadius = 0.0f;
		float vortexAngularSpeed = 0.0f;
		float vortexInwardSpeed = 0.0f;
		float vortexVerticalSpeed = 0.0f;
		float vortexHeightOffset = 0.0f;

		BillboardMode billboardMode = BillboardMode::kBillboard;
	};

private:
	struct ParticleGroup {
		std::string textureFilePath;
		uint32_t textureSrvIndex = 0;

		std::vector<Particle> particles;

		Microsoft::WRL::ComPtr<ID3D12Resource> materialResource;
		Material* materialData = nullptr;

		Microsoft::WRL::ComPtr<ID3D12Resource> instancingResource;
		TransformationMatrix* instancingData = nullptr;

		uint32_t instanceSrvIndex = 0;
		uint32_t instanceCount = 0;

		ParticleCommon::BlendMode blendMode = ParticleCommon::BlendMode::kBlendModeAdd;
		ParticleRenderDesc render;
		Microsoft::WRL::ComPtr<ID3D12Resource> vertexResource;
		D3D12_VERTEX_BUFFER_VIEW vertexBufferView{};
		uint32_t vertexCount = 0;
		Vector2 uvOffset{};
	};

public:
	static const uint32_t kMaxInstanceCount = 20480;

public:
	void Initialize(ParticleCommon* particleCommon, SrvManager* srvManager);
	void Reset();
	void Update();
	void Draw();

	void CreateParticleGroup(const std::string& name, const std::string& textureFilePath);

	bool HasParticleGroup(const std::string& name) const;
	void ClearParticleGroup(const std::string& name);
	void CreateParticleGroupIfNeeded(
		const std::string& name,
		const std::string& textureFilePath
	);

	void Emit(
		const std::string& name,
		const Vector3& position,
		const Vector3& spawnSize,
		uint32_t count,
		const ParticleBehavior& behavior
	);

	void SetCamera(Camera* camera) { camera_ = camera; }

	void SetGroupBlendMode(const std::string& name, ParticleCommon::BlendMode blendMode);
	void SetGroupRenderDesc(const std::string& name, const ParticleRenderDesc& render);
	void SetGpuParticleEnabled(bool enabled) { gpuParticleEnabled_ = enabled; }
	bool IsGpuParticleEnabled() const { return gpuParticleEnabled_; }

private:
	void CreateDirectionalLightResource();
	void CreateGroupVertexResource(ParticleGroup& group);

	float RandomRange(float min, float max);
	Vector3 RandomVector3Range(const Vector3& min, const Vector3& max);
	Vector4 RandomVector4Range(const Vector4& min, const Vector4& max);

	Vector4 LerpColor(const Vector4& start, const Vector4& end, float t);

	void InitializeParticleLife(Particle& particle, const ParticleBehavior& behavior);
	void InitializeParticleMotion(Particle& particle, const ParticleBehavior& behavior);
	void InitializeParticleColor(Particle& particle, const ParticleBehavior& behavior);

	void UpdateParticleMotion(Particle& particle);
	void UpdateParticleColor(Particle& particle);
	void UpdateParticleRotation(Particle& particle);

	bool IsDeadParticle(const Particle& particle) const;
	float GetAnimationRatio(const Particle& particle) const;

	Vector3 LerpVector3(const Vector3& start, const Vector3& end, float t);
	void UpdateParticleScale(Particle& particle);

private:
	ParticleCommon* particleCommon_ = nullptr;
	SrvManager* srvManager_ = nullptr;
	Camera* camera_ = nullptr;

	std::unordered_map<std::string, ParticleGroup> particleGroups_;
	GpuParticle gpuParticle_;
	bool gpuParticleEnabled_ = false;

	Microsoft::WRL::ComPtr<ID3D12Resource> directionalLightResource_;
	DirectionalLight* directionalLightData_ = nullptr;

	float deltaTime_ = 1.0f / 60.0f;
};
