#pragma once

#include "dbt/common.h"
#include "dbt/qjit/regalloc.h"
#include "dbt/tcache/tcache.h"
#include <array>

namespace dbt::qjit
{

#define HELPER extern "C" NOINLINE __attribute__((used))
#define HELPER_ASM extern "C" NOINLINE __attribute__((used, naked))

// returns TBlock::TaggedPtr
HELPER uintptr_t enter_tcache(CPUState *state, void *tc_ptr);
HELPER_ASM uintptr_t trampoline_host_qjit(CPUState *state, void *vmem, void *tc_ptr);
HELPER void *helper_tcache_lookup(CPUState *state, TBlock *tb);
HELPER void helper_raise();

struct QuickJIT;

struct Codegen {
	static constexpr auto STATE = asmjit::x86::Gp::kIdR13;
	static constexpr auto MEMBASE = asmjit::x86::Gp::kIdR12;
	static constexpr auto SP = asmjit::x86::Gp::kIdSp;
	static constexpr auto TMP1C = asmjit::x86::Gp::kIdCx;
	static constexpr auto TMP2 = asmjit::x86::Gp::kIdAx;
	static constexpr u16 TB_PROLOGUE_SZ = 0;

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
	void SetupCtx(QuickJIT *ctx_);
	void Prologue();
	void Epilogue();
	void ResetBranchLinks();
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
	void BranchTBInd(asmjit::Operand target);

	void x86Cmp(asmjit::x86::CondCode *cc, asmjit::Operand lhs, asmjit::Operand rhs);

	asmjit::JitRuntime jrt{};
	asmjit::CodeHolder jcode{};
	asmjit::x86::Assembler j{};
	JitErrorHandler jerr{};
	asmjit::Label to_epilogue{};
	std::array<asmjit::Label, 2> branch_links{};

	QuickJIT *ctx{};
};

#ifdef CONFIG_USE_STATEMAPS
struct StateMap {
	using container = u64;
	static constexpr u8 VREG_ID_BITS = 5;
	static constexpr u8 ENTRY_MASK = (1 << VREG_ID_BITS) - 1;
	static constexpr u8 MAX_ENTRIES = sizeof(container) * 8 / VREG_ID_BITS;

	inline u8 Get(u8 e)
	{
		assert(e < MAX_ENTRIES);
		return ENTRY_MASK & (data >> (VREG_ID_BITS * e));
	}
	inline void Set(u8 e, u8 vreg_id)
	{
		assert(e < MAX_ENTRIES);
		assert((vreg_id >> VREG_ID_BITS) == 0);
		u8 shift = VREG_ID_BITS * e;
		data = (data & ~(ENTRY_MASK << shift)) | (vreg_id << shift);
	}

	container data{0};
};
#ifdef CONFIG_USE_STATEMAPS
void CreateStateMap();
std::pair<bool, StateMap> active_sm{false, {}};
#endif
#endif

struct QuickJIT {
	QuickJIT();
	~QuickJIT();
	QuickJIT(QuickJIT &) = delete;
	QuickJIT(QuickJIT &&) = delete;

	RegAlloc *ra{nullptr};
	Codegen *cg{nullptr};
	TBlock *tb{nullptr};

	RegAlloc::VReg *vreg_ip{nullptr};
};

}; // namespace dbt::qjit