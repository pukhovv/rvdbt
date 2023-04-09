#include "dbt/mmu.h"
#include "dbt/ukernel.h"
#include <cstdlib>
#include <map>

namespace dbt
{
LOG_STREAM(mmu);

void *host_mmap(void *addr, size_t len, int prot, int flags, int fd, __off_t offset)
{
	if (!addr) { // hint: prefer not to allocate in 32-bit guest addresses
		addr = mmu::base + mmu::ASPACE_SIZE;
	}
	void *res = ::mmap(addr, len, prot, flags, fd, offset);
	log_mmu("host_mmap allocated at %p", res);

	if (mmu::check_h2g(res)) {
		log_mmu("host alloc in guest mem");
	}
	return res;
}

u8 *mmu::base{nullptr};
u32 mmu::mmap_hint_page = mmu::MIN_MMAP_ADDR >> mmu::PAGE_BITS;
std::bitset<(mmu::ASPACE_SIZE >> mmu::PAGE_BITS)> mmu::used_pages;

void mmu::Init()
{
	MarkUsedPages(0, MIN_MMAP_ADDR);

	if constexpr (!config::zero_membase) {
		// Allocate and immediately deallocate region, result is g2h(0)
		base =
		    (u8 *)::mmap(NULL, ASPACE_SIZE, PROT_NONE, MAP_PRIVATE | MAP_ANON | MAP_NORESERVE, -1, 0);
		if (base == MAP_FAILED || munmap(base + MIN_MMAP_ADDR, ASPACE_SIZE - MIN_MMAP_ADDR)) {
			Panic("mmu::Init failed");
		}
	}
	log_mmu("mmu::base initialized at %p", base);
}

void mmu::Destroy()
{
	int rc = munmap(base, ASPACE_SIZE);
	if (rc) {
		Panic("mmu::Destroy failed");
	}
}

void mmu::MarkUsedPages(u32 pvaddr, u32 plen)
{
	for (u32 p = pvaddr; p < plen; ++p) {
		used_pages.set(p);
	}
}

void mmu::MarkFreePages(u32 pvaddr, u32 plen)
{
	for (u32 p = pvaddr; p < plen; ++p) {
		used_pages.reset(p);
	}
}

u32 mmu::LookupFreeRange(u32 pvaddr, u32 plen)
{
	u32 count = plen;
	for (u32 p = pvaddr; p < ASPACE_SIZE >> PAGE_BITS; ++p) {
		if (used_pages.test(p)) {
			count = plen;
		} else if (--count == 0) {
			return p + 1 - plen;
		}
	}
	return 0;
}

void *mmu::mmap(u32 vaddr, u32 len, int prot, int flags, int fd, size_t offs)
{
	assert((u64)vaddr + len - 1 < ASPACE_SIZE);
	len = roundup(len, PAGE_SIZE);
	u32 const plen = len >> PAGE_BITS;

	if (flags & MAP_FIXED) {
		void *hptr = g2h(vaddr);
		hptr = ::mmap(hptr, len, prot, flags, fd, offs);
		if (hptr == MAP_FAILED) {
			log_mmu("mmu::mmap fixed failed");
			return MAP_FAILED;
		}
		log_mmu("mmu::mmap allocated at %p", hptr);
		MarkUsedPages(vaddr >> PAGE_BITS, plen);
		return hptr;
	}

	u32 paddr = mmap_hint_page;
	bool paddr_wrapped = false;
	void *hptr;
	while (1) {
		while (1) {
			paddr = LookupFreeRange(paddr, plen);
			if (paddr_wrapped && (paddr == 0 || paddr > mmap_hint_page)) {
				log_mmu("mmu::mmap: no free range in vm");
				return MAP_FAILED;
			}
			if (paddr == 0) {
				log_mmu("mmu::mmap: vm lookup wrapped");
				paddr_wrapped = true;
				paddr = MIN_MMAP_ADDR >> PAGE_BITS;
			} else {
				break;
			}
		}

		log_mmu("mmu::mmap: try at %p", g2h(paddr << PAGE_BITS));
		hptr = ::mmap(g2h(paddr << PAGE_BITS), len, PROT_NONE,
			      MAP_ANONYMOUS | MAP_PRIVATE | MAP_NORESERVE, -1, 0);
		if (hptr == MAP_FAILED) {
			Panic("mmu::mmap failed, probably host oom");
		}
		if (check_h2g((u8 *)hptr + len - 1)) {
			break;
		}
		log_mmu("mmu::mmap: miss %p", hptr);
		if (::munmap(hptr, len) != 0) {
			Panic();
		}
		paddr += plen;
	}

	void *res = ::mmap(hptr, len, prot, flags | MAP_FIXED, fd, offs);
	if (res == MAP_FAILED || res != hptr) {
		Panic();
	}
	log_mmu("mmu::mmap allocated at %p", hptr);
	paddr = h2g(hptr) >> PAGE_BITS;
	MarkUsedPages(paddr, plen);
	mmap_hint_page = paddr + plen;
	return res;
}

} // namespace dbt
