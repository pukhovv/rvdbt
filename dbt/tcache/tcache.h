#pragma once

#include "dbt/arena.h"
#include "dbt/mmu.h"
#include "dbt/util/logger.h"

#include <array>
#include <map>

namespace dbt
{
LOG_STREAM(cflow);

struct alignas(8) TBlock {
	struct TCode {
		void *ptr{nullptr};
		size_t size{0};
	};

	TCode tcode{};
	u32 ip{0};
	struct {
		bool is_brind_target : 1 {false};
	} flags;
};

struct tcache {
	static void Init();
	static void Destroy();
	static void Invalidate();
	static void Insert(TBlock *tb);

	static ALWAYS_INLINE TBlock *LookupFast(u32 ip)
	{
		auto *tb = jmp_cache_generic[jmp_hash(ip)];
		return (tb->ip == ip) ? tb : nullptr;
	}

	static inline TBlock *Lookup(u32 ip)
	{
		auto hash = jmp_hash(ip);
		auto *tb = jmp_cache_generic[hash];
		if (tb != nullptr && tb->ip == ip)
			return tb;
		tb = LookupFull(ip);
		if (tb != nullptr)
			jmp_cache_generic[hash] = tb;
		return tb;
	}

	static TBlock *LookupUpperBound(u32 ip)
	{
		auto it = tcache_map.upper_bound(ip);
		if (it == tcache_map.end()) {
			return nullptr;
		}
		return it->second;
	}

	static inline void OnTranslate(TBlock *tb)
	{
		log_cflow("B%08x[fillcolor=cyan]", tb->ip);
	}

	static inline void OnTranslateBr(TBlock *tb, u32 tgtip)
	{
		if (rounddown(tb->ip, mmu::PAGE_SIZE) != rounddown(tgtip, mmu::PAGE_SIZE)) {
			log_cflow("B%08x->B%08x[color=blue,penwidth=2]", tb->ip, tgtip);
		} else if (tb->ip >= tgtip) {
			log_cflow("B%08x->B%08x[color=red,penwidth=2,dir=back]", tgtip, tb->ip);
		} else {
			log_cflow("B%08x->B%08x", tb->ip, tgtip);
		}
	}

	static inline void OnTranslateBrind(TBlock *tb)
	{
		log_cflow("B%08x_brind[fillcolor=purple,shape=point];"
			  "B%08x->B%08x_brind[color=purple,penwidth=3]",
			  tb->ip, tb->ip, tb->ip);
	}

	static inline void OnBrind(TBlock *tb)
	{
		jmp_cache_brind[jmp_hash(tb->ip)] = tb;
		if (log_cflow.enabled()) {
			if (!tb->flags.is_brind_target) {
				log_cflow("B%08x[fillcolor=orange]", tb->ip);
			}
		}
		tb->flags.is_brind_target = true;
	}

	static void *AllocateCode(size_t sz, u16 align);
	static TBlock *AllocateTBlock();

	static constexpr u32 JMP_CACHE_BITS = 12;
	using JMPCache = std::array<TBlock *, 1u << JMP_CACHE_BITS>;
	static JMPCache jmp_cache_generic;
	static JMPCache jmp_cache_brind;

private:
	tcache() = delete;

	static TBlock *LookupFull(u32 ip);

	static ALWAYS_INLINE u32 jmp_hash(u32 ip)
	{
		return (ip >> 2) & ((1ull << JMP_CACHE_BITS) - 1);
	}

	using MapType = std::map<u32, TBlock *>;
	static MapType tcache_map;

	static constexpr size_t TB_POOL_SIZE = 32 * 1024 * 1024;
	static MemArena tb_pool;

	static constexpr size_t CODE_POOL_SIZE = 128 * 1024 * 1024;
	static MemArena code_pool;
};
} // namespace dbt
