#include "dbt/qjit/qcg.h"

namespace dbt::qcg
{

QRegAlloc::QRegAlloc(qir::Region *region_) : region(region_), vregs_info(region->GetVRegsInfo())
{
	auto n_globals = vregs_info->NumGlobals();
	auto n_all = vregs_info->NumAll();

	for (u16 i = 0; i < n_globals; ++i) {
		auto *gr = vregs_info->GetGlobalInfo(i);
		AddTrackGlobal(gr->type, gr->state_offs);
	}

	for (u16 i = n_globals; i < n_all; ++i) {
		auto type = vregs_info->GetLocalType(i);
		AddTrackLocal(type);
	}
}

qir::RegN QRegAlloc::AllocPReg(RegMask desire, RegMask avoid)
{
	RegMask target = desire & ~avoid;
	for (qir::RegN p = 0; p < N_PREGS; ++p) {
		if (!p2v[p] && target.Test(p)) {
			return p;
		}
	}

	for (qir::RegN p = 0; p < N_PREGS; ++p) {
		if (target.Test(p)) {
			Spill(p);
			return p;
		}
	}
	Panic();
}

void QRegAlloc::EmitSpill(RTrack *v)
{
	if (!v->is_global && (v->spill_offs == RTrack::NO_SPILL)) {
		AllocFrameSlot(v);
	}
	auto pgpr = qir::VOperand::MakePGPR(v->type, v->p);
	qb.Create_mov(qir::VOperand::MakeSlot(v->is_global, v->type, v->spill_offs), pgpr);
}

void QRegAlloc::EmitFill(RTrack *v)
{
	assert(v->spill_offs != RTrack::NO_SPILL);
	auto pgpr = qir::VOperand::MakePGPR(v->type, v->p);
	qb.Create_mov(pgpr, qir::VOperand::MakeSlot(v->is_global, v->type, v->spill_offs));
}

void QRegAlloc::EmitMov(qir::VOperand dst, qir::VOperand src)
{
	qb.Create_mov(dst, src);
}

void QRegAlloc::Spill(qir::RegN p)
{
	RTrack *v = p2v[p];
	if (!v) {
		return;
	}
	Spill(v);
}

void QRegAlloc::Spill(RTrack *v)
{
	SyncSpill(v);
	Release<false>(v);
}

void QRegAlloc::SyncSpill(RTrack *v)
{
	if (v->spill_synced) { // or fixed
		return;
	}
	switch (v->loc) {
	case RTrack::Location::MEM:
		return;
	case RTrack::Location::REG:
		EmitSpill(v);
		break;
	default:
		Panic();
	}
	v->spill_synced = true;
}

template <bool kill>
void QRegAlloc::Release(RTrack *v)
{
	bool release_reg = (v->loc == RTrack::Location::REG);
	if (v->is_global) { // return if fixed
		v->loc = RTrack::Location::MEM;
	} else {
		v->loc = kill ? RTrack::Location::DEAD : RTrack::Location::MEM; // TODO: liveness
	}
	if (release_reg) {
		p2v[v->p] = nullptr;
	}
}

void QRegAlloc::AllocFrameSlot(RTrack *v)
{
	assert(v->spill_offs == RTrack::NO_SPILL);
	assert(!v->is_global);

	u16 slot_sz = qir::VTypeToSize(v->type);
	u16 slot_offs = roundup(frame_cur, slot_sz);
	if (slot_offs + slot_sz > frame_size) {
		Panic();
	}
	v->spill_offs = slot_offs;
	frame_cur = slot_offs + slot_sz;
}

void QRegAlloc::Fill(RTrack *v, RegMask desire, RegMask avoid)
{
	switch (v->loc) {
	case RTrack::Location::MEM:
		v->p = AllocPReg(desire, avoid);
		v->loc = RTrack::Location::REG;
		p2v[v->p] = v;
		v->spill_synced = true;
		EmitFill(v);
		return;
	case RTrack::Location::REG: {
		if (likely(!avoid.Test(v->p))) {
			return;
		}
		// special handling for constrained alloc
		auto opr_old = qir::VOperand::MakePGPR(v->type, v->p);
		p2v[v->p] = nullptr;
		v->p = AllocPReg(desire, avoid);
		p2v[v->p] = v;
		auto opr_new = qir::VOperand::MakePGPR(v->type, v->p);
		EmitMov(opr_new, opr_old);
		return;
	}
	default:
		Panic();
	}
}

// internal
QRegAlloc::RTrack *QRegAlloc::AddTrack()
{
	if (n_vregs == vregs.size()) {
		Panic();
	}
	auto *v = &vregs[n_vregs++];
	return new (v) RTrack();
}

QRegAlloc::RTrack *QRegAlloc::AddTrackGlobal(qir::VType type, u16 state_offs)
{
	auto *v = AddTrack();
	v->is_global = true;
	v->type = type;
	v->spill_offs = state_offs;
	return v;
}

QRegAlloc::RTrack *QRegAlloc::AddTrackLocal(qir::VType type)
{
	auto *v = AddTrack();
	v->is_global = false;
	v->type = type;
	v->spill_offs = RTrack::NO_SPILL;
	return v;
}

void QRegAlloc::Prologue()
{
	for (qir::RegN i = 0; i < n_vregs; ++i) {
		auto *v = &vregs[i];

		if (v->is_global) {
			v->loc = RTrack::Location::MEM;
		} else {
			v->loc = RTrack::Location::DEAD;
		}
	}
}

void QRegAlloc::BlockBoundary()
{
	for (qir::RegN i = 0; i < n_vregs; ++i) {
		Spill(&vregs[i]); // skip if fixed
	}
}

void QRegAlloc::RegionBoundary()
{
	for (qir::RegN i = 0; i < n_vregs; ++i) {
		auto vreg = &vregs[i];
		if (vreg->is_global) {
			Spill(vreg);
		} else {
			Release<false>(vreg);
		}
	}
}

void QRegAlloc::AllocOp(qir::VOperand *dstl, u8 dst_n, qir::VOperand *srcl, u8 src_n, bool unsafe)
{
	auto avoid = fixed;

	for (u8 i = 0; i < src_n; ++i) {
		auto opr = &srcl[i];
		if (!opr->IsVGPR()) {
			continue;
		}
		auto src = &vregs[opr->GetVGPR()];
		Fill(src, PREGS_POOL, avoid);
		avoid.Set(src->p); // TODO: liveness: do not add if last use
		*opr = qir::VOperand::MakePGPR(opr->GetType(), src->p);
	}

	if (unsafe) {
		for (int i = 0; i < n_vregs; ++i) {
			auto *v = &vregs[i];
			if (v->is_global) {
				SyncSpill(v);
			}
		}
	}

	for (u8 i = 0; i < dst_n; ++i) {
		auto opr = &dstl[i];
		if (!opr->IsVGPR()) {
			continue;
		}
		auto dst = &vregs[opr->GetVGPR()];
		if (dst->loc != RTrack::Location::REG) {
			dst->p = AllocPReg(PREGS_POOL, avoid);
			p2v[dst->p] = dst;
			dst->loc = RTrack::Location::REG;
		}
		avoid.Set(dst->p);
		dst->spill_synced = false;
		*opr = qir::VOperand::MakePGPR(opr->GetType(), dst->p);
	}
}

void QRegAlloc::AllocOpConstrained(qir::VOperand *dstl, u8 dst_n, qir::VOperand *srcl, u8 src_n,
				   RegMask require_set, qir::RegN *require, bool unsafe)
{
	auto avoid = fixed | require_set;
	u8 opr_idx;

	// TODO: SpillOrReassign constrained pregs in prologue

	auto get_avoid = [&] {
		RegMask opr_avoid = avoid;
		if (qir::RegN rreg = require[opr_idx]; rreg != qir::RegNBad) {
			opr_avoid = ~RegMask(0);
			opr_avoid.Clear(rreg);
		}
		return opr_avoid;
	};

	opr_idx = dst_n;
	for (u8 i = 0; i < src_n; ++i, ++opr_idx) {
		auto opr = &srcl[i];
		if (!opr->IsVGPR()) {
			continue;
		}
		auto src = &vregs[opr->GetVGPR()];
		auto it_avoid = get_avoid();
		Fill(src, ~it_avoid, it_avoid);
		avoid.Set(src->p); // TODO: liveness: do not add if last use
		*opr = qir::VOperand::MakePGPR(opr->GetType(), src->p);
	}

	if (unsafe) {
		for (int i = 0; i < n_vregs; ++i) {
			auto *v = &vregs[i];
			if (v->is_global) {
				SyncSpill(v);
			}
		}
	}

	opr_idx = 0;
	for (u8 i = 0; i < dst_n; ++i, ++opr_idx) {
		auto opr = &dstl[i];
		if (!opr->IsVGPR()) {
			continue;
		}
		auto dst = &vregs[opr->GetVGPR()];
		auto it_avoid = get_avoid();
		if (dst->loc != RTrack::Location::REG) {
			dst->p = AllocPReg(~it_avoid, it_avoid);
			p2v[dst->p] = dst;
			dst->loc = RTrack::Location::REG;
		} else if (it_avoid.Test(dst->p)) {
			auto opr_old = qir::VOperand::MakePGPR(dst->type, dst->p);
			p2v[dst->p] = nullptr;
			dst->p = AllocPReg(~it_avoid, it_avoid);
			p2v[dst->p] = dst;
			auto opr_new = qir::VOperand::MakePGPR(dst->type, dst->p);
			EmitMov(opr_new, opr_old);
		}
		avoid.Set(dst->p);
		dst->spill_synced = false;
		*opr = qir::VOperand::MakePGPR(opr->GetType(), dst->p);
	}
}

// TODO: resurrect allocation for helpers
void QRegAlloc::CallOp(bool use_globals)
{
	for (u8 p = 0; p < N_PREGS; ++p) {
		if (qjit::PREGS_CALL_CLOBBER.Test(p)) {
			Spill(p);
		}
	}

	if (use_globals) {
		for (u8 i = 0; i < n_vregs; ++i) {
			auto *v = &vregs[i];
			if (v->is_global) {
				Spill(v);
			}
		}
	}
}

struct QRegAllocVisitor : qir::InstVisitor<QRegAllocVisitor, void> {
	using Base = qir::InstVisitor<QRegAllocVisitor, void>;

	template <size_t N_OUT, size_t N_IN>
	void AllocOperands(qir::InstWithOperands<N_OUT, N_IN> *ins)
	{
		bool sideeff = ins->GetFlags() & qir::Inst::Flags::SIDEEFF;
		ra->AllocOp(ins->o, ins->i, sideeff);
	}

	template <size_t N_OUT, size_t N_IN>
	void AllocOperandsConstrained(qir::InstWithOperands<N_OUT, N_IN> *ins, RegMask require_set,
				      std::array<qir::RegN, N_OUT + N_IN> require)
	{
		bool sideeff = ins->GetFlags() & qir::Inst::Flags::SIDEEFF;
		ra->AllocOpConstrained(ins->o.data(), ins->o.size(), ins->i.data(), ins->i.size(),
				       require_set, require.data(), sideeff);
	}

public:
	QRegAllocVisitor(QRegAlloc *ra_) : ra(ra_) {}

	void visitInst(qir::Inst *ins)
	{
		unreachable("");
	}

	void visitInstUnop(qir::InstUnop *ins)
	{
		AllocOperands(ins);
	}

	void visitInstBinop(qir::InstBinop *ins)
	{
		AllocOperands(ins);
	}

	void visitInstSetcc(qir::InstSetcc *ins)
	{
		AllocOperands(ins);
	}

	void visitInstBr(qir::InstBr *ins)
	{
		// has no voperands
		ra->BlockBoundary();
	}

	void visitInstBrcc(qir::InstBrcc *ins)
	{
		AllocOperands(ins);
		ra->BlockBoundary();
	}

	void visitInstGBr(qir::InstGBr *ins)
	{
		// has no voperands
		ra->RegionBoundary();
	}

	void visitInstGBrind(qir::InstGBrind *ins)
	{
		AllocOperands(ins);
		ra->RegionBoundary(); // merge into AllocOp
	}

	void visitInstVMLoad(qir::InstVMLoad *ins)
	{
		AllocOperands(ins);
	}

	void visitInstVMStore(qir::InstVMStore *ins)
	{
		AllocOperands(ins);
	}

	void visitInstHcall(qir::InstHcall *ins)
	{
		ra->CallOp(true);
	}

	void visit_amd64_shifts(qir::InstBinop *ins)
	{
		// amd64
		static constexpr RegMask require_set(asmjit::x86::Gp::kIdCx);
		static const std::array<qir::RegN, 3> require = {qir::RegNBad, qir::RegNBad,
								 asmjit::x86::Gp::kIdCx};
		AllocOperandsConstrained(ins, require_set, require);
	}

	void visit_sll(qir::InstBinop *ins)
	{
		visit_amd64_shifts(ins);
	}

	void visit_srl(qir::InstBinop *ins)
	{
		visit_amd64_shifts(ins);
	}

	void visit_sra(qir::InstBinop *ins)
	{
		visit_amd64_shifts(ins);
	}

private:
	QRegAlloc *ra{};
};

void QRegAlloc::Run()
{
	Prologue();

	auto &blist = region->blist;
	for (auto &bb : blist) {
		auto &ilist = bb.ilist;

		for (auto iit = ilist.begin(); iit != ilist.end(); ++iit) {
			qb = qir::Builder(&bb, iit);
			QRegAllocVisitor(this).visit(&*iit);
		}
	}
}

void QRegAllocPass::run(qir::Region *region)
{
	QRegAlloc ra(region);
	ra.Run();
}

} // namespace dbt::qcg