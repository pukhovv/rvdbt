#pragma once

#include "dbt/common.h"
#include "dbt/qjit/regalloc.h"
#include "dbt/tcache/tcache.h"
#include <array>

namespace dbt::qjit
{

struct BranchSlot {
	void Reset();
	void Link(void *to);

	u8 code[12];
	u32 gip;
} __attribute__((packed));

#define HELPER extern "C" NOINLINE __attribute__((used))
#define HELPER_ASM extern "C" NOINLINE __attribute__((used, naked))

HELPER_ASM BranchSlot *trampoline_host_to_qjit(CPUState *state, void *vmem, void *tc_ptr);

struct QuickJIT;

struct Codegen {
	static constexpr auto STATE = asmjit::x86::Gp::kIdR13;
	static constexpr auto MEMBASE = asmjit::x86::Gp::kIdR12;
	static constexpr auto SP = asmjit::x86::Gp::kIdSp;
	static constexpr auto TMP1C = asmjit::x86::Gp::kIdCx;

	Codegen();
	void SetupCtx(QuickJIT *ctx_);
	void Prologue();
	void Epilogue();
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

	void Spill(RegAlloc::VReg *v); // Internal
	void Fill(RegAlloc::VReg *v);  // Internal

	asmjit::JitRuntime jrt{};
	asmjit::CodeHolder jcode{};
	asmjit::x86::Assembler j{};
	JitErrorHandler jerr{};

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