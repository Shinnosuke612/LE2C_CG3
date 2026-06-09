#include "CollisionManager.h"

#include "Collider.h"

#include <algorithm>

CollisionManager* CollisionManager::GetInstance() {
	static CollisionManager instance;
	return &instance;
}

void CollisionManager::Clear() {
	colliders_.clear();
	collisionPairs_.clear();
}

void CollisionManager::Register(Collider* collider) {
	if (!collider) {
		return;
	}

	if (std::find(colliders_.begin(), colliders_.end(), collider) != colliders_.end()) {
		return;
	}

	colliders_.push_back(collider);
}

void CollisionManager::Unregister(Collider* collider) {
	colliders_.erase(
		std::remove(colliders_.begin(), colliders_.end(), collider),
		colliders_.end()
	);
}

void CollisionManager::CheckAllCollisions() {
	collisionPairs_.clear();

	for (size_t i = 0; i < colliders_.size(); ++i) {
		for (size_t j = i + 1; j < colliders_.size(); ++j) {
			Collider* a = colliders_[i];
			Collider* b = colliders_[j];

			if (!a || !b) {
				continue;
			}

			if (a->Intersects(*b)) {
				collisionPairs_.push_back({ a, b });
			}
		}
	}
}
