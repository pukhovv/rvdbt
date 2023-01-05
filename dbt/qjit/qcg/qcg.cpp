#include "dbt/qjit/qcg/qcg.h"
#include "dbt/qjit/qcg/qemit.h"
#include "dbt/qjit/qir_printer.h"

namespace dbt::qcg
{

struct QCodegen {
	QCodegen(qir::Region *region_, QEmit *ce_) : region(region_), ce(ce_) {}

	void Run(u32 ip);

private:
	qir::Region *region;
	QEmit *ce;

	friend struct QCodegenVisitor;
};

TBlock::TCode Generate(qir::Region *r, u32 ip)
{
	log_qcg("Allocate regs");
	QRegAllocPass::run(r);
	if (log_qcg.enabled()) {
		auto str = qir::PrinterPass::run(r);
		log_qcg.write(str.c_str());
	}

	log_qcg("Generate code");
	QEmit ce(r);
	QCodegen cg(r, &ce);
	cg.Run(ip);

	log_qcg("Relocate to tcache");
	auto tc = ce.EmitTCode();
	QEmit::DumpTCode(tc);
	return tc;
}

struct QCodegenVisitor : qir::InstVisitor<QCodegenVisitor, void> {
public:
	QCodegenVisitor(QCodegen *cg_) : cg(cg_) {}

	void visitInst(qir::Inst *ins)
	{
		unreachable("");
	}

#define OP(name, cls, flags)                                                                                        \
	void visit_##name(qir::cls *ins)                                                                     \
	{                                                                                                    \
		cg->ce->Emit_##name(ins);                                                                    \
	}
	QIR_OPS_LIST(OP)
#undef OP

private:
	QCodegen *cg{};
};

void QCodegen::Run(u32 ip)
{
	ce->Prologue(ip);
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
