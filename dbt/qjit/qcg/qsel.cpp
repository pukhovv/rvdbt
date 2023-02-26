#include "dbt/qjit/qcg/arch_traits.h"
#include "dbt/qjit/qcg/qcg.h"

namespace dbt::qcg
{

struct QSel {
	QSel(qir::Region *region_) : region(region_) {}

	void Run();

	template <typename DstA, typename SrcA>
	void SelectOperands(qir::Inst *ins, DstA &&dst, SrcA &&src)
	{
		SelectOperands(ins, dst.data(), dst.size(), src.data(), src.size());
	}
	void SelectOperands(qir::Inst *ins, qir::VOperand *dstl, u8 dst_n, qir::VOperand *srcl, u8 src_n);

	qir::Region *region{};
	qir::Builder qb{nullptr};
};

void QSel::SelectOperands(qir::Inst *ins, qir::VOperand *dstl, u8 dst_n, qir::VOperand *srcl, u8 src_n)
{
	auto *op_ct = GetOpInfo(ins->GetOpcode()).ra_ct;
	assert(op_ct);
	// satisfy aliases
	for (u8 i = 0; i < src_n; ++i) {
		auto &ct = op_ct[dst_n + i];
		if (!ct.has_alias) {
			continue;
		}
		auto *src = &srcl[i];
		auto *dst = &dstl[ct.alias];
		assert(dst->IsVGPR());
		if (src->IsVGPR() && src->GetVGPR() == dst->GetVGPR()) {
			continue;
		}

		bool live_input = false; // TODO: liveness
		for (u8 k = 0; k < src_n; ++k) {
			auto *src2 = &srcl[k];
			if (k != i && src2->IsVGPR() && src2->GetVGPR() == dst->GetVGPR()) {
				live_input = true;
				break;
			}
		}
		if (live_input) {
			auto tmp = qir::VOperand::MakeVGPR(dst->GetType(), qb.CreateVGPR(dst->GetType()));
			qb.Create_mov(tmp, *dst);
			for (u8 k = 0; k < src_n; ++k) {
				auto *src2 = &srcl[k];
				if (k != i && src2->IsVGPR() && src2->GetVGPR() == dst->GetVGPR()) {
					*src2 = tmp;
				}
			}
		}
		qb.Create_mov(*dst, *src);
		*src = *dst;
	}
	// lower constants
	for (u8 i = 0; i < src_n; ++i) {
		auto &ct = op_ct[dst_n + i];
		auto *src = &srcl[i];
		if (!src->IsConst()) {
			continue;
		}
		auto type = src->GetType();
		auto val = src->GetConst();
		if (ct.ci != RACtImm::NO && match_gp_const(type, val, ct.ci)) {
			continue;
		}
		auto tmp = qir::VOperand::MakeVGPR(type, qb.CreateVGPR(type));
		qb.Create_mov(tmp, *src);
		*src = tmp;
	}
}

struct QSelVisitor : qir::InstVisitor<QSelVisitor, void> {
	using Base = qir::InstVisitor<QSelVisitor, void>;

	template <size_t N_OUT, size_t N_IN>
	void SelectOperands(qir::InstWithOperands<N_OUT, N_IN> *ins)
	{
		sel->SelectOperands(ins, ins->o, ins->i);
	}

public:
	QSelVisitor(QSel *sel_) : sel(sel_) {}

	void visitInst(qir::Inst *ins)
	{
		unreachable("");
	}

	void visitInstUnop(qir::InstUnop *ins)
	{
		SelectOperands(ins);
	}

	void visitInstBinop(qir::InstBinop *ins)
	{
		SelectOperands(ins);
	}

	void visitInstSetcc(qir::InstSetcc *ins)
	{
		SelectOperands(ins);
	}

	void visitInstBr(qir::InstBr *ins) {}

	void visitInstBrcc(qir::InstBrcc *ins)
	{
		SelectOperands(ins);
	}

	void visitInstGBr(qir::InstGBr *ins) {}

	void visitInstGBrind(qir::InstGBrind *ins)
	{
		SelectOperands(ins);
	}

	void visitInstVMLoad(qir::InstVMLoad *ins)
	{
		SelectOperands(ins);
	}

	void visitInstVMStore(qir::InstVMStore *ins)
	{
		SelectOperands(ins);
	}

	void visitInstHcall(qir::InstHcall *ins) {}

	void visit_sll(qir::InstBinop *ins)
	{
		SelectOperands(ins);
	}

	void visit_srl(qir::InstBinop *ins)
	{
		SelectOperands(ins);
	}

	void visit_sra(qir::InstBinop *ins)
	{
		SelectOperands(ins);
	}

private:
	QSel *sel{};
};

void QSel::Run()
{
	auto &blist = region->blist;
	for (auto &bb : blist) {
		auto &ilist = bb.ilist;

		for (auto iit = ilist.begin(); iit != ilist.end(); ++iit) {
			qb = qir::Builder(&bb, iit);
			QSelVisitor(this).visit(&*iit);
		}
	}
}

void QSelPass::run(qir::Region *region)
{
	QSel sel(region);
	sel.Run();
}

} // namespace dbt::qcg
