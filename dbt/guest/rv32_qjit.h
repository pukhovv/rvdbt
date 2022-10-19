#pragma once

#include "dbt/guest/rv32_insn.h"
#include "dbt/qjit/qjit.h"

namespace dbt::qjit::rv32
{
using namespace dbt::rv32;

struct QuickTranslator : public QuickJIT {
	static constexpr u16 TB_MAX_INSNS = dbt::rv32::TB_MAX_INSNS;

	enum class Control { NEXT, BRANCH, TB_OVF } control{Control::NEXT};
	u32 insn_ip{0};
	std::array<RegAlloc::VReg *, 32> vreg_gpr{};

#define OP(name, format_, flags_)                                                                            \
	void H_##name(void *insn);                                                                           \
	void Impl_##name(rv32::insn::Insn_##name);                                                           \
	static constexpr auto _##name = &QuickTranslator::H_##name;
	RV32_OPCODE_LIST()
#undef OP

	static TBlock *Translate(CPUState *state, u32 ip);

private:
	explicit QuickTranslator();

	void TranslateInsn();

	void TranslateBranchCC(rv32::insn::B i, asmjit::x86::CondCode cc);
	void TranslateSetCCR(rv32::insn::R i, asmjit::x86::CondCode cc);
	void TranslateSetCCI(rv32::insn::I i, asmjit::x86::CondCode cc);
	void TranslateArithmRR(rv32::insn::R i, asmjit::x86::Inst::Id asmop, bool force_cx);
	void TranslateArithmRI(rv32::insn::I i, asmjit::x86::Inst::Id asmop, i32 zeroval);
	void TranslateShiftI(rv32::insn::IS i, asmjit::x86::Inst::Id asmop);
	asmjit::x86::Mem CreateMemOp(RegAlloc::VReg *base, u32 offs, u8 sz);
};

} // namespace dbt::qjit::rv32
