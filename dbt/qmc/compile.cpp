#include "dbt/qmc/compile.h"
#include "dbt/guest/rv32_qir.h"
#include "dbt/qmc/qcg/qcg.h"
#include "dbt/qmc/qir_printer.h"

namespace dbt::qir
{

void *CompilerDoJob(CompilerJob &job)
{
	MemArena arena(64_KB);
	qir::Region region(&arena, IRTranslator::state_info);

	auto &iprange = job.iprange;
	auto &cruntime = job.cruntime;
	auto &segment = job.segment;

	cruntime->UpdateIPBoundary(iprange);

	IRTranslator::Translate(&region, iprange.first, iprange.second, cruntime->GetVMemBase());
	PrinterPass::run(log_qir, "Initial IR after IRTranslator", &region);

	auto tcode = qcg::GenerateCode(cruntime, &segment, &region, iprange.first);

	return cruntime->AnnounceRegion(iprange.first, tcode);
}

} // namespace dbt::qir
