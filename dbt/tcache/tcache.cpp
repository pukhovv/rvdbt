#include "dbt/tcache/tcache.h"

namespace dbt
{

tcache::L1Cache tcache::l1_cache{};
tcache::L1BrindCache tcache::l1_brind_cache{};
tcache::MapType tcache::tcache_map{};
MemArena tcache::code_pool{};
MemArena tcache::tb_pool{};

void tcache::Init()
{
	l1_cache.fill(nullptr);
	l1_brind_cache.fill({0, nullptr});
	tcache_map.clear();
	tb_pool.Init(TB_POOL_SIZE, PROT_READ | PROT_WRITE);
	code_pool.Init(CODE_POOL_SIZE, PROT_READ | PROT_WRITE | PROT_EXEC);
}

void tcache::Destroy()
{
	log_tcache("Destroy tcache, code_pool size: %zu", code_pool.GetUsedSize());

	l1_cache.fill(nullptr);
	l1_brind_cache.fill({0, nullptr});
	tcache_map.clear();
	tb_pool.Destroy();
	code_pool.Destroy();
}

void tcache::Invalidate()
{
	unreachable("tcache invalidation is not supported");
	l1_cache.fill(nullptr);
	l1_brind_cache.fill({0, nullptr});
	tcache_map.clear();
	tb_pool.Reset();
	code_pool.Reset();
}

void tcache::Insert(TBlock *tb)
{
	tcache_map.insert({tb->ip, tb});
	l1_cache[l1hash(tb->ip)] = tb;
}

TBlock *tcache::LookupUpperBound(u32 gip)
{
	auto it = tcache_map.upper_bound(gip);
	if (it == tcache_map.end()) {
		return nullptr;
	}
	return it->second;
}

TBlock *tcache::LookupFull(u32 gip)
{
	auto it = tcache_map.find(gip);
	if (likely(it != tcache_map.end())) {
		return it->second;
	}
	return nullptr;
}

TBlock *tcache::AllocateTBlock()
{
	auto *res = tb_pool.Allocate<TBlock>();
	if (res == nullptr) {
		Invalidate();
	}
	return new (res) TBlock{};
}

void *tcache::AllocateCode(size_t code_sz, u16 align)
{
	void *res = code_pool.Allocate(code_sz, align);
	if (res == nullptr) {
		Invalidate();
	}
	return res;
}

} // namespace dbt
