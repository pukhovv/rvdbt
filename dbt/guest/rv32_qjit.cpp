#include "dbt/guest/rv32_qjit.h"
#include "dbt/common.h"
#include "dbt/core.h"
#include "dbt/guest/rv32_decode.h"
#include "dbt/guest/rv32_runtime.h"
#include <cstdint>
#include <cstdlib>
#include <sstream>
#include <type_traits>

namespace dbt::qjit::rv32
{
using namespace rv32;

QuickTranslator::QuickTranslator()
{
	vreg_gpr[0] = nullptr;
	for (u8 i = 1; i < vreg_gpr.size(); ++i) {
		char const *name = rv32::insn::GRPToName(i);
		u16 offs = offsetof(CPUState, gpr) + 4 * i;
		vreg_gpr[i] = ra->AllocVRegMem(name, RegAlloc::VReg::Type::I32, ra->state_base, offs);
#ifdef CONFIG_USE_STATEMAPS
		vreg_gpr[i]->has_statemap = true;
#endif
	}
	vreg_ip = ra->AllocVRegMem("ip", RegAlloc::VReg::Type::I32, ra->state_base, offsetof(CPUState, ip));
}

TBlock *QuickTranslator::Translate(CPUState *state, u32 ip)
{
	// TODO: check if ip is mapped
	QuickTranslator t{};
	t.tb = tcache::AllocateTBlock();
	if (t.tb == nullptr) {
		Panic();
	}
	t.tb->ip = ip;
	t.insn_ip = ip;

	log_qjit("Translate [%08x]", ip);
	t.ra->Prologue();
	t.cg->Prologue();

	u32 upper_bound = -1;
	if (auto *tb_bound = tcache::LookupUpperBound(ip)) {
		upper_bound = tb_bound->ip;
	}

	u32 num_insns = 0;
	while (true) {
		t.TranslateInsn();
		num_insns++;
		if (t.control != Control::NEXT) {
			break;
		}
		if (num_insns == TB_MAX_INSNS || t.insn_ip == upper_bound) {
			t.control = Control::TB_OVF;
			t.cg->BranchTBDir(t.insn_ip);
			break;
		}
	}

	t.cg->Epilogue();

	log_qjit("Emit[%08x]", ip);
	t.cg->EmitCode();
	t.cg->DumpCode();

	return t.tb;
}

static asmjit::Operand ToOperand(RegAlloc::VReg *vreg)
{
	if (!vreg) {
		return asmjit::imm(0);
	}
	switch (vreg->loc) {
	case RegAlloc::VReg::Loc::REG:
		return vreg->GetPReg();
	case RegAlloc::VReg::Loc::MEM:
		return vreg->GetSpill();
	default:
		Panic();
	}
}

static inline std::array<RegAlloc::VReg *, 0> no_regs()
{
	return {};
}

void QuickTranslator::TranslateBranchCC(insn::B i, asmjit::x86::CondCode cc)
{
	std::array vrs = {vreg_gpr[i.rs1()], vreg_gpr[i.rs2()]};
	ra->AllocOp(no_regs(), vrs);

	auto br_taken = cg->j.newLabel();

	cg->BranchCC(br_taken, cc, ToOperand(vrs[0]), ToOperand(vrs[1]));
	cg->BranchTBDir(insn_ip + 4);
	cg->Bind(br_taken);
	// TODO: check alignment
	cg->BranchTBDir(insn_ip + i.imm());
}

void QuickTranslator::TranslateSetCCR(insn::R i, asmjit::x86::CondCode cc)
{
	if (!i.rd()) {
		return;
	}
	std::array vrs = {vreg_gpr[i.rs1()], vreg_gpr[i.rs2()]};
	auto vrd = vreg_gpr[i.rd()];
	ra->AllocOp(std::array{vrd}, vrs);

	cg->SetCC(cc, ToOperand(vrd), ToOperand(vrs[0]), ToOperand(vrs[1]));
}

void QuickTranslator::TranslateSetCCI(insn::I i, asmjit::x86::CondCode cc)
{
	if (!i.rd()) {
		return;
	}
	std::array vrs = {vreg_gpr[i.rs1()]};
	auto vrd = vreg_gpr[i.rd()];
	ra->AllocOp(std::array{vrd}, vrs);

	cg->SetCC(cc, ToOperand(vrd), ToOperand(vrs[0]), asmjit::imm(i.imm()));
}

void QuickTranslator::TranslateArithmRR(insn::R i, asmjit::x86::Inst::Id asmop, bool force_cx)
{
	if (!i.rd()) {
		return;
	}
	std::array vrs = {vreg_gpr[i.rs1()], vreg_gpr[i.rs2()]};
	auto vrd = vreg_gpr[i.rd()];
	ra->AllocOp(std::array{vrd}, vrs);

	asmjit::Operand rhs;
	if (i.rd() == i.rs2() || force_cx) {
		auto rhs_gp = asmjit::x86::gpd(Codegen::TMP1C);
		cg->j.mov(rhs_gp, vrs[1]->GetPReg());
		rhs = rhs_gp;
	} else {
		rhs = ToOperand(vrs[1]);
	}

	if (!vrs[0]) {
		cg->j.xor_(vrd->GetPReg(), vrd->GetPReg());
	} else if (vrd->GetPReg() != vrs[0]->GetPReg()) {
		cg->j.mov(vrd->GetPReg(), vrs[0]->GetPReg());
	}

	cg->j.emit(asmop, vrd->GetPReg(), rhs);
}

void QuickTranslator::TranslateArithmRI(insn::I i, asmjit::x86::Inst::Id asmop, i32 zeroval)
{
	std::array vrs = {vreg_gpr[i.rs1()]};
	auto vrd = vreg_gpr[i.rd()];
	if (!vrd) {
		return;
	}
	ra->AllocOp(std::array{vrd}, vrs);
	if (!vrs[0]) { // Constfold
		cg->j.mov(vrd->GetPReg(), zeroval);
	} else {
		if (vrd->GetPReg() != vrs[0]->GetPReg()) {
			cg->j.mov(vrd->GetPReg(), vrs[0]->GetPReg());
		}
		cg->j.emit(asmop, ToOperand(vrd), asmjit::imm(i.imm()));
	}
}

void QuickTranslator::TranslateShiftI(insn::IS i, asmjit::x86::Inst::Id asmop)
{
	std::array vrs = {vreg_gpr[i.rs1()]};
	auto vrd = vreg_gpr[i.rd()];
	if (!vrd) {
		return;
	}
	ra->AllocOp(std::array{vrd}, vrs);
	if (!vrs[0]) { // Constfold
		cg->j.mov(vrd->GetPReg(), 0);
	} else {
		if (vrd->GetPReg() != vrs[0]->GetPReg()) {
			cg->j.mov(vrd->GetPReg(), vrs[0]->GetPReg());
		}
		cg->j.emit(asmop, ToOperand(vrd), asmjit::imm(i.imm()));
	}
}

asmjit::x86::Mem QuickTranslator::CreateMemOp(RegAlloc::VReg *base, u32 offs, u8 sz)
{
	if (ra->mem_base) {
		auto mem_base = ra->mem_base->GetPReg();
		if (base) {
			return asmjit::x86::ptr(mem_base, base->GetPReg(), 0, offs, sz);
		} else {
			return asmjit::x86::ptr(mem_base, offs, sz);
		}
	} else {
		if (base) {
			return asmjit::x86::ptr(base->GetPReg(), offs, sz);
		} else {
			return asmjit::x86::ptr(offs, sz);
		}
	}
}

void QuickTranslator::TranslateInsn()
{
	auto *insn_ptr = (u32 *)mmu::g2h(insn_ip);

	using decoder = insn::Decoder<QuickTranslator>;
	(this->*decoder::Decode(insn_ptr))(insn_ptr);
}

#define TRANSLATOR(name)                                                                                     \
	void QuickTranslator::H_##name(void *insn)                                                           \
	{                                                                                                    \
		insn::Insn_##name i{*(u32 *)insn};                                                           \
		if (log_qjit.enabled()) {                                                                    \
			std::stringstream ss;                                                                \
			ss << i;                                                                             \
			const auto &res = ss.str();                                                          \
			log_qjit("      %08x: %-8s    %s", insn_ip, #name, res.c_str());                     \
		}                                                                                            \
		static constexpr auto flags = decltype(i)::flags;                                            \
		if constexpr (flags & insn::Flags::Branch || flags & insn::Flags::Trap ||                    \
			      (flags & insn::Flags::MayTrap && config::unsafe_traps)) {                      \
			cg->j.mov(vreg_ip->GetSpill(), insn_ip);                                             \
		}                                                                                            \
		Impl_##name(i);                                                                              \
		if constexpr (flags & insn::Flags::Branch || flags & insn::Flags::Trap) {                    \
			control = QuickTranslator::Control::BRANCH;                                          \
		}                                                                                            \
		insn_ip += 4;                                                                                \
	}                                                                                                    \
	ALWAYS_INLINE void QuickTranslator::Impl_##name(insn::Insn_##name i)

#define TRANSLATOR_TOHELPER(name)                                                                            \
	TRANSLATOR(name)                                                                                     \
	{                                                                                                    \
		std::array<asmjit::Operand, 3> call_ops = {asmjit::imm((uintptr_t)HelperOp_##name),          \
							   ra->state_base->GetPReg(), asmjit::imm(i.raw)};   \
		cg->Call(call_ops.data(), call_ops.size());                                                  \
	}

#define TRANSLATOR_BRCC(name, cc)                                                                            \
	TRANSLATOR(name)                                                                                     \
	{                                                                                                    \
		TranslateBranchCC(insn::B{i.raw}, asmjit::x86::CondCode::cc);                                \
	}
#define TRANSLATOR_SetCCR(name, cc)                                                                          \
	TRANSLATOR(name)                                                                                     \
	{                                                                                                    \
		TranslateSetCCR(i, asmjit::x86::CondCode::cc);                                               \
	}
#define TRANSLATOR_SetCCI(name, cc)                                                                          \
	TRANSLATOR(name)                                                                                     \
	{                                                                                                    \
		TranslateSetCCI(i, asmjit::x86::CondCode::cc);                                               \
	}
#define TRANSLATOR_ArithmRI(name, asmop, zeroval)                                                            \
	TRANSLATOR(name)                                                                                     \
	{                                                                                                    \
		TranslateArithmRI(i, asmop, zeroval);                                                        \
	}
#define TRANSLATOR_ShiftI(name, asmop)                                                                       \
	TRANSLATOR(name)                                                                                     \
	{                                                                                                    \
		TranslateShiftI(i, asmop);                                                                   \
	}
#define TRANSLATOR_ArithmRR_(name, asmop, force_cx)                                                          \
	TRANSLATOR(name)                                                                                     \
	{                                                                                                    \
		TranslateArithmRR(i, asmop, force_cx);                                                       \
	}
#define TRANSLATOR_ArithmRR(name, asmop) TRANSLATOR_ArithmRR_(name, asmop, false)
#define TRANSLATOR_ShiftR(name, asmop) TRANSLATOR_ArithmRR_(name, asmop, true)

#define TRANSLATOR_Store(name, sz)                                                                           \
	TRANSLATOR(name)                                                                                     \
	{                                                                                                    \
		std::array vrs = {vreg_gpr[i.rs1()], vreg_gpr[i.rs2()]};                                     \
		ra->AllocOp(no_regs(), vrs, config::unsafe_traps);                                           \
		auto mem = CreateMemOp(vrs[0], i.imm(), sz / 8);                                             \
		if (vrs[1]) {                                                                                \
			cg->j.mov(mem, vrs[1]->GetPReg().r##sz());                                           \
		} else {                                                                                     \
			cg->j.mov(mem, asmjit::imm(0));                                                      \
		}                                                                                            \
	}

#define TRANSLATOR_Load(name, sz, movop)                                                                     \
	TRANSLATOR(name)                                                                                     \
	{                                                                                                    \
		std::array vrs = {vreg_gpr[i.rs1()]};                                                        \
		auto vrd = vreg_gpr[i.rd()];                                                                 \
		ra->AllocOp(std::array{vrd}, vrs, config::unsafe_traps);                                     \
		auto mem = CreateMemOp(vrs[0], i.imm(), sz / 8);                                             \
		if (vrd) {                                                                                   \
			cg->j.movop(vrd->GetPReg().r32(), mem);                                              \
		} else {                                                                                     \
			cg->j.movop(asmjit::x86::gpq(Codegen::TMP1C), mem);                                  \
		}                                                                                            \
	}

TRANSLATOR_TOHELPER(ill);
TRANSLATOR(lui)
{
	if (!i.rd()) {
		return;
	}
	auto vrd = vreg_gpr[i.rd()];
	ra->AllocOp(std::array{vrd}, no_regs());
	cg->j.mov(vrd->GetPReg(), i.imm());
}
TRANSLATOR(auipc)
{
	if (!i.rd()) {
		return;
	}
	auto vrd = vreg_gpr[i.rd()];
	ra->AllocOp(std::array{vrd}, no_regs());
	cg->j.mov(vrd->GetPReg(), i.imm() + insn_ip);
}
TRANSLATOR(jal)
{
	// TODO: check alignment
	auto vrd = vreg_gpr[i.rd()];
	ra->AllocOp(std::array{vrd}, no_regs());
	if (i.rd()) {
		cg->j.mov(vrd->GetPReg(), insn_ip + 4);
	}
	cg->BranchTBDir(insn_ip + i.imm());
}
TRANSLATOR(jalr)
{
	// TODO: check alignment
	std::array vrs = {vreg_gpr[i.rs1()]};
	auto vrd = vreg_gpr[i.rd()];
	ra->AllocOp(std::array{vrd}, vrs);

	auto tmp1 = asmjit::x86::gpd(Codegen::TMP1C);
	cg->j.mov(tmp1, i.imm());
	if (i.rs1()) {
		cg->j.add(tmp1, vrs[0]->GetPReg());
	}
	cg->j.and_(tmp1, ~(u32)1);
	if (i.rd()) {
		cg->j.mov(vrd->GetPReg(), insn_ip + 4);
	}

	cg->BranchTBInd(tmp1);
}

TRANSLATOR_BRCC(beq, kE);
TRANSLATOR_BRCC(bne, kNE);
TRANSLATOR_BRCC(blt, kL);
TRANSLATOR_BRCC(bge, kGE);
TRANSLATOR_BRCC(bltu, kB);
TRANSLATOR_BRCC(bgeu, kAE);
TRANSLATOR_Load(lb, 8, movsx);
TRANSLATOR_Load(lh, 16, movsx);
TRANSLATOR_Load(lw, 32, mov);
TRANSLATOR_Load(lbu, 8, movzx);
TRANSLATOR_Load(lhu, 16, movzx);
TRANSLATOR_Store(sb, 8);
TRANSLATOR_Store(sh, 16);
TRANSLATOR_Store(sw, 32);
TRANSLATOR_ArithmRI(addi, asmjit::x86::Inst::kIdAdd, i.imm());
TRANSLATOR_SetCCI(slti, kL);
TRANSLATOR_SetCCI(sltiu, kB);
TRANSLATOR_ArithmRI(xori, asmjit::x86::Inst::kIdXor, i.imm());
TRANSLATOR_ArithmRI(ori, asmjit::x86::Inst::kIdOr, i.imm());
TRANSLATOR_ArithmRI(andi, asmjit::x86::Inst::kIdAnd, 0);
TRANSLATOR_ShiftI(slli, asmjit::x86::Inst::kIdShl);
TRANSLATOR_ShiftI(srai, asmjit::x86::Inst::kIdSar);
TRANSLATOR_ShiftI(srli, asmjit::x86::Inst::kIdShr);
TRANSLATOR_ArithmRR(sub, asmjit::x86::Inst::kIdSub);
TRANSLATOR_ArithmRR(add, asmjit::x86::Inst::kIdAdd);
TRANSLATOR_ShiftR(sll, asmjit::x86::Inst::kIdShl);
TRANSLATOR_SetCCR(slt, kL);
TRANSLATOR_SetCCR(sltu, kB);
TRANSLATOR_ArithmRR(xor, asmjit::x86::Inst::kIdXor);
TRANSLATOR_ShiftR(sra, asmjit::x86::Inst::kIdSar);
TRANSLATOR_ShiftR(srl, asmjit::x86::Inst::kIdShr);
TRANSLATOR_ArithmRR(or, asmjit::x86::Inst::kIdOr);
TRANSLATOR_ArithmRR(and, asmjit::x86::Inst::kIdAnd);
TRANSLATOR_TOHELPER(fence);
TRANSLATOR_TOHELPER(fencei);
TRANSLATOR_TOHELPER(ecall);
TRANSLATOR_TOHELPER(ebreak);

TRANSLATOR_TOHELPER(lrw);
TRANSLATOR_TOHELPER(scw);
TRANSLATOR_TOHELPER(amoswapw);
TRANSLATOR_TOHELPER(amoaddw);
TRANSLATOR_TOHELPER(amoxorw);
TRANSLATOR_TOHELPER(amoandw);
TRANSLATOR_TOHELPER(amoorw);
TRANSLATOR_TOHELPER(amominw);
TRANSLATOR_TOHELPER(amomaxw);
TRANSLATOR_TOHELPER(amominuw);
TRANSLATOR_TOHELPER(amomaxuw);

} // namespace dbt::qjit::rv32
