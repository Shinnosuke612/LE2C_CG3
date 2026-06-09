#pragma once

#include <vector>

class Collider;

class CollisionManager {
public:
	struct CollisionPair {
		Collider* a = nullptr;
		Collider* b = nullptr;
	};

public:
	static CollisionManager* GetInstance();

	void Clear();
	void Register(Collider* collider);
	void Unregister(Collider* collider);

	void CheckAllCollisions();

	const std::vector<CollisionPair>& GetCollisionPairs() const { return collisionPairs_; }

private:
	CollisionManager() = default;

private:
	std::vector<Collider*> colliders_;
	std::vector<CollisionPair> collisionPairs_;
};
