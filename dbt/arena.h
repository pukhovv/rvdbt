#pragma once

#include "dbt/util/common.h"

extern "C" {
#include <sys/mman.h>
};

struct MemArena {
	MemArena() = default;
	MemArena(size_t size, int prot = PROT_READ | PROT_WRITE)
	{
		Init(size, prot);
	}
	~MemArena()
	{
		Destroy();
	}

	void Init(size_t size, int prot = PROT_READ | PROT_WRITE);
	void Destroy();
	void Reset();
	void *Allocate(size_t alloc_sz, size_t align);

	template <typename T>
	inline T *Allocate(size_t num = 1)
	{
		return (T *)Allocate(sizeof(T) * num, alignof(T));
	}

	size_t GetUsedSize() const
	{
		return used;
	}

	inline void *BaseAddr() const
	{
		return pool;
	}

private:
	u8 *pool{nullptr};
	size_t pool_sz{0};
	size_t used{0};
};

template <typename T>
struct MemArenaSTL {
	using value_type = T;
	using pointer = T *;
	using size_type = std::size_t;
	using difference_type = std::ptrdiff_t;

	pointer allocate(size_type n)
	{
		return arena->Allocate<T>(n);
	}

	void deallocate(pointer p, size_type n) {}

	bool operator==(MemArenaSTL const &rhs)
	{
		return this->arena == rhs.arena;
	}

	bool operator!=(MemArenaSTL const &rhs)
	{
		return !operator==(rhs);
	}

	explicit MemArenaSTL(MemArena *arena_) : arena(arena_) {}

	MemArenaSTL(MemArenaSTL const &rhs) : arena(rhs.arena) {}

	template <typename U>
	MemArenaSTL(MemArenaSTL<U> const &rhs) : arena(rhs.arena)
	{
	}

	MemArenaSTL(MemArenaSTL &&rhs) : arena(rhs.arena)
	{
		rhs.arena = nullptr;
	}

	template <typename U>
	MemArenaSTL(MemArenaSTL<U> &&rhs) : arena(rhs.arena)
	{
		rhs.arena = nullptr;
	}

private:
	MemArena *arena{};
};
