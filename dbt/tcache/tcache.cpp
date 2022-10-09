#include "dbt/tcache/tcache.h"
#include "dbt/arena.h"
#include "dbt/core.h"
#include "dbt/execute.h"
#include "dbt/guest/rv32_cpu.h"
#include "dbt/guest/rv32_insn.h"
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <ostream>
#include <sys/types.h>
#include <type_traits>

namespace dbt
{

void TBlock::DumpImpl()
{
	size_t sz = tcode.size;
	auto p = (u8 *)tcode.ptr;
	auto log = log_bt();
	log << "jitcode: ";
	for (size_t i = 0; i < sz; ++i) {
		char buf[4] = {};
		sprintf(buf, "%2.2x", p[i]);
		log.write(buf);
	}
}

tcache::JMPCache tcache::jmp_cache_generic{};
tcache::JMPCache tcache::jmp_cache_brind{};
tcache::MapType tcache::tcache_map{};
MemArena tcache::code_pool{};
MemArena tcache::tb_pool{};

void tcache::Init()
{
	jmp_cache_generic.fill(nullptr);
	jmp_cache_brind.fill(nullptr);
	tcache_map.clear();
	tb_pool.Init(TB_POOL_SIZE, PROT_READ | PROT_WRITE);
	code_pool.Init(CODE_POOL_SIZE, PROT_READ | PROT_WRITE | PROT_EXEC);
}

void tcache::Destroy()
{
	jmp_cache_generic.fill(nullptr);
	jmp_cache_brind.fill(nullptr);
	tcache_map.clear();
	tb_pool.Destroy();
	code_pool.Destroy();
}

void tcache::Invalidate()
{
	jmp_cache_generic.fill(nullptr);
	jmp_cache_brind.fill(nullptr);
	tcache_map.clear();
	tb_pool.Reset();
	code_pool.Reset();
}

void tcache::Insert(TBlock *tb)
{
	tcache_map.insert({tb->ip, tb});
	jmp_cache_generic[jmp_hash(tb->ip)] = tb;
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
	TBlock *res = (TBlock *)tb_pool.Allocate(sizeof(*res), std::alignment_of_v<decltype(*res)>);
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
