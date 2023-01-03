#include "dbt/qjit/qcg.h"

namespace dbt::qcg
{

QEmit::QEmit(qir::Region *region)
{
	if (jcode.init(jrt.environment())) {
		Panic();
	}
	jcode.attach(j._emitter());
	j.setErrorHandler(&jerr);
	jcode.setErrorHandler(&jerr);

	u32 n_labels = region->GetNumBlocks();
	labels.reserve(n_labels);
	for (u32 i = 0; i < n_labels; ++i) {
		labels.push_back(j.newLabel());
	}
}

// TODO: return tcode span
TBlock *QEmit::EmitTBlock()
{
	auto tb = tcache::AllocateTBlock();
	if (tb == nullptr) {
		Panic();
	}

	jcode.flatten();
	jcode.resolveUnresolvedLinks();

	size_t jit_sz = jcode.codeSize();
	tb->tcode.size = jit_sz;
	tb->tcode.ptr = tcache::AllocateCode(jit_sz, 8);
	if (tb->tcode.ptr == nullptr) {
		Panic();
	}

	jcode.relocateToBase((uintptr_t)tb->tcode.ptr);
	if (jit_sz < jcode.codeSize()) {
		Panic();
	}
	jcode.copyFlattenedData(tb->tcode.ptr, tb->tcode.size);
	tb->tcode.size = jcode.codeSize();
	return tb;
}

void QEmit::DumpTBlock(TBlock *tb)
{
	if (!log_qcg.enabled()) {
		return;
	}
	auto &tcode = tb->tcode;
	size_t sz = tcode.size;
	auto p = (u8 *)tcode.ptr;

	std::array<char, 4096> buf;

	if (sz * 2 + 1 > buf.size()) {
		log_qcg("jitcode is too long for dump");
		return;
	}

	for (size_t i = 0; i < sz; ++i) {
		sprintf(&buf[2 * i], "%02x", p[i]);
	}
	log_qcg.write(buf.data());
}

static inline asmjit::x86::Gp make_gpr(qir::RegN pr, qir::VType type)
{
	switch (type) {
	case qir::VType::I8:
		return asmjit::x86::gpb(pr);
	case qir::VType::I16:
		return asmjit::x86::gpw(pr);
	case qir::VType::I32:
		return asmjit::x86::gpd(pr);
	default:
		unreachable("");
	}
}

static inline asmjit::x86::Gp make_gpr(qir::VOperand opr)
{
	return make_gpr(opr.GetPGPR(), opr.GetType());
}

static inline asmjit::Imm make_imm(qir::VOperand opr)
{
	return asmjit::imm(opr.GetConst());
}

static inline asmjit::Operand make_operand(qir::VOperand opr)
{
	if (opr.IsConst()) {
		return make_imm(opr);
	}
	return make_gpr(opr);
}

static inline asmjit::x86::CondCode make_cc(qir::CondCode cc)
{
	switch (cc) {
	case qir::CondCode::EQ:
		return asmjit::x86::CondCode::kEqual;
	case qir::CondCode::NE:
		return asmjit::x86::CondCode::kNotEqual;
	case qir::CondCode::LT:
		return asmjit::x86::CondCode::kSignedLT;
	case qir::CondCode::GE:
		return asmjit::x86::CondCode::kSignedGE;
	case qir::CondCode::LTU:
		return asmjit::x86::CondCode::kUnsignedLT;
	case qir::CondCode::GEU:
		return asmjit::x86::CondCode::kUnsignedGE;
	default:
		unreachable("");
	}
}

void QEmit::StateFill(qir::RegN p, qir::VType type, u16 offs)
{
	auto slot = asmjit::x86::ptr(RSTATE, offs);
	slot.setSize(VTypeToSize(type));
	j.mov(make_gpr(p, type), slot);
}

void QEmit::StateSpill(qir::RegN p, qir::VType type, u16 offs)
{
	auto slot = asmjit::x86::ptr(RSTATE, offs);
	slot.setSize(VTypeToSize(type));
	j.mov(slot, make_gpr(p, type));
}

void QEmit::LocFill(qir::RegN p, qir::VType type, u16 offs)
{
	auto slot = asmjit::x86::ptr(RSP, offs);
	slot.setSize(VTypeToSize(type));
	j.mov(make_gpr(p, type), slot);
}

void QEmit::LocSpill(qir::RegN p, qir::VType type, u16 offs)
{
	auto slot = asmjit::x86::ptr(RSP, offs);
	slot.setSize(VTypeToSize(type));
	j.mov(slot, make_gpr(p, type));
}

void QEmit::Emit_hcall(qir::InstHcall *ins)
{
	// args preallocated
	j.call(ins->stub);
}

void QEmit::Emit_br(qir::InstBr *ins)
{
	auto &bb_s = **bb->GetSuccs().begin();
	auto &bb_ff = *++bb->getIter();
	if (&bb_s != &bb_ff) {
		j.jmp(labels[bb_s.GetId()]);
	}
}

void QEmit::Emit_brcc(qir::InstBrcc *ins)
{
	auto sit = bb->GetSuccs().begin();
	auto &bb_t = **sit;
	auto &bb_f = **++sit;
	auto &bb_ff = *++bb->getIter();

	// constfolded
	auto vs1 = &ins->i[0];
	auto vs2 = &ins->i[1];
	auto cc = ins->cc;
	if (vs1->IsConst()) {
		std::swap(vs1, vs2);
		cc = qir::InverseCC(cc);
	}
	j.emit(asmjit::x86::Inst::kIdCmp, make_operand(*vs1), make_operand(*vs2));
	auto jcc = asmjit::x86::Inst::jccFromCond(make_cc(cc));
	j.emit(jcc, labels[bb_t.GetId()]);

	if (&bb_f != &bb_ff) {
		j.jmp(labels[bb_f.GetId()]);
	}
}

void QEmit::Emit_gbr(qir::InstGBr *ins)
{
	j.embedUInt8(0, sizeof(qjit::BranchSlot));
	auto *slot = (qjit::BranchSlot *)(j.bufferPtr() - sizeof(qjit::BranchSlot));
	slot->gip = ins->tpc.GetConst();
	slot->Reset();
}

void QEmit::Emit_gbrind(qir::InstGBrind *ins)
{
	auto ptgt = make_gpr(ins->i[0]);
	{ // TODO: force si alloc
		auto tmp_ptgt = asmjit::x86::gpq(asmjit::x86::Gp::kIdSi);
		j.mov(tmp_ptgt, ptgt);
		ptgt = tmp_ptgt;
	}

	auto slowpath = j.newLabel();
	{
		// Inlined jmp_cache lookup
		auto tmp0 = asmjit::x86::rdi;
		auto tmp1 = asmjit::x86::rdx;
		j.mov(tmp1.r64(), (uintptr_t)tcache::jmp_cache_brind.data());

		j.mov(tmp0.r32(), ptgt.r32());
		j.shr(tmp0.r32(), 2);
		j.and_(tmp0.r32(), (1ull << tcache::JMP_CACHE_BITS) - 1);

		j.mov(tmp0.r64(), asmjit::x86::ptr(tmp1.r64(), tmp0.r64(), 3));
		j.test(tmp0.r64(), tmp0.r64());
		j.je(slowpath);

		j.cmp(ptgt.r32(), asmjit::x86::ptr(tmp0.r64(), offsetof(dbt::TBlock, ip)));
		j.jne(slowpath);

		j.mov(tmp0.r64(), asmjit::x86::ptr(tmp0.r64(), offsetof(dbt::TBlock, tcode) +
								   offsetof(dbt::TBlock::TCode, ptr)));
		j.jmp(tmp0.r64());
	}

	j.bind(slowpath);

	j.mov(asmjit::x86::gpq(asmjit::x86::Gp::kIdDi), STATE);
	assert(ptgt.id() == asmjit::x86::Gp::kIdSi);
	j.call(qjit::helper_brind);
	j.jmp(asmjit::x86::rdx);
}

void QEmit::Emit_vmload(qir::InstVMLoad *ins)
{
	auto &vrd = ins->o[0];
	auto &vbase = ins->i[0];
	auto sgn = ins->sgn;

	auto prd = make_gpr(vrd);
	auto pmembase = asmjit::x86::gpq(MEMBASE);

	asmjit::x86::Mem mem;
	if (likely(!vbase.IsConst())) {
		mem = asmjit::x86::ptr(pmembase, make_gpr(vbase));
	} else {
		mem = asmjit::x86::ptr(pmembase, vbase.GetConst());
	}

	assert(vrd.GetType() == qir::VType::I32);
	switch (ins->sz) {
	case qir::VType::I8:
		mem.setSize(1);
		if (sgn == qir::VSign::U) {
			j.movzx(prd, mem);
		} else {
			j.movsx(prd, mem);
		}
		break;
	case qir::VType::I16:
		mem.setSize(2);
		if (sgn == qir::VSign::U) {
			j.movzx(prd, mem);
		} else {
			j.movsx(prd, mem);
		}
		break;
	case qir::VType::I32:
		mem.setSize(4);
		j.mov(prd, mem);
		break;
	default:
		unreachable("");
	};
}

void QEmit::Emit_vmstore(qir::InstVMStore *ins)
{
	auto &vbase = ins->i[0];
	auto &vdata = ins->i[1];

	auto pdata = make_operand(vdata);
	auto pmembase = asmjit::x86::gpq(MEMBASE);

	asmjit::x86::Mem mem;
	if (likely(!vbase.IsConst())) {
		mem = asmjit::x86::ptr(pmembase, make_gpr(vbase));
	} else {
		mem = asmjit::x86::ptr(pmembase, vbase.GetConst());
	}

	assert(ins->sgn == qir::VSign::U);
	mem.setSize(VTypeToSize(ins->sz));
	j.emit(asmjit::x86::Inst::kIdMov, pdata, mem);
}

void QEmit::Emit_setcc(qir::InstSetcc *ins)
{
	auto prd = make_gpr(ins->o[0]);

	// constfolded
	auto vs1 = &ins->i[0];
	auto vs2 = &ins->i[1];
	auto cc = ins->cc;
	if (vs1->IsConst()) {
		std::swap(vs1, vs2);
		cc = qir::InverseCC(cc);
	}
	j.emit(asmjit::x86::Inst::kIdCmp, make_operand(*vs1), make_operand(*vs2));
	auto setcc = asmjit::x86::Inst::setccFromCond(make_cc(cc));
	j.emit(setcc, prd.r8());
	j.movzx(prd, prd.r8()); // TODO: xor if no alias
}

void QEmit::Emit_mov(qir::InstUnop *ins)
{
	j.emit(asmjit::x86::Inst::kIdMov, make_gpr(ins->o[0]), make_operand(ins->i[0]));
}

template <asmjit::x86::Inst::Id Op>
ALWAYS_INLINE void QEmit::EmitInstBinopCommutative(qir::InstBinop *ins)
{
	// canonicalize
	if (ins->i[0].IsConst()) {
		std::swap(ins->i[0], ins->i[1]);
	}

	// constfolded
	auto &vrd = ins->o[0];
	auto vs1 = ins->i[0];
	auto vs2 = ins->i[1];
	auto prd = make_gpr(vrd);

	// canonicalize // TODO: do first and remove indir
	if (vs1.IsConst()) {
		std::swap(vs1, vs2);
	}
	// rd rx x
	auto prs1 = make_gpr(vs1);

	if (vrd.GetPGPR() == vs1.GetPGPR()) { // rd rd x
		j.emit(Op, prd, make_operand(vs2));
		return;
	}
	// rd r1 x
	if (vs2.IsConst()) { // rd r1 c
		j.emit(asmjit::x86::Inst::kIdMov, prd, prs1);
		j.emit(Op, prd, make_imm(vs2));
		return;
	}
	// rd r1 rx
	auto prs2 = make_gpr(vs2);
	if (vrd.GetPGPR() == vs2.GetPGPR()) { // rd r1 rd
		j.emit(Op, prd, prs1);
		return;
	}
	// rd r1 r2
	j.emit(asmjit::x86::Inst::kIdMov, prd, prs1);
	j.emit(Op, prd, prs2);
}

template <asmjit::x86::Inst::Id Op>
ALWAYS_INLINE void QEmit::EmitInstBinopNonCommutative(qir::InstBinop *ins)
{
	// constfolded
	auto &vrd = ins->o[0];
	auto vs1 = ins->i[0];
	auto vs2 = ins->i[1];
	auto prd = make_gpr(vrd);
	auto ptmp = asmjit::x86::Gp(prd, TMP1C);

	if (vs1.IsConst()) { // rd c rx
		auto cs1 = make_imm(vs1);
		auto prs2 = make_gpr(vs2);
		if (vrd.GetPGPR() == vs2.GetPGPR()) { // rd c rd
			j.mov(ptmp, cs1);
			j.emit(Op, ptmp, prd);
			j.mov(prd, ptmp);
			return;
		}
		j.mov(prd, cs1);
		j.emit(Op, prd, prs2);
		return;
	}
	// rd rx x
	auto prs1 = make_gpr(vs1);

	// rd rx x
	if (vrd.GetPGPR() == vs1.GetPGPR()) { // rd rd x
		j.emit(Op, prd, make_operand(vs2));
		return;
	}
	// rd r1 x
	if (vs2.IsConst()) { // rd r1 c
		j.emit(asmjit::x86::Inst::kIdMov, prd, prs1);
		j.emit(Op, prd, make_imm(vs2));
		return;
	}
	// rd r1 rx
	auto prs2 = make_gpr(vs2);
	if (vrd.GetPGPR() == vs2.GetPGPR()) { // rd r1 rd
		j.mov(ptmp, prs1);	      // TODO: liveness: may kill if r1 is dead
		j.emit(Op, ptmp, prd);
		j.mov(prd, ptmp);
		return;
	}
	// rd r1 r2
	j.emit(asmjit::x86::Inst::kIdMov, prd, prs1);
	j.emit(Op, prd, prs2);
}

void QEmit::Emit_add(qir::InstBinop *ins)
{
	EmitInstBinopCommutative<asmjit::x86::Inst::kIdAdd>(ins);
}

void QEmit::Emit_sub(qir::InstBinop *ins)
{
	EmitInstBinopNonCommutative<asmjit::x86::Inst::kIdSub>(ins);
}

void QEmit::Emit_and(qir::InstBinop *ins)
{
	EmitInstBinopCommutative<asmjit::x86::Inst::kIdAnd>(ins);
}

void QEmit::Emit_or(qir::InstBinop *ins)
{
	EmitInstBinopCommutative<asmjit::x86::Inst::kIdOr>(ins);
}

void QEmit::Emit_xor(qir::InstBinop *ins)
{
	EmitInstBinopCommutative<asmjit::x86::Inst::kIdXor>(ins);
}

void QEmit::Emit_sra(qir::InstBinop *ins)
{
	EmitInstBinopNonCommutative<asmjit::x86::Inst::kIdShr>(ins);
}

void QEmit::Emit_srl(qir::InstBinop *ins)
{
	EmitInstBinopNonCommutative<asmjit::x86::Inst::kIdSar>(ins);
}

void QEmit::Emit_sll(qir::InstBinop *ins)
{
	EmitInstBinopNonCommutative<asmjit::x86::Inst::kIdShl>(ins);
}

} // namespace dbt::qcg
