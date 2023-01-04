#include "dbt/qjit/qcg.h"
#include "dbt/qjit/qir_printer.h"

namespace dbt::qcg
{

TBlock *QCodegen::Generate(qir::Region *r, u32 ip)
{
	log_qcg("Allocate regs");
	QRegAllocPass::run(r);
	qir::PrinterPass::run(r);

	log_qcg("Generate");
	QEmit ce(r);
	QCodegen cg(r, &ce);
	cg.Run(ip);

	log_qcg("Emit code");
	auto tb = ce.EmitTBlock();
	tb->ip = ip;
	ce.DumpTBlock(tb);

	return tb;
}

struct QCodegenVisitor : qir::InstVisitor<QCodegenVisitor, void> {
public:
	QCodegenVisitor(QCodegen *cg_) : cg(cg_) {}

	void visitInst(qir::Inst *ins)
	{
		unreachable("");
	}

#define OP(name, cls)                                                                                        \
	void visit_##name(qir::cls *ins)                                                                     \
	{                                                                                                    \
		cg->ce->Emit_##name(ins);                                                                    \
	}
	QIR_OPS_LIST(OP)
#undef OP

	void visitInstBinop(qir::InstBinop *ins) {}

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
