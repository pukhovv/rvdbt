#include "dbt/qjit/qcg.h"

namespace dbt::qcg
{

TBlock *QCodegen::Generate(qir::Region *r)
{
	auto vregs_info = r->GetVRegsInfo();
	QEmit ce(r);
	QRegAlloc ra(&ce, vregs_info);
	QCodegen cg(r, &ce, &ra);

	log_qcg("Generate");
	ra.Prologue();
	ce.Prologue();

	cg.Translate();

	log_qcg("Emit code");
	auto tb = ce.EmitTBlock();
	ce.DumpTBlock(tb);

	return tb; // manually register ip
}

struct QCodegenVisitor : qir::InstVisitor<QCodegenVisitor, void> {

	using Base = qir::InstVisitor<QCodegenVisitor, void>;

#if 0
	template <size_t N_OUT, size_t N_IN>
	void AllocPregs(std::array<qir::VReg, N_OUT> &out, std::array<qir::VOperand, N_OUT> &in)
	{
		std::array<qir::VReg *, N_OUT> out_regs;
		std::array<qir::VReg *, N_IN> in_regs;
		size_t n_in_regs = 0;

		for (size_t idx = 0; idx < out.size(); ++idx) {
			out_regs = &out[idx];
		}

		for (qir::VOperand &o : in) {
			if (!o.IsConst()) {
				in_regs[n_in_regs++] = &o.ToReg();
			}
		}

		
	}
#endif

public:
	QCodegenVisitor(QCodegen *cg_) : cg(cg_) {}

	void visitInst(qir::Inst *ins)
	{
		unreachable("");
	}

	void visitInstBinop(qir::InstBinop *ins) {}

private:
	[[maybe_unused]] QCodegen *cg;
};

void QCodegen::Translate()
{
	QCodegenVisitor vis(this);

	for (auto &bb : region->blist) {
		ce->SetBlock(&bb);
		auto &ilist = bb.ilist;
		for (auto iit = ilist.begin(); iit != ilist.end(); ++iit) {
			vis.visit(&*iit);
		}
	}
}

} // namespace dbt::qcg
