#pragma once

#include "dbt/arena.h"
#include "dbt/common.h"
#include "dbt/core.h"
#include "dbt/regalloc.h"
#include "dbt/rv32i_runtime.h"

#include "asmjit_deps.h"
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
	u32 size{0};
	TCode tcode{};
	std::array<Branch, 2> branches{};
	u16 epilogue_offs{0};
};

#define HELPER extern "C" NOINLINE __attribute__((used))

// returns TBlock::TaggedPtr
HELPER uintptr_t enter_tcache(rv32i::CPUState *state, void *tc_ptr);
HELPER void *helper_tcache_lookup(rv32i::CPUState *state, TBlock *tb);
HELPER void helper_raise();

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

namespace translator
{

struct Codegen {
	static constexpr auto TR_RREG = asmjit::x86::Gp::kIdAx;
	static constexpr auto TR_TMP_CX = asmjit::x86::Gp::kIdCx;
	static constexpr auto TR_TMP3 = asmjit::x86::Gp::kIdR8;
	static constexpr u16 TB_PROLOGUE_SZ = 7;

	struct TBLinker {
#define USE_REL_BRANCH_SLOT
#ifdef USE_REL_BRANCH_SLOT
		using slot_type = u32;
		static constexpr u16 BRANCH_INSN_SLOT_OFFS = 1;
		static constexpr u16 BRANCH_SLOT_RESET = 0;
#else
		using slot_type = u64;
		static constexpr u16 BRANCH_INSN_SLOT_OFFS = 2;
		static constexpr u16 BRANCH_SLOT_RESET = 10;
#endif
		static inline slot_type *getSlot(TBlock *tb, u8 idx)
		{
			return (slot_type *)((uintptr_t)tb->tcode.ptr + tb->branches[idx].slot_offs);
		}

		static inline void *GetEntrypoint(TBlock *tb)
		{
			return (void *)((uintptr_t)tb->tcode.ptr + TB_PROLOGUE_SZ);
		}

		static inline void *GetExitpoint(TBlock *tb)
		{
			return (void *)((uintptr_t)tb->tcode.ptr + tb->epilogue_offs);
		}

		static inline void InitBranch(TBlock *tb, u8 idx, u16 insn_offs)
		{
			tb->branches[idx].slot_offs = insn_offs + TBLinker::BRANCH_INSN_SLOT_OFFS;
			ResetBranch(tb, idx);
		}

		static inline void LinkBranch(TBlock *from, u8 idx, TBlock *to)
		{
			assert(from->branches[idx].ip == to->ip);
			auto *slot = getSlot(from, idx);
#ifdef USE_REL_BRANCH_SLOT
			uintptr_t base = (uintptr_t)slot + sizeof(*slot); // arch-dependent
#else
			uintptr_t base = 0;
#endif
			unaligned_store<slot_type>(slot, (uintptr_t)GetEntrypoint(to) - base);
		}

		static inline void ResetBranch(TBlock *tb, u8 idx)
		{
			auto *slot = getSlot(tb, idx);
#ifdef USE_REL_BRANCH_SLOT
			uintptr_t reset = BRANCH_SLOT_RESET;
#else
			uintptr_t reset = (uintptr_t)slot + BRANCH_SLOT_RESET;
#endif
			unaligned_store<slot_type>(slot, reset);
		}

	private:
		TBLinker();
	};

	Codegen();
	void SetupRA(RegAlloc *ra);
	void Prologue();
	void Epilogue();
	void SetBranchLinks();
	void EmitCode();

	struct JitErrorHandler : asmjit::ErrorHandler {
		virtual void handleError(asmjit::Error err, const char *message,
					 asmjit::BaseEmitter *origin) override
		{
			Panic("jit codegen failed");
		}
	};

	static inline asmjit::x86::Gp GetPReg(RegAlloc::VReg *v)
	{
		assert(v);
		assert(v->loc == RegAlloc::VReg::Loc::REG);
		switch (v->type) {
		case RegAlloc::VReg::Type::I32:
			return asmjit::x86::Gpd(v->p);
		case RegAlloc::VReg::Type::I64:
			return asmjit::x86::Gpq(v->p);
		default:
			Panic();
		}
	}

	static inline asmjit::x86::Mem GetSpillMem(RegAlloc::VReg *v)
	{
		assert(v);
		assert(v->loc == RegAlloc::VReg::Loc::MEM);
		return GetMemRef(v->spill_base, v->spill_offs, RegAlloc::TypeToSize(v->type));
	}

	static inline asmjit::x86::Mem GetMemRef(RegAlloc::VReg *v, i32 offs, u8 size)
	{
		assert(v->type == RegAlloc::VReg::Type::I64);
		return asmjit::x86::Mem(GetPReg(v), offs, size);
	}

	void Bind(asmjit::Label l);
	void BranchCC(asmjit::Label taken, asmjit::x86::CondCode cc, asmjit::Operand lhs,
		      asmjit::Operand rhs);
	void SetCC(asmjit::x86::CondCode cc, asmjit::Operand rd, asmjit::Operand lhs, asmjit::Operand rhs);

	void Call(asmjit::Operand const *args, u8 nargs);
	void BranchTBDir(u32 ip, u8 no, bool pre_epilogue = false);
	void BranchTBInd(asmjit::x86::Gpd target);

	void x86Cmp(asmjit::x86::CondCode *cc, asmjit::Operand lhs, asmjit::Operand rhs);

	asmjit::JitRuntime jrt{};
	asmjit::CodeHolder jcode{};
	asmjit::x86::Assembler jasm{};
	JitErrorHandler jerr{};
	asmjit::Label to_epilogue{};
	std::array<asmjit::Label, 2> branch_links{};
};

struct Context {
	Context();
	~Context();
	Context(Context &) = delete;
	Context(Context &&) = delete;

	static inline Context *Current()
	{
		return current;
	}

	void TranslateInsn();

	TBlock *tb{nullptr};
	u32 insn_ip{0};
	enum class Control { NEXT, BRANCH, TB_OVF } control{Control::NEXT};

	std::array<RegAlloc::VReg *, 32> vreg_gpr{};
	RegAlloc::VReg *vreg_ip{nullptr};

	RegAlloc ra{};
	Codegen cg{};

private:
	static Context *current;
};

static constexpr u16 TB_MAX_INSNS = 64;
TBlock *Translate(rv32i::CPUState *state, u32 ip);

}; // namespace translator
} // namespace dbt
