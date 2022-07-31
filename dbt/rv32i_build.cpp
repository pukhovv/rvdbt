#include "dbt/common.h"
#include "dbt/core.h"
#include "dbt/rv32i_decode.h"
#include "dbt/rv32i_runtime.h"
#include "dbt/translate.h"
#include <cstdint>
#include <cstdlib>
#include <type_traits>

namespace dbt::rv32i
{

#define TRANSLATOR(name)                                                                                     \
	static ALWAYS_INLINE void TranslateOp_##name(translator::Context &ctx, insn::Insn_##name i);         \
	static void TranslateWrapper_##name(translator::Context &ctx, u32 insn_raw)                          \
	{                                                                                                    \
		insn::Insn_##name i{insn_raw};                                                               \
		log_bt() << std::hex << "\t" << ctx.insn_ip << " " #name " \t " << i;                        \
		static constexpr auto flags = decltype(i)::flags;                                            \
		if constexpr ((flags & insn::Flags::MayTrap) || flags & insn::Flags::Branch) {               \
			ctx.cg.jasm.mov(ctx.vreg_ip->GetSpill(), ctx.insn_ip);                               \
		}                                                                                            \
		TranslateOp_##name(ctx, i);                                                                  \
		if constexpr (flags & insn::Flags::Branch) {                                                 \
			ctx.control = translator::Context::Control::BRANCH;                                  \
		}                                                                                            \
		ctx.insn_ip += 4;                                                                            \
	}                                                                                                    \
	static ALWAYS_INLINE void TranslateOp_##name(translator::Context &ctx, insn::Insn_##name i)

#define TRANSLATOR_TOHELPER(name)                                                                            \
	TRANSLATOR(name)                                                                                     \
	{                                                                                                    \
		std::array<asmjit::Operand, 3> call_ops = {                                                  \
		    asmjit::imm((uintptr_t)rv32i::interp::HelperOp_##name), ctx.ra.state_base->GetPReg(),    \
		    asmjit::imm(i.raw)};                                                                     \
		ctx.cg.Call(call_ops.data(), call_ops.size());                                               \
	}

static asmjit::Operand ToOperand(translator::RegAlloc::VReg *vreg)
{
	if (!vreg) {
		return asmjit::imm(0);
	}
	switch (vreg->loc) {
	case translator::RegAlloc::VReg::Loc::REG:
		return vreg->GetPReg();
	case translator::RegAlloc::VReg::Loc::MEM:
		return vreg->GetSpill();
	default:
		Panic();
	}
}

static void TranslateBranchCC(translator::Context &ctx, insn::B br, asmjit::x86::CondCode cc)
{
	auto vrs1 = ctx.vreg_gpr[br.rs1()];
	auto vrs2 = ctx.vreg_gpr[br.rs2()];
	ctx.ra.AllocOp(nullptr, nullptr, vrs1, vrs2);

	auto br_taken = ctx.cg.jasm.newLabel();

	ctx.cg.BranchCC(br_taken, cc, ToOperand(vrs1), ToOperand(vrs2));
	ctx.cg.BranchTBDir(ctx.insn_ip + 4, 0);
	ctx.cg.Bind(br_taken);
	// TODO: check alignment
	ctx.cg.BranchTBDir(ctx.insn_ip + br.imm(), 1, true);
}

static void TranslateSetCCR(translator::Context &ctx, insn::R i, asmjit::x86::CondCode cc)
{
	if (!i.rd()) {
		return;
	}
	auto vrs1 = ctx.vreg_gpr[i.rs1()];
	auto vrs2 = ctx.vreg_gpr[i.rs2()];
	auto vrd = ctx.vreg_gpr[i.rd()];
	ctx.ra.AllocOp(vrd, nullptr, vrs1, vrs2);

	ctx.cg.SetCC(cc, ToOperand(vrd), ToOperand(vrs1), ToOperand(vrs2));
}

static void TranslateSetCCI(translator::Context &ctx, insn::I i, asmjit::x86::CondCode cc)
{
	if (!i.rd()) {
		return;
	}
	auto vrs1 = ctx.vreg_gpr[i.rs1()];
	auto vrd = ctx.vreg_gpr[i.rd()];
	ctx.ra.AllocOp(vrd, nullptr, vrs1, nullptr);

	ctx.cg.SetCC(cc, ToOperand(vrd), ToOperand(vrs1), asmjit::imm(i.imm()));
}

static void TranslateArithmRR(translator::Context &ctx, insn::R i, asmjit::x86::Inst::Id asmop, bool force_cx)
{
	if (!i.rd()) {
		return;
	}
	auto vrs1 = ctx.vreg_gpr[i.rs1()];
	auto vrs2 = ctx.vreg_gpr[i.rs2()];
	auto vrd = ctx.vreg_gpr[i.rd()];
	ctx.ra.AllocOp(vrd, nullptr, vrs1, vrs2);

	asmjit::Operand rhs;
	if (i.rd() == i.rs2() || force_cx) {
		static_assert(translator::Codegen::TR_TMP_CX == asmjit::x86::Gp::kIdCx);
		auto rhs_gp = asmjit::x86::gpd(translator::Codegen::TR_TMP_CX);
		ctx.cg.jasm.mov(rhs_gp, vrs2->GetPReg());
		rhs = rhs_gp;
	} else {
		rhs = ToOperand(vrs2);
	}

	if (!vrs1) {
		ctx.cg.jasm.xor_(vrd->GetPReg(), vrd->GetPReg());
	} else if (vrd->GetPReg() != vrs1->GetPReg()) {
		ctx.cg.jasm.mov(vrd->GetPReg(), vrs1->GetPReg());
	}

	ctx.cg.jasm.emit(asmop, vrd->GetPReg(), rhs);
}

static void TranslateArithmRI(translator::Context &ctx, insn::I i, asmjit::x86::Inst::Id asmop, i32 zeroval)
{
	auto vrs1 = ctx.vreg_gpr[i.rs1()];
	auto vrd = ctx.vreg_gpr[i.rd()];
	if (!vrd) {
		return;
	}
	ctx.ra.AllocOp(vrd, nullptr, vrs1, nullptr);
	if (!vrs1) { // Constfold
		ctx.cg.jasm.mov(vrd->GetPReg(), zeroval);
	} else {
		if (vrd->GetPReg() != vrs1->GetPReg()) {
			ctx.cg.jasm.mov(vrd->GetPReg(), vrs1->GetPReg());
		}
		ctx.cg.jasm.emit(asmop, ToOperand(vrd), asmjit::imm(i.imm()));
	}
}

static void TranslateShiftI(translator::Context &ctx, insn::IS i, asmjit::x86::Inst::Id asmop)
{
	auto vrs1 = ctx.vreg_gpr[i.rs1()];
	auto vrd = ctx.vreg_gpr[i.rd()];
	if (!vrd) {
		return;
	}
	ctx.ra.AllocOp(vrd, nullptr, vrs1, nullptr);
	if (!vrs1) { // Constfold
		ctx.cg.jasm.mov(vrd->GetPReg(), 0);
	} else {
		if (vrd->GetPReg() != vrs1->GetPReg()) {
			ctx.cg.jasm.mov(vrd->GetPReg(), vrs1->GetPReg());
		}
		ctx.cg.jasm.emit(asmop, ToOperand(vrd), asmjit::imm(i.imm()));
	}
}

static inline asmjit::x86::Mem CreateMemOp(translator::Context &ctx, translator::RegAlloc::VReg *base,
					   u32 offs, u8 sz)
{
	if (ctx.ra.mem_base) {
		auto mem_base = ctx.ra.mem_base->GetPReg();
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

#define TRANSLATOR_BRCC(name, cc)                                                                            \
	TRANSLATOR(name)                                                                                     \
	{                                                                                                    \
		TranslateBranchCC(ctx, insn::B{i.raw}, asmjit::x86::CondCode::cc);                           \
	}
#define TRANSLATOR_SetCCR(name, cc)                                                                          \
	TRANSLATOR(name)                                                                                     \
	{                                                                                                    \
		TranslateSetCCR(ctx, i, asmjit::x86::CondCode::cc);                                          \
	}
#define TRANSLATOR_SetCCI(name, cc)                                                                          \
	TRANSLATOR(name)                                                                                     \
	{                                                                                                    \
		TranslateSetCCI(ctx, i, asmjit::x86::CondCode::cc);                                          \
	}
#define TRANSLATOR_ArithmRI(name, asmop, zeroval)                                                            \
	TRANSLATOR(name)                                                                                     \
	{                                                                                                    \
		TranslateArithmRI(ctx, i, asmop, zeroval);                                                   \
	}
#define TRANSLATOR_ShiftI(name, asmop)                                                                       \
	TRANSLATOR(name)                                                                                     \
	{                                                                                                    \
		TranslateShiftI(ctx, i, asmop);                                                              \
	}
#define TRANSLATOR_ArithmRR_(name, asmop, force_cx)                                                          \
	TRANSLATOR(name)                                                                                     \
	{                                                                                                    \
		TranslateArithmRR(ctx, i, asmop, force_cx);                                                  \
	}
#define TRANSLATOR_ArithmRR(name, asmop) TRANSLATOR_ArithmRR_(name, asmop, false)
#define TRANSLATOR_ShiftR(name, asmop) TRANSLATOR_ArithmRR_(name, asmop, true)

#define TRANSLATOR_Store(name, sz)                                                                           \
	TRANSLATOR(name)                                                                                     \
	{                                                                                                    \
		auto vrs1 = ctx.vreg_gpr[i.rs1()];                                                           \
		auto vrs2 = ctx.vreg_gpr[i.rs2()];                                                           \
		ctx.ra.AllocOp(nullptr, nullptr, vrs1, vrs2, true);                                          \
		auto mem = CreateMemOp(ctx, vrs1, i.imm(), sz / 8);                                          \
		if (vrs2) {                                                                                  \
			ctx.cg.jasm.mov(mem, vrs2->GetPReg().r##sz());                                       \
		} else {                                                                                     \
			ctx.cg.jasm.mov(mem, asmjit::imm(0));                                                \
		}                                                                                            \
	}

#define TRANSLATOR_Load(name, sz, movop)                                                                     \
	TRANSLATOR(name)                                                                                     \
	{                                                                                                    \
		auto vrs1 = ctx.vreg_gpr[i.rs1()];                                                           \
		auto vrd = ctx.vreg_gpr[i.rd()];                                                             \
		ctx.ra.AllocOp(vrd, nullptr, vrs1, nullptr, true);                                           \
		auto mem = CreateMemOp(ctx, vrs1, i.imm(), sz / 8);                                          \
		if (vrd) {                                                                                   \
			ctx.cg.jasm.movop(vrd->GetPReg().r##sz(), mem);                                      \
		} else {                                                                                     \
			ctx.cg.jasm.movop(asmjit::x86::gpq(translator::Codegen::TR_TMP_CX), mem);            \
		}                                                                                            \
	}

TRANSLATOR_TOHELPER(ill);
TRANSLATOR(lui)
{
	if (!i.rd()) {
		return;
	}
	auto vrd = ctx.vreg_gpr[i.rd()];
	ctx.ra.AllocOp(vrd, nullptr, nullptr, nullptr);
	ctx.cg.jasm.mov(vrd->GetPReg(), i.imm());
}
TRANSLATOR(auipc)
{
	if (!i.rd()) {
		return;
	}
	auto vrd = ctx.vreg_gpr[i.rd()];
	ctx.ra.AllocOp(vrd, nullptr, nullptr, nullptr);
	ctx.cg.jasm.mov(vrd->GetPReg(), i.imm() + ctx.insn_ip);
}
TRANSLATOR(jal)
{
	// TODO: check alignment
	auto vrd = ctx.vreg_gpr[i.rd()];
	ctx.ra.AllocOp(vrd, nullptr, nullptr, nullptr);
	if (i.rd()) {
		ctx.cg.jasm.mov(vrd->GetPReg(), ctx.insn_ip + 4);
	}
	ctx.cg.BranchTBDir(ctx.insn_ip + i.imm(), 0, true);
}

TRANSLATOR(jalr)
{
	// TODO: check alignment
	auto vrs1 = ctx.vreg_gpr[i.rs1()];
	auto vrd = ctx.vreg_gpr[i.rd()];
	ctx.ra.AllocOp(vrd, nullptr, vrs1, nullptr);

	auto tmp1 = asmjit::x86::gpd(translator::Codegen::TR_TMP_CX);
	ctx.cg.jasm.mov(tmp1, i.imm());
	if (i.rs1()) {
		ctx.cg.jasm.add(tmp1, vrs1->GetPReg());
	}
	ctx.cg.jasm.and_(tmp1, ~(u32)1);
	if (i.rd()) {
		ctx.cg.jasm.mov(vrd->GetPReg(), ctx.insn_ip + 4);
	}

	ctx.cg.BranchTBInd(tmp1);
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
TRANSLATOR_TOHELPER(ecall);
TRANSLATOR_TOHELPER(ebreak);

} // namespace dbt::rv32i

namespace dbt
{

void translator::Context::TranslateInsn()
{
	auto *insn_ptr = (u32 *)mmu::g2h(insn_ip);
	rv32i::insn::DecodeParams insn{*insn_ptr};

#define OP(name) return rv32i::TranslateWrapper_##name(*this, insn.raw);
#define OP_ILL OP(ill)
	RV32I_DECODE_SWITCH(insn)
#undef OP_ILL
#undef OP
	unreachable("");
}

} // namespace dbt
