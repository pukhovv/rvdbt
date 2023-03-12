#include "dbt/qmc/compile.h"
#include "dbt/guest/rv32_qir.h"
#include "dbt/qmc/qcg/qcg.h"
#include "dbt/qmc/qir_printer.h"

namespace dbt::qir
{

void *CompileAt(CompilerRuntime *cruntime, std::pair<u32, u32> iprange)
{
	MemArena arena(64_KB);
	qir::Region region(&arena, IRTranslator::state_info);

	cruntime->UpdateIPBoundary(iprange);

	IRTranslator::Translate(&region, iprange.first, iprange.second, cruntime->GetVMemBase());
	PrinterPass::run(log_qir, "Initial IR after IRTranslator", &region);

	auto tcode = qcg::GenerateCode(cruntime, &region, iprange.first);

	return cruntime->AnnounceRegion(iprange.first, tcode);
}

} // namespace dbt::qir
