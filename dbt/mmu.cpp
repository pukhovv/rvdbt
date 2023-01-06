#include "dbt/mmu.h"
#include "dbt/ukernel.h"
#include <cstdlib>
#include <map>

namespace dbt
{
LOG_STREAM(mmu);

void *host_mmap(void *addr, size_t len, int prot, int flags, int fd, __off_t offset)
{
	if (!addr) {
		addr = mmu::base + mmu::ASPACE_SIZE;
	}
	void *res = ::mmap(addr, len, prot, flags, fd, offset);
	log_mmu("host_mmap allocated at %p", res);

	if (mmu::check_h2g(res)) {
		log_mmu("host alloc in guest mem"); // TODO: this still fails, need custom allocator
	}
	return res;
}

u8 *mmu::base{nullptr};

void mmu::Init()
{
#ifndef CONFIG_ZERO_MMU_BASE
	// Allocate and immediately deallocate region, result is g2h(0)
	base = (u8 *)::mmap(NULL, ASPACE_SIZE, PROT_NONE, MAP_PRIVATE | MAP_ANON, -1, 0);
	if (base == MAP_FAILED || munmap(base + MIN_MMAP_ADDR, ASPACE_SIZE - MIN_MMAP_ADDR)) {
		Panic("mmu::Init failed");
	}
	log_mmu("mmu::base initialized at %p", base);
#endif
}

void mmu::Destroy()
{
	int rc = munmap(base, ASPACE_SIZE);
	if (rc) {
		Panic("mmu::Destroy failed");
	}
}

void *mmu::mmap(u32 vaddr, u32 len, int prot, int flags, int fd, size_t offs)
{
	assert((u64)vaddr + len - 1 < ASPACE_SIZE);
	if (flags & MAP_FIXED) {
		void *hptr = g2h(vaddr);
		hptr = ::mmap(hptr, len, prot, flags, fd, offs);
		if (hptr == MAP_FAILED) {
			Panic("mmu::mmap fixed failed");
		}
		return hptr;
	}

	void *hptr;
	void *hptr_prev = nullptr;
	while (1) {
		hptr = ::mmap(g2h(mmu::MIN_MMAP_ADDR), len, PROT_NONE,
			      MAP_ANONYMOUS | MAP_PRIVATE | MAP_NORESERVE, -1, 0);
		if (hptr == MAP_FAILED) {
			Panic("mmu::mmap failed, probably host oom");
		}
		if (check_h2g((u8 *)hptr + len - 1)) {
			goto success;
		}
		[[maybe_unused]] int rc = munmap(hptr, len);
		assert(!rc);
		if (hptr == hptr_prev) {
			break;
		}
		log_mmu("mmu::mmap: miss %p", hptr);
		hptr_prev = hptr;
	}
	Panic("mmu::mmap failed");

success:
	void *res = ::mmap(hptr, len, prot, flags | MAP_FIXED, fd, offs);
	if (res == MAP_FAILED) {
		Panic();
	}
	assert(res == hptr);
	log_mmu("mmu::mmap allocated at %p", hptr);
	return res;
}

} // namespace dbt
