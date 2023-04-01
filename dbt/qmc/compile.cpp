#include "dbt/qmc/compile.h"
#include "dbt/guest/rv32_qir.h"
#include "dbt/qmc/qcg/qcg.h"
#include "dbt/qmc/qir_printer.h"

namespace dbt::qir
{

// TODO: reuse code
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

// TODO: ArenaObjects
static qir::Region *AllocRegion(MemArena *arena)
{
	auto *mem = arena->Allocate<Region>();
	assert(mem);
	return new (mem) qir::Region(arena, IRTranslator::state_info);
}

qir::Region *CompilerGenRegionIR(MemArena *arena, CompilerJob &job)
{
	auto *region = AllocRegion(arena);

	auto &iprange = job.iprange;
	auto &cruntime = job.cruntime;

	IRTranslator::Translate(region, &iprange, cruntime->GetVMemBase());
	PrinterPass::run(log_qir, "Initial IR after IRTranslator", region);

	return region;
}

} // namespace dbt::qir
