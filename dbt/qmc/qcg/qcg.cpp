#include "dbt/qmc/qcg/qcg.h"
#include "dbt/qmc/qcg/qemit.h"
#include "dbt/qmc/qir_printer.h"

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

std::span<u8> GenerateCode(CompilerRuntime *cruntime, qir::CodeSegment *segment, qir::Region *r, u32 ip)
{
	ArchTraits::init();
	MachineRegionInfo mregion_info;

	QSelPass::run(r, &mregion_info);
	qir::PrinterPass::run(log_qcg, "IR dump after QSelPass", r);

	QRegAllocPass::run(r);
	qir::PrinterPass::run(log_qcg, "IR dump after QRegAllocPass", r);

	log_qcg("Emit machine instructions: reloc=%u is_leaf=%u", !cruntime->AllowsRelocation(),
		!mregion_info.has_calls);
	QEmit ce(r, cruntime, segment, !mregion_info.has_calls);
	QCodegen cg(r, &ce);
	cg.Run(ip);

	auto code = ce.EmitCode();
	QEmit::DumpCode(code);
	return code;
}

struct QCodegenVisitor : qir::InstVisitor<QCodegenVisitor, void> {
public:
	QCodegenVisitor(QCodegen *cg_) : cg(cg_) {}

	void visitInst(qir::Inst *ins)
	{
		unreachable("");
	}

#define OP(name, cls, flags)                                                                                 \
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
