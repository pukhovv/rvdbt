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

private:
	u8 *pool{nullptr};
	size_t pool_sz{0};
	size_t used{0};
};
