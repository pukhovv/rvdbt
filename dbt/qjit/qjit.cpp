#include "dbt/qjit/qjit.h"
#include "dbt/execute.h"
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <ostream>
#include <sys/types.h>
#include <type_traits>

namespace dbt
{
namespace qjit
{

#define __asm_reg(reg) register u64 reg __asm(#reg)

HELPER uintptr_t enter_tcache(CPUState *state, void *tc_ptr)
{
	void *vmem = mmu::base;
	__asm_reg(rax) = (u64)tc_ptr;
	__asm_reg(rbx) = (u64)state;
	__asm_reg(r12) = (u64)vmem;
	__asm volatile("callq	*%%rax\n\t"
		       : "=r"(rax)
		       : "r"(rax), "r"(rbx), "r"(r12)
		       : "memory", "r13", "r14", "r15");
	return rax;
}

#define __asm_reg(reg) register u64 reg __asm(#reg)

HELPER void *helper_tcache_lookup(CPUState *state, TBlock *tb)
{
	auto *found = tcache::Lookup(state->ip);
	if (likely(found)) {
		tcache::OnBrind(found);
		return qjit::Codegen::TBLinker::GetEntrypoint(found);
	}
	return qjit::Codegen::TBLinker::GetExitpoint(tb);
}

HELPER void helper_raise()
{
	RaiseTrap();
}

Codegen::Codegen()
{
	if (jcode.init(jrt.environment())) {
		Panic();
	}
	jcode.attach(j._emitter());
	to_epilogue = j.newLabel();
	for (auto &link : branch_links) {
		link = j.newLabel();
	}
	j.setErrorHandler(&jerr);
	jcode.setErrorHandler(&jerr);
}

void Codegen::SetupCtx(QuickJIT *ctx_)
{
	ctx = ctx_;
	auto ra = ctx->ra;
	ra->fixed.Set(RegAlloc::PReg(asmjit::x86::Gp::kIdBp));
	ra->fixed.Set(RegAlloc::PReg(TMP1C));
	ra->fixed.Set(RegAlloc::PReg(TMP2));
	ra->fixed.Set(RegAlloc::PReg(TMP3));

	ra->state_base = ra->AllocVRegFixed("state", RegAlloc::VReg::Type::I64, RegAlloc::PReg(STATE));
	ra->frame_base = ra->AllocVRegFixed("frame", RegAlloc::VReg::Type::I64, RegAlloc::PReg(SP));
	if (mmu::base) {
		ra->mem_base = ra->AllocVRegFixed("memory", RegAlloc::VReg::Type::I64,
						  RegAlloc::PReg(asmjit::x86::Gp::kIdR12));
	}
#ifdef CONFIG_USE_STATEMAPS
	ra->state_map = ra->AllocVRegFixed("statemap", RegAlloc::VReg::Type::I64,
					   RegAlloc::PReg(asmjit::x86::Gp::kIdR13));
#endif
}

void Codegen::ResetBranchLinks()
{
	ctx->tb->epilogue_offs = jcode.labelEntry(to_epilogue)->offset();

	for (size_t i = 0; i < branch_links.size(); ++i) {
		auto *le = jcode.labelEntry(branch_links[i]);
		if (le->isBound()) {
			TBLinker::InitBranch(ctx->tb, i, le->offset());
		}
	}
}

void Codegen::Prologue()
{
	tcache::OnTranslate(ctx->tb);
	static_assert(TB_PROLOGUE_SZ == 7);
	j.long_().sub(asmjit::x86::regs::rsp, ctx->ra->frame_size + 8);
}

void Codegen::Epilogue()
{
	Bind(to_epilogue);
	j.long_().add(asmjit::x86::regs::rsp, ctx->ra->frame_size + 8);

	auto rreg = asmjit::x86::rax;
	auto rtmp = asmjit::x86::gpq(TMP1C);

	auto le = jcode.labelEntry(branch_links[0]);
	if (!le->isBound()) { // no linked branches in TB
		j.mov(rreg, (uintptr_t)ctx->tb);
	} else {
		j.mov(rtmp, (uintptr_t)ctx->tb);
		j.or_(rreg, rtmp);
	}
	j.ret();
}

void Codegen::EmitCode()
{
	auto *tb = ctx->tb;
	jcode.flatten();
	jcode.resolveUnresolvedLinks();

	size_t jit_sz = jcode.codeSize();
	tb->tcode.size = jit_sz;
	tb->tcode.ptr = tcache::AllocateCode(jit_sz, 16);
	if (tb->tcode.ptr == nullptr) {
		Panic();
	}

	jcode.relocateToBase((uintptr_t)tb->tcode.ptr);
	if (jit_sz < jcode.codeSize()) {
		Panic();
	}
	jcode.copyFlattenedData(tb->tcode.ptr, tb->tcode.size);
	tb->tcode.size = jcode.codeSize();
}

#ifdef CONFIG_USE_STATEMAPS
void QuickJIT::CreateStateMap()
{
	StateMap sm{};
	u8 map_idx = 0;
	for (RegAlloc::PReg p = 0; p < RegAlloc::N_PREGS; ++p) {
		if (ra->fixed.Test(p)) {
			continue;
		}
		auto *v = ra->p2v[p];
		if (!v || !v->has_statemap) {
			continue;
		}
		uintptr_t v_idx = ((uintptr_t)v - (uintptr_t)vreg_gpr.data()) / sizeof(*v);
		if (v_idx < vreg_gpr.size()) {
			sm.Set(map_idx, v_idx);
		}
	}

	if (active_sm.first && active_sm.second.data == sm.data) {
		return;
	}
	active_sm = {true, sm};
	cg.j.mov(ra->state_map->GetPReg(), asmjit::imm(sm.data));
}
#endif

void Codegen::Call(asmjit::Operand const *args, u8 nargs)
{
	ctx->ra->CallOp(); // may spill, but not modify pregs in args

	auto inst = asmjit::BaseInst(asmjit::x86::Inst::kIdMov);
	asmjit::Operand ops[2];
	switch (nargs) {
	case 4:
		ops[0] = asmjit::x86::regs::rdx;
		ops[1] = args[3];
		j.emitInst(inst, ops, 2);
	case 3:
		ops[0] = asmjit::x86::regs::rsi;
		ops[1] = args[2];
		j.emitInst(inst, ops, 2);
	case 2:
		ops[0] = asmjit::x86::regs::rdi;
		ops[1] = args[1];
		j.emitInst(inst, ops, 2);
	case 1:
		break;
	default:
		assert(0);
	}

	auto &callee = args[0];
	if (!callee.isPhysReg()) {
		assert(callee.as<asmjit::x86::Gp>().id() == asmjit::x86::Gp::kIdAx);
	}
	inst = asmjit::BaseInst(asmjit::x86::Inst::kIdCall);
	j.emitInst(inst, &callee, 1);
}

void Codegen::BranchTBDir(u32 ip, u8 no, bool pre_epilogue)
{
	tcache::OnTranslateBr(ctx->tb, ip);
	ctx->tb->branches[no].ip = ip;

	ctx->ra->BBEnd();
	j.bind(branch_links[no]);

#ifdef USE_REL_BRANCH_SLOT
	static_assert(TBLinker::BRANCH_INSN_SLOT_OFFS == 1);
	static_assert(TBLinker::BRANCH_SLOT_RESET == 0);
	auto dummy = j.newLabel();
	j.long_().jmp(dummy);
	j.bind(dummy);
#else
	static_assert(TBLinker::BRANCH_INSN_SLOT_OFFS == 2);
	static_assert(TBLinker::BRANCH_SLOT_RESET == 10);
	j.long_().mov(asmjit::x86::gpq(TR_RREG), 0);
	j.jmp(asmjit::x86::gpq(TR_RREG));
#endif

	j.mov(ctx->vreg_ip->GetSpill(), ip);
	j.mov(asmjit::x86::eax, TBlock::TaggedPtr(nullptr, no).getRaw());
	if (!pre_epilogue) {
		j.jmp(to_epilogue);
	}
}

void Codegen::x86Cmp(asmjit::x86::CondCode *cc, asmjit::Operand lhs, asmjit::Operand rhs)
{
	assert(lhs.isRegOrMem() || rhs.isRegOrMem());
	if (!lhs.isRegOrMem()) {
		*cc = asmjit::x86::reverseCond(*cc);
		std::swap(lhs, rhs);
	}
	j.emit(asmjit::x86::Inst::kIdCmp, lhs, rhs);
}

void Codegen::SetCC(asmjit::x86::CondCode cc, asmjit::Operand rd, asmjit::Operand lhs, asmjit::Operand rhs)
{
	x86Cmp(&cc, lhs, rhs);
	j.emit(asmjit::x86::Inst::setccFromCond(cc), rd);
	assert(rd.isPhysReg());
	auto prd = rd.as<asmjit::x86::Gp>();
	j.emit(asmjit::x86::Inst::kIdMovzx, prd, prd.r8());
}

void Codegen::BranchCC(asmjit::Label taken, asmjit::x86::CondCode cc, asmjit::Operand lhs,
		       asmjit::Operand rhs)
{
	ctx->ra->BBEnd();
	x86Cmp(&cc, lhs, rhs);
	j.emit(asmjit::x86::Inst::jccFromCond(cc), taken);
}

void Codegen::Bind(asmjit::Label l)
{
	ctx->ra->BBEnd();
	j.bind(l);
}

void Codegen::BranchTBInd(asmjit::Operand target)
{
	assert(target.isPhysReg());
	auto ptgt = target.as<asmjit::x86::Gp>();

	ctx->ra->BBEnd();
	auto slowpath = j.newLabel();
	{
		// Inlined jmp_cache lookup
		auto tmp0 = asmjit::x86::gpq(TMP2);
		auto tmp1 = asmjit::x86::gpq(TMP3);
		j.mov(tmp1.r64(), (uintptr_t)tcache::jmp_cache_brind.data());
		j.imul(tmp0.r32(), ptgt.r32(), tcache::JMP_HASH_MULT);
		j.shr(tmp0.r32(), 32 - tcache::JMP_CACHE_BITS);
		j.mov(tmp0.r64(), asmjit::x86::ptr(tmp1.r64(), tmp0.r64(), 3));
		j.test(tmp0.r64(), tmp0.r64());
		j.je(slowpath);

		j.cmp(ptgt.r32(), asmjit::x86::ptr(tmp0.r64(), offsetof(dbt::TBlock, ip)));
		j.jne(slowpath);

		j.mov(tmp0.r64(), asmjit::x86::ptr(tmp0.r64(), offsetof(dbt::TBlock, tcode) +
								   offsetof(dbt::TBlock::TCode, ptr)));
		j.add(tmp0.r64(), qjit::Codegen::TB_PROLOGUE_SZ);
		j.jmp(tmp0.r64());
	}

	j.bind(slowpath);
	j.mov(ctx->vreg_ip->GetSpill(), ptgt.r32());
	std::array<asmjit::Operand, 3> call_ops = {asmjit::imm((uintptr_t)helper_tcache_lookup),
						   ctx->ra->state_base->GetPReg(),
						   asmjit::imm((uintptr_t)ctx->tb)};
	Call(call_ops.data(), call_ops.size());
	ctx->ra->BBEnd();
	j.jmp(asmjit::x86::eax);
}

QuickJIT::QuickJIT()
{
	ra = new RegAlloc();
	cg = new Codegen();
	cg->SetupCtx(this);
	ra->SetupCtx(this);
}

QuickJIT::~QuickJIT()
{
	delete ra;
	delete cg;
}
} // namespace qjit
} // namespace dbt