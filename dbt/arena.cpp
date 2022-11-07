#include "dbt/arena.h"
#include "dbt/core.h"

void MemArena::Init(size_t size, int prot)
{
	assert(!pool);
	pool_sz = roundup(size, 4096);
	used = 0;
	pool = (u8 *)mmap(NULL, pool_sz, prot, MAP_ANON | MAP_PRIVATE, -1, 0);
	if (pool == MAP_FAILED) {
		dbt::Panic("MemArena::Init failed");
	}
}

void MemArena::Destroy()
{
	if (!pool)
		return;
	int rc = munmap(pool, pool_sz);
	if (rc) {
		dbt::Panic("MemArena::Destroy failed");
	}
	pool = nullptr;
}

void MemArena::Reset()
{
	used = 0;
}

void *MemArena::Allocate(size_t alloc_sz, size_t align)
{
	size_t alloc_start = roundup(used, align);
	if (alloc_start + alloc_sz > pool_sz) {
		return nullptr;
	}
	used = alloc_start + alloc_sz;
	return (void *)(pool + alloc_start);
}
