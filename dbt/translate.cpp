#include "dbt/translate.h"
#include "dbt/arena.h"
#include "dbt/core.h"
#include "dbt/execute.h"
#include "dbt/rv32i_decode.h"
#include "dbt/rv32i_runtime.h"
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <ostream>
#include <sys/types.h>
#include <type_traits>

namespace dbt
{

#define __asm_reg(reg) register u64 reg __asm(#reg)

HELPER uintptr_t enter_tcache(rv32i::CPUState *state, void *tc_ptr)
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

HELPER void *helper_tcache_lookup(rv32i::CPUState *state, TBlock *tb)
{
	auto *found = tcache::Lookup(state->ip);
	if (likely(found)) {
		return translator::Codegen::TBLinker::GetEntrypoint(found);
	}
	return translator::Codegen::TBLinker::GetExitpoint(tb);
}

HELPER void helper_raise()
{
	RaiseTrap();
}

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

std::array<TBlock *, 1u << tcache::JMP_CACHE_BITS> tcache::jmp_cache{};
tcache::MapType tcache::tcache_map{};
MemArena tcache::code_pool{};
MemArena tcache::tb_pool{};

void tcache::Init()
{
	jmp_cache.fill(nullptr);
	tcache_map.clear();
	tb_pool.Init(TB_POOL_SIZE, PROT_READ | PROT_WRITE);
	code_pool.Init(CODE_POOL_SIZE, PROT_READ | PROT_WRITE | PROT_EXEC);
}

void tcache::Destroy()
{
	jmp_cache.fill(nullptr);
	tcache_map.clear();
	tb_pool.Destroy();
	code_pool.Destroy();
}

void tcache::Invalidate()
{
	jmp_cache.fill(nullptr);
	tcache_map.clear();
	tb_pool.Reset();
	code_pool.Reset();
}

void tcache::Insert(TBlock *tb)
{
	tcache_map.insert({tb->ip, tb});
	jmp_cache[jmp_hash(tb->ip)] = tb;
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

namespace translator
{

Codegen::Codegen()
{
	if (jcode.init(jrt.environment())) {
		Panic();
	}
	jcode.attach(jasm._emitter());
	to_epilogue = jasm.newLabel();
	for (auto &link : branch_links) {
		link = jasm.newLabel();
	}
	jasm.setErrorHandler(&jerr);
	jcode.setErrorHandler(&jerr);
}

void Codegen::SetupRA(RegAlloc *ra)
{
	ra->fixed.Set(RegAlloc::PReg(asmjit::x86::Gp::kIdBp));
	ra->fixed.Set(RegAlloc::PReg(TR_RREG));
	ra->fixed.Set(RegAlloc::PReg(TR_TMP_CX));
	ra->fixed.Set(RegAlloc::PReg(TR_TMP3));

	ra->state_base =
	    ra->AllocVRegFixed("state", RegAlloc::VReg::Type::I64, RegAlloc::PReg(asmjit::x86::Gp::kIdBx));
	ra->frame_base =
	    ra->AllocVRegFixed("frame", RegAlloc::VReg::Type::I64, RegAlloc::PReg(asmjit::x86::Gp::kIdSp));
	ra->mem_base =
	    ra->AllocVRegFixed("memory", RegAlloc::VReg::Type::I64, RegAlloc::PReg(asmjit::x86::Gp::kIdR12));
}

void Codegen::SetBranchLinks()
{
	auto *ctx = Context::Current();
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
	auto ctx = Context::Current();
	static_assert(TB_PROLOGUE_SZ == 7);
	jasm.long_().sub(asmjit::x86::regs::rsp, ctx->ra.frame_size + 8);
}

void Codegen::Epilogue()
{
	auto ctx = Context::Current();
	Bind(to_epilogue);
	jasm.long_().add(asmjit::x86::regs::rsp, ctx->ra.frame_size + 8);

	auto rreg = asmjit::x86::gpq(TR_RREG);
	auto rtmp = asmjit::x86::gpq(TR_TMP_CX);

	auto le = jcode.labelEntry(branch_links[0]);
	if (!le->isBound()) { // no linked branches in TB
		jasm.mov(rreg, (uintptr_t)ctx->tb);
	} else {
		jasm.mov(rtmp, (uintptr_t)ctx->tb);
		jasm.or_(rreg, rtmp);
	}
	jasm.ret();
}

void Codegen::EmitCode()
{
	auto *tb = Context::Current()->tb;
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

void Codegen::Call(asmjit::Operand const *args, u8 nargs)
{
	Context::Current()->ra.CallOp(); // may spill, but not modify pregs in args

	auto inst = asmjit::BaseInst(asmjit::x86::Inst::kIdMov);
	asmjit::Operand ops[2];
	switch (nargs) {
	case 4:
		ops[0] = asmjit::x86::regs::rdx;
		ops[1] = args[3];
		jasm.emitInst(inst, ops, 2);
	case 3:
		ops[0] = asmjit::x86::regs::rsi;
		ops[1] = args[2];
		jasm.emitInst(inst, ops, 2);
	case 2:
		ops[0] = asmjit::x86::regs::rdi;
		ops[1] = args[1];
		jasm.emitInst(inst, ops, 2);
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
	jasm.emitInst(inst, &callee, 1);
}

void Codegen::BranchTBDir(u32 ip, u8 no, bool pre_epilogue)
{
	auto ctx = Context::Current();

	ctx->tb->branches[no].ip = ip;

	ctx->ra.BBEnd();
	jasm.bind(branch_links[no]);

#ifdef USE_REL_BRANCH_SLOT
	static_assert(TBLinker::BRANCH_INSN_SLOT_OFFS == 1);
	static_assert(TBLinker::BRANCH_SLOT_RESET == 0);
	auto dummy = jasm.newLabel();
	jasm.long_().jmp(dummy);
	jasm.bind(dummy);
#else
	static_assert(TBLinker::BRANCH_INSN_SLOT_OFFS == 2);
	static_assert(TBLinker::BRANCH_SLOT_RESET == 10);
	jasm.long_().mov(asmjit::x86::gpq(TR_RREG), 0);
	jasm.jmp(asmjit::x86::gpq(TR_RREG));
#endif

	jasm.mov(ctx->vreg_ip->GetSpill(), ip);
	jasm.mov(asmjit::x86::gpd(TR_RREG), TBlock::TaggedPtr(nullptr, no).getRaw());
	if (!pre_epilogue) {
		jasm.jmp(to_epilogue);
	}
}

void Codegen::x86Cmp(asmjit::x86::CondCode *cc, asmjit::Operand lhs, asmjit::Operand rhs)
{
	assert(lhs.isRegOrMem() || rhs.isRegOrMem());
	if (!lhs.isRegOrMem()) {
		*cc = asmjit::x86::reverseCond(*cc);
		std::swap(lhs, rhs);
	}
	jasm.emit(asmjit::x86::Inst::kIdCmp, lhs, rhs);
}

void Codegen::SetCC(asmjit::x86::CondCode cc, asmjit::Operand rd, asmjit::Operand lhs, asmjit::Operand rhs)
{
	x86Cmp(&cc, lhs, rhs);
	jasm.emit(asmjit::x86::Inst::setccFromCond(cc), rd);
	assert(rd.isPhysReg());
	auto prd = rd.as<asmjit::x86::Gp>();
	jasm.emit(asmjit::x86::Inst::kIdMovzx, prd, prd.r8());
}

void Codegen::BranchCC(asmjit::Label taken, asmjit::x86::CondCode cc, asmjit::Operand lhs,
		       asmjit::Operand rhs)
{
	auto *ctx = translator::Context::Current();
	ctx->ra.BBEnd();
	x86Cmp(&cc, lhs, rhs);
	jasm.emit(asmjit::x86::Inst::jccFromCond(cc), taken);
}

void Codegen::Bind(asmjit::Label l)
{
	auto *ctx = translator::Context::Current();
	ctx->ra.BBEnd();
	jasm.bind(l);
}

void Codegen::BranchTBInd(asmjit::x86::Gpd target)
{
	auto *ctx = Context::Current();
	assert(target.isPhysReg());

	ctx->ra.BBEnd();
#if 1
	// Inlined jmp_cache lookup
	auto slowpath = jasm.newLabel();
	auto tmp0 = asmjit::x86::gpq(translator::Codegen::TR_RREG);
	auto tmp1 = asmjit::x86::gpq(translator::Codegen::TR_TMP3);
	jasm.mov(tmp1.r64(), (uintptr_t)tcache::jmp_cache.data());
	jasm.imul(tmp0.r32(), target.r32(), tcache::JMP_HASH_MULT);
	jasm.shr(tmp0.r32(), 32 - tcache::JMP_CACHE_BITS);
	jasm.mov(tmp0.r64(), asmjit::x86::ptr(tmp1.r64(), tmp0.r64(), 3));
	jasm.test(tmp0.r64(), tmp0.r64());
	jasm.je(slowpath);
	jasm.cmp(target.r32(), asmjit::x86::ptr(tmp0.r64(), offsetof(dbt::TBlock, ip)));
	jasm.jne(slowpath);
	jasm.mov(tmp0.r64(), asmjit::x86::ptr(tmp0.r64(), offsetof(dbt::TBlock, tcode) +
							      offsetof(dbt::TBlock::TCode, ptr)));
	jasm.add(tmp0.r64(), translator::Codegen::TB_PROLOGUE_SZ);
	jasm.jmp(tmp0.r64());
	jasm.bind(slowpath);
#endif

	ctx->cg.jasm.mov(ctx->vreg_ip->GetSpill(), target);
	std::array<asmjit::Operand, 3> call_ops = {asmjit::imm((uintptr_t)helper_tcache_lookup),
						   ctx->ra.state_base->GetPReg(),
						   asmjit::imm((uintptr_t)ctx->tb)};
	ctx->cg.Call(call_ops.data(), call_ops.size());
	auto rreg = asmjit::x86::gpq(translator::Codegen::TR_RREG);
	ctx->ra.BBEnd();
	ctx->cg.jasm.jmp(rreg);
}

Context *Context::current{nullptr};

Context::Context()
{
	assert(!current);
	current = this;
	cg.SetupRA(&ra);

	vreg_gpr[0] = nullptr;
	for (u8 i = 1; i < vreg_gpr.size(); ++i) {
		char const *name = rv32i::insn::GRPToName(i);
		u16 offs = offsetof(rv32i::CPUState, gpr) + 4 * i;
		vreg_gpr[i] = ra.AllocVRegMem(name, RegAlloc::VReg::Type::I32, ra.state_base, offs);
	}
	vreg_ip =
	    ra.AllocVRegMem("ip", RegAlloc::VReg::Type::I32, ra.state_base, offsetof(rv32i::CPUState, ip));
}

Context::~Context()
{
	current = nullptr;
}

TBlock *Translate(rv32i::CPUState *state, u32 ip)
{
	// TODO: check if ip is mapped (MMU)
	Context ctx;
	ctx.tb = tcache::AllocateTBlock();
	if (ctx.tb == nullptr) {
		Panic();
	}
	ctx.tb->ip = ip;
	ctx.insn_ip = ip;

	log_bt() << "Translating [" << std::hex << ip << "]:";
	ctx.ra.Prologue();
	ctx.cg.Prologue();

	while (true) {
		ctx.TranslateInsn();
		ctx.tb->size++;
		if (ctx.control != Context::Control::NEXT) {
			break;
		}
		if (ctx.tb->size == TB_MAX_INSNS) {
			ctx.control = Context::Control::TB_OVF;
			break;
		}
	}

	ctx.cg.Epilogue();

	log_bt() << "Emitting    [" << std::hex << ip << "]:";
	ctx.cg.EmitCode();
	ctx.cg.SetBranchLinks();
#ifndef LOG_TRACE_ENABLE
	ctx.tb->Dump();
#endif
	return ctx.tb;
}
} // namespace translator
} // namespace dbt
