#include "GpuParticle.h"
#include "../base/RenderFormats.h"

#include "ParticleCommon.h"
#include "../2d/TextureManager.h"
#include "../3d/Camera.h"
#include "../3d/SrvManager.h"
#include "../base/DirectXCommon.h"
#include "../externals/nlohmann/json.hpp"
#include "../utility/Logger.h"

#include <algorithm>
#include <cassert>
#include <cctype>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <unordered_map>
#include <vector>

#ifdef _DEBUG
#include <imgui.h>
#endif

using json = nlohmann::json;

namespace {

constexpr float kMinEmitterFrequency = 1.0f / 60.0f;

float NormalizeEmitterFrequency(float frequency) {
	return frequency > 0.0f ? frequency : kMinEmitterFrequency;
}

const char* ToString(ParticleCommon::BlendMode mode) {
	switch (mode) {
	case ParticleCommon::BlendMode::kBlendModeNone: return "None";
	case ParticleCommon::BlendMode::kBlendModeNormal: return "Normal";
	case ParticleCommon::BlendMode::kBlendModeAdd: return "Add";
	case ParticleCommon::BlendMode::kBlendModeSubtract: return "Subtract";
	case ParticleCommon::BlendMode::kBlendModeMultiply: return "Multiply";
	case ParticleCommon::BlendMode::kBlendModeScreen: return "Screen";
	default: return "Add";
	}
}

ParticleCommon::BlendMode ToBlendMode(const std::string& text) {
	if (text == "None") return ParticleCommon::BlendMode::kBlendModeNone;
	if (text == "Normal") return ParticleCommon::BlendMode::kBlendModeNormal;
	if (text == "Add") return ParticleCommon::BlendMode::kBlendModeAdd;
	if (text == "Subtract") return ParticleCommon::BlendMode::kBlendModeSubtract;
	if (text == "Multiply") return ParticleCommon::BlendMode::kBlendModeMultiply;
	if (text == "Screen") return ParticleCommon::BlendMode::kBlendModeScreen;
	return ParticleCommon::BlendMode::kBlendModeAdd;
}

void ApplyBlendMode(
	D3D12_BLEND_DESC& blendDesc,
	ParticleCommon::BlendMode blendMode
) {
	blendDesc = {};
	blendDesc.RenderTarget[0].RenderTargetWriteMask =
		D3D12_COLOR_WRITE_ENABLE_ALL;

	if (blendMode == ParticleCommon::BlendMode::kBlendModeNone) {
		blendDesc.RenderTarget[0].BlendEnable = FALSE;
		return;
	}

	blendDesc.RenderTarget[0].BlendEnable = TRUE;
	if (blendMode == ParticleCommon::BlendMode::kBlendModeNormal) {
		blendDesc.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
		blendDesc.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
		blendDesc.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
	}
	else if (blendMode == ParticleCommon::BlendMode::kBlendModeAdd) {
		blendDesc.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
		blendDesc.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
		blendDesc.RenderTarget[0].DestBlend = D3D12_BLEND_ONE;
	}
	else if (blendMode == ParticleCommon::BlendMode::kBlendModeSubtract) {
		blendDesc.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
		blendDesc.RenderTarget[0].BlendOp = D3D12_BLEND_OP_REV_SUBTRACT;
		blendDesc.RenderTarget[0].DestBlend = D3D12_BLEND_ONE;
	}
	else if (blendMode == ParticleCommon::BlendMode::kBlendModeMultiply) {
		blendDesc.RenderTarget[0].SrcBlend = D3D12_BLEND_DEST_COLOR;
		blendDesc.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
		blendDesc.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
	}
	else if (blendMode == ParticleCommon::BlendMode::kBlendModeScreen) {
		blendDesc.RenderTarget[0].SrcBlend = D3D12_BLEND_INV_DEST_COLOR;
		blendDesc.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
		blendDesc.RenderTarget[0].DestBlend = D3D12_BLEND_ONE;
	}

	blendDesc.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ZERO;
	blendDesc.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
	blendDesc.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ONE;
}

Microsoft::WRL::ComPtr<ID3D12Resource> CreateUavBufferResource(
	ID3D12Device* device,
	size_t sizeInBytes
) {
	assert(device);

	D3D12_HEAP_PROPERTIES heapProperties{};
	heapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;

	D3D12_RESOURCE_DESC resourceDesc{};
	resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	resourceDesc.Width = sizeInBytes;
	resourceDesc.Height = 1;
	resourceDesc.DepthOrArraySize = 1;
	resourceDesc.MipLevels = 1;
	resourceDesc.SampleDesc.Count = 1;
	resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
	resourceDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

	Microsoft::WRL::ComPtr<ID3D12Resource> resource;
	const HRESULT hr = device->CreateCommittedResource(
		&heapProperties,
		D3D12_HEAP_FLAG_NONE,
		&resourceDesc,
		D3D12_RESOURCE_STATE_COMMON,
		nullptr,
		IID_PPV_ARGS(&resource)
	);
	assert(SUCCEEDED(hr));
	return resource;
}

D3D12_STATIC_SAMPLER_DESC MakeLinearSampler() {
	D3D12_STATIC_SAMPLER_DESC sampler{};
	sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
	sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
	sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
	sampler.ShaderRegister = 0;
	sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	return sampler;
}

json ToJson(const Vector3& value) {
	return json::array({ value.x, value.y, value.z });
}

json ToJson(const Vector4& value) {
	return json::array({ value.x, value.y, value.z, value.w });
}

Vector3 ReadVector3(const json& value, const Vector3& fallback) {
	if (!value.is_array() || value.size() < 3) {
		return fallback;
	}

	return {
		value.at(0).get<float>(),
		value.at(1).get<float>(),
		value.at(2).get<float>()
	};
}

Vector4 ReadVector4(const json& value, const Vector4& fallback) {
	if (!value.is_array() || value.size() < 4) {
		return fallback;
	}

	return {
		value.at(0).get<float>(),
		value.at(1).get<float>(),
		value.at(2).get<float>(),
		value.at(3).get<float>()
	};
}

bool IsCsvPath(const std::string& filePath) {
	const size_t dot = filePath.find_last_of('.');
	if (dot == std::string::npos) {
		return false;
	}

	std::string extension = filePath.substr(dot);
	std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) {
		return static_cast<char>(std::tolower(c));
	});
	return extension == ".csv";
}

std::vector<std::string> SplitCsvLine(const std::string& line) {
	std::vector<std::string> values;
	std::stringstream stream(line);
	std::string value;
	while (std::getline(stream, value, ',')) {
		values.push_back(value);
	}
	return values;
}

float ReadCsvFloat(
	const std::unordered_map<std::string, std::vector<std::string>>& table,
	const std::string& key,
	size_t index,
	float fallback
) {
	const auto it = table.find(key);
	if (it == table.end() || it->second.size() <= index + 1) {
		return fallback;
	}

	return std::stof(it->second[index + 1]);
}

uint32_t ReadCsvUint(
	const std::unordered_map<std::string, std::vector<std::string>>& table,
	const std::string& key,
	uint32_t fallback
) {
	const auto it = table.find(key);
	if (it == table.end() || it->second.size() <= 1) {
		return fallback;
	}

	return static_cast<uint32_t>(std::stoul(it->second[1]));
}

Vector3 ReadCsvVector3(
	const std::unordered_map<std::string, std::vector<std::string>>& table,
	const std::string& key,
	const Vector3& fallback
) {
	return {
		ReadCsvFloat(table, key, 0, fallback.x),
		ReadCsvFloat(table, key, 1, fallback.y),
		ReadCsvFloat(table, key, 2, fallback.z)
	};
}

Vector4 ReadCsvVector4(
	const std::unordered_map<std::string, std::vector<std::string>>& table,
	const std::string& key,
	const Vector4& fallback
) {
	return {
		ReadCsvFloat(table, key, 0, fallback.x),
		ReadCsvFloat(table, key, 1, fallback.y),
		ReadCsvFloat(table, key, 2, fallback.z),
		ReadCsvFloat(table, key, 3, fallback.w)
	};
}

} // namespace

void GpuParticle::Initialize(
	ParticleCommon* particleCommon,
	SrvManager* srvManager,
	const std::string& textureFilePath
) {
	assert(particleCommon);
	assert(srvManager);

	particleCommon_ = particleCommon;
	srvManager_ = srvManager;
	dxCommon_ = particleCommon_->GetDxCommon();
	config_.textureFilePath = textureFilePath;
	config_.filePath = "resources/particles/gpu_particle.json";
	textureFilePath_ = textureFilePath;

	const bool loaded = ApplyTexture(textureFilePath_);
	assert(loaded);

	CreateParticleResource();
	CreateConstantBuffers();
	CreateRootSignatures();
	CreatePipelineStates();
	if (!LoadConfig(config_.filePath)) {
		ApplyConfigToGpu();
	}
	CopyStringsToBuffers();

	needsInitialize_ = true;
}

void GpuParticle::Reset() {
	particleResource_.Reset();
	freeListIndexResource_.Reset();
	freeListResource_.Reset();
	materialResource_.Reset();
	directionalLightResource_.Reset();
	perViewResource_.Reset();
	emitterResource_.Reset();
	perFrameResource_.Reset();
	behaviorResource_.Reset();
	graphicsRootSignature_.Reset();
	for (auto& pipelineState : graphicsPipelineStates_) {
		pipelineState.Reset();
	}
	initializeRootSignature_.Reset();
	initializePipelineState_.Reset();
	emitRootSignature_.Reset();
	emitPipelineState_.Reset();
	updatePipelineState_.Reset();

	materialData_ = nullptr;
	directionalLightData_ = nullptr;
	perViewData_ = nullptr;
	emitterData_ = nullptr;
	perFrameData_ = nullptr;
	behaviorData_ = nullptr;
	particleCommon_ = nullptr;
	srvManager_ = nullptr;
	dxCommon_ = nullptr;
	textureFilePath_.clear();
	textureSrvIndex_ = 0;
	particleSrvIndex_ = 0;
	particleUavIndex_ = 0;
	freeListIndexUavIndex_ = 0;
	freeListUavIndex_ = 0;
	particleResourceState_ = D3D12_RESOURCE_STATE_COMMON;
	freeListIndexResourceState_ = D3D12_RESOURCE_STATE_COMMON;
	freeListResourceState_ = D3D12_RESOURCE_STATE_COMMON;
	needsInitialize_ = true;
	elapsedTime_ = 0.0f;
	config_ = {};
	std::memset(configPathBuffer_, 0, sizeof(configPathBuffer_));
	std::memset(texturePathBuffer_, 0, sizeof(texturePathBuffer_));
}

void GpuParticle::CreateParticleResource() {
	assert(dxCommon_);
	assert(srvManager_);
	assert(srvManager_->CanAllocate());

	particleResource_ = CreateUavBufferResource(
		dxCommon_->GetDevice(),
		sizeof(ParticleData) * kMaxParticles
	);
	particleResourceState_ = D3D12_RESOURCE_STATE_COMMON;

	particleSrvIndex_ = srvManager_->Allocate();
	srvManager_->CreateSRVforStructuredBuffer(
		particleSrvIndex_,
		particleResource_.Get(),
		kMaxParticles,
		sizeof(ParticleData)
	);

	assert(srvManager_->CanAllocate());
	particleUavIndex_ = srvManager_->Allocate();
	srvManager_->CreateUAVforStructuredBuffer(
		particleUavIndex_,
		particleResource_.Get(),
		kMaxParticles,
		sizeof(ParticleData)
	);

	assert(srvManager_->CanAllocate());
	freeListIndexResource_ =
		CreateUavBufferResource(dxCommon_->GetDevice(), sizeof(int32_t));
	freeListIndexResourceState_ = D3D12_RESOURCE_STATE_COMMON;

	freeListIndexUavIndex_ = srvManager_->Allocate();
	srvManager_->CreateUAVforStructuredBuffer(
		freeListIndexUavIndex_,
		freeListIndexResource_.Get(),
		1,
		sizeof(int32_t)
	);

	assert(srvManager_->CanAllocate());
	freeListResource_ = CreateUavBufferResource(
		dxCommon_->GetDevice(),
		sizeof(uint32_t) * kMaxParticles
	);
	freeListResourceState_ = D3D12_RESOURCE_STATE_COMMON;

	freeListUavIndex_ = srvManager_->Allocate();
	srvManager_->CreateUAVforStructuredBuffer(
		freeListUavIndex_,
		freeListResource_.Get(),
		kMaxParticles,
		sizeof(uint32_t)
	);
}

void GpuParticle::CreateConstantBuffers() {
	materialResource_ = dxCommon_->CreateBufferResource(sizeof(Material));
	materialResource_->Map(0, nullptr, reinterpret_cast<void**>(&materialData_));
	materialData_->color = { 1.0f, 1.0f, 1.0f, 1.0f };
	materialData_->enableLighting = false;
	materialData_->alphaCutoff = 0.0f;
	materialData_->flipU = false;
	materialData_->flipV = false;
	materialData_->uvTransform = MakeIdentity4x4();
	materialData_->emissiveIntensity = 1.0f;

	directionalLightResource_ =
		dxCommon_->CreateBufferResource(sizeof(DirectionalLight));
	directionalLightResource_->Map(
		0,
		nullptr,
		reinterpret_cast<void**>(&directionalLightData_)
	);
	directionalLightData_->color = { 1.0f, 1.0f, 1.0f, 1.0f };
	directionalLightData_->direction = { 0.0f, -1.0f, 0.0f };
	directionalLightData_->intensity = 1.0f;

	perViewResource_ = dxCommon_->CreateBufferResource(sizeof(PerView));
	perViewResource_->Map(0, nullptr, reinterpret_cast<void**>(&perViewData_));
	std::memset(perViewData_, 0, sizeof(PerView));

	emitterResource_ = dxCommon_->CreateBufferResource(sizeof(EmitterSphere));
	emitterResource_->Map(0, nullptr, reinterpret_cast<void**>(&emitterData_));
	*emitterData_ = config_.emitter;

	perFrameResource_ = dxCommon_->CreateBufferResource(sizeof(PerFrame));
	perFrameResource_->Map(0, nullptr, reinterpret_cast<void**>(&perFrameData_));
	perFrameData_->time = 0.0f;
	perFrameData_->deltaTime = deltaTime_;
	perFrameData_->padding[0] = 0.0f;
	perFrameData_->padding[1] = 0.0f;

	behaviorResource_ = dxCommon_->CreateBufferResource(sizeof(BehaviorForGPU));
	behaviorResource_->Map(0, nullptr, reinterpret_cast<void**>(&behaviorData_));
	ApplyConfigToGpu();
}

void GpuParticle::ApplyConfigToGpu() {
	config_.emitter.frequency = NormalizeEmitterFrequency(config_.emitter.frequency);
	if (emitterData_) {
		const uint32_t emit = emitterData_->emit;
		const float frequencyTime = emitterData_->frequencyTime;
		*emitterData_ = config_.emitter;
		emitterData_->emit = emit;
		emitterData_->frequencyTime = frequencyTime;
	}

	if (behaviorData_) {
		const BehaviorSettings& behavior = config_.behavior;
		behaviorData_->lifeScaleVelocityMinRotationMin = {
			behavior.lifeTimeMin,
			behavior.scaleMin,
			behavior.velocityMin,
			behavior.rotationSpeedMin
		};
		behaviorData_->lifeScaleVelocityMaxRotationMax = {
			behavior.lifeTimeMax,
			behavior.scaleMax,
			behavior.velocityMax,
			behavior.rotationSpeedMax
		};
		behaviorData_->colorMin = behavior.colorMin;
		behaviorData_->colorMax = behavior.colorMax;
	}
}

bool GpuParticle::ApplyTexture(const std::string& textureFilePath) {
	if (textureFilePath.empty()) {
		return false;
	}

	if (!TextureManager::GetInstance()->LoadTexture(textureFilePath)) {
		return false;
	}

	textureFilePath_ = textureFilePath;
	config_.textureFilePath = textureFilePath;
	textureSrvIndex_ = TextureManager::GetInstance()->GetSrvIndex(textureFilePath_);
	return true;
}

void GpuParticle::CopyStringsToBuffers() {
	strncpy_s(
		configPathBuffer_,
		sizeof(configPathBuffer_),
		config_.filePath.c_str(),
		_TRUNCATE
	);
	strncpy_s(
		texturePathBuffer_,
		sizeof(texturePathBuffer_),
		config_.textureFilePath.c_str(),
		_TRUNCATE
	);
}

bool GpuParticle::SaveConfig(const std::string& filePath) const {
	if (filePath.empty()) {
		return false;
	}

	std::ofstream file(filePath);
	if (!file.is_open()) {
		return false;
	}

	if (IsCsvPath(filePath)) {
		const BehaviorSettings& behavior = config_.behavior;
		file << std::fixed << std::setprecision(6);
		file << "texture," << config_.textureFilePath << "\n";
		file << "blendMode," << ToString(config_.blendMode) << "\n";
		file << "autoEmit," << (config_.autoEmit ? 1 : 0) << "\n";
		file << "emitter.translate,"
			 << config_.emitter.translate.x << "," << config_.emitter.translate.y
			 << "," << config_.emitter.translate.z << "\n";
		file << "emitter.radius," << config_.emitter.radius << "\n";
		file << "emitter.count," << config_.emitter.count << "\n";
		file << "emitter.frequency," << config_.emitter.frequency << "\n";
		file << "behavior.lifeTime," << behavior.lifeTimeMin << ","
			 << behavior.lifeTimeMax << "\n";
		file << "behavior.scale," << behavior.scaleMin << ","
			 << behavior.scaleMax << "\n";
		file << "behavior.velocity," << behavior.velocityMin << ","
			 << behavior.velocityMax << "\n";
		file << "behavior.rotationSpeed," << behavior.rotationSpeedMin << ","
			 << behavior.rotationSpeedMax << "\n";
		file << "behavior.colorMin," << behavior.colorMin.x << ","
			 << behavior.colorMin.y << "," << behavior.colorMin.z << ","
			 << behavior.colorMin.w << "\n";
		file << "behavior.colorMax," << behavior.colorMax.x << ","
			 << behavior.colorMax.y << "," << behavior.colorMax.z << ","
			 << behavior.colorMax.w << "\n";
		return true;
	}

	json root;
	root["texture"] = config_.textureFilePath;
	root["blendMode"] = ToString(config_.blendMode);
	root["autoEmit"] = config_.autoEmit;
	root["emitter"] = {
		{ "translate", ToJson(config_.emitter.translate) },
		{ "radius", config_.emitter.radius },
		{ "count", config_.emitter.count },
		{ "frequency", config_.emitter.frequency }
	};

	const BehaviorSettings& behavior = config_.behavior;
	root["behavior"] = {
		{ "lifeTime", json::array({ behavior.lifeTimeMin, behavior.lifeTimeMax }) },
		{ "scale", json::array({ behavior.scaleMin, behavior.scaleMax }) },
		{ "velocity", json::array({ behavior.velocityMin, behavior.velocityMax }) },
		{
			"rotationSpeed",
			json::array({ behavior.rotationSpeedMin, behavior.rotationSpeedMax })
		},
		{ "colorMin", ToJson(behavior.colorMin) },
		{ "colorMax", ToJson(behavior.colorMax) }
	};

	file << std::setw(4) << root;
	return true;
}

bool GpuParticle::LoadConfig(const std::string& filePath) {
	if (filePath.empty()) {
		return false;
	}

	std::ifstream file(filePath);
	if (!file.is_open()) {
		return false;
	}

	Config loaded = config_;
	loaded.filePath = filePath;

	try {
		if (IsCsvPath(filePath)) {
			std::unordered_map<std::string, std::vector<std::string>> table;
			std::string line;
			while (std::getline(file, line)) {
				std::vector<std::string> values = SplitCsvLine(line);
				if (!values.empty()) {
					table.emplace(values.front(), std::move(values));
				}
			}

			if (const auto it = table.find("texture"); it != table.end() &&
				it->second.size() > 1) {
				loaded.textureFilePath = it->second[1];
			}
			if (const auto it = table.find("blendMode"); it != table.end() &&
				it->second.size() > 1) {
				loaded.blendMode = ToBlendMode(it->second[1]);
			}
			loaded.autoEmit = ReadCsvUint(table, "autoEmit", loaded.autoEmit ? 1u : 0u) != 0;
			loaded.emitter.translate =
				ReadCsvVector3(table, "emitter.translate", loaded.emitter.translate);
			loaded.emitter.radius =
				ReadCsvFloat(table, "emitter.radius", 0, loaded.emitter.radius);
			loaded.emitter.count =
				ReadCsvUint(table, "emitter.count", loaded.emitter.count);
			loaded.emitter.frequency =
				ReadCsvFloat(table, "emitter.frequency", 0, loaded.emitter.frequency);
			loaded.behavior.lifeTimeMin =
				ReadCsvFloat(table, "behavior.lifeTime", 0, loaded.behavior.lifeTimeMin);
			loaded.behavior.lifeTimeMax =
				ReadCsvFloat(table, "behavior.lifeTime", 1, loaded.behavior.lifeTimeMax);
			loaded.behavior.scaleMin =
				ReadCsvFloat(table, "behavior.scale", 0, loaded.behavior.scaleMin);
			loaded.behavior.scaleMax =
				ReadCsvFloat(table, "behavior.scale", 1, loaded.behavior.scaleMax);
			loaded.behavior.velocityMin =
				ReadCsvFloat(table, "behavior.velocity", 0, loaded.behavior.velocityMin);
			loaded.behavior.velocityMax =
				ReadCsvFloat(table, "behavior.velocity", 1, loaded.behavior.velocityMax);
			loaded.behavior.rotationSpeedMin = ReadCsvFloat(
				table,
				"behavior.rotationSpeed",
				0,
				loaded.behavior.rotationSpeedMin
			);
			loaded.behavior.rotationSpeedMax = ReadCsvFloat(
				table,
				"behavior.rotationSpeed",
				1,
				loaded.behavior.rotationSpeedMax
			);
			loaded.behavior.colorMin =
				ReadCsvVector4(table, "behavior.colorMin", loaded.behavior.colorMin);
			loaded.behavior.colorMax =
				ReadCsvVector4(table, "behavior.colorMax", loaded.behavior.colorMax);
		}
		else {
			json root;
			file >> root;

			loaded.textureFilePath = root.value("texture", loaded.textureFilePath);
			loaded.blendMode =
				ToBlendMode(root.value("blendMode", std::string(ToString(loaded.blendMode))));
			loaded.autoEmit = root.value("autoEmit", loaded.autoEmit);

			if (root.contains("emitter")) {
				const json& emitter = root.at("emitter");
				if (emitter.contains("translate")) {
					loaded.emitter.translate =
						ReadVector3(emitter.at("translate"), loaded.emitter.translate);
				}
				loaded.emitter.radius = emitter.value("radius", loaded.emitter.radius);
				loaded.emitter.count = emitter.value("count", loaded.emitter.count);
				loaded.emitter.frequency =
					emitter.value("frequency", loaded.emitter.frequency);
			}

			if (root.contains("behavior")) {
				const json& behavior = root.at("behavior");
				if (behavior.contains("lifeTime") &&
					behavior.at("lifeTime").is_array() &&
					behavior.at("lifeTime").size() >= 2) {
					loaded.behavior.lifeTimeMin =
						behavior.at("lifeTime").at(0).get<float>();
					loaded.behavior.lifeTimeMax =
						behavior.at("lifeTime").at(1).get<float>();
				}
				if (behavior.contains("scale") && behavior.at("scale").is_array() &&
					behavior.at("scale").size() >= 2) {
					loaded.behavior.scaleMin = behavior.at("scale").at(0).get<float>();
					loaded.behavior.scaleMax = behavior.at("scale").at(1).get<float>();
				}
				if (behavior.contains("velocity") &&
					behavior.at("velocity").is_array() &&
					behavior.at("velocity").size() >= 2) {
					loaded.behavior.velocityMin =
						behavior.at("velocity").at(0).get<float>();
					loaded.behavior.velocityMax =
						behavior.at("velocity").at(1).get<float>();
				}
				if (behavior.contains("rotationSpeed") &&
					behavior.at("rotationSpeed").is_array() &&
					behavior.at("rotationSpeed").size() >= 2) {
					loaded.behavior.rotationSpeedMin =
						behavior.at("rotationSpeed").at(0).get<float>();
					loaded.behavior.rotationSpeedMax =
						behavior.at("rotationSpeed").at(1).get<float>();
				}
				if (behavior.contains("colorMin")) {
					loaded.behavior.colorMin =
						ReadVector4(behavior.at("colorMin"), loaded.behavior.colorMin);
				}
				if (behavior.contains("colorMax")) {
					loaded.behavior.colorMax =
						ReadVector4(behavior.at("colorMax"), loaded.behavior.colorMax);
				}
			}
		}
	}
	catch (...) {
		return false;
	}

	loaded.emitter.count = std::clamp(loaded.emitter.count, 0u, kMaxParticles);
	loaded.emitter.radius = (std::max)(0.0f, loaded.emitter.radius);
	loaded.emitter.frequency = NormalizeEmitterFrequency(loaded.emitter.frequency);
	loaded.behavior.lifeTimeMax =
		(std::max)(loaded.behavior.lifeTimeMin, loaded.behavior.lifeTimeMax);
	loaded.behavior.scaleMax =
		(std::max)(loaded.behavior.scaleMin, loaded.behavior.scaleMax);
	loaded.behavior.velocityMax =
		(std::max)(loaded.behavior.velocityMin, loaded.behavior.velocityMax);
	loaded.behavior.rotationSpeedMax = (std::max)(
		loaded.behavior.rotationSpeedMin,
		loaded.behavior.rotationSpeedMax
	);

	config_ = loaded;
	ApplyTexture(config_.textureFilePath);
	ApplyConfigToGpu();
	CopyStringsToBuffers();
	return true;
}

void GpuParticle::DrawImGui(const char* windowTitle) {
#ifdef _DEBUG
	if (!ImGui::Begin(windowTitle)) {
		ImGui::End();
		return;
	}

	bool dirty = false;
	ImGui::InputText("Config Path", configPathBuffer_, sizeof(configPathBuffer_));
	if (ImGui::Button("Save")) {
		config_.filePath = configPathBuffer_;
		SaveConfig(config_.filePath);
	}
	ImGui::SameLine();
	if (ImGui::Button("Load")) {
		LoadConfig(configPathBuffer_);
	}
	ImGui::SameLine();
	if (ImGui::Button("Reset GPU Buffer")) {
		needsInitialize_ = true;
	}

	if (ImGui::InputText("Texture", texturePathBuffer_, sizeof(texturePathBuffer_))) {
		config_.textureFilePath = texturePathBuffer_;
	}
	ImGui::SameLine();
	if (ImGui::Button("Apply Texture")) {
		ApplyTexture(texturePathBuffer_);
		CopyStringsToBuffers();
	}

	dirty |= ImGui::Checkbox("Auto Emit", &config_.autoEmit);
	const char* blendModeItems[] = {
		"None",
		"Normal",
		"Add",
		"Subtract",
		"Multiply",
		"Screen"
	};
	int blendMode = static_cast<int>(config_.blendMode);
	if (ImGui::Combo(
			"BlendMode",
			&blendMode,
			blendModeItems,
			IM_ARRAYSIZE(blendModeItems)
		)) {
		config_.blendMode = static_cast<ParticleCommon::BlendMode>(blendMode);
		dirty = true;
	}

	if (ImGui::CollapsingHeader("Emitter", ImGuiTreeNodeFlags_DefaultOpen)) {
		dirty |= ImGui::DragFloat3(
			"Translate",
			&config_.emitter.translate.x,
			0.01f
		);
		dirty |= ImGui::DragFloat(
			"Radius",
			&config_.emitter.radius,
			0.01f,
			0.0f,
			100.0f
		);
		int count = static_cast<int>(config_.emitter.count);
		if (ImGui::DragInt(
				"Count",
				&count,
				1.0f,
				0,
				static_cast<int>(kMaxParticles)
			)) {
			config_.emitter.count =
				static_cast<uint32_t>(std::clamp(count, 0, static_cast<int>(kMaxParticles)));
			dirty = true;
		}
		dirty |= ImGui::DragFloat(
			"Frequency",
			&config_.emitter.frequency,
			0.01f,
			kMinEmitterFrequency,
			60.0f
		);
		if (ImGui::Button("Emit Once")) {
			if (emitterData_) {
				emitterData_->emit = 1;
			}
		}
	}

	if (ImGui::CollapsingHeader("Behavior", ImGuiTreeNodeFlags_DefaultOpen)) {
		BehaviorSettings& behavior = config_.behavior;
		float lifeTime[2] = { behavior.lifeTimeMin, behavior.lifeTimeMax };
		if (ImGui::DragFloat2("LifeTime", lifeTime, 0.01f, 0.01f, 60.0f)) {
			behavior.lifeTimeMin = lifeTime[0];
			behavior.lifeTimeMax = (std::max)(lifeTime[0], lifeTime[1]);
			dirty = true;
		}

		float scale[2] = { behavior.scaleMin, behavior.scaleMax };
		if (ImGui::DragFloat2("Scale", scale, 0.001f, 0.0f, 100.0f)) {
			behavior.scaleMin = scale[0];
			behavior.scaleMax = (std::max)(scale[0], scale[1]);
			dirty = true;
		}

		float velocity[2] = { behavior.velocityMin, behavior.velocityMax };
		if (ImGui::DragFloat2("Velocity", velocity, 0.01f, -100.0f, 100.0f)) {
			behavior.velocityMin = velocity[0];
			behavior.velocityMax = (std::max)(velocity[0], velocity[1]);
			dirty = true;
		}

		float rotationSpeed[2] = {
			behavior.rotationSpeedMin,
			behavior.rotationSpeedMax
		};
		if (ImGui::DragFloat2(
				"Rotation Speed",
				rotationSpeed,
				0.01f,
				-100.0f,
				100.0f
			)) {
			behavior.rotationSpeedMin = rotationSpeed[0];
			behavior.rotationSpeedMax = (std::max)(rotationSpeed[0], rotationSpeed[1]);
			dirty = true;
		}

		dirty |= ImGui::ColorEdit4("Color Min", &behavior.colorMin.x);
		dirty |= ImGui::ColorEdit4("Color Max", &behavior.colorMax.x);
	}

	if (dirty) {
		config_.emitter.radius = (std::max)(0.0f, config_.emitter.radius);
		config_.emitter.frequency = NormalizeEmitterFrequency(config_.emitter.frequency);
		ApplyConfigToGpu();
	}

	ImGui::End();
#else
	(void)windowTitle;
#endif
}

void GpuParticle::CreateRootSignatures() {
	HRESULT hr = S_OK;

	{
		D3D12_DESCRIPTOR_RANGE ranges[2]{};
		ranges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
		ranges[0].NumDescriptors = 1;
		ranges[0].BaseShaderRegister = 0;
		ranges[0].OffsetInDescriptorsFromTableStart =
			D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

		ranges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
		ranges[1].NumDescriptors = 1;
		ranges[1].BaseShaderRegister = 0;
		ranges[1].OffsetInDescriptorsFromTableStart =
			D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

		D3D12_ROOT_PARAMETER rootParameters[6]{};
		rootParameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
		rootParameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
		rootParameters[0].Descriptor.ShaderRegister = 0;

		rootParameters[1].ParameterType =
			D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
		rootParameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
		rootParameters[1].DescriptorTable.pDescriptorRanges = &ranges[0];
		rootParameters[1].DescriptorTable.NumDescriptorRanges = 1;

		rootParameters[2].ParameterType =
			D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
		rootParameters[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
		rootParameters[2].DescriptorTable.pDescriptorRanges = &ranges[1];
		rootParameters[2].DescriptorTable.NumDescriptorRanges = 1;

		rootParameters[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
		rootParameters[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
		rootParameters[3].Descriptor.ShaderRegister = 1;

		rootParameters[4].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
		rootParameters[4].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
		rootParameters[4].Descriptor.ShaderRegister = 0;

		D3D12_ROOT_SIGNATURE_DESC rootSignatureDesc{};
		rootSignatureDesc.Flags =
			D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
		rootSignatureDesc.pParameters = rootParameters;
		rootSignatureDesc.NumParameters = _countof(rootParameters);

		const D3D12_STATIC_SAMPLER_DESC sampler = MakeLinearSampler();
		rootSignatureDesc.pStaticSamplers = &sampler;
		rootSignatureDesc.NumStaticSamplers = 1;

		ID3DBlob* signatureBlob = nullptr;
		ID3DBlob* errorBlob = nullptr;
		hr = D3D12SerializeRootSignature(
			&rootSignatureDesc,
			D3D_ROOT_SIGNATURE_VERSION_1,
			&signatureBlob,
			&errorBlob
		);
		if (FAILED(hr)) {
			if (errorBlob) {
				Logger::Log(reinterpret_cast<char*>(errorBlob->GetBufferPointer()));
			}
			assert(false);
		}

		hr = dxCommon_->GetDevice()->CreateRootSignature(
			0,
			signatureBlob->GetBufferPointer(),
			signatureBlob->GetBufferSize(),
			IID_PPV_ARGS(&graphicsRootSignature_)
		);
		assert(SUCCEEDED(hr));
	}

	{
		D3D12_DESCRIPTOR_RANGE uavRanges[3]{};
		for (uint32_t index = 0; index < _countof(uavRanges); ++index) {
			uavRanges[index].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
			uavRanges[index].NumDescriptors = 1;
			uavRanges[index].BaseShaderRegister = index;
			uavRanges[index].OffsetInDescriptorsFromTableStart =
				D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
		}

		D3D12_ROOT_PARAMETER rootParameters[3]{};
		for (uint32_t index = 0; index < _countof(rootParameters); ++index) {
			rootParameters[index].ParameterType =
				D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
			rootParameters[index].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
			rootParameters[index].DescriptorTable.pDescriptorRanges =
				&uavRanges[index];
			rootParameters[index].DescriptorTable.NumDescriptorRanges = 1;
		}

		D3D12_ROOT_SIGNATURE_DESC rootSignatureDesc{};
		rootSignatureDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;
		rootSignatureDesc.pParameters = rootParameters;
		rootSignatureDesc.NumParameters = _countof(rootParameters);

		ID3DBlob* signatureBlob = nullptr;
		ID3DBlob* errorBlob = nullptr;
		hr = D3D12SerializeRootSignature(
			&rootSignatureDesc,
			D3D_ROOT_SIGNATURE_VERSION_1,
			&signatureBlob,
			&errorBlob
		);
		if (FAILED(hr)) {
			if (errorBlob) {
				Logger::Log(reinterpret_cast<char*>(errorBlob->GetBufferPointer()));
			}
			assert(false);
		}

		hr = dxCommon_->GetDevice()->CreateRootSignature(
			0,
			signatureBlob->GetBufferPointer(),
			signatureBlob->GetBufferSize(),
			IID_PPV_ARGS(&initializeRootSignature_)
		);
		assert(SUCCEEDED(hr));
	}

	{
		D3D12_DESCRIPTOR_RANGE uavRanges[3]{};
		for (uint32_t index = 0; index < _countof(uavRanges); ++index) {
			uavRanges[index].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
			uavRanges[index].NumDescriptors = 1;
			uavRanges[index].BaseShaderRegister = index;
			uavRanges[index].OffsetInDescriptorsFromTableStart =
				D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
		}

		D3D12_ROOT_PARAMETER rootParameters[6]{};
		rootParameters[0].ParameterType =
			D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
		rootParameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
		rootParameters[0].DescriptorTable.pDescriptorRanges = &uavRanges[0];
		rootParameters[0].DescriptorTable.NumDescriptorRanges = 1;

		rootParameters[1].ParameterType =
			D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
		rootParameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
		rootParameters[1].DescriptorTable.pDescriptorRanges = &uavRanges[1];
		rootParameters[1].DescriptorTable.NumDescriptorRanges = 1;

		rootParameters[2].ParameterType =
			D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
		rootParameters[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
		rootParameters[2].DescriptorTable.pDescriptorRanges = &uavRanges[2];
		rootParameters[2].DescriptorTable.NumDescriptorRanges = 1;

		rootParameters[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
		rootParameters[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
		rootParameters[3].Descriptor.ShaderRegister = 0;

		rootParameters[4].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
		rootParameters[4].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
		rootParameters[4].Descriptor.ShaderRegister = 1;

		rootParameters[5].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
		rootParameters[5].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
		rootParameters[5].Descriptor.ShaderRegister = 2;

		D3D12_ROOT_SIGNATURE_DESC rootSignatureDesc{};
		rootSignatureDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;
		rootSignatureDesc.pParameters = rootParameters;
		rootSignatureDesc.NumParameters = _countof(rootParameters);

		ID3DBlob* signatureBlob = nullptr;
		ID3DBlob* errorBlob = nullptr;
		hr = D3D12SerializeRootSignature(
			&rootSignatureDesc,
			D3D_ROOT_SIGNATURE_VERSION_1,
			&signatureBlob,
			&errorBlob
		);
		if (FAILED(hr)) {
			if (errorBlob) {
				Logger::Log(reinterpret_cast<char*>(errorBlob->GetBufferPointer()));
			}
			assert(false);
		}

		hr = dxCommon_->GetDevice()->CreateRootSignature(
			0,
			signatureBlob->GetBufferPointer(),
			signatureBlob->GetBufferSize(),
			IID_PPV_ARGS(&emitRootSignature_)
		);
		assert(SUCCEEDED(hr));
	}
}

void GpuParticle::CreatePipelineStates() {
	HRESULT hr = S_OK;

	const auto vertexShaderBlob = dxCommon_->CompileShader(
		L"resources/shaders/GpuParticle.VS.hlsl",
		L"vs_6_0"
	);
	assert(vertexShaderBlob);
	const auto pixelShaderBlob = dxCommon_->CompileShader(
		L"resources/shaders/Particle.PS.hlsl",
		L"ps_6_0"
	);
	assert(pixelShaderBlob);

	D3D12_INPUT_ELEMENT_DESC inputElementDescs[4]{};
	inputElementDescs[0].SemanticName = "POSITION";
	inputElementDescs[0].Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
	inputElementDescs[0].AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;
	inputElementDescs[1].SemanticName = "TEXCOORD";
	inputElementDescs[1].Format = DXGI_FORMAT_R32G32_FLOAT;
	inputElementDescs[1].AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;
	inputElementDescs[2].SemanticName = "NORMAL";
	inputElementDescs[2].Format = DXGI_FORMAT_R32G32B32_FLOAT;
	inputElementDescs[2].AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;
	inputElementDescs[3].SemanticName = "COLOR";
	inputElementDescs[3].Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
	inputElementDescs[3].AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;

	D3D12_INPUT_LAYOUT_DESC inputLayoutDesc{};
	inputLayoutDesc.pInputElementDescs = inputElementDescs;
	inputLayoutDesc.NumElements = _countof(inputElementDescs);

	D3D12_RASTERIZER_DESC rasterizerDesc{};
	rasterizerDesc.CullMode = D3D12_CULL_MODE_NONE;
	rasterizerDesc.FillMode = D3D12_FILL_MODE_SOLID;
	rasterizerDesc.DepthClipEnable = TRUE;

	D3D12_DEPTH_STENCIL_DESC depthStencilDesc{};
	depthStencilDesc.DepthEnable = TRUE;
	depthStencilDesc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
	depthStencilDesc.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;

	D3D12_GRAPHICS_PIPELINE_STATE_DESC graphicsPipelineDesc{};
	graphicsPipelineDesc.pRootSignature = graphicsRootSignature_.Get();
	graphicsPipelineDesc.InputLayout = inputLayoutDesc;
	graphicsPipelineDesc.VS = {
		vertexShaderBlob->GetBufferPointer(),
		vertexShaderBlob->GetBufferSize()
	};
	graphicsPipelineDesc.PS = {
		pixelShaderBlob->GetBufferPointer(),
		pixelShaderBlob->GetBufferSize()
	};
	graphicsPipelineDesc.RasterizerState = rasterizerDesc;
	graphicsPipelineDesc.DepthStencilState = depthStencilDesc;
	graphicsPipelineDesc.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
	graphicsPipelineDesc.NumRenderTargets = 1;
	graphicsPipelineDesc.RTVFormats[0] = RenderFormats::kSceneHdrFormat;
	graphicsPipelineDesc.PrimitiveTopologyType =
		D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	graphicsPipelineDesc.SampleDesc.Count = 1;
	graphicsPipelineDesc.SampleMask = D3D12_DEFAULT_SAMPLE_MASK;

	for (uint32_t blendIndex = 0;
		blendIndex <
			static_cast<uint32_t>(ParticleCommon::BlendMode::kCountOfBlendMode);
		++blendIndex) {
		D3D12_BLEND_DESC blendDesc{};
		ApplyBlendMode(
			blendDesc,
			static_cast<ParticleCommon::BlendMode>(blendIndex)
		);
		graphicsPipelineDesc.BlendState = blendDesc;
		hr = dxCommon_->GetDevice()->CreateGraphicsPipelineState(
			&graphicsPipelineDesc,
			IID_PPV_ARGS(&graphicsPipelineStates_[blendIndex])
		);
		assert(SUCCEEDED(hr));
	}

	const auto initializeShaderBlob = dxCommon_->CompileShader(
		L"resources/shaders/InitializeGpuParticle.CS.hlsl",
		L"cs_6_0"
	);
	assert(initializeShaderBlob);

	D3D12_COMPUTE_PIPELINE_STATE_DESC computePipelineDesc{};
	computePipelineDesc.pRootSignature = initializeRootSignature_.Get();
	computePipelineDesc.CS = {
		initializeShaderBlob->GetBufferPointer(),
		initializeShaderBlob->GetBufferSize()
	};

	hr = dxCommon_->GetDevice()->CreateComputePipelineState(
		&computePipelineDesc,
		IID_PPV_ARGS(&initializePipelineState_)
	);
	assert(SUCCEEDED(hr));

	const auto emitShaderBlob = dxCommon_->CompileShader(
		L"resources/shaders/EmitGpuParticle.CS.hlsl",
		L"cs_6_0"
	);
	assert(emitShaderBlob);

	D3D12_COMPUTE_PIPELINE_STATE_DESC emitPipelineDesc{};
	emitPipelineDesc.pRootSignature = emitRootSignature_.Get();
	emitPipelineDesc.CS = {
		emitShaderBlob->GetBufferPointer(),
		emitShaderBlob->GetBufferSize()
	};

	hr = dxCommon_->GetDevice()->CreateComputePipelineState(
		&emitPipelineDesc,
		IID_PPV_ARGS(&emitPipelineState_)
	);
	assert(SUCCEEDED(hr));

	const auto updateShaderBlob = dxCommon_->CompileShader(
		L"resources/shaders/UpdateGpuParticle.CS.hlsl",
		L"cs_6_0"
	);
	assert(updateShaderBlob);

	D3D12_COMPUTE_PIPELINE_STATE_DESC updatePipelineDesc{};
	updatePipelineDesc.pRootSignature = emitRootSignature_.Get();
	updatePipelineDesc.CS = {
		updateShaderBlob->GetBufferPointer(),
		updateShaderBlob->GetBufferSize()
	};

	hr = dxCommon_->GetDevice()->CreateComputePipelineState(
		&updatePipelineDesc,
		IID_PPV_ARGS(&updatePipelineState_)
	);
	assert(SUCCEEDED(hr));
}

void GpuParticle::TransitionParticleResource(D3D12_RESOURCE_STATES stateAfter) {
	if (!particleResource_ || particleResourceState_ == stateAfter) {
		return;
	}

	D3D12_RESOURCE_BARRIER barrier{};
	barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
	barrier.Transition.pResource = particleResource_.Get();
	barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	barrier.Transition.StateBefore = particleResourceState_;
	barrier.Transition.StateAfter = stateAfter;
	dxCommon_->GetCommandList()->ResourceBarrier(1, &barrier);
	particleResourceState_ = stateAfter;
}

void GpuParticle::TransitionFreeListIndexResource(
	D3D12_RESOURCE_STATES stateAfter
) {
	if (!freeListIndexResource_ || freeListIndexResourceState_ == stateAfter) {
		return;
	}

	D3D12_RESOURCE_BARRIER barrier{};
	barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
	barrier.Transition.pResource = freeListIndexResource_.Get();
	barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	barrier.Transition.StateBefore = freeListIndexResourceState_;
	barrier.Transition.StateAfter = stateAfter;
	dxCommon_->GetCommandList()->ResourceBarrier(1, &barrier);
	freeListIndexResourceState_ = stateAfter;
}

void GpuParticle::TransitionFreeListResource(D3D12_RESOURCE_STATES stateAfter) {
	if (!freeListResource_ || freeListResourceState_ == stateAfter) {
		return;
	}

	D3D12_RESOURCE_BARRIER barrier{};
	barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
	barrier.Transition.pResource = freeListResource_.Get();
	barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	barrier.Transition.StateBefore = freeListResourceState_;
	barrier.Transition.StateAfter = stateAfter;
	dxCommon_->GetCommandList()->ResourceBarrier(1, &barrier);
	freeListResourceState_ = stateAfter;
}

void GpuParticle::InitializeParticlesOnGPU() {
	if (!needsInitialize_) {
		return;
	}

	auto* commandList = dxCommon_->GetCommandList();
	TransitionParticleResource(D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	TransitionFreeListIndexResource(D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	TransitionFreeListResource(D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

	commandList->SetComputeRootSignature(initializeRootSignature_.Get());
	commandList->SetPipelineState(initializePipelineState_.Get());
	srvManager_->SetComputeRootDescriptorTable(0, particleUavIndex_);
	srvManager_->SetComputeRootDescriptorTable(1, freeListIndexUavIndex_);
	srvManager_->SetComputeRootDescriptorTable(2, freeListUavIndex_);
	commandList->Dispatch(1, 1, 1);

	D3D12_RESOURCE_BARRIER uavBarriers[3]{};
	uavBarriers[0].Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
	uavBarriers[0].UAV.pResource = particleResource_.Get();
	uavBarriers[1].Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
	uavBarriers[1].UAV.pResource = freeListIndexResource_.Get();
	uavBarriers[2].Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
	uavBarriers[2].UAV.pResource = freeListResource_.Get();
	commandList->ResourceBarrier(_countof(uavBarriers), uavBarriers);

	TransitionParticleResource(D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	needsInitialize_ = false;
}

void GpuParticle::EmitParticlesOnGPU() {
	if (!emitterData_ || emitterData_->emit == 0) {
		return;
	}

	auto* commandList = dxCommon_->GetCommandList();
	TransitionParticleResource(D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	TransitionFreeListIndexResource(D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	TransitionFreeListResource(D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

	commandList->SetComputeRootSignature(emitRootSignature_.Get());
	commandList->SetPipelineState(emitPipelineState_.Get());
	srvManager_->SetComputeRootDescriptorTable(0, particleUavIndex_);
	srvManager_->SetComputeRootDescriptorTable(1, freeListIndexUavIndex_);
	srvManager_->SetComputeRootDescriptorTable(2, freeListUavIndex_);
	commandList->SetComputeRootConstantBufferView(
		3,
		emitterResource_->GetGPUVirtualAddress()
	);
	commandList->SetComputeRootConstantBufferView(
		4,
		perFrameResource_->GetGPUVirtualAddress()
	);
	commandList->SetComputeRootConstantBufferView(
		5,
		behaviorResource_->GetGPUVirtualAddress()
	);
	commandList->Dispatch(1, 1, 1);

	D3D12_RESOURCE_BARRIER uavBarriers[3]{};
	uavBarriers[0].Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
	uavBarriers[0].UAV.pResource = particleResource_.Get();
	uavBarriers[1].Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
	uavBarriers[1].UAV.pResource = freeListIndexResource_.Get();
	uavBarriers[2].Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
	uavBarriers[2].UAV.pResource = freeListResource_.Get();
	commandList->ResourceBarrier(_countof(uavBarriers), uavBarriers);

	emitterData_->emit = 0;
}

void GpuParticle::UpdateParticlesOnGPU() {
	auto* commandList = dxCommon_->GetCommandList();
	TransitionParticleResource(D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	TransitionFreeListIndexResource(D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	TransitionFreeListResource(D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

	commandList->SetComputeRootSignature(emitRootSignature_.Get());
	commandList->SetPipelineState(updatePipelineState_.Get());
	srvManager_->SetComputeRootDescriptorTable(0, particleUavIndex_);
	srvManager_->SetComputeRootDescriptorTable(1, freeListIndexUavIndex_);
	srvManager_->SetComputeRootDescriptorTable(2, freeListUavIndex_);
	commandList->SetComputeRootConstantBufferView(
		3,
		emitterResource_->GetGPUVirtualAddress()
	);
	commandList->SetComputeRootConstantBufferView(
		4,
		perFrameResource_->GetGPUVirtualAddress()
	);
	commandList->SetComputeRootConstantBufferView(
		5,
		behaviorResource_->GetGPUVirtualAddress()
	);
	commandList->Dispatch(1, 1, 1);

	D3D12_RESOURCE_BARRIER uavBarriers[3]{};
	uavBarriers[0].Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
	uavBarriers[0].UAV.pResource = particleResource_.Get();
	uavBarriers[1].Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
	uavBarriers[1].UAV.pResource = freeListIndexResource_.Get();
	uavBarriers[2].Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
	uavBarriers[2].UAV.pResource = freeListResource_.Get();
	commandList->ResourceBarrier(_countof(uavBarriers), uavBarriers);

	TransitionParticleResource(D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
}

void GpuParticle::Update() {
	if (!emitterData_ || !perFrameData_) {
		return;
	}

	elapsedTime_ += deltaTime_;
	perFrameData_->time = elapsedTime_;
	perFrameData_->deltaTime = deltaTime_;

	if (!config_.autoEmit) {
		return;
	}

	emitterData_->frequency = NormalizeEmitterFrequency(emitterData_->frequency);
	emitterData_->frequencyTime += deltaTime_;
	if (emitterData_->frequency <= emitterData_->frequencyTime) {
		emitterData_->frequencyTime -= emitterData_->frequency;
		emitterData_->emit = 1;
	}
	else {
		emitterData_->emit = 0;
	}
}

void GpuParticle::Draw(Camera* camera) {
	if (!camera || !particleResource_) {
		return;
	}

	InitializeParticlesOnGPU();
	EmitParticlesOnGPU();
	UpdateParticlesOnGPU();
	TransitionParticleResource(D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

	perViewData_->viewProjection = camera->GetViewProjectionMatrix();
	perViewData_->billboardMatrix = camera->GetWorldMatrix();
	perViewData_->billboardMatrix.m[3][0] = 0.0f;
	perViewData_->billboardMatrix.m[3][1] = 0.0f;
	perViewData_->billboardMatrix.m[3][2] = 0.0f;

	auto* commandList = dxCommon_->GetCommandList();
	commandList->SetGraphicsRootSignature(graphicsRootSignature_.Get());
	const uint32_t blendIndex = std::clamp(
		static_cast<uint32_t>(config_.blendMode),
		0u,
		static_cast<uint32_t>(ParticleCommon::BlendMode::kCountOfBlendMode) - 1u
	);
	commandList->SetPipelineState(graphicsPipelineStates_[blendIndex].Get());
	commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	const D3D12_VERTEX_BUFFER_VIEW& vbv =
		particleCommon_->GetVertexBufferView();
	commandList->IASetVertexBuffers(0, 1, &vbv);

	commandList->SetGraphicsRootConstantBufferView(
		0,
		materialResource_->GetGPUVirtualAddress()
	);
	srvManager_->SetGraphicsRootDescriptorTable(1, particleSrvIndex_);
	srvManager_->SetGraphicsRootDescriptorTable(2, textureSrvIndex_);
	commandList->SetGraphicsRootConstantBufferView(
		3,
		directionalLightResource_->GetGPUVirtualAddress()
	);
	commandList->SetGraphicsRootConstantBufferView(
		4,
		perViewResource_->GetGPUVirtualAddress()
	);

	commandList->DrawInstanced(6, kMaxParticles, 0, 0);
}
