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

public:
	QCodegenVisitor(QCodegen *cg_) : cg(cg_) {}

	void visitInst(qir::Inst *ins)
	{
		unreachable("");
	}

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
