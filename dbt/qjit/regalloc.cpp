#include "dbt/qjit/qjit.h"

namespace dbt::qjit
{

void RegAlloc::SetupCtx(QuickJIT *ctx_)
{
	ctx = ctx_;
}

RegAlloc::PReg RegAlloc::AllocPReg(Mask desire, Mask avoid)
{
	Mask target = desire & ~avoid;
	for (PReg p = 0; p < N_PREGS; ++p) {
		if (!p2v[p] && target.Test(p)) {
			return p;
		}
	}

	for (PReg p = 0; p < N_PREGS; ++p) {
		if (target.Test(p)) {
			Spill(p);
			return p;
		}
	}
	Panic();
}

void RegAlloc::Spill(PReg p)
{
	VReg *v = p2v[p];
	if (!v) {
		return;
	}
	Spill(v);
}

void RegAlloc::Spill(VReg *v)
{
	SyncSpill(v);
	Release<false>(v);
}

void RegAlloc::SyncSpill(VReg *v)
{
	if (v->spill_synced || v->scope == VReg::Scope::FIXED) {
		return;
	}
	if (!v->spill_base) {
		AllocFrameSlot(v);
	}
	switch (v->loc) {
	case VReg::Loc::MEM:
		return;
	case VReg::Loc::REG:
		ctx->cg->Spill(v);
		break;
	default:
		Panic();
	}
	v->spill_synced = true;
}

template <bool kill>
void RegAlloc::Release(VReg *v)
{
	bool release_reg = (v->loc == VReg::Loc::REG);
	switch (v->scope) {
	case VReg::Scope::LOC:
		v->loc = kill ? VReg::Loc::DEAD : VReg::Loc::MEM;
		break;
	case VReg::Scope::GLOBAL:
		v->loc = VReg::Loc::MEM;
		break;
	case VReg::Scope::FIXED:
		return;
	default:
		Panic();
	}
	if (release_reg) {
		p2v[v->p] = nullptr;
	}
}

void RegAlloc::AllocFrameSlot(VReg *v)
{
	assert(!v->spill_base);

	u16 slot_sz = TypeToSize(v->type);
	if (frame_cur + slot_sz > frame_size) {
		Panic();
	}
	v->spill_base = frame_base;
	v->spill_offs = frame_cur;
	frame_cur += slot_sz;
}

void RegAlloc::Fill(VReg *v, Mask desire, Mask avoid)
{
	switch (v->loc) {
	case VReg::Loc::MEM:
		v->p = AllocPReg(desire, avoid);
		v->loc = VReg::Loc::REG;
		p2v[v->p] = v;
		ctx->cg->Fill(v);
		v->spill_synced = true;
		return;
	case VReg::Loc::REG:
		return;
	default:
		Panic();
	}
}

// internal
RegAlloc::VReg *RegAlloc::AllocVReg()
{
	if (num_vregs == vregs.size()) {
		Panic();
	}
	auto *v = &vregs[num_vregs++];
	return new (v) VReg();
}

// internal
RegAlloc::VReg *RegAlloc::AllocVRegGlob()
{
	auto *v = AllocVReg();
	return v;
}

RegAlloc::VReg *RegAlloc::AllocVRegFixed(VReg::Type type, PReg p)
{
	auto *v = AllocVRegGlob();
	v->p = p;
	p2v[p] = v;
	assert(!fixed.Test(p));
	fixed.Set(p);
	v->scope = VReg::Scope::FIXED;
	v->type = type;
	return v;
}

RegAlloc::VReg *RegAlloc::AllocVRegMem(VReg::Type type, VReg *base, u16 offs)
{
	assert(base->scope == VReg::Scope::FIXED);
	auto *v = AllocVRegGlob();
	v->scope = VReg::Scope::GLOBAL;
	v->type = type;
	v->spill_base = base;
	v->spill_offs = offs;
	return v;
}

void RegAlloc::Prologue()
{
	for (int i = 0; i < num_vregs; ++i) {
		auto *v = &vregs[i];

		switch (v->scope) {
		case VReg::Scope::GLOBAL:
			v->loc = VReg::Loc::MEM;
			break;
		case VReg::Scope::FIXED:
			v->loc = VReg::Loc::REG;
			break;
		default:
			Panic();
		}
	}
}

void RegAlloc::BlockBoundary()
{
	for (int i = 0; i < num_vregs; ++i) {
		auto *v = &vregs[i];

		switch (v->scope) {
		case VReg::Scope::LOC:
			Spill(v->p);
			break;
		case VReg::Scope::GLOBAL:
			Spill(v->p);
			break;
		case VReg::Scope::FIXED:
			break;
		default:
			Panic();
		}
	}
}

void RegAlloc::AllocOp(VReg **dstl, u8 dst_n, VReg **srcl, u8 src_n, bool unsafe)
{
	auto avoid = fixed;

	for (u8 i = 0; i < src_n; ++i) {
		if (srcl[i]) {
			Fill(srcl[i], PREGS_ALL, avoid);
			avoid.Set(srcl[i]->p);
		}
	}

	if (unsafe) {
#ifdef CONFIG_USE_STATEMAPS
		ctx->CreateStateMap();
#endif
		for (int i = 0; i < num_vregs; ++i) {
			auto *v = &vregs[i];
			if (v && !v->has_statemap) {
				SyncSpill(v);
			}
		}
	}

	for (u8 i = 0; i < dst_n; ++i) {
		auto *dst = dstl[i];
		if (!dst) {
			continue;
		}
		if (dst->loc != VReg::Loc::REG) {
			dst->p = AllocPReg(PREGS_ALL, avoid);
			p2v[dst->p] = dst;
			dst->loc = VReg::Loc::REG;
		}
		avoid.Set(dst->p);
		dst->spill_synced = false;
	}
}

void RegAlloc::CallOp(bool use_globals)
{
	for (u8 p = 0; p < qjit::RegAlloc::N_PREGS; ++p) {
		if (qjit::PREGS_CALL_CLOBBER.Test(p)) {
			Spill(p);
		}
	}

	if (use_globals) {
		for (u8 i = 0; i < num_vregs; ++i) {
			auto *v = &vregs[i];
			if (v && v->scope != qjit::RegAlloc::VReg::Scope::FIXED) {
				Spill(v);
			}
		}
	} // elif unsafe -> spills may be avoided with statemap
}

} // namespace dbt::qjit
