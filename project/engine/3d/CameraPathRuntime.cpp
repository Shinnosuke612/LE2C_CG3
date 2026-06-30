#include "CameraPathRuntime.h"

#include "Camera.h"
#include "../math/Math.h"
#include "../math/Matrix4x4.h"
#include "../scene/SceneDocument.h"

#include <algorithm>
#include <cmath>
#include <unordered_set>

namespace {
	constexpr float kTwoPi = 6.28318530717958647692f;
	constexpr float kPi = 3.14159265358979323846f;

	const SceneComponent* FindEnabledComponent(
		const SceneEntity& entity,
		const char* type
	) {
		const auto found = std::find_if(
			entity.components.begin(),
			entity.components.end(),
			[type](const SceneComponent& component) {
				return component.enabled && component.type == type;
			}
		);
		return found == entity.components.end() ? nullptr : &(*found);
	}

	Matrix4x4 CalculateWorldMatrix(
		const SceneDocument& document,
		const SceneEntity& entity,
		std::unordered_set<uint64_t>& visited
	) {
		const Matrix4x4 local = MakeAffineMatrix(
			entity.transform.scale,
			entity.transform.rotate,
			entity.transform.translate
		);
		if (entity.parentId == 0 || !visited.insert(entity.id).second) {
			return local;
		}
		const SceneEntity* parent = document.FindEntity(entity.parentId);
		if (!parent) {
			return local;
		}
		return Multiply(local, CalculateWorldMatrix(document, *parent, visited));
	}

	Transform ResolveWorldTransform(
		const SceneDocument& document,
		const SceneEntity& entity
	) {
		std::unordered_set<uint64_t> visited;
		const Matrix4x4 world = CalculateWorldMatrix(document, entity, visited);
		Transform result = entity.transform;
		Vector3 scale{};
		Vector3 rotate{};
		Vector3 translate{};
		if (DecomposeAffineMatrix(world, scale, rotate, translate)) {
			result.scale = scale;
			result.rotate = rotate;
			result.translate = translate;
		}
		return result;
	}

	float ApplyEasing(float t, const std::string& easing) {
		t = std::clamp(t, 0.0f, 1.0f);
		if (easing == "EaseIn") {
			return t * t;
		}
		if (easing == "EaseOut") {
			const float inverse = 1.0f - t;
			return 1.0f - inverse * inverse;
		}
		if (easing == "EaseInOut") {
			return t < 0.5f
				? 2.0f * t * t
				: 1.0f - std::pow(-2.0f * t + 2.0f, 2.0f) * 0.5f;
		}
		if (easing == "SmoothStep") {
			return t * t * (3.0f - 2.0f * t);
		}
		return t;
	}

	float Lerp(float a, float b, float t) {
		return a + (b - a) * t;
	}

	float NormalizeAngle(float angle) {
		while (angle > kPi) {
			angle -= kTwoPi;
		}
		while (angle < -kPi) {
			angle += kTwoPi;
		}
		return angle;
	}

	float LerpAngle(float a, float b, float t) {
		return a + NormalizeAngle(b - a) * t;
	}

	Transform LerpTransform(
		const Transform& a,
		const Transform& b,
		float t
	) {
		Transform result = a;
		result.translate = {
			Lerp(a.translate.x, b.translate.x, t),
			Lerp(a.translate.y, b.translate.y, t),
			Lerp(a.translate.z, b.translate.z, t)
		};
		result.rotate = {
			LerpAngle(a.rotate.x, b.rotate.x, t),
			LerpAngle(a.rotate.y, b.rotate.y, t),
			LerpAngle(a.rotate.z, b.rotate.z, t)
		};
		result.scale = {
			Lerp(a.scale.x, b.scale.x, t),
			Lerp(a.scale.y, b.scale.y, t),
			Lerp(a.scale.z, b.scale.z, t)
		};
		return result;
	}

	Vector3 CatmullRom(
		const Vector3& p0,
		const Vector3& p1,
		const Vector3& p2,
		const Vector3& p3,
		float t
	) {
		const float t2 = t * t;
		const float t3 = t2 * t;
		return {
			0.5f * (
				2.0f * p1.x +
				(-p0.x + p2.x) * t +
				(2.0f * p0.x - 5.0f * p1.x + 4.0f * p2.x - p3.x) * t2 +
				(-p0.x + 3.0f * p1.x - 3.0f * p2.x + p3.x) * t3
			),
			0.5f * (
				2.0f * p1.y +
				(-p0.y + p2.y) * t +
				(2.0f * p0.y - 5.0f * p1.y + 4.0f * p2.y - p3.y) * t2 +
				(-p0.y + 3.0f * p1.y - 3.0f * p2.y + p3.y) * t3
			),
			0.5f * (
				2.0f * p1.z +
				(-p0.z + p2.z) * t +
				(2.0f * p0.z - 5.0f * p1.z + 4.0f * p2.z - p3.z) * t2 +
				(-p0.z + 3.0f * p1.z - 3.0f * p2.z + p3.z) * t3
			)
		};
	}
}

void CameraPathRuntime::Play(
	const SceneDocument& document,
	const SceneEntity& pathEntity,
	const SceneComponent& pathComponent,
	const Camera& currentCamera
) {
	points_.clear();
	for (const SceneEntity& entity : document.GetEntities()) {
		if (entity.parentId != pathEntity.id) {
			continue;
		}
		const SceneComponent* pointComponent =
			FindEnabledComponent(entity, "CameraPathPoint");
		if (!pointComponent) {
			continue;
		}
		PointSample sample{};
		sample.transform = ResolveWorldTransform(document, entity);
		sample.durationToNext =
			(std::max)(pointComponent->cameraPathPointDurationToNext, 0.0f);
		sample.easingToNext =
			pointComponent->cameraPathPointEasingToNext.empty()
			? pathComponent.cameraPathDefaultEasing
			: pointComponent->cameraPathPointEasingToNext;
		sample.entityId = entity.id;
		points_.push_back(sample);
	}

	if (points_.empty()) {
		Stop();
		return;
	}

	startTransform_.scale = { 1.0f, 1.0f, 1.0f };
	startTransform_.translate = currentCamera.GetTranslate();
	startTransform_.rotate = currentCamera.GetRotate();
	returnTransform_ = startTransform_;
	if (!pathComponent.cameraPathStartFromCurrentCamera) {
		startTransform_ = points_.front().transform;
	}
	enterDuration_ = pathComponent.cameraPathStartFromCurrentCamera
		? (std::max)(pathComponent.cameraPathEnterDuration, 0.0f)
		: 0.0f;
	exitDuration_ = (std::max)(pathComponent.cameraPathExitDuration, 0.0f);
	enterEasing_ = pathComponent.cameraPathDefaultEasing;
	exitEasing_ = pathComponent.cameraPathDefaultEasing;
	returnToPreviousCamera_ = pathComponent.cameraPathReturnToPreviousCamera;
	interpolation_ = pathComponent.cameraPathInterpolation.empty()
		? "Linear"
		: pathComponent.cameraPathInterpolation;
	elapsed_ = 0.0f;
	pathElapsed_ = 0.0f;
	hasCurrentTransform_ = false;
	phase_ = enterDuration_ > 0.0f ? Phase::Enter : Phase::Path;
}

void CameraPathRuntime::Stop() {
	phase_ = Phase::Finished;
	elapsed_ = 0.0f;
	pathElapsed_ = 0.0f;
	points_.clear();
	hasCurrentTransform_ = false;
}

void CameraPathRuntime::Update(float deltaTime, Camera& camera) {
	if (phase_ == Phase::Finished) {
		return;
	}

	deltaTime = (std::max)(deltaTime, 0.0f);
	elapsed_ += deltaTime;

	Transform evaluated{};
	if (phase_ == Phase::Enter) {
		const float t = enterDuration_ <= 0.0f
			? 1.0f
			: elapsed_ / enterDuration_;
		evaluated = LerpTransform(
			startTransform_,
			points_.front().transform,
			ApplyEasing(t, enterEasing_)
		);
		if (t >= 1.0f) {
			phase_ = Phase::Path;
			elapsed_ = 0.0f;
			pathElapsed_ = 0.0f;
		}
	} else if (phase_ == Phase::Path) {
		pathElapsed_ += deltaTime;
		const float duration = GetPathDuration();
		evaluated = EvaluatePath(pathElapsed_);
		if (duration <= 0.0f || pathElapsed_ >= duration) {
			phase_ =
				returnToPreviousCamera_ && exitDuration_ > 0.0f
				? Phase::Exit
				: Phase::Finished;
			elapsed_ = 0.0f;
			if (phase_ == Phase::Finished) {
				evaluated = returnToPreviousCamera_
					? returnTransform_
					: points_.back().transform;
			}
		}
	} else if (phase_ == Phase::Exit) {
		const float t = exitDuration_ <= 0.0f
			? 1.0f
			: elapsed_ / exitDuration_;
		evaluated = LerpTransform(
			points_.back().transform,
			returnTransform_,
			ApplyEasing(t, exitEasing_)
		);
		if (t >= 1.0f) {
			phase_ = Phase::Finished;
			evaluated = returnTransform_;
		}
	}

	currentTransform_ = evaluated;
	hasCurrentTransform_ = true;
	ApplyTransform(camera, evaluated);
}

void CameraPathRuntime::ApplyTransform(
	Camera& camera,
	const Transform& transform
) {
	camera.SetOrbitMode(false);
	camera.SetTranslate(transform.translate);
	camera.SetRotate(transform.rotate);
	camera.UpdatePreviewMatrices();
}

Transform CameraPathRuntime::EvaluatePath(float elapsed) const {
	if (points_.empty()) {
		return {};
	}
	if (points_.size() == 1) {
		return points_.front().transform;
	}

	float cursor = 0.0f;
	for (size_t index = 0; index + 1 < points_.size(); ++index) {
		const float duration = (std::max)(points_[index].durationToNext, 0.0f);
		if (duration <= 0.0f) {
			continue;
		}
		if (elapsed <= cursor + duration) {
			const float localT = (elapsed - cursor) / duration;
			Transform result = LerpTransform(
				points_[index].transform,
				points_[index + 1].transform,
				ApplyEasing(localT, points_[index].easingToNext)
			);
			if (interpolation_ == "CatmullRom") {
				const float easedT = ApplyEasing(
					localT,
					points_[index].easingToNext
				);
				const size_t p0Index = index == 0 ? index : index - 1;
				const size_t p1Index = index;
				const size_t p2Index = index + 1;
				const size_t p3Index = (std::min)(
					index + 2,
					points_.size() - 1
				);
				result.translate = CatmullRom(
					points_[p0Index].transform.translate,
					points_[p1Index].transform.translate,
					points_[p2Index].transform.translate,
					points_[p3Index].transform.translate,
					easedT
				);
			}
			return result;
		}
		cursor += duration;
	}
	return points_.back().transform;
}

float CameraPathRuntime::GetPathDuration() const {
	if (points_.size() < 2) {
		return 0.0f;
	}
	float duration = 0.0f;
	for (size_t index = 0; index + 1 < points_.size(); ++index) {
		duration += (std::max)(points_[index].durationToNext, 0.0f);
	}
	return duration;
}
