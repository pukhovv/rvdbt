#include "dbt/core.h"
#include "dbt/ukernel.h"
#include <cstdlib>
#include <map>

namespace dbt
{
LOG_STREAM(log_mmu, "[mmu]");

void __attribute__((noreturn)) Panic(char const *msg)
{
	fprintf(stderr, "Panic: %s\n", msg);
	abort();
}

u8 *mmu::base{nullptr};

void mmu::Init()
{
#ifndef CONFIG_ZERO_MMU_BASE
	// Allocate and immediately deallocate region, result is g2h(0)
	base = (u8 *)mmap(NULL, ASPACE_SIZE, PROT_NONE, MAP_PRIVATE | MAP_ANON, -1, 0);
	if (base == MAP_FAILED || munmap(base + MIN_MMAP_ADDR, ASPACE_SIZE - MIN_MMAP_ADDR)) {
		Panic("mmu::Init failed");
	}
#endif
}

void mmu::Destroy()
{
	int rc = munmap(base, ASPACE_SIZE);
	if (rc) {
		Panic("mmu::Destroy failed");
	}
}

void *mmu::MMap(u32 vaddr, u32 len, int prot, int flags, int fd, size_t offs)
{
	assert(len == roundup((size_t)len, PAGE_SIZE));
	assert((u64)vaddr + len - 1 < ASPACE_SIZE);
	if (flags & MAP_FIXED) {
		void *hptr = g2h(vaddr);
		hptr = mmap(hptr, len, prot, flags, fd, offs);
		if (hptr == MAP_FAILED) {
			Panic("mmu::MMap fixed failed");
		}
		return hptr;
	}

	void *hptr_prev = nullptr;
	while (1) {
		void *hptr = mmap(g2h(mmu::PAGE_SIZE), len, prot, flags, fd, offs);
		if (hptr == MAP_FAILED) {
			Panic("mmu::MMap failed, probably host oom");
		}
		if (check_h2g((u8 *)hptr + len - 1)) {
			return hptr;
		}
		[[maybe_unused]] int rc = munmap(hptr, len);
		assert(!rc);
		if (hptr == hptr_prev) {
			break;
		}
		log_mmu("mmap-miss %p", hptr);
		hptr_prev = hptr;
	}
	Panic("mmu::MMap failed");
}

} // namespace dbt
