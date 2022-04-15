#pragma once

#include "dbt/common.h"

extern "C" {
#include <sys/mman.h>
};

struct MemArena {
	void Init(size_t size, int prot = PROT_READ | PROT_WRITE);
	void Destroy();
	void Reset();
	void *Allocate(size_t alloc_sz, size_t align);

private:
	u8 *pool{nullptr};
	size_t pool_sz{0};
	size_t used{0};
};
