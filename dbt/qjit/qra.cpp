#include "dbt/qjit/qcg.h"

namespace dbt::qcg
{

QRegAlloc::QRegAlloc(QEmit *qe_, qir::VRegsInfo const *vregs_info_) : vregs_info(vregs_info_), qe(qe_)
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
	if (v->is_global) {
		qe->StateSpill(v->p, v->type, v->spill_offs);
	} else {
		if (v->spill_offs == RTrack::NO_SPILL) {
			AllocFrameSlot(v);
		}
		qe->LocSpill(v->p, v->type, v->spill_offs);
	}
}

void QRegAlloc::EmitFill(RTrack *v)
{
	if (v->is_global) {
		qe->StateFill(v->p, v->type, v->spill_offs);
	} else {
		assert(v->spill_offs != RTrack::NO_SPILL);
		qe->LocFill(v->p, v->type, v->spill_offs);
	}
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
		v->loc = kill ? RTrack::Location::DEAD : RTrack::Location::MEM;
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
	if (frame_cur + slot_sz > frame_size) {
		Panic();
	}
	v->spill_offs = frame_cur;
	frame_cur += slot_sz;
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
	case RTrack::Location::REG:
		return;
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
	for (int i = 0; i < n_vregs; ++i) {
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
	for (int i = 0; i < n_vregs; ++i) {
		Spill(&vregs[i]); // skip if fixed
	}
}

void QRegAlloc::AllocOp(RTrack **dstl, u8 dst_n, RTrack **srcl, u8 src_n, bool unsafe)
{
	auto avoid = fixed;

	for (u8 i = 0; i < src_n; ++i) {
		if (srcl[i]) {
			Fill(srcl[i], PREGS_POOL, avoid);
			avoid.Set(srcl[i]->p);
		}
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
		auto *dst = dstl[i];
		if (dst->loc != RTrack::Location::REG) {
			dst->p = AllocPReg(PREGS_POOL, avoid);
			p2v[dst->p] = dst;
			dst->loc = RTrack::Location::REG;
		}
		avoid.Set(dst->p);
		dst->spill_synced = false;
	}
}

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

} // namespace dbt::qcg