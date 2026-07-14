// 役割: シードから再現可能な星空頂点データを生成する。
#include "StarFieldGenerator.h"

#include <DirectXTex.h>
#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <random>
#include <system_error>

#include "../utility/EditableResourcePath.h"
#include "../utility/StringUtility.h"
#include "imgui.h"

namespace {

struct Float3 {
	float x;
	float y;
	float z;
};

struct FaceBasis {
	Float3 forward;
	Float3 right;
	Float3 down;
};

constexpr std::array<FaceBasis, 6> kFaces = {
	FaceBasis{{ 1, 0, 0 }, { 0, 0,-1 }, { 0,-1, 0 }},
	FaceBasis{{-1, 0, 0 }, { 0, 0, 1 }, { 0,-1, 0 }},
	FaceBasis{{ 0, 1, 0 }, { 1, 0, 0 }, { 0, 0, 1 }},
	FaceBasis{{ 0,-1, 0 }, { 1, 0, 0 }, { 0, 0,-1 }},
	FaceBasis{{ 0, 0, 1 }, { 1, 0, 0 }, { 0,-1, 0 }},
	FaceBasis{{ 0, 0,-1 }, {-1, 0, 0 }, { 0,-1, 0 }}
};

Float3 Add(Float3 a, Float3 b) { return { a.x + b.x, a.y + b.y, a.z + b.z }; }
Float3 Mul(Float3 value, float scale) { return { value.x * scale, value.y * scale, value.z * scale }; }
float Dot(Float3 a, Float3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
Float3 Normalize(Float3 value) {
	const float length = std::sqrt((std::max)(Dot(value, value), 1.0e-8f));
	return Mul(value, 1.0f / length);
}

uint32_t Hash(uint32_t value) {
	value ^= value >> 16;
	value *= 0x7feb352du;
	value ^= value >> 15;
	value *= 0x846ca68bu;
	return value ^ (value >> 16);
}

float DirectionNoise(Float3 direction, float scale, uint32_t seed) {
	const int32_t x = static_cast<int32_t>(std::floor(direction.x * scale));
	const int32_t y = static_cast<int32_t>(std::floor(direction.y * scale));
	const int32_t z = static_cast<int32_t>(std::floor(direction.z * scale));
	uint32_t value = Hash(static_cast<uint32_t>(x) ^ seed);
	value = Hash(value ^ (static_cast<uint32_t>(y) * 0x9e3779b9u));
	value = Hash(value ^ (static_cast<uint32_t>(z) * 0x85ebca6bu));
	return static_cast<float>(value & 0x00ffffffu) / 16777215.0f;
}

uint32_t CalculateMipCount(uint32_t size) {
	uint32_t count = 1;
	while (size > 1) {
		size = (std::max)(size / 2, 1u);
		++count;
	}
	return count;
}

float Luminance(Float3 color) {
	return color.x * 0.2126f + color.y * 0.7152f + color.z * 0.0722f;
}

void BuildStarPreservingMipChain(
	DirectX::ScratchImage& image,
	std::atomic<float>* progress
) {
	const DirectX::TexMetadata metadata = image.GetMetadata();
	size_t completedRows = 0;
	size_t totalRows = 0;
	for (size_t mip = 1; mip < metadata.mipLevels; ++mip) {
		totalRows += (std::max)(metadata.height >> mip, size_t{ 1 }) * metadata.arraySize;
	}
	for (size_t face = 0; face < metadata.arraySize; ++face) {
		for (size_t mip = 1; mip < metadata.mipLevels; ++mip) {
			const DirectX::Image* source = image.GetImage(mip - 1, face, 0);
			const DirectX::Image* destination = image.GetImage(mip, face, 0);
			if (!source || !destination) continue;

			for (size_t y = 0; y < destination->height; ++y) {
				float* destinationRow = reinterpret_cast<float*>(
					destination->pixels + destination->rowPitch * y
				);
				for (size_t x = 0; x < destination->width; ++x) {
					Float3 average{};
					Float3 brightest{};
					float brightestLuminance = -1.0f;
					uint32_t sampleCount = 0;

					for (size_t offsetY = 0; offsetY < 2; ++offsetY) {
						const size_t sourceY = (std::min)(y * 2 + offsetY, source->height - 1);
						const float* sourceRow = reinterpret_cast<const float*>(
							source->pixels + source->rowPitch * sourceY
						);
						for (size_t offsetX = 0; offsetX < 2; ++offsetX) {
							const size_t sourceX = (std::min)(x * 2 + offsetX, source->width - 1);
							const float* pixel = sourceRow + sourceX * 4;
							const Float3 color = { pixel[0], pixel[1], pixel[2] };
							average = Add(average, color);
							const float luminance = Luminance(color);
							if (luminance > brightestLuminance) {
								brightestLuminance = luminance;
								brightest = color;
							}
							++sampleCount;
						}
					}

					average = Mul(average, 1.0f / static_cast<float>(sampleCount));
					const float peak = (std::max)(brightestLuminance - Luminance(average), 0.0f);
					const float peakWeight = mip <= 4
						? std::clamp(peak / 0.5f, 0.0f, 1.0f) * 0.7f
						: 0.0f;
					const Float3 result = Add(
						Mul(average, 1.0f - peakWeight),
						Mul(brightest, peakWeight)
					);

					float* output = destinationRow + x * 4;
					output[0] = result.x;
					output[1] = result.y;
					output[2] = result.z;
					output[3] = 1.0f;
				}
				++completedRows;
				if (progress && (completedRows & 31u) == 0u) {
					progress->store(0.10f + 0.08f * static_cast<float>(completedRows) /
						static_cast<float>((std::max)(totalRows, size_t{ 1 })), std::memory_order_relaxed);
				}
			}
		}
	}
}

Float3 FaceDirection(size_t face, float u, float v) {
	const FaceBasis& basis = kFaces[face];
	return Normalize(Add(basis.forward, Add(Mul(basis.right, u), Mul(basis.down, v))));
}

void AddColor(float* pixel, Float3 color, float brightness) {
	pixel[0] += color.x * brightness;
	pixel[1] += color.y * brightness;
	pixel[2] += color.z * brightness;
	pixel[3] = 1.0f;
}

} // namespace

bool StarFieldGenerator::GenerateAndSave(const std::string& requestedPath) {
	return GenerateAndSave(requestedPath, nullptr);
}

bool StarFieldGenerator::GenerateAndSave(
	const std::string& requestedPath,
	GenerationProgress* progress
) {
	const auto setProgress = [progress](float value, uint32_t stage) {
		if (!progress) return;
		progress->stage.store(stage, std::memory_order_relaxed);
		progress->value.store(value, std::memory_order_relaxed);
	};
	setProgress(0.0f, 0);
	const uint32_t resolution = std::clamp(settings_.resolution, 128u, 2048u);
	DirectX::ScratchImage image;
	HRESULT hr = image.InitializeCube(
		DXGI_FORMAT_R32G32B32A32_FLOAT,
		resolution,
		resolution,
		1,
		CalculateMipCount(resolution)
	);
	if (FAILED(hr)) {
		status_ = "Failed to allocate cubemap.";
		return false;
	}

	Float3 milkyNormal = Normalize({
		settings_.milkyWayNormal.x,
		settings_.milkyWayNormal.y,
		settings_.milkyWayNormal.z
	});
	for (size_t face = 0; face < kFaces.size(); ++face) {
		const DirectX::Image* faceImage = image.GetImage(0, face, 0);
		for (uint32_t y = 0; y < resolution; ++y) {
			float* row = reinterpret_cast<float*>(faceImage->pixels + faceImage->rowPitch * y);
			for (uint32_t x = 0; x < resolution; ++x) {
				const Float3 direction = FaceDirection(
					face,
					(2.0f * (static_cast<float>(x) + 0.5f) / resolution) - 1.0f,
					(2.0f * (static_cast<float>(y) + 0.5f) / resolution) - 1.0f
				);
				float* pixel = row + x * 4;
				pixel[0] = settings_.backgroundColor.x;
				pixel[1] = settings_.backgroundColor.y;
				pixel[2] = settings_.backgroundColor.z;
				pixel[3] = 1.0f;

				if (settings_.milkyWayEnabled) {
					const float distanceToBand = std::abs(Dot(direction, milkyNormal));
					const float width = (std::max)(settings_.milkyWayWidth, 0.001f);
					const float band = std::exp(-(distanceToBand * distanceToBand) / (width * width));
					const float noise = 0.2f + 0.8f * DirectionNoise(
						direction,
						(std::max)(settings_.milkyWayNoiseScale, 0.1f),
						settings_.seed
					);
					AddColor(pixel, { 0.48f, 0.58f, 0.85f }, band * noise * settings_.milkyWayBrightness);
				}
			}
			if (progress && (y & 31u) == 0u) {
				const float completed = static_cast<float>(face * resolution + y + 1u);
				const float total = static_cast<float>(kFaces.size() * resolution);
				progress->value.store(0.08f * completed / total, std::memory_order_relaxed);
			}
		}
	}

	setProgress(0.08f, 1);
	std::mt19937 random(settings_.seed);
	std::uniform_real_distribution<float> unit(0.0f, 1.0f);
	std::uniform_real_distribution<float> signedUnit(-1.0f, 1.0f);
	const float resolutionScale = static_cast<float>(resolution) / 1024.0f;
	for (uint32_t starIndex = 0; starIndex < settings_.starCount; ++starIndex) {
		const float z = signedUnit(random);
		const float angle = unit(random) * 6.28318530718f;
		const float radial = std::sqrt((std::max)(0.0f, 1.0f - z * z));
		const Float3 direction = { radial * std::cos(angle), z, radial * std::sin(angle) };
		const float sizeAtReferenceResolution = settings_.starSizeMin +
			(settings_.starSizeMax - settings_.starSizeMin) * unit(random);
		const float size = sizeAtReferenceResolution * resolutionScale;
		const float brightness = settings_.starBrightnessMin +
			(settings_.starBrightnessMax - settings_.starBrightnessMin) * std::pow(unit(random), 3.0f);
		const Float3 color = unit(random) < 0.18f
			? Float3{ 0.58f, 0.72f, 1.0f }
			: (unit(random) < 0.12f ? Float3{ 1.0f, 0.68f, 0.42f } : Float3{ 1.0f, 1.0f, 1.0f });

		for (size_t face = 0; face < kFaces.size(); ++face) {
			const FaceBasis& basis = kFaces[face];
			const float depth = Dot(direction, basis.forward);
			if (depth <= 0.0f) continue;
			const float u = Dot(direction, basis.right) / depth;
			const float v = Dot(direction, basis.down) / depth;
			const float margin = (size + 1.0f) * 2.0f / resolution;
			if (std::abs(u) > 1.0f + margin || std::abs(v) > 1.0f + margin) continue;

			const float centerX = (u + 1.0f) * 0.5f * static_cast<float>(resolution);
			const float centerY = (v + 1.0f) * 0.5f * static_cast<float>(resolution);
			const float radius = (std::max)(size, 0.35f);
			const int32_t pixelRadius = (std::max)(1, static_cast<int32_t>(std::ceil(radius + 0.5f)));
			const DirectX::Image* faceImage = image.GetImage(0, face, 0);
			const int32_t baseX = static_cast<int32_t>(std::floor(centerX));
			const int32_t baseY = static_cast<int32_t>(std::floor(centerY));
			for (int32_t offsetY = -pixelRadius; offsetY <= pixelRadius; ++offsetY) {
				const int32_t py = baseY + offsetY;
				if (py < 0 || py >= static_cast<int32_t>(resolution)) continue;
				float* row = reinterpret_cast<float*>(faceImage->pixels + faceImage->rowPitch * py);
				for (int32_t offsetX = -pixelRadius; offsetX <= pixelRadius; ++offsetX) {
					const int32_t px = baseX + offsetX;
					if (px < 0 || px >= static_cast<int32_t>(resolution)) continue;
					const float dx = (static_cast<float>(px) + 0.5f) - centerX;
					const float dy = (static_cast<float>(py) + 0.5f) - centerY;
					const float distanceSquared = dx * dx + dy * dy;
					const float distance = std::sqrt(distanceSquared);
					const float coverage = std::clamp(radius + 0.5f - distance, 0.0f, 1.0f);
					if (coverage <= 0.0f) continue;
					const float sigma = (std::max)(radius * 0.55f, 0.35f);
					const float intensity = coverage * std::exp(-distanceSquared / (2.0f * sigma * sigma));
					AddColor(row + px * 4, color, intensity * brightness);
				}
			}
		}
		if (progress && (starIndex & 63u) == 0u) {
			progress->value.store(0.08f + 0.02f * static_cast<float>(starIndex + 1u) /
				static_cast<float>((std::max)(settings_.starCount, 1u)), std::memory_order_relaxed);
		}
	}

	setProgress(0.10f, 2);
	BuildStarPreservingMipChain(image, progress ? &progress->value : nullptr);

	setProgress(0.18f, 3);
	DirectX::ScratchImage compressed;
	DirectX::TexMetadata compressedMetadata = image.GetMetadata();
	compressedMetadata.format = DXGI_FORMAT_BC6H_UF16;
	hr = compressed.Initialize(compressedMetadata);
	for (size_t imageIndex = 0; SUCCEEDED(hr) && imageIndex < image.GetImageCount(); ++imageIndex) {
		DirectX::ScratchImage compressedImage;
		hr = DirectX::Compress(
			image.GetImages()[imageIndex], DXGI_FORMAT_BC6H_UF16,
			DirectX::TEX_COMPRESS_PARALLEL, DirectX::TEX_THRESHOLD_DEFAULT,
			compressedImage
		);
		if (FAILED(hr)) break;

		const DirectX::Image* source = compressedImage.GetImage(0, 0, 0);
		const DirectX::Image* destination = compressed.GetImages() + imageIndex;
		if (!source || !destination || source->slicePitch != destination->slicePitch) {
			hr = E_FAIL;
			break;
		}
		std::memcpy(destination->pixels, source->pixels, source->slicePitch);
		if (progress) {
			progress->value.store(0.18f + 0.78f * static_cast<float>(imageIndex + 1u) /
				static_cast<float>(image.GetImageCount()), std::memory_order_relaxed);
		}
	}
	if (FAILED(hr)) {
		status_ = "Failed to compress cubemap to BC6H HDR format.";
		return false;
	}

	setProgress(0.96f, 4);
	const std::filesystem::path target = EditableResourcePath::Resolve(requestedPath);
	std::error_code error;
	std::filesystem::create_directories(target.parent_path(), error);
	if (error) {
		status_ = "Failed to create output directory.";
		return false;
	}
	std::filesystem::path temporary = target;
	temporary += ".tmp";
	hr = DirectX::SaveToDDSFile(
		compressed.GetImages(), compressed.GetImageCount(), compressed.GetMetadata(),
		DirectX::DDS_FLAGS_NONE, temporary.c_str()
	);
	if (SUCCEEDED(hr)) {
		DirectX::ScratchImage validationImage;
		DirectX::TexMetadata validationMetadata{};
		hr = DirectX::LoadFromDDSFile(
			temporary.c_str(), DirectX::DDS_FLAGS_NONE,
			&validationMetadata, validationImage
		);
		const bool validCubemap = SUCCEEDED(hr) &&
			validationMetadata.IsCubemap() &&
			validationMetadata.arraySize == 6 &&
			validationMetadata.format == DXGI_FORMAT_BC6H_UF16 &&
			validationImage.GetImageCount() >= 6;
		if (!validCubemap) {
			std::filesystem::remove(temporary, error);
			status_ = "Generated DDS failed cubemap validation.";
			return false;
		}
	}
	if (SUCCEEDED(hr) && std::filesystem::exists(target, error)) {
		CopyFileW(target.c_str(), EditableResourcePath::BackupPath(target).c_str(), FALSE);
	}
	if (FAILED(hr) || !MoveFileExW(
		temporary.c_str(), target.c_str(),
		MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH
	)) {
		std::filesystem::remove(temporary, error);
		status_ = "Failed to save DDS.";
		return false;
	}

	status_ = "Saved BC6H: " + StringUtility::ToUtf8(
		EditableResourcePath::ToProjectRelative(target)
	);
	setProgress(1.0f, 5);
	return true;
}

void StarFieldGenerator::RefreshSkyboxFiles() {
	const std::string previousSelection =
		selectedSkyboxIndex_ >= 0 && selectedSkyboxIndex_ < static_cast<int>(skyboxFiles_.size())
		? skyboxFiles_[selectedSkyboxIndex_]
		: std::string{};

	skyboxFiles_.clear();
	selectedSkyboxIndex_ = -1;
	const std::filesystem::path directory =
		EditableResourcePath::Resolve("resources");
	std::error_code error;
	for (const auto& entry : std::filesystem::recursive_directory_iterator(directory, error)) {
		if (error || !entry.is_regular_file()) continue;
		std::string extension = entry.path().extension().string();
		std::transform(extension.begin(), extension.end(), extension.begin(),
			[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		if (extension != ".dds") continue;
		skyboxFiles_.push_back(
			StringUtility::ToUtf8(
				EditableResourcePath::ToProjectRelative(entry.path())
			)
		);
	}
	std::sort(skyboxFiles_.begin(), skyboxFiles_.end());

	for (int index = 0; index < static_cast<int>(skyboxFiles_.size()); ++index) {
		if (skyboxFiles_[index] == previousSelection ||
			skyboxFiles_[index] == std::string(outputPath_)) {
			selectedSkyboxIndex_ = index;
			break;
		}
	}
	if (selectedSkyboxIndex_ < 0 && !skyboxFiles_.empty()) {
		selectedSkyboxIndex_ = 0;
	}
}

std::optional<std::string> StarFieldGenerator::DrawImGui(const char* windowTitle) {
	std::optional<std::string> appliedPath;
	if (generationInProgress_ && generationFuture_.valid() &&
		generationFuture_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
		GenerationResult result = generationFuture_.get();
		generationInProgress_ = false;
		generationProgress_.reset();
		status_ = std::move(result.status);
		if (result.succeeded) {
			appliedPath = StringUtility::ToUtf8(
				EditableResourcePath::Resolve(
					StringUtility::ToPath(result.requestedPath)
				)
			);
			skyboxFiles_.clear();
			selectedSkyboxIndex_ = -1;
			RefreshSkyboxFiles();
		}
	}

	if (!ImGui::Begin(windowTitle)) {
		ImGui::End();
		return appliedPath;
	}

	if (ImGui::BeginTabBar("EnvironmentTabs")) {
		if (ImGui::BeginTabItem("Stars")) {
			if (ImGui::Button("Reset Defaults")) settings_ = Settings{};
			ImGui::Separator();
			ImGui::InputScalar("Seed", ImGuiDataType_U32, &settings_.seed);
			if (ImGui::Button("Randomize")) settings_.seed = std::random_device{}();
			ImGui::SliderInt("Face Resolution", reinterpret_cast<int*>(&settings_.resolution), 256, 2048);
			ImGui::SliderInt("Star Count", reinterpret_cast<int*>(&settings_.starCount), 0, 20000);
			ImGui::DragFloatRange2("Star Radius at 1024", &settings_.starSizeMin, &settings_.starSizeMax, 0.05f, 0.4f, 6.0f);
			ImGui::DragFloatRange2("HDR Brightness", &settings_.starBrightnessMin, &settings_.starBrightnessMax, 0.1f, 0.0f, 32.0f);
			ImGui::ColorEdit3("Background", &settings_.backgroundColor.x, ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR);
			const float averageRadiusAt1024 =
				(settings_.starSizeMin + settings_.starSizeMax) * 0.5f;
			const float averageRadius = averageRadiusAt1024 *
				(static_cast<float>(settings_.resolution) / 1024.0f);
			const float estimatedCoverage =
				static_cast<float>(settings_.starCount) * 3.14159265f * averageRadius * averageRadius /
				(6.0f * static_cast<float>(settings_.resolution) * static_cast<float>(settings_.resolution));
			ImGui::Text("Estimated coverage: %.2f%%", estimatedCoverage * 100.0f);
			if (estimatedCoverage > 0.08f) {
				ImGui::TextWrapped("High coverage: stars may merge into a bright pattern.");
			}
			ImGui::EndTabItem();
		}
		if (ImGui::BeginTabItem("Milky Way")) {
			ImGui::Checkbox("Enabled", &settings_.milkyWayEnabled);
			ImGui::DragFloat3("Band Normal", &settings_.milkyWayNormal.x, 0.01f, -1.0f, 1.0f);
			ImGui::SliderFloat("Width", &settings_.milkyWayWidth, 0.01f, 0.8f);
			ImGui::SliderFloat("Brightness", &settings_.milkyWayBrightness, 0.0f, 4.0f);
			ImGui::SliderFloat("Noise Scale", &settings_.milkyWayNoiseScale, 0.1f, 32.0f);
			ImGui::EndTabItem();
		}
		if (ImGui::BeginTabItem("Output")) {
			if (skyboxFiles_.empty()) RefreshSkyboxFiles();
			const char* selectedName = selectedSkyboxIndex_ >= 0
				? skyboxFiles_[selectedSkyboxIndex_].c_str()
				: "No DDS files";
			if (ImGui::BeginCombo("Skybox DDS", selectedName)) {
				for (int index = 0; index < static_cast<int>(skyboxFiles_.size()); ++index) {
					const bool selected = index == selectedSkyboxIndex_;
					if (ImGui::Selectable(skyboxFiles_[index].c_str(), selected)) {
						selectedSkyboxIndex_ = index;
					}
					if (selected) ImGui::SetItemDefaultFocus();
				}
				ImGui::EndCombo();
			}
			if (ImGui::Button("Apply Skybox") && selectedSkyboxIndex_ >= 0) {
				const std::string selectedPath = skyboxFiles_[selectedSkyboxIndex_];
				appliedPath = StringUtility::ToUtf8(
					EditableResourcePath::Resolve(
						StringUtility::ToPath(selectedPath)
					)
				);
				status_ = "Applied: " + selectedPath;
			}
			ImGui::SameLine();
			if (ImGui::Button("Refresh List")) RefreshSkyboxFiles();
			ImGui::Separator();
			ImGui::InputText("DDS Path", outputPath_, sizeof(outputPath_));
			const float resolutionScale = static_cast<float>(settings_.resolution) / 1024.0f;
			ImGui::Text("Typical generation time: %.0f-%.0f sec",
				30.0f * resolutionScale * resolutionScale,
				120.0f * resolutionScale * resolutionScale);
			ImGui::BeginDisabled(generationInProgress_);
			if (ImGui::Button("Generate && Save DDS")) {
				const Settings settingsSnapshot = settings_;
				const std::string requestedPath = outputPath_;
				generationInProgress_ = true;
				generationStartTime_ = ImGui::GetTime();
				generationProgress_ = std::make_shared<GenerationProgress>();
				const std::shared_ptr<GenerationProgress> progress = generationProgress_;
				status_ = "Generating DDS in background...";
				generationFuture_ = std::async(
					std::launch::async,
					[settingsSnapshot, requestedPath, progress]() {
						StarFieldGenerator worker;
						worker.settings_ = settingsSnapshot;
						const bool succeeded = worker.GenerateAndSave(requestedPath, progress.get());
						return GenerationResult{
							succeeded,
							std::move(worker.status_),
							requestedPath
						};
					}
				);
			}
			ImGui::EndDisabled();
			if (generationInProgress_) {
				const float progress = generationProgress_
					? generationProgress_->value.load(std::memory_order_relaxed)
					: 0.0f;
				const uint32_t stage = generationProgress_
					? generationProgress_->stage.load(std::memory_order_relaxed)
					: 0u;
				static constexpr const char* stageNames[] = {
					"Background", "Stars", "Mipmaps", "BC6H compression", "Saving", "Complete"
				};
				ImGui::ProgressBar(progress, ImVec2(-1.0f, 0.0f));
				ImGui::Text("Stage: %s", stageNames[(std::min)(stage, 5u)]);
				const double elapsed = ImGui::GetTime() - generationStartTime_;
				if (progress > 0.01f) {
					const double remaining = elapsed * (1.0 - progress) / progress;
					ImGui::Text("Elapsed %.1fs / estimated remaining %.1fs", elapsed, remaining);
				} else {
					ImGui::Text("Elapsed %.1fs", elapsed);
				}
			}
			if (!status_.empty()) ImGui::TextWrapped("%s", status_.c_str());
			ImGui::Separator();
			ImGui::TextWrapped("Shooting stars are dynamic effects and are not baked into this DDS.");
			ImGui::EndTabItem();
		}
		ImGui::EndTabBar();
	}
	ImGui::End();
	return appliedPath;
}
