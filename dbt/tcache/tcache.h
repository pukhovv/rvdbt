#pragma once

#include "dbt/arena.h"
#include "dbt/common.h"
#include "dbt/core.h"
#include "dbt/qjit/regalloc.h"
#include "dbt/guest/rv32_cpu.h"

#include <array>

namespace dbt
{

struct alignas(8) TBlock {
	struct TCode {
		void *ptr{nullptr};
		size_t size{0};
	};

	struct Branch {
		u32 ip{0};
		u16 slot_offs{0};
	};

	struct TaggedPtr {
		TaggedPtr(uintptr_t raw_) : raw(raw_) {}
		TaggedPtr(void *ptr_, int idx)
		{
			auto ptr = (uintptr_t)ptr_;
			assert((ptr & TAG_MASK) == 0);
			assert(TAG_START + idx <= TAG_MASK);
			raw = ptr | (idx + TAG_START);
		}

		inline uintptr_t getRaw()
		{
			return raw;
		}

		inline void *getPtr()
		{
			return (void *)(raw & ~(TAG_MASK));
		}

		inline int getBranchIdx()
		{
			return (int)(raw & TAG_MASK) - TAG_START;
		}

	private:
		static constexpr uintptr_t TAG_START = 1; // Zero tag marks non-direct jump
		static constexpr uintptr_t TAG_MASK = 0b111;
		uintptr_t raw{0};
	};

	inline void Dump()
	{
		if constexpr (decltype(log_bt())::null)
			return;
		DumpImpl();
	}
	void DumpImpl();

	u32 ip{0};
	TCode tcode{};
	std::array<Branch, 2> branches{};
	u16 epilogue_offs{0};
};

struct tcache {
	static void Init();
	static void Destroy();
	static void Invalidate();
	static void Insert(TBlock *tb);
	static inline TBlock *Lookup(u32 ip)
	{
		auto hash = jmp_hash(ip);
		auto *tb = jmp_cache[hash];
		if (tb != nullptr && tb->ip == ip)
			return tb;
		tb = LookupFull(ip);
		if (tb != nullptr)
			jmp_cache[hash] = tb;
		return tb;
	}

	static void *AllocateCode(size_t sz, u16 align);
	static TBlock *AllocateTBlock();

	static constexpr u32 JMP_CACHE_BITS = 10;
	static constexpr u32 JMP_HASH_MULT = 2654435761;
	static std::array<TBlock *, 1u << JMP_CACHE_BITS> jmp_cache;

private:
	tcache() {}

	static TBlock *LookupFull(u32 ip);

	static inline u32 jmp_hash(u32 ip)
	{
		u32 constexpr gr = JMP_HASH_MULT;
		return (gr * ip) >> (32 - JMP_CACHE_BITS);
	}

	using MapType = std::unordered_map<u32, TBlock *>;
	static MapType tcache_map;

	static constexpr size_t TB_POOL_SIZE = 32 * 1024 * 1024;
	static MemArena tb_pool;

	static constexpr size_t CODE_POOL_SIZE = 128 * 1024 * 1024;
	static MemArena code_pool;
};
} // namespace dbt
