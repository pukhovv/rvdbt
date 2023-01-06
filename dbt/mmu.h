#pragma once

#include "dbt/util/logger.h"
#include "dbt/util/allocator.h"
#include <csetjmp>
#include <cstdint>
#include <unordered_map>
extern "C" {
#include <sys/mman.h>
}

namespace dbt
{

struct mmu {
	static constexpr size_t ASPACE_SIZE = (1ull) << 32;
	static constexpr size_t PAGE_BITS = 12; // true for rv32 and amd64
	static constexpr size_t PAGE_SIZE = 1 << PAGE_BITS;
	static constexpr size_t PAGE_MASK = ~(PAGE_SIZE - 1);
	static constexpr size_t MIN_MMAP_ADDR = PAGE_SIZE;
	static void Init();
	static void Destroy();
	static void *mmap(u32 vaddr, u32 len, int prot, int flag = MAP_ANON | MAP_PRIVATE | MAP_FIXED,
			  int fd = -1, size_t offs = 0);

	static inline bool check_h2g(void *hptr)
	{
		return ((uptr)hptr - (uptr)base) < ASPACE_SIZE;
	}

	static inline u32 h2g(void *hptr)
	{
		return (uptr)hptr - (uptr)base;
	}

	static inline void *g2h(u32 gptr)
	{
		return base + gptr;
	}

	static u8 *base;

private:
	mmu() = delete;
};

} // namespace dbt
