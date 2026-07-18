// 役割: State遷移を遅延確定し、組み込みIdle/Move/MeleeAttack行動を提供する。
#include "SceneStateMachineSystem.h"

#include "ScenePrefabAnimationSystem.h"
#include "../../../engine/3d/Object3d.h"
#include "../../../engine/io/Input.h"
#include "../../../engine/math/Math.h"
#include "../../../engine/physics/PhysicsBody.h"
#include "../../../engine/scene/SceneDocument.h"
#include "../../../engine/scene/SceneEntityQuery.h"
#include "../../player/Player.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <unordered_set>
#include <utility>

namespace {
	const SceneStateParameter* FindParameter(
		const SceneStateDefinition& state,
		const std::string& name
	) {
		const auto found = std::find_if(
			state.parameters.begin(),
			state.parameters.end(),
			[&](const SceneStateParameter& parameter) {
				return parameter.name == name;
			}
		);
		return found == state.parameters.end() ? nullptr : &*found;
	}

	BYTE ResolveKey(const std::string& keyName) {
		std::string key = keyName;
		std::transform(
			key.begin(), key.end(), key.begin(),
			[](unsigned char character) {
				return static_cast<char>(std::toupper(character));
			}
		);
		if (key == "ENTER") return DIK_RETURN;
		if (key == "SPACE") return DIK_SPACE;
		if (key == "ESCAPE") return DIK_ESCAPE;
		if (key == "TAB") return DIK_TAB;
		if (key.size() == 1 && key[0] >= 'A' && key[0] <= 'Z') {
			static const BYTE letters[] = {
				DIK_A, DIK_B, DIK_C, DIK_D, DIK_E, DIK_F, DIK_G,
				DIK_H, DIK_I, DIK_J, DIK_K, DIK_L, DIK_M, DIK_N,
				DIK_O, DIK_P, DIK_Q, DIK_R, DIK_S, DIK_T, DIK_U,
				DIK_V, DIK_W, DIK_X, DIK_Y, DIK_Z
			};
			return letters[key[0] - 'A'];
		}
		return 0;
	}

	bool ResolveMouseButton(
		const std::string& inputName,
		Input::MouseButton& button
	) {
		std::string normalized;
		normalized.reserve(inputName.size());
		for (const unsigned char character : inputName) {
			if (std::isalnum(character)) {
				normalized.push_back(static_cast<char>(std::toupper(character)));
			}
		}
		if (
			normalized == "MOUSELEFT" ||
			normalized == "LEFTMOUSE" ||
			normalized == "LMB"
		) {
			button = Input::MouseButton::Left;
			return true;
		}
		if (
			normalized == "MOUSERIGHT" ||
			normalized == "RIGHTMOUSE" ||
			normalized == "RMB"
		) {
			button = Input::MouseButton::Right;
			return true;
		}
		if (
			normalized == "MOUSEMIDDLE" ||
			normalized == "MIDDLEMOUSE" ||
			normalized == "MMB"
		) {
			button = Input::MouseButton::Middle;
			return true;
		}
		return false;
	}

	std::string GetAttackInput(StateContext& context) {
		const std::string input = context.GetString("AttackInput");
		return input.empty()
			? context.GetString("AttackKey", "J")
			: input;
	}

	const SceneStateDefinition* FindState(
		const SceneComponent& machine,
		const std::string& stateName
	) {
		const auto found = std::find_if(
			machine.stateMachineStates.begin(),
			machine.stateMachineStates.end(),
			[&](const SceneStateDefinition& state) {
				return state.name == stateName;
			}
		);
		return found == machine.stateMachineStates.end() ? nullptr : &*found;
	}

	class BuiltinIdleState final : public IEntityStateAction {
	public:
		void Update(StateContext& context, float) override {
			context.StopHorizontalMovement();
			const std::string attackState = context.GetString("AttackState");
			if (
				!attackState.empty() &&
				context.IsInputTriggered(GetAttackInput(context))
			) {
				context.StopHorizontalMovement();
				context.RequestState(attackState);
				return;
			}
			if (context.HasMoveInput()) {
				context.RequestState(context.GetString("MoveState", "Move"));
			}
		}
	};

	class BuiltinMoveState final : public IEntityStateAction {
	public:
		void Update(StateContext& context, float) override {
			const std::string attackState = context.GetString("AttackState");
			if (
				!attackState.empty() &&
				context.IsInputTriggered(GetAttackInput(context))
			) {
				context.StopHorizontalMovement();
				context.RequestState(attackState);
				return;
			}
			Input* input = Input::GetInstance();
			Vector3 move{};
			if (input && input->PushKey(DIK_W)) move.z += 1.0f;
			if (input && input->PushKey(DIK_S)) move.z -= 1.0f;
			if (input && input->PushKey(DIK_A)) move.x -= 1.0f;
			if (input && input->PushKey(DIK_D)) move.x += 1.0f;
			if (Math::Length(move) <= 0.0001f) {
				context.StopHorizontalMovement();
				context.RequestState(context.GetString("IdleState", "Idle"));
				return;
			}
			// PlayerBehaviorはCamera相対移動を先に計算済みなので、その速度を維持する。
			if (!SceneEntityQuery::HasComponent(
				context.GetEntity(), "PlayerBehavior"
			)) {
				move = Math::Multiply(
					Math::Normalize(move),
					(std::max)(context.GetFloat("Speed", 6.0f), 0.0f)
				);
				context.SetHorizontalVelocity(move.x, move.z);
			}
		}
	};

	class BuiltinMeleeAttackState final : public IEntityStateAction {
	public:
		void Enter(StateContext& context) override {
			context.SetEntityActive(context.GetEntityParameter("HitBox"), false);
			const std::string animation = context.GetString("Animation");
			if (!animation.empty()) {
				SceneEntity* animationTarget =
					context.GetEntityParameter("AnimationTarget");
				if (animationTarget) {
					context.PlayPrefabAnimation(animationTarget, animation, true);
				} else {
					context.PlayAnimation(animation, true);
				}
			}
		}

		void Update(StateContext& context, float) override {
			context.StopHorizontalMovement();
			const float windup = (std::max)(context.GetFloat("Windup", 0.15f), 0.0f);
			const float activeTime = (std::max)(
				context.GetFloat("ActiveTime", 0.2f), 0.0f
			);
			const float recovery = (std::max)(
				context.GetFloat("Recovery", 0.35f), 0.0f
			);
			const float time = context.GetStateTime();
			context.SetEntityActive(
				context.GetEntityParameter("HitBox"),
				time >= windup && time < windup + activeTime
			);
			if (time >= windup + activeTime + recovery) {
				context.RequestState(context.GetString("ReturnState", "Idle"));
			}
		}

		void Exit(StateContext& context) override {
			context.SetEntityActive(context.GetEntityParameter("HitBox"), false);
		}
	};

	void RegisterBuiltinStates() {
		EntityStateRegistry& registry = EntityStateRegistry::GetInstance();
		if (!registry.Contains("Builtin.Idle")) {
			registry.Register<BuiltinIdleState>("Builtin.Idle");
		}
		if (!registry.Contains("Builtin.Move")) {
			registry.Register<BuiltinMoveState>("Builtin.Move");
		}
		if (!registry.Contains("Builtin.MeleeAttack")) {
			registry.Register<BuiltinMeleeAttackState>("Builtin.MeleeAttack");
		}
	}
}

StateContext::StateContext(
	SceneDocument& document,
	SceneEntity& entity,
	Object3d* object,
	PhysicsBody* body,
	ScenePrefabAnimationSystem& prefabAnimationSystem,
	const SceneStateDefinition& state,
	float stateTime,
	std::string& requestedState
) :
	document_(document),
	entity_(entity),
	object_(object),
	body_(body),
	prefabAnimationSystem_(prefabAnimationSystem),
	state_(state),
	stateTime_(stateTime),
	requestedState_(requestedState) {}

SceneEntity& StateContext::GetEntity() const { return entity_; }

float StateContext::GetFloat(const std::string& name, float fallback) const {
	const SceneStateParameter* parameter = FindParameter(state_, name);
	return parameter ? parameter->floatValue : fallback;
}

int StateContext::GetInt(const std::string& name, int fallback) const {
	const SceneStateParameter* parameter = FindParameter(state_, name);
	return parameter ? parameter->intValue : fallback;
}

bool StateContext::GetBool(const std::string& name, bool fallback) const {
	const SceneStateParameter* parameter = FindParameter(state_, name);
	return parameter ? parameter->boolValue : fallback;
}

std::string StateContext::GetString(
	const std::string& name,
	const std::string& fallback
) const {
	const SceneStateParameter* parameter = FindParameter(state_, name);
	return parameter ? parameter->stringValue : fallback;
}

SceneEntity* StateContext::GetEntityParameter(const std::string& name) const {
	const SceneStateParameter* parameter = FindParameter(state_, name);
	if (!parameter) {
		return nullptr;
	}
	if (parameter->entityId != 0) {
		if (SceneEntity* entity = document_.FindEntity(parameter->entityId)) {
			return entity;
		}
	}
	return parameter->entityName.empty()
		? nullptr
		: document_.FindEntityByName(parameter->entityName);
}

bool StateContext::IsKeyDown(const std::string& keyName) const {
	Input* input = Input::GetInstance();
	const BYTE key = ResolveKey(keyName);
	return input && key != 0 && input->PushKey(key);
}

bool StateContext::IsKeyTriggered(const std::string& keyName) const {
	Input* input = Input::GetInstance();
	const BYTE key = ResolveKey(keyName);
	return input && key != 0 && input->TriggerKey(key);
}

bool StateContext::IsInputDown(const std::string& inputName) const {
	Input* input = Input::GetInstance();
	if (!input) {
		return false;
	}
	Input::MouseButton mouseButton{};
	if (ResolveMouseButton(inputName, mouseButton)) {
		return input->PushMouse(mouseButton);
	}
	const BYTE key = ResolveKey(inputName);
	return key != 0 && input->PushKey(key);
}

bool StateContext::IsInputTriggered(const std::string& inputName) const {
	Input* input = Input::GetInstance();
	if (!input) {
		return false;
	}
	Input::MouseButton mouseButton{};
	if (ResolveMouseButton(inputName, mouseButton)) {
		return input->TriggerMouse(mouseButton);
	}
	const BYTE key = ResolveKey(inputName);
	return key != 0 && input->TriggerKey(key);
}

bool StateContext::HasMoveInput() const {
	Input* input = Input::GetInstance();
	return input && (
		input->PushKey(DIK_W) || input->PushKey(DIK_A) ||
		input->PushKey(DIK_S) || input->PushKey(DIK_D)
	);
}

void StateContext::SetHorizontalVelocity(float x, float z) {
	if (body_) {
		body_->velocity.x = x;
		body_->velocity.z = z;
	}
}

void StateContext::StopHorizontalMovement() {
	SetHorizontalVelocity(0.0f, 0.0f);
}

void StateContext::SetEntityActive(SceneEntity* entity, bool active) {
	if (entity) {
		entity->active = active;
	}
}

bool StateContext::PlayAnimation(const std::string& clipName, bool restart) {
	if (object_) {
		for (size_t index = 0; index < object_->GetAnimationClipCount(); ++index) {
			if (object_->GetAnimationClipName(index) == clipName) {
				return object_->PlayAnimation(index, 0.1f, restart);
			}
		}
	}
	return prefabAnimationSystem_.Play(
		document_, entity_.id, clipName, restart
	);
}

bool StateContext::PlayPrefabAnimation(
	SceneEntity* target,
	const std::string& clipName,
	bool restart
) {
	return target && prefabAnimationSystem_.Play(
		document_, target->id, clipName, restart
	);
}

void StateContext::RequestState(const std::string& stateName) {
	if (!stateName.empty()) {
		requestedState_ = stateName;
	}
}

EntityStateRegistry& EntityStateRegistry::GetInstance() {
	static EntityStateRegistry instance;
	return instance;
}

void EntityStateRegistry::Register(const std::string& actionId, Factory factory) {
	if (!actionId.empty() && factory) {
		factories_[actionId] = std::move(factory);
	}
}

bool EntityStateRegistry::Contains(const std::string& actionId) const {
	return factories_.contains(actionId);
}

std::unique_ptr<IEntityStateAction> EntityStateRegistry::Create(
	const std::string& actionId
) const {
	const auto found = factories_.find(actionId);
	return found == factories_.end() ? nullptr : found->second();
}

SceneStateMachineSystem::SceneStateMachineSystem() {
	RegisterBuiltinStates();
}

void SceneStateMachineSystem::Update(
	SceneDocument& document,
	const std::vector<SceneRuntimeObjectBinding>& bindings,
	Player* player,
	ScenePrefabAnimationSystem& prefabAnimationSystem,
	float deltaTime
) {
	auto findBinding = [&](uint64_t entityId) -> const SceneRuntimeObjectBinding* {
		const auto found = std::find_if(
			bindings.begin(), bindings.end(),
			[entityId](const SceneRuntimeObjectBinding& binding) {
				return binding.entity && binding.entity->id == entityId;
			}
		);
		return found == bindings.end() ? nullptr : &*found;
	};
	auto makeContext = [&] (
		SceneEntity& entity,
		const SceneStateDefinition& state,
		float stateTime,
		std::string& requestedState
	) {
		const SceneRuntimeObjectBinding* binding = findBinding(entity.id);
		Object3d* object = binding ? binding->object : nullptr;
		PhysicsBody* body = binding ? binding->body : nullptr;
		if (player && object && player->GetObject() == object) {
			body = &player->GetPhysicsBody();
		}
		return StateContext(
			document, entity, object, body, prefabAnimationSystem,
			state, stateTime, requestedState
		);
	};
	auto transition = [&] (
		SceneEntity& entity,
		const SceneComponent& machine,
		Runtime& runtime,
		const std::string& nextState
	) {
		const SceneStateDefinition* next = FindState(machine, nextState);
		if (!next || next->name == runtime.currentState) {
			return;
		}
		std::string ignoredRequest;
		if (runtime.action) {
			if (const SceneStateDefinition* current =
				FindState(machine, runtime.currentState)) {
				StateContext context = makeContext(
					entity, *current, runtime.stateTime, ignoredRequest
				);
				runtime.action->Exit(context);
			}
		}
		runtime.currentState = next->name;
		runtime.stateTime = 0.0f;
		runtime.action = EntityStateRegistry::GetInstance().Create(next->actionId);
		runtime.initialized = true;
		if (runtime.action) {
			StateContext context = makeContext(
				entity, *next, 0.0f, ignoredRequest
			);
			runtime.action->Enter(context);
		}
	};

	std::unordered_set<uint64_t> requiredEntities;
	for (SceneEntity& entity : document.GetEntities()) {
		const SceneComponent* machine =
			SceneEntityQuery::FindComponent(entity, "StateMachine");
		if (!machine) {
			continue;
		}
		requiredEntities.insert(entity.id);
		Runtime& runtime = runtimes_[entity.id];
		const bool active = machine->enabled &&
			SceneEntityQuery::IsEntityActiveInHierarchy(document, entity);
		if (!active) {
			if (machine->stateMachineResetOnDisable) {
				if (runtime.action) {
					if (const SceneStateDefinition* current =
						FindState(*machine, runtime.currentState)) {
						std::string ignoredRequest;
						StateContext context = makeContext(
							entity,
							*current,
							runtime.stateTime,
							ignoredRequest
						);
						runtime.action->Exit(context);
					}
				}
				runtime = {};
			}
			continue;
		}
		if (!runtime.initialized) {
			const std::string initial = machine->stateMachineInitialState.empty() &&
				!machine->stateMachineStates.empty()
				? machine->stateMachineStates.front().name
				: machine->stateMachineInitialState;
			transition(entity, *machine, runtime, initial);
		}
		if (const auto external = externalRequests_.find(entity.id);
			external != externalRequests_.end()) {
			transition(entity, *machine, runtime, external->second);
			externalRequests_.erase(external);
		}
		const SceneStateDefinition* current =
			FindState(*machine, runtime.currentState);
		if (!current) {
			continue;
		}
		std::string requestedState;
		if (runtime.action) {
			StateContext context = makeContext(
				entity, *current, runtime.stateTime, requestedState
			);
			runtime.action->Update(context, deltaTime);
		}
		runtime.stateTime += (std::max)(deltaTime, 0.0f);
		if (!requestedState.empty()) {
			transition(entity, *machine, runtime, requestedState);
		}
	}

	for (auto iterator = runtimes_.begin(); iterator != runtimes_.end();) {
		if (!requiredEntities.contains(iterator->first)) {
			iterator = runtimes_.erase(iterator);
		} else {
			++iterator;
		}
	}
}

bool SceneStateMachineSystem::RequestState(
	uint64_t entityId,
	const std::string& stateName
) {
	if (entityId == 0 || stateName.empty()) {
		return false;
	}
	externalRequests_[entityId] = stateName;
	return true;
}

const std::string* SceneStateMachineSystem::GetCurrentState(
	uint64_t entityId
) const {
	const auto found = runtimes_.find(entityId);
	return found == runtimes_.end() || !found->second.initialized
		? nullptr
		: &found->second.currentState;
}

void SceneStateMachineSystem::Clear() {
	runtimes_.clear();
	externalRequests_.clear();
}
