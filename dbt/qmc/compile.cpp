#include "dbt/qmc/compile.h"
#include "dbt/guest/rv32_qir.h"
#include "dbt/qmc/qcg/qcg.h"
#include "dbt/qmc/qir_printer.h"

namespace dbt::qir
{

void *CompilerDoJob(CompilerJob &job)
{
	MemArena arena(1_MB);
	qir::Region region(&arena, IRTranslator::state_info);

	auto &iprange = job.iprange;
	auto &cruntime = job.cruntime;
	auto &segment = job.segment;
	auto entry_ip = iprange[0].first;

	IRTranslator::Translate(&region, &iprange, cruntime->GetVMemBase());
	PrinterPass::run(log_qir, "Initial IR after IRTranslator", &region);

	auto tcode = qcg::GenerateCode(cruntime, &segment, &region, entry_ip);

	return cruntime->AnnounceRegion(entry_ip, tcode);
}

} // namespace dbt::qir
